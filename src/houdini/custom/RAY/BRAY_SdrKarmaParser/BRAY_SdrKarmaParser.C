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
#include <UT/UT_JSONWriter.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONValueArray.h>
#include <UT/UT_JSONValueMap.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_IStream.h>
#include <UT/UT_ZString.h>
#include <UT/UT_StringStream.h>
#include <SYS/SYS_ParseNumber.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/ndr/debugCodes.h>
#include <pxr/usd/ndr/nodeDiscoveryResult.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

PXR_NAMESPACE_OPEN_SCOPE
    static size_t
    format(char *buffer, size_t bufsize, const VtValue &v)
    {
        UT::Format::Writer          writer(buffer, bufsize);
        UT::Format::Formatter     f;
        UT_OStringStream            os;
        os << v;
        return f.format(writer, "{}", {os.str()});
    }
PXR_NAMESPACE_CLOSE_SCOPE

namespace
{
    class KarmaInput
    {
    public:
        KarmaInput() = default;
        template <typename T>
        void    dump(UT_JSONWriter &w, const char *type,
                        const T *begin, const T *end) const
        {
            w.jsonKeyValue("type", type);
            w.jsonKey("default");
            exint       n = end - begin;
            if (n == 1)
                w.jsonValue(*begin);
            else
            {
                w.jsonBeginArray();
                for (const T *i = begin; i < end; ++i)
                    w.jsonValue(*i);
                w.jsonEndArray();
            }
        }
        void    dump() const
        {
            UT_AutoJSONWriter   w(std::cerr, false);
            dump(*w);
            std::cerr.flush();
        }
        void    dump(UT_JSONWriter &w) const
        {
            w.jsonBeginMap();
            w.jsonKeyValue("name", myName);
            w.jsonKeyValue("tuple_size", mySize);
            w.jsonKeyValue("array_size", myArraySize);
            w.jsonKeyValue("variadic", myVariadic);
            if (isFloat())
                dump(w, "float", fbegin(), fend());
            else if (isInt())
                dump(w, "int", ibegin(), iend());
            else if (isBool())
                dump(w, "bool", bbegin(), bend());
            else
            {
                UT_ASSERT(isString());
                dump(w, "bool", sbegin(), send());
            }
            w.jsonEndMap();

        }
        bool    load(const UT_JSONValue &v)
        {
            const UT_JSONValueArray     *arr = v.getArray();
            UT_ASSERT(arr && arr->size() >= 5);
            if (!arr || arr->size() < 5)
            {
                UTdebugFormat("Bad JSON Value: {}", arr);
                return false;
            }
            UT_WorkBuffer       style;
            int64               size;
            UT_VERIFY(arr->get(0)->import(myName));
            UT_VERIFY(arr->get(1)->import(myVariadic));
            UT_VERIFY(arr->get(2)->import(size));
            UT_VERIFY(arr->get(3)->import(style));
            UT_ASSERT(size > 0 && size < 64);   // Tuple size
            mySize = size;
            return loadDefault(*style.buffer(), *arr->get(4));
        }

        bool    loadDefault(char type, const UT_JSONValue &def)
        {
            myArraySize = 1;
            if (def.getArray())
            {
                myArraySize = def.getArray()->size();
                UT_ASSERT(myArraySize % mySize == 0);
                myArraySize /= mySize;
            }
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
            exint       asize = myArraySize * mySize;
            myF = UTmakeUnique<fpreal64[]>(SYSmax(asize, 1));
            if (asize == 1)
                return def.import(*myF.get());
            return loadArray(myF.get(), def);
        }
        bool    loadBool(const UT_JSONValue &def)
        {
            exint       asize = myArraySize * mySize;
            myB = UTmakeUnique<bool[]>(SYSmax(asize, 1));
            if (asize == 1)
                return def.import(*myB.get());
            return loadArray(myB.get(), def);
        }
        bool    loadInt(const UT_JSONValue &def)
        {
            exint       asize = myArraySize * mySize;
            myI = UTmakeUnique<int64[]>(SYSmax(asize, 1));
            if (asize == 1)
                return def.import(*myI.get());
            return loadArray(myI.get(), def);
        }
        bool    loadString(const UT_JSONValue &def)
        {
            exint       asize = myArraySize * mySize;
            myS = UTmakeUnique<UT_StringHolder[]>(SYSmax(asize, 1));
            if (asize == 1)
                return def.import(*myS.get());
            return loadArray(myS.get(), def);
        }

        bool    isFloat() const { return myF.get() != nullptr; }
        bool    isBool() const { return myB.get() != nullptr; }
        bool    isInt() const { return myI.get() != nullptr; }
        bool    isString() const { return myS.get() != nullptr; }

        const char      *getType() const
        {
            if (isFloat()) return "float";
            if (isBool()) return "bool";
            if (isInt()) return "int";
            UT_ASSERT(isString());
            return "string";
        }

