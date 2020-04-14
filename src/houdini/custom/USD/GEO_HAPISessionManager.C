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
#include <UT/UT_Map.h>
#include <UT/UT_WorkBuffer.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#endif

#define MAX_USERS_PER_SESSION 10

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

GEO_HAPISessionManager::GEO_HAPISessionManager() : myUserCount(0) {}

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
    cookOptions.packedPrimInstancingMode =
        HAPI_PACKEDPRIM_INSTANCING_MODE_HIERARCHY;
    cookOptions.checkPartChanges = true;

    return cookOptions;
}

bool
GEO_HAPISessionManager::createSession(GEO_HAPISessionID id)
{
    HAPI_ThriftServerOptions serverOptions{true, 3000.f};

    std::string pipeName = "hapi" + std::to_string(id) + "_";

    // Add the process id to the pipe name to ensure it is unique when multiple Houdini instances run
    #ifndef _WIN32
    pipeName += std::to_string(getpid());
    #else
    pipeName += std::to_string(_getpid());
    #endif

    if (HAPI_RESULT_SUCCESS != HAPI_StartThriftNamedPipeServer(
                                   &serverOptions, pipeName.c_str(), nullptr))
    {
        return false;
    }

    if (HAPI_RESULT_SUCCESS !=
        HAPI_CreateThriftNamedPipeSession(&mySession, pipeName.c_str()))
    {
        return false;
    }

    // Set up cooking options
    HAPI_CookOptions cookOptions = getCookOptions();

    if (HAPI_RESULT_SUCCESS != HAPI_Initialize(&mySession, &cookOptions, true,
                                               -1, nullptr, nullptr, nullptr,
                                               nullptr, nullptr))
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
