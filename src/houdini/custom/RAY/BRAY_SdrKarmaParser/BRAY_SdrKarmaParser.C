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
#include <VCC/VCC_Utils.h>
#include <VEX/VEX_Types.h>
#include <UT/UT_Debug.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_IStream.h>
#include <UT/UT_ZString.h>
#include <UT/UT_StringStream.h>
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

namespace
{
    class KarmaInput
    {
    public:
        KarmaInput() = default;
        bool    load(const UT_JSONValue &v)
        {
            const UT_JSONValueArray     *arr = v.getArray();
            UT_ASSERT(arr && arr->size() >= 3);
            if (!arr || arr->size() < 3)
                return false;
            UT_WorkBuffer       style;
            UT_VERIFY(arr->get(0)->import(myName));
            UT_VERIFY(arr->get(1)->import(style));
            return loadDefault(*style.buffer(), *arr->get(2));
        }

        bool    loadDefault(char type, const UT_JSONValue &def)
        {
            mySize = 1;
            if (def.getArray())
                mySize = def.getArray()->size();
            switch (type)
            {
                case 'F': return loadFloat(def);
                case 'I': return loadInt(def);
                case 'B': return loadBool(def);
                case 'S': return loadString(def);
            }
            UTdebugFormat("Bad type: {}", type);
            UT_ASSERT(0);
            return false;
        }
        bool    loadFloat(const UT_JSONValue &def)
        {
            myF = UTmakeUnique<fpreal64[]>(mySize);
            if (mySize > 1)
                return loadArray(myF.get(), def);
            return def.import(*myF.get());
        }
        bool    loadBool(const UT_JSONValue &def)
        {
            myB = UTmakeUnique<bool[]>(mySize);
            if (mySize > 1)
                return loadArray(myB.get(), def);
            return def.import(*myB.get());
        }
        bool    loadInt(const UT_JSONValue &def)
        {
            myI = UTmakeUnique<int64[]>(mySize);
            if (mySize > 1)
                return loadArray(myI.get(), def);
            return def.import(*myI.get());
        }
        bool    loadString(const UT_JSONValue &def)
        {
            myS = UTmakeUnique<UT_StringHolder[]>(mySize);
            if (mySize > 1)
                return loadArray(myS.get(), def);
            return def.import(*myS.get());
        }

        bool    isFloat() const { return myF.get() != nullptr; }
        bool    isBool() const { return myB.get() != nullptr; }
        bool    isInt() const { return myI.get() != nullptr; }
        bool    isString() const { return myS.get() != nullptr; }

        const fpreal64          *fbegin() const { return myF.get(); }
        const fpreal64          *fend() const { return myF.get() + mySize; }
        const int64             *ibegin() const { return myI.get(); }
        const int64             *iend() const { return myI.get() + mySize; }
        const bool              *bbegin() const { return myB.get(); }
        const bool              *bend() const { return myB.get() + mySize; }
        const UT_StringHolder   *sbegin() const { return myS.get(); }
        const UT_StringHolder   *send() const { return myS.get() + mySize; }

        template <typename T>
        bool    loadArray(T *result, const UT_JSONValue &val)
        {
            const UT_JSONValueArray     *arr = val.getArray();
            UT_ASSERT(arr && arr->size() == mySize);
            if (!arr)
                return false;
            if (arr->size() != mySize)
                return false;
            for (int i = 0; i < mySize; ++i)
            {
                if (!arr->get(i)->import(result[i]))
                {
                    UT_ASSERT(0);
                    return false;
                }
            }
            return true;
        }

        UT_StringHolder                 myName;
        UT_UniquePtr<fpreal64[]>        myF;
        UT_UniquePtr<int64[]>           myI;
        UT_UniquePtr<bool[]>            myB;
        UT_UniquePtr<UT_StringHolder[]> myS;
        int                             mySize;
    };

