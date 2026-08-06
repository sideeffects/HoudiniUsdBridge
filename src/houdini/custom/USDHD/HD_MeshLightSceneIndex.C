// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "HD_MeshLightSceneIndex.h"
//#include <BRAY/BRAY_Interface.h>

#include "pxr/imaging/hd/dataSourceMaterialNetworkInterface.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/base/tf/smallVector.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usdLux/meshLightAPI.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/imaging/hd/instancedBySchema.h"
#include "pxr/imaging/hd/collectionsSchema.h"
#include "pxr/imaging/hd/dependenciesSchema.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/lightSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/materialBindingsSchema.h"
#include "pxr/imaging/hd/materialNetworkSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/volumeFieldBindingSchema.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/imaging/hd/categoriesSchema.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    // materialSyncMode
    (materialGlowTintsLight)
    (noMaterialResponse)
    (independent)

    (usdCollections)

    // synthesized prim names
    ((meshLightLightName, "meshLight_light"))
    ((meshLightMeshName, "meshLight_mesh"))
    ((meshLight, "meshLight"))
    ((meshLightName, "__geolight_name"))
    ((light_emission, "__geolight_emission"))
    ((light_material_sync, "__geolight_material_sync"))

    // render context / material network selector
    ((renderContext, "kma"))

    // material network tokens
    ((meshLpeTag, "karma:object:lpetag"))
    ((lightLpeTag, "karma:light:lpetag"))
    ((samplingQuality, "karma:light:samplingquality"))
    ((treatAsLightSource, "karma:object:treat_as_lightsource"))

    // dependency tokens
    (meshLight_dep_instancedBy)
    (meshLight_dep_light)
    (meshLight_dep_material)
    (meshLight_dep_material_boundMaterial)
    (meshLight_dep_material_materialBinding)
    (meshLight_dep_materialBinding)
    (meshLight_dep_mesh)
    (meshLight_dep_primvars)
    (meshLight_dep_usdCollections)
    (meshLight_dep_visibility)
    (meshLight_dep_volumeFieldBinding)
    (meshLight_dep_xform)

);

// must match BRAY_GeoLightMaterialSyncMode enum in BRAY_Types.h
enum GeoLightMaterialSyncMode
{
    MaterialGlowTintsLight = 0,
    Independent,
    NoMaterialResponse
};

// must match BRAY_TreatAsLightSourceOpts::TALS_YES_SPRIM in BRAY_Types.h
constexpr int TALS_YES_SPRIM = 3;

namespace
{

bool
_IsMeshLight(const SdfPath &primPath, const HdSceneIndexPrim& prim)
{
    auto lightSchema = HdLightSchema::GetFromParent(prim.dataSource);
    if (lightSchema )
    {
        auto dataSource =
            HdBoolDataSource::Cast(lightSchema.GetContainer()->Get(HdTokens->isLight));
        if (dataSource)
            return dataSource->GetTypedValue(0.0f);
    }
    return false;
}

bool
_IsSupportedGeometryLightSource(const TfToken &primType)
{
    return primType == HdPrimTypeTokens->mesh ||
           primType == HdPrimTypeTokens->points ||
           primType == HdPrimTypeTokens->basisCurves ||
           primType == HdPrimTypeTokens->volume ||
           primType == HdPrimTypeTokens->sphere ||
           primType == HdPrimTypeTokens->cube ||
           primType == HdPrimTypeTokens->cone ||
           primType == HdPrimTypeTokens->cylinder ||
           primType == HdPrimTypeTokens->capsule ||
           primType == HdPrimTypeTokens->plane;
}

TfToken
_GetMaterialSyncMode(const HdContainerDataSourceHandle& primDs)
{
    const static TfToken defaultMaterialSyncMode = UsdLuxTokens->materialGlowTintsLight;

    auto lightSchema = HdLightSchema::GetFromParent(primDs);
    if (lightSchema)
    {
        auto dataSource = HdTokenDataSource::Cast(
            lightSchema.GetContainer()->Get(HdTokens->materialSyncMode));
        if (dataSource)
        {
            const TfToken materialSyncMode = dataSource->GetTypedValue(0.0f);
            return materialSyncMode.IsEmpty() ? defaultMaterialSyncMode : materialSyncMode;
        }
    }

    return defaultMaterialSyncMode;
}

SdfPath
_GetBoundMaterialPath(
    const HdContainerDataSourceHandle& primDS)
{
    HdMaterialBindingsSchema materialBindings =
        HdMaterialBindingsSchema::GetFromParent(primDS);
    HdMaterialBindingSchema materialBinding =
        materialBindings.GetMaterialBinding();
    if (HdPathDataSourceHandle const ds = materialBinding.GetPath())
        return ds->GetTypedValue(0.0f);
    return SdfPath();
}

bool
_HasValidMaterialNetwork(
    const HdSceneIndexPrim& prim,
    const HdSceneIndexBaseRefPtr& inputSceneIndex)
{
    const TfToken terminalToken =
        prim.primType == HdPrimTypeTokens->volume
          ? HdMaterialTerminalTokens->volume
          : HdMaterialTerminalTokens->surface;

    HdMaterialNetworkSchema netSchema =
        HdMaterialSchema::GetFromParent(prim.dataSource)
            .GetMaterialNetwork(TfTokenVector({_tokens->renderContext}));
    return netSchema.GetNodes() && netSchema.GetTerminals();
}

SdfPathVector
_GetLightFilterPaths(const HdContainerDataSourceHandle &inputContainer)
{
    if (HdLightSchema lightSchema =
            HdLightSchema::GetFromParent(inputContainer))
    {
        auto dataSource = HdTypedSampledDataSource<SdfPathVector>::Cast(
            lightSchema.GetContainer()->Get(HdTokens->filters));
        if (dataSource)
            return dataSource->GetTypedValue(0.0f);
    }
    return SdfPathVector();
}

HdContainerDataSourceHandle
_BuildLightDependenciesDataSource(
    const SdfPath& originPath,
    const HdContainerDataSourceHandle& originDS,
    const SdfPath& bindingSourcePath,
    const HdContainerDataSourceHandle& bindingSourceDS)
{
    // XXX: As with _BuildLightShaderDataSource above, bindingSource will
    // ordinarily be the same as origin, except in the (not yet supported)
    // case of geom subsets.

    // Data source locators

    // light
    static const HdLocatorDataSourceHandle lightDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdLightSchema::GetDefaultLocator());

