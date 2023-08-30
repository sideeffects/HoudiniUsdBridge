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

#include "HUSD_EditCollections.h"
#include "HUSD_EditCustomData.h"
#include "HUSD_FindPrims.h"
#include "HUSD_FindProps.h"
#include "HUSD_Info.h"
#include "HUSD_Path.h"
#include "HUSD_PathSet.h"
#include "HUSD_Preferences.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <UT/UT_Debug.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/relationship.h>
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

    UsdCollectionAPI
    getCollectionAPI(HUSD_AutoWriteLock &lock, const UT_StringRef &path)
    {
        if (!HUSDisValidCollectionPath(path))
            return UsdCollectionAPI();

        auto data = lock.data();
        if (!data || !data->isStageValid())
            return UsdCollectionAPI();

        SdfPath		 sdfpath(HUSDgetSdfPath(path));
        auto		 stage = data->stage();
        UsdCollectionAPI api = UsdCollectionAPI::GetCollection(stage, sdfpath);
        if(api)
            return api;

        UT_StringHolder	primpath, collectionname;
        if (!HUSDsplitCollectionPath( primpath, collectionname, path ))
            return UsdCollectionAPI();

        SdfPath		sdfprimpath(HUSDgetSdfPath(primpath));
        auto		prim = stage->GetPrimAtPath(sdfprimpath);
        if (!prim)
            return UsdCollectionAPI();

        TfToken		name(collectionname.toStdString());
        return UsdCollectionAPI::Apply(prim, name);
    }

    void
    expandCollectionPaths(
            UsdStageRefPtr &stage,
            const SdfPath &expandcollectionpath,
            const XUSD_PathSet &pathset,
            SdfPathVector &pathvector)
    {
        // If the path set contains the collection path, remove that path and
        // replace it with all the members of that collection.
        if (pathset.find(expandcollectionpath) != pathset.end())
        {
            UsdCollectionAPI collectionapi =
                UsdCollectionAPI::Get(stage, expandcollectionpath);

            if (collectionapi)
            {
                collectionapi.GetIncludesRel().GetTargets(&pathvector);
                pathvector.reserve(pathvector.size() + pathset.size() - 1);
                for (auto &&path : pathset)
                {
                    if (path != expandcollectionpath)
                        pathvector.push_back(path);
                }
            }
        }
        else
        {
            pathvector.insert(pathvector.end(),
                pathset.begin(), pathset.end());
        }
    }

}

HUSD_EditCollections::HUSD_EditCollections(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_EditCollections::~HUSD_EditCollections()
{
}

bool
HUSD_EditCollections::createCollection(const UT_StringRef &primpath,
	const UT_StringRef &collectionname,
	const UT_StringRef &expansionrule,
	const HUSD_FindPrims &includeprims,
	const HUSD_FindPrims &excludeprims,
        bool setexcludes,
	bool createprim,
	bool forceapply /*=true*/)
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	SdfPath	 sdfpath(HUSDgetSdfPath(primpath));
	auto	 stage = outdata->stage();
	auto	 prim = stage->GetPrimAtPath(sdfpath);

	// If the prim doesn't exist, create it.
	if (!prim && createprim)
        {
            std::string primtype = 
                HUSD_Preferences::defaultCollectionsPrimType().toStdString();

            SdfPrimSpecHandle primspec = HUSDcreatePrimInLayer(
                stage, outdata->activeLayer(), sdfpath, TfToken(),
                SdfSpecifierDef, SdfSpecifierDef, primtype);
            if (primspec)
            {
                if (!primtype.empty())
                    primspec->SetTypeName(primtype);
                prim = stage->GetPrimAtPath(sdfpath);
            }
        }

	if (prim)
	{
	    TfToken	     name_token(collectionname.toStdString());
	    TfTokenVector    name_vector =
		SdfPath::TokenizeIdentifierAsTokens(name_token);
            SdfPath          collectionpath = HUSDgetSdfPath(
                HUSDmakeCollectionPath(primpath, collectionname));

	    // Converting the collection name to a token vector is what the
	    // CollectionsAPI does to validate the collection name, so do the
	    // same thing here. Applying a collection with an invalid name
            // results in a USD "coding error" which always goes to stdout,
            // so we want to avoid that by doing the same check here.
	    if (name_vector.size() > 0)
	    {
		// If the collection already exists (for example how the LightAPI
		// provides a 'lightLink' collection as part of its schema), it's
		// arguably redundant to call Apply. While it should generally be
		// safe to still make the call, and there are multi-layer workflows
		// where it may be better/safer to always do so, we've identified
		// one instance where the redundant call actually caused an issue:
		// https://forum.aousd.org/t/light-linking-compatibility-when-moving-to-23-08/343/3
		// In general we still promote a workflow of always calling Apply,
		// with `forceapply==false` seen as the special-case exception.
		UsdCollectionAPI collection;
		if (!forceapply)
		    collection = UsdCollectionAPI::Get(prim, name_token);
		if (!collection)
		    collection = UsdCollectionAPI::Apply(prim, name_token);

		if (collection)
		{
		    VtValue exprule = VtValue(TfToken(expansionrule));
		    collection.CreateExpansionRuleAttr(exprule).Set(exprule);

		    SdfPathVector includepaths;
		    UsdRelationship includerel =
			collection.CreateIncludesRel();
		    const XUSD_PathSet &includeset = includeprims.
                        getCollectionAwarePathSet().sdfPathSet();
                    const XUSD_PathSet &includemissingset = includeprims.
                        getMissingExplicitPathSet().sdfPathSet();
		    const SdfPath &rootpath =
			SdfPath::AbsoluteRootPath();
                    bool includeroot = false;

                    expandCollectionPaths(stage, collectionpath,
                        includeset, includepaths);
                    includepaths.insert(includepaths.end(),
                        includemissingset.begin(), includemissingset.end());
		    // The root path can't be included in the list of
		    // targets. There is a special attribute for it.
		    if (includeset.find(rootpath) != includeset.end())
		    {
			auto it = std::find(includepaths.begin(),
			    includepaths.end(), rootpath);

			if (it != includepaths.end())
			{
			    includepaths.erase(it);
                            includeroot = true;
			}
		    }
		    success = includerel.SetTargets(includepaths);

                    if (setexcludes)
                    {
                        // For the "exclude" specification, we have to get the
                        // expanded path set, not the collection-aware path
                        // set.  This is because USD collections do not support
                        // the use of collections in the exclude specification.
                        const XUSD_PathSet &excludeset = excludeprims.
                            getExpandedPathSet().sdfPathSet();
                        const XUSD_PathSet &excludemissingset = excludeprims.
                            getMissingExplicitPathSet().sdfPathSet();
                        if (!excludeset.empty() || !excludemissingset.empty())
                        {
                            // We have been asked to exclude specific prims.
                            SdfPathVector excludepaths;
                            UsdRelationship excluderel =
                                collection.CreateExcludesRel();

                            // Note we don't need to call expandCollectionPaths
                            // here because we aren't using the
                            // collection-aware path set, we have to use the
                            // expanded path set.
                            excludepaths.insert(excludepaths.end(),
                                excludeset.begin(),
                                excludeset.end());
                            excludepaths.insert(excludepaths.end(),
                                excludemissingset.begin(),
                                excludemissingset.end());
                            // The root path can't be included in the list of
                            // targets. There is a special attribute for it.
                            if (excludeset.find(rootpath) != excludeset.end())
                            {
                                auto it = std::find(excludepaths.begin(),
                                    excludepaths.end(), rootpath);

                                if (it != excludepaths.end())
                                {
                                    excludepaths.erase(it);
                                    includeroot = false;
                                }
                            }

                            success |= excluderel.SetTargets(excludepaths);
                        }
                        else
                        {
                            // We have been told to exclude nothing. But we
                            // still need to check if there is an existing
                            // exclude rel, in case we are overwriting an
                            // existing collection. Clear it if it exists.
                            UsdRelationship excluderel =
                                collection.GetExcludesRel();

                            if (excluderel)
                                excluderel.SetTargets({});
                        }
                    }

                    // Check if there is already an include root attribute.
                    auto includerootattr = collection.GetIncludeRootAttr();

                    if (includerootattr.IsValid())
                    {
                        // If the include root value doesn't match what we
                        // want, we need to change its value here.
                        bool oldincluderoot = false;
                        includerootattr.Get(&oldincluderoot);
                        if (includeroot != oldincluderoot)
                            includerootattr.Set(includeroot);
                    }
                    else if (includeroot)
                    {
                        // If there is no include root attr, we only need to
                        // create one if we want to set the value to true.
                        collection.CreateIncludeRootAttr(VtValue(true));
                    }
		}
	    }
	}
    }

    return success;
}

