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

#include "HD_HoudiniApexSceneAdapter.h"

#include <HUSD/UsdHoudini/houdiniApexScene.h>
#include <HUSD/XUSD_Tokens.h>

#include "pxr/usdImaging/usdImaging/dataSourceMapped.h"
#include "pxr/usdImaging/usdImaging/dataSourcePrim.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, USD_HD_HoudiniApexSceneAdapter)
{
    using Adapter = HD_HoudiniApexSceneAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

namespace
{
HdDataSourceLocator
hd_GetDefaultLocator()
{
    static const HdDataSourceLocator theLocator(
            HusdHdApexTokens->houdiniApexScene);
    return theLocator;
}

/// Map the HoudiniApexScene attributes to data sources with the same names.
std::vector<UsdImagingDataSourceMapped::PropertyMapping>
hd_GetPropertyMappings()
{
    std::vector<UsdImagingDataSourceMapped::PropertyMapping> result;

    for (const TfToken& usd_name :
         UsdHoudiniHoudiniApexScene::GetSchemaAttributeNames(
                 /*includeInherited=*/false))
    {
        UsdImagingDataSourceMapped::AttributeMapping mapping;
        mapping.usdName = usd_name;
        mapping.hdLocator = HdDataSourceLocator(usd_name);
        result.push_back(mapping);
    }

    return result;
}

const UsdImagingDataSourceMapped::PropertyMappings&
hd_GetMappings()
{
    static const UsdImagingDataSourceMapped::PropertyMappings theResult(
            hd_GetPropertyMappings(), hd_GetDefaultLocator());
    return theResult;
}

/// Data source for HoudiniApexScene primitives. 
class hd_HoudiniApexSceneDataSource : public UsdImagingDataSourcePrim
{
public:
    HD_DECLARE_DATASOURCE(hd_HoudiniApexSceneDataSource)

    TfTokenVector GetNames() override
    {
        TfTokenVector names = UsdImagingDataSourcePrim::GetNames();
        // We add a 'houdiniApexScene' container for data specific to the
        // HoudiniApexScene primitive.
        names.push_back(HusdHdApexTokens->houdiniApexScene);
        return names;
    }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HusdHdApexTokens->houdiniApexScene)
        {
            return UsdImagingDataSourceMapped::New(
                    _GetUsdPrim(), _GetSceneIndexPath(), hd_GetMappings(),
                    _GetStageGlobals());
        }

        return UsdImagingDataSourcePrim::Get(name);
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

        locators.insert(
                UsdImagingDataSourcePrim::Invalidate(
                        prim, subprim, properties, invalidation_type));
        return locators;
    }

private:
    hd_HoudiniApexSceneDataSource(
            const SdfPath& scene_index_path,
            const UsdPrim& usd_prim,
            const UsdImagingDataSourceStageGlobals& stage_globals)
        : UsdImagingDataSourcePrim(scene_index_path, usd_prim, stage_globals)
    {
    }
};

HD_DECLARE_DATASOURCE_HANDLES(hd_HoudiniApexSceneDataSource)
} // namespace

HD_HoudiniApexSceneAdapter::HD_HoudiniApexSceneAdapter() = default;

HD_HoudiniApexSceneAdapter::~HD_HoudiniApexSceneAdapter() = default;

TfTokenVector
HD_HoudiniApexSceneAdapter::GetImagingSubprims(UsdPrim const& prim)
{
    // We don't define any additional sub-prims.
    return { TfToken() };
}

TfToken
HD_HoudiniApexSceneAdapter::GetImagingSubprimType(
        UsdPrim const& prim,
        TfToken const& subprim)
{
    if (!subprim.IsEmpty())
        return TfToken();

    // We don't define any additional sub-prims. The primary primitve has
    // type 'houdiniApexScene'.
    return HusdHdApexTokens->houdiniApexScene;
}

HdContainerDataSourceHandle
HD_HoudiniApexSceneAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals& stage_globals)
{
    if (!subprim.IsEmpty())
        return nullptr;

    return hd_HoudiniApexSceneDataSource::New(
            prim.GetPath(), prim, stage_globals);
}

HdDataSourceLocatorSet
HD_HoudiniApexSceneAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidation_type)
{
    if (!subprim.IsEmpty())
        return HdDataSourceLocatorSet();

    return hd_HoudiniApexSceneDataSource::Invalidate(
            prim, subprim, properties, invalidation_type);
}

PXR_NAMESPACE_CLOSE_SCOPE