    // light.filters
    static const HdLocatorDataSourceHandle lightFiltersDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdLightSchema::GetDefaultLocator().Append(HdTokens->filters));

    // material
    static const HdLocatorDataSourceHandle materialDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdMaterialSchema::GetDefaultLocator());

    // material binding
    static const HdLocatorDataSourceHandle materialBindingsDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdMaterialBindingsSchema::GetDefaultLocator());

    // usdCollections
    static const HdLocatorDataSourceHandle usdCollectionsDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdDataSourceLocator(_tokens->usdCollections));

    // visibility
    static const HdLocatorDataSourceHandle visibilityDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdVisibilitySchema::GetDefaultLocator());

    // xform
    static const HdLocatorDataSourceHandle xformDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdXformSchema::GetDefaultLocator());

    // instanced by
    static const HdLocatorDataSourceHandle instancedByDSL =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdInstancedBySchema::GetDefaultLocator());

    // Build dependencies data source

    // Read "-->" in comments as "depends on"

    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> sources;

    const auto originPathDS =
        HdRetainedTypedSampledDataSource<SdfPath>::New(originPath);

    // meshLight.light --> origin.light
    names.push_back(_tokens->meshLight_dep_light);
    sources.push_back(HdDependencySchema::Builder()
        .SetDependedOnPrimPath(originPathDS)
        .SetDependedOnDataSourceLocator(lightDSL)
        .SetAffectedDataSourceLocator(lightDSL)
        .Build());

    // meshLight.material --> origin.material (the light shader)
    names.push_back(_tokens->meshLight_dep_material);
    sources.push_back(HdDependencySchema::Builder()
        .SetDependedOnPrimPath(originPathDS)
        .SetDependedOnDataSourceLocator(materialDSL)
        .SetAffectedDataSourceLocator(materialDSL)
        .Build());

    // meshLight.material --> bindingSource.materialBinding
    names.push_back(_tokens->meshLight_dep_material_materialBinding);
    sources.push_back(HdDependencySchema::Builder()
        .SetDependedOnPrimPath(
            HdRetainedTypedSampledDataSource<SdfPath>::New(bindingSourcePath))
        .SetDependedOnDataSourceLocator(materialBindingsDSL)
        .SetAffectedDataSourceLocator(materialDSL)
        .Build());

    // meshLight.usdCollections --> origin.usdCollections
    names.push_back(_tokens->meshLight_dep_usdCollections);
    sources.push_back(HdDependencySchema::Builder()
        .SetDependedOnPrimPath(originPathDS)
        .SetDependedOnDataSourceLocator(usdCollectionsDSL)
        .SetAffectedDataSourceLocator(usdCollectionsDSL)
        .Build());

    // meshLight.visibility --> origin.visibility
    names.push_back(_tokens->meshLight_dep_visibility);
    sources.push_back(HdDependencySchema::Builder()
        .SetDependedOnPrimPath(originPathDS)
        .SetDependedOnDataSourceLocator(visibilityDSL)
        .SetAffectedDataSourceLocator(visibilityDSL)
        .Build());

    // meshLight.xform --> origin.xform
    names.push_back(_tokens->meshLight_dep_xform);
    sources.push_back(HdDependencySchema::Builder()
        .SetDependedOnPrimPath(originPathDS)
        .SetDependedOnDataSourceLocator(xformDSL)
        .SetAffectedDataSourceLocator(xformDSL)
        .Build());

    // meshLight.instancedBy --> origin.instancedBy
    names.push_back(_tokens->meshLight_dep_instancedBy);
    sources.push_back(HdDependencySchema::Builder()
        .SetDependedOnPrimPath(originPathDS)
        .SetDependedOnDataSourceLocator(instancedByDSL)
        .SetAffectedDataSourceLocator(instancedByDSL)
        .Build());

    // meshLight.material --> <bindingSource.materialBinding>.material
    names.push_back(_tokens->meshLight_dep_material_boundMaterial);
    sources.push_back(HdDependencySchema::Builder()
        .SetDependedOnPrimPath(HdMaterialBindingsSchema::GetFromParent(bindingSourceDS)
        .GetMaterialBinding().GetPath())
        .SetDependedOnDataSourceLocator(materialDSL)
        .SetAffectedDataSourceLocator(materialDSL)
        .Build());

    // XXX: Light filter dependencies *should* look like this:
    //   meshLight.material --> origin.material,
    //   origin.material --> origin.light.filters,
    //   origin.material --> <each filter>
    // If they did, we would not need any direct dependencies on the
    // light filter prims here. But light filters are not yet Hydra 2.0 enabled,
    // so we will put those direct dependencies here. Delete this stuff when
    // lights and light filters are properly handling the origin.material -->
    // <each filter> dependencies. (Note that the meshLight.material -->
    // origin.light.filters dependency is covered by the meshLight.material -->
    // origin.light dependency.)

    static const std::string prefix = "meshLight_dep_material_filter_";
    for (const SdfPath& filterPath : _GetLightFilterPaths(originDS))
    {
        names.push_back(TfToken(prefix + filterPath.GetAsString()));
        sources.push_back(HdDependencySchema::Builder()
            .SetDependedOnPrimPath(
                HdRetainedTypedSampledDataSource<SdfPath>::New(filterPath))
            .SetDependedOnDataSourceLocator(nullptr)
            .SetAffectedDataSourceLocator(materialDSL)
            .Build());
    }

    // And since these dependencies are dynamic,
    // meshLight.__dependencies --> origin.light.filters
    static const TfToken filtersDepToken("meshLight_dep_dependencies_filters");
    names.push_back(filtersDepToken);
    sources.push_back(HdDependencySchema::Builder()
        .SetDependedOnPrimPath(originPathDS)
        .SetDependedOnDataSourceLocator(lightFiltersDSL)
        .SetAffectedDataSourceLocator(materialDSL)
        .Build());

    return HdRetainedContainerDataSource::New(
        names.size(), names.data(), sources.data());
}

