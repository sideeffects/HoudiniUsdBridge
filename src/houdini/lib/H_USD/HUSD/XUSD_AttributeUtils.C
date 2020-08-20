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

#include "XUSD_AttributeUtils.h"
#include "HUSD_AssetPath.h"
#include "HUSD_Constants.h"
#include "HUSD_Token.h"
#include "XUSD_Utils.h"
#include <gusd/UT_Gf.h>
#include <VOP/VOP_Node.h>
#include <VOP/VOP_NodeParmManager.h>
#include <PRM/PRM_Parm.h>
#include <CH/CH_Manager.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Matrix2.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_ValArray.h>
#include <UT/UT_Vector2.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_Vector4.h>
#include <pxr/usd/sdf/timeCode.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/relationship.h>

PXR_NAMESPACE_OPEN_SCOPE

// ============================================================================
template<typename T>
struct XUSD_EquivalenceMap {};

#define XUSD_EQUIVALENCE(UtType, GfType, SdfTypeName)		\
    template<>								\
    struct XUSD_EquivalenceMap<UtType>					\
    {									\
	static constexpr const char* typeName = #SdfTypeName;		\
	typedef GfType gfType;						\
    };									\
									\
    template<>								\
    struct XUSD_EquivalenceMap<UT_Array<UtType>>			\
    {									\
	static constexpr const char* typeName = #SdfTypeName "[]";	\
	typedef VtArray<GfType> gfType;					\
    };									\
									\
    template<>								\
    struct XUSD_EquivalenceMap<UT_ValArray<UtType>>			\
    {									\
	static constexpr const char* typeName = #SdfTypeName "[]";	\
	typedef VtArray<GfType> gfType;					\
    };

// Note: the following lines are in the format:
//	    UT_Type, Gf_Type, Type_Name // Sdf.ValueTypeNames_Name
//	The correspondence between Gf_Type, Type_Name, & Sdf.ValueTypeNames_Name
//	is established in pxr/usd/lib/sdf/schema.cpp
//	Here we establish a default correspondence between them and the UT.
XUSD_EQUIVALENCE( bool,		    bool,	    bool	) // Bool
XUSD_EQUIVALENCE( int32,	    int,	    int		) // Int
XUSD_EQUIVALENCE( uint32,	    uint,	    uint	) // UInt
XUSD_EQUIVALENCE( int64,	    int64,	    int64	) // Int64
XUSD_EQUIVALENCE( uint64,	    uint64,	    uint64	) // UInt64
XUSD_EQUIVALENCE( fpreal32,	    float,	    float	) // Float
XUSD_EQUIVALENCE( fpreal64,	    double,	    double	) // Double
XUSD_EQUIVALENCE( UT_StringHolder,  std::string,    string	) // String
XUSD_EQUIVALENCE( UT_Vector2i,	    GfVec2i,	    int2	) // Int2
XUSD_EQUIVALENCE( UT_Vector3i,	    GfVec3i,	    int3	) // Int3
XUSD_EQUIVALENCE( UT_Vector4i,	    GfVec4i,	    int4	) // Int4
XUSD_EQUIVALENCE( UT_Vector2F,	    GfVec2f,	    float2	) // Float2
XUSD_EQUIVALENCE( UT_Vector3F,	    GfVec3f,	    vector3f	) // Vector3f
XUSD_EQUIVALENCE( UT_Vector4F,	    GfVec4f,	    float4	) // Float4
XUSD_EQUIVALENCE( UT_Vector2D,	    GfVec2d,	    double2	) // Double2
XUSD_EQUIVALENCE( UT_Vector3D,	    GfVec3d,	    vector3d	) // Vector3d
XUSD_EQUIVALENCE( UT_Vector4D,	    GfVec4d,	    double4	) // Double4
XUSD_EQUIVALENCE( UT_QuaternionH,   GfQuath,	    quath	) // Quath
XUSD_EQUIVALENCE( UT_QuaternionF,   GfQuatf,	    quatf	) // Quatf
XUSD_EQUIVALENCE( UT_QuaternionD,   GfQuatd,	    quatd	) // Quatd
XUSD_EQUIVALENCE( UT_Matrix2D,	    GfMatrix2d,	    matrix2d	) // Matrix2d
XUSD_EQUIVALENCE( UT_Matrix3D,	    GfMatrix3d,	    matrix3d	) // Matrix3d
XUSD_EQUIVALENCE( UT_Matrix4D,	    GfMatrix4d,	    matrix4d	) // Matrix4d
XUSD_EQUIVALENCE( HUSD_AssetPath,   SdfAssetPath,   asset	) // Asset
XUSD_EQUIVALENCE( HUSD_Token,       TfToken,        token	) // Token

#undef XUSD_EQUIVALENCE

#define XUSD_GET_TYPE_NAME(UT_TYPE) XUSD_EquivalenceMap<UT_TYPE>::typeName
#define XUSD_GET_GF_TYPE(UT_TYPE) typename XUSD_EquivalenceMap<UT_TYPE>::gfType


// ============================================================================
#define XUSD_CONVERSION_2(UT_TYPE, GF_FROM_UT, UT_FROM_GF)		\
    static XUSD_GET_GF_TYPE(UT_TYPE)					\
    husdGetGfFromUt(const UT_TYPE &in)					\
    {									\
	XUSD_GET_GF_TYPE(UT_TYPE) out;					\
	GF_FROM_UT;							\
	return out;							\
    }									\
									\
    static VtArray<XUSD_GET_GF_TYPE(UT_TYPE)>				\
    husdGetGfFromUt(const UT_Array<UT_TYPE> &in)			\
    {									\
	VtArray<XUSD_GET_GF_TYPE(UT_TYPE)> out(in.size());		\
	for (int i = 0, n = in.size(); i < n; ++i)			\
	    out[i] = husdGetGfFromUt(in[i]);				\
	return out;							\
    }									\
									\
    static VtArray<XUSD_GET_GF_TYPE(UT_TYPE)>				\
    husdGetGfFromUt(const UT_ValArray<UT_TYPE> &in)			\
    {									\
	VtArray<XUSD_GET_GF_TYPE(UT_TYPE)> out(in.size());		\
	for (int i = 0, n = in.size(); i < n; ++i)			\
	    out[i] = husdGetGfFromUt(in[i]);				\
	return out;							\
    }									\
									\
    static UT_TYPE							\
    husdGetUtFromGf(const XUSD_GET_GF_TYPE(UT_TYPE) &in)		\
    {									\
	UT_TYPE out;							\
	UT_FROM_GF;							\
	return out;							\
    }									\
									\
    static UT_Array<UT_TYPE>						\
    husdGetUtFromGf(const VtArray<XUSD_GET_GF_TYPE(UT_TYPE)> &in)	\
    {									\
	UT_Array<UT_TYPE> out(in.size(), in.size());			\
	for (int i = 0, n = in.size(); i < n; ++i)			\
	    out[i] = husdGetUtFromGf(in[i]);				\
	return out;							\
    }									\

#define XUSD_CONVERSION_1(UT_TYPE, EXPR)				\
    XUSD_CONVERSION_2(UT_TYPE, EXPR, EXPR)				\

