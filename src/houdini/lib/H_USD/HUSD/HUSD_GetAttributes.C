/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Rafeek Rahamut
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_GetAttributes.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE
HUSD_GetAttributes::HUSD_GetAttributes(HUSD_AutoAnyLock &lock)
    : myAnyLock(lock),
      myTimeSampling(HUSD_TimeSampling::NONE)
{
}

HUSD_GetAttributes::~HUSD_GetAttributes()
{
}

static inline UsdPrim
husdGetPrimAtPath( HUSD_AutoAnyLock &lock, const UT_StringRef &primpath) 
{
    UsdPrim prim;

    if (primpath.isstring() &&
	lock.constData() && lock.constData()->isStageValid())
    {
	SdfPath sdfpath(HUSDgetSdfPath(primpath));
	prim = lock.constData()->stage()->GetPrimAtPath(sdfpath);
    }

    return prim;
}

static inline UsdGeomPrimvar
husdGetPrimvar( HUSD_AutoAnyLock &lock, const UT_StringRef &primpath,
	const UT_StringRef &primvarname)
{
    UsdGeomPrimvarsAPI	api(husdGetPrimAtPath(lock, primpath));
    if (!api)
	return UsdGeomPrimvar(UsdAttribute());

    return api.GetPrimvar(TfToken(primvarname.toStdString()));
}

template<typename UtValueType>
bool
HUSD_GetAttributes::getAttribute(const UT_StringRef &primpath,
	const UT_StringRef &attribname, UtValueType &value,
	const HUSD_TimeCode &timecode) const
{
    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    if (!prim)
	return false;

    auto attrib = prim.GetAttribute(TfToken(attribname.toStdString()));
    if (!attrib)
	return false;

    bool success = HUSDgetAttribute(attrib, value, 
	    HUSDgetNonDefaultUsdTimeCode(timecode));
    HUSDupdateValueTimeSampling(myTimeSampling, attrib);
    return success;
}

template<typename UtValueType>
bool
HUSD_GetAttributes::getPrimvar(const UT_StringRef &primpath,
	const UT_StringRef &primvarname, UtValueType &value,
	const HUSD_TimeCode &tc) const
{
    auto primvar(husdGetPrimvar(myAnyLock, primpath, primvarname));
    if (!primvar)
	return false;

    VtValue vt_value;
    bool success = primvar.Get(&vt_value, HUSDgetNonDefaultUsdTimeCode(tc));
    HUSDupdateValueTimeSampling(myTimeSampling, primvar);
    if (!success)
	return false;

    return HUSDgetValue( vt_value, value );
}

template<typename UtValueType>
bool
HUSD_GetAttributes::getFlattenedPrimvar(const UT_StringRef &primpath,
	const UT_StringRef &primvarname, UT_Array<UtValueType> &value,
	const HUSD_TimeCode &tc) const
{
    auto primvar(husdGetPrimvar(myAnyLock, primpath, primvarname));
    if (!primvar)
	return false;

    VtValue vt_value;
    bool success = primvar.ComputeFlattened(&vt_value,
	    HUSDgetNonDefaultUsdTimeCode(tc));
    HUSDupdateValueTimeSampling(myTimeSampling, primvar);
    if (!success)
	return false;

    return HUSDgetValue( vt_value, value );;
}

bool
HUSD_GetAttributes::isPrimvarIndexed(const UT_StringRef &primpath,
	const UT_StringRef &primvarname) const
{
    auto primvar(husdGetPrimvar(myAnyLock, primpath, primvarname));
    if (!primvar)
	return false;

    return primvar.IsIndexed();
}

bool
HUSD_GetAttributes::getPrimvarIndices(const UT_StringRef &primpath,
	const UT_StringRef &primvarname, UT_ExintArray &indices,
	const HUSD_TimeCode &tc) const
{
    VtIntArray vt_indices;

    auto usd_tc(HUSDgetNonDefaultUsdTimeCode(tc));
    auto primvar(husdGetPrimvar(myAnyLock, primpath, primvarname));
    if (!primvar || !primvar.GetIndices(&vt_indices, usd_tc))
	return false;

    indices.setCapacity(vt_indices.size());
    indices.setSize(vt_indices.size());
    for( exint i = 0; i < vt_indices.size(); i++ )
	indices[i] = vt_indices[i];

    return true;
}

UT_StringHolder	
HUSD_GetAttributes::getPrimvarInterpolation(const UT_StringRef &primpath,
	const UT_StringRef &primvarname) const
{
    auto primvar(husdGetPrimvar(myAnyLock, primpath, primvarname));
    if (!primvar)
	return UT_StringHolder();

    return UT_StringHolder(primvar.GetInterpolation().GetString());
}

exint
HUSD_GetAttributes::getPrimvarElementSize(const UT_StringRef &primpath,
	const UT_StringRef &primvarname) const
{
    auto primvar(husdGetPrimvar(myAnyLock, primpath, primvarname));
    if (!primvar)
	return 0;

    return primvar.GetElementSize();
}

bool
HUSD_GetAttributes::getIsTimeVarying() const
{
    return HUSDisTimeVarying( myTimeSampling );
}

bool
HUSD_GetAttributes::getIsTimeSampled() const
{
    return HUSDisTimeSampled( myTimeSampling );
}

//----------------------------------------------------------------------------
// Instantiate the template types explicitly


#define HUSD_EXPLICIT_INSTANTIATION(UtType)				\
    template HUSD_API_TINST bool HUSD_GetAttributes::getAttribute(	\
	const UT_StringRef	&primpath,				\
	const UT_StringRef	&attribname,				\
	UtType			&value,					\
	const HUSD_TimeCode	&timecode) const;			\
    template HUSD_API_TINST bool HUSD_GetAttributes::getPrimvar(	\
	const UT_StringRef	&primpath,				\
	const UT_StringRef	&primvarname,				\
	UtType			&value,					\
	const HUSD_TimeCode	&timecode) const;			\

#define HUSD_EXPLICIT_INSTANTIATION_SET(UtType)				\
    HUSD_EXPLICIT_INSTANTIATION(UtType)					\
    HUSD_EXPLICIT_INSTANTIATION(UT_Array<UtType>)			\
    template HUSD_API_TINST bool HUSD_GetAttributes::getFlattenedPrimvar( \
	const UT_StringRef	&primpath,				\
	const UT_StringRef	&primvarname,				\
	UT_Array<UtType>	&value,					\
	const HUSD_TimeCode	&timecode) const;			\

HUSD_EXPLICIT_INSTANTIATION_SET(bool)
HUSD_EXPLICIT_INSTANTIATION_SET(int)
HUSD_EXPLICIT_INSTANTIATION_SET(int64)
HUSD_EXPLICIT_INSTANTIATION_SET(fpreal32)
HUSD_EXPLICIT_INSTANTIATION_SET(fpreal64)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_StringHolder)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Vector2i)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Vector3i)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Vector4i)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Vector2F)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Vector3F)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Vector4F)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Vector2D)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Vector3D)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Vector4D)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_QuaternionH)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_QuaternionF)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_QuaternionD)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Matrix2F)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Matrix3F)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Matrix4F)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Matrix2D)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Matrix3D)
HUSD_EXPLICIT_INSTANTIATION_SET(UT_Matrix4D)

#undef HUSD_EXPLICIT_INSTANTIATION
#undef HUSD_EXPLICIT_INSTANTIATION_SET

