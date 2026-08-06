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
#ifndef _HD_HOUDINI_FIELD_ADAPTER_H
#define _HD_HOUDINI_FIELD_ADAPTER_H

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdImaging/fieldAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

class UsdPrim;

/// \class HD_HoudiniFieldAssetAdapter
///
/// Adapter class for fields of type HoudiniFieldAsset
///
class HD_HoudiniFieldAssetAdapter : public UsdImagingFieldAdapter {
public:
    using BaseAdapter = UsdImagingFieldAdapter;

    HD_HoudiniFieldAssetAdapter()
        : UsdImagingFieldAdapter()
    {}

    ~HD_HoudiniFieldAssetAdapter() override;

    // ---------------------------------------------------------------------- //
    /// \name Scene Index Support
    // ---------------------------------------------------------------------- //

    TfTokenVector GetImagingSubprims(UsdPrim const& prim) override;

    TfToken GetImagingSubprimType(UsdPrim const& prim, TfToken const& subprim)
        override;

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
    /// \name Data access
    // ---------------------------------------------------------------------- //

    VtValue Get(UsdPrim const& prim,
                SdfPath const& cachePath,
                TfToken const& key,
                UsdTimeCode time,
                VtIntArray *outIndices) const override;

    TfToken GetPrimTypeToken() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // _HD_HOUDINI_FIELD_ADAPTER_H
