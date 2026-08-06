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
#ifndef USDHIMAGING_KARMA_ATMOSPHERIC_SKY_ADAPTER_H
#define USDHIMAGING_KARMA_ATMOSPHERIC_SKY_ADAPTER_H

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdImaging/api.h"
#include "pxr/usdImaging/usdImaging/primAdapter.h"
#include "pxr/usdImaging/usdImaging/gprimAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdHImagingKarmaAtmosphereAdapter
///
/// Adapter class for KarmaSkyAtmosphere primitive
/// (treated as a volume type without any fields)
///
class HD_KarmaAtmosphereAdapter : public UsdImagingGprimAdapter {
public:
    typedef UsdImagingGprimAdapter BaseAdapter;

    HD_KarmaAtmosphereAdapter()
        : UsdImagingGprimAdapter()
    {}
    ~HD_KarmaAtmosphereAdapter() override;

    // ---------------------------------------------------------------------- //
    /// \name Scene Index Support
    // ---------------------------------------------------------------------- //
  
    TfTokenVector GetImagingSubprims(UsdPrim const& prim) override;

    TfToken GetImagingSubprimType(
            UsdPrim const& prim,
            TfToken const& subprim) override;

    HdContainerDataSourceHandle GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals) override;

    HdDataSourceLocatorSet InvalidateImagingSubprim(
          UsdPrim const& prim,
          TfToken const& subprim,
          TfTokenVector const& properties,
          UsdImagingPropertyInvalidationType invalidationType) override;

    // ---------------------------------------------------------------------- //
    /// \name Initialization
    // ---------------------------------------------------------------------- //

    SdfPath Populate(UsdPrim const& prim,
                     UsdImagingIndexProxy* index,
                     UsdImagingInstancerContext const*
                     instancerContext = NULL) override;

    bool IsSupported(UsdImagingIndexProxy const* index) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // USDHIMAGING_KARMA_ATMOSPHERIC_SKY_ADAPTER_H
