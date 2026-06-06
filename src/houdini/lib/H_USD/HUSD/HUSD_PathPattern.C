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

#include "HUSD_Constants.h"
#include "HUSD_Cvex.h"
#include "HUSD_CvexCode.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_Path.h"
#include "HUSD_PathPattern.h"
#include "HUSD_PerfMonAutoCookEvent.h"
#include "HUSD_Preferences.h"
#include "XUSD_AutoCollection.h"
#include "XUSD_Data.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_PathPattern.h"
#include "XUSD_Utils.h"
#include <UT/UT_Function.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_String.h>
#include <UT/UT_StringSet.h>
#include <UT/UT_WorkArgs.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/pointInstancer.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    void
    getAncestors(const UsdStageRefPtr &stage,
            const Usd_PrimFlagsPredicate &predicate,
            XUSD_PathSet &origpaths,
            XUSD_PathSet &newpaths)
    {
        for (auto &&origpath : origpaths)
        {
            auto parentpath = origpath.GetParentPath();

            while (!parentpath.IsEmpty())
            {
                if (newpaths.count(parentpath) > 0)
                    break;
                if (origpaths.count(parentpath) == 0)
                {
                    // The prim must match the predicate. If it doesn't, add
                    // the path to the "origpaths" set so that we don't have
                    // to evaluate the predicate on this path ever again.
                    if (predicate(stage->GetPrimAtPath(parentpath)))
                        newpaths.insert(parentpath);
                    else
                        origpaths.insert(parentpath);
                }
                parentpath = parentpath.GetParentPath();
            }
        }
    }

    void
    getDescendants(const UsdStageRefPtr &stage,
            const Usd_PrimFlagsPredicate &predicate,
            XUSD_PathSet &origpaths,
            XUSD_PathSet &newpaths)
    {
        for (auto &&origpath : origpaths)
        {
            UsdPrim prim = stage->GetPrimAtPath(origpath);

            if (prim)
            {
                for (auto &&descendant : prim.GetFilteredDescendants(predicate))
                {
                    SdfPath descendantpath = descendant.GetPath();

                    if (origpaths.count(descendantpath) > 0)
                        break;
                    newpaths.insert(descendantpath);
                }
            }
        }
    }

    void
    getAncestorsAndDescendants(const UsdStageRefPtr &stage,
            const Usd_PrimFlagsPredicate &predicate,
            XUSD_PathSet &origpaths,
            XUSD_PathSet &newpaths)
    {
        getDescendants(stage, predicate, origpaths, newpaths);
        getAncestors(stage, predicate, origpaths, newpaths);
    }

    typedef UT_Function<void (const UsdStageRefPtr &stage,
                              const Usd_PrimFlagsPredicate &predicate,
                              XUSD_PathSet &origpaths,
                              XUSD_PathSet &newpaths)> PrecedingGroupFn;
    class PrecedingGroupOperator
    {
    public:
        PrecedingGroupFn     myFunction;
        bool                 myUsePermissivePredicate;
    };

    const char *theCollectionSeparator = ".collection:";

    UT_Map<UT_StringHolder, PrecedingGroupOperator> thePrecedingGroupMap({
        { UT_StringHolder("<<"), { getAncestors, true } },
        { UT_StringHolder(">>"), { getDescendants, false } },
        { UT_StringHolder("<<>>"), { getAncestorsAndDescendants, true } }
    });

    UsdCollectionAPI
    husdGetCollection(const UsdStageRefPtr &stage,
        const UT_StringRef &identifier,
        SdfPath *collection_path)
    {
        SdfPath		 sdfpath;
        TfToken		 collection_name;
        UsdCollectionAPI	 collection;

        if (SdfPath::IsValidPathString(identifier.toStdString()))
            sdfpath = HUSDgetSdfPath(identifier);

        if (!UsdCollectionAPI::IsCollectionAPIPath(sdfpath, &collection_name))
        {
            UT_String	 idstr = identifier.c_str();
            UT_String	 prim_part;
            UT_String	 collection_part;

            idstr.splitPath(prim_part, collection_part);
            idstr = prim_part;
            idstr += theCollectionSeparator;
            idstr += collection_part;

            sdfpath = HUSDgetSdfPath(idstr);
        }

        collection = UsdCollectionAPI::GetCollection(stage, sdfpath);
        if (collection)
            *collection_path = sdfpath;

        return collection;
    }

    void
    husdMakeCollectionsPattern(UT_String &pattern, UT_String &secondpattern)
    {
        // If there is a "." or ":" in the path, assume the user is specifying
        // the collections pattern in a form that expects the ".collection:"
        // chunk in the middle.
        if (!pattern.findChar(".:"))
        {
            char		*last_slash = pattern.lastChar('/');

            // There should always be a slash in the pattern at this point.
            if (last_slash && last_slash > pattern.c_str())
            {
                const char *last_doublestar =
                    UT_String(last_slash).fcontain("**");

                if (last_doublestar)
                {
                    // If the pattern has a "**" after the last slash, we need
                    // two patterns to represent this faithfully. One is the
                    // pattern as provided, to match any child prims
                    // recursively. The other is to match any collections on
                    // the prim that appears before the last slash.
                    secondpattern = pattern;
                    secondpattern.replace(
                        (intptr_t)(last_slash - secondpattern.c_str()), 1,
                        theCollectionSeparator);
                }
                else
                {
                    // We have a slash, but no "**" after the last slash. This
                    // means the last slash is really a substitute for the
                    // collection separator. Do the replacement.
                    pattern.replace(
                        (intptr_t)(last_slash - pattern.c_str()), 1,
                        theCollectionSeparator);
                }
            }
        }
    }
}

