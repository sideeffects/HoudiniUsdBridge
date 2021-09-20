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
#include "HUSD_PathSet.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <UT/UT_Debug.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdGeom/mesh.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_BindMaterial::HUSD_BindMaterial( HUSD_AutoWriteLock &lock )
    : myWriteLock( lock )
    , myBindMethod( BindMethod::DIRECT )
    , myBindCollectionExpand( true )
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

SdfPath
husdMakeValidSdfPath( const UT_StringRef &path )
{
    UT_String path_str( path.c_str() );
    HUSDmakeValidUsdPath( path_str, true );
    return HUSDgetSdfPath( path_str );
}

static inline bool
husdGetStage( UsdStageRefPtr &stage, const XUSD_DataPtr &data )

{
    if( !data || !data->isStageValid() )
    {
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, "Invalid stage.");
	return false;
    }

    stage = data->stage();
    return true;
}

static inline bool
husdGetMaterial( UsdShadeMaterial &material, const UsdStageRefPtr &stage, 
	const UT_StringRef &mat_prim_path )

{
    SdfPath sdf_path = husdMakeValidSdfPath( mat_prim_path );
    material = UsdShadeMaterial::Get( stage, sdf_path );
    if( !material )
    {
        HUSD_ErrorScope::addWarning(HUSD_ERR_CANT_FIND_PRIM,
		sdf_path.GetText());
	return false;
    }

    return true;
}

static inline bool
husdGetMaterial( UsdShadeMaterial &material, const UsdStageRefPtr &stage, 
	const UT_StringRef &base_prim_path, const UT_StringRef &mat_prim_path )

{
    UT_WorkBuffer abs_prim_path;
    abs_prim_path.append( base_prim_path );
    abs_prim_path.append( '/' );
    abs_prim_path.append( mat_prim_path );

    return husdGetMaterial( material, stage, abs_prim_path );
}

static inline bool
husdGetStageAndMaterial( UsdStageRefPtr &stage, UsdShadeMaterial &material,
	const XUSD_DataPtr &data, const UT_StringRef &mat_prim_path )

{
    if( !data || !data->isStageValid() )
    {
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, "Invalid stage.");
	return false;
    }

    stage = data->stage();
    material = UsdShadeMaterial::Get( stage, HUSDgetSdfPath(mat_prim_path));
    if( !material.GetPrim().IsValid() )
        HUSD_ErrorScope::addWarning(HUSD_ERR_CANT_FIND_PRIM,
		mat_prim_path.c_str());

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
	b.format("Binding primitive path not specified.\n Using: {}.",
		final_path);
	HUSD_ErrorScope::addWarning( HUSD_ERR_STRING, b.buffer() );
    }
 
    return stage->GetPrimAtPath( HUSDgetSdfPath( final_path ));
}

