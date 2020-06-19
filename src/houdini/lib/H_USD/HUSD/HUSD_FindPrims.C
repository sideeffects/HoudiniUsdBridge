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
#include "HUSD_Cvex.h"
#include "HUSD_CvexCode.h"
#include "HUSD_ErrorScope.h"
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
	  myExpandedPathSetCalculated(false),
	  myExcludedPathSetCalculated{ false, false },
	  myCollectionAwarePathSetCalculated(false),
	  myTimeVarying(false)
    { }

    void invalidateCaches()
    {
	myExpandedPathSetCalculated = false;
	myExcludedPathSetCalculated[0] = false;
	myExcludedPathSetCalculated[1] = false;
	myCollectionAwarePathSetCalculated = false;
    }
    void setBaseType(const UT_StringRef &base_type_name)
    {
	myBaseType = HUSDfindType(base_type_name);
	invalidateCaches();
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
            auto &task = *new(UT_Task::allocate_root())
                XUSD_FindPrimsTask(root, data, myPredicate, &pattern, nullptr);
            UT_Task::spawnRootAndWait(task);

            data.gatherPathsFromThreads(paths.sdfPathSet());
        }

        return true;
    }

    HUSD_PathSet			 myPathSet;
    HUSD_PathSet			 myCollectionPathSet;
    HUSD_PathSet			 myExpandedPathSet;
    HUSD_PathSet			 myAncestorPathSet;
    HUSD_PathSet			 myDescendantPathSet;
    HUSD_PathSet			 myExpandedPathSetCache;
    HUSD_PathSet			 myExcludedPathSetCache[2];
    HUSD_PathSet			 myCollectionAwarePathSetCache;
    UT_UniquePtr<UsdGeomBBoxCache>	 myBBoxCache;
    UT_StringMap<UT_Int64Array>		 myPointInstancerIds;
    Usd_PrimFlagsPredicate		 myPredicate;
    TfType				 myBaseType;
    bool				 myExpandedPathSetCalculated;
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
      myAssumeWildcardsAroundPlainTokens(false)
{
}

HUSD_FindPrims::HUSD_FindPrims(const HUSD_FindPrims &src)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(src.myDemands)),
      myAnyLock(src.myAnyLock),
      myDemands(src.myDemands),
      myFindPointInstancerIds(src.myFindPointInstancerIds),
      myAssumeWildcardsAroundPlainTokens(false)
{
    myPrivate->myBaseType = src.myPrivate->myBaseType;
    myPrivate->myPathSet = src.getExpandedPathSet();
}

HUSD_FindPrims::HUSD_FindPrims(HUSD_AutoAnyLock &lock,
	const UT_StringRef &primpath,
	HUSD_PrimTraversalDemands demands)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(demands)),
      myAnyLock(lock),
      myDemands(demands),
      myFindPointInstancerIds(false),
      myAssumeWildcardsAroundPlainTokens(false)
{
    auto	 indata(lock.constData());

    if (indata && indata->isStageValid())
    {
	auto	 stage = indata->stage();
	bool	 allow_instance_proxies = allowInstanceProxies();

	SdfPath	 sdfpath(HUSDgetSdfPath(primpath));
	UsdPrim	 prim(stage->GetPrimAtPath(sdfpath));

	if (prim)
	{
	    if (allow_instance_proxies || !prim.IsInstanceProxy())
		myPrivate->myPathSet.sdfPathSet().emplace(sdfpath);
	    else
		HUSD_ErrorScope::addWarning(
		    HUSD_ERR_IGNORING_INSTANCE_PROXY,
		    sdfpath.GetText());
	}
    }
    myPrivate->myExpandedPathSetCalculated = true;
    myPrivate->myCollectionAwarePathSetCalculated = true;
}

HUSD_FindPrims::HUSD_FindPrims(HUSD_AutoAnyLock &lock,
	const UT_StringArray &primpaths,
	HUSD_PrimTraversalDemands demands)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(demands)),
      myAnyLock(lock),
      myDemands(demands),
      myFindPointInstancerIds(false),
      myAssumeWildcardsAroundPlainTokens(false)
{
    auto	 indata(lock.constData());

    if (indata && indata->isStageValid())
    {
	auto		 stage = indata->stage();
	bool		 allow_instance_proxies = allowInstanceProxies();

	for (auto &&primpath : primpaths)
	{
	    SdfPath	 sdfpath(HUSDgetSdfPath(primpath));
	    UsdPrim	 prim(stage->GetPrimAtPath(sdfpath));

	    if (prim)
	    {
		if (allow_instance_proxies || !prim.IsInstanceProxy())
		    myPrivate->myPathSet.sdfPathSet().emplace(sdfpath);
		else
		    HUSD_ErrorScope::addWarning(
			HUSD_ERR_IGNORING_INSTANCE_PROXY,
			sdfpath.GetText());
	    }
	}
    }
    myPrivate->myExpandedPathSetCalculated = true;
    myPrivate->myCollectionAwarePathSetCalculated = true;
}