XUSD_CONVERSION_1(bool,		        out=in)
XUSD_CONVERSION_1(int32,		out=in)
XUSD_CONVERSION_1(uint32,		out=in)
XUSD_CONVERSION_1(int64,		out=in)
XUSD_CONVERSION_1(uint64,		out=in)
XUSD_CONVERSION_1(fpreal32,		out=in)
XUSD_CONVERSION_1(fpreal64,		out=in)
XUSD_CONVERSION_2(UT_StringHolder,	out=in.toStdString(), out=in)
XUSD_CONVERSION_1(UT_Vector2i,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Vector3i,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Vector4i,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Vector2F,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Vector3F,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Vector4F,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Vector2D,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Vector3D,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Vector4D,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_QuaternionH,	GusdUT_Gf::Convert(in,out))
XUSD_CONVERSION_1(UT_QuaternionF,	GusdUT_Gf::Convert(in,out))
XUSD_CONVERSION_1(UT_QuaternionD,	GusdUT_Gf::Convert(in,out))
XUSD_CONVERSION_1(UT_Matrix2D,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Matrix3D,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_1(UT_Matrix4D,		out=GusdUT_Gf::Cast(in))
XUSD_CONVERSION_2(HUSD_AssetPath,	out=SdfAssetPath(in.toStdString()),
					out=in.GetAssetPath())
XUSD_CONVERSION_2(HUSD_Token,	        out=TfToken(in.toStdString()),
					out=in.GetText())

#undef XUSD_CONVERSION_2
#undef XUSD_CONVERSION_1

// ============================================================================
// Casting between values of different types:
static inline void xusdConvert(const std::string &from, TfToken &to) 
{
    to = TfToken(from);
}

static inline void xusdConvert(const TfToken &from, std::string &to) 
{
    to = from.GetString();
}

static inline void xusdConvert(const std::string &from, SdfAssetPath &to) 
{
    to = SdfAssetPath(from);
}

static inline void xusdConvert(const SdfAssetPath &from, std::string &to) 
{
    to = from.GetAssetPath();
}

static inline void xusdConvert(const std::string &from, SdfSpecifier &to) 
{
    if (from == HUSD_Constants::getPrimSpecifierClass().c_str())
        to = SdfSpecifierClass;
    else if (from == HUSD_Constants::getPrimSpecifierDefine().c_str())
        to = SdfSpecifierDef;
    else // if (from == HUSD_Constants::getPrimSpecifierOverride().c_str())
        to = SdfSpecifierOver;
}

static inline void xusdConvert(const SdfSpecifier &from, std::string &to) 
{
    if (from == SdfSpecifierClass)
        to = HUSD_Constants::getPrimSpecifierClass().toStdString();
    else if (from == SdfSpecifierDef)
        to = HUSD_Constants::getPrimSpecifierDefine().toStdString();
    else // if (from == SdfSpecifierOver)
        to = HUSD_Constants::getPrimSpecifierOverride().toStdString();
}

#define XUSD_CONVERT_SIMPLE( TYPE_A, TYPE_B ) \
static inline void xusdConvert(const TYPE_A &from, TYPE_B &to) \
{ to = from; } \
static inline void xusdConvert(const TYPE_B &from, TYPE_A &to) \
{ to = from; } \

// These are used in the conversion of array elements.
XUSD_CONVERT_SIMPLE( int, bool )
XUSD_CONVERT_SIMPLE( int, float )
XUSD_CONVERT_SIMPLE( int, double )
XUSD_CONVERT_SIMPLE( int, uchar )
XUSD_CONVERT_SIMPLE( int, uint )
XUSD_CONVERT_SIMPLE( int, uint64 )
XUSD_CONVERT_SIMPLE( int64, bool )
XUSD_CONVERT_SIMPLE( int64, int )
XUSD_CONVERT_SIMPLE( int64, float )
XUSD_CONVERT_SIMPLE( int64, double )
XUSD_CONVERT_SIMPLE( int64, uchar )
XUSD_CONVERT_SIMPLE( int64, uint )
XUSD_CONVERT_SIMPLE( int64, uint64 )
#undef XUSD_CONVERT_SIMPLE


#define XUSD_CONVERT_TIMECODE( TYPE_A ) \
static inline void xusdConvert(const TYPE_A &from, SdfTimeCode &to) \
{ to = SdfTimeCode(from); } \
static inline void xusdConvert(const SdfTimeCode &from, TYPE_A &to) \
{ to = from.GetValue(); }
XUSD_CONVERT_TIMECODE( fpreal32 )
XUSD_CONVERT_TIMECODE( fpreal64 )


#define XUSD_CONVERT_VEC2( TYPE_A, TYPE_B ) \
static inline void xusdConvert(const TYPE_A &from, TYPE_B &to) \
{ to.Set( from[0], from[1] ); } \
static inline void xusdConvert(const TYPE_B &from, TYPE_A &to) \
{ to.Set( from[0], from[1] ); } \

XUSD_CONVERT_VEC2( GfVec2f, GfVec2i )
XUSD_CONVERT_VEC2( GfVec2d, GfVec2i )
#undef XUSD_CONVERT_VEC2


#define XUSD_CONVERT_VEC3( TYPE_A, TYPE_B ) \
static inline void xusdConvert(const TYPE_A &from, TYPE_B &to) \
{ to.Set( from[0], from[1], from[2] ); } \
static inline void xusdConvert(const TYPE_B &from, TYPE_A &to) \
{ to.Set( from[0], from[1], from[2] ); } \

XUSD_CONVERT_VEC3( GfVec3f, GfVec3i )
XUSD_CONVERT_VEC3( GfVec3d, GfVec3i )
#undef XUSD_CONVERT_VEC3


#define XUSD_CONVERT_VEC4( TYPE_A, TYPE_B ) \
static inline void xusdConvert(const TYPE_A &from, TYPE_B &to) \
{ to.Set( from[0], from[1], from[2], from[3] ); } \
static inline void xusdConvert(const TYPE_B &from, TYPE_A &to) \
{ to.Set( from[0], from[1], from[2], from[3] ); } \

XUSD_CONVERT_VEC4( GfVec4f, GfVec4i )
XUSD_CONVERT_VEC4( GfVec4d, GfVec4i )
#undef XUSD_CONVERT_VEC4


#define XUSD_CONVERT_MAT2( MAT2, VEC4 ) \
static inline void xusdConvert(const MAT2 &from, VEC4 &to) \
{ to = VEC4( from[0][0], from[0][1], from[1][0], from[1][1] ); } \
static inline void xusdConvert(const VEC4 &from, MAT2 &to) \
{ to = MAT2( from[0], from[1], from[2], from[3] ); } \

XUSD_CONVERT_MAT2( GfMatrix2d, GfVec4f )
XUSD_CONVERT_MAT2( GfMatrix2d, GfVec4d )
#undef XUSD_CONVERT_MAT2


template<typename V, typename Q>
inline void xusdConvertVQ( const V &from, Q &to )
{
    // Equivalent to GfVec4 -> UT_Vector4 -> UT_Quaternion -> GfQuat.
    to = Q( from[3], from[0], from[1], from[2] );
}

template<typename Q, typename V>
inline void xusdConvertQV( const Q &from, V &to )
{
    // Reverse of GfVec4 -> UT_Vector4 -> UT_Quaternion -> GfQuat.
    const auto &i = from.GetImaginary();
    to = V( i[0], i[1], i[2], from.GetReal() );
}

#define XUSD_CONVERT_VQ( TYPE_V, TYPE_Q ) \
static inline void xusdConvert(const TYPE_V &from, TYPE_Q &to) \
{ xusdConvertVQ( from, to ); } \
static inline void xusdConvert(const TYPE_Q &from, TYPE_V &to)  \
{ xusdConvertQV( from, to ); } \

XUSD_CONVERT_VQ( GfVec4f, GfQuath )
XUSD_CONVERT_VQ( GfVec4f, GfQuatf )
XUSD_CONVERT_VQ( GfVec4f, GfQuatd )
XUSD_CONVERT_VQ( GfVec4d, GfQuath )
XUSD_CONVERT_VQ( GfVec4d, GfQuatf )
XUSD_CONVERT_VQ( GfVec4d, GfQuatd )
#undef XUSD_CONVERT_VQ


template <typename FROM_ELT_T, typename TO_ELT_T> 
inline VtValue
xusdConvertArray(const VtValue &from_value)
{
    VtArray<TO_ELT_T> a;

    UT_ASSERT( from_value.IsHolding<VtArray<FROM_ELT_T>>() );
    for( auto &&from_element : from_value.UncheckedGet<VtArray<FROM_ELT_T>>() )
    {
	TO_ELT_T to_element;

	xusdConvert(from_element, to_element);
	a.push_back(to_element);
    }
    return VtValue::Take(a);
}

#define XUSD_CONVERT_SCLR( TYPE_A, TYPE_B ) \
    if( from_value.IsHolding<TYPE_A>() && \
	 def_value.IsHolding<TYPE_B>() ) \
    { \
	TYPE_B r; \
	xusdConvert(from_value.UncheckedGet<TYPE_A>(), r); \
	return VtValue::Take( r ); \
    } \
    if( from_value.IsHolding<TYPE_B>() && \
	 def_value.IsHolding<TYPE_A>() ) \
    { \
	TYPE_A r; \
	xusdConvert(from_value.UncheckedGet<TYPE_B>(), r); \
	return VtValue::Take( r ); \
    } \

#define XUSD_CONVERT_ARR( TYPE_A, TYPE_B ) \
    if( from_value.IsHolding<VtArray<TYPE_A>>() && \
	 def_value.IsHolding<VtArray<TYPE_B>>() ) \
	return xusdConvertArray<TYPE_A, TYPE_B>( from_value ); \
    if( from_value.IsHolding<VtArray<TYPE_B>>() && \
	 def_value.IsHolding<VtArray<TYPE_A>>() ) \
	return xusdConvertArray<TYPE_B, TYPE_A>( from_value ); \

#define XUSD_CONVERT( TYPE_A, TYPE_B ) \
    XUSD_CONVERT_SCLR( TYPE_A, TYPE_B ) \
    XUSD_CONVERT_ARR( TYPE_A, TYPE_B ) \

static VtValue
xusdCustomCastToTypeOf(const VtValue &from_value, const VtValue &def_value)
{
    // TODO: Avoid n^2 number of conversions by defining conversions to and from
    //	     a common type. Eg, all vectors4 (i, f, d, quats, etc) can convert
    //	     to and from GfVec4d.

    // While VtValue::CastToTypeOf() casts the scalars, it does not cast arrays.
    XUSD_CONVERT_ARR( std::string, TfToken )
    XUSD_CONVERT_ARR( int,   bool )
    XUSD_CONVERT_ARR( int,   float )
    XUSD_CONVERT_ARR( int,   double )
    XUSD_CONVERT_ARR( int,   int64 )
    XUSD_CONVERT_ARR( int,   uchar )
    XUSD_CONVERT_ARR( int,   uint )
    XUSD_CONVERT_ARR( int,   uint64 )
    XUSD_CONVERT_ARR( int64, bool )
    XUSD_CONVERT_ARR( int64, float )
    XUSD_CONVERT_ARR( int64, double )
    XUSD_CONVERT_ARR( int64, int )
    XUSD_CONVERT_ARR( int64, uchar )
    XUSD_CONVERT_ARR( int64, uint )
    XUSD_CONVERT_ARR( int64, uint64 )

    // CVEX will use string for asset paths.
    XUSD_CONVERT( std::string, SdfAssetPath )

    // CVEX may interchangeably use Float4/Double4 and Matrix2d.
    XUSD_CONVERT( GfVec4f, GfMatrix2d )
    XUSD_CONVERT( GfVec4d, GfMatrix2d )

    // CVEX uses vector4 to represent quaternion values. 
    // Main use is for processing orientations array in point instance prim.
    XUSD_CONVERT( GfVec4f, GfQuath )
    XUSD_CONVERT( GfVec4f, GfQuatf )
    XUSD_CONVERT( GfVec4f, GfQuatd )
    XUSD_CONVERT( GfVec4d, GfQuath )
    XUSD_CONVERT( GfVec4d, GfQuatf )
    XUSD_CONVERT( GfVec4d, GfQuatd )

    // CVEX does not have integer vector types so always uses floats/doubles,
    // and USD API does not automatically convert between int3 and float3.
    XUSD_CONVERT( GfVec2d, GfVec2i )
    XUSD_CONVERT( GfVec3d, GfVec3i )
    XUSD_CONVERT( GfVec4d, GfVec4i )
    XUSD_CONVERT( GfVec2f, GfVec2i )
    XUSD_CONVERT( GfVec3f, GfVec3i )
    XUSD_CONVERT( GfVec4f, GfVec4i )

    // Convert a floating point number to an SdfTimeCode.
    XUSD_CONVERT( fpreal32, SdfTimeCode )
    XUSD_CONVERT( fpreal64, SdfTimeCode )

    // Convert from SdfSpecifier to a string.
    XUSD_CONVERT_SCLR( std::string, SdfSpecifier )

    return VtValue();
}

#undef XUSD_CONVERT_ARR
#undef XUSD_CONVERT_SCLR
#undef XUSD_CONVERT

static VtValue
xusdCastToTypeOf(const VtValue &from_value, const VtValue &def_value)
{
    VtValue	result;

    // Try the standard USD conversion first.
    result = VtValue::CastToTypeOf( from_value, def_value );
    if( !result.IsEmpty() )
	return result;

    // Try custom conversion, tailored to the common calls in Houdini, 
    // especially from VEX code that goes thru HUSD_Cvex class.
    return xusdCustomCastToTypeOf( from_value, def_value );
}

template<typename GF_VALUE_TYPE>
bool
husdGetGfFromVt(GF_VALUE_TYPE &gf_value, const VtValue &vt_value )
{
    VtValue	defvalue( gf_value);
    VtValue	castvalue( xusdCastToTypeOf( vt_value, defvalue ));

    bool ok = !castvalue.IsEmpty();
    if(ok)
	gf_value = castvalue.UncheckedGet<GF_VALUE_TYPE>();

    return ok;
}

// ============================================================================
template<typename UT_VALUE_TYPE>
const char *
HUSDgetSdfTypeName()
{
    return XUSD_GET_TYPE_NAME(UT_VALUE_TYPE);
}

template<typename UT_VALUE_TYPE, typename F>
bool
HUSDsetAttributeHelper(const UsdAttribute &attribute,
	const UT_VALUE_TYPE &ut_value, const UsdTimeCode &timecode, F fn)
{
    bool	    ok = false;
    auto	    gf_value = fn(ut_value);

    if (attribute.GetTypeName() == 
	SdfSchema::GetInstance().FindType(HUSDgetSdfTypeName<UT_VALUE_TYPE>()))
    {
	ok = attribute.Set(gf_value, timecode);
	HUSDclearDataId(attribute);
    }
    else
    {
	VtValue	    vt_value(gf_value);
	VtValue	    defvalue(attribute.GetTypeName().GetDefaultValue());
	VtValue	    castvalue(xusdCastToTypeOf(vt_value, defvalue));

	if (!castvalue.IsEmpty())
	{
	    ok = attribute.Set(castvalue, timecode);
	    HUSDclearDataId(attribute);
	}
    }

    return ok;
}

template<typename UT_VALUE_TYPE>
bool
HUSDsetAttribute(const UsdAttribute &attribute, const UT_VALUE_TYPE &ut_value,
	const UsdTimeCode &timecode)
{
    return HUSDsetAttributeHelper(attribute, ut_value, timecode,
	    []( const UT_VALUE_TYPE &v )
	    { 
		return husdGetGfFromUt(v);
	    });
}


namespace {

static inline fpreal
husdGetEvalTime( const UsdTimeCode &tc )
{
    return CHgetTimeFromFrame( tc.GetValue() );
}

template<typename T>
inline void
husdSetAttribVector( const UsdAttribute &attrib, const PRM_Parm &parm,
	const UsdTimeCode &tc )
{
    exint		d = T::dimension;
    exint		n = SYSmax( (exint) parm.getVectorSize(), d );
    UT_Array<typename T::ScalarType>	value(n, n);

    parm.getValues( husdGetEvalTime(tc), value.data(), SYSgetSTID() );
    attrib.Set( T( value.data() ), tc );
}

template<typename T>
inline void
husdSetAttribInt( const UsdAttribute &attrib, const PRM_Parm &parm,
	const UsdTimeCode &tc )
{
    int			value;

    parm.getValue( husdGetEvalTime(tc), value, 0, SYSgetSTID() );
    attrib.Set( T( value ), tc );
}

template<typename T>
inline void
husdSetAttribFloat( const UsdAttribute &attrib, const PRM_Parm &parm,
	const UsdTimeCode &tc )
{
    fpreal		value;

    parm.getValue( husdGetEvalTime(tc), value, 0, SYSgetSTID() );
    attrib.Set( T( value ), tc );
}

template<typename T>
inline void
husdSetAttribString( const UsdAttribute &attrib, const PRM_Parm &parm,
	const UsdTimeCode &tc )
{
    UT_String		value;

    parm.getValue( husdGetEvalTime(tc), value, 0, true, SYSgetSTID() );
    attrib.Set( T( value.toStdString() ), tc );
}

template<typename T>
inline void
husdSetAttribMatrix( const UsdAttribute &attrib, const PRM_Parm &parm,
	const UsdTimeCode &tc )
{
    exint		d = T::numRows * T::numColumns;
    exint		n = SYSmax( (exint) parm.getVectorSize(), d );
    UT_Array<fpreal64>	value(n, n);

    parm.getValues( husdGetEvalTime(tc), value.data(), SYSgetSTID() );
    auto data = reinterpret_cast<fpreal64 (*)[T::numColumns]>( value.data() );
    attrib.Set( T( data ), tc );
}
    
} // end: anonymous namespace

bool
HUSDsetAttribute(const UsdAttribute &attrib, const PRM_Parm &parm, 
	const UsdTimeCode &tc)
{
    bool		ok   = true;
    SdfValueTypeName	type = attrib.GetTypeName();

    // This group is ordered in a perceived frequency of use for shader prims.
    if(	     type == SdfValueTypeNames->Float3   || 
	     type == SdfValueTypeNames->Vector3f ||
	     type == SdfValueTypeNames->Color3f  ||
	     type == SdfValueTypeNames->Point3f  ||
	     type == SdfValueTypeNames->Normal3f )
	husdSetAttribVector<GfVec3f>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Float )
	husdSetAttribFloat<fpreal32>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Int )
	husdSetAttribInt<int>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->String )
	husdSetAttribString<std::string>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Asset )
	husdSetAttribString<SdfAssetPath>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Token )
	husdSetAttribString<TfToken>( attrib, parm, tc );
    
    else if( type == SdfValueTypeNames->Float2 )
	husdSetAttribVector<GfVec2f>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Float4 ||
	     type == SdfValueTypeNames->Color4f )
	husdSetAttribVector<GfVec4f>( attrib, parm, tc);

    else if( type == SdfValueTypeNames->Double )
	husdSetAttribFloat<fpreal>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Double2 )
	husdSetAttribVector<GfVec2d>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Vector3d ||
	     type == SdfValueTypeNames->Color3d )
	husdSetAttribVector<GfVec3d>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Double4 ||
	     type == SdfValueTypeNames->Color4d )
	husdSetAttribVector<GfVec4d>( attrib, parm, tc);

    else if( type == SdfValueTypeNames->Matrix2d )
	husdSetAttribMatrix<GfMatrix2d>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Matrix3d )
	husdSetAttribMatrix<GfMatrix3d>( attrib, parm, tc );
    else if( type == SdfValueTypeNames->Matrix4d )
	husdSetAttribMatrix<GfMatrix4d>( attrib, parm, tc );

    else
	ok = false;

    return ok;
}