    using KarmaOutput = KarmaInput;
    struct KarmaNode
    {
        bool    load(const UT_StringHolder &str)
        {
            UT_AutoJSONParser   j(str.c_str(), str.length());
            UT_JSONValue        contents;
            if (!contents.parseValue(*j))
            {
                UTdebugFormat("ERROR Loading JSON: '{}'", str);
                return false;
            }
            return load(contents);
        }
        bool    load(const UT_JSONValue &val)
        {
            const UT_JSONValueArray     *arr = val.getArray();
            UT_ASSERT(arr && arr->size() >= 3);
            if (!arr || arr->size() < 3)
                return false;
            UT_VERIFY(arr->get(0)->import(myName));
            const UT_JSONValueArray     *iarr = arr->get(1)->getArray();
            const UT_JSONValueArray     *oarr = arr->get(2)->getArray();
            UT_ASSERT(iarr && oarr);
            if (!iarr || !oarr)
                return false;
            myInputSize = iarr->size();
            myOutputSize = oarr->size();
            myInputs = UTmakeUnique<KarmaInput[]>(myInputSize);
            myOutputs = UTmakeUnique<KarmaInput[]>(myOutputSize);
            if (!loadInputs(myInputs.get(), *iarr))
                return false;
            if (!loadInputs(myOutputs.get(), *oarr))
                return false;
            return true;
        }
        bool    loadInputs(KarmaInput *inputs, const UT_JSONValueArray &arr)
        {
            for (int i = 0, n = arr.size(); i < n; ++i)
            {
                if (!inputs[i].load(*arr.get(i)))
                {
                    UT_ASSERT(0);
                    return false;
                }
                UT_ASSERT(inputs[i].isFloat()
                        || inputs[i].isInt()
                        || inputs[i].isBool()
                        || inputs[i].isString());
            }
            return true;
        }
        const KarmaInput        *ibegin() { return myInputs.get(); }
        const KarmaInput        *iend() { return myInputs.get() + myInputSize; }
        const KarmaInput        *obegin() { return myOutputs.get(); }
        const KarmaInput        *oend() { return myOutputs.get() + myOutputSize; }
        UT_StringHolder                 myName;
        UT_UniquePtr<KarmaInput[]>      myInputs;
        UT_UniquePtr<KarmaOutput[]>     myOutputs;
        int                             myInputSize;
        int                             myOutputSize;
    };
}

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(BRAY_SdrKarmaParser)

TF_DEFINE_PRIVATE_TOKENS(
    theTokens,

    ((discoveryTypeVex, "vex"))         // Compiled VEX code
    ((discoveryTypeVfl, "vfl"))         // VEX source code
    ((discoveryTypeKarma, "karma"))     // Built-in karma nodes
    ((sourceType, "VEX"))
);

// Help for UT_Format()
#define FORMAT_TYPE(TYPE) \
    static SYS_FORCE_INLINE size_t \
    format(char *buffer, size_t bufsize, const TYPE &v) \
    { \
        UT::Format::Writer      writer(buffer, bufsize); \
        UT::Format::Formatter<> f; \
        UT_OStringStream        os; \
        os << v << std::ends; \
        return f.format(writer, "{}", {os.str()}); \
    } \
    /* end macro */

FORMAT_TYPE(TfToken)
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

