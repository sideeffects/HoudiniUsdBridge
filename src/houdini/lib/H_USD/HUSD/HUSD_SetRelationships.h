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

#ifndef __HUSD_SetRelationships_h__
#define __HUSD_SetRelationships_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>

class HUSD_API HUSD_SetRelationships
{
public:
			 HUSD_SetRelationships(HUSD_AutoWriteLock &lock);
			~HUSD_SetRelationships();

    bool		 setRelationship(
				const HUSD_FindPrims &findprims,
				const UT_StringRef &relationship_name,
				const UT_StringArray &target_paths) const;
    bool		 setRelationship(const UT_StringRef &primpath,
				const UT_StringRef &relationship_name,
				const UT_StringArray &target_paths) const;

    bool		 blockRelationship(
				const HUSD_FindPrims &findprims,
				const UT_StringRef &relationship_name) const;

    bool		 addRelationshipTarget(
				const HUSD_FindPrims &findprims,
				const UT_StringRef &relationship_name,
				const UT_StringRef &target_path) const;
    bool		 removeRelationshipTarget(
				const HUSD_FindPrims &findprims,
				const UT_StringRef &relationship_name,
				const UT_StringRef &target_path) const;


private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

