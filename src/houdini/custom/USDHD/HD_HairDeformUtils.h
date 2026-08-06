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

#include <GU/GU_Detail.h>
#include <GU/GU_OSDEval.h>
#include <GU/GU_OSDTopology.h>
#include <GU/GU_RayIntersect.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_Map.h>
#include <UT/UT_Optional.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_Set.h>
#include <UT/UT_Tracing.h>

#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/tokens.h>

#include <cstring>
#include <memory>
#include <type_traits>

// #define USDHD_HAIRDEFORM_DEBUG_CACHING

PXR_NAMESPACE_OPEN_SCOPE

namespace HD_HairDeformUtils
{

SYS_FORCE_INLINE UT_StringHolder
makePrimVarCacheKey(const char *primpath, const char *primvar)
{
    UT_StringHolder key;
    key.format("{}:{}:{}", std::strlen(primpath), primpath, primvar);
    return key;
}

SYS_FORCE_INLINE UT_StringHolder
makePrimVarCachePrefix(const char *primpath)
{
    UT_StringHolder prefix;
    prefix.format("{}:{}:", std::strlen(primpath), primpath);
    return prefix;
}

constexpr bool enableDebugCaching()
{
#ifdef USDHD_HAIRDEFORM_DEBUG_CACHING
    return true;
#else
    return false;
#endif
}

template <typename... Args>
inline void cacheLog(SYS_MAYBE_UNUSED const char* fmt, SYS_MAYBE_UNUSED Args&&... args)
{
    if constexpr (enableDebugCaching())
    {
        UT_ErrorLog::warning(fmt, std::forward<Args>(args)...);
    }
}

template <typename T>
SYS_FORCE_INLINE bool
checkHandle(
        const UT_Optional<T> &handle,
        const TfToken &attribname,
        const char *inputname,
        bool report_error = true)
{
    if (!handle.has_value())
    {
        if (report_error)
            UT_ErrorLog::error(
                    "Missing {} attribute on {}\n", attribname.GetText(),
                    inputname);
        return false;
    }
    return true;
}

// Resize a VtArray without default-constructing elements.
template <typename T>
SYS_FORCE_INLINE void
resizeUninitialized(VtArray<T> &arr, size_t n)
{
    arr.resize(n, [](T *, T *) {});
}

template <typename T>
SYS_FORCE_INLINE UT_Optional<const VtArray<T>>
getConstPvVal(const HdPrimvarSchema &primvar, HdSampledDataSource::Time time)
{
    HdSampledDataSourceHandle dshandle = primvar.GetFlattenedPrimvarValue();
    VtValue val = dshandle->GetValue(time);
    if (val.IsHolding<VtArray<T>>())
        return val.Get<VtArray<T>>();

    return UT_NULLOPT;
}

template <typename T>
SYS_FORCE_INLINE UT_Optional<const VtArray<T>>
getConstPvVal(
        const HdPrimvarsSchema &primvars,
        const TfToken &token,
        HdSampledDataSource::Time time)
{
    if (!primvars)
        return UT_NULLOPT;

    HdPrimvarSchema primvar = primvars.GetPrimvar(token);
    if (primvar.IsDefined())
        return getConstPvVal<T>(primvar, time);

    return UT_NULLOPT;
}

SYS_FORCE_INLINE
const float *
getBarbData(
        VtValue &val,
        int &nbarbpts,
        const HdPrimvarsSchema &pvs,
        const TfToken &barbname,
        int npts,
        int shaft_dim)
{
    HdPrimvarSchema primvar = pvs.GetPrimvar(barbname);
    if (!primvar)
        return nullptr;
    HdSampledDataSourceHandle dshandle = primvar.GetFlattenedPrimvarValue();
    if (!dshandle)
        return nullptr;

    val = dshandle->GetValue(0.0f);

    if (val.IsHolding<VtArray<float>>())
    {
        const auto &typedval = val.Get<VtArray<float>>();
        nbarbpts = typedval.size() / (shaft_dim * npts);
        return typedval.cdata();
    }
    else if (val.IsHolding<VtArray<GfVec4f>>())
    {
        const auto &typedval = val.Get<VtArray<GfVec4f>>();
        nbarbpts = 4 * typedval.size() / (shaft_dim * npts);
        return (const float *)(typedval).cdata();
    }

    return nullptr;
}

template <typename T>
SYS_FORCE_INLINE void
expandBarbs(
        VtArray<T> &outshaft,
        int npts,
        int dim,
        int nbarblpts,
        int nbarbrpts,
        const float *barbl,
        const float *barbr,
        const VtArray<GfQuatf> *barborients,
        const VtArray<T> &shaft)
{
    utZoneScoped;
    exint pos_size = npts + nbarblpts * npts + nbarbrpts * npts;

    resizeUninitialized(outshaft, pos_size);
    std::uninitialized_copy(shaft.begin(), shaft.end(), outshaft.begin());

    UTparallelFor(
            UT_BlockedRange<exint>(0, npts),
            [&](const UT_BlockedRange<exint> &r)
            {
                utZoneScopedN("expandBarbs");
                for (exint i = r.begin(), end = r.end(); i < end; ++i)
                {
                    const GfQuatf *barborient = nullptr;
                    if (barborients != nullptr)
                        barborient = &((*barborients)[i]);

                    auto &&set_barb_positions =
                            [&](int outoffset, int nbarbpts, auto barb)
                    {
                        for (int j = 0; j < nbarbpts; ++j)
                        {
                            int offset = dim * (i * nbarbpts + j);

                            T value(0.0f);
                            if constexpr (std::is_same_v<T, GfVec3f>)
                            {
                                GfVec3d pos;

                                if (barb != nullptr)
                                    pos = GfVec3d(
                                            barb[offset],
                                            barb[offset + 1],
                                            barb[offset + 2]);
                                else
                                    pos = GfVec3d(shaft[i]);
                                if (barborient != nullptr)
                                    pos = GfRotation(*barborient)
                                                  .TransformDir(pos);
                                pos += outshaft[i];
                                value = GfVec3f(pos);
                            }
                            else if constexpr (std::is_same_v<T, GfVec2f>)
                            {
                                if (barb != nullptr)
                                    value = GfVec2f(
                                            barb[offset],
                                            barb[offset + 1]);
                                else
                                    value = shaft[i];
                            }
                            else if constexpr (std::is_same_v<T, float>)
                            {
                                if (barb != nullptr)
                                    value = barb[offset];
                                else
                                    value = shaft[i];
                            }
                            outshaft[npts + outoffset + nbarbpts * i + j]
                                    = value;
                        }
                    };

                    set_barb_positions(0, nbarblpts, barbl);
                    set_barb_positions(nbarblpts * npts, nbarbrpts, barbr);
                }
            });
};

} // namespace HD_HairDeformUtils

