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
#include "HUSD_FindProps.h"
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

    return husdConfigProps<UsdAttribute>(myWriteLock, findprops,
	[&](UsdAttribute &attrib)
	{
	    UsdGeomPrimvar	 primvar(attrib);

	    if (!primvar)
		return false;

	    return primvar.SetInterpolation(tf_interpolation);
	});
}

bool
HUSD_ConfigureProps::setElementSize(const HUSD_FindProps &findprops,
	int element_size) const
{
    return husdConfigProps<UsdAttribute>(myWriteLock, findprops,
	[&](UsdAttribute &attrib)
	{
	    UsdGeomPrimvar	 primvar(attrib);

	    if (!primvar)
		return false;

	    return primvar.SetElementSize(element_size);
	});
}

