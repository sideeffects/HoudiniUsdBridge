/*
 * Copyright 2023 Side Effects Software Inc.
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

#include "HUSD_CrowdProcedural.h"

#include "HUSD_DataHandle.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_PathSet.h"
#include "XUSD_Data.h"
#include "XUSD_Format.h"
#include "XUSD_Utils.h"

#include <SYS/SYS_Hash.h>
#include <UT/UT_Algorithm.h>
#include <UT/UT_Array.h>
#include <UT/UT_BitArray.h>
#include <UT/UT_BoundingBox.h>
#include <UT/UT_BoundingRect.h>
#include <UT/UT_Date.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_Map.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_Set.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_WorkBuffer.h>
#include <gusd/UT_Gf.h>

#include <array>
#include <numeric>

#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/relationshipSpec.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdSkel/bakeSkinning.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/utils.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
/// The number of LOD groups. The highest LOD group uses a division size of
/// 2^(n-1), e.g. 32 if there are 6 groups.
static constexpr int NUM_LOD_GROUPS = 6;
static constexpr int BUCKET_DIVS = 1 << (NUM_LOD_GROUPS - 1);

/// Type to store a packed bucket coordinate. For example, if the maximum bucket
/// index is 32^3 = 2^15, this fits into a signed int16.
using husdBucketCoord = int16;
static_assert(
        exint(std::numeric_limits<husdBucketCoord>::max())
        >= ((1 << (3 * (NUM_LOD_GROUPS - 1))) - 1));

/// Construct a packed bucket coordinate.
constexpr husdBucketCoord
husdToBucketCoord(husdBucketCoord x, husdBucketCoord y, husdBucketCoord z)
{
    return x * BUCKET_DIVS * BUCKET_DIVS + y * BUCKET_DIVS + z;
}

/// Number of divisions for each LOD group.
/// Each LOD group uses half as many divisions, e.g. 32, 16, 8, etc
static constexpr std::array<int, NUM_LOD_GROUPS> theLODDivs = []() constexpr
{
    std::array<int, NUM_LOD_GROUPS> divs = {};
    for (int i = 0; i < NUM_LOD_GROUPS; ++i)
        divs[i] = BUCKET_DIVS >> i;

    return divs;
}();

/// Precomputed masks to compute the husdBucketCoord for the other LOD groups
/// from the original coordinates of the first LOD group.
/// For example, if BUCKET_DIVS is 32 then masks[1] is 0x7bde and (coords &
/// 0x7bde) will round each coordinate to the nearest multiple of 2, giving us
/// 16^3 possible values.
static constexpr std::array<husdBucketCoord, NUM_LOD_GROUPS> theLODCoordMasks = []() constexpr
{
    std::array<husdBucketCoord, NUM_LOD_GROUPS> masks = {};

    for (int i = 0; i < NUM_LOD_GROUPS; ++i)
    {
        husdBucketCoord coord_mask = BUCKET_DIVS - (1 << i);
        masks[i] = husdToBucketCoord(coord_mask, coord_mask, coord_mask);
    }

    return masks;
}();

// TODO - factor this out into UT and unify with BRAY_Measure
class husdMeasure
{
public:
    void setPerspective(int xres, int yres, const UT_Matrix4D &projmat)
    {
        myOrtho = false;
        float yaperture = 1.0f / projmat[1][1];
        float xaperture = 1.0f / projmat[0][0];
        float yfov = 2.0f * atan(yaperture);
        float xfov = 2.0f * atan(xaperture);
        myIXSinc = (float)xres / xfov;
        myIYSinc = (float)yres / yfov;

        // offscreen quality prep (same as in RAY_Measure)
        float minangle = SYSatan(
                SYSsqrt(xaperture * xaperture + yaperture * yaperture));

        // negate to ensure min is smaller (for smooth())
        myMinOffscreen = -SYScos(minangle);
        myMaxOffscreen = -SYScos(SYSmin(1.5F * minangle, M_PI));
    }

    void setOrtho(int xres, int yres, float orthowidth, float orthoheight)
    {
        myOrtho = true;
        myIXSinc = (float)xres / orthowidth;
        myIYSinc = (float)yres / orthoheight;

        // offscreen quality prep
        myMinOffscreen
                = SYSsqrt(orthowidth * orthowidth + orthoheight * orthoheight);
        myMaxOffscreen = myMinOffscreen * 1.5f;
    }

    // Approx projected area of bbox in raster space.
    // (code from RAY_Measure)
    float getArea(const UT_BoundingBox &box) const
    {
        if (myOrtho)
        {
            UT_BoundingRect rect;
            rect.initBounds(
                    box.vals[0][0], box.vals[0][1], box.vals[1][0],
                    box.vals[1][1]);

            return (rect.xsize() * myIXSinc) * (rect.ysize() * myIYSinc)
                   * offscreenMult(box.center());
        }

        if (box.isInside(UT_Vector3(0, 0, 0)))
            return 1e12; // Box contains the origin

        UT_Vector3 size = box.size();
        UT_Vector3 pos = box.minDistDelta(UT_Vector3(0.0f));
        UT_Vector3 dir = pos;

        float odist = dir.normalize();
        float osize;

        // Find the projected area of the box
        osize = size.x() * size.y() * SYSabs(dir.z());
        osize += size.y() * size.z() * SYSabs(dir.x());
        osize += size.z() * size.x() * SYSabs(dir.y());

        float sinc = myIXSinc * myIYSinc;

        // Project the box area onto the unit sphere, and measure the area of
        // the projection
        return 0.5F * osize * sinc / (odist * odist) * offscreenMult(pos);
    }

    void setOffscreenQuality(float quality)
    {
	myOffscreenQuality = quality;
    }

private:
    float offscreenMult(const UT_Vector3 &p) const
    {
        float dist;
        if (myOrtho)
            dist = SYSsqrt(p[0] * p[0] + p[1] * p[1]);
        else
            dist = -p.z() / p.length();
        return SYSlerp(
                1.0F, myOffscreenQuality,
                SYSsmooth(myMinOffscreen, myMaxOffscreen, dist));
    }

    float myIXSinc = 0;
    float myIYSinc = 0;
    bool myOrtho = false;

    float myMinOffscreen = 0;
    float myMaxOffscreen = 0;
    float myOffscreenQuality = 0.1f;
};

/// Helper to compute the concatenated poses (joint transforms and blendshapes)
/// for the skeleton(s) under a SkelRoot. The joint transforms are transformed
/// into the space of the SkelRoot prim, since each skeleton can be separately
/// transformed.
class husdComputeSkelRootPose
{
public:
    bool computeJointXforms(
            UsdGeomXformCache &xform_cache,
            const UsdSkelCache &skel_cache,
            const Usd_PrimFlagsPredicate &traversal_predicate,
            const UsdSkelRoot &skel_root,
            const UsdTimeCode &time_sample,
            std::vector<GfMatrix4d> &skel_xforms)
    {
        skel_xforms.clear();

        if (!computeSkelBindings(skel_cache, traversal_predicate, skel_root))
            return false;

        // If there are multiple skeletons under a SkelRoot, they are
        // just combined together and treated as one larger skeleton.
        // (This typically happens when there are different bind poses per
        // shape, which requires separate skeletons)
        for (const UsdSkelBinding &skel_binding : mySkelBindings)
        {
            const UsdSkelSkeleton &skel = skel_binding.GetSkeleton();

            const UsdSkelSkeletonQuery skel_query
                    = skel_cache.GetSkelQuery(skel);
            if (!skel_query)
            {
                UT_WorkBuffer msg;
                msg.format(
                        "Invalid skeleton query for prim '{}'",
                        skel.GetPath());
                HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
                continue;
            }

            // We only need to compare joints which are actually used by the
            // skinned primitives, and can ignore any other joints on the
            // skeleton.
            const exint num_joints = skel_query.GetTopology().GetNumJoints();
            myReferencedSkelJoints.setSize(num_joints);
            myReferencedSkelJoints.setAllBits(false);

            mySkelJointIndices.resize(num_joints);
            std::iota(mySkelJointIndices.begin(), mySkelJointIndices.end(), 0);

            for (auto &&skinning_target : skel_binding.GetSkinningTargets())
            {
                mySkinJointIndices.clear();

                // If the skinned primitive uses all of the skeleton's joints,
                // we don't need to examine any more prims.
                if (!skinning_target.GetJointMapper()
                    || skinning_target.GetJointMapper()->IsIdentity())
                {
                    myReferencedSkelJoints.setAllBits(true);
                    break;
                }

                // Use the skinning query's joint mapper to produce a list of
                // the joint indices which are referenced by the skin.
                static constexpr int theDefaultValue = -1;
                skinning_target.GetJointMapper()->Remap(
                        mySkelJointIndices, &mySkinJointIndices,
                        /* elementSize */ 1,
                        /* defaultValue */ &theDefaultValue);

                for (int joint_idx : mySkinJointIndices)
                {
                    if (joint_idx >= 0)
                        myReferencedSkelJoints.setBitFast(joint_idx, true);
                }
            }

            mySkelXforms.clear();
            skel_query.ComputeJointSkelTransforms(&mySkelXforms, time_sample);

            // Since joints from multiple skeletons can be combined, transform
            // into a sensible common space (the SkelRoot).
            bool reset_xform_stack = false;
            GfMatrix4d to_skel_root = xform_cache.ComputeRelativeTransform(
                    skel.GetPrim(), skel_root.GetPrim(), &reset_xform_stack);

            for (exint i = 0; i < num_joints; ++i)
            {
                if (myReferencedSkelJoints.getBitFast(i))
                    skel_xforms.push_back(mySkelXforms[i] * to_skel_root);
            }
        }

        return true;
    }

    bool computeBlendShapeWeights(
            const UsdSkelCache &skel_cache,
            const Usd_PrimFlagsPredicate &traversal_predicate,
            const UsdSkelRoot &skel_root,
            const UsdTimeCode &time_sample,
            UT_Array<float> &all_weights)
    {
        all_weights.clear();

        if (!computeSkelBindings(skel_cache, traversal_predicate, skel_root))
            return false;

        // If there are multiple skeletons under a SkelRoot, they are
        // just combined together and treated as one larger skeleton.
        // (This typically happens when there are different bind poses per
        // shape, which requires separate skeletons)
        for (const UsdSkelBinding &skel_binding : mySkelBindings)
        {
            const UsdSkelSkeleton &skel = skel_binding.GetSkeleton();

            const UsdSkelSkeletonQuery skel_query
                    = skel_cache.GetSkelQuery(skel);
            if (!skel_query)
            {
                UT_WorkBuffer msg;
                msg.format(
                        "Invalid skeleton query for prim '{}'", skel.GetPath());
                HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
                continue;
            }

            const UsdSkelAnimQuery &anim_query = skel_query.GetAnimQuery();
            if (!anim_query.IsValid())
                continue;

            const exint num_shapes = anim_query.GetBlendShapeOrder().size();
            if (!num_shapes)
                continue;

            // We only need to compare weights which are actually used by the
            // skinned primitives.
            myReferencedBlendShapes.setSize(num_shapes);
            myReferencedBlendShapes.setAllBits(false);

            mySkelBlendShapeIndices.resize(num_shapes);
            std::iota(
                    mySkelBlendShapeIndices.begin(),
                    mySkelBlendShapeIndices.end(), 0);

            for (auto &&skinning_target : skel_binding.GetSkinningTargets())
            {
                mySkinBlendShapeIndices.clear();

                if (!skinning_target.HasBlendShapes())
                    continue;

                // If the skinned primitive uses all of the skeleton's blend
                // shapes, we don't need to examine any more prims.
                if (!skinning_target.GetBlendShapeMapper()
                    || skinning_target.GetBlendShapeMapper()->IsIdentity())
                {
                    myReferencedBlendShapes.setAllBits(true);
                    break;
                }

                // Use the skinning query's mapper to produce a list of
                // the shape indices which are referenced by the skin.
                static constexpr int theDefaultValue = -1;
                skinning_target.GetBlendShapeMapper()->Remap(
                        mySkelBlendShapeIndices, &mySkinBlendShapeIndices,
                        /* elementSize */ 1,
                        /* defaultValue */ &theDefaultValue);

                for (int shape_idx : mySkinBlendShapeIndices)
                {
                    if (shape_idx >= 0)
                        myReferencedBlendShapes.setBitFast(shape_idx, true);
                }
            }

            mySkelBlendShapeWeights.clear();
            anim_query.ComputeBlendShapeWeights(
                    &mySkelBlendShapeWeights, time_sample);

            for (exint i = 0; i < num_shapes; ++i)
            {
                if (myReferencedBlendShapes.getBitFast(i))
                    all_weights.append(mySkelBlendShapeWeights[i]);
            }
        }

        return true;
    }