HUSD_FindPrims::HUSD_FindPrims(HUSD_AutoAnyLock &lock,
	const std::vector<std::string> &primpaths,
	HUSD_PrimTraversalDemands demands)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(demands)),
      myAnyLock(lock),
      myDemands(demands),
      myFindPointInstancerIds(false),
      myAssumeWildcardsAroundPlainTokens(false)
{
    auto	 indata(lock.constData());

    if (indata && indata->isStageValid())
    {
	auto		 stage = indata->stage();
	bool		 allow_instance_proxies = allowInstanceProxies();

	for (auto &&primpath : primpaths)
	{
	    SdfPath	 sdfpath(HUSDgetSdfPath(primpath));
	    UsdPrim	 prim(stage->GetPrimAtPath(sdfpath));

	    if (prim)
	    {
		if (allow_instance_proxies || !prim.IsInstanceProxy())
		    myPrivate->myPathSet.sdfPathSet().emplace(sdfpath);
		else
		    HUSD_ErrorScope::addWarning(
			HUSD_ERR_IGNORING_INSTANCE_PROXY,
			sdfpath.GetText());
	    }
	}
    }
    myPrivate->myExpandedPathSetCalculated = true;
    myPrivate->myCollectionAwarePathSetCalculated = true;
}

HUSD_FindPrims::HUSD_FindPrims(HUSD_AutoAnyLock &lock,
        const HUSD_PathSet &primpaths,
        HUSD_PrimTraversalDemands demands)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(demands)),
      myAnyLock(lock),
      myDemands(demands),
      myFindPointInstancerIds(false),
      myAssumeWildcardsAroundPlainTokens(false)
{
    auto	 indata(lock.constData());

    if (indata && indata->isStageValid())
    {
	auto		 stage = indata->stage();
	bool		 allow_instance_proxies = allowInstanceProxies();

	for (auto &&sdfpath : primpaths.sdfPathSet())
	{
	    UsdPrim	 prim(stage->GetPrimAtPath(sdfpath));

	    if (prim)
	    {
		if (allow_instance_proxies || !prim.IsInstanceProxy())
		    myPrivate->myPathSet.sdfPathSet().emplace(sdfpath);
		else
		    HUSD_ErrorScope::addWarning(
			HUSD_ERR_IGNORING_INSTANCE_PROXY,
			sdfpath.GetText());
	    }
	}
    }
    myPrivate->myExpandedPathSetCalculated = true;
    myPrivate->myCollectionAwarePathSetCalculated = true;
}

HUSD_FindPrims::~HUSD_FindPrims()
{
}

const HUSD_PathSet &
HUSD_FindPrims::getExpandedPathSet() const
{
    if (myPrivate->myExpandedPathSet.empty() &&
	myPrivate->myAncestorPathSet.empty() &&
	myPrivate->myDescendantPathSet.empty() &&
	myPrivate->myBaseType.IsUnknown())
	return myPrivate->myPathSet;
    else if (myPrivate->myExpandedPathSetCalculated)
	return myPrivate->myExpandedPathSetCache;

    myPrivate->myExpandedPathSetCache = myPrivate->myPathSet;
    myPrivate->myExpandedPathSetCache.insert(
	myPrivate->myExpandedPathSet);
    myPrivate->myExpandedPathSetCache.insert(
	myPrivate->myAncestorPathSet);
    myPrivate->myExpandedPathSetCache.insert(
	myPrivate->myDescendantPathSet);

    if (!myPrivate->myBaseType.IsUnknown())
    {
	auto		 indata = myAnyLock.constData();

	if (indata && indata->isStageValid())
	{
	    auto stage = indata->stage();
            SdfPathSet &paths =
                myPrivate->myExpandedPathSetCache.sdfPathSet();

	    for (auto it = paths.begin(); it != paths.end();)
	    {
		UsdPrim	 prim(stage->GetPrimAtPath(*it));

                if (prim && !HUSDisDerivedType(prim, myPrivate->myBaseType))
		    it = paths.erase(it);
		else
		    ++it;
	    }
	}
    }

    myPrivate->myExpandedPathSetCalculated = true;
    return myPrivate->myExpandedPathSetCache;
}

