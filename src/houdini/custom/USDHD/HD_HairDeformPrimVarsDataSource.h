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

#include "HD_HairDeformUtils.h"

#include <UT/UT_Array.h>
#include <UT/UT_ConcurrentHashMap.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_Set.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_Tracing.h>

#include "pxr/usd/sdf/path.h"
#include <pxr/imaging/hd/overlayContainerDataSource.h>

PXR_NAMESPACE_OPEN_SCOPE

class HD_HairDeformPrimVarsDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(HD_HairDeformPrimVarsDataSource);

    TfTokenVector GetNames() override;

    HdDataSourceBaseHandle Get(const TfToken &name) override;

private:
    HD_HairDeformPrimVarsDataSource(
            SdfPath primpath,
            HdContainerDataSourceHandle primds,
            SdfPath deformerprimpath,
            HdContainerDataSourceHandle deformerds,
            SdfPath skinprimpath,
            HdContainerDataSourceHandle skinds,
            SdfPath guideinterpprimpath,
            HdContainerDataSourceHandle guideinterpds,
            HdDataSourceBaseHandle pointsds,
            PrimVarCacheMapPtr cachemap,
            DeformerCacheMapPtr deformercachemap,
            SkinMeshCacheMapPtr skinmeshcachemap,
            RestPointsCacheMapPtr restpointscachemap,
            SurfaceTopoCacheMapPtr surfacetopocachemap,
            CurveSkinCaptureCacheMapPtr maincurveskincapturecachemap,
            CurveSkinCaptureCacheMapPtr deformercurveskincapturecachemap,
            GuideInterpCacheMapPtr guideinterpcachemap,
            GIMSurfaceTopoCacheMapPtr gimsurfacetopocachemap,
            PointDeformCaptureCacheMapPtr pointdeformcapturecachemap,
            SkinSubdEvalCacheMapPtr skinsubdcachemap,
            ClumpTopoCacheMapPtr clumptopocachemap,
            OrientAttribsCacheMapPtr orientattribscachemap)
        : _primpath(primpath)
        , _primds(primds)
        , _deformerprimpath(deformerprimpath)
        , _deformerds(deformerds)
        , _skinprimpath(skinprimpath)
        , _skinds(skinds)
        , _guideinterpprimpath(guideinterpprimpath)
        , _guideinterpds(guideinterpds)
        , _pointsds(pointsds)
        , _cachemap(cachemap)
        , _deformercachemap(deformercachemap)
        , _skinmeshcachemap(skinmeshcachemap)
        , _restpointscachemap(restpointscachemap)
        , _surfacetopocachemap(surfacetopocachemap)
        , _maincurveskincapturecachemap(maincurveskincapturecachemap)
        , _deformercurveskincapturecachemap(deformercurveskincapturecachemap)
        , _guideinterpcachemap(guideinterpcachemap)
        , _gimsurfacetopocachemap(gimsurfacetopocachemap)
        , _pointdeformcapturecachemap(pointdeformcapturecachemap)
        , _skinsubdcachemap(skinsubdcachemap)
        , _clumptopocachemap(clumptopocachemap)
        , _orientattribscachemap(orientattribscachemap)
    {
    }

    static std::string getShaftAttribName(const std::string &name);

    static std::pair<std::string, std::string> getBarbAttribNames(
            const std::string &name);

    HdOverlayContainerDataSourceHandle makePrimvarOverlay(
            HdDataSourceBaseHandle primvar_value,
            HdContainerDataSourceHandle container);

    SdfPath _primpath;
    HdContainerDataSourceHandle _primds;
    SdfPath _deformerprimpath;
    HdContainerDataSourceHandle _deformerds;
    SdfPath _skinprimpath;
    HdContainerDataSourceHandle _skinds;
    SdfPath _guideinterpprimpath;
    HdContainerDataSourceHandle _guideinterpds;
    HdDataSourceBaseHandle _pointsds;
    PrimVarCacheMapPtr _cachemap;
    DeformerCacheMapPtr _deformercachemap;
    SkinMeshCacheMapPtr _skinmeshcachemap;
    RestPointsCacheMapPtr _restpointscachemap;
    SurfaceTopoCacheMapPtr _surfacetopocachemap;
    CurveSkinCaptureCacheMapPtr _maincurveskincapturecachemap;
    CurveSkinCaptureCacheMapPtr _deformercurveskincapturecachemap;
    GuideInterpCacheMapPtr _guideinterpcachemap;
    GIMSurfaceTopoCacheMapPtr _gimsurfacetopocachemap;
    PointDeformCaptureCacheMapPtr _pointdeformcapturecachemap;
    SkinSubdEvalCacheMapPtr _skinsubdcachemap;
    ClumpTopoCacheMapPtr _clumptopocachemap;
    OrientAttribsCacheMapPtr _orientattribscachemap;
};

PXR_NAMESPACE_CLOSE_SCOPE