private:
    bool computeSkelBindings(
            const UsdSkelCache &skel_cache,
            const Usd_PrimFlagsPredicate &traversal_predicate,
            const UsdSkelRoot &skel_root)
    {
        mySkelBindings.clear();

        if (!skel_cache.ComputeSkelBindings(
                    skel_root, &mySkelBindings, traversal_predicate))
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Failed to compute skeleton bindings for prim '{}'",
                    skel_root.GetPrim().GetPath());
            HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
            return false;
        }

        return true;
    }

    // Cached to avoid reallocating for every prim.
    std::vector<UsdSkelBinding> mySkelBindings;
    VtArray<GfMatrix4d> mySkelXforms;
    VtFloatArray mySkelBlendShapeWeights;

    UT_BitArray myReferencedSkelJoints;
    VtArray<int> mySkelJointIndices;
    VtArray<int> mySkinJointIndices;

    UT_BitArray myReferencedBlendShapes;
    VtArray<int> mySkelBlendShapeIndices;
    VtArray<int> mySkinBlendShapeIndices;
};

/// Compute the maximum skeleton-space extent from all instances in a group.
/// This is used for computing the bucketed poses.
class husdComputeMaxExtentTask
{
public:
    husdComputeMaxExtentTask(
            const UsdStageConstPtr &stage,
            const UT_Array<HUSD_Path> &instances,
            UsdSkelCache &skel_cache,
            const UsdTimeCode &time_sample)
        : myStage(stage)
        , myInstances(instances)
        , mySkelCache(skel_cache)
        , myTimeSample(time_sample)
        , myXformCache(time_sample)
    {
    }

