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

#include "HD_HairDeformSceneIndex.h"
#include "HD_HairDeformSchema.h"

#include <CE/CE_Array.h>
#include <CE/CE_Context.h>
#include <GU/GU_GuideCapture.h>
#include <GU/GU_PointDeform.h>
#include <UT/UT_Array.h>
#include <UT/UT_ConcurrentHashMap.h>
#include <UT/UT_Optional.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_StringHolder.h>

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/sdf/path.h"
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/primvarsSchema.h>

#pragma once

class GU_Detail;

PXR_NAMESPACE_OPEN_SCOPE

class HD_HairDeformPointsDataSource
    : public HdTypedSampledDataSource<VtVec3fArray>
{
public:
    HD_DECLARE_DATASOURCE(HD_HairDeformPointsDataSource);

    enum class DeformMethod
    {
        NONE,
        POINTDEFORM,
        SURFACEDEFORM,
        GUIDEINTERPOLATIONMESH,
        GUIDEWEIGHTS,
        GUIDESHAPEINTERPOLATION
    };

    struct PointDeformCEArrays
    {
        CE_Array<int> ce_ptsindex;
        CE_Array<int> ce_pts;
        CE_Array<int> ce_weightsindex;
        CE_Array<float> ce_weights;

        // deformer CE arrays
        CE_Array<float> ce_deformerrestpos;
        CE_Array<float> ce_deformeranimpos;
        CE_Array<int> ce_neighbourindex;
        CE_Array<int> ce_neighbour;

        // orient CE arrays for orient-based xform
        CE_Array<float> ce_deformerrestorient;
        CE_Array<float> ce_deformeranimorient;

        // topology CE arrays for curves
        CE_Array<int> ce_primptsindex;
    };

    struct PointCapturePrimVars
    {
        const VtArray<int> myVtCaptPts;
        const VtArray<float> myVtCaptWeights;
        UT_IntArray myIndex;  // Shared index for both pts and weights (must match)
    };

    VtValue GetValue(const Time shutterOffset) override;
    VtVec3fArray GetTypedValue(const Time shutterOffset) override;
    bool GetContributingSampleTimesForInterval(
            const Time startTime,
            const Time endTime,
            std::vector<Time> *const outSampleTimes) override;

private:
    VtVec3fArray _ComputePoints(const Time shutterOffset);

    HD_HairDeformPointsDataSource(
            SdfPath primpath,
            HdContainerDataSourceHandle primds,
            SdfPath deformerprimpath,
            HdContainerDataSourceHandle deformerds,
            SdfPath skinprimpath,
            HdContainerDataSourceHandle skinds,
            SdfPath guideinterpprimpath,
            HdContainerDataSourceHandle guideinterpds,
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

    bool _ComputeGuideDeform(
            CE_Context &context,
            CE_FloatArray &ce_main_pos_out,
            PointDeformCEArrays &pointdeform_cearrays,
            CE_FloatArray &ce_main_skinxform,
            CE_FloatArray &ce_def_skinxform,
            CE_FloatArray &ce_main_skinrestnml,
            CE_FloatArray &ce_def_skinrestnml,
            const VtVec3fArray &restPoints,
            const UT_IntArray &curveprimptsindex,
            const VtIntArray &vt_curvevtxcounts,
            const HdPrimvarsSchema &groompvs,
            const VtVec3fArray &vt_deformerrestpos,
            const VtVec3fArray &vt_deformeranimpos,
            DeformMethod deformmethod,
            const GU_GuideCaptureParms &gsiParms,
            exint npts,
            bool recompile);

    struct SkinCaptureCEData
    {
        CE_FloatArray ce_skinrestpos, ce_skinrestnml, ce_skinresttan;
        CE_FloatArray ce_skinanimpos, ce_skinanimnml, ce_skinanimtan;
        CE_Int32Array ce_mainskinptstarts, ce_mainskinptindices;
        CE_FloatArray ce_mainskinptweights;
        CE_Int32Array ce_defskinptstarts, ce_defskinptindices;
        CE_FloatArray ce_defskinptweights;
    };

    bool _PrepareSkinCaptureData(
            SkinCaptureCEData &skince,
            const GU_Detail &skinrestgdp,
            const GA_Attribute *cachedrestnml,
            const GA_Attribute *cachedresttan,
            const VtVec3fArray &vt_skinrestpos,
            const VtVec3fArray &vt_skinanimpos);

    void _ComputeCurveSkinXforms(
            CE_Context &context,
            CE_FloatArray &ce_curve_skinxform,
            CE_FloatArray &ce_curve_skinnml,
            CE_Int32Array &ce_curve_skinptstarts,
            CE_Int32Array &ce_curve_skinptindices,
            CE_FloatArray &ce_curve_skinptweights,
            SkinCaptureCEData &skince,
            CE_FloatArray &ce_skinnml_src,
            exint ncurveprims,
            bool recompile);

    bool _ComputeSubdSkinXforms(
            CE_Context &context,
            CE_FloatArray &ce_xform,
            CE_FloatArray *ce_restnml,
            int ncurves,
            const VtVec3fArray &restPoints,
            const UT_IntArray &curveprimptsindex,
            const VtVec3fArray &vt_skinrestpos,
            const VtVec3fArray &vt_skinanimpos);

    bool _ApplySubdSkinXforms(
            CE_Context &context,
            CE_FloatArray &ce_main_pos_out,
            const UT_IntArray &curveprimptsindex,
            exint npts,
            CE_FloatArray &ce_xform,
            CE_FloatArray *ce_mask = nullptr);

    bool _InitPointDeform(
            CE_Context &context,
            CE_FloatArray &ce_main_pos_out,
            PointDeformCEArrays &pointdeform_cearrays,
            GU_PointDeform::CachedItems &pointdeform_cacheditems,
            const VtVec3fArray &vt_deformerrestpos,
            const VtVec3fArray &vt_deformeranimpos,
            const UT_Span<const int> &captpts,
            const UT_Span<const float> &captweights,
            const UT_Span<const int> &captindex,
            exint npts,
            int ndeformerpts,
            bool useorientattrib,
            const VtArray<GfVec4f> *vt_deformerrestorient,
            const VtArray<GfVec4f> *vt_deformeranimorient,
            const VtArray<GfQuatf> *vt_deformerrestorient_quat,
            const VtArray<GfQuatf> *vt_deformeranimorient_quat,
            bool recompile);

    bool _InitSurfaceTopo(const VtVec3fArray &restPoints);

    // Try to read point capture data from primvars
    // Returns true and populates pcaptpvs if all primvars are present and valid
    bool _ReadPointCapturePrimVars(
            const HdPrimvarsSchema &groompvs,
            UT_Optional<PointCapturePrimVars> &pcaptpvs);

    // Compute point capture and store in cache (smooth or BVH based)
    // Returns true if valid capture data is available in the cache
    bool _ComputePointCapture(
            const VtVec3fArray &vt_points,
            const VtVec3fArray &vt_deformerrestpos,
            HairDeformSchema &hairdeformschema);

    // Computed points cached per shutter offset, populated by GetTypedValue().
    // The instance is rebuilt per GetPrim(), so this lives only as long as the
    // consumer holds the handle (e.g. across a motion-blur sampling pass).
    using PointsCacheMap = UT_ConcurrentHashMap<Time, VtVec3fArray>;
    PointsCacheMap _cachedResult;

    SdfPath _primpath;
    HdContainerDataSourceHandle _primds;
    SdfPath _deformerprimpath;
    HdContainerDataSourceHandle _deformerds;
    SdfPath _skinprimpath;
    HdContainerDataSourceHandle _skinds;
    SdfPath _guideinterpprimpath;
    HdContainerDataSourceHandle _guideinterpds;
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