namespace {

template<typename T, typename ParmT = T>
void
husdSetParmScalar( PRM_Parm &parm,
        const UsdAttribute &attrib,
        const UsdTimeCode &timecode )
{
    T value(0);
    // In the case of an array of this type, set the first entry.
    if (attrib.GetTypeName().IsArray())
    {
        VtArray<T> valuearray;
        attrib.Get( &valuearray, timecode );
        if (valuearray.size() > 0)
            value = valuearray[0];
    }
    else
        attrib.Get( &value, timecode );

    parm.setValue( 0, (ParmT)value );
}

template<typename T>
void
husdSetParmVector( PRM_Parm &parm,
        const UsdAttribute &attrib,
        const UsdTimeCode &timecode )
{
    T value(0);
    // In the case of an array of this type, set the first entry.
    if (attrib.GetTypeName().IsArray())
    {
        VtArray<T> valuearray;
        attrib.Get( &valuearray, timecode );
        if (valuearray.size() > 0)
            value = valuearray[0];
    }
    else
        attrib.Get( &value, timecode );

    // Expand array to avoid setValues() accessing out-of-bounds array,
    // and also convert potentially float values to doubles, etc.
    exint a_size = T::dimension;
    exint p_size = parm.getVectorSize();

    UT_Array<fpreal> buff( p_size, p_size );
    for( exint i = 0, n = SYSmin( a_size, p_size); i < n; i++ )
	buff[i] = value.data()[i];

    parm.setValues( 0, buff.data() );
}

template<typename T, typename ConvertToStrFn>
inline void
husdSetParmString( PRM_Parm &parm,
        const UsdAttribute &attrib,
        const UsdTimeCode &timecode,
        const ConvertToStrFn &convert_to_str )
{
    T value;
    // In the case of an array of this type, set the parm to a space separated
    // list of all entries.
    if (attrib.GetTypeName().IsArray())
    {
        VtArray<T> valuearray;
        UT_WorkBuffer buf;
        attrib.Get( &valuearray, timecode );
        for (auto &&v : valuearray)
        {
            if (!buf.isEmpty())
                buf.append(' ');
            buf.append(convert_to_str(v));
        }
        parm.setValue( 0, buf.buffer(), CH_STRING_LITERAL );
    }
    else
    {
        attrib.Get( &value, timecode );
        parm.setValue( 0, convert_to_str(value), CH_STRING_LITERAL );
    }
}

inline void
husdSetParmAssetPath( PRM_Parm &parm,
        const UsdAttribute &attrib,
        const UsdTimeCode &timecode )
{
    SdfAssetPath value;
    // In the case of an array of this type, set the first entry.
    if (attrib.GetTypeName().IsArray())
    {
        VtArray<SdfAssetPath> valuearray;
        attrib.Get( &valuearray, timecode );
        if (valuearray.size() > 0)
            value = valuearray[0];
    }
    else
        attrib.Get( &value, timecode );

    parm.setValue( 0, value.GetAssetPath().c_str(), CH_STRING_LITERAL );
}

template<typename T>
void
husdSetParmQuat( PRM_Parm &parm,
        const UsdAttribute &attrib,
        const UsdTimeCode &timecode )
{
    T value;
    // In the case of an array of this type, set the first entry.
    if (attrib.GetTypeName().IsArray())
    {
        VtArray<T> valuearray;
        attrib.Get( &valuearray, timecode );
        if (valuearray.size() > 0)
            value = valuearray[0];
    }
    else
        attrib.Get( &value, timecode );

    // Expand array to avoid setValues() accessing out-of-bounds array,
    // and also convert potentially float values to doubles, etc.
    exint p_size = parm.getVectorSize();

    UT_Array<fpreal> buff( SYSmax(p_size, 4), SYSmax(p_size, 4) );
    for( exint i = 0; i < 3; i++ )
	buff[i] = value.GetImaginary().data()[i];
    buff[3] = value.GetReal();

    parm.setValues( 0, buff.data() );
}

void
husdSetParmTimeCode( PRM_Parm &parm,
        const UsdAttribute &attrib,
        const UsdTimeCode &timecode )
{
    SdfTimeCode value(0.0);
    // In the case of an array of this type, set the first entry.
    if (attrib.GetTypeName().IsArray())
    {
        VtArray<SdfTimeCode> valuearray;
        attrib.Get( &valuearray, timecode );
        if (valuearray.size() > 0)
            value = valuearray[0];
    }
    else
        attrib.Get( &value, timecode );

    parm.setValue( 0, (fpreal)value.GetValue() );
}

template<typename T>
void
husdSetParmMatrix( PRM_Parm &parm,
        const UsdAttribute &attrib,
        const UsdTimeCode &timecode )
{
    T value;
    // In the case of an array of this type, set the first entry.
    if (attrib.GetTypeName().IsArray())
    {
        VtArray<T> valuearray;
        attrib.Get( &valuearray, timecode );
        if (valuearray.size() > 0)
            value = valuearray[0];
    }
    else
        attrib.Get( &value, timecode );

    // Expand array to avoid setValues() accessing out-of-bounds array,
    // and also convert potentially float values to doubles, etc.
    exint a_size = T::numRows * T::numColumns;
    exint p_size = parm.getVectorSize();

    UT_Array<fpreal> buff( p_size, p_size );
    for( exint i = 0, n = SYSmin(a_size, p_size); i < n; i++ )
	buff[i] = value.GetArray()[i];

    parm.setValues( 0, buff.data() );
}

} // end: anonymous namespace