// Clone-only constructor: no lock/time code, so this pattern is for boolean
// path matching only and must not collect point-instance ids. See the header.
HUSD_PathPattern::HUSD_PathPattern(bool case_sensitive,
        bool assume_wildcards,
        bool allow_instance_indices)
    : UT_PathPattern(case_sensitive, assume_wildcards, allow_instance_indices),
      myLock(nullptr)
{
}

HUSD_PathPattern::HUSD_PathPattern(const UT_StringRef &pattern,
	HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
        bool case_sensitive,
        bool assume_wildcards,
        bool allow_instance_indices,
	int nodeid,
	const HUSD_TimeCode &timecode)
    : UT_PathPattern(pattern, case_sensitive, assume_wildcards, allow_instance_indices),
      myLock(&lock),
      myMatchTimeCode(timecode)
{
    HUSD_PerfMonAutoCookEvent    perf("Primitive pattern evaluation");
    UT_AutoInterrupt             boss("Primitive pattern evaluation");

    initializeSpecialTokens(lock, demands, nodeid, timecode, boss);

    if (boss.wasInterrupted())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_PATTERN_INTERRUPTED);
        patternInterrupted();
    }
}

HUSD_PathPattern::~HUSD_PathPattern()
{
}

UT_PathPattern *
HUSD_PathPattern::createEmptyClone() const
{
    return new XUSD_PathPattern(getCaseSensitive(),
        getAssumeWildcardsAroundPlainTokens(),
        getAllowInstanceIndices());
}