static inline UsdCollectionAPI
husdGetBindCollection( UsdStageRefPtr &stage, HUSD_AutoWriteLock &lock, 
	const HUSD_FindPrims &find_geo_prims,
	UsdPrim &prim, const UT_StringRef &collection_name,
        bool collection_expand_prims )
{
    UT_StringHolder path = find_geo_prims.getSingleCollectionPath();

    if( !path.isstring() )
    {
	HUSD_EditCollections	col_creator( lock );
	UT_StringHolder		prim_path( prim.GetPath().GetString() );

	if( !col_creator.createCollection( prim_path, collection_name,
                    collection_expand_prims
                        ? HUSD_Constants::getExpansionExpandPrims()
                        : HUSD_Constants::getExpansionExplicit(),
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
    auto                         prim = material.GetPrim();

    // If the material has an "id" value on it, copy it onto the binding
    // relationship so that the binding will be marked dirty by hydra, causing
    // the material to be re-populated, ensuring it is updated in the viewport.
    if (prim && prim.HasCustomDataKey(HUSDgetMaterialIdToken()))
    {
        VtValue      id = prim.GetCustomDataByKey(HUSDgetMaterialIdToken());

        rel.SetCustomDataByKey(HUSDgetMaterialIdToken(), id);
    }
}

static inline TfToken
husdGetBindPurposeToken( const UT_StringRef &purpose )
{
    TfToken result( purpose );
    if( result.IsEmpty() )
	result = UsdShadeTokens->allPurpose;

    return result;
}

static inline bool
husdBindDirect( const UsdShadeMaterial &material, UsdPrim &prim,
	HUSD_BindMaterial::Strength strength, const UT_StringRef &purpose) 
{
    TfToken strength_token( husdGetStrengthToken( strength ));
    TfToken purpose_token( husdGetBindPurposeToken( purpose ));

    auto binding_api = UsdShadeMaterialBindingAPI::Apply( prim );
    if (!binding_api ||
	!binding_api.Bind( material, strength_token, purpose_token ))
    {
	UT_WorkBuffer msg;
	msg.format( "Failed to bind material '{0}' to primitive '{1}'.",
		material.GetPath().GetText(), prim.GetPath().GetText() );
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, msg.buffer() );
	return false;
    }
    
    husdSetMaterialBindingId(
	binding_api.GetDirectBindingRel(purpose_token),
	material);
    return true;
}

static inline bool
husdBindDirect(const UsdStageRefPtr &stage,
	const UT_StringRef &mat_prim_path,
	const HUSD_FindPrims &find_geo_prims,
	HUSD_BindMaterial::Strength strength,
        const UT_StringRef &purpose)
{
    UsdShadeMaterial material;

    bool abs_path = UT_StringWrap( mat_prim_path ).isAbsolutePath( false );
    if( abs_path && !husdGetMaterial( material, stage, mat_prim_path ))
	return true; // husdGetMaterial() has already added a warning

    for( auto &&sdfpath : find_geo_prims.getExpandedPathSet().sdfPathSet() )
    {
	auto prim = stage->GetPrimAtPath( sdfpath );

	if( !abs_path && !husdGetMaterial( material, 
		    stage, sdfpath.GetText(), mat_prim_path ))
	    continue; // husdGetMaterial() has already added a warning

	if( !husdBindDirect( material, prim, strength, purpose ))
	    return false;
    }

    return true;
}

static inline bool
husdBindCollection(const UsdStageRefPtr &stage, 
	const UT_StringRef &mat_prim_path,
        const UsdCollectionAPI &collection,
	UsdPrim &bind_prim,
        const TfToken &binding_name,
	HUSD_BindMaterial::Strength strength,
        const UT_StringRef &purpose)
{
    UsdShadeMaterial material;
    if( !husdGetMaterial( material, stage, mat_prim_path ))
	return true; // husdGetMaterial() has already added a warning

    TfToken strength_token(husdGetStrengthToken( strength));
    TfToken purpose_token( husdGetBindPurposeToken( purpose ));

    auto binding_api = UsdShadeMaterialBindingAPI::Apply( bind_prim );
    if (!binding_api ||
	!binding_api.Bind(collection, material, binding_name,
            strength_token, purpose_token))
    {
	UT_WorkBuffer msg;
	msg.format( "Failed to bind material '{0}' to collection '{1}'\n"
		"on primitive '{2}'.",
		material.GetPath().GetText(), 
		collection.GetPath().GetText(),
		bind_prim.GetPath().GetText() );
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, msg.buffer() );
	return false;
    }

    husdSetMaterialBindingId(
	    binding_api.GetCollectionBindingRel(binding_name, purpose_token),
	    material);
    return true;
}

bool
HUSD_BindMaterial::bind(const UT_StringRef &mat_prim_path,
	const HUSD_FindPrims &find_geo_prims) const
{
    auto outdata = myWriteLock.data();
    UsdStageRefPtr stage;
    if( !husdGetStage( stage, outdata ))
	return false;

    if( myBindMethod == BindMethod::DIRECT )
    {
	return husdBindDirect( stage, mat_prim_path, find_geo_prims, 
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

	UT_StringHolder	collection_name( myBindName );
	if( !collection_name )
	    collection_name = husdMakeValidSdfPath(mat_prim_path).GetName();

	auto collection = husdGetBindCollection( stage, myWriteLock,
		find_geo_prims, binding_prim, collection_name,
		myBindCollectionExpand );

	return husdBindCollection(stage, mat_prim_path, collection, 
		binding_prim, collection.GetName(), myStrength, myPurpose);
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
    UsdStageRefPtr stage;
    if( !husdGetStage( stage, outdata ))
	return false;

    auto binding_prim = stage->GetPrimAtPath(
        HUSDgetSdfPath(binding_prim_path));
    if (!binding_prim)
    {
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, 
		"No valid primitive specified on which to define "
		"a collection-based material binding.");
	return false;
    }

    auto collection = UsdCollectionAPI::GetCollection(stage,
        HUSDgetSdfPath(collection_path));
    if (!collection)
    {
	UT_WorkBuffer msg;
	msg.format( "Unable to find collection: '{0}'.", 
		collection_path.c_str());
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, msg.buffer() );
        return false;
    }

    return husdBindCollection(stage, mat_prim_path, collection, binding_prim,
        TfToken(binding_name.toStdString()), myStrength, myPurpose);
}

static inline void 
husdMakeUniqueGeoSubsetName(TfToken &name, const UsdPrim &parent)
{
    UT_String tmp( name.GetString() );
    tmp.append( "_sub0" );

    while( parent.GetChild( name ))
    {
	tmp.incrementNumberedName();
	name = TfToken( tmp.toStdString() );
    }
}

static inline bool
husdBindGeoSubset(const UsdStageRefPtr &stage, const UsdShadeMaterial &material,
	UsdGeomImageable &geo, const UT_ExintArray &face_indices,
	HUSD_BindMaterial::Strength strength, const UT_StringRef &purpose )
{
    VtIntArray vt_face_indices;
    vt_face_indices.assign( face_indices.begin(), face_indices.end() );

    TfToken subset_name( material.GetPath().GetNameToken() );
    husdMakeUniqueGeoSubsetName( subset_name, geo.GetPrim() );
    auto geo_binding_api = UsdShadeMaterialBindingAPI::Apply( geo.GetPrim() );

    UsdGeomSubset geo_subset;
    if( geo_binding_api )
	geo_subset = geo_binding_api.CreateMaterialBindSubset(
		subset_name, vt_face_indices);

    TfToken strength_token( husdGetStrengthToken( strength ));
    TfToken purpose_token( husdGetBindPurposeToken( purpose ));

    UsdShadeMaterialBindingAPI subset_binding_api;
    if( geo_subset )
	subset_binding_api = UsdShadeMaterialBindingAPI::Apply( 
		geo_subset.GetPrim() );

    if (!subset_binding_api ||
	!subset_binding_api.Bind( material, strength_token, purpose_token ))
    {
	UT_WorkBuffer msg;
	msg.format( "Failed to bind material '{0}' to geometry subset '{1}'\n",
		material.GetPath().GetText(), 
		geo_subset.GetPath().GetText() );
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, msg.buffer() );
	return false;
    }

    husdSetMaterialBindingId(
	    subset_binding_api.GetDirectBindingRel(purpose_token),
	    material);
    return true;
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
    if( !material.GetPrim().IsValid() )
        return true;

    UsdGeomMesh geo = UsdGeomMesh::Get(
	    stage, HUSDgetSdfPath( geo_prim_path ));
    if( !geo )
    {
        HUSD_ErrorScope::addWarning(HUSD_ERR_SUBSETS_ONLY_ON_MESH_PRIMITIVES,
		geo_prim_path.c_str());
        return false;
    }

    return husdBindGeoSubset( stage, material, geo, *face_indices,
	    myStrength, myPurpose );
}

static inline UsdRelationship
husdGetAuthoredDirectBinding( UsdPrim &prim, const TfToken &purpose ) 
{
    UsdShadeMaterialBindingAPI api(prim);

    SdfPathVector targets;
    auto rel = api.GetDirectBindingRel( purpose );
    if( !rel || !rel.GetTargets(&targets) || targets.size() == 0 )
	return UsdRelationship(); // invalid relationship

    return rel;
}

static inline bool
husdIsStrongerThanDesc( const UsdRelationship &rel )
{
    return rel 
	&& UsdShadeMaterialBindingAPI::GetMaterialBindingStrength(rel) ==
		UsdShadeTokens->strongerThanDescendants;
}

// Inspired by USD materialBindingAPI data structures.
// Cache is a map from a prim path to the effective direct binding relationship.
using husd_BindingCache = UT_Map<SdfPath, UsdRelationship, SdfPath::Hash>;
using husd_BindingRelList  = UT_Array<UsdRelationship>;


static inline UsdRelationship
husdFindDirectBindingToTransfer( UsdPrim prim, const UT_Set<UsdPrim> &leaf_set,
	const TfToken &purpose, husd_BindingCache &bindings_cache,
	husd_BindingRelList *found_bindings )
{
    // Quick check in the cache.
    auto it = bindings_cache.find( prim.GetPath() );
    if( it != bindings_cache.end() )
	return it->second;

    // Find all potential material bindings up the tree hierachy.
    UT_Array<UsdPrim>		prims_stack;
    UT_Array<UsdRelationship>	bindings_stack;
    for( auto p = prim; !p.IsPseudoRoot(); p = p.GetParent() )
    {
	// Check the cache because ancestor may have been resolved already.
	auto it = bindings_cache.find( p.GetPath() );
	if( it != bindings_cache.end() )
	{
	    prims_stack.append( UsdPrim() ); // empty; not to re-add to cache
	    bindings_stack.append( it->second );
	    break; 
	}

	// Get material binding on iterated prim, if any.
	auto rel = husdGetAuthoredDirectBinding( p, purpose );
	if( found_bindings && rel )
	    found_bindings->append( rel );

	// Stash binding to resolve later.
	prims_stack.append( p );
	bindings_stack.append( rel );
    }

    // Cascade the material down the prims, depending on binding strength.
    UT_ASSERT( bindings_stack.size() == prims_stack.size() );
    for( exint i = bindings_stack.size() - 1; i > 0; i-- )
    {
	// Edge case, but we don't want to transfer bindings past leaves.
	// Otherwise, we may transfer a material to some descendent (eg,
	// a sibling of `prim`), thus not really blocking the look on that
	// descendent; and it should be blocked because of blocking `p`.
	if( prims_stack[i] && leaf_set.contains( prims_stack[i] ) )
	    bindings_stack[i] = UsdRelationship();

	// Trickle down the material.
	if( !bindings_stack[i-1] || husdIsStrongerThanDesc( bindings_stack[i] ))
	    bindings_stack[i-1] = bindings_stack[i];
    }

    // Add the resolved material bindings to cache.
    UT_ASSERT( bindings_stack.size() == prims_stack.size() );
    for( exint i = 0, n = bindings_stack.size(); i < n; i++ )
	if( prims_stack[i] )
	    bindings_cache[ prims_stack[i].GetPath() ] = bindings_stack[i];


    // Binding at index 0 corresponds to the effective material for `prim`.
    return bindings_stack[0];
}

static inline bool
husdBlockBinding( UsdRelationship &rel )
{
    if( !rel.SetTargets({}) )
    {
	UT_WorkBuffer msg;
	msg.format( "Failed to unbind '{0}' on primitive '{1}'.",
		rel.GetName().GetText(),
		rel.GetPrim().GetPath().GetText() );
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, msg.buffer() );
	return false;
    }

    return true;
}

static inline bool
husdBlockDirectBindingIfAuthored( UsdPrim prim, const TfToken &purpose ) 
{
    auto rel = husdGetAuthoredDirectBinding( prim, purpose );
    if( !rel )
	return true; // no authored binding to block
	
    return husdBlockBinding( rel );
}

static inline void
husdTransferDirectBinding( UsdRelationship &src_binding, UsdPrim prim, 
	const UT_Set<UsdPrim> &leaf_set, const UT_Set<UsdPrim> &ancestor_set, 
	const TfToken &purpose )
{
    SdfPathVector targets;
    src_binding.GetTargets( &targets );

    TfToken strength;
    src_binding.GetMetadata( UsdShadeTokens->bindMaterialAs, &strength );

    bool is_src_stronger = husdIsStrongerThanDesc( src_binding );

    // Transfer binding to children.
    for( auto &&child : prim.GetChildren() )
    {
	// But skip transfering to some children.
	if( leaf_set.contains( child ) 
	    || ancestor_set.contains( child )
	    || (!is_src_stronger 
		&& husdGetAuthoredDirectBinding( child, purpose )))
	{
	    continue;
	}

	auto applied_api = UsdShadeMaterialBindingAPI::Apply( child );
	UT_ASSERT( applied_api );
	if( !applied_api )
	    continue;

	auto dst_rel = applied_api.GetDirectBindingRel( purpose );
	dst_rel.SetTargets( targets );
	if( !strength.IsEmpty() )
	     dst_rel.SetMetadata( UsdShadeTokens->bindMaterialAs, strength );
    }
}

static inline void
husdFindAndTransferDirectBindings( 
	const UT_Set<UsdPrim> &leaf_set, const UT_Set<UsdPrim> &ancestor_set, 
	const TfToken &purpose, husd_BindingRelList *found_bindings )
{
    // Transfer any materials bound directly on ancestors, to children
    // that were not selected for un-binding. This preserves their look,
    // when we block the material binding on ancestors, later on.
    husd_BindingCache bindings_cache;
    for( auto &&prim : ancestor_set )
    {
	auto src_binding = husdFindDirectBindingToTransfer( prim, 
		leaf_set, purpose, bindings_cache, found_bindings );
	if( !src_binding )
	    continue; // no material binding to transfer

	husdTransferDirectBinding( src_binding, prim, leaf_set, ancestor_set, 
		purpose );
    }
}

