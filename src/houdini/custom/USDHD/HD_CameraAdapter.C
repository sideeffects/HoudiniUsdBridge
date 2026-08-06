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

#include "HD_CameraAdapter.h"

#ifdef USE_HDCAMERAADAPTER_GETMATERIALRESOURCE
#include "pxr/imaging/hd/material.h"
#include "pxr/usd/ar/resolverContextBinder.h"
#include "pxr/usd/ar/resolverScopedCache.h"
#include "pxr/usdImaging/usdImaging/materialParamUtils.h"
#endif

#include "pxr/usdImaging/usdImaging/indexProxy.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, USD_HD_CameraAdapter)
{
    using Adapter = HD_CameraAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

SdfPath
HD_CameraAdapter::Populate(
        UsdPrim const& prim,
        UsdImagingIndexProxy* index,
        UsdImagingInstancerContext const* instancerContext)
{
    UsdImagingCameraAdapter::Populate(prim, index, instancerContext);
    
    // ************************************************************************
    // See UsdImagingGprimAdapter::_AddRprim
    //
    SdfPath materialUsdPath = GetMaterialUsdPath(prim);
    UsdPrim materialPrim = prim.GetStage()->GetPrimAtPath(materialUsdPath);
    if (materialPrim)
    {
        if (materialPrim.IsA<UsdShadeMaterial>())
        {
            UsdImagingPrimAdapterSharedPtr materialAdapter
                    = index->GetMaterialAdapter(materialPrim);
            if (materialAdapter)
            {
                materialAdapter->Populate(materialPrim, index, nullptr);
            }
        }
        else
        {
            TF_WARN("Prim <%s> has illegal material reference to "
                    "prim <%s> of type (%s)",
                    prim.GetPath().GetText(), materialPrim.GetPath().GetText(),
                    materialPrim.GetTypeName().GetText());
        }
    }
    if (!materialUsdPath.IsEmpty())
        index->AddDependency(prim.GetPath(), _GetPrim(materialUsdPath));
    //
    // ************************************************************************

    return prim.GetPath();
}

void
HD_CameraAdapter::MarkMaterialDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index)
{
    // ************************************************************************
    // UsdImagingGprimAdapter::MarkMaterialDirty
    //
    index->MarkSprimDirty(cachePath, HdChangeTracker::DirtyMaterialId);
    index->RequestUpdateForTime(cachePath);
    //
    // ************************************************************************
}

SdfPath
HD_CameraAdapter::GetMaterialId(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdTimeCode time) const
{
    return GetMaterialUsdPath(prim);
}

#ifdef USE_HDCAMERAADAPTER_GETMATERIALRESOURCE
VtValue
HD_CameraAdapter::GetMaterialResource(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdTimeCode time) const
{
    ArResolverContextBinder binder(prim.GetStage()->GetPathResolverContext());
    ArResolverScopedCache resolverCache;

    HdMaterialNetworkMap networkMap;

    SdfPath materialUsdPath = GetMaterialUsdPath(prim);
    UsdPrim materialPrim = prim.GetStage()->GetPrimAtPath(materialUsdPath);

    // ************************************************************************
    // Largely lifted from UsdImagingMaterialAdapter::GetMaterialResource

    UsdShadeMaterial material(materialPrim);
    if (!material)
    {
        TF_RUNTIME_ERROR(
                "Expected material prim at <%s> to be of type "
                "'UsdShadeMaterial', not type '%s'; ignoring",
                materialPrim.GetPath().GetText(),
                materialPrim.GetTypeName().GetText());
        return VtValue();
    }

    const TfTokenVector contextVector = _GetMaterialRenderContexts();
    if (UsdShadeShader surface = material.ComputeSurfaceSource(contextVector))
    {
        UsdImagingBuildHdMaterialNetworkFromTerminal(
                materialPrim, HdMaterialTerminalTokens->surface,
                _GetShaderSourceTypes(), contextVector, &networkMap, time);
    }

    // ************************************************************************
    
    return VtValue(networkMap);
}
#endif

PXR_NAMESPACE_CLOSE_SCOPE