void
HUSD_PathPattern::initializeSpecialTokens(HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
	int nodeid,
	const HUSD_TimeCode &timecode,
        UT_AutoInterrupt &boss)
{
    auto		 indata(lock.constData());

    if (indata && indata->isStageValid())
    {
	UT_StringArray				 preceding_group_tokens;
	UT_Array<XUSD_SpecialTokenData *>	 preceding_group_data;
        UT_IntArray                              preceding_group_token_indices;
	UT_Array<XUSD_SpecialTokenData *>	 auto_collection_data;
	UT_StringArray				 collection_tokens;
	UT_Array<XUSD_SpecialTokenData *>	 collection_data;
	UT_StringArray				 collection_pm_tokens;
	UT_Array<XUSD_SpecialTokenData *>	 collection_pm_data;
	UT_StringArray				 vex_tokens;
	UT_Array<XUSD_SpecialTokenData *>	 vex_data;
        UT_IntArray                              vex_token_indices;
	bool					 retest_for_wildcards = false;

	for (int tokenidx = 0, n = myTokens.size(); tokenidx < n; ++tokenidx)
	{
            auto &token = myTokens(tokenidx);

	    if (thePrecedingGroupMap.contains(token.myString))
            {
		XUSD_SpecialTokenData	*data(new XUSD_SpecialTokenData());

		token.myIsSpecialToken = true;
		token.mySpecialTokenDataPtr.reset(data);
                preceding_group_tokens.append(token.myString);
                preceding_group_data.append(data);
                preceding_group_token_indices.append(tokenidx);
            }
            else if (token.myString.startsWith("{"))
	    {
		// A VEXpression embedded into the pattern as a token
		// surrounded by curly braces.
		XUSD_SpecialTokenData	*data(new XUSD_SpecialTokenData());
		UT_String		 vex(token.myString);

		token.myIsSpecialToken = true;
		token.mySpecialTokenDataPtr.reset(data);
		// Wildcards might mean anything within a VEXpression.
		if (token.myDoPathMatching)
		{
		    token.myDoPathMatching = false;
		    retest_for_wildcards = true;
		}

		// Remove the opening and closing braces, which will always
		// be the first and last characters in the token. Then trim
		// white space off both ends, just to make the expression as
		// clean as possible.
		vex.eraseHead(1);
		vex.eraseTail(1);
		vex.trimBoundingSpace();
		vex_tokens.append(vex);
		vex_data.append(data);
                vex_token_indices.append(tokenidx);
	    }
	    else if (token.myString.startsWith("%") &&
                     XUSD_AutoCollection::canCreateAutoCollection(
                        token.myString.c_str()+1))
            {
		XUSD_SpecialTokenData	*data(new XUSD_SpecialTokenData());

		token.myIsSpecialToken = true;
		token.mySpecialTokenDataPtr.reset(data);
                // Skip over the "%", which isn't part of the auto
                // collection token, just an indicator that what follows
                // may be an auto collection token.
                data->myRandomAccessAutoCollection.reset(
                    XUSD_AutoCollection::create(token.myString.c_str()+1,
                        lock, demands, nodeid, timecode));
                // We may get back an invalid collection, in which case this
                // special token should act like it isn't there.
                if (data->myRandomAccessAutoCollection)
                {
                    // Auto collections can control whether or not they want to
                    // be part of a standard full traversal.
                    if (token.myDoPathMatching !=
                            data->myRandomAccessAutoCollection->randomAccess())
                    {
                        token.myDoPathMatching =
                            data->myRandomAccessAutoCollection->randomAccess();
                        retest_for_wildcards = true;
                    }

                    UT_StringHolder error = data->
                        myRandomAccessAutoCollection->getTokenParsingError();

                    if (error.isstring())
                    {
                        UT_WorkBuffer buf;

                        buf.sprintf("Error parsing auto collection '%s': %s",
                            token.myString.c_str()+1, error.c_str());
                        HUSD_ErrorScope::addWarning(
                            HUSD_ERR_STRING, buf.buffer());
                    }

                    auto_collection_data.append(data);
                }
                else
                {
                    HUSD_ErrorScope::addWarning(
                        HUSD_ERR_UNKNOWN_AUTO_COLLECTION,
                        token.myString.c_str());
                    if (token.myDoPathMatching)
                    {
                        token.myDoPathMatching = false;
                        retest_for_wildcards = true;
                    }
                    data->myInitialized = true;
                }
            }
	    else if (token.myString.startsWith("%") ||
		     token.myString.findCharIndex(".:") > 0)
	    {
		XUSD_SpecialTokenData	*data(new XUSD_SpecialTokenData());
		UT_String		 secondpattern;
		UT_String		 path;

		token.myIsSpecialToken = true;
		token.mySpecialTokenDataPtr.reset(data);
		// Skip over the "%" character, if we start with one.
		if (token.myString.startsWith("%"))
		{
		    path = token.myString.c_str()+1;
		    // If we aren't given an absolute path after the "%", then
		    // assume the path is relative to "/collections", our
		    // default prim for authoring collections.
		    if (!path.startsWith("/"))
		    {
                        // If the character after the "%" is a "*" (but not
                        // "**"), append a second "*" so we match against
                        // "/collections/**path", since a starting "*" is
                        // equivalent to a "**" in path matching.
                        if (path.startsWith("*") && !path.startsWith("**"))
                            path.insert(0, "/*");
                        else
                            path.insert(0, "/");
			path.insert(0,
			    HUSD_Preferences::defaultCollectionsPrimPath());
		    }
		    // Redo the test for whether we need path matching from
		    // UT_PathPatter::init, in case the "%" made that function
		    // think that we needed to do path matching.
		    token.myDoPathMatching = (path.findChar("*?[]") != nullptr);
		    retest_for_wildcards = true;
		    husdMakeCollectionsPattern(path, secondpattern);
                    // In the case of a path with a double star after the last
                    // slash, we will be given two separate patterns we have
                    // to match against to get the expected behavior (see bug
                    // 94064).
                    if (secondpattern.isstring())
                    {
                        collection_pm_tokens.append(secondpattern);
                        collection_pm_data.append(data);
                    }
		}
		else
		    path = token.myString;

		if (token.myDoPathMatching)
		{
		    // Once we are done with this token, it won't have
		    // any wildcards any more.
		    token.myDoPathMatching = false;
		    retest_for_wildcards = true;
		    collection_pm_tokens.append(path);
		    collection_pm_data.append(data);
		}
		else
		{
		    collection_tokens.append(path);
		    collection_data.append(data);
		}
	    }
            else if (!token.myHasWildcards &&
                     !getAssumeWildcardsAroundPlainTokens())
            {
                UT_String                tokenstr(token.myString.c_str());

                if (HUSDmakeValidUsdPath(tokenstr, false))
                    token.myString = tokenstr;
            }
	}

	auto	 stage = indata->stage();
	auto	 predicate = HUSDgetUsdPrimPredicate(demands);
	bool	 check_for_instance_proxies = false;

	if ((demands & HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES) == 0)
	    check_for_instance_proxies = true;

	if (collection_tokens.size() > 0)
	{
	    // Specific collections named in tokens.
	    for (int i = 0, n = collection_tokens.size(); i < n; i++)
	    {
		SdfPath collection_path;
		auto collection = husdGetCollection(stage,
		    collection_tokens(i), &collection_path);

		if (collection)
		{
		    collection_data(i)->myCollectionExpandedPathSet =
			UsdCollectionAPI::ComputeIncludedPaths(
			    collection.ComputeMembershipQuery(),
			    stage, predicate);
		    collection_data(i)->myCollectionPathSet.
			insert(collection_path);
		}
                collection_data(i)->myInitialized = true;
	    }
	}
	if (collection_pm_tokens.size() > 0)
	{
            UsdPrimRange range(stage->Traverse(predicate));

	    // Wildcard collections named in tokens. We have to traverse.
            for (auto iter = range.cbegin(); iter != range.cend(); ++iter)
	    {
                const UsdPrim &test_prim = *iter;
		std::vector<UsdCollectionAPI> test_collections =
		    UsdCollectionAPI::GetAllCollections(test_prim);
                bool prune_branch = true;

                if (test_collections.empty())
                {
                    UT_String prim_path(
                        HUSD_Path(test_prim.GetPath()).pathStr(), true);

		    for (int i = 0, n = collection_pm_tokens.size(); i< n; i++)
		    {
                        bool exclude_branches = false;

                        prim_path.matchPath(collection_pm_tokens(i), 1,
                            &exclude_branches);
                        if (!exclude_branches)
                        {
                            prune_branch = false;
                            break;
                        }
                    }
                }

		for (auto &&collection : test_collections)
		{
		    SdfPath sdfpath = collection.GetCollectionPath();
                    UT_String test_path(HUSD_Path(sdfpath).pathStr(), true);
		    SdfPathSet collection_pathset;
		    bool collection_pathset_computed = false;

		    for (int i = 0, n = collection_pm_tokens.size(); i< n; i++)
		    {
                        bool exclude_branches = false;

			if (test_path.matchPath(collection_pm_tokens(i), 1,
                                &exclude_branches))
			{
			    collection_pm_data(i)->
				myCollectionPathSet.insert(sdfpath);
			    if (!collection_pathset_computed)
			    {
				collection_pathset =
				    UsdCollectionAPI::ComputeIncludedPaths(
					collection.ComputeMembershipQuery(),
					stage, predicate);
				collection_pathset_computed = true;
			    }

			    collection_pm_data(i)->
				myCollectionExpandedPathSet.insert(
				    collection_pathset.begin(),
				    collection_pathset.end());
			}
                        collection_pm_data(i)->myInitialized = true;
                        if (!exclude_branches)
                            prune_branch = false;
		    }
		}

                if (prune_branch)
                    iter.PruneChildren();
	    }
	}
	if (auto_collection_data.size() > 0)
	{
	    // Specific auto auto_collections named in tokens.
	    for (int i = 0, n = auto_collection_data.size(); i < n; i++)
	    {
                if (!auto_collection_data(i)->
                        myRandomAccessAutoCollection->randomAccess())
                {
                    if (boss.wasInterrupted())
                    {
                        HUSD_ErrorScope::addError(HUSD_ERR_PATTERN_INTERRUPTED);
                        return;
                    }

                    auto_collection_data(i)->
                        myRandomAccessAutoCollection->matchPrimitives(
                            auto_collection_data(i)->
                                myCollectionlessPathSet,
                            getAllowInstanceIndices()
                                ? &auto_collection_data(i)->myMatchedInstanceIds
                                : nullptr);
                    // A non-random-access auto collection may match specific
                    // instances of a point instancer without matching the
                    // instancer prim as a whole (so the instancer is not in
                    // myCollectionlessPathSet). Add those instancer prims to
                    // the path set so the traversal visits them - and does not
                    // prune the branch above them - and so matchSpecialToken
                    // surfaces the per-instance ids from myMatchedInstanceIds.
                    if (getAllowInstanceIndices())
                    {
                        for (auto &&iit : auto_collection_data(i)->
                                myMatchedInstanceIds)
                            if (!iit.second.isEmpty())
                                auto_collection_data(i)->myCollectionlessPathSet.
                                    insert(HUSDgetSdfPath(iit.first));
                    }
                    auto_collection_data(i)->myMayBeTimeVarying =
                        auto_collection_data(i)->
                            myRandomAccessAutoCollection->getMayBeTimeVarying();
                    auto_collection_data(i)->
                        myRandomAccessAutoCollection.reset();
                }
                auto_collection_data(i)->myInitialized = true;
	    }
	}
	if (vex_tokens.size() > 0)
	{
	    // VEXpression in a token.
	    for (int i = 0, n = vex_tokens.size(); i < n; i++)
	    {
                if (boss.wasInterrupted())
                {
                    HUSD_ErrorScope::addError(HUSD_ERR_PATTERN_INTERRUPTED);
                    return;
                }

                UT_UniquePtr<UT_PathPattern> pruning_pattern(
                    createPruningPattern(vex_token_indices(i)));

		UT_StringArray	 paths;

		HUSD_Cvex cvex;
		cvex.setCwdNodeId(nodeid);
		cvex.setTimeCode(timecode);

		HUSD_CvexCode code( vex_tokens(i), /*is_cmd=*/ false );
		code.setReturnType( HUSD_CvexCode::ReturnType::BOOLEAN );

		if (cvex.matchPrimitives(lock, paths, code, demands,
                        pruning_pattern.get()))
		{
		    for (auto &&path : paths)
			vex_data(i)->myCollectionlessPathSet.
			    insert(SdfPath(path.toStdString()));
		}
                vex_data(i)->myInitialized = true;
                vex_data(i)->myMayBeTimeVarying = cvex.getIsTimeVarying();
	    }
	}
	if (preceding_group_tokens.size() > 0)
        {
            // Preceding Group tokens. These must be handled last, because we
            // are potentially going to use the computed results of prior
            // tokens to evaluate these tokens.
	    for (int i = 0, n = preceding_group_tokens.size(); i < n; i++)
	    {
                const auto &preceding_group_operator =
                    thePrecedingGroupMap[preceding_group_tokens(i)];
                UT_UniquePtr<UT_PathPattern> composing_pattern(
                    createPrecedingGroupPattern(
                        preceding_group_token_indices(i)));
                UsdPrim root = stage->GetPseudoRoot();
                XUSD_PathSet paths;

                if (root)
                {
                    // We may need to evaluate the driving pattern with a
                    // completely permissive predicate. Imagine the case where
                    // we want to find all prims with a child that has a
                    // certain attribute. That child may be an instance proxy,
                    // but we still want to be able to find its non-proxy
                    // ancestors.
                    XUSD_FindPrimPathsTaskData data(timecode);
                    auto allpredicate = HUSDgetUsdPrimPredicate(
                        HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES);
                    XUSDfindPrims(root, data,
                            preceding_group_operator.myUsePermissivePredicate
                                ? allpredicate : predicate,
                            composing_pattern.get());

                    // TODO: some day we may want to support instance ids with
                    // "preceding group tokens", in which case we will need to
                    // gather instance ids in the following call. But for now,
                    // we just leave them out. We only care about paths here.
                    data.gatherDataFromThreads(paths, nullptr);
                }

                preceding_group_operator.myFunction(
                    stage, predicate, paths,
                    preceding_group_data(i)->myCollectionlessPathSet);
                preceding_group_data(i)->myInitialized = true;
            }
        }

	// When getting a list of prim paths from collections, instance
	// proxies are not screened out. So here we need to go through all
	// path sets built from collections, test each prim to see if it's
	// an instance proxy, and if so, remove it. Note that the collection
	// sets matching each token are unchanged here. Only the full expanded
	// prim paths matter.
	if (check_for_instance_proxies)
	{
	    UT_Array<XUSD_SpecialTokenData *>	 tokens_data;

	    tokens_data.concat(collection_data);
	    tokens_data.concat(collection_pm_data);
	    for (auto &&data : tokens_data)
	    {
		for (auto it = data->myCollectionExpandedPathSet.begin();
		     it != data->myCollectionExpandedPathSet.end(); )
		{
		    UsdPrim  prim(stage->GetPrimAtPath(*it));

		    if (!prim || prim.IsInstanceProxy())
		    {
			HUSD_ErrorScope::addWarning(
			    HUSD_ERR_IGNORING_INSTANCE_PROXY, it->GetText());
			it = data->myCollectionExpandedPathSet.erase(it);
		    }
		    else
			++it;
		}
	    }
	}

	if (retest_for_wildcards)
	{
	    // We have removed the "wildcard" flag from some tokens above by
	    // expanding the wildcard in collection specifiers. So double check
	    // whether this whole pattern now consists of explicit paths.
	    testForExplicitList();
	}
    }
}

