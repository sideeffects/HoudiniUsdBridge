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

#include "BRAY_SdrKarmaParser.h"
#include <HUSD/XUSD_Format.h>
#include <BRAY/BRAY_Interface.h>
#include <VCC/VCC_Utils.h>
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

NDR_REGISTER_PARSER_PLUGIN(BRAY_SdrKarmaParser)

TF_DEFINE_PRIVATE_TOKENS(
    theTokens,

    ((discoveryTypeVex, "vex"))         // Compiled VEX code
    ((discoveryTypeVfl, "vfl"))         // VEX source code
    ((discoveryTypeKarma, "karma"))     // Built-in karma nodes
    ((sourceType, "VEX"))
);

const NdrTokenVec&
BRAY_SdrKarmaParser::GetDiscoveryTypes() const
{
    static const NdrTokenVec _DiscoveryTypes = {
	theTokens->discoveryTypeVex,
	theTokens->discoveryTypeVfl,
	theTokens->discoveryTypeKarma,
    };
    return _DiscoveryTypes;
}

const TfToken&
BRAY_SdrKarmaParser::GetSourceType() const
{
    return theTokens->sourceType;
}

BRAY_SdrKarmaParser::BRAY_SdrKarmaParser()
{
}

BRAY_SdrKarmaParser::~BRAY_SdrKarmaParser()
{
}

NdrNodeUniquePtr
BRAY_SdrKarmaParser::Parse(const NdrNodeDiscoveryResult& discoveryResult)
{
#if 0
    UTdebugFormat("Parse:");
    UTdebugFormat("   id  {}", discoveryResult.identifier);
    UTdebugFormat("   ver {}", discoveryResult.version.GetString());
    UTdebugFormat("   nam {}", discoveryResult.name);
    UTdebugFormat("   fam {}", discoveryResult.family);
    UTdebugFormat("   src {}", theTokens->sourceType);
    UTdebugFormat("   src {}", theTokens->sourceType);
    UTdebugFormat("   uri {}", discoveryResult.uri);
    UTdebugFormat("   Uri {}", discoveryResult.resolvedUri);
#endif
    if (discoveryResult.discoveryType == theTokens->discoveryTypeKarma)
    {
        // Built-in Karma node
        return NdrNodeUniquePtr(
                new SdrShaderNode(
                    discoveryResult.identifier,
                    discoveryResult.version,
                    discoveryResult.name,
                    discoveryResult.family,
                    theTokens->discoveryTypeKarma,
                    theTokens->discoveryTypeKarma,
                    discoveryResult.uri,
                    discoveryResult.resolvedUri,
                    getNodeProperties(discoveryResult),
                    NdrTokenMap(),
                    discoveryResult.sourceCode
            )
        );
    }

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
        "\n\tVEX Context:\t", info.getContextType(),
        "\n\tFn Name:\t", info.getFunctionName(),
        "\n\tParms:", parms.buffer());
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

template <typename ARRAY_T>
static inline VtValue
brayVtFromString(const ARRAY_T &vals, bool is_array)
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