bool
HUSDsetNodeParm(PRM_Parm &parm,
        const UsdAttribute &attrib, 
	const UsdTimeCode &timecode,
        bool save_for_undo)
{
    SdfValueTypeName	 type = attrib.GetTypeName().GetScalarType();
    bool		 ok = true;

    // Save the parameter value for undo.
    if (save_for_undo)
    {
        OP_Node         *node = dynamic_cast<OP_Node *>(parm.getParmOwner());
        if (node)
            node->saveParmForUndo(&parm);
    }

    if(	     type == SdfValueTypeNames->Double4 ||
	     type == SdfValueTypeNames->Color4d )
	husdSetParmVector<GfVec4d>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Double3 ||
	     type == SdfValueTypeNames->Vector3d ||
	     type == SdfValueTypeNames->TexCoord3d ||
	     type == SdfValueTypeNames->Color3d ||
	     type == SdfValueTypeNames->Point3d ||
	     type == SdfValueTypeNames->Normal3d )
	husdSetParmVector<GfVec3d>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Double2 ||
             type == SdfValueTypeNames->TexCoord2d )
	husdSetParmVector<GfVec2d>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Double )
	husdSetParmScalar<fpreal64, fpreal>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Quatd )
	husdSetParmQuat<GfQuatd>( parm, attrib, timecode );

    else if( type == SdfValueTypeNames->Float4 ||
	     type == SdfValueTypeNames->Color4f )
	husdSetParmVector<GfVec4f>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Float3 ||
	     type == SdfValueTypeNames->Vector3f ||
	     type == SdfValueTypeNames->TexCoord3f ||
	     type == SdfValueTypeNames->Color3f ||
	     type == SdfValueTypeNames->Point3f ||
	     type == SdfValueTypeNames->Normal3f )
	husdSetParmVector<GfVec3f>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Float2 ||
             type == SdfValueTypeNames->TexCoord2f )
	husdSetParmVector<GfVec2f>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Float )
	husdSetParmScalar<fpreal32, fpreal>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Quatf )
	husdSetParmQuat<GfQuatf>( parm, attrib, timecode );

    else if( type == SdfValueTypeNames->Half4 ||
	     type == SdfValueTypeNames->Color4h )
	husdSetParmVector<GfVec4h>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Half3 ||
	     type == SdfValueTypeNames->Vector3h ||
	     type == SdfValueTypeNames->TexCoord3h ||
	     type == SdfValueTypeNames->Color3h ||
	     type == SdfValueTypeNames->Point3h ||
	     type == SdfValueTypeNames->Normal3h )
	husdSetParmVector<GfVec3h>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Half2 ||
             type == SdfValueTypeNames->TexCoord2h )
	husdSetParmVector<GfVec2h>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Half )
	husdSetParmScalar<GfHalf, fpreal>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Quath )
	husdSetParmQuat<GfQuath>( parm, attrib, timecode );

    else if( type == SdfValueTypeNames->Int4 )
	husdSetParmVector<GfVec4i>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Int3 )
	husdSetParmVector<GfVec3i>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Int2 )
	husdSetParmVector<GfVec2i>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Int )
	husdSetParmScalar<int32>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Int64 )
	husdSetParmScalar<int64>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->UChar )
	husdSetParmScalar<uchar, int32>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->UInt )
	husdSetParmScalar<uint32, int32>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->UInt64 )
	husdSetParmScalar<uint64, int64>( parm, attrib, timecode );

    else if( type == SdfValueTypeNames->Bool )
	husdSetParmScalar<bool>( parm, attrib, timecode );

    else if( type == SdfValueTypeNames->String )
	husdSetParmString<std::string>( parm, attrib, timecode,
            [](const std::string &v) { return v.c_str(); } );
    else if( type == SdfValueTypeNames->Token )
	husdSetParmString<TfToken>( parm, attrib, timecode,
            [](const TfToken &v) { return v.GetText(); } );
    else if( type == SdfValueTypeNames->Asset )
	husdSetParmAssetPath( parm, attrib, timecode );

    else if( type == SdfValueTypeNames->Matrix2d )
	husdSetParmMatrix<GfMatrix2d>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Matrix3d )
	husdSetParmMatrix<GfMatrix3d>( parm, attrib, timecode );
    else if( type == SdfValueTypeNames->Matrix4d ||
             type == SdfValueTypeNames->Frame4d )
	husdSetParmMatrix<GfMatrix4d>( parm, attrib, timecode );

    else if( type == SdfValueTypeNames->TimeCode )
	husdSetParmTimeCode( parm, attrib, timecode );
    else
	ok = false;

    return ok;
}

