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

#include "HD_MetaCurvesAdapter.h"
#include <HUSD/XUSD_Tokens.h>

#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImaging/indexProxy.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#include <pxr/imaging/hd/basisCurves.h>
#include <pxr/imaging/hd/perfLog.h>

#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xformCache.h>

#include <pxr/base/tf/type.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, USD_HD_MetaCurvesAdapter)
{
    typedef HD_MetaCurvesAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
}

HD_MetaCurvesAdapter::HD_MetaCurvesAdapter()
    : UsdImagingBasisCurvesAdapter(),
      _metaCurvesSupported(false)
{
}

HD_MetaCurvesAdapter::~HD_MetaCurvesAdapter()
{
}

bool
HD_MetaCurvesAdapter::IsSupported(
        UsdImagingIndexProxy const* index) const
{
    if (index->IsRprimTypeSupported(HusdHdPrimTypeTokens->metaCurves))
        return true;

    return BaseAdapter::IsSupported(index);
}

SdfPath
HD_MetaCurvesAdapter::Populate(UsdPrim const& prim,
        UsdImagingIndexProxy* index,
        UsdImagingInstancerContext const* instancerContext)
{
    if (index->IsRprimTypeSupported(HusdHdPrimTypeTokens->metaCurves))
    {
        _metaCurvesSupported = true;
        return _AddRprim(HusdHdPrimTypeTokens->metaCurves,
            prim, index, GetMaterialUsdPath(prim), instancerContext);
    }

    return _AddRprim(HdPrimTypeTokens->basisCurves,
        prim, index, GetMaterialUsdPath(prim), instancerContext);
}

void
HD_MetaCurvesAdapter::TrackVariability(UsdPrim const& prim,
        SdfPath const& cachePath,
        HdDirtyBits* timeVaryingBits,
        UsdImagingInstancerContext const* instancerContext) const
{
    BaseAdapter::TrackVariability(
        prim, cachePath, timeVaryingBits, instancerContext);
}

void
HD_MetaCurvesAdapter::UpdateForTime(
    UsdPrim const& prim,
    SdfPath const& cachePath, 
    UsdTimeCode time,
    HdDirtyBits requestedBits,
    UsdImagingInstancerContext const* instancerContext) const
{
    BaseAdapter::UpdateForTime(
        prim, cachePath, time, requestedBits, instancerContext);
}

HdDirtyBits
HD_MetaCurvesAdapter::ProcessPropertyChange(UsdPrim const& prim,
        SdfPath const& cachePath,
        TfToken const& propertyName)
{
    return BaseAdapter::ProcessPropertyChange(prim, cachePath, propertyName);
}

VtValue
HD_MetaCurvesAdapter::GetTopology(UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdTimeCode time) const
{
    return BaseAdapter::GetTopology(prim, cachePath, time);
}

VtValue
HD_MetaCurvesAdapter::Get(UsdPrim const& prim,
        SdfPath const& cachePath,
        TfToken const& key,
        UsdTimeCode time,
        VtIntArray *outIndices) const
{
    if (key == "topology")
        return GetTopology(prim, cachePath, time);
    return BaseAdapter::Get(prim, cachePath, key, time, outIndices);
}

PXR_NAMESPACE_CLOSE_SCOPE