        const fpreal64          *fbegin() const { return myF.get(); }
        const fpreal64          *fend() const { return myF.get() + myArraySize*mySize; }
        const int64             *ibegin() const { return myI.get(); }
        const int64             *iend() const { return myI.get() + myArraySize*mySize; }
        const bool              *bbegin() const { return myB.get(); }
        const bool              *bend() const { return myB.get() + myArraySize*mySize; }
        const UT_StringHolder   *sbegin() const { return myS.get(); }
        const UT_StringHolder   *send() const { return myS.get() + myArraySize*mySize; }

        template <typename T>
        bool    loadArray(T *result, const UT_JSONValue &val)
        {
            const UT_JSONValueArray     *arr = val.getArray();
            UT_ASSERT(arr && arr->size() == myArraySize*mySize);
            if (!arr)
            {
                UTdebugFormat("Missing array!");
                return false;
            }
            if (arr->size() != myArraySize * mySize)
            {
                UTdebugFormat("Bad array size: {} vs {}", myArraySize*mySize, arr->size());
                return false;
            }
            for (int i = 0; i < myArraySize * mySize; ++i)
            {
                if (!arr->get(i)->import(result[i]))
                {
                    UT_ASSERT(0);
                    UTdebugFormat("Bad import of element {}", i);
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
        int                             myArraySize;
        uint8                           mySize;
        bool                            myVariadic;
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

        void    dump(UT_JSONWriter &w, const char *label,
                        const KarmaInput *inputs, int n) const
        {
            w.jsonBeginArray();
            for (int i = 0; i < n; ++i)
                inputs[i].dump(w);
            w.jsonEndArray();
        }
        void    dump(UT_JSONWriter &w) const
        {
            w.jsonBeginMap();
            w.jsonKeyValue("name", myName);
            w.jsonKeyValue("ninputs", myInputSize);
            w.jsonKeyValue("noutputs", myOutputSize);
            dump(w, "inputs", myInputs.get(), myInputSize);
            dump(w, "outputs", myOutputs.get(), myOutputSize);
            w.jsonEndMap();
        }
        void    dump() const
        {
            UT_AutoJSONWriter   w(std::cerr, false);
            dump(*w);
            std::cerr.flush();
        }
    };
}

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(BRAY_SdrKarmaParser)

TF_DEFINE_PRIVATE_TOKENS(
    theTokens,

    ((discoveryTypeVex, "vex"))         // Compiled VEX code
    ((discoveryTypeVfl, "vfl"))         // VEX source code
    ((discoveryTypeKarma, "kma"))       // Built-in karma nodes
    ((sourceType, "VEX"))
);

// Help for UT_Format()
#define FORMAT_TYPE(TYPE) \
    static SYS_FORCE_INLINE size_t \
    format(char *buffer, size_t bufsize, const TYPE &v) \
    { \
        UT::Format::Writer      writer(buffer, bufsize); \
        UT::Format::Formatter f; \
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
    static const TfToken theKarmaRep("karma_rep", TfToken::Immortal);
    static const TfToken theKarmaRepOLen("karma_rep_olen", TfToken::Immortal);
    static const TfToken theShaderType("shaderType", TfToken::Immortal);
    auto metadata = discoveryResult.metadata;
    TfToken shaderType;
    if (metadata.find(theShaderType) != metadata.end())
    {
        shaderType = TfToken(metadata.find(theShaderType)->second);
        metadata.erase(theShaderType);
    }
    else
        shaderType = theTokens->discoveryTypeKarma;
    metadata.erase(theKarmaRep);
    metadata.erase(theKarmaRepOLen);

    if (discoveryResult.discoveryType == theTokens->discoveryTypeKarma)
    {
        // Built-in Karma node
        return NdrNodeUniquePtr(
                new SdrShaderNode(
                    discoveryResult.identifier,
                    discoveryResult.version,
                    discoveryResult.name,
                    discoveryResult.family,
                    shaderType,
                    theTokens->discoveryTypeKarma,
                    discoveryResult.uri,
                    discoveryResult.resolvedUri,
                    getNodeProperties(discoveryResult),
                    metadata,
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
            metadata,
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
    if (is_array)
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

template <typename T>
static GfMatrix3f
makeMatrix3f(const T *fv)
{
    return GfMatrix3f(fv[0], fv[1], fv[2],
                      fv[3], fv[4], fv[5],
                      fv[6], fv[7], fv[8]);
}

template <typename T>
static GfMatrix4f
makeMatrix4f(const T *fv)
{
    return GfMatrix4f(fv[ 0], fv[ 1], fv[ 2], fv[ 3],
                      fv[ 4], fv[ 5], fv[ 6], fv[ 7],
                      fv[ 8], fv[ 9], fv[10], fv[11],
                      fv[12], fv[13], fv[14], fv[15]);
}

static VtValue
floatArray(const fpreal64 *fv, int array_size)
{
    VtArray<float>      array;
    array.resize(array_size);
    for (int i = 0; i < array_size; ++i)
        array[i] = fv[i];
    return VtValue::Take(array);
}

template <typename T>
static VtValue
vectorValue(const fpreal64 *fv, int array_size)
{
    VtArray<T>  array;
    array.resize(array_size);
    for (int i = 0; i < array_size; ++i, fv += T::dimension)
    {
        // We use an explicit loop here to avoid using aggregate initialization
        // that results in error C2397 on MSVC when assigning to a single
        // precision T::ScalarType from double precision.
        T &dst = array[i];
        for (int j = 0; j < T::dimension; ++j)
            dst[j] = fv[j];
    }
    return VtValue::Take(array);
}

static VtValue
matrix3Value(const fpreal64 *fv, int array_size)
{
    VtArray<GfMatrix3f> array;
    array.resize(array_size);
    for (int i = 0; i < array_size; ++i, fv += 9)
        array[i] = makeMatrix3f(fv);
    return VtValue::Take(array);
}

static VtValue
matrix4Value(const fpreal64 *fv, int array_size)
{
    VtArray<GfMatrix4f> array;
    array.resize(array_size);
    for (int i = 0; i < array_size; ++i, fv += 16)
        array[i] = makeMatrix4f(fv);
    return VtValue::Take(array);
}

static VtValue
getFloat(const KarmaInput &parm, TfToken &type)
{
    const fpreal64      *fv = parm.myF.get();
    type = SdrPropertyTypes->Float;
    if (parm.myVariadic)
    {
        switch (parm.mySize)
        {
            case  1: return floatArray(fv, parm.myArraySize);
            case  2: return vectorValue<GfVec2f>(fv, parm.myArraySize);
            case  3:
                     type = SdrPropertyTypes->Color;    // Guess
                     return vectorValue<GfVec3f>(fv, parm.myArraySize);
            case  4:
                     type = SdrPropertyTypes->Color;    // Guess
                     return vectorValue<GfVec4f>(fv, parm.myArraySize);
            case  9: return matrix3Value(fv, parm.myArraySize);
            case 16:
                     type = SdrPropertyTypes->Matrix;
                     return matrix4Value(fv, parm.myArraySize);
            default: return floatArray(fv, parm.myArraySize*parm.mySize);
        }
    }
    switch (parm.mySize)
    {
        case 1:
            return VtValue(float(fv[0]));
        case 2:
            return VtValue(GfVec2f(fv[0], fv[1]));
        case 3:
             type = SdrPropertyTypes->Color;
            return VtValue(GfVec3f(fv[0], fv[1], fv[2]));
        case 4:
             type = SdrPropertyTypes->Color;
            return VtValue(GfVec4f(fv[0], fv[1], fv[2], fv[3]));
        case 9:
            return VtValue(makeMatrix3f(fv));
        case 16:
            type = SdrPropertyTypes->Matrix;
            return VtValue(makeMatrix4f(fv));
        default:
            VtArray<float>  array;
            array.assign(fv, fv+parm.mySize);
            return VtValue::Take(array);
    }
    return VtValue();
}

static void
brayGetSdrTypeInfo(const KarmaInput &parm,
        TfToken &type,
        VtValue &value,
        int &array_size,
        NdrTokenMap &metadata)
{
    array_size = parm.myVariadic ? parm.myArraySize : 0;
    if (parm.isFloat())
    {
        value = getFloat(parm, type);
    }
    else if (parm.isInt())
    {
        array_size *= parm.mySize;
        type = SdrPropertyTypes->Int;
        value = brayVtFromScalar<int>(parm.ibegin(), parm.iend());
    }
    else if (parm.isBool())
    {
        array_size *= parm.mySize;
        type = SdrPropertyTypes->Int;
        // We need to make sure the 'bool' type is properly noted in USD
        // (see https://github.com/PixarAnimationStudios/OpenUSD/issues/1784)
        metadata.emplace(SdrPropertyMetadata->SdrUsdDefinitionType,
                         SdfValueTypeNames->Bool.GetType().GetTypeName());
        value = brayVtFromScalar<int>(parm.bbegin(), parm.bend());
    }
    else
    {
        array_size *= parm.mySize;
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
    static const TfToken theKarmaRep("karma_rep", TfToken::Immortal);
    static const TfToken theKarmaRepOLen("karma_rep_olen", TfToken::Immortal);
    std::string olen_str, krep_str;

    KarmaNode   knode;
    if (!getMetadata(theKarmaRep, krep_str, metadata)
        || !getMetadata(theKarmaRepOLen, olen_str, metadata))
    {
        return NdrPropertyUniquePtrVec();
    }

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
        NdrTokenMap     metadata;
        NdrTokenMap     hints;
        NdrOptionVec    options;

        brayGetSdrTypeInfo(*it, type, value, arr_size, metadata);

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

        brayGetSdrTypeInfo(*it, type, value, arr_size, metadata);

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
			metadata,
                        NdrTokenMap(),
                        NdrOptionVec())));
    }


    return properties;
}

PXR_NAMESPACE_CLOSE_SCOPE
