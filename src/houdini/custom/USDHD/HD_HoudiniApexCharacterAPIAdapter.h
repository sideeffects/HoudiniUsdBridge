/*
* Copyright 2025 Side Effects Software Inc.
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

#pragma once

#include "pxr/pxr.h"

#include "pxr/base/tf/token.h"
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/usdImaging/usdImaging/apiSchemaAdapter.h"

#include <UT/UT_NonCopyable.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Hydra prim adapter for HoudiniApexCharacterAPI.
class HD_HoudiniApexCharacterAPIAdapter : public UsdImagingAPISchemaAdapter
{
public:
    using BaseAdapter = UsdImagingAPISchemaAdapter;

    HD_HoudiniApexCharacterAPIAdapter();
    ~HD_HoudiniApexCharacterAPIAdapter() override;

    UT_NON_COPYABLE(HD_HoudiniApexCharacterAPIAdapter)

    HdContainerDataSourceHandle GetImagingSubprimData(
            UsdPrim const& prim,
            TfToken const& subprim,
            TfToken const& applied_instance_name,
            const UsdImagingDataSourceStageGlobals &stage_globals) override;

    HdDataSourceLocatorSet InvalidateImagingSubprim(
            UsdPrim const& prim,
            TfToken const& subprim,
            TfToken const& applied_instance_name,
            TfTokenVector const& properties,
            UsdImagingPropertyInvalidationType invalidation_type) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
