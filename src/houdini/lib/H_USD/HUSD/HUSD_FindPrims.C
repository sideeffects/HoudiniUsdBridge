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
#include "HUSD_Path.h"
#include "HUSD_PathSet.h"
#include "HUSD_PerfMonAutoCookEvent.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_PathPattern.h"
#include "XUSD_Utils.h"
#include <OP/OP_Node.h>
#include <UT/UT_Array.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_Performance.h>
#include <UT/UT_String.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
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

#define HUSD_PATH_EXPR_AUTO_COLLECTION "pathexpr"

namespace
{
    class xusd_IdHolder
    {
    public:
        UT_Array<int64>	&myAvailableIds;
        std::set<int64>	&myMatchedIds;
    };

    void
    runVex(HUSD_AutoAnyLock &lock,
            const HUSD_TimeCode &timecode,
            const UT_StringRef &primpath,
            const UT_StringHolder &vexpr,
            xusd_IdHolder &ids,
            UT_String &error)
    {
        HUSD_Cvex	 cvex;
        HUSD_CvexCode	 cvexcode(vexpr, false);
        UT_ExintArray	 matched_instance_indices;

        cvex.setCwdNodeId(lock.dataHandle().nodeId());
        cvex.setTimeCode(timecode);
        cvexcode.setReturnType(HUSD_CvexCode::ReturnType::BOOLEAN);
        cvex.matchInstances(lock, matched_instance_indices,
            primpath, nullptr, cvexcode);
        for (auto &&id : matched_instance_indices)
            ids.myMatchedIds.insert(id);
    }

    void
    parseInstanceIdPattern(HUSD_AutoAnyLock &lock,
            const HUSD_TimeCode &timecode,
            const UT_StringRef &primpath,
            char *pattern,
            xusd_IdHolder &ids,
            UT_String &error)
    {
        static const char	*theNumerics = "0123456789.*:!-^";
        char			*start, *end, end_char;
        int			 len;

        // Skip over any whitespace.
        while (*pattern && (SYSisspace(*pattern) || *pattern == ','))
            pattern++;

        // Keep running through the pattern string until we hit the end.
        while (*pattern)
        {
            start = pattern;
            if (*pattern == '{')
            {
                int		 bracecount = 1;

                while (bracecount > 0 && *pattern)
                {
                    pattern++;
                    if (*pattern == '}')
                        bracecount--;
                    else if (*pattern == '{')
                        bracecount++;
                }
                end = pattern;

                if (!*pattern)
                {
                    error.harden("found unmatched open brace");
                    break;
                }

                // Get the string inside the braces, but without the braces.
                UT_StringHolder	 vexpr(start + 1,
                    (exint)(intptr_t)(end - start - 1));

                runVex(lock, timecode, primpath, vexpr, ids, error);
                if (error.isstring())
                    break;
            }
            else
            {
                // Find a chunk of numeric characters.
                len = strspn(start, theNumerics);
                if (!len)
                    break;
                end = start + len;

                UT_String	 token;

                end_char = *end;
                *end = '\0';
                int maxid = ids.myAvailableIds.size() > 0
                    ? ids.myAvailableIds.last() + 1
                    : 0;
                if (*start == '^')
                {
                    token = start+1;
                    token.traversePattern(maxid, &ids,
                        [](int num, int, void *data) {
                            xusd_IdHolder *ids = (xusd_IdHolder *)data;

                            if (ids->myAvailableIds.uniqueSortedFind(num) >= 0)
                                ids->myMatchedIds.erase(num);
                            return 1;
                        });
                }
                else
                {
                    token = start;
                    token.traversePattern(maxid, &ids,
                        [](int num, int, void *data) {
                            xusd_IdHolder *ids = (xusd_IdHolder *)data;

                            if (ids->myAvailableIds.uniqueSortedFind(num) >= 0)
                                ids->myMatchedIds.emplace(num);
                            return 1;
                        });
                }
                *end = end_char;
            }

            pattern = end;
            while (*pattern && (SYSisspace(*pattern) || *pattern == ','))
                pattern++;
        }
    }

