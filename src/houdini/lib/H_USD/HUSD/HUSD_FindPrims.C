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

#include "HUSD_FindPrims.h"
#include "HUSD_Constants.h"
#include "HUSD_Cvex.h"
#include "HUSD_CvexCode.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_Path.h"
#include "HUSD_PathSet.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_PathPattern.h"
#include "XUSD_Utils.h"
#include <gusd/UT_Gf.h>
#include <OP/OP_Node.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_Performance.h>
#include <UT/UT_String.h>
#include <UT/UT_WorkArgs.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/pyContainerConversions.h>
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
    void
    addAllIds(const UsdGeomPointInstancer &instancer,
            const UsdTimeCode &usdtime,
            UT_StringMap<UT_Int64Array> &ids)
    {
        UT_StringHolder	 path = instancer.GetPath().GetText();
        UT_Int64Array	&bound_ids = ids[path];
        UsdAttribute	 ids_attr = instancer.GetIdsAttr();
        VtArray<int64>	 ids_value;

        if (ids_attr.Get(&ids_value, usdtime))
        {
            for (int64 i = 0, n = ids_value.size(); i < n; i++)
                bound_ids.append(ids_value[i]);
        }
        else
        {
            auto		 protos_attr = instancer.GetProtoIndicesAttr();
            VtArray<int>	 protos_value;

            if (protos_attr.Get(&protos_value, usdtime))
            {
                for (int64 i = 0, n = protos_value.size(); i < n; i++)
                    bound_ids.append(i);
            }
        }
    }

    void
    addBoundIds(const UsdGeomPointInstancer &instancer,
            const GfRange3d &boxrange,
            const UsdTimeCode &usdtime,
            HUSD_FindPrims::BBoxContainment containment,
            UsdGeomBBoxCache &bbox_cache,
            UT_StringMap<UT_Int64Array> &ids)
    {
        UT_StringHolder	         path = instancer.GetPath().GetText();
        UT_Int64Array	        &bound_ids = ids[path];
        UsdAttribute	         ids_attr = instancer.GetIdsAttr();
        UsdAttribute	         protos_attr = instancer.GetProtoIndicesAttr();
        VtArray<int>	         protos_value;
        VtArray<int64>	         ids_value;
        UT_Array<GfBBox3d>	 bounds;

        if (!protos_attr.Get(&protos_value, usdtime))
            return;

        int64		 numids = protos_value.size();

        if (!ids_attr.Get(&ids_value, usdtime))
        {
            ids_value.resize(numids);
            for (int64 i = 0; i < numids; i++)
                ids_value[i] = i;
        }
        bounds.setSize(numids);
        bbox_cache.ComputePointInstanceWorldBounds(
            instancer, ids_value.data(), numids, bounds.data());

        for (int64 i = 0; i < numids; i++)
        {
            GfRange3d		 instrange;

            instrange = bounds(i).ComputeAlignedRange();
            if (boxrange.IsInside(instrange))
            {
                // This inst is fully contained, and therefore it's children
                // are too. No need to look at the children. Just add this
                // inst to the set.
                if (containment == HUSD_FindPrims::BBOX_FULLY_INSIDE ||
                    containment == HUSD_FindPrims::BBOX_PARTIALLY_INSIDE)
                    bound_ids.append(ids_value[i]);
            }
            else if (boxrange.IsOutside(instrange))
            {
                // This inst is fully excluded, and therefore it's children
                // are too. Skip processing any children.
                if (containment == HUSD_FindPrims::BBOX_FULLY_OUTSIDE ||
                    containment == HUSD_FindPrims::BBOX_PARTIALLY_OUTSIDE)
                    bound_ids.append(ids_value[i]);
            }
            else
            {
                // This inst is partially inside, partially outside. If we are
                // interested in partial containment, and this inst has no
                // children, then add this inst to the matching set.
                if (containment == HUSD_FindPrims::BBOX_PARTIALLY_INSIDE ||
                    containment == HUSD_FindPrims::BBOX_PARTIALLY_OUTSIDE)
                    bound_ids.append(ids_value[i]);
            }
        }
    }
}

