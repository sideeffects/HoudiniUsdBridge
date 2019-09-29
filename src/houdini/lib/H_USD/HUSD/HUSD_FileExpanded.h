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

#ifndef __HUSD_FILEEXPANDED_h__
#define __HUSD_FILEEXPANDED_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>

class HUSD_API HUSD_FileExpanded
{
public:
    static UT_StringHolder expand(const char* str, fpreal ff, fpreal inc, int i,
				  bool& changed);
};

#endif

