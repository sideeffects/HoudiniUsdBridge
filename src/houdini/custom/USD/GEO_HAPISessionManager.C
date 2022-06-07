/*
 * Copyright 2020 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "GEO_HAPISessionManager.h"
#include <UT/UT_Exit.h>
#include <UT/UT_Map.h>
#include <UT/UT_RecursiveTimedLock.h>
#include <UT/UT_Thread.h>
#include <UT/UT_ThreadQueue.h>
#include <UT/UT_WorkBuffer.h>

#include <thread>

#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#endif

#define MAX_USERS_PER_SESSION 100

// Objects for session management

// Protects myUserCount on each manager and the map containing all current
// managers
static UT_Lock &
hapiSessionsLock()
{
    static UT_Lock theHapiSessionsLock;
    return theHapiSessionsLock;
}

static UT_Map<GEO_HAPISessionID, GEO_HAPISessionManager> &
managersMap()
{
    static UT_Map<GEO_HAPISessionID, GEO_HAPISessionManager> theManagers;
    return theManagers;
}

static UT_Array<GEO_HAPISessionID> &
idsArray()
{
    static UT_Array<GEO_HAPISessionID> theIds;
    return theIds;
}

//
// SessionScopeLock
//

void
GEO_HAPISessionManager::SessionScopeLock::addToUsers()
{
    UT_AutoLock lock(hapiSessionsLock());
    UT_ASSERT(managersMap().contains(myId));
    managersMap()[myId].myUserCount++;
}

void
GEO_HAPISessionManager::SessionScopeLock::removeFromUsers()
{
    unregister(myId);
}

//
// GEO_HAPISessionManager
//

GEO_HAPISessionManager::GEO_HAPISessionManager() : myUserCount(0), mySession{}
{
}

GEO_HAPISessionID
GEO_HAPISessionManager::registerAsUser()
{
    static GEO_HAPISessionID theIdCounter = 0;

    UT_AutoLock autoLock(hapiSessionsLock());

    GEO_HAPISessionID id = -1;

    // Find an available session
    for (exint i = 0; i < idsArray().size(); i++)
    {
        GEO_HAPISessionID tempId = idsArray()(i);
        UT_ASSERT(managersMap().contains(tempId));
        GEO_HAPISessionManager &manager = managersMap()[tempId];
        if (manager.myUserCount < MAX_USERS_PER_SESSION)
        {
            manager.myUserCount++;
            id = tempId;
            break;
        }
    }

    // Create a new session
    if (id < 0)
    {
        GEO_HAPISessionID newId = theIdCounter++;
        UT_ASSERT(!managersMap().contains(newId));

        GEO_HAPISessionManager &manager = managersMap()[newId];

        if (manager.createSession(id))
        {
            manager.myUserCount++;
            idsArray().append(newId);
            id = newId;
        }
        else
        {
            managersMap().erase(newId);
        }
    }

    return id;
}

void
GEO_HAPISessionManager::unregister(GEO_HAPISessionID id)
{
    UT_AutoLock lock(hapiSessionsLock());
    UT_ASSERT(managersMap().contains(id));

    GEO_HAPISessionManager &manager = managersMap()[id];
    manager.myUserCount--;
    if (manager.myUserCount == 0)
    {
        manager.cleanupSession();
        managersMap().erase(id);
        idsArray().findAndRemove(id);
    }
}

// Delayed Unregister functions ------------------------------------------------------------------

static UT_ThreadQueue<GEO_HAPISessionStatusHandle>&
statusQueue()
{
    static UT_ThreadQueue<GEO_HAPISessionStatusHandle> theStatusQueue;
    return theStatusQueue;
}

// Used when the unregisterThread sleeps so that it can be cleanly interrupted
// for shutdown.
// This is initially locked on the main thread to match the unlock() which
// happens from the main thread in the exit callback.
namespace
{
class geoTimerLock
{
public:
    geoTimerLock() { myLock.lock(); }
    UT_RecursiveTimedLock &get() { return myLock; }

private:
    UT_RecursiveTimedLock myLock;
};

static geoTimerLock theTimerLock;
} // namespace

static UT_Thread &
unregisterThread()
{
    static UT_Thread* unregisterThread(
            UT_Thread::allocThread(UT_Thread::SpinMode::ThreadSingleRun, false));

    return *unregisterThread;
}

static bool exitUnregisterThread = false;

static void
waitAndUnregisterExitCB(void* data)
{
    exitUnregisterThread = true;
    // add a dummy to the statusQueue to wake the thread if it is blocked on
    // the queue.
    statusQueue().append(GEO_HAPISessionStatusHandle());
    // Wake up the thread if it is sleeping.
    theTimerLock.get().unlock();
    delete &unregisterThread();
}

static void*
waitAndUnregister(void* data)
{
    while (true)
    {
        GEO_HAPISessionStatusHandle status = statusQueue().waitAndRemove();

        // An empty handle signals that the thread can finish.
        if (!status)
        {
            UT_ASSERT(statusQueue().entries() == 0);
            break;
        }

        fpreal64 time = status->getLifeTime();
        while (time < GEO_HAPI_SESSION_CLOSE_DELAY && !exitUnregisterThread)
        {
            int diff = (int)(GEO_HAPI_SESSION_CLOSE_DELAY - time + 1);
            // Allow a brief window where the session can be reclaimed even if
            // the user didn't explicitly request to keep the session open.
            // This has a noticeable improvement for cooking the HDA Dynamic
            // Payload LOP, and for the regression test timings.
            if (!status->isValid())
                diff = 1;

            if (theTimerLock.get().timedLock(1000 * diff))
                theTimerLock.get().unlock();

            time = status->getLifeTime();

            if (!status->isValid())
                break;
        }

        status->close();
    }

    return nullptr;
}

static UT_Lock&
unregisterThreadLock()
{
    static UT_Lock theLock;
    return theLock;
}

static bool theUnregisterThreadInitialized = false;

GEO_HAPISessionStatusHandle
GEO_HAPISessionManager::delayedUnregister(
        const HAPI_NodeId nodeId,
        const GEO_HAPISessionID sessionId)
{
    GEO_HAPISessionStatusHandle status = GEO_HAPISessionStatus::trackSession(
            nodeId, sessionId);

    // intialize the thread if needed
    if (!theUnregisterThreadInitialized)
    {
        UT_AutoLock l(unregisterThreadLock());
	
	// Make sure the thread wasn't initialized while waiting on the lock
        if (!theUnregisterThreadInitialized)
        {
            unregisterThread().startThread(waitAndUnregister, nullptr);
            UT_Exit::addExitCallback(waitAndUnregisterExitCB, nullptr);
            theUnregisterThreadInitialized = true;
        }
    }

    statusQueue().append(status);

    return status;
}

// --------------------------------------------------------------------------------------------------

HAPI_Session &
GEO_HAPISessionManager::sharedSession(GEO_HAPISessionID id)
{
    hapiSessionsLock().lock();
    UT_ASSERT(managersMap().contains(id));
    GEO_HAPISessionManager &manager = managersMap()[id];
    hapiSessionsLock().unlock();

    return manager.mySession;
}

void
GEO_HAPISessionManager::lockSession(GEO_HAPISessionID id)
{
    hapiSessionsLock().lock();
    UT_ASSERT(managersMap().contains(id));
    GEO_HAPISessionManager &manager = managersMap()[id];
    hapiSessionsLock().unlock();

    manager.myLock.lock();
}

void
GEO_HAPISessionManager::unlockSession(GEO_HAPISessionID id)
{
    hapiSessionsLock().lock();
    UT_ASSERT(managersMap().contains(id));
    GEO_HAPISessionManager &manager = managersMap()[id];
    hapiSessionsLock().unlock();

    manager.myLock.unlock();
}

static HAPI_CookOptions
getCookOptions()
{
    HAPI_CookOptions cookOptions = HAPI_CookOptions_Create();
    cookOptions.handleSpherePartTypes = true;
    cookOptions.packedPrimInstancingMode
            = HAPI_PACKEDPRIM_INSTANCING_MODE_HIERARCHY;
    cookOptions.checkPartChanges = true;

    return cookOptions;
}

bool
GEO_HAPISessionManager::createSession(GEO_HAPISessionID id)
{
    HAPI_ThriftServerOptions serverOptions{true, 3000.f,
        HAPI_STATUSVERBOSITY_WARNINGS};

    std::string pipeName = "hapi" + std::to_string(id) + "_";

// Add the process id to the pipe name to ensure it is unique when multiple
// Houdini instances run
#ifndef _WIN32
    pipeName += std::to_string(getpid());
#else
    pipeName += std::to_string(_getpid());
#endif

    if (HAPI_RESULT_SUCCESS
        != HAPI_StartThriftNamedPipeServer(
                   &serverOptions, pipeName.c_str(), nullptr, nullptr))
    {
        return false;
    }

    if (HAPI_RESULT_SUCCESS
        != HAPI_CreateThriftNamedPipeSession(&mySession, pipeName.c_str()))
    {
        return false;
    }

    // Set up cooking options
    HAPI_CookOptions cookOptions = getCookOptions();

    if (HAPI_RESULT_SUCCESS
        != HAPI_Initialize(
                   &mySession, &cookOptions, true, -1, nullptr, nullptr,
                   nullptr, nullptr, nullptr))
    {
        return false;
    }

    return true;
}

void
GEO_HAPISessionManager::cleanupSession()
{
    if (HAPI_IsSessionValid(&mySession) == HAPI_RESULT_SUCCESS)
    {
        HAPI_Cleanup(&mySession);
        HAPI_CloseSession(&mySession);
    }
}

//
// GEO_HAPISessionStatus
//

GEO_HAPISessionStatus::GEO_HAPISessionStatus(
        const HAPI_NodeId nodeId,
        const GEO_HAPISessionID sessionId)
    : myNodeId(nodeId), mySessionId(sessionId), myDataValid(true)
{
    myLifetime.start();
}

GEO_HAPISessionStatus::~GEO_HAPISessionStatus()
{
    close();
}

GEO_HAPISessionStatusHandle
GEO_HAPISessionStatus::trackSession(
        const HAPI_NodeId nodeId,
        const GEO_HAPISessionID sessionId)
{
    GEO_HAPISessionStatusHandle status(
            new GEO_HAPISessionStatus(nodeId, sessionId));
    status->myLifetime.start();
    return status;
}

fpreal64
GEO_HAPISessionStatus::getLifeTime()
{
    UT_AutoLock l(myLock);
    return myLifetime.getTime();
}

bool
GEO_HAPISessionStatus::isValid()
{
    UT_AutoLock l(myLock);
    return !myDataValid;
}

bool
GEO_HAPISessionStatus::claim(
        HAPI_NodeId &nodeIdOut,
        GEO_HAPISessionID &sessionIdOut)
{
    UT_AutoLock l(myLock);

    if (myDataValid)
    {
        myDataValid = false;
        nodeIdOut = myNodeId;
        sessionIdOut = mySessionId;
        return true;
    }
    return false;
}

bool
GEO_HAPISessionStatus::close()
{
    UT_AutoLock l(myLock);

    if (myDataValid)
    {
        // Delete a node if we were given one
        if (myNodeId >= 0)
        {
            GEO_HAPISessionManager::SessionScopeLock lock(mySessionId);
            HAPI_Session &session = lock.getSession();
            if (HAPI_IsSessionValid(&session) == HAPI_RESULT_SUCCESS)
            {
                HAPI_DeleteNode(&session, myNodeId);
            }
        }
        GEO_HAPISessionManager::unregister(mySessionId);
        myDataValid = false;
        return true;
    }
    return false;
}
