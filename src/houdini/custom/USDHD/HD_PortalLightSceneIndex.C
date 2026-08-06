//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "HD_PortalLightSceneIndex.h"

#include "pxr/base/tf/staticTokens.h"
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceMaterialNetworkInterface.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/lightSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/version.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <unordered_map>
#include <unordered_set>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (PortalLight)

    (KMAskyDomeLight)
    ((isKMAskyDomeLight,    "isKMAskyDomeLight"))
    ((solar_altitude,       "solar_altitude"))
    ((solar_azimuth,        "solar_azimuth"))
    ((turbidity,            "turbidity"))
    ((horizon_blur,         "horizon_blur"))
    ((ground_albedo,        "ground_albedo"))
    ((ground_color,         "ground_color"))

    // light schema tokens
    (domeOffset)
    ((isPortalLight,        "isPortalLight"))
    ((portalDomeXform,      "portalDomeXform"))

    ((material,             "material"))
    ((portalMesh,           "portalmesh"))
    ((width,                "width"))
    ((height,               "height"))
    ((leftHanded,           "leftHanded"))

    // karma obj props
    ((isPortal,             "karma:object:isportal"))
    ((portalDomeLights,     "karma:object:portaldomelights"))
    ((portalMISBias,        "karma:light:portalmisbias"))
    ((singleSided,          "karma:light:singlesided"))
    ((renderLightGeo,       "karma:light:renderlightgeo"))

    // material network tokens
    ((colorMap,             "texture:file"))

    // render context / material network selector
    ((renderContext, ""))
    ((renderContext_kma,    "kma"))

);

// Material parameters for which we should overwrite unauthored values
// on a portal light with authored values from the portal's dome light.
TF_DEFINE_PRIVATE_TOKENS(
    _inheritedAttrTokens,

    (colorEnableTemperature)
    (colorTemperature)
    (diffuse)
    (specular)
    ((shadowColor,             "shadow:color"))
    ((shadowDistance,          "shadow:distance"))
    ((shadowEnable,            "shadow:enable"))
    ((shadowFalloff,           "shadow:falloff"))
    ((shadowFalloffGamma,      "shadow:falloffGamma"))
);

