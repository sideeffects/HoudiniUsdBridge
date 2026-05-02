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

#include "HUSD_ConfigureProps.h"
#include "HUSD_AssetPath.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindProps.h"
#include "HUSD_PathExpression.h"
#include "HUSD_Token.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include <UT/UT_StringMMPattern.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/path.h>
#include <functional>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_ConfigureProps::HUSD_ConfigureProps(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_ConfigureProps::~HUSD_ConfigureProps()
{
}

template <class UsdDerivedType, typename F>
static inline bool
husdConfigProps(HUSD_AutoWriteLock &lock,
	const HUSD_FindProps &findprops,
	F config_fn)
{
    auto		 outdata = lock.data();

    if (!outdata || !outdata->isStageValid())
	return false;

    auto		 stage(outdata->stage());
    bool		 success = true;

    for (auto &&sdfpath : findprops.getExpandedPathSet().sdfPathSet())
    {
	UsdObject	 obj = stage->GetObjectAtPath(sdfpath);

	if (obj)
	{
	    UsdDerivedType	 derived = obj.As<UsdDerivedType>();
	    if (derived && !config_fn(derived))
                success = false;
	}
	else
            success = false;
    }

    return success;
}

bool
HUSD_ConfigureProps::setVariability(const HUSD_FindProps &findprops,
	HUSD_Variability variability) const
{
    SdfVariability	 sdf_variability = HUSDgetSdfVariability(variability);

    return husdConfigProps<UsdAttribute>(myWriteLock, findprops,
	[&](UsdAttribute &attrib)
	{
	    return attrib.SetVariability(sdf_variability);
	});
}

bool
HUSD_ConfigureProps::setColorSpace(const HUSD_FindProps &findprops,
	const UT_StringRef &colorspace) const
{
    TfToken		 tf_colorspace(colorspace.toStdString());

    return husdConfigProps<UsdAttribute>(myWriteLock, findprops,
	[&](UsdAttribute &attrib)
	{
	    if (tf_colorspace.IsEmpty())
		attrib.ClearColorSpace();
	    else
		attrib.SetColorSpace(tf_colorspace);

	    return true;
	});
}

bool
HUSD_ConfigureProps::setInterpolation(const HUSD_FindProps &findprops,
	const UT_StringRef &interpolation) const
{
    TfToken		 tf_interpolation(interpolation.toStdString());

    if (!UsdGeomPrimvar::IsValidInterpolation(tf_interpolation))
    {
        HUSD_ErrorScope::addWarning(
            HUSD_ERR_INVALID_INTERPOLATION, interpolation.c_str());
        return false;
    }

    return husdConfigProps<UsdAttribute>(myWriteLock, findprops,
	[&](UsdAttribute &attrib)
	{
            // Sometimes it is useful/necessary to set interpolation on a
            // non-primvar attribute (such as "widths" on a BasisCurves prim).
            // But one case we can be sure makes no sense is the "indices"
            // attribute associated with a primvar. So exclude that case.
            if (TfStringStartsWith(attrib.GetName(), "primvars:") &&
                TfStringEndsWith(attrib.GetName(), ":indices"))
                return false;

            return attrib.SetMetadata(
                UsdGeomTokens->interpolation, tf_interpolation);
	});
}

bool
HUSD_ConfigureProps::setElementSize(const HUSD_FindProps &findprops,
	int element_size) const
{
    return husdConfigProps<UsdAttribute>(myWriteLock, findprops,
	[&](UsdAttribute &attrib)
	{
	    return attrib.SetMetadata(UsdGeomTokens->elementSize, element_size);
	});
}

bool
HUSD_ConfigureProps::addEditorNodeId(const HUSD_FindProps &findprops,
         int nodeid) const
{
    return husdConfigProps<UsdAttribute>(myWriteLock, findprops,
        [&](UsdAttribute &attrib)
    {
        HUSDaddPropertyEditorNodeId(attrib, nodeid);

        return true;
    });
}

