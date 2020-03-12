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
#include <UT/UT_Lock.h>

typedef exint GEO_HAPISessionID;

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