namespace {

static const auto& theLightLocator    = HdLightSchema::GetDefaultLocator();
static const auto& theMaterialLocator = HdMaterialSchema::GetDefaultLocator();
static const auto& theXformLocator    = HdXformSchema::GetDefaultLocator();
static const auto& thePrimvarLocator  = HdPrimvarsSchema::GetDefaultLocator();
static const auto& theTopologyLocator = HdMeshTopologySchema::GetDefaultLocator();
static const HdDataSourceLocatorSet thePortalLocators =
    {theMaterialLocator, theLightLocator, theXformLocator, thePrimvarLocator, theTopologyLocator};


HdContainerDataSourceHandle
_GetMaterialDataSource(const HdSceneIndexPrim &prim)
{
    return HdMaterialSchema::GetFromParent(prim.dataSource)
            .GetMaterialNetwork(_tokens->renderContext).GetContainer();
}

bool
_IsPortalLight(const HdSceneIndexPrim& prim, const SdfPath& primPath)
{
    const HdContainerDataSourceHandle matDataSource =
        _GetMaterialDataSource(prim);
    if (matDataSource)
    {
        HdDataSourceMaterialNetworkInterface matInterface(
            primPath, matDataSource, prim.dataSource);

        const auto matTerminal =
            matInterface.GetTerminalConnection(HdMaterialTerminalTokens->light);
        const auto nodeName = matTerminal.second.upstreamNodeName;
        const TfToken nodeTypeName = matInterface.GetNodeType(nodeName);
        return nodeTypeName == _tokens->PortalLight;
    }
    return false;
}

bool
_IsPhysicalSky(const HdSceneIndexPrim& prim, const SdfPath& primPath)
{
    const HdContainerDataSourceHandle matDataSource =
        _GetMaterialDataSource(prim);

    if (matDataSource)
    {
        HdDataSourceMaterialNetworkInterface matInterface(
            primPath, matDataSource, prim.dataSource);

        const auto matTerminal =
            matInterface.GetTerminalConnection(HdMaterialTerminalTokens->light);

        // Get the original light shader network
        HdMaterialNetworkSchema netSchema =
            HdMaterialSchema::GetFromParent(prim.dataSource)
                .GetMaterialNetwork(TfTokenVector({_tokens->renderContext}));
        HdMaterialNodeSchema nodeSchema = netSchema.GetNodes().Get(
            TfToken(matTerminal.second.upstreamNodeName));
        if (nodeSchema)
        {
            HdContainerDataSourceHandle idsDs = nodeSchema.GetRenderContextNodeIdentifiers();
            if (idsDs)
            {
                HdTokenDataSourceHandle ds = HdTokenDataSource::Cast(
                    idsDs->Get(_tokens->renderContext_kma));
                if (ds)
                    return ds->GetTypedValue(0) == _tokens->KMAskyDomeLight;
            }
        }
    }
    return false;
}

// Helper function to extract a value from a light data source.
template <typename T>
T
_GetLightData(
    const HdContainerDataSourceHandle& primDataSource,
    const TfToken& name,
    const T defaultValue=T())
{
    if (auto lightSchema = HdLightSchema::GetFromParent(primDataSource)) {
        if (auto dataSource = HdTypedSampledDataSource<T>::Cast(
                lightSchema.GetContainer()->Get(name))) {
            return dataSource->GetTypedValue(0.0f);
        }
    }

    return defaultValue;
}

TfToken
_ResolveLinking(
    const HdContainerDataSourceHandle& portalDataSource,
    const HdContainerDataSourceHandle& domeDataSource,
    const TfToken& linkType)
{
    // Light/shadow linking set directly on the portal light wins, if present.
    // Otherwise, fall back to linking set on the dome light.
    TfToken linking = _GetLightData<TfToken>(portalDataSource, linkType);
    if (linking.IsEmpty()) {
        linking = _GetLightData<TfToken>(domeDataSource, linkType);
    }
    return linking;
}

SdfPathVector
_GetPortalPaths(const HdContainerDataSourceHandle& primDataSource)
{
    return _GetLightData<SdfPathVector>(primDataSource, HdTokens->portals);
}

SdfPathVector
_GetLightFilterPaths(const HdContainerDataSourceHandle& primDataSource)
{
    return _GetLightData<SdfPathVector>(primDataSource, HdTokens->filters);
}


HdContainerDataSourceHandle
_BuildPortalDataSource(
    const SdfPath& portalPrimPath,
    const SdfPath& domePrimPath,
    const HdSceneIndexBaseRefPtr& inputSceneIndex)
{
    const auto domePrim   = inputSceneIndex->GetPrim(domePrimPath);
    const auto portalPrim = inputSceneIndex->GetPrim(portalPrimPath);

    if (!domePrim.dataSource || !_IsPortalLight(portalPrim, portalPrimPath))
        return portalPrim.dataSource;

    // Get data sources for the associated dome light.
    // -------------------------------------------------------------------------
    const HdContainerDataSourceHandle domeMatDataSource =
        _GetMaterialDataSource(domePrim);
    HdDataSourceMaterialNetworkInterface domeMatInterface(domePrimPath,
                                                          domeMatDataSource,
                                                          domePrim.dataSource);

    const auto domeMatTerminal =
        domeMatInterface.GetTerminalConnection(HdMaterialTerminalTokens->light);

    HdXformSchema domeXformSchema =
        HdXformSchema::GetFromParent(domePrim.dataSource);

    // Get some relevant values from the dome light's data sources.
    // -------------------------------------------------------------------------
    const auto getDomeMatVal =
        [&domeMatInterface, &domeMatTerminal](const TfToken& paramName){
            return domeMatInterface.GetNodeParameterValue(
                domeMatTerminal.second.upstreamNodeName, paramName);
        };

    // Note that the attribute name for colorMap is "texture:file";
    // That is the attribute name used by USD, and reflected in the
    // RenderMan light plugin args files for most light types -- with
    // the exception of portals, which is why map it to domeColorMap.
    const VtValue domeColorMapVal  = getDomeMatVal(_tokens->colorMap);
    const VtValue domeColorVal     = getDomeMatVal(HdLightTokens->color);
    const VtValue domeIntensityVal = getDomeMatVal(HdLightTokens->intensity);
    const VtValue domeExposureVal  = getDomeMatVal(HdLightTokens->exposure);
    const VtValue domeSingleSided  = getDomeMatVal(_tokens->singleSided);
    const VtValue domePortalMISBias= getDomeMatVal(_tokens->portalMISBias);
    const VtValue domeRenderGeo    = getDomeMatVal(_tokens->renderLightGeo);

    // Use the resolved path of the asset if available, otherwise
    // pass through the original asset path.  This is important in
    // order to support RenderMan's texture plugin system, which
    // uses texture paths of the form "rtxplugin:...".
    const SdfAssetPath domeColorMapAssetPath =
        domeColorMapVal.IsHolding<SdfAssetPath>()
        ? domeColorMapVal.UncheckedGet<SdfAssetPath>() : SdfAssetPath();

    const auto domeColor     = domeColorVal.GetWithDefault(GfVec3f(1.0f));
    const auto domeIntensity = domeIntensityVal.GetWithDefault(1.0f);
    const auto domeExposure  = domeExposureVal.GetWithDefault(0.0f);

    // domeOffset exists in the light schema, not in the material netowrk.
    // See UsdImaging/domeLight_1_Adapter.cpp for an example provider,
    // and hdPrman/light.cpp for where this is used.
    GfMatrix4d domeOffset =
        _GetLightData<GfMatrix4d>(domePrim.dataSource, _tokens->domeOffset,
                                  GfMatrix4d(1.0));

    GfMatrix4d domeXform;
    if (const auto origDomeXform = domeXformSchema.GetMatrix()) {
        domeXform = domeOffset * origDomeXform->GetTypedValue(0.0f);
    }
    else {
        domeXform.SetIdentity();
    }

    // Get data sources for the portal light.
    // -------------------------------------------------------------------------
    const HdContainerDataSourceHandle portalMatDataSource =
        _GetMaterialDataSource(portalPrim);
    HdDataSourceMaterialNetworkInterface portalMatInterface(
        portalPrimPath, portalMatDataSource, portalPrim.dataSource);

    const auto portalMatTerminal =
        portalMatInterface.GetTerminalConnection(
            HdMaterialTerminalTokens->light);

    HdXformSchema portalXformSchema =
        HdXformSchema::GetFromParent(portalPrim.dataSource);

    GfMatrix4d portalXform;
    if (const auto portalDomeXform = portalXformSchema.GetMatrix()) {
        portalXform = portalDomeXform->GetTypedValue(0.0f);
    }
    else {
        portalXform.SetIdentity();
    }

    // Get some relevant values from the portal light's data sources.
    // -------------------------------------------------------------------------
    const auto getPortalMatVal =
        [&portalMatInterface, &portalMatTerminal](const TfToken& paramName){
            return portalMatInterface.GetNodeParameterValue(
                portalMatTerminal.second.upstreamNodeName, paramName);
        };

    const VtValue portalColorVal     = getPortalMatVal(HdLightTokens->color);
    const VtValue portalIntensityVal = getPortalMatVal(HdLightTokens->intensity);
    const VtValue portalExposureVal  = getPortalMatVal(HdLightTokens->exposure);

    const auto portalColor     = portalColorVal.GetWithDefault(GfVec3f(1.0f));
    const auto portalIntensity = portalIntensityVal.GetWithDefault(1.0f);
    const auto portalExposure  = portalExposureVal.GetWithDefault(0.0f);

    // Compute new values for the portal's material data source.
    // -------------------------------------------------------------------------
    const auto setPortalParamVal =
        [&portalMatInterface, &portalMatTerminal](
            const TfToken& paramName, const VtValue& value)
        {
            portalMatInterface.SetNodeParameterValue(
                portalMatTerminal.second.upstreamNodeName, paramName, value);
        };

    const auto computedPortalColor = GfCompMult(portalColor, domeColor);
    const float computedPortalIntensity = portalIntensity * domeIntensity;
    const float computedPortalExposure = domeExposure + portalExposure;
    // The scene index returns two prims in case of portal:
    // the portal light and the proxy mesh prims.
    // 1. The portal proxy mesh: takes the portal prim transform.
    //    The proxy is mainly required by Karma delegate to
    //    maintain the portal workflow and backward compatiblity.
    // 2. The portal light: has a viewport guide which requires to have the
    //    same transform as the portal prim for it to work correctly
    //    in the viewport. Hence setting portalXform in below code.
    //    However Karma, under the hood, converts portal light to a
    //    dome light which requires the linked dome-light transform.
    //    We piggyback the dome light transform into light schema
    //    for karma delegate. BRAY_HdLight then overrides the xform
    //    by portalDomeXform.
    const auto computedPortalToDome = portalXform; //domeXform;

    setPortalParamVal(_tokens->colorMap,         VtValue(domeColorMapAssetPath));
    setPortalParamVal(HdLightTokens->color,      VtValue(computedPortalColor));
    setPortalParamVal(HdLightTokens->intensity,  VtValue(computedPortalIntensity));
    setPortalParamVal(HdLightTokens->exposure,   VtValue(computedPortalExposure));

    // XXX -- We can probably delete the portal's tint and intensityMult params
    //        now, since they're not used by the RenderMan light shader.

    // Directly copy a bunch of other params from the dome to the portal.
    // XXX -- We'd like to do this only for *unauthored* portal params. However,
    //        there's no obvious way to tell which params are user-authored.
    for (const auto& attr: _inheritedAttrTokens->allTokens)
        setPortalParamVal(attr, getDomeMatVal(attr));

    // Compute new values for the portal's light data source.
    // -------------------------------------------------------------------------
    // All we're going to do is copy the light filter paths from the dome's
    // light.filters data source to the portal's light.filters data source.
    // This means that the filter prims will still just exist under the dome
    // and filter xforms will be relative to the dome, not the portal. That
    // xform behavior is expected; it matches what happens in Katana.
    SdfPathVector domeFilters = _GetLightFilterPaths(domePrim.dataSource);
    SdfPathVector allFilters  = _GetLightFilterPaths(portalPrim.dataSource);
    allFilters.insert(allFilters.end(),
                      std::make_move_iterator(domeFilters.begin()),
                      std::make_move_iterator(domeFilters.end()));
    const auto computedFiltersDataSource =
        HdRetainedTypedSampledDataSource<SdfPathVector>::New(allFilters);

    // Resolve light and shadow linking.
    const auto lightLink = _ResolveLinking(
        portalPrim.dataSource, domePrim.dataSource, HdTokens->lightLink);
    const auto shadowLink = _ResolveLinking(
        portalPrim.dataSource, domePrim.dataSource, HdTokens->shadowLink);

    const auto computedLightLinkDataSource =
        HdRetainedTypedSampledDataSource<TfToken>::New(lightLink);
    const auto computedShadowLinkDataSource =
        HdRetainedTypedSampledDataSource<TfToken>::New(shadowLink);
    const auto portalMISBiasDataSource =
        HdRetainedTypedSampledDataSource<VtValue>::New(domePortalMISBias);
    const auto singlesidedDataSource =
        HdRetainedTypedSampledDataSource<VtValue>::New(domeSingleSided);
    const auto renderLightGeoDataSource =
        HdRetainedTypedSampledDataSource<VtValue>::New(domeRenderGeo);
    const auto domeXformDataSource =
        HdRetainedTypedSampledDataSource<GfMatrix4d>::New(domeXform);
    const auto isPortalDataSource =
        HdRetainedTypedSampledDataSource<bool>::New(true);
    bool isKMAPhysicalSkyDome = _IsPhysicalSky(domePrim, domePrimPath);
    const auto isKMADomeLightDataSource =
        HdRetainedTypedSampledDataSource<bool>::New(isKMAPhysicalSkyDome);

    // Assemble the final data source for the portal light.
    // -------------------------------------------------------------------------
    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> sources;

    names.push_back(HdMaterialSchemaTokens->material);
    sources.push_back(HdRetainedContainerDataSource::New(
        _tokens->renderContext, portalMatInterface.Finish()));

    names.push_back(HdLightSchemaTokens->light);
    auto baseLightSchemaContainer = HdRetainedContainerDataSource::New(
        HdTokens->filters,          computedFiltersDataSource,
        HdTokens->lightLink,        computedLightLinkDataSource,
        HdTokens->shadowLink,       computedShadowLinkDataSource,
        _tokens->portalMISBias,     portalMISBiasDataSource,
        _tokens->singleSided,       singlesidedDataSource,
        _tokens->renderLightGeo,    renderLightGeoDataSource);
    auto extendedLightSchemaContainer = HdOverlayContainerDataSource::New(
        baseLightSchemaContainer,
        HdRetainedContainerDataSource::New(
            _tokens->isPortalLight,     isPortalDataSource,
            _tokens->portalDomeXform,   domeXformDataSource,
            _tokens->isKMAskyDomeLight, isKMADomeLightDataSource));
    if (isKMAPhysicalSkyDome)
    {
        portalMatInterface.SetNodeType(
            portalMatTerminal.second.upstreamNodeName,
            _tokens->KMAskyDomeLight);

        setPortalParamVal(_tokens->solar_altitude,
            getDomeMatVal(_tokens->solar_altitude));
        setPortalParamVal(_tokens->solar_azimuth,
            getDomeMatVal(_tokens->solar_azimuth));
        setPortalParamVal(_tokens->turbidity,
            getDomeMatVal(_tokens->turbidity));
        setPortalParamVal(_tokens->ground_albedo,
            getDomeMatVal(_tokens->ground_albedo));
        setPortalParamVal(_tokens->ground_color,
            getDomeMatVal(_tokens->ground_color));

        /*HdContainerDataSourceHandle kmaSkyLightSchemaContainer =
            HdRetainedContainerDataSource::New(
                _tokens->solar_altitude,
                HdRetainedTypedSampledDataSource<VtValue>::New(
                    getDomeMatVal(_tokens->solar_altitude)),
                _tokens->solar_azimuth,
                HdRetainedTypedSampledDataSource<VtValue>::New(
                    getDomeMatVal(_tokens->solar_azimuth)),
                _tokens->turbidity,
                HdRetainedTypedSampledDataSource<VtValue>::New(
                    getDomeMatVal(_tokens->turbidity)),
                _tokens->ground_albedo,
                HdRetainedTypedSampledDataSource<VtValue>::New(
                    getDomeMatVal(_tokens->ground_albedo)),
                _tokens->ground_color,
                HdRetainedTypedSampledDataSource<VtValue>::New(
                    getDomeMatVal(_tokens->ground_color)));

        extendedLightSchemaContainer = HdOverlayContainerDataSource::New(
            kmaSkyLightSchemaContainer,
            extendedLightSchemaContainer); */
    }
    sources.push_back(extendedLightSchemaContainer);

    // portal xform
    HdDataSourceBaseHandle xformDS = HdXformSchema::BuildRetained(
        HdRetainedTypedSampledDataSource<GfMatrix4d>::New(computedPortalToDome),
        HdRetainedTypedSampledDataSource<bool>::New(true) // resetXform = true
    );
    names.push_back(HdXformSchema::GetSchemaToken());
    sources.push_back(xformDS);

    // prune material from the original data, otherwise it takes precedence
    std::vector<TfToken> orgNamesWithoutMat;
    std::vector<HdDataSourceBaseHandle> orgSourcesWithoutMat;
    for (const TfToken& name : portalPrim.dataSource->GetNames())
    {
        if (name != HdMaterialSchemaTokens->material)
        {
            orgNamesWithoutMat.push_back(name);
            orgSourcesWithoutMat.push_back(portalPrim.dataSource->Get(name));
        }
    }
    HdContainerDataSourceHandle prunedContainer =
        HdRetainedContainerDataSource::New(
            orgNamesWithoutMat.size(), orgNamesWithoutMat.data(), orgSourcesWithoutMat.data());

    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(names.size(), names.data(), sources.data()),
        prunedContainer);
}

