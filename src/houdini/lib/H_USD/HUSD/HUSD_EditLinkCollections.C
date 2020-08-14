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
#include <pxr/usd/usdLux/light.h>
#include <pxr/usd/usdLux/listAPI.h>
#include <algorithm>

#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

/*
light will light everything by default, even if not explicitly included.  for a
light to only light certain objects by using geom rules, there must be a light
rule to exclude everything?

for rule in geomRules:
for light in rule.includes:
if light.lightLink is not default:
light.lightLink.includes += rule.sourcegeom
if rule.sourcegeom in light.lightLink.excludes:
light.lightLink.excludes -= rule.sourcegeom
*/


class husd_EditLinkCollectionsPrivate
{
public:
    class LinkDefinition
    {
    public:
	UT_StringHolder		             myPrimPath;
	HUSD_PathSet		             myIncludes;
	HUSD_PathSet		             myExcludes;
	HUSD_EditLinkCollections::LinkType   myType;
	bool			             myIncludeRoot = true;
	bool			             myReversed = false;
    };

    typedef UT_Map<SdfPath, LinkDefinition> LinkDefinitionsMap;
    LinkDefinitionsMap		             myLinkDefinitions;

};

static UsdCollectionAPI
husdGetCollectionAPI(HUSD_AutoWriteLock &lock, const SdfPath &sdfpath,
		     HUSD_EditLinkCollections::LinkType type,
		     UT_StringArray *errors)
{
    auto data = lock.data();
    if (!data || !data->isStageValid())
    {
	if (errors)
	    errors->append("Invalid stage");
	return UsdCollectionAPI();
    }

    auto		 stage = data->stage();
    UsdPrim		 prim = stage->GetPrimAtPath(sdfpath);

    if (type == HUSD_EditLinkCollections::LightLink)
    {
	UsdLuxLight	 light(prim);
	if (!prim.IsValid())
	{
	    if (errors)
		errors->append("Prim not a UsdLuxLight");
	    return UsdCollectionAPI();
	}
	return light.GetLightLinkCollectionAPI();
    }

    if (type == HUSD_EditLinkCollections::ShadowLink)
    {
	UsdLuxLight	 light(prim);
	if (!prim.IsValid())
	{
	    if (errors)
		errors->append("Prim not a UsdLuxLight");
	    return UsdCollectionAPI();
	}
	return light.GetShadowLinkCollectionAPI();
    }

    if (errors)
	errors->append("Unknown link type");
    return UsdCollectionAPI();
}


static husd_EditLinkCollectionsPrivate::LinkDefinition &
getLinkData(const SdfPath &sdfpath,
        HUSD_PathSet &includes,
        HUSD_PathSet &excludes,
        HUSD_EditLinkCollections::LinkType linktype,
        HUSD_AutoWriteLock &writelock,
        husd_EditLinkCollectionsPrivate::LinkDefinitionsMap &linkdefs,
        UT_StringArray *errors)
{
    auto collection=husdGetCollectionAPI(writelock, sdfpath, linktype, errors);
    auto linkpair=linkdefs.find(sdfpath);

    if (linkpair == linkdefs.end())
    {
	SdfPathVector		 sdfpaths;

	linkpair = linkdefs.emplace(sdfpath,
	    husd_EditLinkCollectionsPrivate::LinkDefinition()).first;

	// Add any existing includes/excludes
	if (collection.GetIncludesRel().GetTargets(&sdfpaths))
            includes.sdfPathSet().insert(sdfpaths.begin(), sdfpaths.end());

	if (collection.GetExcludesRel().GetTargets(&sdfpaths))
            excludes.sdfPathSet().insert(sdfpaths.begin(), sdfpaths.end());

	collection.GetIncludeRootAttr().Get(&linkpair->second.myIncludeRoot);
    }
    linkpair->second.myIncludes.swap(includes);
    linkpair->second.myExcludes.swap(excludes);

    return linkpair->second;
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
        int nodeid,
        const HUSD_TimeCode &tc,
        UT_StringArray *errors)
{
    auto			 outdata = myWriteLock.data();
    bool			 success = true;

    if (!outdata || !outdata->isStageValid())
    {
	if (errors)
	    errors->append("Invalid stage");
    }

    auto			 stage = outdata->stage();

    // First, deal with includes list.  If the list is empty, take no action.
    if (!includeprims.getIsEmpty())
    {
	// Find and load all lights, using lightList cache where available
	UsdLuxListAPI listAPI(stage->GetPseudoRoot());
	{
	    SdfPathSet all_lights = listAPI.ComputeLightList(
		UsdLuxListAPI::ComputeModeIgnoreCache);
	    const SdfPathSet &includelights =
                includeprims.getExpandedPathSet().sdfPathSet();

	    // First deal with included link targets
	    for (auto && sdfpath : all_lights)
	    {
		auto			 prim = stage->GetPrimAtPath(sdfpath);
		if (!prim.IsValid())
		{
		    if (errors)
			errors->append("Invalid prim");
		    continue;
		}

		HUSD_PathSet		 includes;
		HUSD_PathSet		 excludes;

		if (includelights.find(sdfpath) != includelights.end())
		    includes = linksource.getCollectionAwarePathSet();
		else
		    excludes = linksource.getCollectionAwarePathSet();

		// Get the link info or create a new one.
		getLinkData(sdfpath, includes, excludes, myLinkType,
		    myWriteLock, myPrivate->myLinkDefinitions, errors);
	    }
	}
    }

    //
    // Now deal with excludes

    for (auto &&sdfpath : excludeprims.getExpandedPathSet().sdfPathSet())
    {
	auto			 prim = stage->GetPrimAtPath(sdfpath);
	auto			 collection = 
	    husdGetCollectionAPI(myWriteLock, sdfpath, myLinkType, errors);

	if (!prim.IsValid())
	{
	    if (errors)
		errors->append("Invalid prim");
	    continue;
	}

	HUSD_PathSet		 includes;
	HUSD_PathSet		 excludes;

	excludes = linksource.getCollectionAwarePathSet();

	// Get the link info or create a new one.
	getLinkData(sdfpath, includes, excludes, myLinkType,
	    myWriteLock, myPrivate->myLinkDefinitions, errors);
    }
    return success;
}