struct HD_HairDeformPrimVarCache
{
    HdDataSourceLocator myLocator;
    VtValue myArray;
};

using PrimVarCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformPrimVarCache>;

struct HD_HairDeformDeformerNeighbourCache
{
    UT_Array<int> myNeighbours;
    UT_Array<int> myNeighbourindex;
};

using DeformerCacheMapType = UT_ConcurrentHashMap<
        UT_StringHolder,
        HD_HairDeformDeformerNeighbourCache>;

struct HD_HairDeformSkinMeshCache
{
    UT_Optional<GU_Detail> myGdp;
    GA_AttributeUPtr myNormal;
    GA_AttributeUPtr myTangent;
    UT_Optional<GU_RayIntersect> myRayIntersect;
    UT_UniquePtr<GU_OSDTopology> mySubdTopology;  // non-null for subd meshes
};

using SkinMeshCacheMapType = UT_ConcurrentHashMap<
        UT_StringHolder,
        HD_HairDeformSkinMeshCache>;

struct HD_HairDeformRestPointsCache
{
    VtArray<GfVec3f> myPoints;
};

using RestPointsCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformRestPointsCache>;

struct HD_HairDeformSurfaceTopoCache
{
    UT_IntArray myPrimPtStarts;
    GA_OffsetArray myPtOffsets;
    UT_FloatArray myPtWeights;
    UT_IntArray myPtIndices;
};

using SurfaceTopoCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformSurfaceTopoCache>;

using CurveSkinCaptureCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformSurfaceTopoCache>;

