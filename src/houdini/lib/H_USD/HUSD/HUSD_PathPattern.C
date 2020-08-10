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

#include "HUSD_PathPattern.h"
#include "HUSD_Constants.h"
#include "HUSD_Cvex.h"
#include "HUSD_CvexCode.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_Preferences.h"
#include "XUSD_AutoCollection.h"
#include "XUSD_Data.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_PathPattern.h"
#include "XUSD_PerfMonAutoCookEvent.h"
#include "XUSD_Utils.h"
#include <UT/UT_StringSet.h>
#include <UT/UT_WorkArgs.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>

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

    typedef std::function<void (const UsdStageRefPtr &stage,
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

HUSD_PathPattern::HUSD_PathPattern()
    : UT_PathPattern()
{
}

HUSD_PathPattern::HUSD_PathPattern(const UT_StringArray &pattern_tokens,
	HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
        int nodeid)
    : UT_PathPattern(pattern_tokens, true)
{
    XUSD_PerfMonAutoCookEvent perf(nodeid, "Primitive pattern evaluation");

    initializeSpecialTokens(lock, demands, OP_INVALID_NODE_ID, HUSD_TimeCode());
}

HUSD_PathPattern::HUSD_PathPattern(const UT_StringRef &pattern,
	HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
	int nodeid,
	const HUSD_TimeCode &timecode)
    : UT_PathPattern(pattern, true)
{
    XUSD_PerfMonAutoCookEvent perf(nodeid, "Primitive pattern evaluation");

    initializeSpecialTokens(lock, demands, nodeid, timecode);
}

HUSD_PathPattern::~HUSD_PathPattern()
{
}

UT_PathPattern *
HUSD_PathPattern::createEmptyClone() const
{
    return new XUSD_PathPattern();
}

void
HUSD_PathPattern::initializeSpecialTokens(HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
	int nodeid,
	const HUSD_TimeCode &timecode)
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
            else if (!token.myHasWildcards)
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

		for (auto &&collection : test_collections)
		{
		    SdfPath sdfpath = collection.GetCollectionPath();
		    UT_String test_path(sdfpath.GetText());
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
                    auto_collection_data(i)->
                        myRandomAccessAutoCollection->matchPrimitives(
                            auto_collection_data(i)->myCollectionlessPathSet);
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
                    XUSD_FindPrimPathsTaskData data;
                    auto allpredicate = HUSDgetUsdPrimPredicate(
                        HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES);
                    auto &task = *new(UT_Task::allocate_root())
                        XUSD_FindPrimsTask(root, data,
                            preceding_group_operator.myUsePermissivePredicate
                                ? allpredicate : predicate,
                            composing_pattern.get(), nullptr);
                    UT_Task::spawnRootAndWait(task);

                    data.gatherPathsFromThreads(paths);
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
    XUSD_SpecialTokenData *xusddata =
	static_cast<XUSD_SpecialTokenData *>(token.mySpecialTokenDataPtr.get());

    // It's possible we haven't been evaluated yet, if we are just showing up
    // in a test pattern for pruning the set of paths that need to be tested
    // against some other special token.
    if (!xusddata || !xusddata->myInitialized)
        return true;

    SdfPath sdfpath(HUSDgetSdfPath(path));

    // Random access collections don't pre-traverse the stage to build a
    // full matching set. The get evaluated as we go.
    if (xusddata->myRandomAccessAutoCollection)
        return xusddata->myRandomAccessAutoCollection->
            matchRandomAccessPrimitive(sdfpath, excludes_branch);

    if (xusddata->myCollectionExpandedPathSet.count(sdfpath) > 0)
	return true;

    if (xusddata->myCollectionlessPathSet.count(sdfpath) > 0)
	return true;

    return false;
}