class HUSD_FindPrims::husd_FindPrimsPrivate
{
public:
    husd_FindPrimsPrivate(HUSD_PrimTraversalDemands demands)
	: myPredicate(HUSDgetUsdPrimPredicate(demands)),
	  myCollectionExpandedPathSetCalculated(false),
	  myExcludedPathSetCalculated{ false, false },
	  myCollectionAwarePathSetCalculated(false),
	  myTimeVarying(false)
    { }

    void invalidateCaches()
    {
	myCollectionExpandedPathSetCalculated = false;
	myExcludedPathSetCalculated[0] = false;
	myExcludedPathSetCalculated[1] = false;
	myCollectionAwarePathSetCalculated = false;
    }
    UsdPrimRange getPrimRange(const UsdStageRefPtr &stage)
    {
        return stage->Traverse(myPredicate);
    }
    bool parallelFindPrims(const UsdStageRefPtr &stage,
            const XUSD_PathPattern &pattern,
            HUSD_PathSet &paths) const
    {
        UsdPrim root = stage->GetPseudoRoot();

        if (root)
        {
            XUSD_FindPrimPathsTaskData data;
            XUSDfindPrims(root, data, myPredicate, &pattern, nullptr);

            data.gatherPathsFromThreads(paths.sdfPathSet());
        }

        return true;
    }

    HUSD_PathSet			 myCollectionlessPathSet;
    HUSD_PathSet			 myCollectionPathSet;
    HUSD_PathSet			 myCollectionExpandedPathSet;
    HUSD_PathSet			 myAncestorPathSet;
    HUSD_PathSet			 myDescendantPathSet;
    HUSD_PathSet			 myCollectionExpandedPathSetCache;
    HUSD_PathSet			 myExcludedPathSetCache[2];
    HUSD_PathSet			 myCollectionAwarePathSetCache;
    HUSD_PathSet                         myMissingExplicitPathSet;
    UT_UniquePtr<UsdGeomBBoxCache>	 myBBoxCache;
    UT_StringMap<UT_Int64Array>		 myPointInstancerIds;
    Usd_PrimFlagsPredicate		 myPredicate;
    bool				 myCollectionExpandedPathSetCalculated;
    bool				 myExcludedPathSetCalculated[2];
    bool				 myCollectionAwarePathSetCalculated;
    bool				 myTimeVarying;
};

HUSD_FindPrims::HUSD_FindPrims(HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
	bool find_point_instancer_ids)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(demands)),
      myAnyLock(lock),
      myDemands(demands),
      myFindPointInstancerIds(find_point_instancer_ids),
      myAssumeWildcardsAroundPlainTokens(false),
      myCaseSensitive(true)
{
}

HUSD_FindPrims::HUSD_FindPrims(HUSD_AutoAnyLock &lock,
	const UT_StringRef &primpath,
	HUSD_PrimTraversalDemands demands)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(demands)),
      myAnyLock(lock),
      myDemands(demands),
      myFindPointInstancerIds(false),
      myAssumeWildcardsAroundPlainTokens(false),
      myCaseSensitive(true)
{
    HUSD_PathSet pathset;
    pathset.insert(primpath);
    addPaths(pathset);
}

HUSD_FindPrims::HUSD_FindPrims(HUSD_AutoAnyLock &lock,
	const UT_StringArray &primpaths,
	HUSD_PrimTraversalDemands demands)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(demands)),
      myAnyLock(lock),
      myDemands(demands),
      myFindPointInstancerIds(false),
      myAssumeWildcardsAroundPlainTokens(false),
      myCaseSensitive(true)
{
    HUSD_PathSet pathset;
    pathset.insert(primpaths);
    addPaths(pathset);
}

HUSD_FindPrims::HUSD_FindPrims(HUSD_AutoAnyLock &lock,
        const HUSD_PathSet &primpaths,
        HUSD_PrimTraversalDemands demands)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(demands)),
      myAnyLock(lock),
      myDemands(demands),
      myFindPointInstancerIds(false),
      myAssumeWildcardsAroundPlainTokens(false),
      myCaseSensitive(true)
{
    addPaths(primpaths);
}

