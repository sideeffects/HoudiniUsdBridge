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

#ifndef __HUSD_ObjectHandle_h__
#define __HUSD_ObjectHandle_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Overrides.h"
#include <UT/UT_StringHolder.h>

// This class is a standalone wrapper around a specific object in a USD
// stage wrapped in an HUSD_DataHandle. It's purpose is to serve as the data
// accessor for tree nodes in the Scene Graph Tree. It should not be used for
// any other purpose, as it is extremely inefficient. Each function call locks
// the HUSD_DataHandle, queries its information, then unlocks it again. This
// is a matter of convenience for the calling pattern of the scene graph tree.
// Because it is inefficient the scene graph tree caches any information that
// comes out of this object.
//
// Anyone else tempted to use this object should use HUSD_Info instead.
class HUSD_API HUSD_ObjectHandle
{
public:
				 HUSD_ObjectHandle();
				 HUSD_ObjectHandle(const UT_StringHolder &path,
					const UT_StringHolder &name);
    virtual			~HUSD_ObjectHandle();

    virtual const HUSD_DataHandle	&dataHandle() const = 0;
    virtual const HUSD_ConstOverridesPtr&overrides() const = 0;

    const UT_StringHolder	&path() const
				 { return myPath; }
    const UT_StringHolder	&name() const
				 { return myName; }
    bool			 isValid() const
				 { return myPath.isstring(); }

protected:
    static const UT_StringHolder theRootPrimPath;
    static const UT_StringHolder theRootPrimName;

    UT_StringHolder		 myPath;
    UT_StringHolder		 myName;
};

#endif

