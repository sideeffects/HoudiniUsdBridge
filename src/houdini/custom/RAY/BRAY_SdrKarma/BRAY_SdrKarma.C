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
 */

#include "BRAY_SdrKarma.h"
#include <VCC/VCC_Utils.h>
#include <VEX/VEX_ContextManager.h>
#include <VEX/VEX_Types.h>
#include <UT/UT_Debug.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/ndr/debugCodes.h>
#include <pxr/usd/ndr/nodeDiscoveryResult.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(BRAY_SdrKarma)

TF_DEFINE_PRIVATE_TOKENS(
    theTokens,

    ((discoveryTypeVex, "vex"))
    ((discoveryTypeVfl, "vfl"))
    ((sourceType, "VEX"))
);

const NdrTokenVec&
BRAY_SdrKarma::GetDiscoveryTypes() const
{
    static const NdrTokenVec _DiscoveryTypes = {
	theTokens->discoveryTypeVex,
	theTokens->discoveryTypeVfl,
    };
    return _DiscoveryTypes;
}

const TfToken&
BRAY_SdrKarma::GetSourceType() const
{
    return theTokens->sourceType;
}

BRAY_SdrKarma::BRAY_SdrKarma()
{
}

BRAY_SdrKarma::~BRAY_SdrKarma()
{
}

NdrNodeUniquePtr
BRAY_SdrKarma::Parse(const NdrNodeDiscoveryResult& discoveryResult)
{
    return NdrNodeUniquePtr(
        new SdrShaderNode(
            discoveryResult.identifier,
            discoveryResult.version,
            discoveryResult.name,
            discoveryResult.family,
            theTokens->sourceType,
            theTokens->sourceType,
            discoveryResult.uri,
            discoveryResult.resolvedUri,
            getNodeProperties(discoveryResult),
            NdrTokenMap(),
            discoveryResult.sourceCode
        )
    );
}

static inline void
brayDumpShaderInfo( const VCC_Utils::ShaderInfo &info )
{
    UT_WorkBuffer parms;
    for( auto &&p : info.getParameters() )
    {
	parms.append( "\n\t\t" );

	if( p.isExport() )
	    parms.append( "export\t" );
	else
	    parms.append( "      \t" );

	parms.append( VEXgetType( p.getType() ));
	parms.append( "\t" );
	parms.append( p.getName() );

	if( p.isArray() )
	{
	    parms.append( "[" );
	    parms.appendSprintf( "%" SYS_PRId64, p.getArraySize() );
	    parms.append( "]" );
	}

	parms.append( " val:");
	if( p.getType() == VEX_TYPE_STRING )
	{
	    for( auto &&v : p.getStringValues() )
		parms.appendSprintf( " %s", v.c_str() );
	}
	else if( p.getType() == VEX_TYPE_INTEGER )
	{
	    for( auto &&v : p.getIntValues() )
		parms.appendSprintf( " %d", v );
	}
	else 
	{
	    for( auto &&v : p.getFloatValues() )
		parms.appendSprintf( " %f", v );
	}
    }


    UTdebugPrintCd(none, "\nVEX Shader info:",
	    "\n\tVEX Context:\t", 
	    VEX_ContextManager::getNameFromContextType(
		info.getContextType()),
	    "\n\tFn Name:\t", info.getFunctionName(),
	    "\n\tParms:", parms.buffer() 
	    );
}

template <typename VT, typename UT>
inline VtValue
brayVtFromScalar( const UT &vals, bool is_array )
{
    if( is_array )
    {
	VtArray<VT> array;
	array.assign( vals.begin(), vals.end() );
	return VtValue::Take( array );
    }
    else if( vals.size() > 0 )
    {
	// Cast mainly for casting from 'double' to 'float'.
	return VtValue( static_cast<VT>( vals[0] ));
    }
    else
    {
	return VtValue();
    }
}

static inline VtValue
brayVtFromString( const UT_StringArray &vals, bool is_array )
{
    if( is_array )
    {
	VtStringArray array;
	array.reserve( vals.size() );
	for( auto &&s : vals )
	    array.push_back( s.toStdString() );
	return VtValue::Take( array );
    }
    else if( vals.size() > 0 )
    {
	return VtValue( vals[0].toStdString() );
    }
    else
    {
	return VtValue();
    }
}

template <typename VT>
inline VtValue
brayVtFromVector( const UT_DoubleArray &vals, bool is_array )
{
    auto n = VT::dimension;

    if( is_array )
    {
	VtArray<VT> array( vals.size() / n );
	for( int i = 0; i < array.size(); i++ )
	{
	    VT v;
	    for( int j = 0; j < n; j++ )
		v[j] = vals[ n*i + j ];
	    array[i] = std::move(v);
	}
	return VtValue::Take( array );
    }
    else if( vals.size() > 0 )
    {
	VT v;
	for( int i = 0; i < n; i++ )
	    v[i] = vals[i];
	return VtValue::Take( v );
    }
    else
    {
	return VtValue();
    }
}

