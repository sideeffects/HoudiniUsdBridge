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

#include "HD_HairDeformPointsDataSource.h"
#include "HD_HairDeformPrimVarsDataSource.h"
#include "HD_HairDeformSceneIndex.h"
#include "HD_HairDeformSchema.h"
#include "HD_HairDeformUtils.h"

#include <UT/UT_Array.h>
#include <UT/UT_ConcurrentHashMap.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_Tracing.h>
#include <UT/UT_WorkBuffer.h>
#include <SYS/SYS_Compiler.h>
#include <SYS/SYS_Math.h>

#include <pxr/imaging/hd/basisCurvesSchema.h>
#include <pxr/imaging/hd/basisCurvesTopologySchema.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/extentSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/meshTopologySchema.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tetMeshTopologySchema.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{

const TfToken thePosName("P");
const TfToken theUvName("uv");
const TfToken theStName("st");
const TfToken theWidthName("width");
const TfToken theRestName("rest");
const TfToken theGuidesName("guides");
const TfToken theGuidesLengthsName("guides:lengths");
const TfToken theWeightsName("weights");
const TfToken theWeightsLengthsName("weights:lengths");

const auto &thePrimvarsLocator = HdPrimvarsSchema::GetDefaultLocator();
const auto &thePointsLocator
        = thePrimvarsLocator.Append(HdDataSourceLocator(HdTokens->points));
const auto &theRestPointsLocator
        = thePrimvarsLocator.Append(HdDataSourceLocator(theRestName));
const auto &theGuidesLocator
        = thePrimvarsLocator.Append(HdDataSourceLocator(theGuidesName));
const auto &theGuidesLengthsLocator
        = thePrimvarsLocator.Append(HdDataSourceLocator(theGuidesLengthsName));
const auto &theWeightsLocator
        = thePrimvarsLocator.Append(HdDataSourceLocator(theWeightsName));
const auto &theWeightsLengthsLocator
        = thePrimvarsLocator.Append(HdDataSourceLocator(theWeightsLengthsName));
const auto &theXformLocator = HdXformSchema::GetDefaultLocator();
const auto &theExtentLocator = HdExtentSchema::GetDefaultLocator();

const auto &theBasisCurvesTopologyLocator
        = HdBasisCurvesTopologySchema::GetDefaultLocator();
const auto &theMeshTopologyLocator = HdMeshTopologySchema::GetDefaultLocator();
const auto &theTetMeshTopologyLocator
        = HdTetMeshTopologySchema::GetDefaultLocator();
const auto &theSubdivisionSchemeLocator
        = HdMeshSchema::GetSubdivisionSchemeLocator();

const auto &theFeatherPBarblLocator
        = thePrimvarsLocator.Append(HdDataSourceLocator(TfToken("P_barbl")));
const auto &theFeatherPBarbrLocator
        = thePrimvarsLocator.Append(HdDataSourceLocator(TfToken("P_barbr")));
const auto &theFeatherBarbOrientLocator
        = thePrimvarsLocator.Append(HdDataSourceLocator(TfToken("barborient")));

HdDataSourceLocator theHairDeformLoc(HairDeformSchemaTokens->houdiniHairDeform);
HdDataSourceLocator theSkinPrimLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->skinPrim));
HdDataSourceLocator theGuideInterpMeshPrimLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->guideInterpMeshPrim));
HdDataSourceLocator theDeformerPrimLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->deformerPrim));
HdDataSourceLocator thePreserveShapeEnableLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveshapeenable));
HdDataSourceLocator thePreserveShapeIterationsLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveshapeiterations));
HdDataSourceLocator thePreserveShapeLockRootsLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveshapelockroots));
HdDataSourceLocator thePreserveShapeKStretchLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveshapekstretch));
HdDataSourceLocator thePreserveShapeKBendLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveshapekbend));
HdDataSourceLocator thePreserveShapeRefPosStrengthLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveshaperefposstrength));
HdDataSourceLocator thePreserveClumpsStiffnessLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveclumpsstiffness));
HdDataSourceLocator thePreserveClumpsDampingLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveclumpsdamping));
HdDataSourceLocator thePreserveClumpsEnableLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveclumpsenable));
HdDataSourceLocator thePreserveClumpsMaxNeighborsLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveclumpsmaxneighbors));
HdDataSourceLocator thePreserveClumpsMaxConstraintsLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->preserveclumpsmaxconstraints));
HdDataSourceLocator theDeformMethodLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->deformmethod));
HdDataSourceLocator theCaptureRadiusLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->captureradius));
HdDataSourceLocator theCaptureMaxPointsLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->capturemaxpoints));
HdDataSourceLocator theCaptureMinPointsLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->captureminpoints));
HdDataSourceLocator theSmoothCaptureLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->smoothcapture));
HdDataSourceLocator theSmoothCaptureRadiusLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->smoothcaptureradius));
HdDataSourceLocator theKernelTypeLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->kerneltype));
HdDataSourceLocator theSmoothingMethodLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->smoothingmethod));
HdDataSourceLocator theSmoothingLevelLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->smoothinglevel));
HdDataSourceLocator theTetMeshTreatmentLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->tetmeshtreatment));
HdDataSourceLocator theUseOrientAttribLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->useorientattrib));
HdDataSourceLocator theOrientBlendLoc(
        theHairDeformLoc.Append(HairDeformSchemaTokens->orientblend));

static const HdDataSourceLocator theOrientLocator(
        HdPrimvarsSchemaTokens->primvars, TfToken("orient"));
