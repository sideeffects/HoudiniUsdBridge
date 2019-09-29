/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *      Calvin Gu
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_SetMetadata_h__
#define __HUSD_SetMetadata_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_TimeCode.h"
#include <UT/UT_StringHolder.h>

class HUSD_API HUSD_SetMetadata
{
public:
			 HUSD_SetMetadata(HUSD_AutoWriteLock &lock);
			~HUSD_SetMetadata();

    template<typename UtValueType>
    bool		 setMetadata(const UT_StringRef &object_path,
				const UT_StringRef &metadata_name,
				const UtValueType &value) const;

    bool		 clearMetadata(const UT_StringRef &object_path,
				const UT_StringRef &metadata_name) const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif
