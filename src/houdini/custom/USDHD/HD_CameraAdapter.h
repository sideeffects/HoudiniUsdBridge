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

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdImaging/api.h"
#include "pxr/usdImaging/usdImaging/cameraAdapter.h"

//#define USE_HDCAMERAADAPTER_GETMATERIALRESOURCE

PXR_NAMESPACE_OPEN_SCOPE

class UsdPrim;

class HD_CameraAdapter : public UsdImagingCameraAdapter
{
public:
    using BaseAdapter = UsdImagingCameraAdapter;

    HD_CameraAdapter() : UsdImagingCameraAdapter() {};

    ~HD_CameraAdapter() override = default;

    SdfPath Populate(
            UsdPrim const& prim,
            UsdImagingIndexProxy* index,
            UsdImagingInstancerContext const* instancerContext = NULL) override;

    void MarkMaterialDirty(
            UsdPrim const& prim,
            SdfPath const& cachePath,
            UsdImagingIndexProxy* index) override;
    
    SdfPath GetMaterialId(
            UsdPrim const& prim,
            SdfPath const& cachePath,
            UsdTimeCode time) const override;

#ifdef USE_HDCAMERAADAPTER_GETMATERIALRESOURCE
    VtValue GetMaterialResource(
            UsdPrim const& prim,
            SdfPath const& cachePath,
            UsdTimeCode time) const override;
#endif
};

PXR_NAMESPACE_CLOSE_SCOPE
