/*
 * Copyright 2019 Side Effects Software Inc.
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
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __XUSD_DataLock_h__
#define __XUSD_DataLock_h__

#include "HUSD_API.h"
#include <OP/OP_ItemId.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_Lock.h>
#include <UT/UT_String.h>
#include <UT/UT_StringHolder.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_DataLock : public UT_IntrusiveRefCounter<XUSD_DataLock>
{
public:
			 XUSD_DataLock()
			     : myLockCount(0),
			       myLockedNodeId(OP_INVALID_ITEM_ID),
			       myWriteLock(false),
			       myLayerLock(false)
			 {
			 }
			~XUSD_DataLock()
			 { }

    bool		 isLocked() const
			 { return myLockCount > 0; }
    bool		 isReadLocked() const
			 { return myLockCount > 0 && !myWriteLock; }
    bool		 isWriteLocked() const
			 { return myLockCount > 0 && myWriteLock; }
    bool		 isLayerLocked() const
			 { return myLockCount > 0 && myLayerLock; }
    int			 getLockedNodeId() const
			 { return myLockedNodeId; }

private:
    UT_Lock		 myMutex;
    int			 myLockCount;
    int			 myLockedNodeId;
    bool		 myWriteLock;
    bool		 myLayerLock;

    friend class ::HUSD_DataHandle;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