    husdComputeMaxExtentTask(const husdComputeMaxExtentTask &src, UT_Split)
        : myStage(src.myStage)
        , myInstances(src.myInstances)
        , mySkelCache(src.mySkelCache)
        , myTimeSample(src.myTimeSample)
        , myXformCache(src.myTimeSample)
    {
    }

    const GfRange3f &getMaxExtent() const { return myMaxExtent; }

    void operator()(const UT_BlockedRange<exint> &range)
    {
        const auto traversal_predicate
                = UsdTraverseInstanceProxies(UsdPrimDefaultPredicate);

        husdComputeSkelRootPose compute_xforms;
        std::vector<GfMatrix4d> skel_xforms;

        for (exint path_idx : range.items())
        {
            UsdPrim prim
                    = myStage->GetPrimAtPath(myInstances[path_idx].sdfPath());
            UsdSkelRoot skelroot(prim);
            UT_ASSERT(skelroot);

            mySkelCache.Populate(skelroot, traversal_predicate);

            if (!compute_xforms.computeJointXforms(
                        myXformCache, mySkelCache, traversal_predicate,
                        skelroot, myTimeSample, skel_xforms))
            {
                continue;
            }

            UsdSkelComputeJointsExtent(
                    TfMakeConstSpan(skel_xforms), &myMaxExtent);
        }
    }

    void join(husdComputeMaxExtentTask &other)
    {
        myMaxExtent.UnionWith(other.myMaxExtent);
    }

private:
    UsdStageConstPtr myStage;
    const UT_Array<HUSD_Path> &myInstances;
    UsdSkelCache &mySkelCache; // Note this cache is thread-safe.
    const UsdTimeCode myTimeSample;

    UsdGeomXformCache myXformCache;
    GfRange3f myMaxExtent;
};

/// For agents that aren't part of the normal LOD-based optimizations, we still
/// allow finding agents that have *exactly* the same pose.
/// Since this applies to foreground agents, also take blendshape weights into
/// account.
class husdExactPose
{
public:
    explicit husdExactPose(
            TfSpan<const GfMatrix4d> pose,
            TfSpan<const float> blend_weights)
    {
        myXforms.setSizeNoInit(pose.size());

        for (exint i = 0, n = myXforms.size(); i < n; ++i)
        {
            myXforms[i] = GusdUT_Gf::Cast(pose[i]);
            SYShashRange(
                    myHash, myXforms[i].data(),
                    myXforms[i].data() + UT_Matrix4D::tuple_size);
        }

        myBlendShapeWeights.setSizeNoInit(blend_weights.size());
        std::copy(
                blend_weights.begin(), blend_weights.end(),
                myBlendShapeWeights.begin());
        SYShashRange(
                myHash, myBlendShapeWeights.begin(), myBlendShapeWeights.end());
    }

    inline size_t hash() const { return myHash; }

    inline bool operator==(const husdExactPose &other) const
    {
        return myXforms == other.myXforms
               && myBlendShapeWeights == other.myBlendShapeWeights;
    }

private:
    UT_Array<UT_Matrix4D> myXforms;
    UT_Array<float> myBlendShapeWeights;
    size_t myHash = 0;
};

static inline size_t
hash_value(const husdExactPose &pose)
{
    return pose.hash();
}

/// Represents a bucketed pose.
class husdLODPose
{
public:
    husdLODPose() = default;

    /// Computes the pose at the highest LOD level (0).
    husdLODPose(TfSpan<const GfMatrix4d> pose, const UT_BoundingBoxD &box);

    /// Reduces the pose to a lower LOD level.
    void reduceLOD(int lod_level);

    bool operator==(const husdLODPose& other) const;
    bool operator!=(const husdLODPose &other) const
    {
        return !(*this == other);
    }

    size_t hash() const;

private:
    UT_Array<husdBucketCoord> myCoords;
    mutable size_t myHash = 0;
};

husdLODPose::husdLODPose(
        TfSpan<const GfMatrix4d> pose,
        const UT_BoundingBoxD &box)
{
    const exint num_joints = pose.size();
    myCoords.setSizeNoInit(num_joints);

    for (exint i = 0; i < num_joints; ++i)
    {
        GfVec3d t = pose[i].ExtractTranslation();

        husdBucketCoord c[3];
        for (int i = 0; i < 3; ++i)
            c[i] = SYSfloor(SYSfit(t[i], box(i, 0), box(i, 1), 0, BUCKET_DIVS));

        myCoords[i] = husdToBucketCoord(c[0], c[1], c[2]);
    }
}

void
husdLODPose::reduceLOD(int lod_level)
{
    UT_ASSERT(lod_level <= NUM_LOD_GROUPS);

    const husdBucketCoord mask = theLODCoordMasks[lod_level];
    for (husdBucketCoord &coord : myCoords)
        coord &= mask;

    myHash = 0;
}

inline size_t
hash_value(const husdLODPose &pose)
{
    return pose.hash();
}

inline bool
husdLODPose::operator==(const husdLODPose &other) const
{
    return myCoords == other.myCoords;
}

inline size_t
husdLODPose::hash() const
{
    if (!myHash)
    {
        SYShashRange(myHash, myCoords.begin(), myCoords.end());
        if (myHash == 0)
            myHash = 1;
    }

    return myHash;
}

/// Equivalent to RAY_Procedural::getLevelOfDetail().
inline fpreal
husdGetLevelOfDetail(
        const husdMeasure &measure,
        const UT_BoundingBoxD &camera_space_bounds)
{
    return SYSsqrt(measure.getArea(camera_space_bounds));
}

/// Compute the LOD group that the agent belongs to. Choose the lowest LOD
/// group where the voxel quantization size <= ~1 pixel (or the custom LOD
/// threshold). An LOD group of -1 indicates that the agent should never become
/// an instance of another agent.
inline int
husdGetLODGroup(fpreal lod, fpreal lod_limit)
{
    for (int i = NUM_LOD_GROUPS - 1; i >= 0; --i)
    {
        if (SYSisLessOrEqual(lod, (lod_limit * theLODDivs[i]) / BUCKET_DIVS))
            return i;
    }

    return -1;
}

/// A group of instances which should share the same exemplar. The choice of
/// which instance becomes the exemplar can change as instances are added.
struct husdExemplarInfo
{
    husdExemplarInfo(exint exemplar_idx, exint exemplar_lod)
        : myExemplarIdx(exemplar_idx), myExemplarLOD(exemplar_lod)
    {
    }

