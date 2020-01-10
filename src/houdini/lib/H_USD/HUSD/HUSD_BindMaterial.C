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
 */


#include "HUSD_BindMaterial.h"

#include "HUSD_Constants.h"
#include "HUSD_EditCollections.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usdShade/materialBindingAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_BindMaterial::HUSD_BindMaterial( HUSD_AutoWriteLock &lock )
    : myWriteLock( lock )
    , myBindMethod( BindMethod::DIRECT )
    , myStrength( Strength::DEFAULT )
    , myPurpose( HUSD_Constants::getMatPurposeAll() )
    , myBindPrimPath("/geo")
{
}

bool
HUSD_BindMaterial::bind(const UT_StringRef &mat_prim_path,
	const UT_StringRef &geo_prim_path )const 
{
    HUSD_FindPrims  find_geo_prims( myWriteLock, geo_prim_path );

    return bind( mat_prim_path, find_geo_prims );
}

TfToken
husdGetStrengthToken( HUSD_BindMaterial::Strength strength )
{
    switch( strength )
    {
	case HUSD_BindMaterial::Strength::DEFAULT: 
	    return UsdShadeTokens->fallbackStrength;
	case HUSD_BindMaterial::Strength::STRONG:
	    return UsdShadeTokens->strongerThanDescendants;
	case HUSD_BindMaterial::Strength::WEAK:
	    return UsdShadeTokens->weakerThanDescendants;
	default:
	    break;
    };

    UT_ASSERT( !"Invalid strength" );
    return UsdShadeTokens->fallbackStrength;
}

static inline bool
husdGetStageAndMaterial( UsdStageRefPtr &stage, UsdShadeMaterial &material,
	const XUSD_DataPtr &data, const UT_StringRef &mat_prim_path )

{
    if( !data || !data->isStageValid() )
	return false;

    stage = data->stage();
    material = UsdShadeMaterial::Get(
	    stage, HUSDgetSdfPath(mat_prim_path.buffer()));
    if( !material.GetPrim().IsValid() )
	return false;

    return true;
}

static inline UsdPrim
husdGetBindPrim( UsdStageRefPtr &stage, const UT_StringRef &path,
	const HUSD_FindPrims *find_prims ) 
{
    UT_StringHolder final_path( path );
    if( !final_path.isstring() && find_prims )
    {
	final_path = find_prims->getSharedRootPrim();

	UT_WorkBuffer b;
	b.format("Binding primitive path not specified.\n Using: {}",
		final_path);
	HUSD_ErrorScope::addWarning( HUSD_ERR_STRING, b.buffer() );
    }
 
    return stage->GetPrimAtPath( HUSDgetSdfPath( final_path ));
}

static inline UsdCollectionAPI
husdGetBindCollection( UsdStageRefPtr &stage, HUSD_AutoWriteLock &lock, 
	const HUSD_FindPrims &find_geo_prims,
	UsdPrim &prim, const UT_StringRef &collection_name )
{
    UT_StringHolder path = find_geo_prims.getSingleCollectionPath();

    if( !path.isstring() )
    {
	HUSD_EditCollections	col_creator( lock );
	UT_StringHolder		prim_path( prim.GetPath().GetString() );

	if( !col_creator.createCollection( prim_path, collection_name,
		    HUSD_Constants::getExpansionExplicit(),
		    find_geo_prims, false ))
	{
	    return UsdCollectionAPI();
	}

	path = UsdCollectionAPI( prim, TfToken( collection_name ))
	    .GetCollectionPath().GetString();
    }

    return UsdCollectionAPI::GetCollection( stage, HUSDgetSdfPath( path ));
}

static inline void
husdSetMaterialBindingId(const UsdRelationship &rel,
        const UsdShadeMaterial &material)
{
    static SYS_AtomicCounter     theMaterialBindingIdCounter;
    auto                         prim = material.GetPrim();

    // If the material has an "id" value on it, copy it onto the binding
    // relationship so that the binding will be marked dirty by hydra, causing
    // the material to be re-populated, ensuring it is updated in the viewport.
    if (prim && prim.HasCustomDataKey(HUSDgetMaterialIdToken()))
    {
        VtValue      id = prim.GetCustomDataByKey(HUSDgetMaterialIdToken());

        rel.SetCustomDataByKey(HUSDgetMaterialIdToken(), id);
    }
    // Even if the material doesn't have an "id" value, set a unique binding
    // id value on the relationship. This is overkill for some situations and
    // will result in additional re-population of geometry, but covers a lot
    // more cases where updates might otherwise not happen.
    rel.SetCustomDataByKey(HUSDgetMaterialBindingIdToken(),
        VtValue(theMaterialBindingIdCounter.add(1)));
}

static inline bool
husdBindDirect( const UsdShadeMaterial &material, UsdPrim &prim,
	HUSD_BindMaterial::Strength strength, const UT_StringRef &purpose) 
{
    TfToken strength_token( husdGetStrengthToken( strength ));

    TfToken purpose_token( purpose );
    if( purpose_token.IsEmpty() )
	purpose_token = UsdShadeTokens->allPurpose;

    UsdShadeMaterialBindingAPI binding_api( prim );
    if (binding_api.Bind( material, strength_token, purpose_token ))
    {
        husdSetMaterialBindingId(
            binding_api.GetDirectBindingRel(purpose_token),
            material);
        return true;
    }

    return false;
}

