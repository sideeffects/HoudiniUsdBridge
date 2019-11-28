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
#include "XUSD_PathPattern.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <UT/UT_WorkArgs.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

static const char *theCollectionSeparator = ".collection:";

UsdCollectionAPI
husdGetCollection(const UsdStageRefPtr &stage,
    const UT_StringRef &identifier,
    SdfPath *collection_path)
{
    SdfPath		 sdfpath(HUSDgetSdfPath(identifier));
    TfToken		 collection_name;
    UsdCollectionAPI	 collection;

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
	    const char	*last_doublestar = UT_String(last_slash).fcontain("**");

	    if (last_doublestar)
            {
                // If the pattern has a "**" after the last slash, we need two
                // patterns to represent this faithfully. One is the pattern
                // as provided, to match any child prims recursively. The
                // other is to match any collections on the prim that appears
                // before the last slash.
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

HUSD_PathPattern::HUSD_PathPattern(const UT_StringArray &pattern_tokens,
	HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands)
    : UT_PathPattern(pattern_tokens, true)
{
    initializeSpecialTokens(lock, demands, OP_INVALID_NODE_ID, HUSD_TimeCode());
}

HUSD_PathPattern::HUSD_PathPattern(const UT_StringRef &pattern,
	HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
	int nodeid,
	const HUSD_TimeCode &timecode)
    : UT_PathPattern(pattern, true)
{
    initializeSpecialTokens(lock, demands, nodeid, timecode);
}

HUSD_PathPattern::~HUSD_PathPattern()
{
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
	UT_StringArray				 special_tokens;
	UT_Array<XUSD_SpecialTokenData *>	 special_tokens_data;
	UT_StringArray				 special_pm_tokens;
	UT_Array<XUSD_SpecialTokenData *>	 special_pm_tokens_data;
	UT_StringArray				 special_vex_tokens;
	UT_Array<XUSD_SpecialTokenData *>	 special_vex_tokens_data;
	bool					 retest_for_wildcards = false;

	for (auto &&token : myTokens)
	{
	    if (token.myString.startsWith("{"))
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
		special_vex_tokens.append(vex);
		special_vex_tokens_data.append(data);
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
                        special_pm_tokens.append(secondpattern);
                        special_pm_tokens_data.append(data);
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
		    special_pm_tokens.append(path);
		    special_pm_tokens_data.append(data);
		}
		else
		{
		    special_tokens.append(path);
		    special_tokens_data.append(data);
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

	if (special_tokens.size() > 0)
	{
	    // Specific collections named in tokens.
	    for (int i = 0, n = special_tokens.size(); i < n; i++)
	    {
		SdfPath collection_path;
		auto collection = husdGetCollection(stage,
		    special_tokens(i), &collection_path);

		if (collection)
		{
		    special_tokens_data(i)->myExpandedCollectionPathSet =
			UsdCollectionAPI::ComputeIncludedPaths(
			    collection.ComputeMembershipQuery(),
			    stage, predicate);
		    special_tokens_data(i)->myCollectionPathSet.
			insert(collection_path);
		}
	    }
	}
	if (special_pm_tokens.size() > 0)
	{
	    // Wildcard collections named in tokens. We have to traverse.
	    for (auto &&test_prim : stage->Traverse(predicate))
	    {
		std::vector<UsdCollectionAPI> test_collections =
		    UsdCollectionAPI::GetAllCollections(test_prim);

		for (auto &&collection : test_collections)
		{
		    SdfPath sdfpath = collection.GetCollectionPath();
		    UT_String test_path(sdfpath.GetText());
		    SdfPathSet collection_pathset;
		    bool collection_pathset_computed = false;

		    for (int i = 0, n = special_pm_tokens.size(); i< n; i++)
		    {
			if (test_path.matchPath(special_pm_tokens(i)))
			{
			    special_pm_tokens_data(i)->
				myCollectionPathSet.insert(sdfpath);
			    if (!collection_pathset_computed)
			    {
				collection_pathset =
				    UsdCollectionAPI::ComputeIncludedPaths(
					collection.ComputeMembershipQuery(),
					stage, predicate);
				collection_pathset_computed = true;
			    }

			    special_pm_tokens_data(i)->
				myExpandedCollectionPathSet.insert(
				    collection_pathset.begin(),
				    collection_pathset.end());
			}
		    }
		}
	    }
	}
	if (special_vex_tokens.size() > 0)
	{
	    // VEXpression in a token.
	    for (int i = 0, n = special_vex_tokens.size(); i < n; i++)
	    {
		UT_StringArray	 paths;

		HUSD_Cvex cvex;
		cvex.setCwdNodeId(nodeid);
		cvex.setTimeCode(timecode);

		HUSD_CvexCode code( special_vex_tokens(i), /*is_cmd=*/ false );
		code.setReturnType( HUSD_CvexCode::ReturnType::BOOLEAN );

		if (cvex.matchPrimitives(lock, paths, code, demands))
		{
		    for (auto &&path : paths)
			special_vex_tokens_data(i)->myVexpressionPathSet.
			    insert(SdfPath(path.toStdString()));
		}
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

	    tokens_data.concat(special_tokens_data);
	    tokens_data.concat(special_pm_tokens_data);
	    for (auto &&data : tokens_data)
	    {
		for (auto it = data->myExpandedCollectionPathSet.begin();
		     it != data->myExpandedCollectionPathSet.end(); )
		{
		    UsdPrim  prim(stage->GetPrimAtPath(*it));

		    if (!prim || prim.IsInstanceProxy())
		    {
			HUSD_ErrorScope::addWarning(
			    HUSD_ERR_IGNORING_INSTANCE_PROXY, it->GetText());
			it = data->myExpandedCollectionPathSet.erase(it);
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
	const UT_PathPattern::Token &token) const
{
    XUSD_SpecialTokenData *xusddata =
	static_cast<XUSD_SpecialTokenData *>(token.mySpecialTokenDataPtr.get());
    SdfPath sdfpath(HUSDgetSdfPath(path));

    if (xusddata->myExpandedCollectionPathSet.count(sdfpath) > 0)
	return true;

    if (xusddata->myVexpressionPathSet.count(sdfpath) > 0)
	return true;

    return false;
}