    /// Add an agent which can be used as an exemplar for the pose.
    /// The agent closest to the camera is preferred, since that allows distant
    /// agents to become an instance of the closer agent (the closer agent may
    /// be ineligible to instance another agent) and reduce the number of unique
    /// deformed prims.
    void updateBestExemplar(exint exemplar_idx, exint exemplar_lod)
    {
        if (exemplar_lod < myExemplarLOD)
        {
            myExemplarLOD = exemplar_lod;
            myExemplarIdx = exemplar_idx;
        }
    }

    /// Instance id of the exemplar agent.
    exint myExemplarIdx = -1;
    /// LOD level of the exemplar agent.
    exint myExemplarLOD = -1;
    /// Instance ids which should become an instance of the exemplar.
    UT_Array<exint> myInstances;
};

using husdPoseMap = UT_Map<husdLODPose, husdExemplarInfo>;
using husdPoseMapList = std::array<husdPoseMap, NUM_LOD_GROUPS>;
using husdExactPoseMap = UT_Map<husdExactPose, husdExemplarInfo>;

/// Determine which agents should become exemplars, and which should become
/// instances of those exemplars.
class husdFindExemplarsTask
{
public:
    husdFindExemplarsTask(
            const UsdStageConstPtr &stage,
            const UT_Array<HUSD_Path> &instances,
            UsdSkelCache &skel_cache,
            const UsdTimeCode &time_sample,
            fpreal lod_threshold,
            bool optimize_identical_poses,
            const husdMeasure &measure,
            const UT_Matrix4D &world_to_camera_xform,
            const UT_BoundingBoxD &max_extent)
        : myStage(stage)
        , myInstances(instances)
        , mySkelCache(skel_cache)
        , myTimeSample(time_sample)
        , myLODThreshold(lod_threshold)
        , myOptimizeIdenticalPoses(optimize_identical_poses)
        , myMeasure(measure)
        , myWorldToCameraXform(world_to_camera_xform)
        , myMaxExtent(max_extent)
        , myXformCache(time_sample)
    {
    }

    husdFindExemplarsTask(const husdFindExemplarsTask &src, UT_Split)
        : myStage(src.myStage)
        , myInstances(src.myInstances)
        , mySkelCache(src.mySkelCache)
        , myTimeSample(src.myTimeSample)
        , myLODThreshold(src.myLODThreshold)
        , myOptimizeIdenticalPoses(src.myOptimizeIdenticalPoses)
        , myMeasure(src.myMeasure)
        , myWorldToCameraXform(src.myWorldToCameraXform)
        , myMaxExtent(src.myMaxExtent)
        , myXformCache(myTimeSample)
    {
    }

    void stealResult(
            husdPoseMapList &pose_maps,
            husdExactPoseMap &exact_pose_map)
    {
        pose_maps = std::move(myPoseMaps);
        exact_pose_map = std::move(myExactPoseMap);
    }

    void operator()(const UT_BlockedRange<exint> &range)
    {
        // Compute the size of a single bucket in skeleton space.
        UT_BoundingBoxD div_box;
        div_box.initBounds(myMaxExtent.center());
        div_box.expandBounds(
                myMaxExtent.xsize() / (BUCKET_DIVS * 2),
                myMaxExtent.ysize() / (BUCKET_DIVS * 2),
                myMaxExtent.zsize() / (BUCKET_DIVS * 2));

        husdComputeSkelRootPose compute_pose;
        std::vector<GfMatrix4d> skel_xforms;
        UT_Array<float> skel_weights;
        const auto traversal_predicate
                = UsdTraverseInstanceProxies(UsdPrimDefaultPredicate);

        for (exint instance_idx : range.items())
        {
            UsdPrim prim = myStage->GetPrimAtPath(
                    myInstances[instance_idx].sdfPath());
            UsdSkelRoot skelroot(prim);
            UT_ASSERT(skelroot);

            // Note: we should have already populated the cache when computing
            // the max extent.
            if (!compute_pose.computeJointXforms(
                        myXformCache, mySkelCache, traversal_predicate,
                        skelroot, myTimeSample, skel_xforms))
            {
                continue;
            }

            // Transform the bucket to world space, and then estimate the screen
            // space size of the bucket.
            const GfMatrix4d skelroot_xform
                    = myXformCache.GetLocalToWorldTransform(prim);
            UT_Matrix4D skelroot_to_camera = GusdUT_Gf::Cast(skelroot_xform)
                                             * myWorldToCameraXform;

            UT_BoundingBoxD lod_box;
            div_box.transform(skelroot_to_camera, lod_box);

            const fpreal agent_lod = husdGetLevelOfDetail(myMeasure, lod_box);
            const int lod_group = husdGetLODGroup(agent_lod, myLODThreshold);

            husdLODPose pose(skel_xforms, myMaxExtent);
            for (int i = 0; i < NUM_LOD_GROUPS; ++i)
            {
                if (i > 0)
                    pose.reduceLOD(i);

                husdPoseMap &pose_map = myPoseMaps[i];

                // Record the poses.
                // If there are multiple similar poses at a lower LOD
                // level, use the one that comes from the highest LOD
                // agent. For example, if an agent that is very close to
                // the camera has a similar pose to an agent that is far
                // away, the distant agent should reuse the shape from
                // the close agent (the close agent may be ineligible to
                // instance another agent).
                auto it = pose_map.find(pose);
                if (it == pose_map.end())
                {
                    husdExemplarInfo info(instance_idx, lod_group);
                    it = pose_map.emplace(pose, std::move(info)).first;
                }
                else
                {
                    it->second.updateBestExemplar(instance_idx, lod_group);
                }

                // If this agent is eligible for reduction, mark it as
                // needing to become an instance of the final exemplar
                // choice.
                if (i == lod_group)
                    it->second.myInstances.append(instance_idx);
            }

            if (myOptimizeIdenticalPoses && lod_group < 0)
            {
                // Blend shape weights are included when comparing exact poses
                // since this may include foreground agents.
                UT_VERIFY(compute_pose.computeBlendShapeWeights(
                        mySkelCache, traversal_predicate, skelroot,
                        myTimeSample, skel_weights));

                husdExactPose pose(skel_xforms, skel_weights);

                auto it = myExactPoseMap.find(pose);
                if (it == myExactPoseMap.end())
                {
                    it = myExactPoseMap.emplace(
                            std::move(pose),
                            husdExemplarInfo(instance_idx, -1)).first;
                }

                it->second.myInstances.append(instance_idx);
            }
        }
    }

    void join(husdFindExemplarsTask &other)
    {
        for (exint i = 0, n = myPoseMaps.size(); i < n; ++i)
            joinPoseMaps(myPoseMaps[i], other.myPoseMaps[i]);

        joinPoseMaps(myExactPoseMap, other.myExactPoseMap);
    }

private:
    template <typename PoseT>
    void joinPoseMaps(
            UT_Map<PoseT, husdExemplarInfo> &map,
            UT_Map<PoseT, husdExemplarInfo> &other_map)
    {
        for (auto &&[other_pose, other_exemplar_info] : other_map)
        {
            auto it = map.find(other_pose);
            if (it == map.end())
                map.emplace(other_pose, std::move(other_exemplar_info));
            else
            {
                it->second.updateBestExemplar(
                        other_exemplar_info.myExemplarIdx,
                        other_exemplar_info.myExemplarLOD);
                it->second.myInstances.concat(
                        std::move(other_exemplar_info.myInstances));
            }
        }
    }

