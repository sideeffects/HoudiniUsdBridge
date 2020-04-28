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
 *      Calvin Gu
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_SetAttributes.h"
#include "HUSD_AssetPath.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <UT/UT_Matrix2.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Options.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_Vector2.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_Vector4.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_SetAttributes::HUSD_SetAttributes(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_SetAttributes::~HUSD_SetAttributes()
{
}

static inline UsdPrim
husdGetPrimAtPath(HUSD_AutoWriteLock &lock, const UT_StringRef &primpath) 
{
    UsdPrim	prim;
    auto	outdata = lock.data();

    if (primpath.isstring() && outdata && outdata->isStageValid())
    {
	SdfPath sdfpath(HUSDgetSdfPath(primpath));

	// We never want to return the root prim. We can't get or set
	// any attributes on a root prim.
	if (!sdfpath.IsEmpty() && sdfpath != SdfPath::AbsoluteRootPath())
	    prim = outdata->stage()->OverridePrim(sdfpath);
    }

    return prim;
}

static inline UsdAttribute
husdGetAttrib( HUSD_AutoWriteLock &lock, const UT_StringRef &primpath,
	const UT_StringRef &attrib_name)
{
    auto prim = husdGetPrimAtPath(lock, primpath);
    if (!prim)
	return UsdAttribute();

    return prim.GetAttribute(TfToken(attrib_name.toStdString()));
}

static inline UsdGeomPrimvar
husdGetPrimvar( HUSD_AutoWriteLock &lock, const UT_StringRef &primpath,
	const UT_StringRef &primvar_name)
{
    UsdGeomPrimvarsAPI	api(husdGetPrimAtPath(lock, primpath));
    if (!api)
	return UsdGeomPrimvar(UsdAttribute());

    return api.GetPrimvar(TfToken(primvar_name.toStdString()));
}

bool
HUSD_SetAttributes::addAttribute(const UT_StringRef &primpath,
	const UT_StringRef &attrname, const UT_StringRef &type) const
{
    auto prim = husdGetPrimAtPath(myWriteLock, primpath);
    if (!prim)
	return false;

    auto sdftype = SdfSchema::GetInstance().FindType(type.c_str());
    return (bool) prim.CreateAttribute(TfToken(attrname.toStdString()),sdftype);
}

bool
HUSD_SetAttributes::addPrimvar(const UT_StringRef &primpath,
	const UT_StringRef &primvar_name,
	const UT_StringRef &interpolation,
	const UT_StringRef &type_name) const
{
    UsdGeomPrimvarsAPI	api(husdGetPrimAtPath(myWriteLock, primpath));
    if (!api)
	return false;

    auto sdftype = SdfSchema::GetInstance().FindType(type_name.c_str());
    return (bool) api.CreatePrimvar(TfToken(primvar_name.toStdString()), 
	    sdftype, TfToken(interpolation));
}

template<typename UtValueType>
bool
HUSD_SetAttributes::setAttribute(const UT_StringRef &primpath,
				const UT_StringRef &attrname,
				const UtValueType &value,
				const HUSD_TimeCode &timecode,
				const UT_StringRef &valueType) const
{
    auto prim = husdGetPrimAtPath(myWriteLock, primpath);
    if (!prim)
	return false;

    const char*	sdfvaluename = (valueType.isEmpty() ?
	    HUSDgetSdfTypeName<UtValueType>() : valueType.c_str());
    auto sdftype = SdfSchema::GetInstance().FindType(sdfvaluename);
    auto attr = prim.CreateAttribute(TfToken(attrname.toStdString()), sdftype);
    if (!attr)
	return false;

    attr.SetVariability(SdfVariability::SdfVariabilityVarying);
    return HUSDsetAttribute(attr, value, HUSDgetUsdTimeCode(timecode));
}