bool
HUSD_EditCollections::createCollection(const UT_StringRef &primpath,
	const UT_StringRef &collectionname,
	const UT_StringRef &expansionrule,
	const HUSD_FindPrims &includeprims,
	bool createprim,
	bool forceapply /*=true*/)
{
    return createCollection(primpath, collectionname, expansionrule,
	includeprims, HUSD_FindPrims(myWriteLock), true, createprim, forceapply);
}

bool
HUSD_EditCollections::setCollectionExpansionRule( 
	const UT_StringRef &collectionpath, const UT_StringRef &expansionrule)
{
    TfToken rule(expansionrule.toStdString());
    if( rule != UsdTokens->explicitOnly &&
	rule != UsdTokens->expandPrims &&
	rule != UsdTokens->expandPrimsAndProperties )
    {
	UT_ASSERT( !"Invalid expansion rule" );
	return false;
    }

    auto api = getCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    return (bool) api.CreateExpansionRuleAttr(VtValue(rule));
}

bool
HUSD_EditCollections::setCollectionIncludes( 
	const UT_StringRef &collectionpath, const UT_StringArray &paths)
{
    auto api = getCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    SdfPathVector sdfpaths = HUSDgetSdfPaths(paths);
    return api.CreateIncludesRel().SetTargets(sdfpaths);
}

bool
HUSD_EditCollections::addCollectionInclude( 
	const UT_StringRef &collectionpath, const UT_StringRef &path)
{
    auto api = getCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    return api.IncludePath(HUSDgetSdfPath(path));
}

bool
HUSD_EditCollections::setCollectionExcludes( 
	const UT_StringRef &collectionpath, const UT_StringArray &paths)
{
    auto api = getCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    SdfPathVector sdfpaths = HUSDgetSdfPaths(paths);
    return api.CreateExcludesRel().SetTargets(sdfpaths);
}

bool
HUSD_EditCollections::addCollectionExclude( 
	const UT_StringRef &collectionpath, const UT_StringRef &path)
{
    auto api = getCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    return api.ExcludePath(HUSDgetSdfPath(path));
}

bool
HUSD_EditCollections::setCollectionIcon(
        const UT_StringRef &collectionpath,
        const UT_StringHolder &icon)
{
    auto api = getCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    HUSD_EditCustomData editcustomdata(myWriteLock);
    UsdRelationship includes = api.CreateIncludesRel();
    HUSD_FindProps findprops(myWriteLock,
        includes.GetPrimPath().GetText(),
        includes.GetName().GetText());

    return editcustomdata.setIconCustomData(findprops, icon);
}