template <typename VT, typename UT>
inline VtValue
brayVtFromScalar(const UT *begin, const UT *end)
{
    if (end == begin + 1)
        return VtValue(VT(*begin));
    VtArray<VT> arr;
    arr.assign(begin, end);
    return VtValue::Take(arr);
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
brayGetSdrTypeInfo(const KarmaInput &parm,
        TfToken &type,
        VtValue &value,
        int &array_size)
{
    array_size = 0;
    if (parm.isFloat())
    {
        const fpreal64  *fv = parm.myF.get();
        exint            size = parm.mySize;

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
        array_size = parm.mySize > 1 ? parm.mySize : 0;
        type = SdrPropertyTypes->Int;
        value = brayVtFromScalar<int>(parm.ibegin(), parm.iend());
    }
    else if (parm.isBool())
    {
        array_size = parm.mySize > 1 ? parm.mySize : 0;
        type = SdrPropertyTypes->Int;
        value = brayVtFromScalar<int>(parm.bbegin(), parm.bend());
    }
    else
    {
        array_size = parm.mySize > 1 ? parm.mySize : 0;
        UT_ASSERT(parm.isString());
        type = SdrPropertyTypes->String;
        value = brayVtFromScalar<std::string>(parm.sbegin(), parm.send());
        UT_ASSERT(parm.mySize == 1 || value.IsHolding<VtStringArray>());
    }
}

static inline TfToken
brayGetSdrTypeName( const VCC_Utils::ShaderParmInfo &p )
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


static inline TfToken
brayGetSdfTypeName( const VCC_Utils::ShaderParmInfo &p )
{
    VEX_Type vex_type = p.getType();

    if( vex_type == VEX_TYPE_INTEGER )
        if( p.isArray() )
            return SdfValueTypeNames->IntArray.GetAsToken();
        else
            return SdfValueTypeNames->Int.GetAsToken();

    else if( vex_type == VEX_TYPE_FLOAT )
        if( p.isArray() )
            return SdfValueTypeNames->FloatArray.GetAsToken();
        else
            return SdfValueTypeNames->Float.GetAsToken();

    else if( vex_type == VEX_TYPE_STRING )
        if( p.isArray() )
            return SdfValueTypeNames->StringArray.GetAsToken();
        else
            return SdfValueTypeNames->String.GetAsToken();

    else if( vex_type == VEX_TYPE_VECTOR2 )
        if( p.isArray() )
            return SdfValueTypeNames->Float2Array.GetAsToken();
        else
            return SdfValueTypeNames->Float2.GetAsToken();

    else if( vex_type == VEX_TYPE_VECTOR )
        if( p.isArray() )
            return SdfValueTypeNames->Float3Array.GetAsToken();
        else
            return SdfValueTypeNames->Float3.GetAsToken();

    else if( vex_type == VEX_TYPE_VECTOR4 )
        if( p.isArray() )
            return SdfValueTypeNames->Float4Array.GetAsToken();
        else
            return SdfValueTypeNames->Float4.GetAsToken();

    else if( vex_type == VEX_TYPE_MATRIX2 )
        if( p.isArray() )
            return SdfValueTypeNames->Matrix2dArray.GetAsToken();
        else
            return SdfValueTypeNames->Matrix2d.GetAsToken();

    else if( vex_type == VEX_TYPE_MATRIX3 )
        if( p.isArray() )
            return SdfValueTypeNames->Matrix3dArray.GetAsToken();
        else
            return SdfValueTypeNames->Matrix3d.GetAsToken();

    else if( vex_type == VEX_TYPE_MATRIX4 )
        if( p.isArray() )
            return SdfValueTypeNames->Matrix4dArray.GetAsToken();
        else
            return SdfValueTypeNames->Matrix4d.GetAsToken();

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

namespace
{
    bool
    getMetadata(const TfToken &key, std::string &value,
            const NdrTokenMap &metadata)
    {
        auto it = metadata.find(key);
        if (it == metadata.end())
            return false;
        value = it->second;
        return true;
    }
}

static NdrPropertyUniquePtrVec
propertiesFromBuiltin(const TfToken &name, const NdrTokenMap &metadata)
{
    static const TfToken    theKarmaRep("karma_rep", TfToken::Immortal);
    static const TfToken    theKarmaRepOLen("karma_rep_olen",
                                        TfToken::Immortal);
    std::string olen_str, krep_str;

    KarmaNode   knode;
    if (!getMetadata(theKarmaRep, krep_str, metadata)
            || !getMetadata(theKarmaRepOLen, olen_str, metadata))
        return NdrPropertyUniquePtrVec();

    {
        int             olen = SYSatoi(olen_str.c_str());
        UT_ZString      zs(UTmakeUnsafeRef(UT_StringRef(krep_str)),
                            UT_ZString::GZIP, UT::source_is_compressed(), olen);
        if (!knode.load(zs.uncompress()))
        {
            UTdebugFormat("JSON load Failed: {} '{}'", name, olen_str);
            return NdrPropertyUniquePtrVec();
        }
    }

    NdrPropertyUniquePtrVec properties;

    for (auto &&it = knode.ibegin(), end = knode.iend(); it != end; ++it)
    {
        TfToken         name(it->myName.toStdString());
        TfToken         type;
        VtValue         value;
        int             arr_size;
        NdrTokenMap     hints;
        NdrOptionVec    options;

        brayGetSdrTypeInfo(*it, type, value, arr_size);

        properties.emplace_back(SdrShaderPropertyUniquePtr(
                    new SdrShaderProperty(name, type, value,
                        false, arr_size, metadata, hints, options)));
    }
    for (auto &&it = knode.obegin(), end = knode.oend(); it != end; ++it)
    {
        TfToken         name(it->myName.toStdString());
        TfToken         type;
        VtValue         value;
        int             arr_size;
        NdrTokenMap     metadata;
        NdrTokenMap     hints;
        NdrOptionVec    options;

        brayGetSdrTypeInfo(*it, type, value, arr_size);

        properties.emplace_back(SdrShaderPropertyUniquePtr(
                    new SdrShaderProperty(name, type, value,
                        true, arr_size, metadata, hints, options)));
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
        return propertiesFromBuiltin(discoveryResult.identifier,
                discoveryResult.metadata);
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
	TfToken	    sdrtype( brayGetSdrTypeName( p ));
	TfToken	    sdftype( brayGetSdfTypeName( p ));
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
        metadata[ SdrPropertyMetadata->SdrUsdDefinitionType ] = sdftype;

	properties.emplace_back( SdrShaderPropertyUniquePtr(
		    new SdrShaderProperty( name, sdrtype, value,
			p.isExport(), arr_size,
			metadata , NdrTokenMap(), NdrOptionVec() )));
    }


    return properties;
}

PXR_NAMESPACE_CLOSE_SCOPE