static inline void
husdRemovePrimFromBindingCollections( UsdPrim prim, const TfToken &purpose )
{
    auto prim_path = prim.GetPath();
    for( auto p = prim; !p.IsPseudoRoot(); p = p.GetParent() )
    {
	UsdShadeMaterialBindingAPI api(p);

	auto bindings = api.GetCollectionBindings( purpose );
	for( auto &&binding : bindings )
	{
	    // Note, ExludePath() checks for membership, so no need to
	    // duplicate the work here. Just call it for any prim.
	    auto collection = binding.GetCollection();
	    if( collection )
		collection.ExcludePath( prim_path );
	}
    }
}

static inline bool
husdTransferAndBlockDirectBindings( UT_Set<UsdPrim> &leaf_set,
    UT_Set<UsdPrim> &ancestor_set, const TfToken &purpose ) 
{
    bool ok = true;

    // Transfer any direct binding to non-ancestor-set prims,
    // to preserve the look of prims that we are not unbinding.
    husd_BindingRelList	bindings_to_block;
    husdFindAndTransferDirectBindings( leaf_set, ancestor_set, purpose,
	    &bindings_to_block );

    // After transfering, block the direct binding on all prims that should
    // not have any bound materials.
    for( auto &&rel : bindings_to_block )
	ok = husdBlockBinding( rel ) && ok;

    return ok;
}

static inline bool
husdUnbindAllMatsForPurpose( UT_Set<UsdPrim> &leaf_set,
    UT_Set<UsdPrim> &ancestor_set, const TfToken &purpose ) 
{
    // Transfer and unbind any direct bindings on ancestors.
    bool ok = husdTransferAndBlockDirectBindings( leaf_set, ancestor_set, 
	    purpose );

    // Unbind any direct bindings on primitives selected for un-assignment.
    for( auto &&prim : leaf_set )
	ok = husdBlockDirectBindingIfAuthored( prim, purpose ) && ok;

    // Look for any collection-based bindings and remove leaf prims from them.
    for( auto &&prim : leaf_set )
	husdRemovePrimFromBindingCollections( prim, purpose );

    return ok;
}

static inline void
husdFindCurrentBindings( 
	UT_Array<UsdRelationship> &direct_bindings,
	UT_Array<UsdCollectionAPI> &bind_collections,
	UT_Array<UsdPrim> &member_prims, 
	const TfToken &purpose,
	const UT_Set<UsdPrim> &leaf_set )
{
    std::vector<UsdPrim> prims;
    for( auto &&prim : leaf_set )
	prims.push_back(prim);

    std::vector<UsdRelationship> binding_rels;
    auto mats = UsdShadeMaterialBindingAPI::ComputeBoundMaterials( 
	    prims, purpose, &binding_rels );

    UT_Set<UsdPrim> direct_binding_set;
    UT_ASSERT( mats.size() == prims.size() );
    UT_ASSERT( mats.size() == binding_rels.size() );
    for( int i = 0; i < mats.size(); i++ )
    {
	// Check if material was bound to the primitive.
	if( !mats[i] )
	    continue;

	const UsdRelationship &rel = binding_rels[i];
	const UsdPrim &prim = prims[i];
	UT_ASSERT( prim && rel );

	auto name_vec = SdfPath::TokenizeIdentifierAsTokens( rel.GetName() );
	if( name_vec.size() <= 3 )
	{
	    // As per UsdShadeMaterialBindingAPI docs, 
	    // direct binding has three or less name components.
	    if( !direct_binding_set.contains( rel.GetPrim() ))
	    {
		direct_binding_set.insert( rel.GetPrim() );
		direct_bindings.append( rel );
	    }
	}
	else
	{
	    UsdShadeMaterialBindingAPI::CollectionBinding coll_binding( rel );
	    UsdCollectionAPI coll = coll_binding.GetCollection();
	    member_prims.append( prim );
	    bind_collections.append( coll );
	}
    }
}

static inline bool
husdIsAncestor( UsdPrim ancestor, UsdPrim descendant )
{
    for( auto p = descendant; !p.IsPseudoRoot(); p = p.GetParent() )
	if( p == ancestor )
	    return true;
    return false;
}