HdContainerDataSourceHandle
_BuildPortalMehsDataSource(
    const SdfPath& portalPath,
    const HdSceneIndexPrim& portalPrim)
{
    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> sources;

    // Temp storage for overriding primvars.
    TfSmallVector<TfToken, 3> primvarNames;
    TfSmallVector<HdDataSourceBaseHandle, 3> primvarVals;

    // Build the primvar container
    float width  = _GetLightData<float>(portalPrim.dataSource, _tokens->width,  1.0f);
    float height = _GetLightData<float>(portalPrim.dataSource, _tokens->height, 1.0f);
    primvarNames.push_back(HdTokens->points);
    primvarVals.push_back(HdRetainedContainerDataSource::New(
        HdPrimvarSchemaTokens->primvarValue,
        HdRetainedTypedSampledDataSource<VtVec3fArray>::New(VtVec3fArray{
            GfVec3f(-0.5 * width, -0.5 * height, 0),
            GfVec3f( 0.5 * width, -0.5 * height, 0),
            GfVec3f( 0.5 * width,  0.5 * height, 0),
            GfVec3f(-0.5 * width,  0.5 * height, 0)
        }),
        HdPrimvarSchemaTokens->role,
        HdRetainedTypedSampledDataSource<TfToken>::New(HdPrimvarRoleTokens->point),
        HdPrimvarSchemaTokens->interpolation,
        HdRetainedTypedSampledDataSource<TfToken>::New(HdPrimvarSchemaTokens->vertex)));

    primvarNames.push_back(_tokens->isPortal);
    primvarVals.push_back(HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<bool>::New(true))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build());

    primvarNames.push_back(_tokens->portalDomeLights);
    primvarVals.push_back(HdPrimvarSchema::Builder()
        .SetPrimvarValue(HdRetainedTypedSampledDataSource<std::string>::New(
            std::string(portalPath.GetText())))
        .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
            HdPrimvarSchemaTokens->constant))
        .Build());

    names.push_back(HdPrimvarsSchemaTokens->primvars);
    sources.push_back(HdPrimvarsSchema::BuildRetained(
        primvarNames.size(),
        primvarNames.data(),
        primvarVals.data()));

    // Knock out light
    names.push_back(HdLightSchemaTokens->light);
    sources.push_back(HdBlockDataSource::New());

    // build topology
    HdContainerDataSourceHandle topologyDataSource =
    HdMeshTopologySchema::Builder()
        .SetFaceVertexCounts(
            HdRetainedTypedSampledDataSource<VtIntArray>::New(VtIntArray{4}))
        .SetFaceVertexIndices(
            HdRetainedTypedSampledDataSource<VtIntArray>::New(VtIntArray{0, 1, 2, 3}))
        .SetHoleIndices( // optional, empty array = no holes
            HdRetainedTypedSampledDataSource<VtIntArray>::New(
                VtIntArray{}))
        .SetOrientation( // "leftHanded" or "rightHanded"
            HdRetainedTypedSampledDataSource<TfToken>::New(
                _tokens->leftHanded))
        .Build();
    HdContainerDataSourceHandle meshSchema =
    HdRetainedContainerDataSource::New(
        HdTokens->topology, topologyDataSource
    );
    names.push_back(HdPrimTypeTokens->mesh);
    sources.push_back(meshSchema);

    // Knock out material
    names.push_back(HdMaterialSchemaTokens->material);
    sources.push_back(HdBlockDataSource::New());

    HdContainerDataSourceHandle handles[2] =
    {
        HdRetainedContainerDataSource::New(names.size(), names.data(), sources.data()),
        portalPrim.dataSource
    };
    return HdOverlayContainerDataSource::New(2, handles);
}

}; // namespace

