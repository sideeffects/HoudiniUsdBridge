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
*	Canada   M5J 2M2
*	416-504-9876
*
*/

#include "HUSD_EditLinkCollections.h"
#include "HUSD_Constants.h"
#include "HUSD_EditCollections.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_Path.h"
#include "HUSD_PathSet.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <UT/UT_Debug.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/listAPI.h>
#include <algorithm>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

// Every light will light everything by default, even if not explicitly
// included. For a light to only light certain objects by using geom rules,
// there must be a light rule to exclude everything?
//
// for rule in geomRules:
//   for light in rule.includes:
//     if light.lightLink is not default:
//       light.lightLink.includes += rule.sourcegeom
//     if rule.sourcegeom in light.lightLink.excludes:
//       light.lightLink.excludes -= rule.sourcegeom

class husd_EditLinkCollectionsPrivate
{
public:
    class LinkDefinition
    {
    public:
	UT_StringHolder		             myPrimPath;
	HUSD_PathSet		             myIncludes;
	HUSD_PathSet		             myExcludes;
        UT_StringHolder                      myPathExpr;
	HUSD_EditLinkCollections::LinkType   myType;
	bool			             myReversed = false;
        bool                                 myIsPathExprMode = false;
    };

    typedef UT_Map<HUSD_Path, LinkDefinition> LinkDefinitionsMap;
    LinkDefinitionsMap		             myLinkDefinitions;
    UT_UniquePtr<HUSD_FindPrims>             myAllLights;
};

static UsdCollectionAPI
husdGetCollectionAPI(HUSD_AutoWriteLock &lock, const SdfPath &sdfpath,
		     HUSD_EditLinkCollections::LinkType type)
{
    auto data = lock.data();
    if (!data || !data->isStageValid())
	return UsdCollectionAPI();

    auto		 stage = data->stage();
    UsdPrim		 prim = stage->GetPrimAtPath(sdfpath);

    if (type == HUSD_EditLinkCollections::LightLink)
    {
	UsdLuxLightAPI	 lightapi(prim);
	if (!lightapi)
	{
            HUSD_ErrorScope::addWarning(HUSD_ERR_MISSING_LIGHT_API_ERROR,
                sdfpath.GetAsString().c_str());
	    return UsdCollectionAPI();
	}
	return lightapi.GetLightLinkCollectionAPI();
    }

    if (type == HUSD_EditLinkCollections::ShadowLink)
    {
	UsdLuxLightAPI	 lightapi(prim);
	if (!lightapi)
	{
            HUSD_ErrorScope::addWarning(HUSD_ERR_MISSING_LIGHT_API_ERROR,
                sdfpath.GetAsString().c_str());
	    return UsdCollectionAPI();
	}
	return lightapi.GetShadowLinkCollectionAPI();
    }

    return UsdCollectionAPI();
}