static inline bool
husdBindDirect(const UsdStageRefPtr &stage,
        const UsdShadeMaterial &material,
	const HUSD_FindPrims &find_geo_prims,
	HUSD_BindMaterial::Strength strength,
        const UT_StringRef &purpose)
{
    for( auto &&sdfpath : find_geo_prims.getExpandedPathSet() )
    {
	auto prim = stage->GetPrimAtPath( sdfpath );

	if (!prim.IsValid())
	    return false;

	if( !husdBindDirect( material, prim, strength, purpose ))
	    return false;
    }

    return true;
}

static inline bool
husdBindCollection(const UsdStageRefPtr &stage, 
	const UsdShadeMaterial &material,
        const UsdCollectionAPI &collection,
	UsdPrim &bind_prim,
        const TfToken &binding_name,
	HUSD_BindMaterial::Strength strength,
        const UT_StringRef &purpose)
{
    TfToken strength_token(husdGetStrengthToken( strength));

    TfToken purpose_token(purpose);
    if( purpose_token.IsEmpty() )
	purpose_token = UsdShadeTokens->allPurpose;

    UsdShadeMaterialBindingAPI binding_api( bind_prim );
    if (binding_api.Bind(collection, material, binding_name,
            strength_token, purpose_token))
    {
        husdSetMaterialBindingId(
            binding_api.GetCollectionBindingRel(purpose_token, binding_name),
            material);
        return true;
    }

    return false;
}

bool
HUSD_BindMaterial::bind(const UT_StringRef &mat_prim_path,
	const HUSD_FindPrims &find_geo_prims) const
{
    auto outdata = myWriteLock.data();

    UsdStageRefPtr	stage;
    UsdShadeMaterial	material;
    if( !husdGetStageAndMaterial( stage, material, outdata, mat_prim_path ))
	return false;

    if( myBindMethod == BindMethod::DIRECT )
    {
	return husdBindDirect( stage, material, find_geo_prims, 
		myStrength, myPurpose );
    }
    else if( myBindMethod == BindMethod::COLLECTION )
    {
	auto binding_prim = husdGetBindPrim( stage, myBindPrimPath, 
		&find_geo_prims );
	if( !binding_prim || binding_prim.IsPseudoRoot() )
	{
	    HUSD_ErrorScope::addError( HUSD_ERR_STRING, 
		    "No valid primitive specified on which to define "
		    "a collection-based material binding.");
	    return false;
	}

	UT_StringHolder	collection_name( material.GetPath().GetName() );
	auto		collection = husdGetBindCollection( stage, myWriteLock,
				find_geo_prims, binding_prim, collection_name );

	return husdBindCollection(stage, material, collection, binding_prim,
            collection.GetName(), myStrength, myPurpose);
    }

    UT_ASSERT( !"Unknown binding method requested" );
    return false;
}

bool
HUSD_BindMaterial::bindAsCollection(const UT_StringRef &mat_prim_path, 
        const UT_StringRef &collection_path,
        const UT_StringRef &binding_prim_path,
        const UT_StringRef &binding_name) const
{
    auto outdata = myWriteLock.data();

    UsdStageRefPtr	stage;
    UsdShadeMaterial	material;
    if( !husdGetStageAndMaterial( stage, material, outdata, mat_prim_path ))
	return false;

    auto binding_prim = stage->GetPrimAtPath(
        HUSDgetSdfPath(binding_prim_path));
    if (!binding_prim)
        return false;

    auto collection = UsdCollectionAPI::GetCollection(stage,
        HUSDgetSdfPath(collection_path));
    if (!collection)
        return false;

    return husdBindCollection(stage, material, collection, binding_prim,
        TfToken(binding_name.toStdString()), myStrength, myPurpose);
}

static inline bool
husdBindGeoSubset(const UsdStageRefPtr &stage, const UsdShadeMaterial &material,
	UsdGeomImageable &geo, const UT_ExintArray &face_indices,
	HUSD_BindMaterial::Strength strength, const UT_StringRef &purpose )
{
    VtIntArray vt_face_indices;
    vt_face_indices.assign( face_indices.begin(), face_indices.end() );

    TfToken subset_name( material.GetPath().GetNameToken() );
    UsdShadeMaterialBindingAPI geo_binding_api( geo.GetPrim() );

    UsdGeomSubset geo_subset = 
	geo_binding_api.CreateMaterialBindSubset(subset_name, vt_face_indices);

    TfToken strength_token( husdGetStrengthToken( strength ));
    TfToken purpose_token( purpose );
    if( purpose_token.IsEmpty() )
	purpose_token = UsdShadeTokens->allPurpose;

    UsdShadeMaterialBindingAPI subset_binding_api( geo_subset.GetPrim() );
    if (subset_binding_api.Bind( material, strength_token, purpose_token ))
    {
        husdSetMaterialBindingId(
            subset_binding_api.GetDirectBindingRel(purpose_token),
            material);
        return true;
    }

    return false;
}

bool
HUSD_BindMaterial::bindSubset(const UT_StringRef &mat_prim_path, 
	const UT_StringRef &geo_prim_path,
	const UT_ExintArray *face_indices) const
{
    if( !face_indices )
    {
	HUSD_FindPrims	find_geo_prims( myWriteLock, geo_prim_path );
	return bind( mat_prim_path, find_geo_prims );
    }

    auto		outdata = myWriteLock.data();
    UsdStageRefPtr	stage;
    UsdShadeMaterial	material;
    if( !husdGetStageAndMaterial( stage, material, outdata, mat_prim_path ))
	return false;

    UsdGeomImageable geo = UsdGeomImageable::Get( 
	    stage, HUSDgetSdfPath( geo_prim_path ));
    if( !geo.GetPrim().IsValid() )
	return false;

    return husdBindGeoSubset( stage, material, geo, *face_indices,
	    myStrength, myPurpose );
}