template <typename VT>
inline VtValue
brayVtFromMatrix( const UT_DoubleArray &vals, bool is_array )
{
    auto n = VT::numRows * VT::numColumns;

    if( is_array )
    {
	VtArray<VT> array( vals.size() / n);
	for( int i = 0; i < array.size(); i++ )
	{
	    VT m;
	    double *v = m.GetArray();
	    for( int j = 0; j < n; j++ )
		v[j] = vals[ n*i + j ];
	    array[i] = std::move(m);
	}
	return VtValue::Take( array );
    }
    else if( vals.size() > 0 )
    {
	VT m;
	double *v = m.GetArray();
	for( int i = 0; i < n; i++ )
	    v[i] = vals[i];
	return VtValue::Take( m );
    }
    else
    {
	return VtValue();
    }
}

static inline VtValue
brayGetDefaultValue( const VCC_Utils::ShaderParmInfo &p )
{
    if( p.getType() == VEX_TYPE_INTEGER )
	return brayVtFromScalar<int>( p.getIntValues(), p.isArray() );

    else if( p.getType() == VEX_TYPE_FLOAT )
	return brayVtFromScalar<float>( p.getFloatValues(), p.isArray() );

    else if( p.getType() == VEX_TYPE_STRING )
	return brayVtFromString( p.getStringValues(), p.isArray() );

    else if( p.getType() == VEX_TYPE_VECTOR2 )
	return brayVtFromVector<GfVec2f>( p.getFloatValues(), p.isArray() );

    else if( p.getType() == VEX_TYPE_VECTOR )
	return brayVtFromVector<GfVec3f>( p.getFloatValues(), p.isArray() );

    else if( p.getType() == VEX_TYPE_VECTOR4 )
	return brayVtFromVector<GfVec4f>( p.getFloatValues(), p.isArray() );

    else if( p.getType() == VEX_TYPE_MATRIX2 )
	return brayVtFromMatrix<GfMatrix2d>( p.getFloatValues(), p.isArray() );

    else if( p.getType() == VEX_TYPE_MATRIX3 )
	return brayVtFromMatrix<GfMatrix3d>( p.getFloatValues(), p.isArray() );

    else if( p.getType() == VEX_TYPE_MATRIX4 )
	return brayVtFromMatrix<GfMatrix4d>( p.getFloatValues(), p.isArray() );

    return VtValue();
}

static inline TfToken
brayGetSdfTypeName( VEX_Type vex_type )
{
    if( vex_type == VEX_TYPE_INTEGER )
	return SdrPropertyTypes->Int;

    else if( vex_type == VEX_TYPE_FLOAT )
	return SdrPropertyTypes->Float;

    else if( vex_type == VEX_TYPE_STRING )
	return SdrPropertyTypes->String;

    // Note, not in SdrPropertyTypes so using SdfValueTypeNames.
    else if( vex_type == VEX_TYPE_VECTOR2 )
	return SdfValueTypeNames->Float2.GetAsToken();

    else if( vex_type == VEX_TYPE_VECTOR )
	return SdrPropertyTypes->Vector;

    // Note, not in SdrPropertyTypes so using SdfValueTypeNames.
    else if( vex_type == VEX_TYPE_VECTOR4 )
	return SdfValueTypeNames->Float4.GetAsToken();

    // Note, not in SdrPropertyTypes so using SdfValueTypeNames.
    else if( vex_type == VEX_TYPE_MATRIX2 )
	return SdfValueTypeNames->Matrix2d.GetAsToken();

    // Note, not in SdrPropertyTypes so using SdfValueTypeNames.
    else if( vex_type == VEX_TYPE_MATRIX3 )
	return SdfValueTypeNames->Matrix3d.GetAsToken();

    else if( vex_type == VEX_TYPE_MATRIX4 )
	return SdrPropertyTypes->Matrix;

    return TfToken( VEXgetType( vex_type ));
}

NdrPropertyUniquePtrVec
BRAY_SdrKarma::getNodeProperties(const NdrNodeDiscoveryResult& discoveryResult)
{
    bool ok = false;

    VCC_Utils::ShaderInfo info;
    if( !discoveryResult.uri.empty() )
	ok = VCC_Utils::getShaderInfoFromFile( info, discoveryResult.uri );
    else if( !discoveryResult.sourceCode.empty() )
	ok = VCC_Utils::getShaderInfoFromCode(info, discoveryResult.sourceCode);

    NdrPropertyUniquePtrVec properties;
    if( !ok )
	return properties;  // empty property list

    //brayDumpShaderInfo( info );
    for( auto &&p : info.getParameters() )
    {
	TfToken	    name( p.getName().toStdString() );
	TfToken	    type( brayGetSdfTypeName( p.getType() ));
	VtValue	    value( brayGetDefaultValue( p ));
	size_t	    arr_size = p.isArray() ? p.getArraySize() : 0;
	NdrTokenMap metadata;

	// USD's Sdr concludes that parm is an array if arr_size > 0 or 
	// if the metadata indicates that parm is a dynamic array.
	// In VEX, the default array may be empty (ie, size = 0), but VEX
	// shader will accept a non-empty array as argument, 
	// ie, all VEX array parameters are "dynamic". So set the metadata.
	if( p.isArray() )
	    metadata[ SdrPropertyMetadata->IsDynamicArray ] = "true";

	properties.emplace_back( SdrShaderPropertyUniquePtr(
		    new SdrShaderProperty( name, type, value,
			p.isExport(), arr_size,
			metadata , NdrTokenMap(), NdrOptionVec() )));
    }


    return properties;
}

PXR_NAMESPACE_CLOSE_SCOPE