static void
addToLinkData(const HUSD_Path &path,
        const HUSD_PathSet &includes,
        const HUSD_PathSet &excludes,
        const UT_StringHolder &pathexpr,
        HUSD_EditLinkCollections::LinkType linktype,
        HUSD_AutoWriteLock &writelock,
        husd_EditLinkCollectionsPrivate::LinkDefinitionsMap &linkdefs,
        const UT_StringRef &linkid,
        bool force_clear_include_root)
{
    auto linkdef = linkdefs.find(path);

    if (linkdef == linkdefs.end())
    {
        linkdef = linkdefs.emplace(path,
            husd_EditLinkCollectionsPrivate::LinkDefinition()).first;

        UsdCollectionAPI collection(husdGetCollectionAPI(
            writelock, path.sdfPath(), linktype));
        if (collection &&
            (collection.GetIncludeRootAttr().HasAuthoredValue() ||
             collection.GetExpansionRuleAttr().HasAuthoredValue() ||
             collection.GetIncludesRel().HasAuthoredTargets() ||
             collection.GetExcludesRel().HasAuthoredTargets() ||
             collection.GetMembershipExpressionAttr().HasAuthoredValue()))
        {
            // First link created here, and there's already linking information
            // on the prim. Roll the existing links into the new link since
            // whatever we author here will replace these existing values.

            // TODO: This code was copied from
            //  UsdCollectionAPI::IsInRelationshipsMode in USD 24.11. In
            //  future, we should just call that method.
            bool is_path_expr =
                !collection.ComputeMembershipQuery().UsesPathExpansionRuleMap();

            if (is_path_expr)
            {
                SdfPathExpression oldpathexpr;
                collection.GetMembershipExpressionAttr().Get(&oldpathexpr);
                linkdef->second.myIsPathExprMode = true;
                linkdef->second.myPathExpr = oldpathexpr.GetText();
            }
            else
            {
                SdfPathVector includepaths;
                SdfPathVector excludepaths;
                bool includeroot = false;

                // Add any existing includes/excludes
                collection.GetIncludesRel().GetTargets(&includepaths);
                collection.GetExcludesRel().GetTargets(&excludepaths);

                linkdef->second.myIsPathExprMode = false;
                linkdef->second.myIncludes.sdfPathSet().insert(
                    includepaths.begin(), includepaths.end());
                linkdef->second.myExcludes.sdfPathSet().insert(
                    excludepaths.begin(), excludepaths.end());
                if (collection.GetIncludeRootAttr() &&
                    collection.GetIncludeRootAttr().Get(&includeroot) &&
                    includeroot)
                    linkdef->second.myIncludes.insert(
                        HUSD_Path::theRootPrimPath);
            }
        }
        else
        {
            // This is a brand new link collection. We will follow the
            // regular procedure below for adding new data to this empty
            // data. But we might as well initialize the path expression
            // mode to what the current request wants it to be.
            linkdef->second.myIsPathExprMode = pathexpr.isstring();
            // If we are not in expression mode, the default behavior of a
            // completely un-authored link collection is to include the root
            // prim. So in this case, add the root prim to our includes.
            if (!linkdef->second.myIsPathExprMode)
                linkdef->second.myIncludes.insert(HUSD_Path::theRootPrimPath);
        }
    }

    // Add new link data to existing data.
    if (linkdef->second.myIsPathExprMode)
    {
        if (pathexpr.isstring())
        {
            // The existing path expression may be blank, in which case we
            // don't want to create a compound expression.
            if (linkdef->second.myPathExpr.isstring())
            {
                // Union the old path expr with the new path expr.
                UT_WorkBuffer buf;
                buf.sprintf("(%s) + (%s)", linkdef->second.myPathExpr.c_str(),
                    pathexpr.c_str());
                linkdef->second.myPathExpr = std::move(buf);
            }
            else
                linkdef->second.myPathExpr = pathexpr;
        }
        else if (!includes.empty() || !excludes.empty())
        {
            // Amend the path expression with explcit includes/excludes.
            UT_WorkBuffer includebuf;
            UT_WorkBuffer excludebuf;
            UT_WorkBuffer newpathexpr;
            for (auto &&sdfpath : includes)
            {
                includebuf.append(sdfpath.pathStr());
                includebuf.append(' ');
            }
            for (auto &&sdfpath : excludes)
            {
                excludebuf.append(sdfpath.pathStr());
                excludebuf.append(' ');
            }
            if (includebuf.isstring() && excludebuf.isstring())
                newpathexpr.sprintf("(%s) + (%s) - (%s)",
                    linkdef->second.myPathExpr.c_str(),
                    includebuf.buffer(), excludebuf.buffer());
            else if (includebuf.isstring())
                newpathexpr.sprintf("(%s) + (%s)",
                    linkdef->second.myPathExpr.c_str(),
                    includebuf.buffer());
            else
                newpathexpr.sprintf("(%s) - (%s)",
                    linkdef->second.myPathExpr.c_str(),
                    excludebuf.buffer());
            linkdef->second.myPathExpr = std::move(newpathexpr);
        }
    }
    else
    {
        if (pathexpr.isstring())
        {
            // Evaluate the old path expr and add the results to our
            // includes list. We are no longer tracking this link data
            // in path expression mode.
            HUSD_FindPrims findprims(writelock,
                HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES);
            findprims.addPathExpression(pathexpr);
            linkdef->second.myIncludes.insert(
                findprims.getExpandedPathSet());
            linkdef->second.myIsPathExprMode = false;
            HUSD_ErrorScope::addWarning(
                HUSD_ERR_PATH_EXPRESSION_HARDENED, linkid);
        }

        // Add the new includes/excludes to existing includes/excludes.
        linkdef->second.myIncludes.insert(includes);
        linkdef->second.myExcludes.insert(excludes);

        if (force_clear_include_root)
            linkdef->second.myIncludes.erase(HUSD_Path::theRootPrimPath);
    }
}