bool
HUSD_PathPattern::matchSpecialToken(const UT_StringRef &path,
	const UT_PathPattern::Token &token,
        bool *excludes_branch) const
{
    // The plain boolean match is exactly the payload-aware match without a
    // payload, so just forward to the single implementation.
    UT_PathPatternMatchDataPtr   match_data;

    return matchSpecialTokenWithData(path, token, excludes_branch, match_data);
}

bool
HUSD_PathPattern::matchSpecialTokenWithData(const UT_StringRef &path,
	const UT_PathPattern::Token &token,
        bool *excludes_branch,
        UT_PathPatternMatchDataPtr &match_data) const
{
    XUSD_SpecialTokenData *xusddata =
	static_cast<XUSD_SpecialTokenData *>(token.mySpecialTokenDataPtr.get());

    match_data.reset();

    // It's possible we haven't been evaluated yet, if we are just showing up
    // in a test pattern for pruning the set of paths that need to be tested
    // against some other special token.
    if (!xusddata || !xusddata->myInitialized)
        return true;

    SdfPath sdfpath(HUSDgetSdfPath(path));

    // Random access collections don't pre-traverse the stage to build a
    // full matching set. They get evaluated as we go.
    if (xusddata->myRandomAccessAutoCollection)
    {
        if (getAllowInstanceIndices())
        {
            UT_Array<int64> ids;
            bool result = xusddata->myRandomAccessAutoCollection->
                matchRandomAccessPrimitive(
                    sdfpath, excludes_branch, &ids);

            // An instance-aware auto collection signals a point instancer
            // match by filling in the matched instance ids. It returns false
            // in that case, because the instancer prim itself is not a
            // whole-prim match - only (some of) its instances are. So a
            // non-empty instance set is a match regardless of the boolean
            // result.
            if (!ids.isEmpty())
            {
                auto *idsdata = new XUSD_InstanceMatchData();
                idsdata->myInstanceIds = ids;
                match_data.reset(idsdata);
                return true;
            }

            // Otherwise this is an ordinary whole-prim match (or no match).
            // If the matched prim is a point instancer, it contributes all of
            // its instances to the instance algebra.
            if (result)
                match_data = instanceMatchData(path, UT_StringRef());
            return result;
        }
        return xusddata->myRandomAccessAutoCollection->
            matchRandomAccessPrimitive(sdfpath, excludes_branch);
    }

    bool     contains;
    bool     containsdescendant;

    // Check the collection expanded set for this path.
    containsdescendant = xusddata->myCollectionExpandedPathSet.
        containsPathOrDescendant(sdfpath, &contains);
    if (contains)
    {
        if (getAllowInstanceIndices())
            match_data = instanceMatchData(path, UT_StringRef());
        return true;
    }

    // Check the collectionless set for exact containment.
    containsdescendant |= xusddata->myCollectionlessPathSet.
        containsPathOrDescendant(sdfpath, &contains);
    if (contains)
    {
        if (getAllowInstanceIndices())
        {
            // A non-random-access auto collection may have precomputed a
            // specific set of matched instances for this path. Otherwise treat
            // a whole-prim match as all instances of the instancer.
            auto it = xusddata->myMatchedInstanceIds.find(sdfpath.GetText());
            if (it != xusddata->myMatchedInstanceIds.end() &&
                !it->second.isEmpty())
            {
                auto *idsdata = new XUSD_InstanceMatchData();
                idsdata->myInstanceIds = it->second;
                match_data.reset(idsdata);
            }
            else
                match_data = instanceMatchData(path, UT_StringRef());
        }
        return true;
    }

    // If neither set includes any children of the provided path, we can
    // prune the whole branch.
    if (!containsdescendant)
        *excludes_branch = true;

    return false;
}

