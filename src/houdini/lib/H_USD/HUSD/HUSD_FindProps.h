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

#ifndef __HUSD_FindProps_h__
#define __HUSD_FindProps_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_FindPrims.h"
#include "HUSD_Utils.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_PathSet;
class XUSD_PathPattern;

PXR_NAMESPACE_CLOSE_SCOPE

class HUSD_API HUSD_FindProps
{
public:
				 HUSD_FindProps(HUSD_AutoAnyLock &lock,
					 HUSD_PrimTraversalDemands demands =
					    HUSD_TRAVERSAL_DEFAULT_DEMANDS);
				 // Simple constructor when you just want to
				 // operate on a single property.
				 HUSD_FindProps(HUSD_AutoAnyLock &lock,
					 const UT_StringRef &primpath,
					 const UT_StringRef &propname,
					 HUSD_PrimTraversalDemands demands =
					    HUSD_TRAVERSAL_DEFAULT_DEMANDS);
				~HUSD_FindProps();

    const HUSD_FindPrims	&findPrims() const
				 { return myFindPrims; }
    HUSD_FindPrims		&findPrims()
				 { return myFindPrims; }

    const UT_StringHolder	&propertyPattern() const
				 { return myPropertyPattern; }
    void			 setPropertyPattern(
					const UT_StringHolder &pattern);

    const PXR_NS::XUSD_PathSet	&getExpandedPathSet() const;
    void			 getExpandedPaths(UT_StringArray &paths) const;

private:
    class husd_FindPropsPrivate;

    UT_UniquePtr<husd_FindPropsPrivate>	 myPrivate;
    HUSD_AutoAnyLock			&myAnyLock;
    HUSD_FindPrims			 myFindPrims;
    UT_StringHolder			 myPropertyPattern;
};

#endif