bool
HUSDsetNodeParm(PRM_Parm &parm,
        const UsdRelationship &rel, 
        bool save_for_undo)
{
    bool		 ok = true;

    // Save the parameter value for undo.
    if (save_for_undo)
    {
        OP_Node         *node = dynamic_cast<OP_Node *>(parm.getParmOwner());
        if (node)
            node->saveParmForUndo(&parm);
    }

    SdfPathVector targets;
    if (rel.GetTargets(&targets))
    {
        UT_WorkBuffer buf;

        for (auto &&target : targets)
        {
            if (!buf.isEmpty())
                buf.append(' ');
            buf.append(target.GetString());
        }
        parm.setValue( 0, buf.buffer(), CH_STRING_LITERAL );
        ok = true;
    }

    return ok;
}

template<typename UT_VALUE_TYPE>
bool
HUSDgetAttribute(const UsdAttribute &attribute, UT_VALUE_TYPE &ut_value,
	const UsdTimeCode &timecode)
{
    bool			    ok = false;
    XUSD_GET_GF_TYPE(UT_VALUE_TYPE) gf_value;

    if (attribute.GetTypeName() == 
	SdfSchema::GetInstance().FindType(XUSD_GET_TYPE_NAME(UT_VALUE_TYPE)))
    {
	ok = attribute.Get(&gf_value, timecode);
    }
    else
    {
	VtValue	    vt_value;

	if(attribute.Get(&vt_value, timecode))
	    ok = husdGetGfFromVt(gf_value, vt_value);
    }

    if(ok)
	ut_value = husdGetUtFromGf(gf_value);

    return ok;
}

