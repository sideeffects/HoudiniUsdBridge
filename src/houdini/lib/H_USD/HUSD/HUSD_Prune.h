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

#ifndef __HUSD_Prune_h__
#define __HUSD_Prune_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Utils.h"
#include <UT/UT_StringArray.h>

class HUSD_FindPrims;
class HUSD_TimeCode;

class HUSD_API HUSD_Prune
{
public:
			 HUSD_Prune(HUSD_AutoWriteLock &dest);
			~HUSD_Prune();

    enum PruneMethod
    {
	Deactivate,
	MakeInvisible
    };

    bool		 prune(const HUSD_FindPrims &findprims,
                                const HUSD_FindPrims *excludeprims,
                                const HUSD_FindPrims *limitpruneprims,
				const HUSD_TimeCode &timecode,
				PruneMethod prune_method,
                                bool prune,
				bool prune_unselected,
                                bool prune_ancestors_automatically,
                                UT_StringArray *pruned_prims) const;

    bool                 getIsTimeVarying() const;

private:
    HUSD_AutoWriteLock	        &myWriteLock;
    mutable HUSD_TimeSampling    myTimeSampling;
};

#endif