    UsdStageConstPtr myStage;
    const UT_Array<HUSD_Path> &myInstances;
    UsdSkelCache &mySkelCache; // Note this cache is thread-safe.
    const UsdTimeCode myTimeSample;
    const fpreal myLODThreshold;
    const bool myOptimizeIdenticalPoses;
    const husdMeasure &myMeasure;
    const UT_Matrix4D &myWorldToCameraXform;
    const UT_BoundingBoxD &myMaxExtent;

    UsdGeomXformCache myXformCache;
    husdPoseMapList myPoseMaps;
    husdExactPoseMap myExactPoseMap;
};

/// A set of instances of the same prototype SkelRoot (i.e. the bind state,
/// similar to https://openusd.org/dev/api/_usd_skel__instancing.html).
class husdSkelInstanceGroup
{
public:
    void addInstance(const HUSD_Path &instance_path);

    void findExemplars(
            const UsdStageConstPtr &stage,
            const UsdTimeCode &time_sample,
            fpreal lod_threshold,
            bool optimize_identical_poses,
            const husdMeasure &measure,
            const UT_Matrix4D &world_to_camera_xform);

    /// After findExemplars() has been called, returns a list of the exemplar
    /// poses and agents to become instances of those exemplars.
    void getPathsToReduce(
            UT_Set<HUSD_Path> &exemplars_to_bake,
            UT_Map<HUSD_Path, HUSD_Path> &paths_to_reduce,
            UT_Array<HUSD_Path> *paths_skipped) const;

private:
    UT_BoundingBoxD computeMaxExtent(
            const UsdStageConstPtr &stage,
            const UsdTimeCode &time_sample);

    UsdSkelCache mySkelCache; // Note this cache is thread-safe.
    UT_Array<HUSD_Path> myInstances;

    husdPoseMapList myPoseMaps;
    husdExactPoseMap myExactPoseMap;
};

inline void
husdSkelInstanceGroup::addInstance(const HUSD_Path &instance_path)
{
    myInstances.append(instance_path);
}

UT_BoundingBoxD
husdSkelInstanceGroup::computeMaxExtent(
        const UsdStageConstPtr &stage,
        const UsdTimeCode &time_sample)
{
    husdComputeMaxExtentTask task(
            stage, myInstances, mySkelCache, time_sample);
    UTparallelReduce(UT_BlockedRange<exint>(0, myInstances.size()), task);

    return UT_BoundingBoxD(
            GusdUT_Gf::Cast(task.getMaxExtent().GetMin()),
            GusdUT_Gf::Cast(task.getMaxExtent().GetMax()));
}

void
husdSkelInstanceGroup::findExemplars(
        const UsdStageConstPtr &stage,
        const UsdTimeCode &time_sample,
        fpreal lod_threshold,
        bool optimize_identical_poses,
        const husdMeasure &measure,
        const UT_Matrix4D &world_to_camera_xform)
{
    const UT_BoundingBoxD max_extent = computeMaxExtent(stage, time_sample);

    husdFindExemplarsTask task(
            stage, myInstances, mySkelCache, time_sample, lod_threshold,
            optimize_identical_poses, measure, world_to_camera_xform,
            max_extent);
    // Note that the grain size shouldn't be too small, since join() is somewhat
    // expensive.
    UTparallelReduce(
            UT_BlockedRange<exint>(0, myInstances.size()), task, 1, 32);
    task.stealResult(myPoseMaps, myExactPoseMap);

#if 0 // Debugging information: statistics for each LOD level.
    for (int i = 0; i < NUM_LOD_GROUPS; ++i)
    {
        UTdebugPrint(
                "lod group", i, " -> ", myPoseMaps[i].size(), "exemplars from",
                myInstances.size(), "agents");
    }
#endif

#if 0 // Debugging information: statistics for identical poses.
    for (auto &&[pose, exemplar] : myExactPoseMap)
    {
        UTdebugPrint(
                "exact pose", exemplar.myExemplarIdx, "instances",
                exemplar.myInstances.size());
    }
#endif
}

void
husdRecordPathsToReduce(
        const UT_Array<HUSD_Path> &all_instance_paths,
        const HUSD_Path &exemplar_path,
        const UT_Array<exint> &instance_ids,
        UT_Set<HUSD_Path> &exemplars_to_bake,
        UT_Map<HUSD_Path, HUSD_Path> &paths_to_reduce)
{
    exint count = 0;
    for (exint instance_idx : instance_ids)
    {
        const HUSD_Path &instance_path = all_instance_paths[instance_idx];
        if (instance_path != exemplar_path)
        {
            paths_to_reduce.emplace(instance_path, exemplar_path);
            ++count;
        }
    }

    // If there were any other instances, the exemplar should be baked
    // and then become an instance of the baked result.
    if (count > 0 && !exemplars_to_bake.contains(exemplar_path))
    {
        exemplars_to_bake.insert(exemplar_path);

        UT_WorkBuffer name;
        name.format("{}_instance", exemplar_path.nameStr());
        HUSD_Path instance_path = exemplar_path.parentPath().appendChild(name);
        paths_to_reduce.emplace(instance_path, exemplar_path);
    }
}

void
husdSkelInstanceGroup::getPathsToReduce(
        UT_Set<HUSD_Path> &exemplars_to_bake,
        UT_Map<HUSD_Path, HUSD_Path> &paths_to_reduce,
        UT_Array<HUSD_Path> *paths_skipped) const
{
    for (exint lod_group = 0; lod_group < NUM_LOD_GROUPS; ++lod_group)
    {
        const husdPoseMap &pose_map = myPoseMaps[lod_group];

        for (auto &&[_, exemplar_info] : pose_map)
        {
            if (exemplar_info.myInstances.isEmpty())
            {
                // If there are no instances of this pose, just leave the
                // exemplar as-is and let the skinning computation run in Hydra
                // as usual.
                continue;
            }

            const HUSD_Path &exemplar_path
                    = myInstances[exemplar_info.myExemplarIdx];
            husdRecordPathsToReduce(
                    myInstances, exemplar_path, exemplar_info.myInstances,
                    exemplars_to_bake, paths_to_reduce);
        }
    }

    for (auto &&[_, exemplar_info] : myExactPoseMap)
    {
        if (exemplar_info.myInstances.isEmpty())
            continue;

        // Since these agents have identical poses, it's possible for exactly
        // one of the agents to be an exemplar for background agents (see
        // above). We should use that same agent as the exemplar here to
        // avoid creating a new prototype. Otherwise, any agent is equally fine
        // to use as the exemplar.
        exint exemplar_idx = exemplar_info.myExemplarIdx;
        for (exint instance_idx : exemplar_info.myInstances)
        {
            if (exemplars_to_bake.contains(myInstances[instance_idx]))
            {
                exemplar_idx = instance_idx;
                break;
            }
        }

        const HUSD_Path &exemplar_path = myInstances[exemplar_idx];
        husdRecordPathsToReduce(
                myInstances, exemplar_path, exemplar_info.myInstances,
                exemplars_to_bake, paths_to_reduce);
    }

    // Optionally output the paths which were not affected (used for
    // visualization purposes).
    if (paths_skipped)
    {
        for (const HUSD_Path &path : myInstances)
        {
            if (!exemplars_to_bake.contains(path)
                && !paths_to_reduce.contains(path))
            {
                paths_skipped->append(path);
            }
        }
    }
}