HUSD_EditLinkCollections::HUSD_EditLinkCollections(HUSD_AutoWriteLock &lock,
        HUSD_EditLinkCollections::LinkType linktype)
    : myWriteLock(lock),
      myLinkType(linktype),
      myPrivate(new husd_EditLinkCollectionsPrivate)
{
}

HUSD_EditLinkCollections::~HUSD_EditLinkCollections()
{
}

bool
HUSD_EditLinkCollections::addReverseLinkItems(const HUSD_FindPrims &linksource,
        const HUSD_FindPrims &includeprims,
        const HUSD_FindPrims &excludeprims,
        const UT_StringHolder &pathexpr,
        const UT_StringRef &linkid)
{
    // In a reverse collection link, the include prims and exclude prims are
    // really the owner prim of the collection link.  The owner prim here is
    // then put into the appropriate section of that collection, to be either
    // included or excluded as indicated.
    auto outdata = myWriteLock.data();
    if (!outdata || !outdata->isStageValid())
        return false;
    auto stage = outdata->stage();

    // If we are using path expressions, run the path expression and
    // put the result in the includes set. No excludes are necessary
    // because the path expression will have already excluded any
    // lights that would have been specified in the excludes parm.
    // Don't allow instance proxies when finding path expr include
    // prims because these are light prims, and we can't modify light
    // instance proxies.
    const HUSD_FindPrims *final_includeprims = &includeprims;
    HUSD_FindPrims pathexpr_includeprims(myWriteLock);
    if (pathexpr.isstring())
    {
        pathexpr_includeprims.addPathExpression(pathexpr);
        final_includeprims = &pathexpr_includeprims;
        HUSD_ErrorScope::addWarning(HUSD_ERR_PATH_EXPRESSION_HARDENED, linkid);
    }

    // First, deal with includes list.  If the list is empty, take no action.
    if (!final_includeprims->getIsEmpty())
    {
	// Find and load all lights. Don't include instance proxies because
        // we can't edit their link collections. We cache the set of lights
        // on the assumption that no lights will be added in the middle of
        // doing light linking.
        if (!myPrivate->myAllLights)
        {
            myPrivate->myAllLights = UTmakeUnique<HUSD_FindPrims>(
                myWriteLock, HUSD_TRAVERSAL_DEFAULT_DEMANDS);
            myPrivate->myAllLights->addPattern("%active & %type(LightAPI)",
                OP_INVALID_NODE_ID, HUSD_TimeCode());
        }

        // First deal with included link targets
        for (auto &&path : myPrivate->myAllLights->getExpandedPathSet())
        {
            auto prim = stage->GetPrimAtPath(path.sdfPath());
            if (!prim.IsValid())
            {
                HUSD_ErrorScope::addWarning(HUSD_ERR_CANT_FIND_PRIM,
                    path.pathStr().c_str());
                continue;
            }

            // The linksource is either going to be added to the includes
            // or excludes of each light in the final_includeprims.
            const HUSD_PathSet &linksource_pathset =
                linksource.getCollectionAwarePathSet();
            bool linksource_includes =
                final_includeprims->getExpandedPathSet().contains(path);

            // Get the link info or create a new one.
            addToLinkData(path,
                linksource_includes ? linksource_pathset : HUSD_PathSet(),
                linksource_includes ? HUSD_PathSet() : linksource_pathset,
                UT_StringHolder::theEmptyString,
                myLinkType,
                myWriteLock,
                myPrivate->myLinkDefinitions,
                linkid,
                false);
        }
    }

    // Now deal with excludes
    for (auto &&path : excludeprims.getExpandedPathSet())
    {
	auto prim = stage->GetPrimAtPath(path.sdfPath());
	if (!prim.IsValid())
	{
            HUSD_ErrorScope::addWarning(HUSD_ERR_CANT_FIND_PRIM,
                path.pathStr().c_str());
	    continue;
	}

	// Get the link info or create a new one.
        addToLinkData(path,
            HUSD_PathSet(),
            linksource.getCollectionAwarePathSet(),
            UT_StringHolder::theEmptyString,
            myLinkType,
	    myWriteLock,
            myPrivate->myLinkDefinitions,
            linkid,
            false);
    }

    return true;
}

