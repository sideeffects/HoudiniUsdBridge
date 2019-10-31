/*
* PROPRIETARY INFORMATION.  This software is proprietary to
* Side Effects Software Inc., and is not to be reproduced,
* transmitted, or disclosed in any way without written permission.
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



#if 0
#define RKRCOUT(msg) std::cout << __FUNCTION__ << ":" << __LINE__ << " - " << msg << std::endl
#else
#define RKRCOUT(msg)
#endif

/*
// light will light everything by default, even if not explicitly included.
// for a light to only light certain objects by using geom rules, there must be a light rule to exclude everything?

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
	LinkDefinition(HUSD_AutoWriteLock &lock)
	    : myIncludes(lock), myExcludes(lock) {}
	UT_StringHolder		 myPrimPath;
	HUSD_FindPrims		 myIncludes;
	HUSD_FindPrims		 myExcludes;
	bool			 myIncludeRoot = true;
	HUSD_EditLinkCollections::LinkType
				 myType;
	bool			 myReversed;
    };

    typedef UT_Map<SdfPath, LinkDefinition> LinkDefinitionsMap;
    LinkDefinitionsMap		 myLinkDefinitions;

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
getLinkData(
    const SdfPath &sdfpath,
    UT_StringArray &includes,
    UT_StringArray &excludes,
    HUSD_EditLinkCollections::LinkType linktype,
    HUSD_AutoWriteLock &writelock,
    husd_EditLinkCollectionsPrivate::LinkDefinitionsMap &linkdefs,
    UT_StringArray *errors
)
{
    auto			 collection =
	husdGetCollectionAPI(writelock, sdfpath, linktype, errors);

    auto			 linkpair =
	linkdefs.find(sdfpath);
    if (linkpair == linkdefs.end())
    {
	SdfPathVector		 sdfpaths;
	linkdefs.emplace(
	    sdfpath,
	    husd_EditLinkCollectionsPrivate::LinkDefinition(writelock));
	linkpair = linkdefs.find(sdfpath);

	// initialize with existing includes/excludes
	if (collection.GetIncludesRel().GetTargets(&sdfpaths))
	{
	    for (auto && it : sdfpaths)
	    {
		includes.append(it.GetText());
	    }
	}

	if (collection.GetExcludesRel().GetTargets(&sdfpaths))
	{
	    for (auto && it : sdfpaths)
	    {
		excludes.append(it.GetText());
	    }
	}
	collection.GetIncludeRootAttr().Get(&linkpair->second.myIncludeRoot);
	RKRCOUT(" init"
		<< " - " << collection.GetPath().GetString()
		<< ":" << collection.GetName()
		<< " INC: " << (include_root ? "/ " : "") << includes
		<< " EXC: " << excludes
	);
    }
    return linkpair->second;
}

HUSD_EditLinkCollections::HUSD_EditLinkCollections(HUSD_AutoWriteLock &lock, HUSD_EditLinkCollections::LinkType linktype)
    : myWriteLock(lock), myLinkType(linktype), myPrivate(new husd_EditLinkCollectionsPrivate)
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

    {
	// RKR This block is for debugging
	UT_StringArray  tmp_src, tmp_inc, tmp_exc;
	linksource.getCollectionAwarePaths(tmp_src);
	includeprims.getCollectionAwarePaths(tmp_inc);
	excludeprims.getCollectionAwarePaths(tmp_exc);
	RKRCOUT(""
		<< " - " << tmp_src
		<< " INC: " << tmp_inc
		<< " EXC: " << tmp_exc
	);
    }


    // First, deal with includes list.  If the list is empty, take no action.
    if (!includeprims.getIsEmpty())
    {
	// Find and load all lights, using lightList cache where available
	UsdLuxListAPI listAPI(stage->GetPseudoRoot());
	{
	    SdfPathSet all_lights = listAPI.ComputeLightList(
		UsdLuxListAPI::ComputeModeIgnoreCache);

	    //   UsdLuxListAPI list(stage->GetPseudoRoot());
	    //   SdfPathSet all_lights = list.ComputeLightList(
	    //UsdLuxListAPI::ComputeModeConsultModelHierarchyCache);
	    //   stage->LoadAndUnload(all_lights, SdfPathSet());

	    SdfPathSet includelights = includeprims.getExpandedPathSet();
	    //
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

		UT_StringArray		 includes;
		UT_StringArray		 excludes;

		if (includelights.find(sdfpath) != includelights.end())
		{
		    RKRCOUT(" not found"
			    << " - " << sdfpath
		    );
		    linksource.getCollectionAwarePaths(includes);
		}
		else
		{
		    linksource.getCollectionAwarePaths(excludes);
		    RKRCOUT(" found"
			    << " - " << sdfpath
		    );
		}

		// Get the link info or create a new one.
		auto & linkdata = getLinkData(
		    sdfpath, includes, excludes, myLinkType,
		    myWriteLock, myPrivate->myLinkDefinitions, errors);
		linkdata.myIncludes.addPattern(includes);
		linkdata.myExcludes.addPattern(excludes);
		{
		    // RKR This block is for debugging
		    UT_StringArray  tmp_inc, tmp_exc;
		    linkdata.myIncludes.getCollectionAwarePaths(tmp_inc);
		    linkdata.myExcludes.getCollectionAwarePaths(tmp_exc);
		    RKRCOUT(" link-include"
			    << " - " << sdfpath
			    << " INC: " << tmp_inc
			    << " EXC: " << tmp_exc
		    );
		}
	    }
	}
    }

    //
    // Now deal with excludes

    for (auto &&sdfpath : excludeprims.getExpandedPathSet())
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

	UT_StringArray		 includes;
	UT_StringArray		 excludes;

	linksource.getCollectionAwarePaths(excludes);

	// Get the link info or create a new one.
	auto & linkdata = getLinkData(
	    sdfpath, includes, excludes, myLinkType,
	    myWriteLock, myPrivate->myLinkDefinitions, errors);
	linkdata.myIncludes.addPattern(includes);
	linkdata.myExcludes.addPattern(excludes);
	{
	    // RKR This block is for debugging
	    UT_StringArray  tmp_inc, tmp_exc;
	    linkdata.myIncludes.getCollectionAwarePaths(tmp_inc);
	    linkdata.myExcludes.getCollectionAwarePaths(tmp_exc);
	    RKRCOUT(" link-exclude"
		    << " - " << collection.GetPath().GetString()
		    << ":" << collection.GetName()
		    << " INC: " << tmp_inc
		    << " EXC: " << tmp_exc
	    );

	}
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

    const			 XUSD_PathSet &sourceset =
	linksource.getExpandedPathSet();

    {
	// RKR This block is for debugging
	UT_StringArray  tmp_src, tmp_inc, tmp_exc;
	linksource.getCollectionAwarePaths(tmp_src);
	includeprims.getCollectionAwarePaths(tmp_inc);
	excludeprims.getCollectionAwarePaths(tmp_exc);
	RKRCOUT(""
		<< " - " << tmp_src
		<< " INC: " << tmp_inc
		<< " EXC: " << tmp_exc
	);
    }

    // First, deal with includes list.  If the list is empty, take no action.
    if (includeprims.getIsEmpty() && excludeprims.getIsEmpty())
	return success;
    for (auto &&sdfpath : sourceset)
    {
	//auto			 link = linkdefs->myLinkDefinitions.find(sdfpath);

	auto			 collection = husdGetCollectionAPI(myWriteLock, sdfpath, myLinkType, errors);

	auto			 stage = outdata->stage();
	auto			 prim = stage->GetPrimAtPath(sdfpath);

	if (!prim.IsValid())
	{
	    if (errors)
		errors->append("Invalid prim");
	    continue;
	}

	UT_StringArray		 includes;
	UT_StringArray		 excludes;

	includeprims.getCollectionAwarePaths(includes);
	excludeprims.getCollectionAwarePaths(excludes);

	// Get the link info or create a new one.
	auto & linkdata = getLinkData(
	    sdfpath, includes, excludes, myLinkType,
	    myWriteLock, myPrivate->myLinkDefinitions, errors);
	// If we're setting an explicit include for a rule then we should
	// clear the implicit include everything.
	if (!includeprims.getIsEmpty())
	    linkdata.myIncludeRoot = false;
	linkdata.myIncludes.addPattern(includes);
	linkdata.myExcludes.addPattern(excludes);
	{
	    // RKR This block is for debugging
	    UT_StringArray  tmp_inc, tmp_exc;
	    linkdata.myIncludes.getCollectionAwarePaths(tmp_inc);
	    linkdata.myExcludes.getCollectionAwarePaths(tmp_exc);
	    RKRCOUT(" link-include"
		    << " - " << collection.GetPath().GetString()
		    << ":" << collection.GetName()
		    << " INC: " << tmp_inc
		    << " EXC: " << tmp_exc
	    );
	}
    }
    return success;
}

bool
HUSD_EditLinkCollections::createCollections(UT_StringArray * errors)
{
    bool			 success = true;

    HUSD_EditCollections	 editor(myWriteLock);
    for (auto && linkpair : myPrivate->myLinkDefinitions)
    {
	auto			 collection = 
	    husdGetCollectionAPI(myWriteLock, linkpair.first, myLinkType, errors);

	if (linkpair.second.myIncludeRoot)
	{
	    UT_StringArray  rootPrim({"/"});
	    linkpair.second.myIncludes.addPattern(rootPrim);
	    RKRCOUT(" Adding ROOT"
		    << " - " << collection.GetPath().GetString()
		    << " : " << collection.GetName()
	    );
	}
	if (!editor.createCollection(
	    collection.GetPath().GetString().c_str(), collection.GetName().GetText(),
	    HUSD_Constants::getExpansionExpandPrims(),
	    linkpair.second.myIncludes, linkpair.second.myExcludes, true))
	{
	    //addError(LOP_COLLECTION_NOT_CREATED, parmset.myCollectionName);
	    RKRCOUT(" ERROR: failed to create"
		    << " - " << collection.GetPath().GetString()
		    << " : " << collection.GetName()
		    << " INC: " << linkpair.second.myIncludes.getLastError()
		    << " EXC: " << linkpair.second.myExcludes.getLastError()
	    );
	    return false;
	}
	else
	{
	    // RKR This block is for debugging
	    UT_StringArray  tmp_inc, tmp_exc;
	    linkpair.second.myIncludes.getCollectionAwarePaths(tmp_inc);
	    linkpair.second.myExcludes.getCollectionAwarePaths(tmp_exc);
	    RKRCOUT(" create"
		    << " - " << collection.GetPath().GetString()
		    << ":" << collection.GetName()
		    << " INC: " << tmp_inc
		    << " EXC: " << tmp_exc
	    );

	}
    }
    return success;
}