template<typename UT_VALUE_TYPE>
bool
HUSDgetAttributeSpecDefault(const SdfAttributeSpec &spec,
	UT_VALUE_TYPE &ut_value)
{
    bool			    ok = false;
    VtValue			    vt_value = spec.GetDefaultValue();
    XUSD_GET_GF_TYPE(UT_VALUE_TYPE) gf_value;

    if (spec.GetTypeName() == 
	SdfSchema::GetInstance().FindType(XUSD_GET_TYPE_NAME(UT_VALUE_TYPE)))
    {
	gf_value = vt_value.UncheckedGet<XUSD_GET_GF_TYPE(UT_VALUE_TYPE)>();
	ok = true;
    }
    else
    {
	ok = husdGetGfFromVt(gf_value, vt_value);
    }

    if(ok)
	ut_value = husdGetUtFromGf(gf_value);

    return ok;
}

static inline bool
husdSplitName(TfToken &key, TfToken &sub_keys, const TfToken &name)
{
    auto keys = SdfPath::TokenizeIdentifier(name.GetString());
    if( keys.size() <= 0 )
	return false;

    key = TfToken(keys.front());
    keys.erase(keys.begin());
    sub_keys = TfToken(SdfPath::JoinIdentifier( keys ));

    return true;
}

template<typename UT_VALUE_TYPE, typename F>
bool
HUSDsetMetadataHelper(const UsdObject &object, const TfToken &name,
	const UT_VALUE_TYPE &ut_value, F fn)
{
    TfToken key, sub_keys;
    if( !husdSplitName(key, sub_keys, name))
	return false;

    auto gf_value = fn(ut_value);
    VtValue vt_value(gf_value);
    if (vt_value.IsEmpty())
	return false;

    return object.SetMetadataByDictKey(key, sub_keys, vt_value);
}

template<typename UT_VALUE_TYPE>
bool
HUSDsetMetadata(const UsdObject &object, const TfToken &name,
	const UT_VALUE_TYPE &ut_value)
{
    return HUSDsetMetadataHelper(object, name, ut_value, 
	    []( const UT_VALUE_TYPE &v )
	    { 
		return husdGetGfFromUt(v);
	    });
}

template<typename UT_VALUE_TYPE>
HUSD_API bool
HUSDgetMetadata(const UsdObject &object, const TfToken &name,
	UT_VALUE_TYPE &ut_value)
{
    TfToken key, sub_keys;
    if( !husdSplitName(key, sub_keys, name))
	return false;

    VtValue vt_value;
    if(!object.GetMetadataByDictKey(key, sub_keys, &vt_value))
	return false;

    XUSD_GET_GF_TYPE(UT_VALUE_TYPE) gf_value;
    if( !husdGetGfFromVt(gf_value, vt_value))
	return false;

    ut_value = husdGetUtFromGf(gf_value);
    return true;
}

bool
HUSDclearMetadata(const UsdObject &object, const TfToken &name)
{
    TfToken key, sub_keys;
    if( !husdSplitName(key, sub_keys, name))
	return false;

    return object.ClearMetadataByDictKey(key, sub_keys);
}

bool
HUSDhasMetadata(const UsdObject &object, const TfToken &name)
{
    TfToken key, sub_keys;
    if( !husdSplitName(key, sub_keys, name))
	return false;

    return object.HasMetadataDictKey(key, sub_keys);
}

bool
HUSDisArrayMetadata(const UsdObject &object, const TfToken &name)
{
    TfToken key, sub_keys;
    if( !husdSplitName(key, sub_keys, name))
	return false;

    VtValue vt_value;
    if(!object.GetMetadataByDictKey(key, sub_keys, &vt_value))
	return false;

    return vt_value.IsArrayValued();
}

exint
HUSDgetMetadataLength(const UsdObject &object, const TfToken &name)
{
    TfToken key, sub_keys;
    if( !husdSplitName(key, sub_keys, name))
	return 0;

    VtValue vt_value;
    if(!object.GetMetadataByDictKey(key, sub_keys, &vt_value))
	return 0;

    // Non-array values have a conceptual length of 1.
    return vt_value.IsArrayValued() ? vt_value.GetArraySize() : 1;
}

template<typename UT_VALUE_TYPE>
bool
HUSDgetValue( const VtValue &vt_value, UT_VALUE_TYPE &ut_value )
{
    XUSD_GET_GF_TYPE(UT_VALUE_TYPE) gf_value;

    bool ok = husdGetGfFromVt(gf_value, vt_value);
    if(ok)
	ut_value = husdGetUtFromGf(gf_value);

    return ok;
}

template<typename UT_VALUE_TYPE>
VtValue
HUSDgetVtValue( const UT_VALUE_TYPE &ut_value )
{
    XUSD_GET_GF_TYPE(UT_VALUE_TYPE) gf_value = husdGetGfFromUt(ut_value);

    return VtValue(gf_value);
}

// ============================================================================
#define XUSD_INSTANTIATION(UT_VALUE_TYPE)				    \
    template HUSD_API const char *  HUSDgetSdfTypeName<UT_VALUE_TYPE>();    \
    template HUSD_API bool	    HUSDsetAttribute(const UsdAttribute &,  \
	    const UT_VALUE_TYPE &, const UsdTimeCode &);		    \
    template HUSD_API bool	    HUSDgetAttribute(const UsdAttribute &,  \
	    UT_VALUE_TYPE &, const UsdTimeCode &);			    \
    template HUSD_API bool	    HUSDgetAttributeSpecDefault(	    \
	    const SdfAttributeSpec &, UT_VALUE_TYPE &);			    \
    template HUSD_API bool	    HUSDsetMetadata(const UsdObject &,	    \
	    const TfToken &, const UT_VALUE_TYPE &);			    \
    template HUSD_API bool	    HUSDgetMetadata(const UsdObject &,	    \
	    const TfToken &, UT_VALUE_TYPE &);				    \
    template HUSD_API bool	    HUSDgetValue( const VtValue &,	    \
	    UT_VALUE_TYPE &);						    \
    template HUSD_API VtValue	    HUSDgetVtValue( const UT_VALUE_TYPE &); \

