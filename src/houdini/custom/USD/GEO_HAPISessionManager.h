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

#ifndef __GEO_HAPI_SESSION_MANAGER_H__
#define __GEO_HAPI_SESSION_MANAGER_H__

#include <HAPI/HAPI.h>
#include <UT/UT_SharedPtr.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_Lock.h>

// Time to wait before closing an unused session in seconds:
#define GEO_HAPI_SESSION_CLOSE_DELAY 60.0

typedef exint GEO_HAPISessionID;

class GEO_HAPISessionStatus;
typedef UT_SharedPtr<GEO_HAPISessionStatus> GEO_HAPISessionStatusHandle;

// Class to moniter the status of a session and node closed with
// delayedUnregister()
class GEO_HAPISessionStatus
{
public:
    static GEO_HAPISessionStatusHandle trackSession(
            const HAPI_NodeId nodeId,
            const GEO_HAPISessionID sessionId);

    ~GEO_HAPISessionStatus();

    // Reclaim the session and prevent anything from being deleted
    // Returns true if the session was successfully reclaimed and false
    // if it has already closed
    bool claim(HAPI_NodeId &nodeIdOut, GEO_HAPISessionID &sessionIdOut);

    // Delete the node and unregister from the HAPI Session
    // The HAPI Session will close if this is the last registered user
    // Nothing will happen if the session has already been claimed
    // Returns true iff this object was successfully unregistered from the
    // session
    bool close();

    fpreal64 getLifeTime();
    bool isValid();

private:
    GEO_HAPISessionStatus() = default;
    GEO_HAPISessionStatus(
            const HAPI_NodeId nodeId,
            const GEO_HAPISessionID sessionId);

    HAPI_NodeId myNodeId;
    GEO_HAPISessionID mySessionId;

    bool myDataValid; // true if this session has not been claimed or closed
    UT_StopWatch myLifetime;
    UT_Lock myLock;
};

/// \class GEO_HAPISessionManager
///
/// Class to manage HAPI sessions used by multiple objects
///
class GEO_HAPISessionManager
{
public:
    GEO_HAPISessionManager();

    // Must be called to use a shared session. Returns a GEO_HAPISessionID to be
    // used to access the session. A session remains open until all registered
    // users call unregister(). If the session fails to initialize, this will
    // return -1. Valid ids are never negative
    static GEO_HAPISessionID registerAsUser();

    // Notifies the manager that the session is no longer being used. Should be
    // called once with the id returned from registerAsUser(). Using id after
    // this call will result in undefined behaviour
    static void unregister(GEO_HAPISessionID id);

    // If it is expected that the session might be needed in a short period of
    // time this function will wait before deleting the Node at nodeId and
    // unregistering from the session Calling GEO_HAPISessionStatus::claim()
    // before the delay time is up will prevent any changes from being made to
    // the session
    static GEO_HAPISessionStatusHandle delayedUnregister(
            const HAPI_NodeId nodeId,
            const GEO_HAPISessionID sessionId);

    //
    // Helper class for locking the session manager and accessing the
    // HAPI_Session.
    //
    // Must be constructed with the id from registerAsUser(). For example:
    //
    //     GEO_HAPISessionID id = GEO_HAPISessionManager::registerAsUser(); /*
    //     Open session */ SessionScopeLock scopeLock(id); HAPI_Session &session
    //     = scopeLock.getSession();
    //     ...
    //     /* Do stuff with session */
    //     ...
    //     GEO_HAPISessionManager::unregister(id); /* Close session */
    //
    class SessionScopeLock : UT_NonCopyable
    {
    public:
        explicit SessionScopeLock(GEO_HAPISessionID id) : myId(id)
        {
            lockSession(myId);

            // Add as a user so the lock being managed isn't destroyed before
            // this destructor is called
            addToUsers();
        }
        ~SessionScopeLock()
        {
            unlockSession(myId);
            removeFromUsers();
        }

        HAPI_Session &getSession() { return sharedSession(myId); }

    private:
        // Make this constructor private to ensure this object is created with a
        // GEO_HAPISessionID
        SessionScopeLock() {}

        void addToUsers();
        void removeFromUsers();

        GEO_HAPISessionID myId;
    };

private:
    static HAPI_Session &sharedSession(GEO_HAPISessionID id);

    static void lockSession(GEO_HAPISessionID id);
    static void unlockSession(GEO_HAPISessionID id);

    bool createSession(GEO_HAPISessionID id);
    void cleanupSession();

    int myUserCount;
    HAPI_Session mySession;
    UT_Lock myLock;
};

#endif // __GEO_HAPI_SESSION_MANAGER_H__