void
HUSD_EditLinkCollections::clear()
{
    myPrivate->myLinkDefinitions.clear();
}

bool
HUSD_EditLinkCollections::addLinkItems(const HUSD_FindPrims &linksource,
				       const HUSD_FindPrims &includeprims,
				       const HUSD_FindPrims &excludeprims,
				       int nodeid,
				       const HUSD_TimeCode &tc,
				       UT_StringArray *errors)
{
    auto			 outdata = myWriteLock.data();
    bool			 success = true;

    if (!outdata || !outdata->isStageValid())
    {
	if (errors)
	    errors->append("Invalid stage");
    }

    // First, deal with includes list.  If the list is empty, take no action.
    if (includeprims.getIsEmpty() && excludeprims.getIsEmpty())
	return success;
    for (auto &&sdfpath : linksource.getExpandedPathSet().sdfPathSet())
    {
	auto collection =husdGetCollectionAPI(
            myWriteLock, sdfpath, myLinkType, errors);
	auto stage = outdata->stage();
	auto prim = stage->GetPrimAtPath(sdfpath);

	if (!prim.IsValid())
	{
	    if (errors)
		errors->append("Invalid prim");
	    continue;
	}

	HUSD_PathSet		 includes;
	HUSD_PathSet		 excludes;

	includes = includeprims.getCollectionAwarePathSet();
	excludes = excludeprims.getCollectionAwarePathSet();

	// Get the link info or create a new one.
	auto &linkdata = getLinkData(sdfpath, includes, excludes, myLinkType,
	    myWriteLock, myPrivate->myLinkDefinitions, errors);
	// If we're setting an explicit include for a rule then we should
	// clear the implicit include everything.
	if (!includeprims.getIsEmpty())
	    linkdata.myIncludeRoot = false;
    }
    return success;
}

bool
HUSD_EditLinkCollections::createCollections(UT_StringArray * errors)
{
    HUSD_EditCollections	 editor(myWriteLock);
    bool			 success = true;

    for (auto && linkpair : myPrivate->myLinkDefinitions)
    {
	auto collection = husdGetCollectionAPI(
            myWriteLock, linkpair.first, myLinkType, errors);
        HUSD_PrimTraversalDemands demands(
            HUSD_PrimTraversalDemands(
                HUSD_TRAVERSAL_DEFAULT_DEMANDS |
                HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES));
        HUSD_FindPrims includes(myWriteLock,
            linkpair.second.myIncludes, demands);
        HUSD_FindPrims excludes(myWriteLock,
            linkpair.second.myExcludes, demands);

	if (linkpair.second.myIncludeRoot)
	    includes.addPattern("/", OP_INVALID_NODE_ID, HUSD_TimeCode());

	if (!editor.createCollection(collection.GetPath().GetString().c_str(),
                collection.GetName().GetText(),
                HUSD_Constants::getExpansionExpandPrims(),
                includes, excludes, true))
	    return false;
    }

    return success;
}
