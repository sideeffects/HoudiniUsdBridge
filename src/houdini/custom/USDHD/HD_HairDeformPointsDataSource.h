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
#include <UT/UT_Lock.h>
#include <UT/UT_SharedPtr.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_TaskExclusive.h>

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/range3d.h"
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

    /// Bounds of the deformed points, scanned once when they are cached.
    /// Empty if the prim has no points.
    GfRange3d GetExtent(const Time shutterOffset);
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
            GuideInterpCacheMapPtr guideinterpcachemap,
            GIMSurfaceTopoCacheMapPtr gimsurfacetopocachemap,
            PointDeformCaptureCacheMapPtr pointdeformcapturecachemap,
            SkinSubdEvalCacheMapPtr skinsubdcachemap,
            ClumpTopoCacheMapPtr clumptopocachemap,
            OrientAttribsCacheMapPtr orientattribscachemap,
            ResolvedPointsCacheMapPtr resolvedpointscachemap)
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
        , _guideinterpcachemap(guideinterpcachemap)
        , _gimsurfacetopocachemap(gimsurfacetopocachemap)
        , _pointdeformcapturecachemap(pointdeformcapturecachemap)
        , _skinsubdcachemap(skinsubdcachemap)
        , _clumptopocachemap(clumptopocachemap)
        , _orientattribscachemap(orientattribscachemap)
        , _resolvedpointscachemap(resolvedpointscachemap)
    {
    }

    bool _ComputeGuideDeform(
            CE_Context &context,
            CE_FloatArray &ce_main_pos_out,
            PointDeformCEArrays &pointdeform_cearrays,
            CE_FloatArray &ce_main_skinxform,
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
            CE_Int32Array &ce_curve_skinptstarts,
            CE_Int32Array &ce_curve_skinptindices,
            CE_FloatArray &ce_curve_skinptweights,
            SkinCaptureCEData &skince,
            exint ncurveprims,
            bool recompile);

    // ncurves counts what the capture is per: points with perpointcapture,
    // curves otherwise.
    // lockscope, when set, is locked once the capture and patch coords are in
    // hand and before the first device allocation, so the host-side capture
    // still runs concurrently across grooms.  The caller owns it, because
    // ce_xform outlives this call.
    bool _ComputeSubdSkinXforms(
            CE_Context &context,
            CE_FloatArray &ce_xform,
            int ncurves,
            const HD_HairDeformRestPointsCache &restpoints,
            const UT_IntArray &curveprimptsindex,
            const VtVec3fArray &vt_skinrestpos,
            const VtVec3fArray &vt_skinanimpos,
            bool perpointcapture,
            const UT_StringHolder &captureidattrib,
            UT_Lock::Scope *lockscope = nullptr);

    // With perpointxform the xform array is indexed by point rather than
    // by curve, and curveprimptsindex is unused. Otherwise npts may cover the
    // barb points appended after the shaft, described by barbs, and they take
    // their shaft point's curve xform.
    bool _ApplySubdSkinXforms(
            CE_Context &context,
            CE_FloatArray &ce_main_pos_out,
            const UT_IntArray &curveprimptsindex,
            exint npts,
            CE_FloatArray &ce_xform,
            CE_FloatArray *ce_mask = nullptr,
            bool perpointxform = false,
            const HD_HairDeformBarbLayout *barbs = nullptr);

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

    bool _InitSurfaceTopo(
            const UT_IntArray &curveprimptsindex,
            const HD_HairDeformRestPointsCache &restpoints,
            const UT_StringHolder &captureidattrib);

    // Capture the groom's curve roots against the skin, keyed by this groom,
    // and upload the result to CE.  Only a cache miss captures; a failed
    // capture drops the entry so the next cook retries instead of uploading a
    // half filled one.
    bool _InitCurveSkinCapture(
            CE_Int32Array &ce_skinptstarts,
            CE_Int32Array &ce_skinptindices,
            CE_FloatArray &ce_skinptweights,
            const HD_HairDeformSkinMeshCache &skincache,
            const UT_IntArray &curveprimptsindex,
            const HD_HairDeformRestPointsCache &restpoints,
            const UT_StringHolder &captureidattrib);

    // Bucket the skin polygons by the named id attribute if that hasn't been
    // done yet.  Buckets are built with the skin mesh cache for whichever
    // attribute the first groom asked for; a second groom on the same skin
    // naming a different one tops the map up here.
    bool _InitSkinIdBuckets(const UT_StringHolder &captureidattrib);

    // The skin bucket each capture position belongs to, by the groom's ids:
    // one entry per curve, or per expanded point with per-point capture.  Only
    // a capture cache miss reads these, so callers resolve inside their miss
    // branch rather than up front.
    bool _BucketIndicesFromCaptureIds(
            UT_Array<int> &bucketindices,
            const HD_HairDeformIdBuckets &buckets,
            const UT_StringHolder &captureidattrib,
            bool perpoint,
            const UT_IntArray &curveprimptsindex,
            const HD_HairDeformRestPointsCache &restpoints);

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

    // Computed points and their bounds, cached per shutter offset.  The
    // instance is rebuilt per GetPrim(), so this lives only as long as the
    // consumer holds the handle (e.g. across a motion-blur sampling pass).
    struct CachedPoints
    {
        VtVec3fArray myPoints;
        GfRange3d myExtent;
    };
    struct CachedPointsEntry;
    // Run by CachedPointsEntry::myExclusive, once per entry.
    struct CachedPointsCompute
    {
        HD_HairDeformPointsDataSource &mySource;
        CachedPointsEntry &myEntry;
        Time myShutterOffset;
        bool myComputed = false;

        void operator()();
    };
    struct CachedPointsEntry
    {
        UT_TaskExclusive<CachedPointsCompute> myExclusive;
        CachedPoints myResult;
    };

    using PointsCacheMap
            = UT_ConcurrentHashMap<Time, UT_SharedPtr<CachedPointsEntry>>;
    PointsCacheMap _cachedResult;

    // Cached entry for shutterOffset, computed on the first pull.
    CachedPoints _CachedPointsFor(const Time shutterOffset);

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
    GuideInterpCacheMapPtr _guideinterpcachemap;
    GIMSurfaceTopoCacheMapPtr _gimsurfacetopocachemap;
    PointDeformCaptureCacheMapPtr _pointdeformcapturecachemap;
    SkinSubdEvalCacheMapPtr _skinsubdcachemap;
    ClumpTopoCacheMapPtr _clumptopocachemap;
    OrientAttribsCacheMapPtr _orientattribscachemap;
    ResolvedPointsCacheMapPtr _resolvedpointscachemap;
};
PXR_NAMESPACE_CLOSE_SCOPE