    bool
    matchInstanceIds(HUSD_AutoAnyLock &lock,
            const UT_StringRef &pattern,
            const UsdGeomPointInstancer &instancer,
            const HUSD_TimeCode &timecode,
            UT_Array<int64> &matched_ids)
    {
        UT_Array<int64> availableids;
        HUSDgetPointInstancerIds(instancer.GetPrim(), timecode, availableids);
        if (availableids.size() == 0)
            return false;

        std::set<int64> matchedids;
        xusd_IdHolder   holder = { availableids, matchedids };
        UT_String       pat(pattern.c_str(), true);
        UT_String       error;
        int             vex_brace_count = 0;

        // Run over the string looking for ":/" outside the context of a
        // VEXpression. This indicates a break in a string like
        // "/instancer[26:/instancer/prototypes/foo]", where the part after
        // the ":" is the full path to the prototype within this instance
        // we are pointing at for handling nested instancing. But we don't
        // actually care about nested instancing here, and so this prototype
        // part has no meaning, so we can just strip it off to avoid messing
        // up our parsing.
        for (int i = 0; pat[i]; i++)
        {
            if (pat[i] == '{')
                vex_brace_count++;
            else if (pat[i] == '}')
                vex_brace_count--;
            else if (pat[i] == ':' && pat[i+1] == '/' && vex_brace_count == 0)
            {
                pat[i] = '\0';
                break;
            }
        }
        parseInstanceIdPattern(lock, timecode,
            instancer.GetPath().GetText(), pat, holder, error);
        if (error.isstring())
        {
            HUSD_ErrorScope::addError(
                HUSD_ERR_FAILED_TO_PARSE_PATTERN,
                error.c_str());
            return false;
        }

        for (auto &&id : matchedids)
            matched_ids.append(id);

        return (matched_ids.size() > 0);
    }
}

PXR_NAMESPACE_OPEN_SCOPE

// Shared entry point (declared in XUSD_PathPattern.h) so the path pattern
// matcher can resolve instance-id patterns against an instancer the same way
// the explicit-list fast path below does.
bool
XUSDmatchPointInstanceIds(HUSD_AutoAnyLock &lock,
        const UT_StringRef &pattern,
        const UsdPrim &instancer_prim,
        const HUSD_TimeCode &timecode,
        UT_Array<int64> &matched_ids)
{
    UsdGeomPointInstancer instancer(instancer_prim);

    if (!instancer)
        return false;

    return matchInstanceIds(lock, pattern, instancer, timecode, matched_ids);
}

PXR_NAMESPACE_CLOSE_SCOPE

class HUSD_FindPrims::husd_FindPrimsPrivate
{
public:
    husd_FindPrimsPrivate(HUSD_PrimTraversalDemands demands)
	: myPredicate(HUSDgetUsdPrimPredicate(demands)),
	  myCollectionExpandedPathSetCalculated(false),
	  myExcludedPathSetCalculated{ false, false },
	  myCollectionAwarePathSetCalculated(false),
          myExpandedOrMissingExplicitPathSetCalculated(false),
	  myTimeVarying(false),
          myAllowHoudiniLayerInfo(false)
    { }

    void invalidateCaches()
    {
	myCollectionExpandedPathSetCalculated = false;
	myExcludedPathSetCalculated[0] = false;
	myExcludedPathSetCalculated[1] = false;
	myCollectionAwarePathSetCalculated = false;
        myExpandedOrMissingExplicitPathSetCalculated = false;
    }

    UsdPrimRange getPrimRange(const UsdStageRefPtr &stage)
    {
        return stage->Traverse(myPredicate);
    }