//
// HD_PortalLightSceneIndex
//

/* static */
HD_PortalLightSceneIndexRefPtr
HD_PortalLightSceneIndex::New(
    const HdSceneIndexBaseRefPtr& inputSceneIndex)
{
    return TfCreateRefPtr(
        new HD_PortalLightSceneIndex(inputSceneIndex));
}

HD_PortalLightSceneIndex::HD_PortalLightSceneIndex(
    const HdSceneIndexBaseRefPtr& inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
    // Do nothing
}

HdSceneIndexPrim
HD_PortalLightSceneIndex::GetPrim(
    const SdfPath& primPath) const
{
    auto prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (isDomelight(prim, primPath))
    {
        // suppress the dome light if the dome has associated portals,
        const auto domeIt = _domesWithPortals.find(primPath);
        if (domeIt != _domesWithPortals.end() && domeIt->second)
            return HdSceneIndexPrim();
    }
    else if (prim.primType == HdPrimTypeTokens->light)
    {
        auto portalIt = _portalsToDomes.find(primPath);
        if (portalIt != _portalsToDomes.end())
        {
            return {
                HdPrimTypeTokens->light,
                _BuildPortalDataSource(
                    primPath,           // portal path
                    portalIt->second,   // dome path
                    _GetInputSceneIndex())
            };
        }
        else if (_IsPortalLight(_GetInputSceneIndex()->GetPrim(primPath), primPath))
        {
            // supress the portal prim if has no link
            return HdSceneIndexPrim();
        }
    }
    else if (primPath.GetNameToken() == _tokens->portalMesh)
    {
        const SdfPath& parentPath = primPath.GetParentPath();
        auto portalIt = _portalsToDomes.find(parentPath);
        if (portalIt != _portalsToDomes.end())
        {
            // an injected portal mesh prim
            return {
                HdPrimTypeTokens->mesh,
                _BuildPortalMehsDataSource(parentPath,
                    _GetInputSceneIndex()->GetPrim(parentPath))
            };
        }
        else
            return HdSceneIndexPrim();
    }

    return prim;
}