#define XUSD_INSTANTIATION_PAIR(UT_VALUE_TYPE)		\
    XUSD_INSTANTIATION(UT_VALUE_TYPE)			\
    XUSD_INSTANTIATION(UT_Array<UT_VALUE_TYPE>)		\
    XUSD_INSTANTIATION(UT_ValArray<UT_VALUE_TYPE>)	\

XUSD_INSTANTIATION_PAIR( bool )
XUSD_INSTANTIATION_PAIR( int32 )
XUSD_INSTANTIATION_PAIR( uint32 )
XUSD_INSTANTIATION_PAIR( int64 )
XUSD_INSTANTIATION_PAIR( uint64 )
XUSD_INSTANTIATION_PAIR( fpreal32 )
XUSD_INSTANTIATION_PAIR( fpreal64 )
XUSD_INSTANTIATION_PAIR( UT_StringHolder )
XUSD_INSTANTIATION_PAIR( UT_Vector2i )
XUSD_INSTANTIATION_PAIR( UT_Vector3i )
XUSD_INSTANTIATION_PAIR( UT_Vector4i )
XUSD_INSTANTIATION_PAIR( UT_Vector2F )
XUSD_INSTANTIATION_PAIR( UT_Vector3F )
XUSD_INSTANTIATION_PAIR( UT_Vector4F )
XUSD_INSTANTIATION_PAIR( UT_Vector2D )
XUSD_INSTANTIATION_PAIR( UT_Vector3D )
XUSD_INSTANTIATION_PAIR( UT_Vector4D )
XUSD_INSTANTIATION_PAIR( UT_QuaternionH )
XUSD_INSTANTIATION_PAIR( UT_QuaternionF )
XUSD_INSTANTIATION_PAIR( UT_QuaternionD )
XUSD_INSTANTIATION_PAIR( UT_Matrix2D )
XUSD_INSTANTIATION_PAIR( UT_Matrix3D )
XUSD_INSTANTIATION_PAIR( UT_Matrix4D )
XUSD_INSTANTIATION_PAIR( HUSD_AssetPath )
XUSD_INSTANTIATION_PAIR( HUSD_Token )

#undef XUSD_INSTANTIATION
#undef XUSD_INSTANTIATION_PAIR

// ============================================================================
// Special case for using `const char *` to set a string attribute value.
template<> HUSD_API const char *
HUSDgetSdfTypeName<const char *>()
{
    return "string";
}

template<> HUSD_API bool
HUSDsetAttribute(const UsdAttribute &attribute, const char * const &ut_value,
	const UsdTimeCode &timecode)
{
    return HUSDsetAttributeHelper(attribute, ut_value, timecode,
	    []( const char * const &v )
	    { 
		return std::string(v);
	    });
}

template<> HUSD_API const char *
HUSDgetSdfTypeName<UT_Array<const char *>>()
{
    return "string[]";
}

template<> HUSD_API bool
HUSDsetAttribute(const UsdAttribute &attribute, 
	const UT_Array<const char *> &ut_value, const UsdTimeCode &timecode)
{
    return HUSDsetAttributeHelper(attribute, ut_value, timecode,
	    []( const UT_Array<const char *> &v )
	    { 
		VtArray<std::string> out(v.size());
		for (int i = 0, n = v.size(); i < n; ++i)
		    out[i] = v[i];
		return out;
	    });
}

template<> HUSD_API bool
HUSDsetMetadata(const UsdObject &obj, const TfToken &name,
	const char * const &ut_value)
{
    return HUSDsetMetadataHelper(obj, name, ut_value, 
	    []( const char * const &v )
	    { 
		return std::string(v);
	    });
}

template<> HUSD_API bool
HUSDsetMetadata(const UsdObject &obj, const TfToken &name,
	const UT_Array<const char *> &ut_value)
{
    return HUSDsetMetadataHelper(obj, name, ut_value, 
	    []( const UT_Array<const char *> &v )
	    { 
		VtArray<std::string> out(v.size());
		for (int i = 0, n = v.size(); i < n; ++i)
		    out[i] = v[i];
		return out;
	    });
}

// ============================================================================
// USD's SdfValueTypeNames does not have float versions of matrices (ie,
// Matrix2f for GfMatrix2f, Matrix3f for GfMatrix3f, or Matrix4f for GfMatrix4f)
// so if we try to make equivalence between GfMatrix2f and UT_Matrix2F,
// we get compile-time assertions in Sdf.
// To work around this, we use the available double-precission functions 
// and convert to float matrices.
// TODO: When USD has "matrix2f" type name linked to GfMatrix2f type 
//	 in pxr/usd/lib/sdf/schema.cpp, remove these specializations and 
//	 implement them as explicit instantiations like we did for GfMatrix2d.
// NOTE: It's unlikely that SdfValueTypeNames will ever have float matrices:
//	 https://groups.google.com/forum/#!topic/usd-interest/DZaRCUlg3RA
#define XUSD_SPECIALIZE_FLOAT_MATRIX(F_TYPE, D_TYPE)			\
template<>								\
HUSD_API const char * HUSDgetSdfTypeName<F_TYPE>()			\
{									\
    return XUSD_GET_TYPE_NAME(D_TYPE);					\
}									\
									\
template<>								\
HUSD_API bool HUSDsetAttribute<F_TYPE>(	 const UsdAttribute &a,		\
	const F_TYPE &v, const UsdTimeCode &t)				\
{									\
    D_TYPE tmp(v);							\
    return HUSDsetAttribute<D_TYPE>(a, tmp, t);				\
}									\
									\
template<>								\
HUSD_API bool HUSDgetAttribute<F_TYPE>( const UsdAttribute &a,		\
	F_TYPE &v, const UsdTimeCode &t)				\
{									\
    D_TYPE tmp;								\
    if(!HUSDgetAttribute<D_TYPE>(a, tmp, t))				\
	return false;							\
									\
    v = tmp;								\
    return true;							\
}									\
									\
template<>								\
HUSD_API bool HUSDsetMetadata<F_TYPE>( const UsdObject &o,		\
	const TfToken &n, const F_TYPE &v)				\
{									\
    D_TYPE tmp(v);							\
    return HUSDsetMetadata<D_TYPE>(o, n, tmp);				\
}									\
									\
template<>								\
HUSD_API bool HUSDgetMetadata<F_TYPE>( const UsdObject &o,		\
	const TfToken &n, F_TYPE &v)					\
{									\
    D_TYPE tmp;								\
    if(!HUSDgetMetadata<D_TYPE>(o, n, tmp))				\
	return false;							\
									\
    v = tmp;								\
    return true;							\
}									\
									\
template<> 								\
HUSD_API bool HUSDgetValue<F_TYPE>(const VtValue &vt, F_TYPE &ut)	\
{									\
    D_TYPE tmp;								\
    if(!HUSDgetValue<D_TYPE>(vt, tmp))					\
	return false;							\
									\
    ut = tmp;								\
    return true;							\
}									\
									\
