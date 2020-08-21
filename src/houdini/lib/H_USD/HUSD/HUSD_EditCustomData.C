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

#include "HUSD_EditCustomData.h"
#include "HUSD_AssetPath.h"
#include "HUSD_Constants.h"
#include "HUSD_FindPrims.h"
#include "HUSD_FindProps.h"
#include "HUSD_Token.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/layer.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_EditCustomData::HUSD_EditCustomData(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock),
      myModifyRootLayer(false)
{
}

HUSD_EditCustomData::~HUSD_EditCustomData()
{
}

void
HUSD_EditCustomData::setModifyRootLayer(bool modifyrootlayer)
{
    myModifyRootLayer = modifyrootlayer;
}

template<typename UtValueType>
bool
HUSD_EditCustomData::setLayerCustomData(const UT_StringRef &key,
	const UtValueType &value) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 layer = outdata->activeLayer();
	VtDictionary	 data = layer->GetCustomLayerData();
	std::string	 std_key = key.toStdString();
	VtValue		 vt_value = HUSDgetVtValue(value);

	data.SetValueAtPath(std_key, vt_value);
	layer->SetCustomLayerData(data);
        if (myModifyRootLayer)
            outdata->setStageRootPrimMetadata(
                SdfFieldKeys->CustomLayerData, VtValue(data));
	success = true;
    }

    return success;
}

template<typename UtValueType>
bool
HUSD_EditCustomData::setCustomData(const HUSD_FindPrims &findprims,
	const UT_StringRef &key,
	const UtValueType &value) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 stage = outdata->stage();
	TfToken		 tf_key(key.toStdString());
	VtValue		 vt_value = HUSDgetVtValue(value);

	for (auto &&path : findprims.getExpandedPathSet().sdfPathSet())
	{
	    UsdPrim	 prim(stage->GetPrimAtPath(path));

	    if (prim)
		prim.SetCustomDataByKey(tf_key, vt_value);
	}
	success = true;
    }

    return success;
}

template<typename UtValueType>
bool
HUSD_EditCustomData::setCustomData(const HUSD_FindProps &findprops,
	const UT_StringRef &key,
	const UtValueType &value) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 stage = outdata->stage();
	TfToken		 tf_key(key.toStdString());
	VtValue		 vt_value = HUSDgetVtValue(value);

	for (auto &&path : findprops.getExpandedPathSet().sdfPathSet())
	{
	    UsdObject	 obj(stage->GetObjectAtPath(path));

	    if (obj)
	    {
		UsdProperty	 property(obj.As<UsdProperty>());

		if (property)
		    property.SetCustomDataByKey(tf_key, vt_value);
	    }
	}
	success = true;
    }

    return success;
}

bool
HUSD_EditCustomData::setIconCustomData(const HUSD_FindPrims &findprims,
        const UT_StringHolder &icon)
{
    return setCustomData(findprims,
        HUSD_Constants::getIconCustomDataName(), icon);
}

bool
HUSD_EditCustomData::setIconCustomData(const HUSD_FindProps &findprops,
        const UT_StringHolder &icon)
{
    return setCustomData(findprops,
        HUSD_Constants::getIconCustomDataName(), icon);
}

bool
HUSD_EditCustomData::removeLayerCustomData(const UT_StringRef &key) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 layer = outdata->activeLayer();
	std::string	 std_key = key.toStdString();
	VtDictionary	 data = layer->GetCustomLayerData();

	data.EraseValueAtPath(std_key);
	layer->SetCustomLayerData(data);
	success = true;
    }

    return success;
}

bool
HUSD_EditCustomData::removeCustomData(const HUSD_FindPrims &findprims,
	const UT_StringRef &key) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 stage = outdata->stage();
	TfToken		 tf_key(key.toStdString());

	for (auto &&path : findprims.getExpandedPathSet().sdfPathSet())
	{
	    UsdPrim	 prim(stage->GetPrimAtPath(path));

	    if (prim)
		prim.ClearCustomDataByKey(tf_key);
	}
	success = true;
    }

    return success;
}

bool
HUSD_EditCustomData::removeCustomData(const HUSD_FindProps &findprops,
	const UT_StringRef &key) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 stage = outdata->stage();
	TfToken		 tf_key(key.toStdString());

	for (auto &&path : findprops.getExpandedPathSet().sdfPathSet())
	{
	    UsdObject	 obj(stage->GetObjectAtPath(path));

	    if (obj)
	    {
		UsdProperty	 property(obj.As<UsdProperty>());

		if (property)
		    property.ClearCustomDataByKey(tf_key);
	    }
	}
	success = true;
    }

    return success;
}

bool
HUSD_EditCustomData::clearLayerCustomData() const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 layer = outdata->activeLayer();

	layer->ClearCustomLayerData();
	success = true;
    }

    return success;
}

bool
HUSD_EditCustomData::clearCustomData(const HUSD_FindPrims &findprims) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 stage = outdata->stage();

	for (auto &&path : findprims.getExpandedPathSet().sdfPathSet())
	{
	    UsdPrim	 prim(stage->GetPrimAtPath(path));

	    if (prim)
		prim.ClearCustomData();
	}
	success = true;
    }

    return success;
}

bool
HUSD_EditCustomData::clearCustomData(const HUSD_FindProps &findprops) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 stage = outdata->stage();

	for (auto &&path : findprops.getExpandedPathSet().sdfPathSet())
	{
	    UsdObject	 obj(stage->GetObjectAtPath(path));

	    if (obj)
	    {
		UsdProperty	 property(obj.As<UsdProperty>());

		if (property)
		    property.ClearCustomData();
	    }
	}
	success = true;
    }

    return success;
}


#define HUSD_EXPLICIT_INSTANTIATION(UtType)				\
    template HUSD_API_TINST bool					\
    HUSD_EditCustomData::setLayerCustomData(				\
	const UT_StringRef	&key,					\
	const UtType		&value) const;				\
    template HUSD_API_TINST bool					\
    HUSD_EditCustomData::setCustomData(					\
	const HUSD_FindPrims	&findprims,				\
	const UT_StringRef	&key,					\
	const UtType		&value) const;				\
    template HUSD_API_TINST bool					\
    HUSD_EditCustomData::setCustomData(					\
	const HUSD_FindProps	&findprops,				\
	const UT_StringRef	&key,					\
	const UtType		&value) const;

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
HUSD_EXPLICIT_INSTANTIATION(HUSD_AssetPath)
HUSD_EXPLICIT_INSTANTIATION(HUSD_Token)

#undef HUSD_EXPLICIT_INSTANTIATION