const HUSD_PathSet &
HUSD_FindPrims::getCollectionAwarePathSet() const
{
    if (myPrivate->myCollectionPathSet.empty() &&
	myPrivate->myAncestorPathSet.empty() &&
	myPrivate->myDescendantPathSet.empty() &&
	myPrivate->myBaseType.IsUnknown())
	return myPrivate->myPathSet;
    else if (myPrivate->myCollectionAwarePathSetCalculated)
	return myPrivate->myCollectionAwarePathSetCache;

    myPrivate->myCollectionAwarePathSetCache = myPrivate->myPathSet;
    myPrivate->myCollectionAwarePathSetCache.insert(
	myPrivate->myCollectionPathSet);
    myPrivate->myCollectionAwarePathSetCache.insert(
	myPrivate->myAncestorPathSet);
    myPrivate->myCollectionAwarePathSetCache.insert(
	myPrivate->myDescendantPathSet);

    if (!myPrivate->myBaseType.IsUnknown())
    {
	auto		 indata = myAnyLock.constData();

	if (indata && indata->isStageValid())
	{
	    auto stage = indata->stage();
            SdfPathSet &paths =
                myPrivate->myCollectionAwarePathSetCache.sdfPathSet();

	    for (auto it = paths.begin(); it != paths.end();)
	    {
		UsdPrim	 prim(stage->GetPrimAtPath(*it));

                if (prim && !HUSDisDerivedType(prim, myPrivate->myBaseType))
		    it = paths.erase(it);
		else
		    ++it;
	    }
	}
    }

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

	    if (!HUSDisDerivedType(*iter, myPrivate->myBaseType))
		continue;

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

bool
HUSD_FindPrims::getIsEmpty() const
{
    return getExpandedPathSet().empty();
}

void
HUSD_FindPrims::setBaseTypeName(const UT_StringRef &base_type_name)
{
    myPrivate->setBaseType(base_type_name);
}

void
HUSD_FindPrims::setTraversalDemands(HUSD_PrimTraversalDemands demands)
{
    myDemands = demands;
    myPrivate->myPredicate = HUSDgetUsdPrimPredicate(demands);
}

void
HUSD_FindPrims::setAssumeWildcardsAroundPlainTokens(bool assume)
{
    myAssumeWildcardsAroundPlainTokens = assume;
}

bool
HUSD_FindPrims::addPattern(const XUSD_PathPattern &path_pattern, int nodeid)
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

		    if (allow_instance_proxies || !prim.IsInstanceProxy())
			myPrivate->myPathSet.sdfPathSet().emplace(sdfpath);
		    else
			HUSD_ErrorScope::addWarning(
			    HUSD_ERR_IGNORING_INSTANCE_PROXY,
			    sdfpath.GetText());
		}
	    }
	    // Collections will have been parsed separately, and we can
	    // ask the XUSD_PathPattern for them explicitly.
	    path_pattern.getSpecialTokenPaths(
		myPrivate->myCollectionPathSet.sdfPathSet(),
		myPrivate->myExpandedPathSet.sdfPathSet());
	}
	else
	{
	    // Anything more complicated than a flat list of paths means we
	    // need to traverse the stage.
            success = myPrivate->parallelFindPrims(
                stage, path_pattern, myPrivate->myPathSet);
	}

	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::addPattern(const HUSD_PathSet &paths)
{
    UT_StringArray       path_tokens;

    for (auto path : paths.sdfPathSet())
        path_tokens.append(path.GetText());

    return addPattern(path_tokens);
}

bool
HUSD_FindPrims::addPattern(const UT_StringArray &pattern_tokens, int nodeid)
{
    XUSD_PathPattern	 path_pattern(pattern_tokens, myAnyLock,
                                myDemands, nodeid);

    path_pattern.setAssumeWildcardsAroundPlainTokens(
        myAssumeWildcardsAroundPlainTokens);

    return addPattern(path_pattern, nodeid);
}

bool
HUSD_FindPrims::addPattern(const UT_StringRef &pattern,
	int nodeid,
	const HUSD_TimeCode &timecode)
{
    XUSD_PathPattern	 path_pattern(pattern, myAnyLock,
                                myDemands, nodeid, timecode);

    path_pattern.setAssumeWildcardsAroundPlainTokens(
        myAssumeWildcardsAroundPlainTokens);

    return addPattern(path_pattern, nodeid);
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
		    myPrivate->myPathSet.sdfPathSet().
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
		    myPrivate->myPathSet.sdfPathSet().
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
		    myPrivate->myPathSet.sdfPathSet().
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
    // TODO: optimize by just recording the vexpression here, and then running
    //       HUSD_Cvex only once with all vexpressions "or'ed" together. 
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
	    myPrivate->myPathSet.sdfPathSet().emplace(HUSDgetSdfPath(path));
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
			myPrivate->myPathSet.sdfPathSet().
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
			myPrivate->myPathSet.sdfPathSet().
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
		    myPrivate->myPathSet.sdfPathSet().
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
    if (!myPrivate->myPathSet.empty())
	return UT_StringHolder();

    if (myPrivate->myCollectionPathSet.size() != 1)
	return UT_StringHolder();

    // This find-prim object contains just a single named collection.
    return myPrivate->myCollectionPathSet.getFirstPathAsString();
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