template<>								\
HUSD_API const char * HUSDgetSdfTypeName<UT_Array<F_TYPE>>()		\
{									\
    return XUSD_GET_TYPE_NAME(UT_Array<D_TYPE>);			\
}									\
									\
template<>								\
HUSD_API bool HUSDsetAttribute<UT_Array<F_TYPE>>(const UsdAttribute &a, \
	const UT_Array<F_TYPE> &v, const UsdTimeCode &t)		\
{									\
    UT_Array<D_TYPE> tmp(v.size(), v.size());				\
    for( int i=0; i < v.size(); ++i )					\
	tmp[i] = v[i];							\
    return HUSDsetAttribute<UT_Array<D_TYPE>>(a, tmp, t);		\
}									\
									\
template<>								\
HUSD_API bool HUSDgetAttribute<UT_Array<F_TYPE>>(const UsdAttribute &a,	\
	UT_Array<F_TYPE> &v, const UsdTimeCode &t)			\
{									\
    UT_Array<D_TYPE> tmp;						\
    if(!HUSDgetAttribute<UT_Array<D_TYPE>>(a, tmp, t))			\
	return false;							\
									\
    v.setSize( tmp.size() );						\
    for( int i=0; i < tmp.size(); ++i )					\
	v[i] = tmp[i];							\
									\
    return true;							\
}									\
									\
template<>								\
HUSD_API bool HUSDsetMetadata<UT_Array<F_TYPE>>(const UsdObject &o,	\
	const TfToken &n, const UT_Array<F_TYPE> &v)			\
{									\
    UT_Array<D_TYPE> tmp(v.size(), v.size());				\
    for( int i=0; i < v.size(); ++i )					\
	tmp[i] = v[i];							\
    return HUSDsetMetadata<UT_Array<D_TYPE>>(o, n, tmp);		\
}									\
									\
template<>								\
HUSD_API bool HUSDgetMetadata<UT_Array<F_TYPE>>(const UsdObject &o,	\
	const TfToken &n, UT_Array<F_TYPE> &v)				\
{									\
    UT_Array<D_TYPE> tmp;						\
    if(!HUSDgetMetadata<UT_Array<D_TYPE>>(o, n, tmp))			\
	return false;							\
									\
    v.setSize( tmp.size() );						\
    for( int i=0; i < tmp.size(); ++i )					\
	v[i] = tmp[i];							\
									\
    return true;							\
}									\
									\
template<> 								\
HUSD_API bool HUSDgetValue<UT_Array<F_TYPE>>(const VtValue &vt,		\
	UT_Array<F_TYPE> &ut)						\
{									\
    UT_Array<D_TYPE> tmp;						\
    if(!HUSDgetValue<UT_Array<D_TYPE>>(vt, tmp))			\
	return false;							\
									\
    ut.setSize( tmp.size() );						\
    for( int i=0; i < tmp.size(); ++i )					\
	ut[i] = tmp[i];							\
									\
    return true;							\
}									\
									
XUSD_SPECIALIZE_FLOAT_MATRIX( UT_Matrix2F, UT_Matrix2D )
XUSD_SPECIALIZE_FLOAT_MATRIX( UT_Matrix3F, UT_Matrix3D )
XUSD_SPECIALIZE_FLOAT_MATRIX( UT_Matrix4F, UT_Matrix4D )

#undef XUSD_SPECIALIZE_FLOAT_MATRIX


// ============================================================================
/// Maps the VOP data type to USD's value type name.
static inline SdfValueTypeName
husdGetSdfTypeFromVopType( VOP_Type vop_type )
{
    // Note: UsdShade stipulates using float values for shading (colors, etc)
    //       and even for point position. The idea is to transform the 
    //       geometery with double-precision matrices first.
    switch( vop_type )
    {
	case VOP_TYPE_VECTOR4:
	    return SdfValueTypeNames->Float4;

	case VOP_TYPE_VECTOR:	
	    return SdfValueTypeNames->Vector3f;

	case VOP_TYPE_POINT:
	    return SdfValueTypeNames->Point3f;

	case VOP_TYPE_NORMAL:
	    return SdfValueTypeNames->Normal3f;

	case VOP_TYPE_COLOR:
	    return SdfValueTypeNames->Color3f;

	case VOP_TYPE_VECTOR2:	
	    return SdfValueTypeNames->Float2;

	case VOP_TYPE_FLOAT:	
	    return SdfValueTypeNames->Float;

	case VOP_TYPE_INTEGER:	
	    return SdfValueTypeNames->Int;

	case VOP_TYPE_STRING:	
	    return SdfValueTypeNames->String;

	case VOP_TYPE_MATRIX2:	
	    return SdfValueTypeNames->Matrix2d;

	case VOP_TYPE_MATRIX3:	
	    return SdfValueTypeNames->Matrix3d;

	case VOP_TYPE_MATRIX4:	
	    return SdfValueTypeNames->Matrix4d;

	case VOP_TYPE_CUSTOM:	
	    UT_ASSERT( !"Not implemented yet." );
	    return SdfValueTypeNames->String;

	default:
	    UT_ASSERT( !"Unhandled parameter type" );
	    return SdfValueTypeNames->String;
    }

    return SdfValueTypeName();
}

static inline VOP_Type
husdGetVopTypeFromParm( const PRM_Parm &parm )
{
    VOP_Node *vop = CAST_VOPNODE( parm.getParmOwner()->castToOPNode() );
    if( !vop )
	return VOP_TYPE_UNDEF;

    const VOP_NodeParmManager &mgr = vop->getLanguage()->getParmManager();
    int parm_type_idx = mgr.guessParmIndex(VOP_TYPE_UNDEF, 
		    parm.getType(), parm.getVectorSize());
    return mgr.getParmType( parm_type_idx );
}

SdfValueTypeName
HUSDgetShaderAttribSdfTypeName( const PRM_Parm &parm )
{
    // Some specialized handling of some parameters. 
    // It's based on PI_EditScriptedParm::getScriptType() 
    if( (parm.getType() & PRM_FILE) == PRM_FILE )
	// Any file parameter represents a resolvable USD asset.
	return SdfValueTypeNames->Asset;
    
    // For generic parameters, leverage the VOP_NodeParmManager.
    return husdGetSdfTypeFromVopType( husdGetVopTypeFromParm( parm ));
}

SdfValueTypeName
HUSDgetShaderInputSdfTypeName( const VOP_Node &vop, int input_idx,
	const PRM_Parm *parm )
{
    if( !parm )
    {
	UT_String parm_name;

	vop.getParmNameFromInput( parm_name, input_idx );
	parm = vop.getParmPtr( parm_name );
    }

    SdfValueTypeName result;
    if( parm )
	result = HUSDgetShaderAttribSdfTypeName( *parm );
    else
	result = husdGetSdfTypeFromVopType( vop.getInputType( input_idx ));
    return result;
}

SdfValueTypeName
HUSDgetShaderOutputSdfTypeName( const VOP_Node &vop, int output_idx,
	const PRM_Parm *parm )
{
    if( !parm )
    {
	UT_String parm_name;

	vop.getParmNameFromOutput( parm_name, output_idx );
	parm = vop.getParmPtr( parm_name );
    }

    SdfValueTypeName result; 
    if( parm )
	result = HUSDgetShaderAttribSdfTypeName( *parm );
    else
	result = husdGetSdfTypeFromVopType( 
		SYSconst_cast(&vop)->getOutputType( output_idx ));
    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE

