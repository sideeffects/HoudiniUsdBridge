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

#include "HD_HoudiniApexCharacterAPIAdapter.h"

#include <HUSD/UsdHoudini/houdiniApexCharacterAPI.h>
#include <HUSD/XUSD_Tokens.h>

#include "pxr/usdImaging/usdImaging/dataSourceMapped.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, USD_HD_HoudiniApexCharacterAPIAdapter)
{
    using Adapter = HD_HoudiniApexCharacterAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

namespace
{
HdDataSourceLocator
hd_GetDefaultLocator()
{
    static const HdDataSourceLocator theLocator(
            HusdHdApexTokens->houdiniApexCharacter);
    return theLocator;
}

/// Map the HoudiniApexCharacterAPI attributes to Hydra data sources.
std::vector<UsdImagingDataSourceMapped::PropertyMapping>
hd_GetPropertyMappings()
{
    return
    {
        UsdImagingDataSourceMapped::AttributeMapping
        {
            {
                UsdHoudiniTokens->houdiniApexCharacterFiles,
                HdDataSourceLocator(HusdHdApexTokens->files)
            }
        },
        UsdImagingDataSourceMapped::AttributeMapping
        {
            {
                UsdHoudiniTokens->houdiniApexCharacterRig,
                HdDataSourceLocator(HusdHdApexTokens->rig)
            }
        },
    };
}

const UsdImagingDataSourceMapped::PropertyMappings&
hd_GetMappings()
{
    static const UsdImagingDataSourceMapped::PropertyMappings theResult(
            hd_GetPropertyMappings(), hd_GetDefaultLocator());
    return theResult;
}

/// Data source for HoudiniApexCharacterAPI. 
class hd_HoudiniApexCharacterAPIDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(hd_HoudiniApexCharacterAPIDataSource)

    TfTokenVector GetNames() override
    {
        // We add a 'houdiniApexCharacter' container for data specific to the
        // HoudiniApexCharacterAPI applied to the prim.
        static const TfTokenVector theResult
                = {HusdHdApexTokens->houdiniApexCharacter};
        return theResult;
    }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HusdHdApexTokens->houdiniApexCharacter)
        {
            return UsdImagingDataSourceMapped::New(
                    myUsdPrim, mySceneIndexPath, hd_GetMappings(),
                    myStageGlobals);
        }

        return nullptr;
    }

    static HdDataSourceLocatorSet Invalidate(
            UsdPrim const& prim,
            const TfToken& subprim,
            const TfTokenVector& properties,
            UsdImagingPropertyInvalidationType invalidation_type)
    {
        HdDataSourceLocatorSet locators
                = UsdImagingDataSourceMapped::Invalidate(
                        properties, hd_GetMappings());
        return locators;
    }

private:
    hd_HoudiniApexCharacterAPIDataSource(
            const SdfPath& scene_index_path,
            const UsdPrim& usd_prim,
            const UsdImagingDataSourceStageGlobals& stage_globals)
        : mySceneIndexPath(scene_index_path)
        , myUsdPrim(usd_prim)
        , myStageGlobals(stage_globals)
    {
    }

    const SdfPath mySceneIndexPath;
    const UsdPrim myUsdPrim;
    const UsdImagingDataSourceStageGlobals &myStageGlobals;
};

HD_DECLARE_DATASOURCE_HANDLES(hd_HoudiniApexCharacterAPIDataSource)
} // namespace

HD_HoudiniApexCharacterAPIAdapter::HD_HoudiniApexCharacterAPIAdapter() = default;

HD_HoudiniApexCharacterAPIAdapter::~HD_HoudiniApexCharacterAPIAdapter() = default;

HdContainerDataSourceHandle
HD_HoudiniApexCharacterAPIAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& applied_instance_name,
        const UsdImagingDataSourceStageGlobals& stage_globals)
{
    if (!subprim.IsEmpty() || !applied_instance_name.IsEmpty())
        return nullptr;

    return hd_HoudiniApexCharacterAPIDataSource::New(
            prim.GetPath(), prim, stage_globals);
}

HdDataSourceLocatorSet
HD_HoudiniApexCharacterAPIAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& applied_instance_name,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidation_type)
{
    if (!subprim.IsEmpty() || !applied_instance_name.IsEmpty())
        return HdDataSourceLocatorSet();

    return hd_HoudiniApexCharacterAPIDataSource::Invalidate(
            prim, subprim, properties, invalidation_type);
}

PXR_NAMESPACE_CLOSE_SCOPE