template<typename UtValueType>
bool
HUSD_SetAttributes::setPrimvar(const UT_StringRef &primpath,
			    const UT_StringRef &primvarname,
			    const UT_StringRef &interpolation,
			    const UtValueType &value,
			    const HUSD_TimeCode &timecode,
			    const UT_StringRef &valueType,
                            int elementsize) const
{
    UsdGeomPrimvarsAPI	api(husdGetPrimAtPath(myWriteLock, primpath));
    if (!api)
	return false;

    const char* sdfvaluename = (valueType == UT_String::getEmptyString() ?
	    HUSDgetSdfTypeName<UtValueType>() : valueType.c_str());
    auto sdfvaluetype = SdfSchema::GetInstance().FindType(sdfvaluename);
    auto primvar = api.CreatePrimvar( TfToken(primvarname.toStdString()),
	    sdfvaluetype, TfToken(interpolation));
    if (!primvar)
	return false;

    auto attr = primvar.GetAttr();
    attr.SetVariability(SdfVariability::SdfVariabilityVarying);
    if (elementsize > 1)
        primvar.SetElementSize(elementsize);

    return HUSDsetAttribute(attr, value, HUSDgetUsdTimeCode(timecode));
}

bool
HUSD_SetAttributes::setAttributes(const UT_StringRef &primpath,
        const UT_Options &options,
        const HUSD_TimeCode &timecode,
        const UT_StringRef &attrnamespace) const
{
    auto prim = husdGetPrimAtPath(myWriteLock, primpath);
    if (!prim)
	return false;

    for (auto it = options.begin(); it != options.end(); ++it)
    {
        UT_WorkBuffer    buf;
        if (attrnamespace.isstring())
        {
            buf.append(attrnamespace);
            buf.append(':');
        }
        buf.append(it.name());
        switch (it.entry()->getType())
        {
            case UT_OPTION_INT:
            {
                int val = it.entry()->getOptionI();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_BOOL:
            {
                bool val = it.entry()->getOptionI();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_FPREAL:
            {
                fpreal64 val = it.entry()->getOptionF();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_STRING:
            case UT_OPTION_STRINGRAW:
            {
                UT_StringHolder val = it.entry()->getOptionS();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_UV:
            case UT_OPTION_VECTOR2:
            {
                UT_Vector2D val = it.entry()->getOptionV2();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_UVW:
            case UT_OPTION_VECTOR3:
            {
                UT_Vector3D val = it.entry()->getOptionV3();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_VECTOR4:
            {
                UT_Vector4D val = it.entry()->getOptionV4();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_QUATERNION:
            {
                UT_QuaternionD val = it.entry()->getOptionQ();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_MATRIX2:
            {
                UT_Matrix2D val = it.entry()->getOptionM2();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_MATRIX3:
            {
                UT_Matrix3D val = it.entry()->getOptionM3();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_MATRIX4:
            {
                UT_Matrix4D val = it.entry()->getOptionM4();
                if (!setAttribute(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_DICT:
            {
                if (!setAttributes(primpath,
                        *it.entry()->getOptionDict().options(),
                        timecode, buf.buffer()))
                    return false;
                break;
            }
            case UT_OPTION_INTARRAY:
            {
                const UT_Int64Array &val = it.entry()->getOptionIArray();
                if (!setAttributeArray(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_FPREALARRAY:
            {
                const UT_Fpreal64Array &val = it.entry()->getOptionFArray();
                if (!setAttributeArray(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }
            case UT_OPTION_STRINGARRAY:
            {
                const UT_StringArray &val = it.entry()->getOptionSArray();
                if (!setAttributeArray(primpath, buf.buffer(), val, timecode))
                    return false;
                break;
            }

            case UT_OPTION_DICTARRAY:
            case UT_OPTION_INVALID:
            case UT_OPTION_NUM_TYPES:
                return false;
        };
    }

    return true;
}

bool
HUSD_SetAttributes::blockAttribute(const UT_StringRef &primpath,
	const UT_StringRef &attrname) const
{
    auto attrib(husdGetAttrib(myWriteLock, primpath, attrname));
    // If the attribute doesn't exist, that's as good as being blocked.
    if (!attrib)
	return true;

    attrib.Block();
    return true;
}

bool
HUSD_SetAttributes::blockPrimvar(
	const UT_StringRef &primpath, const UT_StringRef &primvar_name) const
{
    auto primvar(husdGetPrimvar(myWriteLock, primpath, primvar_name));
    if (!primvar)
	return false;

    primvar.GetAttr().Block();
    return true;
}

bool
HUSD_SetAttributes::blockPrimvarIndices(
	const UT_StringRef &primpath, const UT_StringRef &primvar_name) const
{
    auto primvar(husdGetPrimvar(myWriteLock, primpath, primvar_name));
    if (!primvar)
	return false;

    primvar.BlockIndices();
    return true;
}

bool
HUSD_SetAttributes::setPrimvarIndices( 
	const UT_StringRef &primpath, const UT_StringRef &primvar_name,
	const UT_ExintArray &indices, const HUSD_TimeCode &timecode) const
{
    auto primvar(husdGetPrimvar(myWriteLock, primpath, primvar_name));
    if (!primvar)
	return false;

    VtIntArray vt_indices;
    vt_indices.assign( indices.begin(), indices.end() );
    return primvar.SetIndices(vt_indices);
}

HUSD_TimeCode
HUSD_SetAttributes::getAttribEffectiveTimeCode( const UT_StringRef &primpath,
	const UT_StringRef &attribname, const HUSD_TimeCode &timecode) const
{
    auto attrib(husdGetAttrib(myWriteLock, primpath, attribname));
    if( !attrib )
	return timecode;

    return HUSDgetEffectiveTimeCode( timecode, attrib );
}

HUSD_TimeCode
HUSD_SetAttributes::getPrimvarEffectiveTimeCode( const UT_StringRef &primpath,
	const UT_StringRef &primvarname, const HUSD_TimeCode &timecode) const
{
    auto primvar(husdGetPrimvar(myWriteLock, primpath, primvarname));
    if (!primvar)
	return timecode;

    return HUSDgetEffectiveTimeCode( timecode, primvar.GetAttr() );
}

HUSD_TimeCode
HUSD_SetAttributes::getPrimvarIndicesEffectiveTimeCode(
	const UT_StringRef &primpath, const UT_StringRef &primvarname,
	const HUSD_TimeCode &timecode) const
{
    auto primvar(husdGetPrimvar(myWriteLock, primpath, primvarname));
    if (!primvar || !primvar.GetIndicesAttr())
	return timecode;

    return HUSDgetEffectiveTimeCode( timecode, primvar.GetIndicesAttr() );
}


#define HUSD_EXPLICIT_INSTANTIATION(UtType)				\
    template HUSD_API_TINST bool HUSD_SetAttributes::setPrimvar(	\
	const UT_StringRef	&primpath,				\
	const UT_StringRef	&attrname,				\
	const UT_StringRef	&interpolation,				\
	const UtType		&value,					\
	const HUSD_TimeCode	&timecode,				\
	const UT_StringRef	&valueType,                             \
        int                     elementsize) const;			\
									\
    template HUSD_API_TINST bool HUSD_SetAttributes::setAttribute(	\
	const UT_StringRef	&primpath,				\
	const UT_StringRef	&attrname,				\
	const UtType		&value,					\
	const HUSD_TimeCode	&timecode,				\
	const UT_StringRef	&valueType) const;

#define HUSD_EXPLICIT_INSTANTIATION_PAIR(UtType)			\
    HUSD_EXPLICIT_INSTANTIATION(UtType)					\
    HUSD_EXPLICIT_INSTANTIATION(UT_Array<UtType>)

HUSD_EXPLICIT_INSTANTIATION_PAIR(bool)
HUSD_EXPLICIT_INSTANTIATION_PAIR(int32)
HUSD_EXPLICIT_INSTANTIATION_PAIR(int64)
HUSD_EXPLICIT_INSTANTIATION_PAIR(fpreal32)
HUSD_EXPLICIT_INSTANTIATION_PAIR(fpreal64)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_StringHolder)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector2i)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector3i)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector4i)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector2F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector3F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector4F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector2D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector3D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector4D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_QuaternionH)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_QuaternionF)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_QuaternionD)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix2F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix3F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix4F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix2D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix3D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix4D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(HUSD_AssetPath)

// Special case for `const char *` arguments.
HUSD_EXPLICIT_INSTANTIATION(char * const)
HUSD_EXPLICIT_INSTANTIATION(UT_Array<const char *>)

#undef HUSD_EXPLICIT_INSTANTIATION
#undef HUSD_EXPLICIT_INSTANTIATION_PAIR