SdfPathVector
HD_PortalLightSceneIndex::GetChildPrimPaths(
    const SdfPath& primPath) const
{
    SdfPathVector paths = _GetInputSceneIndex()->GetChildPrimPaths(primPath);

    if (_portalMesh.count(primPath.AppendChild(_tokens->portalMesh)))
        paths.push_back(primPath.AppendChild(_tokens->portalMesh));

    return paths;
}

void
HD_PortalLightSceneIndex::_PrimsAdded(
    const HdSceneIndexBase& sender,
    const HdSceneIndexObserver::AddedPrimEntries& entries)
{
    if (!_IsObserved())
        return;

    HdSceneIndexObserver::AddedPrimEntries added;
    SdfPathSet dirtiedPortals;

    for (const auto& entry: entries)
    {
        if (isDomelight(entry.primPath, entry.primType))
        {
            const SdfPathVector& portalPaths = _AddMappingsForDome(entry.primPath);

            for (const auto& portalPath : portalPaths)
            {
                const auto& portalPrim = _GetInputSceneIndex()->GetPrim(portalPath);
                if (portalPrim.dataSource && _IsPortalLight(portalPrim, portalPath))
                {
                    // if portal already exists in the scene and now
                    // a dome light is added with need to update the
                    // portal and it's proxy mesh. So queue them for updating
                    dirtiedPortals.insert(portalPath);
                    dirtiedPortals.insert(portalPath.AppendChild(_tokens->portalMesh));
                }
            }
        }
        else if (entry.primType == HdPrimTypeTokens->light &&
            _IsPortalLight(_GetInputSceneIndex()->GetPrim(entry.primPath), entry.primPath))
        {
            SdfPath portalMeshPath = entry.primPath.AppendChild(_tokens->portalMesh);

            // if it's a new portal add portal mesh proxy
            added.emplace_back(portalMeshPath, HdPrimTypeTokens->mesh);

            // register the portal mesh
            _portalMesh.insert({portalMeshPath, true});

            if (dirtiedPortals.count(entry.primPath))
            {
                // no need to update the portals if they are being added newly
                dirtiedPortals.erase(entry.primPath);
                dirtiedPortals.erase(portalMeshPath);
            }
        }
        added.push_back(entry);
    }

    // update portals if required
    if (dirtiedPortals.size())
    {
        for (const auto& portalPath : dirtiedPortals)
         _SendPrimsDirtied({{ portalPath, { thePortalLocators }}});
    }

    _SendPrimsAdded(added);
}