UT_PathPatternMatchDataPtr
HUSD_PathPattern::makeLeafMatchData(const UT_StringRef &path,
        const UT_PathPattern::Token &token) const
{
    if (!getAllowInstanceIndices())
        return UT_PathPatternMatchDataPtr();

    // A plain token matching a point instancer selects the instances named by
    // its trailing [...] pattern, or all instances when no pattern is given.
    return instanceMatchData(path, token.myInstanceIdPattern);
}

UT_PathPatternMatchDataPtr
HUSD_PathPattern::instanceMatchData(const UT_StringRef &path,
        const UT_StringRef &instance_id_pattern) const
{
    // Without a lock we can't inspect the stage (e.g. lightweight clones used
    // for building pruning patterns), so produce no payload.
    if (!myLock)
        return UT_PathPatternMatchDataPtr();

    auto indata = myLock->constData();
    if (!indata || !indata->isStageValid())
        return UT_PathPatternMatchDataPtr();

    UsdStageRefPtr stage = indata->stage();
    UsdPrim prim = stage->GetPrimAtPath(HUSDgetSdfPath(path));
    UsdGeomPointInstancer instancer(prim);
    // Not a point instancer: no instance payload, so matching stays boolean.
    if (!instancer)
        return UT_PathPatternMatchDataPtr();

    UT_Array<int64> ids;
    if (instance_id_pattern.isstring())
        XUSDmatchPointInstanceIds(*myLock, instance_id_pattern, prim,
            myMatchTimeCode, ids);
    else
        HUSDgetPointInstancerIds(prim, myMatchTimeCode, ids);

    // Return a payload even when empty: this is an instancer path, so set
    // algebra (e.g. intersection) must treat it as an instance set rather than
    // falling back to boolean matching.
    auto *idsdata = new XUSD_InstanceMatchData();
    idsdata->myInstanceIds = ids;
    return UT_PathPatternMatchDataPtr(idsdata);
}

