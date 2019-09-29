/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 */

#include "HUSD_GeoSubset.h"

#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include "XUSD_AttributeUtils.h"
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/subset.h>


PXR_NAMESPACE_USING_DIRECTIVE


HUSD_GeoSubset::HUSD_GeoSubset( HUSD_AutoWriteLock &lock )
    : myWriteLock( lock )
    , myFamilyType( FamilyType::UNRESTRICTED )
{
}

static inline TfToken
husdGetFamilyTypeToken( HUSD_GeoSubset::FamilyType family_type)
{
    if( family_type == HUSD_GeoSubset::FamilyType::PARTITION )
	return UsdGeomTokens->partition;
    if( family_type == HUSD_GeoSubset::FamilyType::NONOVERLAPPING )
	return UsdGeomTokens->nonOverlapping;
    if( family_type == HUSD_GeoSubset::FamilyType::UNRESTRICTED )
	return UsdGeomTokens->unrestricted;

    UT_ASSERT( !"Invalid geometry subset family type" );
    return TfToken();
}

bool
HUSD_GeoSubset::createGeoSubset( const UT_StringRef &prim_path,
	const UT_ExintArray &face_indices,
	const UT_StringRef &subset_name ) const
{
    if( !subset_name.isstring() )
    {
	UT_ASSERT( !"invalid geometry subset name" );
	return false;
    }

    const XUSD_DataPtr &data = myWriteLock.data();
    if( !data || !data->isStageValid() )
	return false;

    SdfPath		sdf_path = HUSDgetSdfPath( prim_path );
    UsdGeomImageable	geo = UsdGeomImageable::Get( data->stage(), sdf_path );
    if( !geo )
	return false;

    VtIntArray vt_indices;
    vt_indices.assign( face_indices.begin(), face_indices.end() );

    TfToken family_type_token = husdGetFamilyTypeToken( myFamilyType ); 

    return (bool) UsdGeomSubset::CreateGeomSubset( geo,
	    TfToken( subset_name ), UsdGeomTokens->face, vt_indices, 
	    TfToken( myFamilyName ), family_type_token );
}

bool
HUSD_GeoSubset::getGeoPrimitiveAndFaceIndices( 
	UT_StringHolder &geo_prim_path, UT_ExintArray &face_indices,
	const UT_StringRef &subset_prim_path, 
	const HUSD_TimeCode &time_code ) const
{
    const XUSD_DataPtr &data = myWriteLock.data();
    if( !data || !data->isStageValid() )
	return false;

    SdfPath	    sdf_path = HUSDgetSdfPath( subset_prim_path );
    UsdGeomSubset   subset = UsdGeomSubset::Get( data->stage(), sdf_path );
    if( !subset )
	return false;

    geo_prim_path = subset.GetPrim().GetParent().GetPath().GetString();

    UsdAttribute    attrib = subset.GetIndicesAttr();
    UsdTimeCode	    usd_time_code = HUSDgetNonDefaultUsdTimeCode( time_code );
    HUSDgetAttribute( attrib, face_indices, usd_time_code );

    return true;
}

