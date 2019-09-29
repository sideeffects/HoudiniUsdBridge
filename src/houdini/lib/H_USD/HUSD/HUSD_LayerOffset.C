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

#include "HUSD_LayerOffset.h"
#include <SYS/SYS_Math.h>

bool
HUSD_LayerOffset::operator==(const HUSD_LayerOffset &other) const
{
    if (!isValid())
	return !other.isValid();

    return SYSisEqual(myOffset, other.myOffset, SYS_FP64_EPSILON) &&
	   SYSisEqual(myScale, other.myScale, SYS_FP64_EPSILON);
}

bool
HUSD_LayerOffset::isIdentity() const
{
    static HUSD_LayerOffset	 theIdentityOffset;

    return (*this == theIdentityOffset);
}

bool
HUSD_LayerOffset::isValid() const
{
    return SYSisFinite(myOffset) && SYSisFinite(myScale);
}

HUSD_LayerOffset
HUSD_LayerOffset::inverse() const
{
    if (isIdentity())
	return *this;

    fpreal64	 newscale;

    if (myScale != 0.0)
	newscale = 1.0 / myScale;
    else
	newscale = SYS_Types<fpreal64>::infinity();

    return HUSD_LayerOffset(-myOffset, newscale);
}

