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

#include "HD_HoudiniApexShapeBindingAPIAdapter.h"

#include <HUSD/UsdHoudini/houdiniApexShapeBindingAPI.h>
#include <HUSD/UsdHoudini/tokens.h>
#include <HUSD/XUSD_Tokens.h>

#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/usdImaging/usdImaging/dataSourceMapped.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION_WITH_TAG(
        TfType,
        USD_HD_HoudiniApexShapeBindingAPIAdapter)
{
    using Adapter = HD_HoudiniApexShapeBindingAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

namespace
{
/// Map the HoudiniApexShapeBindingAPI attributes to Hydra data sources.
static std::vector<UsdImagingDataSourceMapped::PropertyMapping>
hd_GetPropertyMappings(const TfToken &instance_name)
{
    return
    {
        UsdImagingDataSourceMapped::AttributeMapping
        {
            {
                UsdSchemaRegistry::MakeMultipleApplyNameInstance(
                    UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Input,
                    instance_name),
                HdDataSourceLocator(HusdHdApexTokens->input)
            }
        },
        UsdImagingDataSourceMapped::AttributeMapping
        {
            {
                UsdSchemaRegistry::MakeMultipleApplyNameInstance(
                    UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Output,
                    instance_name),
                HdDataSourceLocator(HusdHdApexTokens->output)
            }
        },
        UsdImagingDataSourceMapped::RelationshipMapping
        {
            {
                UsdSchemaRegistry::MakeMultipleApplyNameInstance(
                    UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Binding,
                    instance_name),
                HdDataSourceLocator(HusdHdApexTokens->binding)
            },
            UsdImagingDataSourceMapped::GetPathFromRelationshipDataSourceFactory()
        },
    };
}

static UsdImagingDataSourceMapped::PropertyMappings
hd_GetMappings(const TfToken &instance_name)
{
    return UsdImagingDataSourceMapped::PropertyMappings(
            hd_GetPropertyMappings(instance_name),
            HdDataSourceLocator(instance_name));
}

/// Data source for the list of shape bindings.
/// This has one child container named after the API schema's instance name,
/// e.g. 'skin', which in turn contains the schema's attributes.
class hd_BindingsContainerDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(hd_BindingsContainerDataSource);

    hd_BindingsContainerDataSource(
            const UsdPrim& prim,
            const TfToken& name,
            const SdfPath& scene_index_path,
            const UsdImagingDataSourceStageGlobals& stage_globals)
        : myAPI(prim, name)
        , mySceneIndexPath(scene_index_path)
        , myStageGlobals(stage_globals)
    {
    }

    TfTokenVector GetNames() override { return { myAPI.GetName() }; }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == myAPI.GetName())
        {
            return UsdImagingDataSourceMapped::New(
                    myAPI.GetPrim(), mySceneIndexPath,
                    hd_GetMappings(myAPI.GetName()), myStageGlobals);
        }

        return nullptr;
    }

private:
    UsdHoudiniHoudiniApexShapeBindingAPI myAPI;
    SdfPath mySceneIndexPath;
    const UsdImagingDataSourceStageGlobals& myStageGlobals;
};

HD_DECLARE_DATASOURCE_HANDLES(hd_BindingsContainerDataSource);
} // namespace

HD_HoudiniApexShapeBindingAPIAdapter::HD_HoudiniApexShapeBindingAPIAdapter() = default;

HD_HoudiniApexShapeBindingAPIAdapter::~HD_HoudiniApexShapeBindingAPIAdapter() = default;

HdContainerDataSourceHandle
HD_HoudiniApexShapeBindingAPIAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& applied_instance_name,
        const UsdImagingDataSourceStageGlobals& stage_globals)
{
    if (!subprim.IsEmpty() || applied_instance_name.IsEmpty())
        return nullptr;

    // Provide a container for each applied instance,
    // e.g. 'houdiniApexShapeBindings/skin'
    // When there are multiple applied instances, the containers are overlayed.
    return HdRetainedContainerDataSource::New(
            HusdHdApexTokens->houdiniApexShapeBindings,
            hd_BindingsContainerDataSource::New(
                    prim, applied_instance_name, prim.GetPath(),
                    stage_globals));
}

HdDataSourceLocatorSet
HD_HoudiniApexShapeBindingAPIAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& applied_instance_name,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidation_type)
{
    if (!subprim.IsEmpty() || applied_instance_name.IsEmpty())
        return HdDataSourceLocatorSet();

    // We need to check whether the invalidated properties are relevant to this
    // instance of the API schema. e.g 'houdini:apex:shape:skin:*'
    std::string prefix = TfStringPrintf(
            "%s:%s:", UsdHoudiniTokens->houdiniApexShape.data(),
            applied_instance_name.data());

    for (const TfToken& property_name : properties)
    {
        if (TfStringStartsWith(property_name.GetString(), prefix))
        {
            return HdDataSourceLocator(
                    HusdHdApexTokens->houdiniApexShapeBindings,
                    applied_instance_name);
        }
    }

    return HdDataSourceLocatorSet();
}

PXR_NAMESPACE_CLOSE_SCOPE