HdContainerDataSourceHandle
_AddNodeIdentifier(const HdSceneIndexPrim& originPrim)
{
    const TfToken kmaContext = _tokens->renderContext;
    const TfToken nodeIdentifier = (originPrim.primType == HdPrimTypeTokens->volume) ?
        UsdLuxTokens->VolumeLight : UsdLuxTokens->MeshLight;

    HdContainerDataSourceHandle networkDS =
        HdMaterialSchema::GetFromParent(originPrim.dataSource)
            .GetMaterialNetwork(TfTokenVector{ kmaContext })
            .GetContainer();

    HdMaterialNodeContainerSchema nodesSchema = HdMaterialNetworkSchema(networkDS).GetNodes();
    HdContainerDataSourceHandle nodesDS = nodesSchema.GetContainer();
    if (!nodesDS)
        return nullptr;

    TfToken nodeName;
    for (const TfToken &name : nodesDS->GetNames())
    {
        nodeName = name;
        break; // first child
    }
    if (nodeName.IsEmpty())
        return nullptr;

    HdContainerDataSourceHandle existingNodeDS =
        HdContainerDataSource::Cast(nodesDS->Get(nodeName));

    HdMaterialNodeSchema existingNode(existingNodeDS);
    HdTokenDataSourceHandle existingIdDS = existingNode.GetNodeIdentifier();
    if (existingIdDS)
    {
        TfToken existingId = existingIdDS->GetTypedValue(0.0f);
        if (existingId == nodeIdentifier)
        {
            // Identifier already present and correct — do nothing
            return nullptr;
        }
    }

    HdMaterialNodeSchema::Builder nodeBuilder;
    nodeBuilder.SetNodeIdentifier(
        HdRetainedTypedSampledDataSource<TfToken>::New(nodeIdentifier));
    HdContainerDataSourceHandle identifierNodeDS = nodeBuilder.Build();

    HdContainerDataSourceHandle nodeOverlays[] = {
        identifierNodeDS,
        existingNodeDS
    };
    HdContainerDataSourceHandle mergedNodeDS =
        HdOverlayContainerDataSource::New(2, nodeOverlays);

    // Rebuild the nodes container with merged node
    HdContainerDataSourceHandle newNodesDS =
        HdRetainedContainerDataSource::New(nodeName, mergedNodeDS);

    HdContainerDataSourceHandle nodesOverlays[] = {
        newNodesDS,
        nodesDS
    };
    HdContainerDataSourceHandle mergedNodesDS =
        HdOverlayContainerDataSource::New(2, nodesOverlays);

    // Overlay the nodes container onto the existing network
    HdContainerDataSourceHandle networkOverlays[] = {
        HdRetainedContainerDataSource::New(HdMaterialNetworkSchemaTokens->nodes, mergedNodesDS),
        networkDS
    };
    HdContainerDataSourceHandle newNetworkDS =
        HdOverlayContainerDataSource::New(2, networkOverlays);

    // Put the updated network back into the material
    HdDataSourceBaseHandle networkBaseDS = newNetworkDS;
    HdContainerDataSourceHandle materialDS =
        HdMaterialSchema::BuildRetained(1, &kmaContext, &networkBaseDS);

    return materialDS;
}