static inline bool
husdUnbindCurrentMat( UT_Set<UsdPrim> &leaf_set, UT_Set<UsdPrim> &ancestor_set,
	const TfToken &purpose, int *found) 
{
    bool ok = true;

    UT_Array<UsdRelationship>	direct_bindings;
    UT_Array<UsdCollectionAPI>	bind_collections;
    UT_Array<UsdPrim>		member_prims;
    husdFindCurrentBindings( direct_bindings, bind_collections, member_prims, 
	    purpose, leaf_set );
    UT_ASSERT( bind_collections.size() == member_prims.size() );

    if( found )
	*found = (direct_bindings.size() > 0 || bind_collections.size() > 0);

    for( auto && rel : direct_bindings )
    {
	UsdShadeMaterialBindingAPI::DirectBinding dir_binding( rel );

	// Transfer any direct binding to non-ancestor-set prims,
	// to preserve the look of prims that we are not unbinding.
	// But only for ancestors that are affected by direct binding.
	for( auto &&prim : ancestor_set )
	{
	    if( husdIsAncestor( rel.GetPrim(), prim ))
		husdTransferDirectBinding( rel, prim, leaf_set, ancestor_set, 
			dir_binding.GetMaterialPurpose() );
	}

	ok = husdBlockBinding( rel ) && ok;
    }

    UT_ASSERT( bind_collections.size() == member_prims.size() );
    for( int i = 0; i < bind_collections.size(); i++ )
	ok = bind_collections[i].ExcludePath( member_prims[i].GetPath() ) && ok;
	
    return ok;
}

static inline bool
husdGetPrimsToUnbind( UT_Set<UsdPrim> &leaf_set, UT_Set<UsdPrim> &ancestor_set,
	HUSD_AutoWriteLock &lock, const HUSD_FindPrims &find_prims )
{
    // Get the stage to operate on.
    auto data = lock.data();
    if( !data || !data->isStageValid() )
    {
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, "Invalid stage.");
	return false;
    }

    // Get list of prims to un-assign materials from.
    UsdStageRefPtr stage = data->stage();
    for( auto &&sdfpath : find_prims.getExpandedPathSet().sdfPathSet() )
    {
	auto prim = stage->GetPrimAtPath( sdfpath );
	leaf_set.insert( prim );
    }

    // Get a list of ancestors that will need to transfer material binding 
    // to its children, and which cannot have any direct bindings anymore
    // (otherwise it may affect the descendent leaves).
    for( auto &&prim : leaf_set )
    {
	for( auto p = prim.GetParent(); !p.IsPseudoRoot(); p = p.GetParent() )
	{ 
	    // If p is a leaf then it does not want to transfer material to 
	    // children, since we want to unbind it altogether as is.
	    if( leaf_set.contains( p ))
		break;

	    // If it has been already traversed upwards, no need to do it again.
	    if( ancestor_set.contains( p ))
		break; 
	    
	    ancestor_set.insert( p );
	}
    }

    return true;
}

bool
HUSD_BindMaterial::unbindAll( const HUSD_FindPrims &find_prims ) const
{
    UT_Set<UsdPrim> leaf_set, ancestor_set;
    if( !husdGetPrimsToUnbind( leaf_set, ancestor_set, myWriteLock, find_prims))
	return false;

    // Clear the bindings for all known standard purposes. 
    // Note, if we need to support non-standard purposes, we will need to
    // interrogate each prim as we traverse, and unbind each discovered
    // purpose separately, rather than explicitly listing purposes here.
    bool ok = true;
    std::vector<TfToken> purposes{ UsdShadeTokens->allPurpose,
	UsdShadeTokens->full, UsdShadeTokens->preview };
    for( auto const & purpose: purposes )
	ok &= husdUnbindAllMatsForPurpose( leaf_set, ancestor_set, purpose );

    return ok;
}

bool
HUSD_BindMaterial::unbind( const HUSD_FindPrims &find_prims,
	const UT_StringHolder &purpose, int unbind_limit ) const
{ 
    UT_Set<UsdPrim> leaf_set, ancestor_set;
    if( !husdGetPrimsToUnbind( leaf_set, ancestor_set, myWriteLock, find_prims))
	return false;

    TfToken purp_tk(purpose.toStdString());
    bool ok = true;
    for( int i = 0, found = true; i < unbind_limit && found; i++ )
	ok &= husdUnbindCurrentMat( leaf_set, ancestor_set, purp_tk, &found );

    return ok;
}