    bool parallelFindPrims(HUSD_AutoAnyLock &lock,
            const XUSD_PathPattern &pattern,
            HUSD_PathSet &paths,
            UT_StringMap<UT_Array<int64>> *instance_ids)
    {
        UsdPrim root = lock.constData()->stage()->GetPseudoRoot();

        if (root)
        {
            XUSD_FindPrimPathsTaskData data(pattern.timeCode());
            data.setCollectInstanceIds(instance_ids != nullptr);
            data.setAllowHoudiniLayerInfo(myAllowHoudiniLayerInfo);
            XUSDfindPrims(root, data, myPredicate, &pattern);
            data.gatherDataFromThreads(paths.sdfPathSet(), instance_ids);
            if (instance_ids)
                resolveInstanceIds();
        }

        return true;
    }

    void resolveInstanceIds()
    {
        // Instance ids have already been collected during traversal via the
        // match payload (which applies the pattern's set operators - union,
        // intersection, difference - to each instancer's instance set), or
        // inline by the explicit-list fast path. All that remains is to
        // finalize the result: sort each instancer's id list, and remove the
        // instancer prims from the regular path set, since matched instancers
        // are represented by their instance id sets rather than as whole prims.
        for (auto &&instit : myPointInstancerIds)
            instit.second.sortAndRemoveDuplicates();
        for (auto &&instit : myPointInstancerIds)
            myCollectionlessPathSet.sdfPathSet().erase(
                HUSDgetSdfPath(instit.first));
    }

    HUSD_PathSet                   myCollectionlessPathSet;
    HUSD_PathSet                   myCollectionPathSet;
    HUSD_PathSet                   myCollectionExpandedPathSet;
    HUSD_PathSet                   myAncestorPathSet;
    HUSD_PathSet                   myDescendantPathSet;
    HUSD_PathSet                   myCollectionExpandedPathSetCache;
    HUSD_PathSet                   myExcludedPathSetCache[2];
    HUSD_PathSet                   myCollectionAwarePathSetCache;
    HUSD_PathSet                   myMissingExplicitPathSet;
    HUSD_PathSet                   myExpandedOrMissingExplicitPathSet;
    UT_StringMap<UT_Array<int64>>  myPointInstancerIds;
    Usd_PrimFlagsPredicate         myPredicate;
    bool                           myCollectionExpandedPathSetCalculated;
    bool                           myExcludedPathSetCalculated[2];
    bool                           myCollectionAwarePathSetCalculated;
    bool                           myExpandedOrMissingExplicitPathSetCalculated;
    bool                           myTimeVarying;
    bool                           myAllowHoudiniLayerInfo;
};

HUSD_FindPrims::HUSD_FindPrims(HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
	bool find_point_instancer_ids)
    : myPrivate(new HUSD_FindPrims::husd_FindPrimsPrivate(demands)),
      myAnyLock(lock),
      myDemands(demands),
      myFindPointInstancerIds(find_point_instancer_ids),
      myAssumeWildcardsAroundPlainTokens(false),
      myTrackMissingExplicitPrimitives(false),
      myWarnMissingExplicitPrimitives(true),
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
      myTrackMissingExplicitPrimitives(false),
      myWarnMissingExplicitPrimitives(true),
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
      myTrackMissingExplicitPrimitives(false),
      myWarnMissingExplicitPrimitives(true),
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
      myTrackMissingExplicitPrimitives(false),
      myWarnMissingExplicitPrimitives(true),
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

	    if (sdfpath == HUSDgetHoudiniLayerInfoSdfPath() &&
                !allowHoudiniLayerInfo())
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