HdContainerDataSourceHandle
_BuildPrimvarDataSource(
    const SdfPath originPath,
    const HdSceneIndexPrim& originPrim)
{
    // Temp storage for overriding primvars.
    TfSmallVector<TfToken, 4> primvarNames;
    TfSmallVector<HdDataSourceBaseHandle, 4> primvarVals;

    SdfPath tmpPath = originPath;
    tmpPath = tmpPath.AppendChild(_tokens->meshLightLightName);
    const HdContainerDataSourceHandle meshLightNameDS =
        HdPrimvarSchema::Builder()
            .SetPrimvarValue(
                HdRetainedTypedSampledDataSource<std::string>::New(std::string(tmpPath.GetText())))
            .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
                HdPrimvarSchemaTokens->constant))
            .Build();
    primvarNames.push_back(_tokens->meshLightName);
    primvarVals.push_back(meshLightNameDS);

    const TfToken materialSyncMode = _GetMaterialSyncMode(originPrim.dataSource);

    // syncmode should match BRAY_GeoLightMaterialSyncMode enum
    int syncmode = MaterialGlowTintsLight;
    // since volume is defined by it's density field it would
    // require to evaluate the bould emission. Hence we only
    // support for MaterialGlowTintsLight mode in case of volume.
    if (originPrim.primType != HdPrimTypeTokens->volume)
    {
        if (materialSyncMode == _tokens->materialGlowTintsLight)
            syncmode = MaterialGlowTintsLight;
        else if (materialSyncMode == _tokens->independent)
            syncmode = Independent;
        else if (materialSyncMode == _tokens->noMaterialResponse)
            syncmode = NoMaterialResponse;
    }
    const HdContainerDataSourceHandle materialSyncDS = HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<int>::New(syncmode))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build();
    primvarNames.push_back(_tokens->light_material_sync);
    primvarVals.push_back(materialSyncDS);

    // Get the original light shader network
    const HdContainerDataSourceHandle& originalShaderDS =
        HdMaterialSchema::GetFromParent(originPrim.dataSource)
            .GetMaterialNetwork(TfTokenVector({_tokens->renderContext}))
		.GetContainer();

    // interface with the original light shader network
    HdDataSourceMaterialNetworkInterface shaderNI(originPath, originalShaderDS,
                                                  originPrim.dataSource);
    // look up the light terminal connection
    const auto lightTC = shaderNI.GetTerminalConnection(
        HdMaterialTerminalTokens->light);

    GfVec3f color(1,1,1);
    VtValue vtcolor = shaderNI.GetNodeParameterValue(
        lightTC.second.upstreamNodeName,
        HdLightTokens->color);
    if (vtcolor.IsHolding<GfVec3f>())
        color = vtcolor.Get<GfVec3f>();
    const HdContainerDataSourceHandle colorDS = HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<GfVec3f>::New(color))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build();
    primvarNames.push_back(HdLightTokens->color);
    primvarVals.push_back(colorDS);

    float intensity = 1.f;
    VtValue vtintensity = shaderNI.GetNodeParameterValue(
        lightTC.second.upstreamNodeName,
        HdLightTokens->intensity);
    if (vtintensity.IsHolding<float>())
        intensity = vtintensity.Get<float>();
    const HdContainerDataSourceHandle intensityDS = HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<float>::New(intensity))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build();
    primvarNames.push_back(HdLightTokens->intensity);
    primvarVals.push_back(intensityDS);

    float exposure = 0.f;
    VtValue vtexposure = shaderNI.GetNodeParameterValue(
        lightTC.second.upstreamNodeName,
        HdLightTokens->exposure);
    if (vtexposure.IsHolding<float>())
        exposure = vtexposure.Get<float>();
    const HdContainerDataSourceHandle exposureDS = HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<float>::New(exposure))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build();
    primvarNames.push_back(HdLightTokens->exposure);
    primvarVals.push_back(exposureDS);

    bool enable_clr_temp = false;
    VtValue vtenclrtemp = shaderNI.GetNodeParameterValue(
        lightTC.second.upstreamNodeName,
        HdLightTokens->enableColorTemperature);
    if (vtenclrtemp .IsHolding<bool>())
        enable_clr_temp = vtenclrtemp.Get<bool>();
    const HdContainerDataSourceHandle enable_clr_tempr_DS = HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<bool>::New(enable_clr_temp))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build();
    primvarNames.push_back(HdLightTokens->enableColorTemperature);
    primvarVals.push_back(enable_clr_tempr_DS);

    float colortemp = 0.f;
    VtValue vtcolortemp = shaderNI.GetNodeParameterValue(
        lightTC.second.upstreamNodeName,
        HdLightTokens->colorTemperature);
    if (vtcolortemp.IsHolding<float>())
        colortemp = vtcolortemp.Get<float>();
    const HdContainerDataSourceHandle clr_tempr_DS = HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<float>::New(colortemp))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build();
    primvarNames.push_back(HdLightTokens->colorTemperature);
    primvarVals.push_back(clr_tempr_DS);

    GfVec3f emission(1,1,1);
    const HdContainerDataSourceHandle emissionDS = HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<GfVec3f>::New(emission))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build();
    primvarNames.push_back(_tokens->light_emission);
    primvarVals.push_back(emissionDS);

    const HdContainerDataSourceHandle treatLightSourceDS = HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<int>::New((int)TALS_YES_SPRIM))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build();
    primvarNames.push_back(_tokens->treatAsLightSource);
    primvarVals.push_back(treatLightSourceDS);

    std::string lpetag;
    VtValue vtlpetag = shaderNI.GetNodeParameterValue(
        lightTC.second.upstreamNodeName,
        _tokens->lightLpeTag);
    if (!vtlpetag.IsEmpty() && vtlpetag.IsHolding<std::string>())
    {
        lpetag = vtlpetag.Get<std::string>();
        const HdContainerDataSourceHandle lpetagDS = HdPrimvarSchema::Builder()
            .SetPrimvarValue(HdRetainedTypedSampledDataSource<std::string>::New(lpetag))
            .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
                HdPrimvarSchemaTokens->constant))
            .Build();
        primvarNames.push_back(_tokens->meshLpeTag);
        primvarVals.push_back(lpetagDS);
    }

    return HdPrimvarsSchema::BuildRetained(
        primvarNames.size(),
        primvarNames.data(),
        primvarVals.data());
}

