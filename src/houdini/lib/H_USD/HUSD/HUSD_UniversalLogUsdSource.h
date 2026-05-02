/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *      Side Effects Software Inc
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 * NAME:	HUSD_UniversalLogUsdSource.h ( FS Library, C++)
 *
 * COMMENTS:
 *
 */

#ifndef __HUSD_UniversalLogUsdSource__
#define __HUSD_UniversalLogUsdSource__

#include "HUSD_API.h"
#include <UT/UT_UniversalLogSource.h>

class HUSD_API HUSD_UniversalLogUsdSource : public UT_UniversalLogSource
{
public:
                     HUSD_UniversalLogUsdSource();
                    ~HUSD_UniversalLogUsdSource() override;

    static const UT_StringHolder    &staticName();
};

#endif

