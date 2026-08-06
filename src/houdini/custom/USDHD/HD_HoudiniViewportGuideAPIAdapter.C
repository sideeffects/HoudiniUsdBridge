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

#include "pxr/pxr.h"
#include "pxr/imaging/hd/cameraSchema.h"
#include "pxr/imaging/hd/lightSchema.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdLux/lightAPI.h"
#include "pxr/usdImaging/usdImaging/apiSchemaAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

class HD_HoudiniViewportGuideAPIAdapter : public UsdImagingAPISchemaAdapter
{
public:
    using BaseAdapter = UsdImagingAPISchemaAdapter;

    HdDataSourceLocatorSet InvalidateImagingSubprim(
            UsdPrim const& prim,
            TfToken const& subprim,
            TfToken const& appliedInstanceName,
            TfTokenVector const& properties,
            UsdImagingPropertyInvalidationType invalidationType) override;
};

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HD_HoudiniViewportGuideAPIAdapter)
{
    using Adapter = HD_HoudiniViewportGuideAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

HdDataSourceLocatorSet
HD_HoudiniViewportGuideAPIAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& appliedInstanceName,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType)
{
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty())
        return HdDataSourceLocatorSet();

    const TfToken schema("HoudiniViewportGuideAPI");

    const UsdPrimDefinition* const primDef
            = UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(
                    schema);
    if (!primDef)
    {
        TF_CODING_ERROR(
                "Could not find definition for applied schema '%s'.",
                schema.GetText());
        return HdDataSourceLocatorSet();
    }

    HdDataSourceLocatorSet locators;
    const TfTokenVector& schemaProps = primDef->GetPropertyNames();
    for (const TfToken& dirtyProp : properties)
    {
        if (std::find(schemaProps.begin(), schemaProps.end(), dirtyProp)
            != schemaProps.end())
        {
            // Note we need to handle each relevant prim type independently
            // to ensure the right dirty signals are generated. For lights
            // and cameras this means dirtying `light` or `camera`.
            // Right now we don't have a generic fallback for other types.
            if (prim.HasAPI<UsdLuxLightAPI>())
                locators.insert(HdLightSchema::GetDefaultLocator());
            else if (UsdGeomCamera(prim))
                locators.insert(HdCameraSchema::GetDefaultLocator());
            break;
        }
    }

    return locators;
}

PXR_NAMESPACE_CLOSE_SCOPE
