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
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <UT/UT_Debug.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/relationship.h>
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

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
	bool createprim)
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
	    prim = stage->DefinePrim(sdfpath);

	if (prim)
	{
	    TfToken	     name_token(collectionname.toStdString());
	    TfTokenVector    name_vector =
		SdfPath::TokenizeIdentifierAsTokens(name_token);

	    // Converting the collection name to a token vector is what the
	    // CollectionsAPI does to validate the collection name, so do the
	    // same thing here. There is a bug in 0.8.4 where the success of
	    // this tokenization is not checked, resulting in a possible crash.
	    if (name_vector.size() > 0)
	    {
		UsdCollectionAPI collection =
		    UsdCollectionAPI::ApplyCollection(prim, name_token);

		if (collection)
		{
		    VtValue exprule = VtValue(TfToken(expansionrule));
		    collection.CreateExpansionRuleAttr(exprule).Set(exprule);

		    SdfPathVector includepaths;
		    UsdRelationship includerel =
			collection.CreateIncludesRel();
		    const XUSD_PathSet &includeset =
			includeprims.getCollectionAwarePathSet().sdfPathSet();
		    const SdfPath &rootpath =
			SdfPath::AbsoluteRootPath();
                    bool includeroot = false;

		    includepaths.insert(includepaths.end(),
			includeset.begin(), includeset.end());
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

		    // For the "exclude" specification, we have to get the
		    // expanded path set, not the collection-aware path set.
		    // This is because USD collections do not support the use
		    // of collections in the exclude specification.
		    const XUSD_PathSet &excludeset =
			excludeprims.getExpandedPathSet().sdfPathSet();
		    if (!excludeset.empty())
		    {
			// We have been asked to exclude specific prims.
			SdfPathVector excludepaths;
			UsdRelationship excluderel =
			    collection.CreateExcludesRel();

			excludepaths.insert(excludepaths.end(),
			    excludeset.begin(), excludeset.end());
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
			    excluderel.BlockTargets();
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
	bool createprim)
{
    return createCollection(primpath, collectionname, expansionrule,
	includeprims, HUSD_FindPrims(myWriteLock), createprim);
}

static inline UsdCollectionAPI
husdGetCollectionAPI(HUSD_AutoWriteLock &lock, const UT_StringRef &path)
{
    if (!HUSDisValidCollectionPath(path))
	return UsdCollectionAPI();

    auto data = lock.data();
    if (!data || !data->isStageValid())
	return UsdCollectionAPI();

    SdfPath		sdfpath(HUSDgetSdfPath(path));
    auto		stage = data->stage();
    UsdCollectionAPI	api = UsdCollectionAPI::GetCollection(stage, sdfpath);
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
    return UsdCollectionAPI::ApplyCollection(prim, name);
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

    auto api = husdGetCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    return (bool) api.CreateExpansionRuleAttr(VtValue(rule));
}

bool
HUSD_EditCollections::setCollectionIncludes( 
	const UT_StringRef &collectionpath, const UT_StringArray &paths)
{
    auto api = husdGetCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    SdfPathVector sdfpaths = HUSDgetSdfPaths(paths);
    return api.CreateIncludesRel().SetTargets(sdfpaths);
}

bool
HUSD_EditCollections::addCollectionInclude( 
	const UT_StringRef &collectionpath, const UT_StringRef &path)
{
    auto api = husdGetCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    return api.IncludePath(HUSDgetSdfPath(path));
}

bool
HUSD_EditCollections::setCollectionExcludes( 
	const UT_StringRef &collectionpath, const UT_StringArray &paths)
{
    auto api = husdGetCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    SdfPathVector sdfpaths = HUSDgetSdfPaths(paths);
    return api.CreateExcludesRel().SetTargets(sdfpaths);
}

bool
HUSD_EditCollections::addCollectionExclude( 
	const UT_StringRef &collectionpath, const UT_StringRef &path)
{
    auto api = husdGetCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    return api.ExcludePath(HUSDgetSdfPath(path));
}

bool
HUSD_EditCollections::setCollectionIcon(
        const UT_StringRef &collectionpath,
        const UT_StringHolder &icon)
{
    auto api = husdGetCollectionAPI(myWriteLock, collectionpath);
    if( !api )
	return false;

    HUSD_EditCustomData editcustomdata(myWriteLock);
    UsdRelationship includes = api.CreateIncludesRel();
    HUSD_FindProps findprops(myWriteLock,
        includes.GetPrimPath().GetText(),
        includes.GetName().GetText());

    return editcustomdata.setIconCustomData(findprops, icon);
}