void
HD_PortalLightSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase& sender,
    const HdSceneIndexObserver::RemovedPrimEntries& entries)
{
    if (!_IsObserved())
        return;

    HdSceneIndexObserver::RemovedPrimEntries removed;
    SdfPathSet dirtiedPortals;

    for (const auto& entry: entries)
    {
        if (_domesWithPortals.count(entry.primPath))
        {
            const SdfPathVector& removedPortals =
                _RemoveMappingsForDome(entry.primPath);

            for (const auto& portalPath : removedPortals)
            {
                const auto& portalPrim = _GetInputSceneIndex()->GetPrim(portalPath);
                if (portalPrim.dataSource && _IsPortalLight(portalPrim, portalPath))
                {
                    // if portal already exists in the scene and now
                    // a dome light is removed with need to update the
                    // portal and it's proxy mesh. So queue them for updating
                    dirtiedPortals.insert(portalPath);
                    dirtiedPortals.insert(portalPath.AppendChild(_tokens->portalMesh));
                }
            }
        }
        else
        {
            auto it = _portalsToDomes.find(entry.primPath);
            if (it != _portalsToDomes.end())
            {
                // if it's a portal mesh proxy, also remove it
                SdfPath portalMeshPath = entry.primPath.AppendChild(_tokens->portalMesh);

                _portalMesh.erase(portalMeshPath);

                removed.emplace_back(portalMeshPath);

                if (dirtiedPortals.count(entry.primPath))
                {
                    // no need to update the portals if they are being added newly
                    dirtiedPortals.erase(entry.primPath);
                    dirtiedPortals.erase(portalMeshPath);
                }
            }
        }
        removed.push_back(entry);
    }

    // update portals if required
    if (dirtiedPortals.size())
    {
        for (const auto& portalPath : dirtiedPortals)
         _SendPrimsDirtied({{ portalPath, { thePortalLocators }}});
    }

    _SendPrimsRemoved(removed);
}