struct HD_HairDeformGuideInterpCache
{
    UT_IntArray myGuideStarts;
    UT_IntArray myGuideIndices;
    UT_FloatArray myGuideWeights;
    VtArray<int> myGuideIndicesVt;
    VtArray<float> myGuideWeightsVt;
    bool myUseVtGuideWeights = false;
};

using GuideInterpCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformGuideInterpCache>;

// GIM surface capture cache - keyed by groom path (not GIM path) since
// capture depends on both GIM mesh and groom curve root positions
using GIMSurfaceTopoCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformSurfaceTopoCache>;

// Point deform capture cache - keyed by groom path
// Stores BVH-computed capture weights when pCaptPts/pCaptWeights primvars are
// missing
struct HD_HairDeformPointDeformCaptureCache
{
    UT_IntArray   captStarts;   // npts+1: start indices per groom point
    UT_IntArray   captPts;      // flattened deformer point indices
    UT_FloatArray captWeights;  // flattened weights (not normalized)
};

using PointDeformCaptureCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformPointDeformCaptureCache>;

// Subd surface deform eval cache - keyed by groom path
// Holds GPU evaluators and captured ptex patch coordinates
struct HD_HairDeformSkinSubdEvalCache
{
    UT_UniquePtr<GU_OSDEval> myRestEval;
    UT_UniquePtr<GU_OSDEval> myAnimEval;
    UT_IntArray myPatchFace;
    UT_FloatArray myPatchU;
    UT_FloatArray myPatchV;
    bool myPatchCoordsUploaded = false;
    bool myEvalInitialized = false;
};

using SkinSubdEvalCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformSkinSubdEvalCache>;

struct HD_HairDeformClumpTopoCache
{
    UT_Array<int>   myClumpPts;      // flattened point indices
    UT_Array<int>   myClumpPtsIndex; // npts+1 start indices
    UT_Array<float> myClumpDists;    // flattened distances
};

using ClumpTopoCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformClumpTopoCache>;

// Per-prim cache of the orient/restorient/flags arrays produced by
// hdInitOrientAttribs. These depend on rest points + curve vertex counts,
// both of which are stable per prim across cooks.
struct HD_HairDeformOrientAttribsCache
{
    UT_Array<UT_Quaternion> myOrients;
    UT_Array<UT_Quaternion> myRestOrients;
    UT_Array<int>           myFlags;

    // Lightweight identity keys: we cache by primpath in the map but also
    // store input identity to detect rest-data swaps without recomputing
    // a hash over the full point set.
    const void *myRestPtsData  = nullptr;
    size_t      myRestPtsSize  = 0;
    const void *myCountsData   = nullptr;
    size_t      myCountsSize   = 0;
};

using OrientAttribsCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformOrientAttribsCache>;

// Shared pointer types for cache maps (to ensure lifetime safety)
using PrimVarCacheMapPtr = std::shared_ptr<PrimVarCacheMapType>;
using DeformerCacheMapPtr = std::shared_ptr<DeformerCacheMapType>;
using SkinMeshCacheMapPtr = std::shared_ptr<SkinMeshCacheMapType>;
using RestPointsCacheMapPtr = std::shared_ptr<RestPointsCacheMapType>;
using SurfaceTopoCacheMapPtr = std::shared_ptr<SurfaceTopoCacheMapType>;
using CurveSkinCaptureCacheMapPtr = std::shared_ptr<CurveSkinCaptureCacheMapType>;
using GuideInterpCacheMapPtr = std::shared_ptr<GuideInterpCacheMapType>;
using GIMSurfaceTopoCacheMapPtr = std::shared_ptr<GIMSurfaceTopoCacheMapType>;
using PointDeformCaptureCacheMapPtr = std::shared_ptr<PointDeformCaptureCacheMapType>;
using SkinSubdEvalCacheMapPtr = std::shared_ptr<SkinSubdEvalCacheMapType>;
using ClumpTopoCacheMapPtr = std::shared_ptr<ClumpTopoCacheMapType>;
using OrientAttribsCacheMapPtr = std::shared_ptr<OrientAttribsCacheMapType>;

PXR_NAMESPACE_CLOSE_SCOPE