HUSD_FindPrims::~HUSD_FindPrims()
{
}

const HUSD_PathSet &
HUSD_FindPrims::getExpandedPathSet() const
{
    if (myPrivate->myCollectionExpandedPathSet.empty() &&
	myPrivate->myAncestorPathSet.empty() &&
	myPrivate->myDescendantPathSet.empty())
	return myPrivate->myCollectionlessPathSet;
    else if (myPrivate->myCollectionExpandedPathSetCalculated)
	return myPrivate->myCollectionExpandedPathSetCache;

    myPrivate->myCollectionExpandedPathSetCache =
        myPrivate->myCollectionlessPathSet;
    myPrivate->myCollectionExpandedPathSetCache.insert(
	myPrivate->myCollectionExpandedPathSet);
    myPrivate->myCollectionExpandedPathSetCache.insert(
	myPrivate->myAncestorPathSet);
    myPrivate->myCollectionExpandedPathSetCache.insert(
	myPrivate->myDescendantPathSet);

    myPrivate->myCollectionExpandedPathSetCalculated = true;
    return myPrivate->myCollectionExpandedPathSetCache;
}

const HUSD_PathSet &
HUSD_FindPrims::getCollectionAwarePathSet() const
{
    if (myPrivate->myCollectionPathSet.empty() &&
	myPrivate->myAncestorPathSet.empty() &&
	myPrivate->myDescendantPathSet.empty())
	return myPrivate->myCollectionlessPathSet;
    else if (myPrivate->myCollectionAwarePathSetCalculated)
	return myPrivate->myCollectionAwarePathSetCache;

    myPrivate->myCollectionAwarePathSetCache =
        myPrivate->myCollectionlessPathSet;
    myPrivate->myCollectionAwarePathSetCache.insert(
	myPrivate->myCollectionPathSet);
    myPrivate->myCollectionAwarePathSetCache.insert(
	myPrivate->myAncestorPathSet);
    myPrivate->myCollectionAwarePathSetCache.insert(
	myPrivate->myDescendantPathSet);

    myPrivate->myCollectionAwarePathSetCalculated = true;
    return myPrivate->myCollectionAwarePathSetCache;
}

const HUSD_PathSet &
HUSD_FindPrims::getExcludedPathSet(bool skipdescendants) const
{
    int                  setidx = skipdescendants ? 1 : 0;
    if (myPrivate->myExcludedPathSetCalculated[setidx])
	return myPrivate->myExcludedPathSetCache[setidx];

    const SdfPathSet	&sdfpaths = getExpandedPathSet().sdfPathSet();
    auto		 indata = myAnyLock.constData();

    myPrivate->myExcludedPathSetCache[setidx].clear();
    if (indata && indata->isStageValid())
    {
	auto	 stage = indata->stage();
	auto	 range = myPrivate->getPrimRange(stage);

	for (auto iter = range.cbegin(); iter != range.cend(); ++iter)
	{
	    const SdfPath	&sdfpath = iter->GetPrimPath();

	    if (sdfpaths.find(sdfpath) != sdfpaths.end())
		continue;

	    if (myFindPointInstancerIds && UsdGeomPointInstancer(*iter))
	    {
		iter.PruneChildren();
		continue;
	    }

	    if (sdfpath == HUSDgetHoudiniLayerInfoSdfPath())
		continue;

	    myPrivate->myExcludedPathSetCache[setidx].
                sdfPathSet().emplace(sdfpath);
            if (skipdescendants)
                iter.PruneChildren();
	}
    }

    myPrivate->myExcludedPathSetCalculated[setidx] = true;
    return myPrivate->myExcludedPathSetCache[setidx];
}

const HUSD_PathSet &
HUSD_FindPrims::getMissingExplicitPathSet() const
{
    return myPrivate->myMissingExplicitPathSet;
}

bool
HUSD_FindPrims::getIsEmpty() const
{
    return getExpandedPathSet().empty();
}

void
HUSD_FindPrims::setTraversalDemands(HUSD_PrimTraversalDemands demands)
{
    myDemands = demands;
    myPrivate->myPredicate = HUSDgetUsdPrimPredicate(demands);
}

