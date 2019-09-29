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

#ifndef __HUSD_Stitch_h__
#define __HUSD_Stitch_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>

class HUSD_API HUSD_Stitch
{
public:
			 HUSD_Stitch();
			~HUSD_Stitch();

    bool		 addHandle(const HUSD_DataHandle &src);
    bool		 execute(HUSD_AutoWriteLock &lock,
                                bool copy_stitched_layers = false) const;

private:
    class husd_StitchPrivate;

    UT_UniquePtr<husd_StitchPrivate>	 myPrivate;
};

#endif