void
HD_PortalLightSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase& sender,
    const HdSceneIndexObserver::DirtiedPrimEntries& entries)
{
    if (!_IsObserved())
        return;

    HdSceneIndexObserver::DirtiedPrimEntries dirtied;
    HdSceneIndexObserver::AddedPrimEntries added;
    HdSceneIndexObserver::RemovedPrimEntries removed;
    SdfPathSet dirtiedPortals;
    for (const auto& entry: entries)
    {
        const HdSceneIndexPrim inputPrim =
            _GetInputSceneIndex()->GetPrim(entry.primPath);
        auto domeIt = _domesWithPortals.find(entry.primPath);
        if (domeIt != _domesWithPortals.end()
            || isDomelight(inputPrim, entry.primPath))
        {
            // entry.primPath is a known dome
            bool hadPortals =
                domeIt != _domesWithPortals.end() && domeIt->second;
            bool hasPortals = hadPortals;

            if (entry.dirtyLocators.Intersects(theLightLocator)
                || domeIt == _domesWithPortals.end())
            {
                // The dome's portals may have changed.
                SdfPathVector removedPortals =
                    _RemoveMappingsForDome(entry.primPath);
                SdfPathVector currentPortals =
                    _AddMappingsForDome(entry.primPath);
                hadPortals = hadPortals || !removedPortals.empty();
                hasPortals = !currentPortals.empty();

                SdfPathSet removedPortalSet;
                SdfPathSet currentPortalSet;
                for (const SdfPath& portalPath : removedPortals)
                    removedPortalSet.insert(portalPath);
                for (const SdfPath& portalPath : currentPortals)
                    currentPortalSet.insert(portalPath);

                for (const SdfPath& portalPath : removedPortals)
                {
                    if (currentPortalSet.count(portalPath))
                    {
                        dirtiedPortals.insert(portalPath);
                    }
                    else
                    {
                        const SdfPath portalMeshPath =
                            portalPath.AppendChild(_tokens->portalMesh);
                        removed.emplace_back(portalPath);
                        removed.emplace_back(portalMeshPath);
                        _portalMesh.erase(portalMeshPath);
                    }
                }

                for (const SdfPath& portalPath : currentPortals)
                {
                    if (removedPortalSet.count(portalPath))
                        continue;

                    const auto& portalPrim =
                        _GetInputSceneIndex()->GetPrim(portalPath);
                    if (portalPrim.dataSource
                        && _IsPortalLight(portalPrim, portalPath))
                    {
                        added.emplace_back(portalPath, portalPrim.primType);
                        added.emplace_back(
                            portalPath.AppendChild(_tokens->portalMesh),
                            HdPrimTypeTokens->mesh);
                        _portalMesh.insert({
                            portalPath.AppendChild(_tokens->portalMesh),
                            true});
                    }
                }

                if (hadPortals != hasPortals)
                {
                    if (hasPortals)
                        removed.emplace_back(entry.primPath);
                    else
                        added.emplace_back(entry.primPath, inputPrim.primType);
                }
            }
            if (entry.dirtyLocators.Intersects(thePortalLocators))
            {
                // Assume that the dome's portals should be considered dirty.
                for (const auto& [portalPath, domePath]: _portalsToDomes)
                {
                    if (domePath == entry.primPath)
                        dirtiedPortals.insert(portalPath);
                }
            }
            if (!hasPortals)
                dirtied.push_back(entry);
        }
        else if (_portalsToDomes.count(entry.primPath))
        {
            if (entry.dirtyLocators.Intersects(theXformLocator))
            {
                // An xform change will affect portalToDome and portalName,
                // so we need to make sure the material data source gets dirtied.
                HdSceneIndexObserver::DirtiedPrimEntry newEntry(entry);
                newEntry.dirtyLocators.insert(theMaterialLocator);
                dirtied.push_back(newEntry);
            }
            else if (_portalsToDomes.count(entry.primPath) &&
                 entry.dirtyLocators.Intersects(theMaterialLocator))
            {
                // A material(width or height) change will affect portal mesh topology,
                // so we need to make sure the primvar data source gets dirtied.
                HdSceneIndexObserver::DirtiedPrimEntry newEntry(entry);
                newEntry.dirtyLocators = thePortalLocators;
                dirtied.push_back(newEntry);
            }
            else
                dirtied.push_back(entry);
        }
        else
            dirtied.push_back(entry);
    }

    // Check for elements of "dirtiedPortals" that are already in "dirtied".
    for (auto& entry: dirtied)
    {
        if (dirtiedPortals.find(entry.primPath) != dirtiedPortals.end())
        {
            // If the portal is already in the dirtied vector, we don't want to
            // add it again.
            dirtiedPortals.erase(entry.primPath);
            // We do, however, need to invalidate the portal data sources.
            entry.dirtyLocators.insert(thePortalLocators);
        }
    }

    for (const auto& portalPath : dirtiedPortals)
        dirtied.emplace_back(portalPath, thePortalLocators);

    // also add portal meshes
    HdSceneIndexObserver::DirtiedPrimEntries dirtiedPortalsMesh;
    for (auto& dirtyEntry : dirtied)
    {
        auto prim = _GetInputSceneIndex()->GetPrim(dirtyEntry.primPath);
        if (_IsPortalLight(prim, dirtyEntry.primPath))
        {
            dirtiedPortalsMesh.push_back({
                dirtyEntry.primPath.AppendChild(_tokens->portalMesh),
                thePortalLocators
            });
        }
    }
    for (auto& dirtiedPortalMesh : dirtiedPortalsMesh)
        dirtied.push_back(dirtiedPortalMesh);

    if (!removed.empty())
        _SendPrimsRemoved(removed);
    if (!added.empty())
        _SendPrimsAdded(added);
    _SendPrimsDirtied(dirtied);
}

