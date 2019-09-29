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

#ifndef __XUSD_PathSet_h__
#define __XUSD_PathSet_h__

#include "HUSD_API.h"
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

class HUSD_API XUSD_PathSet : public SdfPathSet
{
public:
			 XUSD_PathSet();
			~XUSD_PathSet();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

