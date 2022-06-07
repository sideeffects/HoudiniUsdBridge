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

#include "HUSD_GeoUtils.h"
#include "HUSD_DataHandle.h"
#include "HUSD_FindPrims.h"
#include "HUSD_PathSet.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <CH/CH_Manager.h>
#include <gusd/GU_PackedUSD.h>
#include <gusd/GU_USD.h>
#include <gusd/purpose.h>
#include <gusd/stageCache.h>
#include <GU/GU_Detail.h>
#include <GU/GU_PrimPacked.h>
#include <UT/UT_String.h>
#include <UT/UT_StringArray.h>
#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool
husdImportUsdIntoGeometry(
	GU_Detail *gdp,
	const UsdStageRefPtr &stage,
	const UT_StringHolder &stage_identifier,
	const HUSD_FindPrims &findprims,
	const UT_StringHolder &purpose,
	const UT_StringHolder &traversal,
	const UT_StringHolder &pathattribname,
	const UT_StringHolder &nameattribname,
	const HUSD_TimeCode &timecode)
{
    const GusdUSD_Traverse	*trav = NULL;
    if(traversal.isstring()) {
	const auto	&table = GusdUSD_TraverseTable::GetInstance();

	trav = table.FindTraversal(traversal);
	if(!trav)
	    return false;
    }

    // Load the root prims.
    UT_Array<UsdPrim>	 rootPrims;
    for (auto &&it : findprims.getExpandedPathSet().sdfPathSet())
    {
	UsdPrim	 prim = stage->GetPrimAtPath(it);

	if (prim)
	    rootPrims.append(prim);
    }

    GusdDefaultArray<UT_StringHolder> stageids;
    stageids.SetConstant(stage_identifier);
    GusdDefaultArray<UsdTimeCode> times;
    times.SetConstant(HUSDgetUsdTimeCode(timecode));
    GusdDefaultArray<GusdPurposeSet> purposes;
    purposes.SetConstant(
        GusdPurposeSet(GusdPurposeSetFromMask(purpose) | GUSD_PURPOSE_DEFAULT));
    GusdDefaultArray<UT_StringHolder> lods;
    lods.SetConstant("full");

    UT_Array<UsdPrim>	 prims;
    if(trav)
    {
	UT_Array<GusdUSD_Traverse::PrimIndexPair> primIndexPairs;
	UT_UniquePtr<GusdUSD_Traverse::Opts> opts(trav->CreateOpts());

	if(!trav->FindPrims(rootPrims, times, purposes, primIndexPairs,
				/*skip root*/ false, opts.get()))
	    return false;

	// Resize the prims list to match the size of primIndexPairs.
	prims.setSize(primIndexPairs.size());
	// Then iterate through primIndexPairs to populate the prim list.
	for (exint i = 0, n = prims.size(); i < n; i++)
	    prims(i) = primIndexPairs(i).first;
    }
    else
    {
	std::swap(prims, rootPrims);
    }

    // We have the resolved set of USD prims. Now create packed prims in the
    // geometry.
    GusdGU_USD::AppendPackedPrimsFromLopNode(*gdp,
        prims, stageids, times, lods, purposes,
        GusdGU_PackedUSD::PivotLocation::Origin);

    GA_Attribute	*pathAttrib = nullptr;
    GA_Attribute	*nameAttrib = nullptr;
    if (pathattribname.isstring())
	pathAttrib = gdp->addStringTuple(
	    GA_ATTRIB_PRIMITIVE, pathattribname, 1);
    if (nameattribname.isstring())
	nameAttrib = gdp->addStringTuple(
	    GA_ATTRIB_PRIMITIVE, nameattribname, 1);
    if (pathAttrib || nameAttrib)
    {
	GA_RWHandleS	 hpath(pathAttrib);
	GA_RWHandleS	 hname(nameAttrib);

	if (hpath.isValid() || hname.isValid())
	{
	    for (GA_Iterator it(gdp->getPrimitiveRange());
		 !it.atEnd(); ++it)
	    {
		GA_Primitive *prim = gdp->getPrimitive(*it);

		if (prim->getTypeId() != GusdGU_PackedUSD::typeId())
		    continue;

                GU_PrimPacked *packed = UTverify_cast<GU_PrimPacked *>(prim);
                const GU_PackedImpl *packedImpl = packed->sharedImplementation();

                // NOTE: GCC 6.3 doesn't allow dynamic_cast on non-exported classes,
                //       and GusdGU_PackedUSD isn't exported for some reason,
                //       so to avoid Linux debug builds failing, we static_cast
                //       instead of UTverify_cast.
                const GusdGU_PackedUSD *packedUsd =
#if !defined(LINUX)
                    UTverify_cast<const GusdGU_PackedUSD *>(packedImpl);
#else
                    static_cast<const GusdGU_PackedUSD *>(packedImpl);
#endif
		SdfPath sdfpath = packedUsd->primPath();
		if (hpath.isValid())
		    hpath.set(*it, sdfpath.GetText());
		if (hname.isValid())
		    hname.set(*it, sdfpath.GetName());
	    }
	}
    }

    return true;
}

}

bool
HUSDimportUsdIntoGeometry(
	GU_Detail *gdp,
	const HUSD_LockedStagePtr &locked_stage,
	const HUSD_FindPrims &findprims,
	const UT_StringHolder &purpose,
	const UT_StringHolder &traversal,
	const UT_StringHolder &pathattribname,
	const UT_StringHolder &nameattribname,
	const HUSD_TimeCode &timecode)
{
    GusdStageCacheReader    cache_reader;
    UsdStageRefPtr          stage;
    stage = cache_reader.Find(locked_stage->getStageCacheIdentifier());
    if (!stage)
	return false;

    return husdImportUsdIntoGeometry(
	    gdp, stage, locked_stage->getStageCacheIdentifier(),
	    findprims, purpose, traversal, pathattribname, nameattribname,
            timecode);
}

bool
HUSDimportUsdIntoGeometry(
	GU_Detail *gdp,
	void *stage_ptr,
	const HUSD_FindPrims &findprims,
	const UT_StringHolder &purpose,
	const UT_StringHolder &traversal,
	const UT_StringHolder &pathattribname,
	const UT_StringHolder &nameattribname,
	const HUSD_TimeCode &timecode)
{
    UT_ASSERT(stage_ptr);
    if (!stage_ptr)
        return false;
    
    UsdStageWeakPtr stage =
	    BOOST_NS::python::extract<UsdStageWeakPtr>((PyObject*)stage_ptr);
    
    return husdImportUsdIntoGeometry(
	    gdp, stage, stage->GetRootLayer()->GetIdentifier(),
	    findprims, purpose, traversal, pathattribname, nameattribname,
            timecode);
}
