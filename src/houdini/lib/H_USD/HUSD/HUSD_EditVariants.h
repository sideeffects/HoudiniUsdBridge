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

#ifndef __HUSD_EditVariants_h__
#define __HUSD_EditVariants_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>

class HUSD_FindPrims;

class HUSD_API HUSD_EditVariants
{
public:
			 HUSD_EditVariants(HUSD_AutoWriteLock &lock);
			~HUSD_EditVariants();

    bool		 setVariant(const HUSD_FindPrims &findprims,
				const UT_StringRef &variantset,
				const UT_StringRef &variantname,
                                int variantsetindex = -1,
                                int variantnameindex = -1);

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