static const HdDataSourceLocator theRestOrientLocator(
        HdPrimvarsSchemaTokens->primvars, TfToken("restorient"));

const HdDataSourceLocatorSet thePreserveShapeLocators = {
        thePreserveShapeEnableLoc, thePreserveShapeIterationsLoc,
        thePreserveShapeLockRootsLoc, thePreserveShapeKStretchLoc,
        thePreserveShapeKBendLoc,
        thePreserveShapeRefPosStrengthLoc, thePreserveClumpsStiffnessLoc,
        thePreserveClumpsDampingLoc, thePreserveClumpsEnableLoc,
        thePreserveClumpsMaxNeighborsLoc, thePreserveClumpsMaxConstraintsLoc};

const HdDataSourceLocatorSet theHairDeformLocators = {
        theSkinPrimLoc, theGuideInterpMeshPrimLoc, thePreserveShapeEnableLoc,
        thePreserveShapeIterationsLoc, thePreserveShapeLockRootsLoc,
        thePreserveShapeKStretchLoc,
        thePreserveShapeKBendLoc, thePreserveShapeRefPosStrengthLoc,
        thePreserveClumpsStiffnessLoc, thePreserveClumpsDampingLoc,
        thePreserveClumpsEnableLoc, thePreserveClumpsMaxNeighborsLoc,
        thePreserveClumpsMaxConstraintsLoc,
        theDeformMethodLoc};

const HdDataSourceLocatorSet theGroomRelationshipLocators = {
        theSkinPrimLoc, theGuideInterpMeshPrimLoc, theDeformerPrimLoc};

const HdDataSourceLocatorSet theGroomLocators = {
        thePointsLocator, theExtentLocator, theSkinPrimLoc,
        thePreserveShapeEnableLoc,
        thePreserveShapeIterationsLoc, thePreserveShapeLockRootsLoc,
        thePreserveShapeKStretchLoc,
        thePreserveShapeKBendLoc, thePreserveShapeRefPosStrengthLoc,
        thePreserveClumpsStiffnessLoc, thePreserveClumpsDampingLoc,
        thePreserveClumpsEnableLoc, thePreserveClumpsMaxNeighborsLoc,
        thePreserveClumpsMaxConstraintsLoc,
        theUseOrientAttribLoc, theOrientBlendLoc};
const HdDataSourceLocatorSet theDeformMethodLocators = {theDeformMethodLoc};

const HdDataSourceLocatorSet theCaptureParamLocators = {
        theCaptureRadiusLoc, theCaptureMaxPointsLoc, theCaptureMinPointsLoc,
        theSmoothCaptureLoc, theSmoothCaptureRadiusLoc, theKernelTypeLoc,
        theSmoothingMethodLoc, theSmoothingLevelLoc, theTetMeshTreatmentLoc};

const HdDataSourceLocatorSet theTopoLocators = {
        theBasisCurvesTopologyLocator, theMeshTopologyLocator,
        theTetMeshTopologyLocator, theSubdivisionSchemeLocator};

const HdDataSourceLocatorSet theSkinLocators = {
        thePointsLocator, theXformLocator, theMeshTopologyLocator,
        theTetMeshTopologyLocator, theSubdivisionSchemeLocator};

const HdDataSourceLocatorSet thePointDeformDeformerLocators = {
        thePointsLocator, theXformLocator, theMeshTopologyLocator,
        theTetMeshTopologyLocator,
        theOrientLocator, theRestOrientLocator};

const HdDataSourceLocatorSet theRestSkinLocators = {
        theRestPointsLocator, theXformLocator, theMeshTopologyLocator,
        theTetMeshTopologyLocator, theSubdivisionSchemeLocator};

const HdDataSourceLocatorSet theFeatherRestLocators = {
        theBasisCurvesTopologyLocator, thePointsLocator,
        theFeatherPBarblLocator, theFeatherPBarbrLocator,
        theFeatherBarbOrientLocator};

const HdDataSourceLocatorSet theGuideWeightsPrimvarLocators = {
        theGuidesLocator, theGuidesLengthsLocator, theWeightsLocator,
        theWeightsLengthsLocator};

// NOTE: Using specific primvar locators instead of broad thePrimvarsLocator
// to avoid invalidating guide interp cache on unrelated primvar changes (e.g. animated P)
const HdDataSourceLocatorSet theGuideInterpLocators = {
        theMeshTopologyLocator, theXformLocator,
        theGuidesLocator, theGuidesLengthsLocator, theWeightsLocator,
        theWeightsLengthsLocator};

// Point deform capture invalidation: rest positions or topology changes
const HdDataSourceLocatorSet thePointDeformRestLocators = {
        theRestPointsLocator, theXformLocator, theMeshTopologyLocator,
        theTetMeshTopologyLocator};

} // namespace