static void
brayGetSdrTypeInfo(const BRAY::ShaderGraphPtr::NodeDecl::Parameter &parm,
        TfToken &type,
        VtValue &value,
        int &array_size)
{
    array_size = 0;
    if (parm.isReal())
    {
        const fpreal64  *fv = parm.myF.data();
        exint            size = parm.myF.size();

        type = SdrPropertyTypes->Float;
        switch (size)
        {
            case 1:
                value = VtValue(static_cast<float>(fv[0]));
                break;
            case 2:
                value = VtValue(GfVec2f(fv[0], fv[1]));
                break;
            case 3:
                value = VtValue(GfVec3f(fv[0], fv[1], fv[2]));
                break;
            case 4:
                value = VtValue(GfVec4f(fv[0], fv[1], fv[2], fv[3]));
                break;
            default:
            {
                array_size = size;
                VtArray<float>  array;
                array.assign(fv, fv+size);
                value = VtValue::Take(array);
                break;
            }
        }
    }
    else if (parm.isInt())
    {
        if (parm.myI.size() > 1)
            array_size = parm.myI.size();
        type = SdrPropertyTypes->Int;
        value = brayVtFromScalar<int>(parm.myI, parm.myI.size() > 1);
    }
    else if (parm.isBool())
    {
        if (parm.myB.size() > 1)
            array_size = parm.myB.size();
        type = SdrPropertyTypes->Int;
        value = brayVtFromScalar<int>(parm.myB, parm.myB.size() > 1);
    }
    else
    {
        UT_ASSERT(parm.isString());
        if (parm.myS.size() > 1)
            array_size = parm.myS.size();
        type = SdrPropertyTypes->String;
        value = brayVtFromString(parm.myS, parm.myS.size() > 1);
    }
}

static inline TfToken
brayGetSdfTypeName( const VCC_Utils::ShaderParmInfo &p )
{
    VEX_Type vex_type = p.getType();

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

    else if( p.getStructName() )
    {
	// Strip the "struct_" prefix, if any.
	if( p.getStructName().startsWith("struct_") )
	    return TfToken( std::string( p.getStructName().c_str() + 7, 
			p.getStructName().length() - 7));

	return TfToken( p.getStructName().toStdString() );
    }

    return TfToken( VEXgetType( vex_type ));
}

static NdrPropertyUniquePtrVec
propertiesFromBuiltin(const TfToken &name)
{
    const auto  *node = BRAY::ShaderGraphPtr::findNode(name.GetText());
    if (!node)
        return NdrPropertyUniquePtrVec();

    NdrPropertyUniquePtrVec properties;

    for (const auto &parm : node->inputs())
    {
        TfToken         name(parm.myName.toStdString());
        TfToken         type;
        VtValue         value;
        int             arr_size;
        NdrTokenMap     metadata;
        NdrTokenMap     hints;
        NdrOptionVec    options;

        brayGetSdrTypeInfo(parm, type, value, arr_size);

        properties.emplace_back(SdrShaderPropertyUniquePtr(
                    new SdrShaderProperty(name, type, value, false, arr_size,
                        metadata, hints, options)));
    }
    for (const auto &parm : node->outputs())
    {
        TfToken         name(parm.myName.toStdString());
        TfToken         type;
        VtValue         value;
        int             arr_size;
        NdrTokenMap     metadata;
        NdrTokenMap     hints;
        NdrOptionVec    options;

        brayGetSdrTypeInfo(parm, type, value, arr_size);

        properties.emplace_back(SdrShaderPropertyUniquePtr(
                    new SdrShaderProperty(name, type, value,
                        true, arr_size,
                        metadata, hints, options)));
    }
    return properties;
}

NdrPropertyUniquePtrVec
BRAY_SdrKarmaParser::getNodeProperties(const NdrNodeDiscoveryResult& discoveryResult)
{
    bool ok = false;

    VCC_Utils::ShaderInfo info;
    if (discoveryResult.uri == theTokens->discoveryTypeKarma.GetText())
    {
        return propertiesFromBuiltin(discoveryResult.identifier);
    }
    else if (!discoveryResult.uri.empty())
    {
	ok = VCC_Utils::getShaderInfoFromFile(info, discoveryResult.uri);
    }
    else if (!discoveryResult.sourceCode.empty())
    {
	ok = VCC_Utils::getShaderInfoFromCode(info, discoveryResult.sourceCode);
    }

    NdrPropertyUniquePtrVec properties;
    if( !ok )
	return properties;  // empty property list

    //brayDumpShaderInfo( info );
    for( auto &&p : info.getParameters() )
    {
	TfToken	    name( p.getName().toStdString() );
	TfToken	    type( brayGetSdfTypeName( p ));
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
