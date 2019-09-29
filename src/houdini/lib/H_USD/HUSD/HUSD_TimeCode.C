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

#include "HUSD_TimeCode.h"
#include <CH/CH_Manager.h>

HUSD_TimeCode::HUSD_TimeCode(fpreal value,
	HUSD_TimeCode::TimeFormat format,
	bool is_default)
    : myFrame(value),
      myIsDefault(is_default)
{
    if (format == TIME)
	myFrame = CHgetSampleFromTime(myFrame);
}

fpreal
HUSD_TimeCode::time() const
{
    return CHgetTimeFromFrame(myFrame);
}