class HD_HairDeformCurveVertexCountsDataSource
    : public HdTypedSampledDataSource<VtIntArray>
{
public:
    HD_DECLARE_DATASOURCE(HD_HairDeformCurveVertexCountsDataSource);

    VtValue GetValue(const Time shutterOffset) override
    {
        return VtValue(GetTypedValue(shutterOffset));
    }

    VtIntArray GetTypedValue(const Time shutterOffset) override
    {
        utZoneScoped;
        utZoneTextSH(UT_StringHolder("curvevertexcounts"));
        auto curveschema = HdBasisCurvesSchema::GetFromParent(_primds);
        if (!curveschema)
            return VtIntArray();

        HdPrimvarsSchema pvschema = HdPrimvarsSchema::GetFromParent(_primds);
        if (!pvschema)
            return VtIntArray();

        HdBasisCurvesTopologySchema toposchema = curveschema.GetTopology();
        const VtArray<int> curvecounts
                = toposchema.GetCurveVertexCounts()->GetTypedValue(0.0f);

        HdPrimvarSchema barbl = pvschema.GetPrimvar(TfToken("P_barbl"));
        HdPrimvarSchema barbr = pvschema.GetPrimvar(TfToken("P_barbr"));

        if (barbl && barbr)
        {
            int nbarblvals
                    = barbl.GetPrimvarValue()->GetValue(0.0f).GetArraySize();
            int nbarbrvals
                    = barbr.GetPrimvarValue()->GetValue(0.0f).GetArraySize();

            int ncurves = curvecounts.size();

            VtArray<int> newcurvecounts;

            exint npts = 0;
            for (int count : curvecounts)
                npts += count;

            int nbarblpts = nbarblvals / (3 * npts);
            int nbarbrpts = nbarbrvals / (3 * npts);

            HD_HairDeformUtils::resizeUninitialized(newcurvecounts, ncurves + 2 * npts);

            std::copy(
                    curvecounts.begin(), curvecounts.end(),
                    newcurvecounts.begin());

            for (int i = 0; i < npts; ++i)
            {
                newcurvecounts[i + ncurves] = nbarblpts;
                newcurvecounts[i + ncurves + npts] = nbarbrpts;
            }

            return newcurvecounts;
        }
        else
        {
            return curvecounts;
        }
    }

    bool GetContributingSampleTimesForInterval(
            const Time startTime,
            const Time endTime,
            std::vector<Time> *const outSampleTimes) override
    {
        return false;
    }

private:
    HD_HairDeformCurveVertexCountsDataSource(HdContainerDataSourceHandle primds)
        : _primds(primds)
    {
    }

    HdContainerDataSourceHandle _primds;
};

class HD_HairDeformExtentDataSource
    : public HdTypedSampledDataSource<GfVec3d>
{
public:
    HD_DECLARE_DATASOURCE(HD_HairDeformExtentDataSource);

    VtValue GetValue(const Time shutterOffset) override
    {
        return VtValue(GetTypedValue(shutterOffset));
    }

    GfVec3d GetTypedValue(const Time shutterOffset) override
    {
        utZoneScoped;
        UT_StringHolder zonetext;
        zonetext.format("extents: {}", shutterOffset);
        utZoneTextSH(UT_StringHolder(zonetext.buffer()));

        VtVec3fArray pts = _pointsds->GetTypedValue(shutterOffset);
        if (pts.empty())
            return GfVec3d(0);

        GfVec3f lo = pts[0], hi = pts[0];
        const exint npts = pts.size();
        for (exint i = 1; i < npts; ++i)
        {
            const GfVec3f &p = pts[i];
            lo[0] = SYSmin(lo[0], p[0]);
            lo[1] = SYSmin(lo[1], p[1]);
            lo[2] = SYSmin(lo[2], p[2]);
            hi[0] = SYSmax(hi[0], p[0]);
            hi[1] = SYSmax(hi[1], p[1]);
            hi[2] = SYSmax(hi[2], p[2]);
        }

        return _isMin ? GfVec3d(lo[0], lo[1], lo[2])
                       : GfVec3d(hi[0], hi[1], hi[2]);
    }

    bool GetContributingSampleTimesForInterval(
            const Time startTime,
            const Time endTime,
            std::vector<Time> *const outSampleTimes) override
    {
        return _pointsds->GetContributingSampleTimesForInterval(
                startTime, endTime, outSampleTimes);
    }

private:
    HD_HairDeformExtentDataSource(
            HD_HairDeformPointsDataSource::Handle pointsds,
            bool isMin)
        : _pointsds(pointsds)
        , _isMin(isMin)
    {
    }

    HD_HairDeformPointsDataSource::Handle _pointsds;
    bool _isMin;
};