HdContainerDataSourceHandle
_BuildLightDataSource(
    const SdfPath& originPath,
    const HdSceneIndexPrim& originPrim,
    const SdfPath& bindingSourcePath,
    const HdContainerDataSourceHandle& bindingSourceDS,
    const HdSceneIndexBaseRefPtr& inputSceneIndex)
{
    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> sources;

    // Add dependencies
    names.push_back(HdDependenciesSchemaTokens->__dependencies);
    sources.push_back(
        _BuildLightDependenciesDataSource(
            originPath, originPrim.dataSource,
            bindingSourcePath, bindingSourceDS));

    // Knock out primvars
    names.push_back(HdPrimvarsSchemaTokens->primvars);
    sources.push_back(HdBlockDataSource::New());

    // Knock out mesh
    names.push_back(HdMeshSchemaTokens->mesh);
    sources.push_back(HdBlockDataSource::New());

    if (originPrim.primType != HdPrimTypeTokens->volume)
    {
        // Knock out material binding
        names.push_back(HdMaterialBindingsSchema::GetSchemaToken());
        sources.push_back(HdBlockDataSource::New());
    }

    // Knock out volume field binding
    names.push_back(HdVolumeFieldBindingSchemaTokens->volumeFieldBinding);
    sources.push_back(HdBlockDataSource::New());

    HdContainerDataSourceHandle nodeIdDS = _AddNodeIdentifier(originPrim);
    if (nodeIdDS)
    {
        names.push_back(HdMaterialSchemaTokens->material);
        sources.push_back(nodeIdDS);
    }

    HdContainerDataSourceHandle handles[2] =
    {
        HdRetainedContainerDataSource::New(names.size(), names.data(), sources.data()),
        originPrim.dataSource
    };
    return HdOverlayContainerDataSource::New(2, handles);
}

}; // namespace

/* static */
HD_MeshLightSceneIndexRefPtr
HD_MeshLightSceneIndex::New(
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
{
    return TfCreateRefPtr(
            new HD_MeshLightSceneIndex(inputSceneIndex));
}

HD_MeshLightSceneIndex::HD_MeshLightSceneIndex(
        const HdSceneIndexBaseRefPtr &inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
}

HdSceneIndexPrim
HD_MeshLightSceneIndex::GetPrim(
        const SdfPath &primPath) const
{
    // The origin prim -> no primType (should only be visible to HSB)
    if (_meshLights.count(primPath))
    {
        return {
            TfToken(),
            _GetInputSceneIndex()->GetPrim(primPath).dataSource
        };
    }

    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    const SdfPath& parentPath = primPath.GetParentPath();

    if (_meshLights.count(parentPath) > 0)
    {
        const HdSceneIndexPrim& parentPrim = _GetInputSceneIndex()->GetPrim(parentPath);

        if (primPath.GetNameToken() == _tokens->meshLightMeshName)
        {
            HdContainerDataSourceHandle handles[2] = {
                HdRetainedContainerDataSource::New(
                    HdMaterialSchemaTokens->material,
                    HdBlockDataSource::New(),
                    _tokens->usdCollections,
                    HdBlockDataSource::New(),
                    HdPrimvarsSchema::GetSchemaToken(),
                    _BuildPrimvarDataSource(parentPath, parentPrim)),
                parentPrim.dataSource
            };

           return {
               parentPrim.primType,
               HdOverlayContainerDataSource::New(2, handles)
           };
        }
        if (primPath.GetNameToken() == _tokens->meshLightLightName)
        {
           return {
               HdPrimTypeTokens->meshLight,
               _BuildLightDataSource(
                   parentPath, parentPrim,
                   parentPath, parentPrim.dataSource,
                   _GetInputSceneIndex())
           };
        }
    }

    return prim;
}

SdfPathVector
HD_MeshLightSceneIndex::GetChildPrimPaths(
        const SdfPath &primPath) const
{
    SdfPathVector paths = _GetInputSceneIndex()->GetChildPrimPaths(primPath);

    if (_meshLights.count(primPath))
    {
        paths.push_back(primPath.AppendChild(_tokens->meshLightLightName));
        paths.push_back(primPath.AppendChild(_tokens->meshLightMeshName));
    }
    return paths;
}

void
HD_MeshLightSceneIndex::_PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    if (!_IsObserved())
        return;

    HdSceneIndexObserver::AddedPrimEntries added;

    for (const auto& entry : entries)
    {
        if (_IsSupportedGeometryLightSource(entry.primType))
        {
            HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(entry.primPath);

            if (_IsMeshLight(entry.primPath, prim))
                //&& _HasValidMaterialNetwork(prim, _GetInputSceneIndex()))
            {
                const bool meshVisible = true;
                _meshLights.insert({ entry.primPath, meshVisible });

                // The light prim
                added.emplace_back(
                    entry.primPath.AppendChild(_tokens->meshLightLightName),
                    _tokens->meshLight);

                // The mesh prim
                added.emplace_back(
                    entry.primPath.AppendChild(_tokens->meshLightMeshName),
                    entry.primType);

                // skip fallback insertion
                continue;
            }
            else if (_meshLights.count(entry.primPath))
            {
                // prim was a mesh light but not anymore.
                // It seems change on applied API schema triggers prim to be (re)added!

                // clear the cache
                _meshLights.erase(entry.primPath);

                _SendPrimsRemoved({
                  { entry.primPath.AppendChild(_tokens->meshLightLightName) },
                  { entry.primPath.AppendChild(_tokens->meshLightMeshName) }
                });
                continue;
            }
        }
        added.push_back(entry);
    }
    _SendPrimsAdded(added);
}