const HUSD_PathSet &
HUSD_FindPrims::getExpandedOrMissingExplicitPathSet() const
{
    if (!myTrackMissingExplicitPrimitives ||
        myPrivate->myMissingExplicitPathSet.empty())
        return getExpandedPathSet();
    if (myPrivate->myExpandedOrMissingExplicitPathSetCalculated)
        return myPrivate->myExpandedOrMissingExplicitPathSet;

    myPrivate->myExpandedOrMissingExplicitPathSet = getExpandedPathSet();
    myPrivate->myExpandedOrMissingExplicitPathSet.insert(
        myPrivate->myMissingExplicitPathSet);
    myPrivate->myExpandedOrMissingExplicitPathSetCalculated = true;

    return myPrivate->myExpandedOrMissingExplicitPathSet;
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
HUSD_FindPrims::setTrackMissingExplicitPrimitives(bool track_missing)
{
    myTrackMissingExplicitPrimitives = track_missing;
}

bool
HUSD_FindPrims::trackMissingExplicitPrimitives() const
{
    return myTrackMissingExplicitPrimitives;
}

void
HUSD_FindPrims::setWarnMissingExplicitPrimitives(bool warn_missing)
{
    myWarnMissingExplicitPrimitives = warn_missing;
}

bool
HUSD_FindPrims::warnMissingExplicitPrimitives() const
{
    return myWarnMissingExplicitPrimitives;
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

void
HUSD_FindPrims::setFindPointInstancerIds(bool find_instancer_ids)
{
    myFindPointInstancerIds = find_instancer_ids;
}

bool
HUSD_FindPrims::findPointInstancerIds() const
{
    return myFindPointInstancerIds;
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
        UT_StringArray            instance_patterns;
        HUSD_PerfMonAutoCookEvent perf("Primitive pattern evaluation");

        if (path_pattern.getExplicitListWithInstanceIds(
                explicit_paths, instance_patterns))
        {
            bool allow_instance_proxies = allowInstanceProxies();

            for (exint idx = 0; idx < explicit_paths.size(); idx++)
            {
                auto &&path = explicit_paths(idx);
                SdfPath sdfpath(HUSDgetSdfPath(path));
                UsdPrim prim(stage->GetPrimAtPath(sdfpath));

                if (prim)
                {
                    if (sdfpath == HUSDgetHoudiniLayerInfoSdfPath() &&
                        !allowHoudiniLayerInfo())
                        continue;

                    if (prim.IsInPrototype())
                    {
                        HUSD_ErrorScope::addWarning(
                            HUSD_ERR_IGNORING_PROTOTYPE,
                            path.c_str());
                        continue;
                    }

                    // Skip instance proxies if they aren't allowed, for both
                    // instance and prim path matching.
                    if (allow_instance_proxies || !prim.IsInstanceProxy())
                    {
                        if (myFindPointInstancerIds &&
                            path_pattern.getAllowInstanceIndices())
                        {
                            UsdGeomPointInstancer instancer(prim);
                            if (instancer)
                            {
                                // Resolve the selected instances: the ids named
                                // by the trailing [...] pattern, or all of the
                                // instancer's instances when no pattern is
                                // given (a whole-instancer selection). The ids
                                // are sorted and de-duplicated later by
                                // resolveInstanceIds(). The map entry is created
                                // even when empty, so callers can tell that the
                                // instancer was explicitly targeted.
                                UT_Array<int64> ids;
                                if (instance_patterns(idx).isstring())
                                    matchInstanceIds(myAnyLock,
                                        instance_patterns(idx),
                                        instancer,
                                        path_pattern.timeCode(),
                                        ids);
                                else
                                    HUSDgetPointInstancerIds(prim,
                                        path_pattern.timeCode(), ids);
                                myPrivate->myPointInstancerIds[path].
                                    concat(ids);
                                continue;
                            }
                        }

                        myPrivate->myCollectionlessPathSet.
                            sdfPathSet().emplace(sdfpath);
                    }
                    else
                        HUSD_ErrorScope::addWarning(
                            HUSD_ERR_IGNORING_INSTANCE_PROXY,
                            path.c_str());
                }
                else if (myTrackMissingExplicitPrimitives)
                {
                    myPrivate->myMissingExplicitPathSet.
                        sdfPathSet().emplace(sdfpath);
                    if (myWarnMissingExplicitPrimitives)
                        HUSD_ErrorScope::addMessage(
                            HUSD_ERR_TARGETED_MISSING_EXPLICIT_PRIM,
                            path.c_str());
                }
                else if (myWarnMissingExplicitPrimitives)
                    HUSD_ErrorScope::addWarning(
                        HUSD_ERR_IGNORING_MISSING_EXPLICIT_PRIM,
                        path.c_str());
            }
            // Get the prim paths matching special tokens (auto-collections).
            path_pattern.getSpecialTokenPaths(
                myPrivate->myCollectionPathSet.sdfPathSet(),
                myPrivate->myCollectionExpandedPathSet.sdfPathSet(),
                myPrivate->myCollectionlessPathSet.sdfPathSet());
            // Gather instances matched by non-random-access auto collections.
            // These precompute their matched instances into each token's
            // myMatchedInstanceIds, but the explicit-list path does not run the
            // traversal that would otherwise surface them. An explicit list is
            // always union-only (it contains only additive operations), so it
            // is correct to simply union the per-token instance sets here.
            if (myFindPointInstancerIds)
            {
                for (auto &&token : path_pattern.getTokens())
                {
                    if (!token.myIsSpecialToken || !token.mySpecialTokenDataPtr)
                        continue;

                    auto *data = static_cast<const XUSD_SpecialTokenData *>(
                        token.mySpecialTokenDataPtr.get());
                    if (!data)
                        continue;

                    for (auto &&entry : data->myMatchedInstanceIds)
                        if (!entry.second.isEmpty())
                            myPrivate->myPointInstancerIds[entry.first].
                                concat(entry.second);
                }
            }
            // Finalize the collected instance ids (sort, and remove instancer
            // prims from the regular path set).
            if (myFindPointInstancerIds)
                myPrivate->resolveInstanceIds();

            success = true;
        }
        else
        {
            success = myPrivate->parallelFindPrims(
                myAnyLock, path_pattern,
                myPrivate->myCollectionlessPathSet,
                myFindPointInstancerIds
                    ? &myPrivate->myPointInstancerIds
                    : nullptr);
        }

        // Note that `bool(getExplicitList(...)) == true` does not specifically
        // mean that the user provided an explicit list of paths.
        // This also can be `true` when there is a `XUSD_AutoCollection` which
        // is not random-access.
        // As such, it's important to check for time variability in *all* cases.
        if (success)
            myPrivate->myTimeVarying |= path_pattern.getMayBeTimeVarying();
    }

    return success;
}

bool
HUSD_FindPrims::addPaths(const HUSD_PathSet &paths)
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
                    if (sdfpath == HUSDgetHoudiniLayerInfoSdfPath() &&
                        !allowHoudiniLayerInfo())
                        continue;

                    if (prim.IsInPrototype())
                        HUSD_ErrorScope::addWarning(
                            HUSD_ERR_IGNORING_PROTOTYPE,
                            sdfpath.GetAsString().c_str());
                    else if (allow_instance_proxies || !prim.IsInstanceProxy())
                        myPrivate->myCollectionlessPathSet.
                            sdfPathSet().emplace(sdfpath);
                    else
                        HUSD_ErrorScope::addWarning(
                            HUSD_ERR_IGNORING_INSTANCE_PROXY,
                            sdfpath.GetAsString().c_str());
                }
                else if (myTrackMissingExplicitPrimitives)
                {
                    myPrivate->myMissingExplicitPathSet.
                        sdfPathSet().emplace(sdfpath);
                    if (myWarnMissingExplicitPrimitives)
                        HUSD_ErrorScope::addMessage(
                            HUSD_ERR_TARGETED_MISSING_EXPLICIT_PRIM,
                            sdfpath.GetAsString().c_str());
                }
                else if (myWarnMissingExplicitPrimitives)
                    HUSD_ErrorScope::addWarning(
                        HUSD_ERR_IGNORING_MISSING_EXPLICIT_PRIM,
                        sdfpath.GetAsString().c_str());
            }
	}

	success = true;
    }

    return success;
}

