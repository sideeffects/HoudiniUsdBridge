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
#include <UT/UT_SharedPtr.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_TaskExclusive.h>
#include <UT/UT_Tracing.h>

#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/tokens.h>

#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>

// #define USDHD_HAIRDEFORM_DEBUG_CACHING

PXR_NAMESPACE_OPEN_SCOPE

namespace HD_HairDeformUtils
{

// Most floats a single barb point can hold. The shaft primvars this expands
// are float, float2 or float3, so nothing wider than a float4 per barb
// point has anywhere to go; a dim above this is not a wide attribute but a
// barb attribute whose barb count disagrees with the one the positions have.
inline constexpr int theMaxBarbDim = 4;

SYS_FORCE_INLINE UT_StringHolder
makePrimVarCacheKey(const char *primpath, const char *primvar)
{
    UT_StringHolder key;
    key.format("{}:{}:{}", std::strlen(primpath), primpath, primvar);
    return key;
}

// Shares the makePrimVarCachePrefix() prefix so one prefix erase drops every
// shutter offset held for a prim.
SYS_FORCE_INLINE UT_StringHolder
makeResolvedPointsCacheKey(const char *primpath, float shutteroffset)
{
    UT_StringHolder key;
    key.format("{}:{}:points@{}", std::strlen(primpath), primpath,
            shutteroffset);
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

// A packed barb attribute's floats, whichever way the import typed it.
// Matrix arrays hold doubles. The storage is kept as it arrived and
// converted a value at a time rather copied to float array.
struct BarbFloats
{
    const float *myFloats = nullptr;
    const double *myDoubles = nullptr;
    exint myCount = 0;

    SYS_FORCE_INLINE bool
    isValid() const
    {
        return myFloats != nullptr || myDoubles != nullptr;
    }

    SYS_FORCE_INLINE float
    operator[](exint i) const
    {
        return myFloats ? myFloats[i] : (float)myDoubles[i];
    }
};

// The floats of a packed barb attribute.
//
// A barb attribute is one wide float tuple per shaft point, and
// GEOinitAttribute() picks the USD type from that width alone:
// float2/float3/float4 at 2, 3 and 4, matrix3d/matrix4d at 9 and 16, a flat
// float array otherwise.
SYS_FORCE_INLINE
BarbFloats
getBarbFloats(
        VtValue &val,
        const HdPrimvarsSchema &pvs,
        const TfToken &barbname)
{
    BarbFloats floats;

    HdPrimvarSchema primvar = pvs.GetPrimvar(barbname);
    if (!primvar)
        return floats;
    HdSampledDataSourceHandle dshandle = primvar.GetFlattenedPrimvarValue();
    if (!dshandle)
        return floats;

    val = dshandle->GetValue(0.0f);

    if (val.IsHolding<VtArray<float>>())
    {
        const auto &typedval = val.Get<VtArray<float>>();
        floats.myFloats = typedval.cdata();
        floats.myCount = typedval.size();
    }
    else if (val.IsHolding<VtArray<GfVec2f>>())
    {
        const auto &typedval = val.Get<VtArray<GfVec2f>>();
        floats.myFloats = (const float *)(typedval).cdata();
        floats.myCount = 2 * typedval.size();
    }
    else if (val.IsHolding<VtArray<GfVec3f>>())
    {
        const auto &typedval = val.Get<VtArray<GfVec3f>>();
        floats.myFloats = (const float *)(typedval).cdata();
        floats.myCount = 3 * typedval.size();
    }
    else if (val.IsHolding<VtArray<GfVec4f>>())
    {
        const auto &typedval = val.Get<VtArray<GfVec4f>>();
        floats.myFloats = (const float *)(typedval).cdata();
        floats.myCount = 4 * typedval.size();
    }
    // GfMatrix3d/GfMatrix4d are plain double[N][N], so the array is one run
    // of doubles.
    else if (val.IsHolding<VtArray<GfMatrix3d>>())
    {
        const auto &typedval = val.Get<VtArray<GfMatrix3d>>();
        floats.myDoubles = (const double *)(typedval).cdata();
        floats.myCount = 9 * typedval.size();
    }
    else if (val.IsHolding<VtArray<GfMatrix4d>>())
    {
        const auto &typedval = val.Get<VtArray<GfMatrix4d>>();
        floats.myDoubles = (const double *)(typedval).cdata();
        floats.myCount = 16 * typedval.size();
    }

    return floats;
}

// Barb points per shaft point, from an attribute whose dim is already known.
// Only sound for P, which is 3 floats per point on either side of the import.
//
// Returns null when the size is not an exact multiple of dim * npts, rather
// than rounding down and skewing every count derived from it.
SYS_FORCE_INLINE
BarbFloats
getBarbData(
        VtValue &val,
        int &nbarbpts,
        const HdPrimvarsSchema &pvs,
        const TfToken &barbname,
        exint npts,
        int shaft_dim)
{
    BarbFloats floats = getBarbFloats(val, pvs, barbname);
    if (!floats.isValid())
        return BarbFloats();

    exint stride = (exint)shaft_dim * npts;
    if (stride <= 0 || floats.myCount % stride != 0)
        return BarbFloats();

    nbarbpts = (int)(floats.myCount / stride);
    return floats;
}

// The rest points expand barbs only when barborient is there too, so the
// topology and primvars have to ask the same before they expand theirs.
SYS_FORCE_INLINE bool
hasBarbOrient(const HdPrimvarsSchema &pvs)
{
    return getConstPvVal<GfQuatf>(pvs, TfToken("barborient"), 0.0f)
            .has_value();
}

// Given the barb counts, return the attribute and how many floats it holds
// per barb point.  That is its own dim, not the shaft primvar's stride: a
// 3-float uv from SOPs feeds a 2-float st, since only the shaft attribute is
// cut and renamed.
//
// It may instead hold one value per shaft point that all of that point's
// barb points share, which GR_PolyRibbonVK's update_attrib() also accepts as
// its tuple_size == vec_size index mode.  one_value_per_barb reports which.
// Per barb point is tried first; the two collide at a broadcast float2
// against 2 barb points, and there the commoner per barb point reading wins.
//
// Returns null when neither layout gives a whole number of floats.
SYS_FORCE_INLINE
BarbFloats
getBarbDataForCount(
        VtValue &val,
        int &barb_dim,
        bool &one_value_per_barb,
        const HdPrimvarsSchema &pvs,
        const TfToken &barbname,
        int npts,
        int nbarbpts)
{
    barb_dim = 0;
    one_value_per_barb = false;

    BarbFloats floats = getBarbFloats(val, pvs, barbname);
    if (!floats.isValid())
        return BarbFloats();

    exint npoints = (exint)npts * nbarbpts;
    if (npoints > 0 && floats.myCount % npoints == 0)
    {
        barb_dim = (int)(floats.myCount / npoints);
        return floats;
    }

    if (npts > 0 && floats.myCount % npts == 0)
    {
        barb_dim = (int)(floats.myCount / npts);
        one_value_per_barb = true;
        return floats;
    }

    return BarbFloats();
}

// Append each shaft point's barb points to the shaft values.
//
// barb_stride steps between barb points in barbl/barbr; how many floats are
// taken from each is T's own.  With one_value_per_barb the step is by shaft
// point instead.
//
// Only P_barbl/P_barbr are stored relative to the shaft point, in its orient
// frame -- GU_Feather applies that frame through its separate
// xformToObject().  Pass xform_to_object for those two; every other barb
// attribute is already in object space.
template <typename T>
SYS_FORCE_INLINE void
expandBarbs(
        VtArray<T> &outshaft,
        int npts,
        int barb_stride,
        int nbarblpts,
        int nbarbrpts,
        BarbFloats barbl,
        BarbFloats barbr,
        const VtArray<GfQuatf> *barborients,
        const VtArray<T> &shaft,
        bool xform_to_object = false,
        bool one_value_per_barb = false)
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
                            int offset = barb_stride
                                    * (one_value_per_barb ? i
                                                        : (i * nbarbpts + j));

                            T value(0.0f);
                            if constexpr (std::is_same_v<T, GfVec3f>)
                            {
                                if (!barb.isValid())
                                {
                                    value = shaft[i];
                                }
                                else
                                {
                                    GfVec3d pos(
                                            barb[offset],
                                            barb[offset + 1],
                                            barb[offset + 2]);

                                    if (xform_to_object)
                                    {
                                        if (barborient != nullptr)
                                            pos = GfRotation(*barborient)
                                                          .TransformDir(pos);
                                        pos += outshaft[i];
                                    }
                                    value = GfVec3f(pos);
                                }
                            }
                            else if constexpr (std::is_same_v<T, GfVec2f>)
                            {
                                if (barb.isValid())
                                    value = GfVec2f(
                                            barb[offset],
                                            barb[offset + 1]);
                                else
                                    value = shaft[i];
                            }
                            else if constexpr (std::is_same_v<T, float>)
                            {
                                if (barb.isValid())
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

// Skin polygons bucketed by an id attribute, one closest-point tree per
// bucket, so a groom point only ever captures against polygons carrying its
// own id.  GU_RayIntersect can only be restricted by a primitive group at
// build time -- there is no per-query filter -- so matching costs one tree
// per id rather than a test per query.  The trees reference the skin gdp
// they were built from and must not outlive it.
struct HD_HairDeformIdBuckets
{
    UT_Map<exint, int> myIntToBucket;
    UT_Array<UT_UniquePtr<GU_RayIntersect>> myTrees;

    int findBucket(exint id) const
    {
        auto it = myIntToBucket.find(id);
        return it == myIntToBucket.end() ? -1 : it->second;
    }
    const GU_RayIntersect *tree(int bucket) const
    {
        if (bucket < 0 || bucket >= myTrees.size())
            return nullptr;
        return myTrees[bucket].get();
    }
};

struct HD_HairDeformSkinMeshCache
{
    UT_Optional<GU_Detail> myGdp;
    GA_AttributeUPtr myNormal;
    GA_AttributeUPtr myTangent;
    UT_Optional<GU_RayIntersect> myRayIntersect;
    UT_UniquePtr<GU_OSDTopology> mySubdTopology;  // non-null for subd meshes

    // Keyed by id attribute name: grooms sharing a skin may name different
    // attributes, and each set of buckets is built on first use.
    UT_StringMap<HD_HairDeformIdBuckets> myIdBuckets;
};

using SkinMeshCacheMapType = UT_ConcurrentHashMap<
        UT_StringHolder,
        HD_HairDeformSkinMeshCache>;

// Feather grooms append barb points after the shaft points: barb j of shaft
// point i lives at npts + (block offset) + nbarbpts * i + j, left block first.
// See HD_HairDeformUtils::expandBarbs().
struct HD_HairDeformBarbLayout
{
    exint myNumShaftPts = 0;
    int myNumBarbL = 0;
    int myNumBarbR = 0;

    bool hasBarbs() const { return myNumBarbL > 0 || myNumBarbR > 0; }

    // Shaft point a barb point hangs off, or -1 if pt is a shaft point.
    // The layout is left all-zero for a groom without barbs, so every point
    // has to come back as a shaft point rather than dividing by zero.
    exint shaftPoint(exint pt) const
    {
        if (!hasBarbs())
            return -1;
        exint rel = pt - myNumShaftPts;
        if (rel < 0)
            return -1;
        exint lsize = (exint)myNumBarbL * myNumShaftPts;
        if (rel < lsize)
            return rel / myNumBarbL;
        return (rel - lsize) / myNumBarbR;
    }
};

struct HD_HairDeformRestPointsCache
{
    VtArray<GfVec3f> myPoints;
    HD_HairDeformBarbLayout myBarbLayout;
};

using RestPointsCacheMapType
        = UT_ConcurrentHashMap<UT_StringHolder, HD_HairDeformRestPointsCache>;

struct HD_HairDeformSurfaceTopoCache
{
    UT_IntArray myPrimPtStarts;
    UT_IntArray myPtIndices;
    UT_FloatArray myPtWeights;
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

// A target's animated points, keyed by prim path and shutter offset.
//
// Skel-skinned targets are read through a pruning scene index that resolves
// the skinning ext computation into primvars/points, and that resolution
// caches nothing: every pull re-executes the whole computation network (see
// the XXX in hdsi/extComputationPrimvarPruningSceneIndex.cpp).  Without this
// the skin would be re-skinned once per groom that shares it, per motion
// sample.  Dropped whenever the target is dirtied at all, so a cached entry
// can never outlive the notice that changed it.
//
// Filled under myExclusive rather than the map accessor: the pull runs the
// skinning in parallel, and a thread blocked on an accessor it already holds
// (via a stolen task for another groom on the same skin) never wakes.
namespace HD_HairDeformUtils
{
struct ResolvedPointsPull;
}

struct HD_HairDeformResolvedPoints
{
    UT_TaskExclusive<HD_HairDeformUtils::ResolvedPointsPull> myExclusive;
    VtVec3fArray myPoints;
    bool myValid = false;
};

using ResolvedPointsCacheMapType = UT_ConcurrentHashMap<
        UT_StringHolder, UT_SharedPtr<HD_HairDeformResolvedPoints>>;

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
using ResolvedPointsCacheMapPtr = std::shared_ptr<ResolvedPointsCacheMapType>;

namespace HD_HairDeformUtils
{
// Run by HD_HairDeformResolvedPoints::myExclusive, once per entry.
struct ResolvedPointsPull
{
    HD_HairDeformResolvedPoints &myEntry;
    const HdPrimvarsSchema &myPrimvars;
    HdSampledDataSource::Time myShutterOffset;
    const UT_StringHolder &myKey;

    void operator()()
    {
        cacheLog("HairDeform: CACHE MISS _resolvedpointscachemap[{}], "
                "pulling...", myKey);
        auto val = getConstPvVal<GfVec3f>(
                myPrimvars, HdTokens->points, myShutterOffset);
        myEntry.myValid = val.has_value();
        if (val.has_value())
            myEntry.myPoints = *val;
    }
};

// A target's animated points, pulled once per (prim, shutter offset) and
// shared by every groom bound to it.  Falls straight through when there is
// no cache to use.
SYS_FORCE_INLINE UT_Optional<const VtVec3fArray>
getAnimPoints(
        const HdPrimvarsSchema &primvars,
        const SdfPath &primpath,
        HdSampledDataSource::Time shutteroffset,
        ResolvedPointsCacheMapType *cachemap)
{
    if (!cachemap || !primvars || primpath.IsEmpty())
        return getConstPvVal<GfVec3f>(primvars, HdTokens->points, shutteroffset);

    const UT_StringHolder key
            = makeResolvedPointsCacheKey(primpath.GetText(), shutteroffset);

    // The accessor only hands out the entry; a second groom on the same
    // target waits on myExclusive, helping with the pull rather than
    // repeating it.
    UT_SharedPtr<HD_HairDeformResolvedPoints> entry;
    {
        ResolvedPointsCacheMapType::accessor acc;
        if (cachemap->insert(acc, key))
            acc->second = UTmakeShared<HD_HairDeformResolvedPoints>();
        entry = acc->second;
    }

    ResolvedPointsPull pull{*entry, primvars, shutteroffset, key};
    entry->myExclusive.execute(pull);

    if (!entry->myValid)
        return UT_NULLOPT;
    return entry->myPoints;
}
}  // namespace HD_HairDeformUtils

PXR_NAMESPACE_CLOSE_SCOPE