bool
HUSD_EditLinkCollections::addLinkItems(const HUSD_FindPrims &linksource,
        const HUSD_FindPrims &includeprims,
        const HUSD_FindPrims &excludeprims,
        const UT_StringHolder &pathexpr,
        const UT_StringRef &linkid)
{
    // In a regular (forward) collection link, the include and exclude prims
    // map directly to the include and exclude parts of the collection on the
    // owner prim.
    auto outdata = myWriteLock.data();
    if (!outdata || !outdata->isStageValid())
        return false;
    auto stage = outdata->stage();

    // If all prim specifiers are empty, don't do anything.
    if (includeprims.getIsEmpty() &&
        excludeprims.getIsEmpty() &&
        !pathexpr.isstring())
	return true;

    for (auto &&path : linksource.getExpandedPathSet())
    {
	auto prim = stage->GetPrimAtPath(path.sdfPath());
	if (!prim.IsValid())
	{
            HUSD_ErrorScope::addWarning(HUSD_ERR_CANT_FIND_PRIM,
                path.pathStr().c_str());
	    continue;
	}

	// Get the link info or create a new one.
        // If we're setting an explicit include for a rule then we should
        // clear the implicit include everything (last parm).
        addToLinkData(path,
            includeprims.getCollectionAwarePathSet(),
            excludeprims.getCollectionAwarePathSet(),
            pathexpr,
            myLinkType,
	    myWriteLock,
            myPrivate->myLinkDefinitions,
            linkid,
            !includeprims.getIsEmpty());
    }

    return true;
}

bool
HUSD_EditLinkCollections::createCollections(UT_String *error_message)
{
    HUSD_EditCollections	 editor(myWriteLock);

    for (auto && linkpair : myPrivate->myLinkDefinitions)
    {
	auto collection = husdGetCollectionAPI(
            myWriteLock, linkpair.first.sdfPath(), myLinkType);

        if (linkpair.second.myIsPathExprMode)
        {
            if (!editor.createPathExpressionCollection(
                    linkpair.first.pathStr().c_str(),
                    collection.GetName().GetText(),
                    linkpair.second.myPathExpr, true,
                    /*forceapply=*/false))
            {
                if (error_message)
                {
                    error_message->sprintf(
                            "Failed to create collection path expression - '%s', '%s', '%s'",
                            linkpair.first.pathStr().c_str(),
                            collection.GetName().GetText(),
                            linkpair.second.myPathExpr.c_str());
                }
                return false;
            }
        }
        else
        {
            HUSD_PrimTraversalDemands demands(
                HUSD_TRAVERSAL_DEFAULT_WITH_PROXIES);
            HUSD_FindPrims includes(myWriteLock,
                linkpair.second.myIncludes, demands);
            if (includes.getLastError())
            {
                if (error_message)
                {
                    UT_StringArray paths;
                    linkpair.second.myIncludes.getPathsAsStrings(paths);
                    UT_WorkBuffer pathstring;
                    paths.join(" ", pathstring);
                    error_message->sprintf(
                            "Includes findPrims '%s' failed - %s",
                            pathstring.c_str(),
                            includes.getLastError().c_str());
                }
                return false;
            }

            HUSD_FindPrims excludes(myWriteLock,
                linkpair.second.myExcludes, demands);
            if (excludes.getLastError())
            {
                if (error_message)
                {
                    UT_StringArray paths;
                    linkpair.second.myExcludes.getPathsAsStrings(paths);
                    UT_WorkBuffer pathstring;
                    paths.join(" ", pathstring);
                    error_message->sprintf(
                            "Excludes findPrims '%s' failed - %s",
                            pathstring.c_str(),
                            excludes.getLastError().c_str());
                }
                return false;
            }

            if (!editor.createCollection(
                    linkpair.first.pathStr().c_str(),
                    collection.GetName().GetText(),
                    HUSD_Constants::getExpansionExpandPrims(),
                    includes, excludes, true, true,
                    /*forceapply=*/false))
            {
                if (error_message)
                {
                    error_message->sprintf(
                            "Failed to create collection '%s' for '%s'",
                            collection.GetName().GetText(),
                            linkpair.first.pathStr().c_str());
                }
                return false;
            }
        }
    }

    return true;
}