HdContainerDataSourceHandle
HD_HairDeformSceneIndex::_BuildOverlayDataSource(
        const SdfPath &prim_path,
        const HdSceneIndexPrim &prim) const
{
    utZoneScoped;

    HairDeformSchema hairdeformschema
            = HairDeformSchema::GetFromParent(prim.dataSource);
    if (!hairdeformschema)
        return prim.dataSource;

    SdfPath skin_path;
    HdContainerDataSourceHandle skinds;
    if (HdPathArrayDataSourceHandle relds = hairdeformschema.GetSkinPrims())
    {
        VtArray<SdfPath> skinprims = relds->GetTypedValue(0);
        if (skinprims.size())
        {
            auto skinprim = _GetInputSceneIndex()->GetPrim(skinprims[0]);
            if (skinprim.dataSource)
            {
                skin_path = skinprims[0];
                skinds = skinprim.dataSource;
            }
        }
    }

    SdfPath deformer_path;
    HdContainerDataSourceHandle deformerds;
    if (HdPathArrayDataSourceHandle defrelds = hairdeformschema.GetDeformerPrims())
    {
        VtArray<SdfPath> defprims = defrelds->GetTypedValue(0);
        if (defprims.size())
        {
            auto defprim = _GetInputSceneIndex()->GetPrim(defprims[0]);
            if (defprim.dataSource)
            {
                deformer_path = defprims[0];
                deformerds = defprim.dataSource;
            }
        }
    }

    SdfPath guideinterp_path;
    HdContainerDataSourceHandle guideinterpds;
    if (HdPathArrayDataSourceHandle girelds = hairdeformschema.GetGuideInterpMeshPrims())
    {
        VtArray<SdfPath> gimprims = girelds->GetTypedValue(0);
        if (gimprims.size())
        {
            auto gimprim = _GetInputSceneIndex()->GetPrim(gimprims[0]);
            if (gimprim.dataSource)
            {
                guideinterp_path = gimprims[0];
                guideinterpds = gimprim.dataSource;
            }
        }
    }

    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> sources;

    auto pointsds = HD_HairDeformPointsDataSource::New(
            prim_path, prim.dataSource, deformer_path, deformerds,
            skin_path, skinds, guideinterp_path, guideinterpds,
            _deformercachemap,
            _skinmeshcachemap,
            _restpointscachemap,
            _surfacetopocachemap,
            _maincurveskincapturecachemap,
            _deformercurveskincapturecachemap,
            _guideinterpcachemap,
            _gimsurfacetopocachemap,
            _pointdeformcapturecachemap,
            _skinsubdcachemap,
            _clumptopocachemap,
            _orientattribscachemap);

    names.push_back(HdPrimvarsSchemaTokens->primvars);
    sources.push_back(HD_HairDeformPrimVarsDataSource::New(
            prim_path, prim.dataSource, deformer_path, deformerds,
            skin_path, skinds, guideinterp_path, guideinterpds,
            pointsds,
            _cachemap,
            _deformercachemap,
            _skinmeshcachemap,
            _restpointscachemap,
            _surfacetopocachemap,
            _maincurveskincapturecachemap,
            _deformercurveskincapturecachemap,
            _guideinterpcachemap,
            _gimsurfacetopocachemap,
            _pointdeformcapturecachemap,
            _skinsubdcachemap,
            _clumptopocachemap,
            _orientattribscachemap));

    names.push_back(HdExtentSchemaTokens->extent);
    sources.push_back(
            HdExtentSchema::Builder()
                .SetMin(HD_HairDeformExtentDataSource::New(pointsds, true))
                .SetMax(HD_HairDeformExtentDataSource::New(pointsds, false))
                .Build());

    auto curveschema = HdBasisCurvesSchema::GetFromParent(prim.dataSource);

    HdBasisCurvesTopologySchema toposchema = curveschema.GetTopology();

    auto basiscurvessrc = HdRetainedContainerDataSource::New(
            HdBasisCurvesSchemaTokens->topology,
            HdRetainedContainerDataSource::New(
                    HdBasisCurvesTopologySchemaTokens->curveVertexCounts,
                    HD_HairDeformCurveVertexCountsDataSource::New(
                            prim.dataSource)));
    names.push_back(HdBasisCurvesSchemaTokens->basisCurves);
    sources.push_back(basiscurvessrc);

    // Reset groom xform to identity when deformation is active, since
    // deformed positions incorporate deformer/skin prim transforms.
    if (auto dm = hairdeformschema.GetDeformMethod())
    {
        std::string deformmethodstr = dm->GetTypedValue(0.0f);
        if (!deformmethodstr.empty())
        {
            names.push_back(HdXformSchemaTokens->xform);
            sources.push_back(
                HdXformSchema::Builder()
                    .SetMatrix(
                        HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                            GfMatrix4d(1.0)))
                    .SetResetXformStack(
                        HdRetainedTypedSampledDataSource<bool>::New(true))
                    .Build());
        }
    }

    HdContainerDataSourceHandle handles[2] = {
            HdRetainedContainerDataSource::New(
                    names.size(), names.data(), sources.data()),
            prim.dataSource};
    return HdOverlayContainerDataSource::New(2, handles);
}

/* static */
HD_HairDeformSceneIndexRefPtr
HD_HairDeformSceneIndex::New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    return TfCreateRefPtr(new HD_HairDeformSceneIndex(inputSceneIndex));
}

HD_HairDeformSceneIndex::HD_HairDeformSceneIndex(
        const HdSceneIndexBaseRefPtr &inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    , _cachemap(std::make_shared<PrimVarCacheMapType>())
    , _deformercachemap(std::make_shared<DeformerCacheMapType>())
    , _skinmeshcachemap(std::make_shared<SkinMeshCacheMapType>())
    , _restpointscachemap(std::make_shared<RestPointsCacheMapType>())
    , _surfacetopocachemap(std::make_shared<SurfaceTopoCacheMapType>())
    , _maincurveskincapturecachemap(
            std::make_shared<CurveSkinCaptureCacheMapType>())
    , _deformercurveskincapturecachemap(
            std::make_shared<CurveSkinCaptureCacheMapType>())
    , _guideinterpcachemap(std::make_shared<GuideInterpCacheMapType>())
    , _gimsurfacetopocachemap(std::make_shared<GIMSurfaceTopoCacheMapType>())
    , _pointdeformcapturecachemap(std::make_shared<PointDeformCaptureCacheMapType>())
    , _skinsubdcachemap(std::make_shared<SkinSubdEvalCacheMapType>())
    , _clumptopocachemap(std::make_shared<ClumpTopoCacheMapType>())
    , _orientattribscachemap(std::make_shared<OrientAttribsCacheMapType>())
{
}

HdSceneIndexPrim
HD_HairDeformSceneIndex::GetPrim(const SdfPath &primPath) const
{
    utZoneScoped;
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    HairDeformSchema hairdeformschema
            = HairDeformSchema::GetFromParent(prim.dataSource);
    if (hairdeformschema)
        prim.dataSource = _BuildOverlayDataSource(primPath, prim);

    return prim;
}

SdfPathVector
HD_HairDeformSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const
{
    SdfPathVector paths = _GetInputSceneIndex()->GetChildPrimPaths(primPath);

    return paths;
}

// Relationship & cache helper methods

void
HD_HairDeformSceneIndex::_ClearPrimVarCache(const SdfPath &primpath)
{
    UT_StringHolder prefix
            = HD_HairDeformUtils::makePrimVarCachePrefix(primpath.GetText());

    UT_StringArray erase_keys;
    for (auto it = _cachemap->begin(); it != _cachemap->end(); ++it)
    {
        if (it->first.startsWith(prefix))
            erase_keys.append(it->first);
    }

    for (const UT_StringHolder &key : erase_keys)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR primvar '{}'", key);
        _cachemap->erase(key);
    }
}

void
HD_HairDeformSceneIndex::_DetachGroom(const SdfPath &groomPath)
{
    _grooms.erase(groomPath);

    // Detach from skin
    auto skinIt = _groomtoskinmap.find(groomPath);
    if (skinIt != _groomtoskinmap.end())
    {
        HD_HairDeformUtils::cacheLog("HairDeform: detaching groom '{}' from skin '{}'",
                groomPath.GetText(), skinIt->second.GetText());
        if (_RemoveFromForwardMap(
                _skintogroommap, skinIt->second, groomPath))
            _ClearSkinMeshCache(skinIt->second);
        _groomtoskinmap.erase(skinIt);
    }

    // Detach from GIM
    auto gimIt = _groomtoguideinterpmeshmap.find(groomPath);
    if (gimIt != _groomtoguideinterpmeshmap.end())
    {
        HD_HairDeformUtils::cacheLog("HairDeform: detaching groom '{}' from GIM '{}'",
                groomPath.GetText(), gimIt->second.GetText());
        _RemoveFromForwardMap(
                _guideinterpmeshtogroommap, gimIt->second, groomPath);
        _groomtoguideinterpmeshmap.erase(gimIt);
        _ClearGuideInterpCache(groomPath);
        _ClearGIMSurfaceTopoCache(groomPath);
    }

    // Detach from point deform deformer
    auto pdIt = _groomtopointdeformmap.find(groomPath);
    if (pdIt != _groomtopointdeformmap.end())
    {
        HD_HairDeformUtils::cacheLog("HairDeform: detaching groom '{}' from point deform '{}'",
                groomPath.GetText(), pdIt->second.GetText());
        if (_RemoveFromForwardMap(
                _pointdeformtogroommap, pdIt->second, groomPath))
            _ClearDeformerCache(pdIt->second);
        _groomtopointdeformmap.erase(pdIt);
        _ClearPointDeformCaptureCache(groomPath);
    }

    _ClearPrimVarCache(groomPath);
    _ClearRestPointsCache(groomPath);
    _ClearSurfaceTopoCache(groomPath);
    _ClearCurveSkinCaptureCache(groomPath);
    _ClearSkinSubdEvalCache(groomPath);
}

void
HD_HairDeformSceneIndex::_AttachGroom(
        const SdfPath &groomPath,
        HairDeformSchema &schema)
{
    _grooms.insert(groomPath);

    if (HdPathArrayDataSourceHandle relds = schema.GetSkinPrims())
    {
        VtArray<SdfPath> skinprims = relds->GetTypedValue(0);
        if (skinprims.size())
        {
            HD_HairDeformUtils::cacheLog("HairDeform: attaching groom '{}' to skin '{}'",
                    groomPath.GetText(), skinprims[0].GetText());
            _skintogroommap[skinprims[0]].insert(groomPath);
            _groomtoskinmap[groomPath] = skinprims[0];
        }
    }

    if (HdPathArrayDataSourceHandle girelds = schema.GetGuideInterpMeshPrims())
    {
        VtArray<SdfPath> gimprims = girelds->GetTypedValue(0);
        if (gimprims.size())
        {
            HD_HairDeformUtils::cacheLog("HairDeform: attaching groom '{}' to GIM '{}'",
                    groomPath.GetText(), gimprims[0].GetText());
            _guideinterpmeshtogroommap[gimprims[0]].insert(groomPath);
            _groomtoguideinterpmeshmap[groomPath] = gimprims[0];
        }
    }

    if (HdPathArrayDataSourceHandle defrelds = schema.GetDeformerPrims())
    {
        VtArray<SdfPath> defprims = defrelds->GetTypedValue(0);
        if (defprims.size())
        {
            HD_HairDeformUtils::cacheLog("HairDeform: attaching groom '{}' to point deform '{}'",
                    groomPath.GetText(), defprims[0].GetText());
            _pointdeformtogroommap[defprims[0]].insert(groomPath);
            _groomtopointdeformmap[groomPath] = defprims[0];
        }
    }
}

