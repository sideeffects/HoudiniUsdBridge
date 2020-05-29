/*
 * Copyright 2019 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_EditCollections_h__
#define __HUSD_EditCollections_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>

class HUSD_FindPrims;

class HUSD_API HUSD_EditCollections
{
public:
			 HUSD_EditCollections(HUSD_AutoWriteLock &lock);
			~HUSD_EditCollections();

    bool		 createCollection(const UT_StringRef &primpath,
				const UT_StringRef &collectionname,
				const UT_StringRef &expansionrule,
				const HUSD_FindPrims &includeprims,
				const HUSD_FindPrims &excludeprims,
				bool createprim);

    bool		 createCollection(const UT_StringRef &primpath,
				const UT_StringRef &collectionname,
				const UT_StringRef &expansionrule,
				const HUSD_FindPrims &includeprims,
				bool createprim);

    // Note, use HUSDmakeCollectionPath() to obtain collection path, if needed.
    bool		 setCollectionExpansionRule( 
				const UT_StringRef &collectionpath,
				const UT_StringRef &expansionrule);
    bool		 setCollectionIncludes( 
				const UT_StringRef &collectionpath,
				const UT_StringArray &paths);
    bool		 addCollectionInclude( 
				const UT_StringRef &collectionpath,
				const UT_StringRef &path);
    bool		 setCollectionExcludes( 
				const UT_StringRef &collectionpath,
				const UT_StringArray &paths);
    bool		 addCollectionExclude( 
				const UT_StringRef &collectionpath,
				const UT_StringRef &path);

    // Set an icon for use in our tree views.
    bool                 setCollectionIcon(
				const UT_StringRef &collectionpath,
				const UT_StringHolder &icon);

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