bool
HUSD_ConfigureProps::clearEditorNodeIds(const HUSD_FindProps &findprops) const
{
    return husdConfigProps<UsdAttribute>(myWriteLock, findprops,
        [&](UsdAttribute &attrib)
    {
        HUSDclearPropertyEditorNodeIds(attrib);

        return true;
    });
}

template<typename UtValueType>
bool
HUSD_ConfigureProps::setAssetInfo(const HUSD_FindProps &findprops,
        const UT_StringRef &key,
        const UtValueType &value) const
{
    std::string  key_str(key.toStdString());
    VtValue      vt_value = HUSDgetVtValue(value);

    return husdConfigProps<UsdProperty>(myWriteLock, findprops,
        [&](UsdProperty &prop)
    {
        VtDictionary asset_info = prop.GetAssetInfo();
        asset_info.SetValueAtPath(key_str, vt_value);
        prop.SetAssetInfo(asset_info);

        return true;
    });
}

bool
HUSD_ConfigureProps::removeAssetInfo(const HUSD_FindProps &findprops,
        const UT_StringRef &key) const
{
    std::string	 key_str(key.toStdString());

    return husdConfigProps<UsdProperty>(myWriteLock, findprops,
        [&](UsdProperty &prop)
    {
        VtDictionary asset_info = prop.GetAssetInfo();
        if (asset_info.GetValueAtPath(key_str))
        {
            asset_info.EraseValueAtPath(key_str);
            prop.SetAssetInfo(asset_info);
        }

        return true;
    });
}

bool
HUSD_ConfigureProps::clearAssetInfo(const HUSD_FindProps &findprops) const
{
    return husdConfigProps<UsdProperty>(myWriteLock, findprops,
        [&](UsdProperty &prop)
    {
        if (!prop.HasAssetInfo())
            prop.SetAssetInfo(VtDictionary());

        return true;
    });
}

#define HUSD_EXPLICIT_INSTANTIATION(UtType)				\
    template HUSD_API_TINST bool					\
    HUSD_ConfigureProps::setAssetInfo(					\
	const HUSD_FindProps	&findprops,				\
	const UT_StringRef	&key,					\
	const UtType		&value) const;				\

// Keep the list of supported data types here synchronized with the list of
// data types in the comment in the header file. Otherwise there is no way to
// know which data types can be used to call these templated functions.
HUSD_EXPLICIT_INSTANTIATION(bool)
HUSD_EXPLICIT_INSTANTIATION(int)
HUSD_EXPLICIT_INSTANTIATION(int64)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector2i)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector3i)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector4i)
HUSD_EXPLICIT_INSTANTIATION(fpreal32)
HUSD_EXPLICIT_INSTANTIATION(fpreal64)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector2F)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector3F)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector4F)
HUSD_EXPLICIT_INSTANTIATION(UT_QuaternionF)
HUSD_EXPLICIT_INSTANTIATION(UT_QuaternionH)
HUSD_EXPLICIT_INSTANTIATION(UT_Matrix3D)
HUSD_EXPLICIT_INSTANTIATION(UT_Matrix4D)
HUSD_EXPLICIT_INSTANTIATION(UT_StringHolder)
HUSD_EXPLICIT_INSTANTIATION(UT_Array<UT_StringHolder>)
HUSD_EXPLICIT_INSTANTIATION(HUSD_AssetPath)
HUSD_EXPLICIT_INSTANTIATION(UT_Array<HUSD_AssetPath>)
HUSD_EXPLICIT_INSTANTIATION(HUSD_Token)
HUSD_EXPLICIT_INSTANTIATION(UT_Array<HUSD_Token>)
HUSD_EXPLICIT_INSTANTIATION(HUSD_PathExpression)
HUSD_EXPLICIT_INSTANTIATION(UT_Array<HUSD_PathExpression>)

#undef HUSD_EXPLICIT_INSTANTIATION
