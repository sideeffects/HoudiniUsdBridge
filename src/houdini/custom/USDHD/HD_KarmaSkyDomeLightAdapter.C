//
// Copyright 2018 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "HD_KarmaSkyDomeLightAdapter.h"

#include <HUSD/XUSD_Tokens.h>
#include "pxr/usdImaging/usdImaging/dataSourceGprim.h"
#include "pxr/usdImaging/usdImaging/delegate.h"
#include "pxr/usdImaging/usdImaging/indexProxy.h"
#include "pxr/usdImaging/usdImaging/tokens.h"
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/lightSchema.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/usd/relationship.h"

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

class hd_KarmaSkyPortalsDataSource final
    : public HdTypedSampledDataSource<SdfPathVector>
{
public:
    HD_DECLARE_DATASOURCE(hd_KarmaSkyPortalsDataSource);

    explicit hd_KarmaSkyPortalsDataSource(const UsdRelationship& portalsRel)
        : _portalsRel(portalsRel)
    {
    }

    SdfPathVector GetTypedValue(Time /*shutterOffset*/) override
    {
        SdfPathVector portals;
        if (_portalsRel)
            _portalsRel.GetForwardedTargets(&portals);
        return portals;
    }

    VtValue GetValue(Time shutterOffset) override
    {
        return VtValue(GetTypedValue(shutterOffset));
    }

    bool GetContributingSampleTimesForInterval(
        Time /*startTime*/,
        Time /*endTime*/,
        std::vector<Time>* /*outSampleTimes*/) override
    {
        return false;
    }

private:
    UsdRelationship _portalsRel;
};

} // namespace

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, USD_HD_KarmaSkyDomeLightAdapter)
{
    typedef HD_KarmaSkyDomeLightAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
}

TfTokenVector
HD_KarmaSkyDomeLightAdapter::GetImagingSubprims(UsdPrim const& prim)
{
    return { TfToken() };
}

TfToken
HD_KarmaSkyDomeLightAdapter::GetImagingSubprimType(
        UsdPrim const& prim,
        TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
        return HdPrimTypeTokens->light;
    }
    return TfToken();
}

HdContainerDataSourceHandle
HD_KarmaSkyDomeLightAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
{
    if (!subprim.IsEmpty())
        return nullptr;

    HdContainerDataSourceHandle baseDataSource =
        BaseAdapter::GetImagingSubprimData(prim, subprim, stageGlobals);

    HdSampledDataSourceHandle portalsDataSource;
    if (UsdRelationship portalsRel = prim.GetRelationship(HdTokens->portals))
    {
        portalsDataSource = hd_KarmaSkyPortalsDataSource::New(portalsRel);
    }
    else
    {
        portalsDataSource =
            HdRetainedTypedSampledDataSource<SdfPathVector>::New(
                SdfPathVector());
    }

    HdContainerDataSourceHandle lightDataSource =
        HdRetainedContainerDataSource::New(
            HdLightSchemaTokens->light,
            HdRetainedContainerDataSource::New(
                HdTokens->portals,
                portalsDataSource));

    return HdOverlayContainerDataSource::New(lightDataSource, baseDataSource);
}

HdDataSourceLocatorSet
HD_KarmaSkyDomeLightAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType)
{
    HdDataSourceLocatorSet locators =
        BaseAdapter::InvalidateImagingSubprim(
            prim, subprim, properties, invalidationType);

    if (subprim.IsEmpty()
        && std::find(properties.begin(), properties.end(), HdTokens->portals)
            != properties.end())
    {
        // The portal scene index rebuilds dome/portal mappings when the
        // light container is dirtied. Include the parent locator so adding or
        // removing this relationship behaves like a native dome light.
        locators.insert(HdLightSchema::GetDefaultLocator());
        locators.insert(
            HdLightSchema::GetDefaultLocator().Append(HdTokens->portals));
    }

    return locators;
}

PXR_NAMESPACE_CLOSE_SCOPE