void
HD_HairDeformSceneIndex::_HandleTargetRemoved(
        const SdfPath &targetPath)
{
    HdSceneIndexObserver::DirtiedPrimEntries dirtied_entries;
    // If this was a skin
    auto skintogroom_it = _skintogroommap.find(targetPath);
    if (skintogroom_it != _skintogroommap.end())
    {
        HD_HairDeformUtils::cacheLog("HairDeform: skin '{}' removed with {} dependant groom(s)",
                targetPath.GetText(), skintogroom_it->second.size());

        _ClearSkinMeshCache(targetPath);
        for (const SdfPath &dependant : skintogroom_it->second)
        {
            HD_HairDeformUtils::cacheLog("HairDeform:   -> propagating dirty to groom '{}'",
                    dependant.GetText());
            _ClearSurfaceTopoCache(dependant);
            _ClearCurveSkinCaptureCache(dependant);
            _ClearSkinSubdEvalCache(dependant);
            dirtied_entries.push_back(
                    HdSceneIndexObserver::DirtiedPrimEntry(
                            dependant, theGroomLocators));
        }
    }

    // If this was a GIM
    auto guidetogroom_it = _guideinterpmeshtogroommap.find(targetPath);
    if (guidetogroom_it != _guideinterpmeshtogroommap.end())
    {
        HD_HairDeformUtils::cacheLog("HairDeform: GIM '{}' removed with {} dependant groom(s)",
                targetPath.GetText(), guidetogroom_it->second.size());

        for (const SdfPath &dependant : guidetogroom_it->second)
        {
            HD_HairDeformUtils::cacheLog("HairDeform:   -> clearing guide interp + GIM cache "
                    "for groom '{}'", dependant.GetText());
            _ClearGuideInterpCache(dependant);
            _ClearGIMSurfaceTopoCache(dependant);
            _groomtoguideinterpmeshmap.erase(dependant);
            dirtied_entries.push_back(
                    HdSceneIndexObserver::DirtiedPrimEntry(
                            dependant, theGroomLocators));
        }
        _guideinterpmeshtogroommap.erase(guidetogroom_it);
    }

    // If this was a point deform deformer
    auto pdtogroom_it = _pointdeformtogroommap.find(targetPath);
    if (pdtogroom_it != _pointdeformtogroommap.end())
    {
        HD_HairDeformUtils::cacheLog("HairDeform: point deform '{}' removed with {} dependant "
                "groom(s)",
                targetPath.GetText(), pdtogroom_it->second.size());

        for (const SdfPath &dependant : pdtogroom_it->second)
        {
            HD_HairDeformUtils::cacheLog("HairDeform:   -> clearing point deform cache "
                    "for groom '{}'", dependant.GetText());
            _ClearPointDeformCaptureCache(dependant);
            _ClearCurveSkinCaptureCache(dependant);
            _groomtopointdeformmap.erase(dependant);
            dirtied_entries.push_back(
                    HdSceneIndexObserver::DirtiedPrimEntry(
                            dependant, theGroomLocators));
        }
        _pointdeformtogroommap.erase(pdtogroom_it);
    }

    _SendPrimsDirtied(dirtied_entries);
}

void
HD_HairDeformSceneIndex::_DirtyDependants(
        const UT_Set<SdfPath> &dependants,
        HdSceneIndexObserver::DirtiedPrimEntries &entries,
        std::function<void(const SdfPath &)> perDependant)
{
    for (const SdfPath &dependant : dependants)
    {
        entries.push_back(
                HdSceneIndexObserver::DirtiedPrimEntry(
                        dependant, theGroomLocators));
        if (perDependant)
            perDependant(dependant);
    }
}

void
HD_HairDeformSceneIndex::_UpdateRelationship(
        const SdfPath &groomPath,
        const VtArray<SdfPath> &paths,
        ForwardMapType &forwardMap,
        ReverseMapType &reverseMap,
        const char *label,
        std::function<void()> onOldRemoved)
{
    if (!paths.size())
        return;

    SdfPath newTarget = paths[0];
    auto existing = reverseMap.find(groomPath);
    if (existing != reverseMap.end() && existing->second == newTarget)
        return;

    if (existing != reverseMap.end())
    {
        _RemoveFromForwardMap(forwardMap, existing->second, groomPath);
        if (onOldRemoved)
            onOldRemoved();
    }

    HD_HairDeformUtils::cacheLog("HairDeform: groom '{}' now uses {} '{}'",
            groomPath.GetText(), label, newTarget.GetText());
    forwardMap[newTarget].insert(groomPath);
    reverseMap[groomPath] = newTarget;
}

// Scene index observer callbacks

void
HD_HairDeformSceneIndex::_PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    if (!_IsObserved())
        return;

    for (auto &&entry : entries)
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(entry.primPath);

        HairDeformSchema hairdeformschema
                = HairDeformSchema::GetFromParent(prim.dataSource);

        if (hairdeformschema)
        {
            _AttachGroom(entry.primPath, hairdeformschema);
        }
        else
        {
            // Prim re-added without HairDeformSchema — if it was
            // previously a groom, clean up stale state.
            if (_IsKnownGroom(entry.primPath))
            {
                HD_HairDeformUtils::cacheLog("HairDeform: _PrimsAdded prim '{}' lost "
                        "HairDeformSchema, cleaning up stale groom state",
                        entry.primPath.GetText());
                _DetachGroom(entry.primPath);
            }

            // Dirty all topo-dependent caches if skin is re-added.
            if (_skintogroommap.count(entry.primPath))
            {
                HdSceneIndexObserver::DirtiedPrimEntries topoentries;
                topoentries.emplace_back(entry.primPath, theTopoLocators);

                HD_HairDeformUtils::cacheLog(
                        "HairDeform: _PrimsAdded known skin '{}', "
                        "dirtying topology.",
                        entry.primPath.GetText());
                _PrimsDirtied(*this, topoentries);
            }
        }

        // Track ancestors for any prim we're managing.
        if (_IsKnownGroom(entry.primPath)
            || _skintogroommap.count(entry.primPath))
        {
            for (const SdfPath &path : entry.primPath.GetAncestorsRange())
                _parents.insert(path);
        }
    }

    _SendPrimsAdded(entries);
}

