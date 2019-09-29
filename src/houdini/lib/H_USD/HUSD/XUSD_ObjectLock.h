/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_ObjectLock_h__
#define __HUSD_ObjectLock_h__

#include "HUSD_API.h"
#include "XUSD_Data.h"
#include "HUSD_PrimHandle.h"
#include "HUSD_PropertyHandle.h"
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/object.h>

PXR_NAMESPACE_OPEN_SCOPE

template <class ObjectType>
class XUSD_AutoObjectLock : UT_NonCopyable
{
public:
			 XUSD_AutoObjectLock(const HUSD_PrimHandle &prim)
			    : myObjectHandle(prim)
			 {
			    auto data = myObjectHandle.dataHandle().
				readLock(myObjectHandle.overrides(), false);

			    if (data && data->isStageValid())
			    {
				myObject = ObjectType(data->stage()->
				    GetPrimAtPath(SdfPath(
					myObjectHandle.path().toStdString())));
			    }
			 }
			 XUSD_AutoObjectLock(const HUSD_PropertyHandle &prop)
			    : myObjectHandle(prop)
			 {
			    auto data = myObjectHandle.dataHandle().
				readLock(myObjectHandle.overrides(), false);

			    if (data && data->isStageValid())
			    {
				UsdObject obj = data->stage()->
				    GetObjectAtPath(SdfPath(
					myObjectHandle.path().toStdString()));
				myObject = obj.As<ObjectType>();
			    }
			 }

			~XUSD_AutoObjectLock()
			 { myObjectHandle.dataHandle().release(); }

    const ObjectType	&obj() const
			 { return myObject; }

private:
    const HUSD_ObjectHandle	&myObjectHandle;
    ObjectType			 myObject;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