void
HD_MeshLightSceneIndex::_PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    HdSceneIndexObserver::RemovedPrimEntries removed;

    for (const auto& entry : entries)
    {
        if (_meshLights.count(entry.primPath))
        {
            // The light prim
            removed.emplace_back(
                entry.primPath.AppendChild(_tokens->meshLightLightName));

            // The stripped-down origin prim
            removed.emplace_back(
                entry.primPath.AppendChild(_tokens->meshLightMeshName));

            _meshLights.erase(entry.primPath);

            // skip fallback removal
            continue;
        }
        removed.push_back(entry);
    }

    _SendPrimsRemoved(removed);
}

void
HD_MeshLightSceneIndex::_PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    for (const auto& entry : entries)
    {
        if (_meshLights.count(entry.primPath))
        {
            // If the lightLink collection on the origin changed then we need
            // to dirty the light's lightLink collection as well.
            // NOTE: This can *not* be handled by dependency forwarding since
            //       this dirty "signal" is expected and looked for by the
            //       `HdsiLightLinkingSceneIndex`.
            static const HdDataSourceLocator collectionsLightLinkLoc =
                HdCollectionsSchema::GetDefaultLocator().Append(HdTokens->lightLink);
            if (entry.dirtyLocators.Intersects(collectionsLightLinkLoc))
                _SendPrimsDirtied({{
                    entry.primPath.AppendChild(_tokens->meshLightLightName),
                    { collectionsLightLinkLoc }
                }});

            // Propogate dirtiness to the mesh and meshLight light if applicable.
            // HdDataSourceLocator::EmptyLocator() == AllDirty in Hydra 1.0
            if (entry.dirtyLocators.Intersects(HdDataSourceLocator::EmptyLocator()) ||
                entry.dirtyLocators.Intersects(HdPrimvarsSchema::GetDefaultLocator()) ||
                entry.dirtyLocators.Intersects(HdCategoriesSchema::GetDefaultLocator()) ||
                entry.dirtyLocators.Intersects(HdMaterialBindingsSchema::GetDefaultLocator()))
            {
                _SendPrimsDirtied({{ entry.primPath.AppendChild(_tokens->meshLightLightName),
                {
                    HdLightSchema::GetDefaultLocator(),
                    HdMaterialSchema::GetDefaultLocator(),
                    HdPrimvarsSchema::GetDefaultLocator(),
                    HdVisibilitySchema::GetDefaultLocator(),
                    HdXformSchema::GetDefaultLocator()
                }}});

                HdSceneIndexObserver::DirtiedPrimEntry dirtyEntry = entry;
                dirtyEntry.primPath = entry.primPath.AppendChild(_tokens->meshLightMeshName);
                dirtyEntry.dirtyLocators.insert(HdPrimvarsSchema::GetDefaultLocator());
                _SendPrimsDirtied( { dirtyEntry });
            }
        }
    }

    _SendPrimsDirtied(entries);
}


PXR_NAMESPACE_CLOSE_SCOPE