void
HD_HairDeformSceneIndex::_PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    if (!_IsObserved())
        return;

    for (auto &&entry : entries)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: _PrimsRemoved processing entry '{}'",
                entry.primPath.GetText());

        SdfPathVector pathstoremove;

        // Always include the entry itself.
        pathstoremove.push_back(entry.primPath);

        // Also collect any children in _parents that have this entry's path
        // as a prefix (i.e., descendants of the removed prim).
        for (const auto &parent : _parents)
        {
            if (parent.HasPrefix(entry.primPath) && parent != entry.primPath)
            {
                HD_HairDeformUtils::cacheLog("HairDeform:   also removing child '{}' of '{}'",
                        parent.GetText(), entry.primPath.GetText());
                pathstoremove.push_back(parent);
            }
        }

        for (auto &&childpath : pathstoremove)
        {
            HD_HairDeformUtils::cacheLog("HairDeform:   removing '{}'", childpath.GetText());

            // Phase 1: If this was a groom, detach from all targets.
            _DetachGroom(childpath);

            // Phase 2: If this was a target (skin/GIM/deformer), dirty
            // all dependent grooms and clean up forward maps.
            _HandleTargetRemoved(childpath);

            _parents.erase(childpath);
        }
    }

    _SendPrimsRemoved(entries);
}

