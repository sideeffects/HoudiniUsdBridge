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

#include "HUSD_Prune.h"
#include "HUSD_FindPrims.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <gusd/UT_Gf.h>
#include <pxr/base/vt/types.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/pointInstancer.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_Prune::HUSD_Prune(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_Prune::~HUSD_Prune()
{
}

void
HUSD_Prune::prune(const HUSD_FindPrims &findprims,
	const HUSD_TimeCode &timecode,
	HUSD_Prune::PruneMethod prune_method,
	bool prune_unselected) const
{
    auto	 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	auto			 stage = outdata->stage();
	UsdTimeCode		 usdtime(HUSDgetUsdTimeCode(timecode));
	const XUSD_PathSet	&paths = prune_unselected
				    ? findprims.getExcludedPathSet(true)
				    : findprims.getExpandedPathSet();

	for (auto &&path : paths)
	{
	    auto	 usdprim = stage->GetPrimAtPath(path);

	    if (!usdprim)
		continue;

	    if (prune_method == HUSD_Prune::MakeInvisible)
	    {
		UsdGeomImageable imageable(usdprim);

		if (!imageable)
		    continue;

		imageable.MakeInvisible(usdtime);
	    }
	    else
		usdprim.SetActive(false);
	}

    	UT_StringMap<UT_Int64Array> excluded_ids;
    	const UT_StringMap<UT_Int64Array> *instmap;
	if (prune_unselected)
	{
	    findprims.getExcludedPointInstancerIds(excluded_ids, timecode);
	    instmap = &excluded_ids;
	}
	else
	    instmap = &findprims.getPointInstancerIds();

	VtArray<int64>	 invisible_ids;

	for (auto it = instmap->begin(); it != instmap->end(); ++it)
	{
	    SdfPath		  sdfpath(HUSDgetSdfPath(it->first));
	    UsdGeomPointInstancer instancer(stage->GetPrimAtPath(sdfpath));

	    if (instancer)
	    {
		auto	 invisible_ids_attr = instancer.GetInvisibleIdsAttr();

		if (invisible_ids_attr.Get(&invisible_ids, usdtime))
		{
		    UT_Int64Array    combined_ids;

		    combined_ids.setSize(invisible_ids.size());
		    for (int i = 0, n = invisible_ids.size(); i < n; i++)
			combined_ids(i) = invisible_ids[i];
		    combined_ids.sort();
		    combined_ids.sortedUnion(it->second);
		    invisible_ids.assign(
			combined_ids.begin(), combined_ids.end());
		}
		else
		    invisible_ids.assign(it->second.begin(), it->second.end());
		invisible_ids_attr.Set(invisible_ids, usdtime);
	    }
	}
    }
}