UT_PathPatternMatchDataPtr
HUSD_PathPattern::combineMatchData(MatchOp op,
        const UT_PathPatternMatchDataPtr &lhs,
        const UT_PathPatternMatchDataPtr &rhs) const
{
    // Null payloads mean "not an instancer path". If both sides are null, this
    // path carries no instance selection, so we produce no payload and let the
    // boolean operator result stand.
    if (!lhs && !rhs)
        return UT_PathPatternMatchDataPtr();

    // Instance ids are only meaningful within a single instancer, and each
    // match evaluation is for one prim, so combining here is correctly scoped
    // to that one instancer.
    //
    // Both operands satisfy the XUSD_InstanceMatchData invariant (their id
    // arrays are sorted ascending and duplicate-free), so every set operation
    // is a single linear merge of the two sorted arrays - no temporary sets are
    // needed. The result is produced in ascending order with no duplicates,
    // which preserves the invariant for the payload we return.
    static const UT_Array<int64> theEmptyIds;
    const XUSD_InstanceMatchData *l =
        static_cast<const XUSD_InstanceMatchData *>(lhs.get());
    const XUSD_InstanceMatchData *r =
        static_cast<const XUSD_InstanceMatchData *>(rhs.get());
    const UT_Array<int64>       &la = l ? l->myInstanceIds : theEmptyIds;
    const UT_Array<int64>       &ra = r ? r->myInstanceIds : theEmptyIds;
    const exint                  nl = la.size();
    const exint                  nr = ra.size();

    auto            *result = new XUSD_InstanceMatchData();
    UT_Array<int64> &out = result->myInstanceIds;
    exint            i = 0, j = 0;

    switch (op)
    {
        case MATCH_PRUNE:
            // Pruning leaves the left instance set unchanged.
            out = la;
            break;

        case MATCH_ADD:
            // Union: emit the smaller head, skipping the duplicate when both
            // heads are equal so each shared id appears once.
            out.setCapacity(nl + nr);
            while (i < nl && j < nr)
            {
                if (la(i) < ra(j))
                    out.append(la(i++));
                else if (ra(j) < la(i))
                    out.append(ra(j++));
                else
                {
                    out.append(la(i++));
                    ++j;
                }
            }
            while (i < nl)
                out.append(la(i++));
            while (j < nr)
                out.append(ra(j++));
            break;

        case MATCH_INTERSECT:
            // Intersection: emit only the ids that appear on both sides.
            while (i < nl && j < nr)
            {
                if (la(i) < ra(j))
                    ++i;
                else if (ra(j) < la(i))
                    ++j;
                else
                {
                    out.append(la(i++));
                    ++j;
                }
            }
            break;

        case MATCH_SUBTRACT:
            // Difference (left minus right): emit left ids not present on the
            // right.
            while (i < nl && j < nr)
            {
                if (la(i) < ra(j))
                    out.append(la(i++));
                else if (ra(j) < la(i))
                    ++j;
                else
                {
                    ++i;
                    ++j;
                }
            }
            while (i < nl)
                out.append(la(i++));
            break;
    }

    return UT_PathPatternMatchDataPtr(result);
}

bool
HUSD_PathPattern::matchDataIsEmpty(
        const UT_PathPatternMatchDataPtr &match_data) const
{
    if (!match_data)
        return true;

    const XUSD_InstanceMatchData *d =
        static_cast<const XUSD_InstanceMatchData *>(match_data.get());
    return d->myInstanceIds.isEmpty();
}

bool
HUSD_PathPattern::getMayBeTimeVarying() const
{
    for (int tokenidx = 0, n = myTokens.size(); tokenidx < n; ++tokenidx)
    {
        XUSD_SpecialTokenData *xusddata =
            static_cast<XUSD_SpecialTokenData *>(
                myTokens(tokenidx).mySpecialTokenDataPtr.get());

        if (xusddata)
        {
            if (xusddata->myMayBeTimeVarying)
                return true;
            if (xusddata->myRandomAccessAutoCollection &&
                xusddata->myRandomAccessAutoCollection->getMayBeTimeVarying())
                return true;
        }
    }

    return false;
}