void
HD_HairDeformSceneIndex::_PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    if (!_IsObserved())
        return;

    HdSceneIndexObserver::DirtiedPrimEntries extra_entries;
    extra_entries.reserve(entries.size());

    for (auto &&entry : entries)
    {
        const bool knowngroom = _IsKnownGroom(entry.primPath);
        HairDeformSchema hairdeformschema(nullptr);
        if (knowngroom || entry.dirtyLocators.Intersects(theHairDeformLoc))
        {
            HdSceneIndexPrim prim
                    = _GetInputSceneIndex()->GetPrim(entry.primPath);
            hairdeformschema
                    = HairDeformSchema::GetFromParent(prim.dataSource);
        }

        if (hairdeformschema)
        {
            _grooms.insert(entry.primPath);

            // Groom-specific cache invalidation

            if (entry.dirtyLocators.Intersects(theFeatherRestLocators))
            {
                HD_HairDeformUtils::cacheLog("HairDeform:   -> feather rest dirty, clearing "
                        "rest points cache for '{}'",
                        entry.primPath.GetText());
                _ClearRestPointsCache(entry.primPath);
                _ClearSurfaceTopoCache(entry.primPath);
                _ClearCurveSkinCaptureCache(entry.primPath);
                _ClearSkinSubdEvalCache(entry.primPath);
                _ClearClumpTopoCache(entry.primPath);
                _ClearOrientAttribsCache(entry.primPath);
            }

            if (entry.dirtyLocators.Intersects(thePreserveClumpsEnableLoc)
                || entry.dirtyLocators.Intersects(
                        thePreserveClumpsMaxNeighborsLoc)
                || entry.dirtyLocators.Intersects(
                        thePreserveClumpsMaxConstraintsLoc))
            {
                _ClearClumpTopoCache(entry.primPath);
            }

            if (entry.dirtyLocators.Intersects(theDeformMethodLocators)
                || entry.dirtyLocators.Intersects(
                        theGuideWeightsPrimvarLocators))
            {
                HD_HairDeformUtils::cacheLog("HairDeform:   -> deform method/guide weights "
                        "dirty on '{}', clearing deformer + guide interp cache",
                        entry.primPath.GetText());
                if (HdPathArrayDataSourceHandle defrelds
                    = hairdeformschema.GetDeformerPrims())
                {
                    VtArray<SdfPath> defprims = defrelds->GetTypedValue(0);
                    if (defprims.size())
                        _ClearDeformerCache(defprims[0]);
                }
                _ClearGuideInterpCache(entry.primPath);
            }

            if (entry.dirtyLocators.Intersects(theCaptureParamLocators))
            {
                HD_HairDeformUtils::cacheLog("HairDeform:   -> capture params changed on "
                        "groom '{}', clearing point deform capture cache",
                        entry.primPath.GetText());
                _ClearPointDeformCaptureCache(entry.primPath);
            }

            // Groom relationship updates

            if (entry.dirtyLocators.Intersects(theGroomRelationshipLocators))
            {
                if (HdPathArrayDataSourceHandle relds
                    = hairdeformschema.GetSkinPrims())
                {
                    _UpdateRelationship(entry.primPath,
                            relds->GetTypedValue(0),
                            _skintogroommap, _groomtoskinmap, "skin",
                            [this, &entry]
                            {
                                _ClearSurfaceTopoCache(entry.primPath);
                                _ClearCurveSkinCaptureCache(entry.primPath);
                                _ClearSkinSubdEvalCache(entry.primPath);
                            });
                }

                if (HdPathArrayDataSourceHandle girelds
                    = hairdeformschema.GetGuideInterpMeshPrims())
                {
                    _UpdateRelationship(entry.primPath,
                            girelds->GetTypedValue(0),
                            _guideinterpmeshtogroommap,
                            _groomtoguideinterpmeshmap, "GIM",
                            [this, &entry]{
                                _ClearGuideInterpCache(entry.primPath);
                                _ClearGIMSurfaceTopoCache(entry.primPath);
                            });
                }

                if (HdPathArrayDataSourceHandle defrelds
                    = hairdeformschema.GetDeformerPrims())
                {
                    _UpdateRelationship(entry.primPath,
                            defrelds->GetTypedValue(0),
                            _pointdeformtogroommap,
                            _groomtopointdeformmap, "point deform",
                            [this, &entry]{
                                _ClearPointDeformCaptureCache(entry.primPath);
                                _ClearCurveSkinCaptureCache(entry.primPath);
                            });
                }
            }

            if (entry.dirtyLocators.Intersects(theHairDeformLoc))
            {
                static const HdDataSourceLocatorSet thePointsAndExtent = {
                        thePointsLocator, theExtentLocator};
                extra_entries.push_back(
                    HdSceneIndexObserver::DirtiedPrimEntry(
                        entry.primPath, thePointsAndExtent));
            }

            // Per-locator primvar cache invalidation

            for (const auto &locator : entry.dirtyLocators)
            {
                const char *primpath = entry.primPath.GetText();
                const char *primvar = locator.GetLastElement().GetText();

                UT_StringHolder cache_key
                        = HD_HairDeformUtils::makePrimVarCacheKey(
                                primpath, primvar);

                SYS_MAYBE_UNUSED
                bool erased = _cachemap->erase(cache_key);
                if (erased)
                    HD_HairDeformUtils::cacheLog("HairDeform: Clearing cache for primvar '{}' "
                            "on prim '{}'", primvar, primpath);
            }
        }
        else if (knowngroom)
        {
            // Prim was previously a groom but lost HairDeformSchema
            // (e.g. configureguidedeform bypassed). Clean up stale state.
            HD_HairDeformUtils::cacheLog("HairDeform: _PrimsDirtied prim '{}' lost "
                    "HairDeformSchema, cleaning up stale groom state",
                    entry.primPath.GetText());
            _DetachGroom(entry.primPath);
        }
        else
        {
            // Target prim dirtied — propagate to dependent grooms.
            // These checks are independent: a prim appears in at most
            // one forward map, so at most one inner block does work.
            // Look the path up before testing locators: almost every
            // dirtied prim is in none of these maps, and a map lookup is
            // cheaper than a locator set intersection.

            // Skin prim
            {
                auto found = _skintogroommap.find(entry.primPath);
                if (found != _skintogroommap.end()
                    && entry.dirtyLocators.Intersects(theSkinLocators))
                {
                    HD_HairDeformUtils::cacheLog("HairDeform: skin '{}' dirtied, propagating "
                            "to {} groom(s)",
                            entry.primPath.GetText(), found->second.size());

                    if (entry.dirtyLocators.Intersects(theTopoLocators))
                        _ClearSkinMeshCache(entry.primPath);

                    const bool restskin_dirty
                            = entry.dirtyLocators.Intersects(theRestSkinLocators);
                    const bool skin_topo_dirty
                            = entry.dirtyLocators.Intersects(theTopoLocators);
                    _DirtyDependants(found->second, extra_entries,
                            [this, restskin_dirty, skin_topo_dirty](
                                    const SdfPath &d)
                            {
                                if (restskin_dirty || skin_topo_dirty)
                                {
                                    _ClearSurfaceTopoCache(d);
                                    _ClearCurveSkinCaptureCache(d);
                                }
                                _ClearSkinSubdEvalCache(d);
                            });
                }
            }

            // Point-deform deformer — animated P propagation
            {
                auto found = _pointdeformtogroommap.find(entry.primPath);
                if (found != _pointdeformtogroommap.end()
                    && entry.dirtyLocators.Intersects(
                            thePointDeformDeformerLocators))
                {
                    HD_HairDeformUtils::cacheLog("HairDeform: point deform deformer '{}' "
                            "animated P dirtied",
                            entry.primPath.GetText());
                    _DirtyDependants(found->second, extra_entries);
                }
            }

            // Guide interpolation mesh
            {
                auto found = _guideinterpmeshtogroommap.find(entry.primPath);
                if (found != _guideinterpmeshtogroommap.end()
                    && entry.dirtyLocators.Intersects(theGuideInterpLocators))
                {
                    HD_HairDeformUtils::cacheLog("HairDeform: GIM '{}' dirtied, propagating "
                            "to {} groom(s)",
                            entry.primPath.GetText(), found->second.size());
                    _DirtyDependants(found->second, extra_entries,
                            [this](const SdfPath &d)
                            {
                                _ClearGuideInterpCache(d);
                                _ClearGIMSurfaceTopoCache(d);
                            });
                }
            }

            // Point-deform deformer — rest positions or topology
            {
                auto found = _pointdeformtogroommap.find(entry.primPath);
                if (found != _pointdeformtogroommap.end()
                    && entry.dirtyLocators.Intersects(thePointDeformRestLocators))
                {
                    HD_HairDeformUtils::cacheLog("HairDeform: point deform '{}' rest/topo "
                            "dirtied, propagating to {} groom(s)",
                            entry.primPath.GetText(), found->second.size());
                    _DirtyDependants(found->second, extra_entries,
                            [this](const SdfPath &d)
                            {
                                _ClearPointDeformCaptureCache(d);
                                _ClearCurveSkinCaptureCache(d);
                            });
                }
            }
        }
    }

    _SendPrimsDirtied(entries);
    _SendPrimsDirtied(extra_entries);
}

PXR_NAMESPACE_CLOSE_SCOPE
