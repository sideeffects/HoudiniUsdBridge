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

#ifndef __HD_METACURVES_ADAPTER_H__
#define __HD_METACURVES_ADAPTER_H__

#include <pxr/pxr.h>
#include <pxr/usdImaging/usdImaging/basisCurvesAdapter.h>

PXR_NAMESPACE_OPEN_SCOPE

/// \class HD_MetaCurvesAdapter
///
/// Delegate support for HoudiniMetaCurves primitive.
///
class HD_MetaCurvesAdapter : public UsdImagingBasisCurvesAdapter
{
public:
    using BaseAdapter = UsdImagingBasisCurvesAdapter;

    HD_MetaCurvesAdapter();
    ~HD_MetaCurvesAdapter() override;

    SdfPath Populate(
        UsdPrim const& prim,
        UsdImagingIndexProxy* index,
        UsdImagingInstancerContext const* instancerContext = nullptr) override;

    bool IsSupported(UsdImagingIndexProxy const* index) const override;

    // ---------------------------------------------------------------------- //
    /// \name Parallel Setup and Resolve
    // ---------------------------------------------------------------------- //

    /// Thread Safe.
    void TrackVariability(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        HdDirtyBits* timeVaryingBits,
        UsdImagingInstancerContext const* instancerContext = nullptr) 
            const override;

    /// Thread Safe.
    void UpdateForTime(
        UsdPrim const& prim,
        SdfPath const& cachePath, 
        UsdTimeCode time,
        HdDirtyBits requestedBits,
        UsdImagingInstancerContext const* instancerContext = nullptr) 
            const override;

    // ---------------------------------------------------------------------- //
    /// \name Change Processing
    // ---------------------------------------------------------------------- //

    HdDirtyBits ProcessPropertyChange(UsdPrim const& prim,
                                      SdfPath const& cachePath,
                                      TfToken const& propertyName) override;

    // ---------------------------------------------------------------------- //
    /// \name Data access
    // ---------------------------------------------------------------------- //

    VtValue GetTopology(UsdPrim const& prim,
                        SdfPath const& cachePath,
                        UsdTimeCode time) const override;

    VtValue Get(UsdPrim const& prim,
                SdfPath const& cachePath,
                TfToken const& key,
                UsdTimeCode time,
                VtIntArray *outIndices) const override;

private:
    bool         _metaCurvesSupported;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