/// Construct the husdMeasure from the camera properties and render resolution,
/// for determinining LOD from the camera's view (similar to dicing).
bool
husdReadCameraProperties(
        const UsdStageConstPtr &stage,
        const HUSD_Path &camera_path,
        const UsdTimeCode &time_sample,
        const UT_Vector2i &resolution,
        fpreal offscreen_quality,
        husdMeasure &measure,
        UT_Matrix4D &world_to_camera_xform,
        GfInterval &shutter_range)
{
    UsdGeomCamera camera(stage->GetPrimAtPath(camera_path.sdfPath()));
    if (!camera)
    {
        UT_WorkBuffer msg;
        msg.format("Invalid camera: '{}'", camera_path.pathStr());
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

#if 0
    UTdebugPrint("camera path", camera_path.pathStr());
    UTdebugPrint("resolution", resolution);
#endif

    UsdGeomXformCache xform_cache(time_sample);
    world_to_camera_xform = GusdUT_Gf::Cast(
            xform_cache.GetLocalToWorldTransform(camera.GetPrim()));
    world_to_camera_xform.invert();

    TfToken projection;
    camera.GetProjectionAttr().Get(&projection, time_sample);

    float haperture = 0;
    camera.GetHorizontalApertureAttr().Get(&haperture, time_sample);
    float vaperture = 0;
    camera.GetVerticalApertureAttr().Get(&vaperture, time_sample);

    if (projection == UsdGeomTokens->orthographic)
    {
        fpreal ortho_width = vaperture * 0.1; // Match Karma's unit conversion
        fpreal iaspect = resolution[1] / static_cast<fpreal>(resolution[0]);
        measure.setOrtho(
                resolution[0], resolution[1], ortho_width,
                ortho_width * iaspect);
    }
    else if (projection == UsdGeomTokens->perspective)
    {
        float focal = 0;
        camera.GetFocalLengthAttr().Get(&focal, time_sample);

        UT_Matrix4D dummymat(1.0);
        dummymat[0][0] = 2.0f * focal / haperture;
        dummymat[1][1] = 2.0f * focal / vaperture;

        measure.setPerspective(resolution[0], resolution[1], dummymat);
    }
    else
    {
        UT_WorkBuffer msg;
        msg.format("Projection '{}' is not currently supported", projection);
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    double shutter_open = 0;
    camera.GetShutterOpenAttr().Get(&shutter_open, time_sample);
    double shutter_close = 0;
    camera.GetShutterCloseAttr().Get(&shutter_close, time_sample);
    shutter_range = GfInterval(shutter_open, shutter_close);

    measure.setOffscreenQuality(SYSclamp01(offscreen_quality));

    return true;
}

void
husdBindMaterial(
        const SdfPrimSpecHandle &prim_spec,
        const HUSD_Path &material_path)
{
    // Apply the MaterialBindingAPI
    VtValue listopval = prim_spec->GetInfo(UsdTokens->apiSchemas);
    SdfTokenListOp listop = listopval.Get<SdfTokenListOp>();
    auto items = listop.GetPrependedItems();
    items.push_back(UsdShadeTokens->MaterialBindingAPI);
    listop.SetPrependedItems(items);
    prim_spec->SetInfo(UsdTokens->apiSchemas, VtValue::Take(listop));

    // Set the material:binding relationship, and configure as
    // strongerThanDescendants to override any material bindings underneath.
    SdfRelationshipSpecHandle rel_spec = SdfRelationshipSpec::New(
            prim_spec, UsdShadeTokens->materialBinding, /*custom=*/false);
    rel_spec->GetTargetPathList().Append(material_path.sdfPath());

    rel_spec->SetField(
            UsdShadeTokens->bindMaterialAs,
            UsdShadeTokens->strongerThanDescendants);
}

void
husdSetAnimSource(
        const SdfPrimSpecHandle &prim_spec,
        const SdfPath &anim_source)
{
    // Apply the SkelBindingAPI since we're authoring skel:animationSource.
    VtValue listopval = prim_spec->GetInfo(UsdTokens->apiSchemas);
    SdfTokenListOp listop = listopval.Get<SdfTokenListOp>();
    auto items = listop.GetPrependedItems();
    items.push_back(UsdSkelTokens->SkelBindingAPI);
    listop.SetPrependedItems(items);
    prim_spec->SetInfo(UsdTokens->apiSchemas, VtValue::Take(listop));

    // Author an empty skel:animationSource relationship.
    SdfRelationshipSpecHandle rel_spec = SdfRelationshipSpec::New(
            prim_spec, UsdSkelTokens->skelAnimationSource,
            /*custom=*/false);
    rel_spec->GetTargetPathList().ClearEditsAndMakeExplicit();
    if (!anim_source.IsEmpty())
        rel_spec->GetTargetPathList().Append(anim_source);
}

/// Author the instancing-related overlays for the exemplars to be baked, and
/// the instances of those exemplars.
void
husdSetupBakedInstances(
        const UsdStageRefPtr &stage,
        const SdfLayerRefPtr &layer,
        UT_Set<HUSD_Path> &exemplars_to_bake,
        const UT_Map<HUSD_Path, HUSD_Path> &paths_to_reduce,
        const UT_Array<HUSD_Path> &paths_skipped,
        bool bake_all_agents,
        bool visualize,
        const HUSD_Path &prototype_material,
        const HUSD_Path &instance_material,
        const HUSD_Path &default_material)
{
    SdfChangeBlock changeblock;

    // Remove instancing for the SkelRoot's we're about to bake, since
    // BakeSkinning() needs to author overrides for the skinned primitives'
    // points.
    for (const HUSD_Path &exemplar_path : exemplars_to_bake)
    {
        SdfPrimSpecHandle prim_spec = SdfCreatePrimInLayer(
                layer, exemplar_path.sdfPath());
        prim_spec->SetInstanceable(false);

        // Hide this prim, since an extra instance is created to render the
        // original exemplar as an instance of the baked result.
        SdfAttributeSpecHandle vis_spec = SdfAttributeSpec::New(
                prim_spec, UsdGeomTokens->visibility, SdfValueTypeNames->Token);
        vis_spec->SetDefaultValue(VtValue(UsdGeomTokens->invisible));
    }

    // The original instances become instanceable references to the baked
    // exemplar.
    for (auto &&[path, target_path] : paths_to_reduce)
    {
        SdfPrimSpecHandle prim_spec = HUSDcreatePrimInLayer(
                stage, layer, path.sdfPath(), TfToken(), SdfSpecifierDef,
                SdfSpecifierDef, UsdGeomTokens->Xform.GetString());
        prim_spec->SetTypeName(UsdGeomTokens->Xform);

        prim_spec->SetInstanceable(true);

        prim_spec->GetReferenceList().ClearEditsAndMakeExplicit();
        prim_spec->GetReferenceList().Append(
                SdfReference(std::string(), target_path.sdfPath()));

        // In Hydra 2, the skel binding data (in particular,
        // skel:animationSource) participates in instance aggregation,
        // regardless of whether there are actually SkelRoot prims underneath.
        // There is typically a unique SkelAnimation bound to each SkelRoot for
        // the agent's pose.
        // Since we're baking skinning for the prototype, we need to author an
        // empty skel:animationSource (overriding the inherited animation
        // binding) so Hydra can actually aggregate these instances together.
        husdSetAnimSource(prim_spec, SdfPath::EmptyPath());

        SdfAttributeSpecHandle vis_spec = SdfAttributeSpec::New(
                prim_spec, UsdGeomTokens->visibility, SdfValueTypeNames->Token);
        vis_spec->SetDefaultValue(VtValue(UsdGeomTokens->inherited));

        // For debugging, visualize the exemplars and the reduced agents.
        if (visualize)
        {
            const HUSD_Path &material_path
                    = (path.parentPath() == target_path.parentPath())
                              ? prototype_material
                              : instance_material;

            husdBindMaterial(prim_spec, material_path);
        }
    }

    // If we're baking all agents, disable instancing for the remaining agents.
    // If visualization is enabled, color the primitives which weren't affected
    // by the procedural.
    if (bake_all_agents || visualize)
    {
        for (const HUSD_Path &path : paths_skipped)
        {
            SdfPrimSpecHandle prim_spec = SdfCreatePrimInLayer(
                    layer, path.sdfPath());

            if (bake_all_agents)
            {
                prim_spec->SetInstanceable(false);
                exemplars_to_bake.insert(path);
            }

            if (visualize)
                husdBindMaterial(prim_spec, default_material);
        }
    }
}

/// Author overlays for instances to use the skel:animationSource of the
/// exemplar agent.
void
husdModifyAnimSources(
        const UsdStageRefPtr &stage,
        const SdfLayerRefPtr &layer,
        const UT_Set<HUSD_Path> &exemplars_to_bake,
        const UT_Map<HUSD_Path, HUSD_Path> &paths_to_reduce,
        const UT_Array<HUSD_Path> &paths_skipped,
        bool visualize,
        const HUSD_Path &prototype_material,
        const HUSD_Path &instance_material,
        const HUSD_Path &default_material)
{
    SdfChangeBlock changeblock;

    UT_Map<HUSD_Path, HUSD_Path> exemplar_anim_sources;
    exemplar_anim_sources.reserve(exemplars_to_bake.size());

    // Record the animation sources for the exemplar agents.
    for (const HUSD_Path &exemplar_path : exemplars_to_bake)
    {
        const UsdSkelRoot skel_root(
                stage->GetPrimAtPath(exemplar_path.sdfPath()));
        UT_ASSERT(skel_root);

        UsdSkelBindingAPI skel_binding(skel_root);
        UsdPrim anim_source = skel_binding.GetInheritedAnimationSource();
        exemplar_anim_sources.emplace(
                exemplar_path, anim_source.IsValid() ? anim_source.GetPrimPath()
                                                     : SdfPath::EmptyPath());

        if (visualize)
        {
            SdfPrimSpecHandle prim_spec = SdfCreatePrimInLayer(
                    layer, exemplar_path.sdfPath());
            husdBindMaterial(prim_spec, prototype_material);
        }
    }

    // Update the instanced agents to use the same skel:animationSource (same
    // pose) as the exemplars. In Hydra 2, this is translated into instances of
    // the same prototype..
    for (auto &&[path, target_path] : paths_to_reduce)
    {
        // Ignore the exemplar agent, which is already using the correct
        // animation source (this shows up in the list because the baking mode
        // needs to adjust the exemplar to instance the baked geo)
        const bool is_exemplar
                = (path.parentPath() == target_path.parentPath());
        if (is_exemplar)
            continue;

        const HUSD_Path &exemplar_anim_source
                = exemplar_anim_sources.at(target_path);

        SdfPrimSpecHandle prim_spec = SdfCreatePrimInLayer(
                layer, path.sdfPath());
        husdSetAnimSource(prim_spec, exemplar_anim_source.sdfPath());

        if (visualize)
            husdBindMaterial(prim_spec, instance_material);
    }

    // If visualization is enabled, color the primitives which weren't affected
    // by the procedural.
    if (visualize)
    {
        for (const HUSD_Path &path : paths_skipped)
        {
            SdfPrimSpecHandle prim_spec = SdfCreatePrimInLayer(
                    layer, path.sdfPath());

            if (visualize)
                husdBindMaterial(prim_spec, default_material);
        }
    }
}

/// Determines the time interval to bake skinning over, when calling
/// UsdSkelBakeSkinning(). This time interval includes any samples required for
/// motion blur.
GfInterval
husdComputeBakeInterval(
        const UsdStagePtr &stage,
        UsdTimeCode time_sample,
        const GfInterval &shutter_range)
{
    // Add the shutter interval around the time sample being rendered.
    GfInterval bake_interval(
            time_sample.GetValue() + shutter_range.GetMin(),
            time_sample.GetValue() + shutter_range.GetMax());

    // UsdSkelBakeSkinning() will only bake skinning at the time samples
    // within the interval for which the joint animation is authored.
    //
    // If the shutter range is e.g. [-0.25, 0.25] but the skeleton animation has
    // samples at [-0.5, 0, 0.5], this would only bake out skinning at the
    // current time sample and we wouldn't get any motion blur.
    // So, we need to extend the time interval to include the adjacent samples.
    // We don't have a great way to find the exact range, without replicating
    // much of the BakeSkinning logic to query all the attributes that affect
    // skinning for every SkelRoot.
    //
    // However, BakeSkinning will also explicitly bake out skinning at frames
    // the stage is expected to be sampled at, based on the timecode metadata
    // (see _ComputeTimeSamples() in bakeSkinning.cpp), in case the animation is
    // sparsely sampled.
    // So, extending the interval to include the previous and next frames is
    // sufficient to produce the adjacent samples we need for motion blur,
    // although it possibly may do more baking than strictly necessary if the
    // stage did have sub-frame samples for the skeleton animation.
    if (!bake_interval.IsEmpty())
    {
        const double tcps = stage->GetTimeCodesPerSecond();
        const double fps = stage->GetFramesPerSecond();
        if (!SYSequalZero(tcps) && !SYSequalZero(fps))
        {
            const double time_step = SYSabs(tcps / fps);

            bake_interval.SetMin(SYSroundDownToMultipleOf(
                    bake_interval.GetMin(), time_step));
            bake_interval.SetMax(SYSroundUpToMultipleOf(
                    bake_interval.GetMax(), time_step));
        }
    }

    return bake_interval;
}

/// Run UsdSkelBakeSkinning() to generate the deformed geometry for the exemplar
/// SkelRoots.
void
husdBakeSkinning(
        const UsdStagePtr &stage,
        const SdfLayerRefPtr &edit_layer,
        const UT_Set<HUSD_Path> &exemplars_to_bake,
        const GfInterval &time_interval)
{
    if (exemplars_to_bake.empty())
        return;

    UsdSkelBakeSkinningParms parms;
    parms.saveLayers = false;

    UsdSkelCache cache;
    std::vector<UsdSkelBinding> bindings;

    for (const HUSD_Path &exemplar_path : exemplars_to_bake)
    {
        const UsdSkelRoot skel_root(
                stage->GetPrimAtPath(exemplar_path.sdfPath()));
        UT_ASSERT(skel_root);

        cache.Populate(skel_root, UsdPrimDefaultPredicate);
        if (!cache.ComputeSkelBindings(
                    skel_root, &bindings, UsdPrimDefaultPredicate))
        {
            continue;
        }

        parms.bindings.insert(
                parms.bindings.end(), bindings.begin(), bindings.end());
    }

    parms.layers.push_back(edit_layer);
    parms.layerIndices.assign(parms.bindings.size(), 0);

    UT_ErrorLog::format(
            4, "Crowd Procedural: baking skinning over interval {0} to {1}",
            time_interval.GetMin(), time_interval.GetMax());

    UT_VERIFY(UsdSkelBakeSkinning(cache, parms, time_interval));
}

} // namespace

bool
HUSDapplyCrowdProcedural(
        HUSD_AutoWriteLock &lock,
        const HUSD_PathSet &prim_paths,
        const HUSD_Path &camera_path,
        const UT_Vector2i &resolution,
        const HUSD_TimeCode &time_sample,
        fpreal lod_threshold,
        fpreal offscreen_quality,
        bool optimize_identical_poses,
        bool bake_prototype_agents,
        bool bake_all_agents,
        const HUSD_Path &prototype_material,
        const HUSD_Path &instance_material,
        const HUSD_Path &default_material)
{
    UT_StopWatch timer;
    timer.start();

    if (!lock.data() || !lock.data()->isStageValid())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid stage.");
        return false;
    }

    UsdStageRefPtr stage = lock.data()->stage();

    // Read the camera's properties and construct the husdMeasure for
    // evaluating LOD quality.
    UT_Matrix4D world_to_camera_xform(1.0);
    husdMeasure measure;
    GfInterval shutter_range;
    if (!husdReadCameraProperties(
                stage, camera_path, HUSDgetUsdTimeCode(time_sample), resolution,
                offscreen_quality, measure, world_to_camera_xform,
                shutter_range))
    {
        return false;
    }

    // Partition the instanceable SkelRoots by their prototype. Each partition
    // can be processed independently.
    UT_Array<husdSkelInstanceGroup> instance_groups;
    UT_Map<HUSD_Path, exint> prototype_map;
    for (const HUSD_Path &path : prim_paths)
    {
        UsdPrim prim = stage->GetPrimAtPath(path.sdfPath());
        UsdSkelRoot skelroot(prim);

        // We expect the prim to be an instanced SkelRoot.
        if (!skelroot || !prim.IsInstance())
            continue;

        HUSD_Path prototype_path = prim.GetPrototype().GetPath();

        const exint group_idx = UTfindOrInsert(
                prototype_map, prototype_path,
                [&]() { return instance_groups.append(); });

        instance_groups[group_idx].addInstance(path);
    }

    // For each group of instances, find the exemplar agents that background
    // agents should become instances of.
    UTparallelForEachNumber(
            instance_groups.size(),
            [&](const UT_BlockedRange<exint> &range)
            {
                for (exint group_idx : range.items())
                {
                    husdSkelInstanceGroup &group = instance_groups[group_idx];

                    group.findExemplars(
                            stage, HUSDgetUsdTimeCode(time_sample),
                            lod_threshold, optimize_identical_poses, measure,
                            world_to_camera_xform);
                }
            });

    // Accumulate the full list of SkelRoots to bake skinning for, and the
    // instancing changes to overlay.
    UT_Set<HUSD_Path> exemplars_to_bake;
    UT_Map<HUSD_Path, HUSD_Path> paths_to_reduce;

    const bool visualize
            = (!prototype_material.isEmpty() && !instance_material.isEmpty()
               && !default_material.isEmpty());
    UT_Array<HUSD_Path> paths_skipped;

    for (const husdSkelInstanceGroup &group : instance_groups)
    {
        group.getPathsToReduce(
                exemplars_to_bake, paths_to_reduce, &paths_skipped);
    }

    // Output stats about the optimizations.
    UT_WorkBuffer seconds;
    UT_Date::printSeconds(seconds, timer.stop(), false, true, true);
    UT_ErrorLog::format(
            3, "Crowd Procedural: analysis time {0}", seconds);
    UT_ErrorLog::format(
            3,
            "Crowd Procedural: creating {0} instances with {1} prototype(s), "
            "skipping {2} prims",
            paths_to_reduce.size(), exemplars_to_bake.size(),
            prim_paths.size() - paths_to_reduce.size());

    SdfLayerRefPtr edit_layer = stage->GetEditTarget().GetLayer();
    if (!edit_layer)
    {
        HUSD_ErrorScope::addError(
                HUSD_ERR_STRING, "Stage does not have a valid edit target.");
        return false;
    }

    timer.restart();

    if (bake_prototype_agents)
    {
        // Apply the instancing changes to the stage and bake out skinning for
        // the prototypes.
        husdSetupBakedInstances(
                stage, edit_layer, exemplars_to_bake, paths_to_reduce,
                paths_skipped, bake_all_agents, visualize, prototype_material,
                instance_material, default_material);

        GfInterval bake_interval = husdComputeBakeInterval(
                stage, HUSDgetUsdTimeCode(time_sample), shutter_range);
        husdBakeSkinning(stage, edit_layer, exemplars_to_bake, bake_interval);

        UT_Date::printSeconds(seconds, timer.stop(), false, true, true);
        UT_ErrorLog::format(
                3, "Crowd Procedural: bake skinning time {0}", seconds);
    }
    else
    {
        husdModifyAnimSources(
                stage, edit_layer, exemplars_to_bake, paths_to_reduce,
                paths_skipped, visualize, prototype_material, instance_material,
                default_material);

        UT_Date::printSeconds(seconds, timer.stop(), false, true, true);
        UT_ErrorLog::format(
                3, "Crowd Procedural: edit skel:animationSource time {0}",
                seconds);
    }

    return true;
}