SdfPathVector
HD_PortalLightSceneIndex::_AddMappingsForDome(
    const SdfPath& domePrimPath)
{
    const auto domePrim = _GetInputSceneIndex()->GetPrim(domePrimPath);

    if (!isDomelight(domePrim, domePrimPath))
    {
        // Caller should have already confirmed this is a dome.
        TF_CODING_ERROR("_AddMappingsForDome invoked for non-"
                        "domeLight path <%s>", domePrimPath.GetText());
        return SdfPathVector();
    }

    SdfPathVector portalPaths = _GetPortalPaths(domePrim.dataSource);

    _domesWithPortals[domePrimPath] = !portalPaths.empty();

    for (const auto& portalPath: portalPaths)
    {
        const auto it = _portalsToDomes.insert({portalPath,domePrimPath}).first;
        if (it->second != domePrimPath)
        {
            TF_WARN("Failed to register <%s> as a portal light for <%s>. "
                    "The portal is already in use with <%s> and cannot be "
                    "reused with another dome light.",
                    portalPath.GetText(), domePrimPath.GetText(),
                    it->second.GetText());
        }
    }

    return portalPaths;
}

SdfPathVector
HD_PortalLightSceneIndex::_RemoveMappingsForDome(
    const SdfPath& domePrimPath)
{
    SdfPathVector portalPaths;
    auto domeIt = _domesWithPortals.find(domePrimPath);

    // _domesWithPortals is only a cache. If it becomes stale false, the
    // authoritative mapping still lives in _portalsToDomes and must be removed.
    for (auto it = _portalsToDomes.begin(); it != _portalsToDomes.end();)
    {
        if (it->second == domePrimPath)
        {
            portalPaths.push_back(it->first);
            it = _portalsToDomes.erase(it);
        }
        else
        {
            it++;
        }
    }

    if (domeIt != _domesWithPortals.end())
        _domesWithPortals.erase(domeIt);

    return portalPaths;
}

bool
HD_PortalLightSceneIndex::isDomelight(const HdSceneIndexPrim& prim, const SdfPath& primPath) const
{
    return (prim.primType == HdPrimTypeTokens->domeLight ||
           (prim.primType == HdPrimTypeTokens->light && _IsPhysicalSky(prim, primPath)));
}

bool
HD_PortalLightSceneIndex::isDomelight(const SdfPath& primPath, const TfToken& primType) const
{
    return ((primType == HdPrimTypeTokens->domeLight) ||
            (primType == HdPrimTypeTokens->light    &&
             _IsPhysicalSky(_GetInputSceneIndex()->GetPrim(primPath), primPath)));
}

PXR_NAMESPACE_CLOSE_SCOPE
