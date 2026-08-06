//
// Copyright 2018 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "HD_KarmaAtmosphereAdapter.h"

#include <HUSD/XUSD_Tokens.h>
#include "pxr/usdImaging/usdImaging/dataSourceGprim.h"
#include "pxr/usdImaging/usdImaging/delegate.h"
#include "pxr/usdImaging/usdImaging/indexProxy.h"
#include "pxr/usdImaging/usdImaging/tokens.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, USD_HD_KarmaAtmosphereAdapter)
{
    typedef HD_KarmaAtmosphereAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
}

HD_KarmaAtmosphereAdapter::~HD_KarmaAtmosphereAdapter()
{
}

TfTokenVector
HD_KarmaAtmosphereAdapter::GetImagingSubprims(UsdPrim const& prim)
{
    return { TfToken() };
}

TfToken
HD_KarmaAtmosphereAdapter::GetImagingSubprimType(
        UsdPrim const& prim,
        TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
        return HusdHdPrimTypeTokens->karmaSkyAtmosphere;
    }
    return TfToken();
}

HdContainerDataSourceHandle
HD_KarmaAtmosphereAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
{
    if (subprim.IsEmpty()) {
        return UsdImagingDataSourceGprim::New(
            prim.GetPath(),
            prim,
            stageGlobals);
    }
    return nullptr;
}

HdDataSourceLocatorSet
HD_KarmaAtmosphereAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType)
{
    if (subprim.IsEmpty()) {
        return UsdImagingDataSourceGprim::Invalidate(
            prim, subprim,properties, invalidationType);
    }
    
    return HdDataSourceLocatorSet();
}

SdfPath
HD_KarmaAtmosphereAdapter::Populate(UsdPrim const& prim,
                            UsdImagingIndexProxy* index,
                            UsdImagingInstancerContext const* instancerContext)
{
    return  _AddRprim(HusdHdPrimTypeTokens->karmaSkyAtmosphere,
        prim, index, GetMaterialUsdPath(prim), instancerContext);
}

bool
HD_KarmaAtmosphereAdapter::IsSupported(UsdImagingIndexProxy const* index) const
{
    return index->IsRprimTypeSupported(HusdHdPrimTypeTokens->karmaSkyAtmosphere);
}

PXR_NAMESPACE_CLOSE_SCOPE

