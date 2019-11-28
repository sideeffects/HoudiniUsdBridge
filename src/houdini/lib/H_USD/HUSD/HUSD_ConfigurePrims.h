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

#ifndef __HUSD_ConfigurePrims_h__
#define __HUSD_ConfigurePrims_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_TimeCode.h"
#include <UT/UT_StringHolder.h>

class HUSD_FindPrims;

class HUSD_API HUSD_ConfigurePrims
{
public:
			 HUSD_ConfigurePrims(HUSD_AutoWriteLock &lock);
			~HUSD_ConfigurePrims();

    bool		 setActive(const HUSD_FindPrims &findprims,
				bool active) const;
    bool		 setKind(const HUSD_FindPrims &findprims,
				const UT_StringRef &kind) const;
    bool		 setDrawMode(const HUSD_FindPrims &findprims,
				const UT_StringRef &drawmode) const;
    bool		 setPurpose(const HUSD_FindPrims &findprims,
				const UT_StringRef &purpose) const;
    bool		 setProxy(const HUSD_FindPrims &findprims,
				const UT_StringRef &proxy) const;
    bool		 setInstanceable(const HUSD_FindPrims &findprims,
				bool instanceable) const;
    enum Visibility {
	VISIBILITY_INHERIT,
	VISIBILITY_INVISIBLE,
	VISIBILITY_VISIBLE
    };
    bool		 setInvisible(const HUSD_FindPrims &findprims,
				Visibility vis,
				bool for_all_time,
				const HUSD_TimeCode &timecode,
				bool is_timecode_strict = true) const;
    bool		 setVariantSelection(const HUSD_FindPrims &findprims,
				const UT_StringRef &variantset,
				const UT_StringRef &variant) const;

    bool		 setAssetName(const HUSD_FindPrims &findprims,
				const UT_StringRef &name) const;
    bool		 setAssetIdentifier(const HUSD_FindPrims &findprims,
				const UT_StringRef &identifier) const;
    bool		 setAssetVersion(const HUSD_FindPrims &findprims,
				const UT_StringRef &version) const;
    bool		 setAssetDependencies(const HUSD_FindPrims &findprims,
				const UT_StringArray &dependencies) const;

    bool		 setEditorNodeId(const HUSD_FindPrims &findprims,
				int nodeid) const;

    bool		 applyAPI(const HUSD_FindPrims &findprims,
				const UT_StringRef &schema) const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