bool
HUSD_FindPrims::addPattern(const UT_StringRef &pattern,
	int nodeid,
	const HUSD_TimeCode &timecode)
{
    XUSD_PathPattern	 path_pattern(pattern, myAnyLock,
                                myDemands, myCaseSensitive,
                                myAssumeWildcardsAroundPlainTokens,
                                myFindPointInstancerIds,
                                nodeid, timecode);

    return addPattern(path_pattern, nodeid);
}

bool
HUSD_FindPrims::addPathExpression(const UT_StringRef &path_expr)
{
    UT_StringHolder      pattern;
    primPatternFromPathExpression(path_expr, pattern);

    // Because we are just using the "pathexpr" auto collection, we know that
    // the result does not depend on the node id, and is not time varying. So
    // we can evaluate with an invalid node id, and the default time code.
    XUSD_PathPattern	 path_pattern(pattern, myAnyLock,
        myDemands, myCaseSensitive,
        myAssumeWildcardsAroundPlainTokens,
        myFindPointInstancerIds,
        OP_INVALID_NODE_ID, HUSD_TimeCode());

    return addPattern(path_pattern, OP_INVALID_NODE_ID);
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

void
HUSD_FindPrims::setAllowHoudiniLayerInfo(bool allow)
{
    myPrivate->myAllowHoudiniLayerInfo = allow;
}

bool
HUSD_FindPrims::allowHoudiniLayerInfo() const
{
    return myPrivate->myAllowHoudiniLayerInfo;
}

const UT_StringMap<UT_Array<int64>> &
HUSD_FindPrims::getPointInstancerIds() const
{
    return myPrivate->myPointInstancerIds;
}

bool
HUSD_FindPrims::getExcludedPointInstancerIds(
	UT_StringMap<UT_Array<int64>> &excludedids,
	const HUSD_TimeCode &timecode) const
{
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

	    UT_Array<int64> &ids = excludedids[pair.first];
	    auto &&sdfpath = HUSDgetSdfPath(pair.first);
	    auto &&prim = stage->GetPrimAtPath(sdfpath);
	    UT_Array<int64> allids;
	    if (HUSDgetPointInstancerIds(prim, timecode, allids))
	    {
	        for (int64 i = 0, n = allids.size(); i < n; i++)
	        {
	            if (included.find(allids[i]) != included.end())
	                continue;

	            ids.append(allids[i]);
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

bool
HUSD_FindPrims::primPatternFromPathExpression(
        const UT_StringRef &path_expr,
        UT_StringHolder &pattern)
{
    // The input and output parameters may be the same string, so build
    // the pattern in a separate buffer;
    UT_WorkBuffer pattern_buf;
    pattern_buf.sprintf("%%" HUSD_PATH_EXPR_AUTO_COLLECTION "(%s)",
        path_expr.c_str());
    pattern = pattern_buf;
    return true;
}

bool
HUSD_FindPrims::pathExpressionFromPrimPattern(
        const UT_StringRef &pattern,
        UT_StringHolder &path_expr)
{
    // The input and output parameters may be the same string, so create
    // the path expression in a separate string;
    UT_String pattern_str(pattern.c_str());
    pattern_str.trimBoundingSpace();
    if (pattern_str.startsWith("%" HUSD_PATH_EXPR_AUTO_COLLECTION "("))
    {
        if (pattern_str.endsWith(")"))
        {
            UT_String path_expr_str;
            int prefix_len = strlen(HUSD_PATH_EXPR_AUTO_COLLECTION) + 2;
            int paren_depth = 0;
            pattern_str.substr(path_expr_str, prefix_len,
                pattern_str.length() - prefix_len - 1);
            for (int i = 0; path_expr_str[i]; i++)
            {
                if (path_expr_str[i] == '(')
                    paren_depth++;
                else if (path_expr_str[i] == ')')
                {
                    paren_depth--;
                    if (paren_depth < 0)
                        break;
                }
            }
            if (paren_depth == 0)
            {
                path_expr = path_expr_str;
                return true;
            }
        }
    }

    return false;
}
