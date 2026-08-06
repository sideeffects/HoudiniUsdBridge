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

#include "pxr/imaging/hd/lightSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/pxr.h"
#include "pxr/usdImaging/usdImaging/apiSchemaAdapter.h"
#include "pxr/usdImaging/usdImaging/dataSourceMapped.h"

#include <HUSD/UsdHoudini/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

class HD_HoudiniLightBarnDoorAPIAdapter : public UsdImagingAPISchemaAdapter
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

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HD_HoudiniLightBarnDoorAPIAdapter)
{
    using Adapter = HD_HoudiniLightBarnDoorAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

HdDataSourceLocatorSet
HD_HoudiniLightBarnDoorAPIAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& appliedInstanceName,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType)
{
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty())
        return HdDataSourceLocatorSet();

    const TfToken schema("HoudiniLightBarnDoorAPI");

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
            locators.insert(HdLightSchema::GetDefaultLocator());
            break;
        }
    }

    return locators;
}

PXR_NAMESPACE_CLOSE_SCOPE