HUSD_PrimTraversalDemands
HUSD_FindPrims::traversalDemands() const
{
    return myDemands;
}

void
HUSD_FindPrims::setAssumeWildcardsAroundPlainTokens(bool assume)
{
    myAssumeWildcardsAroundPlainTokens = assume;
}

bool
HUSD_FindPrims::assumeWildcardsAroundPlainTokens() const
{
    return myAssumeWildcardsAroundPlainTokens;
}

void
HUSD_FindPrims::setCaseSensitive(bool casesensitive)
{
    myCaseSensitive = casesensitive;
}

bool
HUSD_FindPrims::caseSensitive() const
{
    return myCaseSensitive;
}

bool
HUSD_FindPrims::addPattern(const XUSD_PathPattern &path_pattern,
        int nodeid,
        bool track_missing_explicit_prims)
{
    auto	 indata = myAnyLock.constData();
    bool	 success = false;

    if (path_pattern.getPatternError())
    {
	myLastError = path_pattern.getPatternError();
	return false;
    }

    myPrivate->invalidateCaches();
    if (indata && indata->isStageValid())
    {
	auto                      stage = indata->stage();
	UT_StringArray            explicit_paths;
        XUSD_PerfMonAutoCookEvent perf(nodeid, "Primitive pattern evaluation");

	if (path_pattern.getExplicitList(explicit_paths))
	{
	    bool	 allow_instance_proxies = allowInstanceProxies();

	    // For a simple list of paths we don't need to traverse the whole
	    // stage. Just look for the specific paths in the list.
	    for (auto &&path : explicit_paths)
	    {
		SdfPath	 sdfpath(HUSDgetSdfPath(path));
		UsdPrim	 prim(stage->GetPrimAtPath(sdfpath));

		if (prim)
		{
		    if (sdfpath == HUSDgetHoudiniLayerInfoSdfPath())
			continue;

                    if (prim.IsInPrototype())
                        HUSD_ErrorScope::addWarning(
                            HUSD_ERR_IGNORING_PROTOTYPE,
                            path.c_str());
		    else if (allow_instance_proxies || !prim.IsInstanceProxy())
			myPrivate->myCollectionlessPathSet.
                            sdfPathSet().emplace(sdfpath);
		    else
			HUSD_ErrorScope::addWarning(
			    HUSD_ERR_IGNORING_INSTANCE_PROXY,
			    path.c_str());
		}
                else if (track_missing_explicit_prims)
                    myPrivate->myMissingExplicitPathSet.
                        sdfPathSet().emplace(sdfpath);
                else
                    HUSD_ErrorScope::addMessage(
                        HUSD_ERR_IGNORING_MISSING_EXPLICIT_PRIM,
                        path.c_str());
	    }
	    // Collections will have been parsed separately, and we can
	    // ask the XUSD_PathPattern for them explicitly.
	    path_pattern.getSpecialTokenPaths(
		myPrivate->myCollectionPathSet.sdfPathSet(),
		myPrivate->myCollectionExpandedPathSet.sdfPathSet(),
		myPrivate->myCollectionlessPathSet.sdfPathSet());
	}
	else
	{
	    // Anything more complicated than a flat list of paths means we
	    // need to traverse the stage.
            success = myPrivate->parallelFindPrims(
                stage, path_pattern, myPrivate->myCollectionlessPathSet);
            if (success)
                myPrivate->myTimeVarying |= path_pattern.getMayBeTimeVarying();
	}

	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::addPaths(const HUSD_PathSet &paths,
        bool track_missing_explicit_prims)
{
    auto	 indata = myAnyLock.constData();
    bool	 success = false;

    myPrivate->invalidateCaches();
    if (indata && indata->isStageValid())
    {
	auto		 stage = indata->stage();
	bool		 allow_instance_proxies = allowInstanceProxies();

	for (auto &&sdfpath : paths.sdfPathSet())
	{
            if (sdfpath.IsPropertyPath())
            {
                UsdCollectionAPI collection =
                    UsdCollectionAPI::GetCollection(stage, sdfpath);

                if (collection)
                {
                    SdfPathSet collectionset =
                        UsdCollectionAPI::ComputeIncludedPaths(
                            collection.ComputeMembershipQuery(),
                            stage, myPrivate->myPredicate);
                    myPrivate->myCollectionExpandedPathSet.sdfPathSet().
                        insert(collectionset.begin(), collectionset.end());
                    myPrivate->myCollectionPathSet.sdfPathSet().
                        emplace(sdfpath);
                }
            }
            else
            {
                UsdPrim prim(stage->GetPrimAtPath(sdfpath));

                if (prim)
                {
                    if (sdfpath == HUSDgetHoudiniLayerInfoSdfPath())
                        continue;

                    if (prim.IsInPrototype())
                        HUSD_ErrorScope::addWarning(
                            HUSD_ERR_IGNORING_PROTOTYPE,
                            HUSD_Path(sdfpath).pathStr().c_str());
                    else if (allow_instance_proxies || !prim.IsInstanceProxy())
                        myPrivate->myCollectionlessPathSet.
                            sdfPathSet().emplace(sdfpath);
                    else
                        HUSD_ErrorScope::addWarning(
                            HUSD_ERR_IGNORING_INSTANCE_PROXY,
                            HUSD_Path(sdfpath).pathStr().c_str());
                }
                else if (track_missing_explicit_prims)
                    myPrivate->myMissingExplicitPathSet.
                        sdfPathSet().emplace(sdfpath);
                else
                    HUSD_ErrorScope::addMessage(
                        HUSD_ERR_IGNORING_MISSING_EXPLICIT_PRIM,
                        HUSD_Path(sdfpath).pathStr().c_str());
            }
	}

	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::addPattern(const UT_StringRef &pattern,
	int nodeid,
	const HUSD_TimeCode &timecode,
        bool track_missing_explicit_prims)
{
    XUSD_PathPattern	 path_pattern(pattern, myAnyLock,
                                myDemands, myCaseSensitive,
                                myAssumeWildcardsAroundPlainTokens,
                                nodeid, timecode);

    return addPattern(path_pattern, nodeid, track_missing_explicit_prims);
}

bool
HUSD_FindPrims::addPrimitiveType(const UT_StringRef &primtype)
{
    auto	 indata = myAnyLock.constData();
    bool	 success = false;

    myPrivate->invalidateCaches();
    if (indata && indata->isStageValid())
    {
	std::string	 stdprimtype(primtype.toStdString());
	auto		 tfprimtype(TfType::FindByName(stdprimtype));
	auto		 stage = indata->stage();

        for (auto &&test_prim : myPrivate->getPrimRange(stage))
	{
	    const TfToken	&type_name = test_prim.GetTypeName();

	    if (!type_name.IsEmpty())
	    {
		if (PlugRegistry::FindDerivedTypeByName<UsdSchemaBase>(
			type_name).IsA(tfprimtype))
		    myPrivate->myCollectionlessPathSet.sdfPathSet().
                        emplace(test_prim.GetPrimPath());
	    }
	}

	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::addPrimitiveKind(const UT_StringRef &primkind)
{
    auto	 indata = myAnyLock.constData();
    bool	 success = false;

    myPrivate->invalidateCaches();
    if (indata && indata->isStageValid())
    {
	TfToken		 tfprimkind(primkind.toStdString());
	auto		 stage = indata->stage();

        for (auto &&test_prim : myPrivate->getPrimRange(stage))
	{
	    UsdModelAPI		 model(test_prim);
	    TfToken		 model_kind;

	    if (model.GetKind(&model_kind))
	    {
		if (KindRegistry::IsA(model_kind, tfprimkind))
		    myPrivate->myCollectionlessPathSet.sdfPathSet().
                        emplace(test_prim.GetPrimPath());
	    }
	}

	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::addPrimitivePurpose(const UT_StringRef &primpurpose)
{
    auto	 indata = myAnyLock.constData();
    bool	 success = false;

    myPrivate->invalidateCaches();
    if (indata && indata->isStageValid())
    {
	TfToken		 tfprimpurpose(primpurpose.toStdString());
	auto		 stage = indata->stage();

        for (auto &&test_prim : myPrivate->getPrimRange(stage))
	{
	    UsdGeomImageable	 imageable(test_prim);

	    if (imageable)
	    {
		if (imageable.ComputePurpose() == tfprimpurpose)
		    myPrivate->myCollectionlessPathSet.sdfPathSet().
                        emplace(test_prim.GetPrimPath());
	    }
	}

	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::addVexpression(const UT_StringRef &vexpression,
	int nodeid,
	const HUSD_TimeCode &timecode) const
{
    bool		success = false;

    myPrivate->invalidateCaches();

    HUSD_Cvex		cvex;
    cvex.setCwdNodeId( nodeid );
    cvex.setTimeCode( timecode );

    HUSD_CvexCode code( vexpression, /*is_cmd=*/ false );
    code.setReturnType( HUSD_CvexCode::ReturnType::BOOLEAN );

    UT_StringArray	paths;
    if (cvex.matchPrimitives(myAnyLock, paths, code, myDemands))
    {
	for(auto &&path : paths)
	    myPrivate->myCollectionlessPathSet.
                sdfPathSet().emplace(HUSDgetSdfPath(path));
	success = true;
    }
    myPrivate->myTimeVarying |= cvex.getIsTimeVarying();

    return success;
}

bool
HUSD_FindPrims::addBoundingBox(const UT_BoundingBox &bbox,
	const HUSD_TimeCode &t,
	const UT_StringArray &purposes,
	HUSD_FindPrims::BBoxContainment containment)
{
    GfRange3d		 boxrange(GusdUT_Gf::Cast(bbox.minvec()),
				GusdUT_Gf::Cast(bbox.maxvec()));
    TfTokenVector	 tfpurposes;
    UsdTimeCode		 usdtime(HUSDgetNonDefaultUsdTimeCode(t));
    auto		 indata = myAnyLock.constData();
    bool		 success = false;

    myPrivate->invalidateCaches();

    for (auto &&purpose : purposes)
	tfpurposes.push_back(TfToken(purpose.toStdString()));
    if (!myPrivate->myBBoxCache)
	myPrivate->myBBoxCache.reset(new UsdGeomBBoxCache(usdtime, tfpurposes));
    myPrivate->myBBoxCache->SetTime(usdtime);
    myPrivate->myBBoxCache->SetIncludedPurposes(tfpurposes);
    if (myFindPointInstancerIds)
	myPrivate->myPointInstancerIds.clear();

    if (indata && indata->isStageValid())
    {
	auto		 stage = indata->stage();
	UsdPrimRange	 range(myPrivate->getPrimRange(stage));

	for (auto iter = range.cbegin(); iter != range.cend(); ++iter)
	{
	    UsdGeomPointInstancer	 instancer(*iter);

	    // Don't process the prototypes contained by a point instancer.
	    if (instancer)
		iter.PruneChildren();

	    GfBBox3d		 primbounds;
	    GfRange3d		 primrange;

	    if (iter->GetPrimPath() == HUSDgetHoudiniLayerInfoSdfPath())
		continue;

	    primbounds = myPrivate->myBBoxCache->ComputeWorldBound(*iter);
	    primrange = primbounds.ComputeAlignedRange();
	    if (boxrange.IsInside(primrange))
	    {
		// This prim is fully contained, and therefore it's children
		// are too. No need to look at the children. Just add this
		// prim to the set.
		if (containment == BBOX_FULLY_INSIDE ||
		    containment == BBOX_PARTIALLY_INSIDE)
		{
		    if (myFindPointInstancerIds && instancer)
			addAllIds(instancer, usdtime,
			    myPrivate->myPointInstancerIds);
		    else
			myPrivate->myCollectionlessPathSet.sdfPathSet().
                            emplace(iter->GetPrimPath());
		}
		iter.PruneChildren();
	    }
	    else if (boxrange.IsOutside(primrange))
	    {
		// This prim is fully excluded, and therefore it's children
		// are too. Skip processing any children.
		if (containment == BBOX_FULLY_OUTSIDE ||
		    containment == BBOX_PARTIALLY_OUTSIDE)
		{
		    if (myFindPointInstancerIds && instancer)
			addAllIds(instancer, usdtime,
			    myPrivate->myPointInstancerIds);
		    else
			myPrivate->myCollectionlessPathSet.sdfPathSet().
                            emplace(iter->GetPrimPath());
		}
		iter.PruneChildren();
	    }
	    else
	    {
		// This prim is partially inside, partially outside. If we are
		// interested in partial containment, and this prim has no
		// children, then add this prim to the matching set.
		if (myFindPointInstancerIds && instancer)
		{
		    // We have to look at each instance to decide if it's in
		    // the bounding box.
		    addBoundIds(instancer,
			boxrange,
			usdtime,
			containment,
			*myPrivate->myBBoxCache,
			myPrivate->myPointInstancerIds);
		}
		else if ((containment == BBOX_PARTIALLY_INSIDE ||
		     containment == BBOX_PARTIALLY_OUTSIDE) &&
		    (iter->GetChildren().empty() || instancer))
		    myPrivate->myCollectionlessPathSet.sdfPathSet().
                        emplace(iter->GetPrimPath());
	    }

	    if (myFindPointInstancerIds && instancer)
	    {
		const SdfPath &sdfpath = instancer.GetPrim().GetPath();
		myPrivate->myPointInstancerIds[sdfpath.GetText()];
	    }
	}

	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::addDescendants()
{
    auto	 indata = myAnyLock.constData();
    bool	 success = false;

    if (indata && indata->isStageValid())
    {
	auto			 stage = indata->stage();
	const HUSD_PathSet	&inputset = getExpandedPathSet();

	for (auto &&inputpath : inputset.sdfPathSet())
	{
	    UsdPrimRange childrange = UsdPrimRange(
		    stage->GetPrimAtPath(inputpath), myPrivate->myPredicate);

	    for (auto &&childprim : childrange)
		myPrivate->myDescendantPathSet.sdfPathSet().
                    emplace(childprim.GetPath());
	}

	myPrivate->invalidateCaches();
	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::addAncestors()
{
    auto	 indata = myAnyLock.constData();
    bool	 success = false;

    if (indata && indata->isStageValid())
    {
	auto			 stage = indata->stage();
	const HUSD_PathSet	&inputset = getExpandedPathSet();

	for (auto &&inputpath : inputset.sdfPathSet())
	{
	    auto &&parentprim = stage->GetPrimAtPath(inputpath);
	    if (parentprim)
		while ((parentprim = parentprim.GetParent()).IsValid())
		    myPrivate->myAncestorPathSet.sdfPathSet().
			emplace(parentprim.GetPath());
	}

	myPrivate->invalidateCaches();
	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::allowInstanceProxies() const
{
    return myPrivate->myPredicate.IncludeInstanceProxiesInTraversal();
}

const UT_StringMap<UT_Int64Array> &
HUSD_FindPrims::getPointInstancerIds() const
{
    return myPrivate->myPointInstancerIds;
}

bool
HUSD_FindPrims::getExcludedPointInstancerIds(
	UT_StringMap<UT_Int64Array> &excludedids,
	const HUSD_TimeCode &timecode) const
{
    UsdTimeCode		 usdtime(HUSDgetNonDefaultUsdTimeCode(timecode));
    UT_Set<int64>	 included;
    auto		 indata = myAnyLock.constData();
    bool		 success = false;

    excludedids.clear();
    if (indata && indata->isStageValid())
    {
	auto	     stage = indata->stage();

	for (auto &&pair : myPrivate->myPointInstancerIds)
	{
	    included.clear();
	    included.insert(pair.second.begin(), pair.second.end());

	    UT_Int64Array &ids = excludedids[pair.first];

	    auto &&sdfpath = HUSDgetSdfPath(pair.first);
	    auto &&prim = stage->GetPrimAtPath(sdfpath);

	    UsdGeomPointInstancer    instancer(prim);
	    UsdAttribute	     ids_attr = instancer.GetIdsAttr();
	    VtArray<int64>	     ids_value;

	    if (ids_attr.Get(&ids_value, usdtime))
	    {
		for (int64 i = 0, n = ids_value.size(); i < n; i++)
		{
		    if (included.find(ids_value[i]) != included.end())
			continue;

		    ids.append(ids_value[i]);
		}
	    }
	    else
	    {
		auto		 protos_attr = instancer.GetProtoIndicesAttr();
		VtArray<int>	 protos_value;

		if (protos_attr.Get(&protos_value, usdtime))
		{
		    for (int64 i = 0, n = protos_value.size(); i < n; i++)
		    {
			if (included.find(i) != included.end())
			    continue;

			ids.append(i);
		    }
		}
	    }
	}
	success = true;
    }
    return success;
}

bool
HUSD_FindPrims::getFindPointInstancerIds() const
{
    return myFindPointInstancerIds;
}

bool
HUSD_FindPrims::getIsTimeVarying() const
{
    return myPrivate->myTimeVarying;
}

UT_StringHolder	 
HUSD_FindPrims::getSingleCollectionPath() const
{
    if (!myPrivate->myCollectionlessPathSet.empty())
	return UT_StringHolder();

    if (myPrivate->myCollectionPathSet.size() != 1)
	return UT_StringHolder();

    // This find-prim object contains just a single named collection.
    return myPrivate->myCollectionPathSet.getFirstPathAsString();
}

bool
HUSD_FindPrims::partitionShadePrims( 
	UT_StringArray &shadeprimpaths, UT_StringArray &geoprimpaths,
	bool include_bound_materials, bool use_shader_for_mat_with_no_inputs 
	) const
{
    auto indata = myAnyLock.constData();
    if (!indata || !indata->isStageValid())
	return false;

    auto stage = indata->stage();

    UT_StringArray primpaths;
    getExpandedPathSet().getPathsAsStrings(primpaths);

    for( auto &&primpath : primpaths )
    {
	auto &&prim = stage->GetPrimAtPath(HUSDgetSdfPath(primpath));
	UT_StringHolder primtype = prim.GetTypeName().GetString();

	// Check if prim is Material or Shader (ie, one of editable
	// shading primitives).
	if (primtype == HUSD_Constants::getMaterialPrimTypeName() ||
	    primtype == HUSD_Constants::getShaderPrimTypeName())
	{
	    shadeprimpaths.append(primpath);
	}
	else
	{
	    geoprimpaths.append(primpath);
	}

	// Note, currently this method is geared towards a workflow for
	// editing materials and shaders. To streamline that workflow,
	// we use certain heuristics to judge how editable the material is.
	// Eg, the workflow wants a list of shade prims (ie, mats or shaders)
	// whether specified directly or thru binding to a specified geo pirm.
	// But also, a material without inputs is not quite editable, so
	// we allow substituting such materials with a surface shader, which
	// should offer more input attributes for editing and customization.
	if( include_bound_materials )
	{
	    // Try resolving to a bound material.
	    UsdShadeMaterialBindingAPI api(prim);
	    auto material = api.ComputeBoundMaterial();
	    if( material )
	    {
		auto inputs = material.GetInterfaceInputs();
		if (inputs.size() <= 0 && use_shader_for_mat_with_no_inputs)
		{
		    // Mat has no input attribs to edit; surf shader is better.
		    auto shader = material.ComputeSurfaceSource();
		    if (shader) 
			shadeprimpaths.append(shader.GetPath().GetAsString());
		}
		else
		{
		    // There are input attribs to edit, so add material.
		    shadeprimpaths.append(material.GetPath().GetAsString());
		}
	    }
	}
    }

    return true;
}

UT_StringHolder
HUSD_FindPrims::getSharedRootPrim() const
{
    const HUSD_PathSet &pathset = getExpandedPathSet();
    SdfPath rootpath;

    if (pathset.empty())
        return UT_StringHolder();

    rootpath = *pathset.sdfPathSet().begin();
    for (auto &&path : pathset.sdfPathSet())
    {
        rootpath = rootpath.GetCommonPrefix(path);
        if (rootpath == SdfPath::AbsoluteRootPath())
            return UT_StringHolder();
    }

    return rootpath.GetString();
}

