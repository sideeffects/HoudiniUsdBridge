/*
 * Copyright 2019 Side Effects Software Inc.
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

#include "HUSD_Skeleton.h"

#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_Info.h"
#include "HUSD_LockedStage.h"
#include "HUSD_Path.h"
#include "HUSD_PathSet.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_Skeleton.h"
#include "XUSD_Utils.h"

#include <GA/GA_Names.h>
#include <GEO/GEO_PolyCounts.h>
#include <GEO/GEO_PrimPoly.h>
#include <GEO/GEO_PrimPolySoup.h>
#include <GEO/GEO_StandardAttribs.h>
#include <GT/GT_RefineParms.h>
#include <GU/GU_Detail.h>
#include <GU/GU_MergeUtils.h>
#include <GU/GU_MotionClipUtil.h>
#include <gusd/GU_USD.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Gf.h>
#include <gusd/agentUtils.h>
#include <gusd/purpose.h>
#include <gusd/stageCache.h>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/utils.h>

static constexpr UT_StringLit theSkelRootPathAttrib("usdskelrootpath");
static constexpr UT_StringLit theSkelPathAttrib("usdskelpath");
static constexpr UT_StringLit theAnimPathAttrib("usdanimpath");

PXR_NAMESPACE_USING_DIRECTIVE

struct HUSD_SkeletonCache::Impl
{
    Impl(const UsdStageRefPtr &stage) : myStage(stage) {}

    UsdStageRefPtr myStage;
    UsdSkelCache mySkelCache;
    std::vector<UsdSkelBinding> myBindings;
};

HUSD_SkeletonCache::HUSD_SkeletonCache() = default;

HUSD_SkeletonCache::~HUSD_SkeletonCache() = default;

void
HUSD_SkeletonCache::reset()
{
    myImpl.reset();
}

bool
HUSD_SkeletonCache::init(
            HUSD_AutoReadLock &readlock,
            const HUSD_LockedStagePtr &locked_stage)
{
    UsdStageRefPtr stage;

    // If the data handle is from a lop we require a locked stage, since
    // the cache's stage persists across cooks.
    if (locked_stage)
    {
        GusdStageCacheReader cache_reader;
        stage = cache_reader.Find(locked_stage->getStageCacheIdentifier());
    }
    else if (readlock.isStageValid())
        stage = readlock.data()->stage();

    if (!stage)
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid stage.");
        return false;
    }

    myImpl = UTmakeUnique<Impl>(stage);
    return true;
}

static GT_RefineParms
husdShapeRefineParms()
{
    GT_RefineParms refine_parms;
    refine_parms.set(GUSD_REFINE_ADDXFORMATTRIB, false);

    // Skip creating the usdpath attribute, which is random for stages from
    // LOPs. This could be revisited if importing directly from a file is
    // allowed.
    refine_parms.set(GUSD_REFINE_ADDPATHATTRIB, false);

    return refine_parms;
}

static bool
husdFindSkelBindings(
        const UsdStageRefPtr &stage,
        HUSD_AutoReadLock &readlock,
        const UT_StringRef &skelrootpath,
        UsdSkelCache &skelcache,
        std::vector<UsdSkelBinding> &bindings)
{
    SdfPath sdfpath = HUSDgetSdfPath(skelrootpath);
    UsdPrim prim(stage->GetPrimAtPath(sdfpath));
    if (!prim)
    {
        HUSD_ErrorScope::addError(HUSD_ERR_CANT_FIND_PRIM,
                                  skelrootpath.c_str());
        return false;
    }

    UsdSkelRoot skelroot(prim);
    if (!skelroot)
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                  "Primitive is not a SkelRoot.");
        return false;
    }

    auto predicate = UsdTraverseInstanceProxies(UsdPrimDefaultPredicate);

    skelcache.Populate(skelroot, predicate);

    bindings.clear();
    if (!skelcache.ComputeSkelBindings(skelroot, &bindings, predicate))
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                  "Failed to compute skeleton bindings.");
        return false;
    }

    // ComputeSkelBindings() does nothing if there aren't any skinned prims
    // under the SkelRoot. In this situation, we still want to find Skeleton
    // prims that aren't bound to any skinned geometry.
    if (bindings.empty())
    {
        HUSD_FindPrims findprims(readlock);

        UT_WorkBuffer pattern;
        pattern.format("{0}/** & %type:Skeleton", skelrootpath);
        findprims.addPattern(
                pattern.buffer(), OP_INVALID_NODE_ID, HUSD_TimeCode());

        for (auto &&skelpath : findprims.getExpandedPathSet())
        {
            UsdPrim prim(stage->GetPrimAtPath(skelpath.sdfPath()));
            UT_ASSERT(prim);

            UsdSkelSkeleton skel(prim);
            UT_ASSERT(skel);

            bindings.push_back(UsdSkelBinding(skel, {}));
        }
    }

    if (bindings.empty())
    {
        HUSD_ErrorScope::addError(
                HUSD_ERR_STRING,
                "Primitive does not have any Skeleton children");
        return false;
    }

    return true;
}

static bool
husdFindSkelBindings(HUSD_AutoReadLock &readlock,
                     const UT_StringRef &skelrootpath,
                     UsdSkelCache &skelcache,
                     std::vector<UsdSkelBinding> &bindings)
{
    if (!readlock.isStageValid())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid stage.");
        return false;
    }

    return husdFindSkelBindings(
            readlock.data()->stage(), readlock, skelrootpath, skelcache,
            bindings);
}

UT_StringHolder
HUSDdefaultSkelRootPath(HUSD_AutoReadLock &readlock)
{
    HUSD_FindPrims findprims(readlock);
    findprims.addPattern("%type:SkelRoot",
        OP_INVALID_NODE_ID, HUSD_TimeCode());

    if (!findprims.getExpandedPathSet().empty())
        return findprims.getExpandedPathSet().getFirstPathAsString();

    HUSD_ErrorScope::addWarning(
        HUSD_ERR_STRING, "Could not find a SkelRoot prim.");
    return UT_StringHolder::theEmptyString;
}

bool
HUSDimportSkinnedGeometry(GU_Detail &gdp, HUSD_AutoReadLock &readlock,
                          const UT_StringRef &skelrootpath,
                          const UT_StringRef &purpose,
                          const UT_StringHolder &shapeattrib)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    GA_IndexMap::Marker prim_marker(gdp.getPrimitiveMap());

    const SdfPath root_path = HUSDgetSdfPath(skelrootpath);
    GT_RefineParms refine_parms = husdShapeRefineParms();

    GusdSkinImportParms parms;
    parms.myPurpose = GusdPurposeSet(
            GusdPurposeSetFromMask(purpose) | GUSD_PURPOSE_DEFAULT);
    parms.myRefineParms = &refine_parms;

    for (const UsdSkelBinding &binding : bindings)
    {
        UT_Array<GU_DetailHandle> details;
        details.setSize(binding.GetSkinningTargets().size());

        GusdForEachSkinnedPrim(
            binding, parms,
            [&binding, &details, &root_path, &shapeattrib](
                exint i, const GusdSkinImportParms &parms,
                const VtTokenArray &joint_names,
                const VtMatrix4dArray &inv_bind_transforms) {

                const UsdSkelSkinningQuery &skinning_query =
                    binding.GetSkinningTargets()[i];

                GU_DetailHandle gdh = XUSDimportSkinnedPrim(
                        parms, skinning_query, joint_names, inv_bind_transforms,
                        root_path, shapeattrib);
                details[i] = gdh;
                return gdh.isValid();
            });

        // Merge all the shapes together.
        GUmatchAttributesAndMerge(gdp, details);
    }

    // Record the SkelRoot path for improved round-tripping.
    GA_RWBatchHandleS skelroot_h(gdp.addStringTuple(
            GA_ATTRIB_PRIMITIVE, theSkelRootPathAttrib.asHolder(), 1));
    skelroot_h.set(prim_marker.getRange(), skelrootpath);

    // Bump all data ids since we've created new geometry.
    gdp.bumpAllDataIds();

    return true;
}

/// Build the list of blendshape channels.
/// This is the combination of blendshape channels defined on the SkelAnimation,
/// as well as blendshapes defined on any skinned prims. The SkelAnimation might
/// not have animation defined for all available blendshapes, or might have
/// extra channels that we don't want to lose animation for.
static UT_StringArray
husdGetBlendShapeChannels(
        const UsdSkelAnimQuery &animquery,
        const VtArray<UsdSkelSkinningQuery> &skinning_targets)
{
    UT_StringSet unique_channels;

    if (animquery.IsValid())
    {
        for (const TfToken &channel_name : animquery.GetBlendShapeOrder())
        {
            unique_channels.insert(
                    GusdUSD_Utils::TokenToStringHolder(channel_name));
        }
    }

    for (const UsdSkelSkinningQuery &target : skinning_targets)
    {
        if (!target.HasBlendShapes())
            continue;

        VtTokenArray blend_shapes;
        if (!target.GetBlendShapeOrder(&blend_shapes))
            continue;

        for (const TfToken &channel_name : blend_shapes)
        {
            unique_channels.insert(
                    GusdUSD_Utils::TokenToStringHolder(channel_name));
        }
    }

    // Sort to avoid depending on UT_StringSet's arbitrary ordering.
    UT_StringArray channel_names(unique_channels.size());
    for (const UT_StringHolder &channel_name : unique_channels)
        channel_names.append(channel_name);

    channel_names.sort();

    return channel_names;
}

static bool
husdImportRestPoseAttrib(
        GU_Detail &detail,
        GA_RWHandleM4D &rest_pose_attrib,
        const GA_Offset start_ptoff,
        const UsdSkelSkeleton &skel,
        const UsdSkelTopology &topology)
{
    VtMatrix4dArray local_xforms;
    if (!skel.GetRestTransformsAttr().Get(&local_xforms))
    {
        HUSD_ErrorScope::addWarning(
                HUSD_ERR_STRING, "'restTransforms' attribute is invalid");
        return false;
    }
    else if (local_xforms.size() != topology.GetNumJoints())
    {
        HUSD_ErrorScope::addError(
                HUSD_ERR_STRING, "'restTransforms' attribute does not match "
                                 "the size of the 'joints' attribute.");
        return false;
    }

    // Note unlike husdComputeWorldTransforms(), we don't apply the skeleton
    // prim's transform to the rest transforms.
    VtMatrix4dArray world_xforms(local_xforms.size());
    if (!UsdSkelConcatJointTransforms(
                topology, local_xforms, world_xforms,
                /*rootXform=*/nullptr))
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                  "Failed to compute world transforms.");
        return false;
    }

    // Write out to the skeleton geometry.
    for (exint i = 0, n = topology.GetNumJoints(); i < n; ++i)
    {
        GA_Offset ptoff = start_ptoff + i;
        rest_pose_attrib.set(ptoff, GusdUT_Gf::Cast(world_xforms[i]));
    }

    return true;
}

bool
HUSDimportSkeleton(
        GU_Detail &gdp,
        HUSD_SkeletonCache &opaque_cache,
        HUSD_AutoReadLock &readlock,
        const HUSD_LockedStagePtr &locked_stage,
        const UT_StringRef &skelrootpath,
        HUSD_SkeletonPoseType pose_type,
        const UT_StringHolder &rest_pose_attrib_name)
{
    if (!opaque_cache.init(readlock, locked_stage))
        return false;

    auto &&cache = opaque_cache.impl();

    /// Cache the skeleton bindings for use in HUSDimportSkeletonPose().
    if (!husdFindSkelBindings(
                cache.myStage, readlock, skelrootpath, cache.mySkelCache,
                cache.myBindings))
    {
        return false;
    }

    GA_RWHandleS name_attrib =
        gdp.addStringTuple(GA_ATTRIB_POINT, GA_Names::name, 1);
    GA_RWHandleS path_attrib =
        gdp.addStringTuple(GA_ATTRIB_POINT, GA_Names::path, 1);

    GEO_StandardAttribs::createTransform3(gdp);

    GA_RWHandleM4D rest_pose_attrib;
    if (rest_pose_attrib_name.isstring())
    {
        rest_pose_attrib = GEO_StandardAttribs::createRestTransform4(
                gdp, rest_pose_attrib_name);
    }

    GA_RWHandleS skelpath_attrib = gdp.addStringTuple(
        GA_ATTRIB_PRIMITIVE, theSkelPathAttrib.asHolder(), 1);

    GA_RWHandleS animpath_attrib;
    if (pose_type == HUSD_SkeletonPoseType::Animation)
    {
        animpath_attrib = gdp.addStringTuple(GA_ATTRIB_PRIMITIVE,
                                             theAnimPathAttrib.asHolder(), 1);
    }

    GU_MotionClipChannelMap channel_map;
    for (const UsdSkelBinding &binding : cache.myBindings)
    {
        const UsdSkelSkeleton &skel = binding.GetSkeleton();
        UsdSkelSkeletonQuery skelquery = cache.mySkelCache.GetSkelQuery(skel);
        if (!skelquery.IsValid())
        {
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                      "Invalid skeleton query.");
            return false;
        }

        // Add attributes for blendshape channels (unless we're just generating
        // the bind pose).
        if (pose_type != HUSD_SkeletonPoseType::BindPose)
        {
            UT_StringArray channel_names = husdGetBlendShapeChannels(
                    skelquery.GetAnimQuery(), binding.GetSkinningTargets());

            for (const UT_StringHolder &channel_name : channel_names)
            {
                UT_StringHolder attrib_name
                        = channel_name.forceValidVariableName();

                gdp.addFloatTuple(GA_ATTRIB_DETAIL, attrib_name, 1);
                channel_map.addDetailAttrib(channel_name, attrib_name);
            }
        }

        VtTokenArray joint_paths;
        if (!skel.GetJointsAttr().Get(&joint_paths))
        {
            // It's possible that a skeleton could just have blendshape
            // channels, and no joints, so this is not an error.
            continue;
        }

        VtTokenArray joint_names;
        GusdGetJointNames(skel, joint_names);

        // Create a point for each joint, and connect each point to its parent
        // with a polygon.
        const UsdSkelTopology &topology = skelquery.GetTopology();
        GA_Offset start_ptoff = gdp.appendPointBlock(topology.GetNumJoints());
        UT_Array<int> poly_ptnums;
        for (exint i = 0, n = topology.GetNumJoints(); i < n; ++i)
        {
            GA_Offset ptoff = start_ptoff + i;
            name_attrib.set(ptoff,
                            GusdUSD_Utils::TokenToStringHolder(joint_names[i]));
            path_attrib.set(ptoff,
                            GusdUSD_Utils::TokenToStringHolder(joint_paths[i]));

            if (!topology.IsRoot(i))
            {
                int parent = topology.GetParent(i);
                poly_ptnums.append(parent);
                poly_ptnums.append(i);
            }
        }

        GEO_PolyCounts poly_sizes;
        poly_sizes.append(2, poly_ptnums.size() / 2);
        const GA_Offset start_primoff =
            GEO_PrimPoly::buildBlock(&gdp, start_ptoff, topology.GetNumJoints(),
                                     poly_sizes, poly_ptnums.data(),
                                     /* closed */ false);

        // Record the skeleton prim's path for round-tripping.
        const UT_StringHolder skelpath = skel.GetPath().GetString();
        for (exint i = 0, n = poly_sizes.getNumPolygons(); i < n; ++i)
            skelpath_attrib.set(start_primoff + i, skelpath);

        // Record the SkelAnimation prim's path for round-tripping.
        if (pose_type == HUSD_SkeletonPoseType::Animation)
        {
            const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();
            if (animquery.IsValid())
            {
                const UT_StringHolder animpath
                        = animquery.GetPrim().GetPath().GetString();
                for (exint i = 0, n = poly_sizes.getNumPolygons(); i < n; ++i)
                    animpath_attrib.set(start_primoff + i, animpath);
            }
            else
            {
                UT_WorkBuffer msg;
                msg.format(
                        "Skeleton '{0}' does not have an animation binding.",
                        skelpath);
                HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
            }
        }

        // Record the rest pose.
        if (rest_pose_attrib.isValid())
        {
            husdImportRestPoseAttrib(
                    gdp, rest_pose_attrib, start_ptoff, skel, topology);
        }
    }

    if (!channel_map.isEmpty())
        channel_map.save(gdp);

    // Bump all data ids since new geometry was generated.
    gdp.bumpAllDataIds();

    return true;
}

static bool
husdComputeWorldTransforms(const UsdSkelSkeleton &skel,
                           const UsdSkelTopology &topology,
                           const UsdTimeCode &timecode,
                           const VtMatrix4dArray &local_xforms,
                           VtMatrix4dArray &world_xforms)
{
    const GfMatrix4d root_xform = skel.ComputeLocalToWorldTransform(timecode);

    world_xforms.resize(local_xforms.size());
    if (!UsdSkelConcatJointTransforms(topology, local_xforms, world_xforms,
                                      &root_xform))
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                  "Failed to compute world transforms.");
        return false;
    }

    return true;
}

bool
HUSDimportSkeletonPose(
        GU_Detail &gdp,
        const HUSD_SkeletonCache &opaque_cache,
        HUSD_SkeletonPoseType pose_type,
        HUSD_TimeCode timecode)
{
    UsdTimeCode usd_timecode = HUSDgetUsdTimeCode(timecode);

    UT_ASSERT(opaque_cache.isValid());
    auto &&cache = opaque_cache.impl();

    GA_RWHandleM3D xform_attrib =
        gdp.findFloatTuple(GA_ATTRIB_POINT, GA_Names::transform, 9);
    UT_ASSERT(xform_attrib.isValid());

    GA_Index ptidx = 0;
    for (const UsdSkelBinding &binding : cache.myBindings)
    {
        const UsdSkelSkeleton &skel = binding.GetSkeleton();
        UsdSkelSkeletonQuery skelquery = cache.mySkelCache.GetSkelQuery(skel);
        if (!skelquery.IsValid())
        {
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                      "Invalid skeleton query.");
            return false;
        }

        const UsdSkelTopology &topology = skelquery.GetTopology();

        VtMatrix4dArray world_xforms;
        VtTokenArray channel_names;
        VtFloatArray channel_values;

        switch (pose_type)
        {
        case HUSD_SkeletonPoseType::Animation:
        {
            VtMatrix4dArray local_xforms;
            if (!skelquery.ComputeJointLocalTransforms(
                        &local_xforms, usd_timecode))
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "Failed to compute local transforms.");
                return false;
            }

            if (!husdComputeWorldTransforms(
                        skel, topology, usd_timecode, local_xforms,
                        world_xforms))
            {
                return false;
            }

            // Evaluate the blend shape channels.
            const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();
            if (animquery.IsValid())
            {
                channel_names = animquery.GetBlendShapeOrder();

                if (!channel_names.empty()
                    && !animquery.ComputeBlendShapeWeights(
                            &channel_values, usd_timecode))
                {
                    HUSD_ErrorScope::addWarning(
                            HUSD_ERR_STRING,
                            "Failed to compute blend shape weights");
                    channel_names.clear();
                }

                UT_ASSERT(channel_names.size() == channel_values.size());
            }
        }
        break;

        case HUSD_SkeletonPoseType::BindPose:
        {
            if (!skel.GetBindTransformsAttr().Get(&world_xforms))
            {
                HUSD_ErrorScope::addWarning(
                    HUSD_ERR_STRING, "'bindTransforms' attribute is invalid");
                return false;
            }
            else if (world_xforms.size() != topology.GetNumJoints())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING,
                    "'bindTransforms' attribute does not match "
                    "the size of the 'joints' attribute.");
                return false;
            }
        }
        break;

        case HUSD_SkeletonPoseType::RestPose:
        {
            VtMatrix4dArray local_xforms;
            if (!skel.GetRestTransformsAttr().Get(&local_xforms))
            {
                HUSD_ErrorScope::addWarning(
                    HUSD_ERR_STRING, "'restTransforms' attribute is invalid");
                return false;
            }
            else if (local_xforms.size() != topology.GetNumJoints())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING,
                    "'restTransforms' attribute does not match "
                    "the size of the 'joints' attribute.");
                return false;
            }

            if (!husdComputeWorldTransforms(
                        skel, topology, usd_timecode, local_xforms,
                        world_xforms))
            {
                return false;
            }
        }
        break;
        }

        UT_ASSERT(ptidx + topology.GetNumJoints() <= gdp.getNumPoints());
        UT_ASSERT(world_xforms.size() == topology.GetNumJoints());
        for (exint i = 0, n = topology.GetNumJoints(); i < n; ++i, ++ptidx)
        {
            GA_Offset ptoff = gdp.pointOffset(ptidx);

            const UT_Matrix4D &xform = GusdUT_Gf::Cast(world_xforms[i]);
            xform_attrib.set(ptoff, UT_Matrix3D(xform));

            UT_Vector3D t;
            xform.getTranslates(t);
            gdp.setPos3(ptoff, t);
        }

        for (exint i = 0, n = channel_names.size(); i < n; ++i)
        {
            UT_StringHolder channel_name
                    = GusdUSD_Utils::TokenToStringHolder(channel_names[i]);
            UT_StringHolder attrib_name = channel_name.forceValidVariableName();

            GA_RWHandleF attrib = gdp.findFloatTuple(
                    GA_ATTRIB_DETAIL, attrib_name, 1);
            UT_ASSERT(attrib.isValid());

            attrib.set(GA_DETAIL_OFFSET, channel_values[i]);
            attrib.bumpDataId();
        }
    }

    gdp.getP()->bumpDataId();
    xform_attrib.bumpDataId();

    return true;
}

GU_AgentRigPtr
HUSDimportAgentRig(HUSD_AutoReadLock &readlock,
                   const UT_StringRef &skelrootpath,
                   const UT_StringHolder &rig_name,
                   bool create_locomotion_joint)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return nullptr;

    if (bindings.size() > 1)
    {
        UT_WorkBuffer msg;
        msg.format(
                "'{0}' contains multiple skeleton bindings. Using '{1}'",
                skelrootpath,
                bindings[0].GetSkeleton().GetPath().GetAsString());
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
    }

    const UsdSkelBinding &binding = bindings[0];

    const UsdSkelSkeleton &skel = binding.GetSkeleton();
    UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);
    GU_AgentRigPtr rig = GusdCreateAgentRig(rig_name, skelquery,
                                            create_locomotion_joint);
    if (!rig)
        return nullptr;

    // Add blendshape channels to the rig.
    UT_StringArray channels = husdGetBlendShapeChannels(
            skelquery.GetAnimQuery(), binding.GetSkinningTargets());

    for (const UT_StringHolder &channel_name : channels)
        rig->addChannel(channel_name, 0.0, -1);

    return rig;
}

bool
HUSDimportAgentShapes(GU_AgentShapeLib &shapelib,
                      GU_AgentLayer &layer,
                      HUSD_AutoReadLock &readlock,
                      const UT_StringRef &skelrootpath,
                      const UT_StringRef &purpose,
                      const UT_Vector3F &layer_bounds_scale)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    const UsdSkelBinding &binding = bindings[0];
    const SdfPath root_path = HUSDgetSdfPath(skelrootpath);

    struct ShapeInfo
    {
        GU_DetailHandle myDetail;
        GU_AgentShapeDeformerConstPtr myDeformer;
        UT_StringHolder myTransformName;

        UT_Array<GU_DetailHandle> myBlendShapeDetails;
        UT_StringArray myBlendShapeNames;
    };
    UT_Array<ShapeInfo> shapes;
    shapes.setSize(binding.GetSkinningTargets().size());

    GT_RefineParms refine_parms = husdShapeRefineParms();
    GusdSkinImportParms parms;
    parms.myPurpose = GusdPurposeSet(
            GusdPurposeSetFromMask(purpose) | GUSD_PURPOSE_DEFAULT);
    parms.myRefineParms = &refine_parms;

    // Convert the shapes to Houdini geometry.
    GusdForEachSkinnedPrim(binding, parms,
        [&binding, &shapes, &root_path](
            exint i, const GusdSkinImportParms &parms,
            const VtTokenArray &skel_joint_names,
            const VtMatrix4dArray &skel_inv_bind_transforms) {
            const UsdSkelSkinningQuery &skinning_query =
                binding.GetSkinningTargets()[i];

            GU_DetailHandle gdh;
            gdh.allocateAndSet(new GU_Detail);
            GU_Detail *gdp = gdh.gdpNC();

            // A static shape is equivalent to a rigid deformation with a
            // single influence.
            const bool is_static_shape =
                !skinning_query.HasBlendShapes() &&
                skinning_query.HasJointInfluences() &&
                skinning_query.IsRigidlyDeformed() &&
                (skinning_query.GetNumInfluencesPerComponent() == 1);

            UT_Matrix4D geom_bind_xform(1.0);
            if (skinning_query.HasJointInfluences())
            {
                geom_bind_xform = GusdUT_Gf::Cast(
                        skinning_query.GetGeomBindTransform());
            }

            // For a static shape, record the joint that it's attached to, and
            // bake in the inverse bind transform since static agent shapes are
            // simply transformed by the joint transform.
            if (is_static_shape)
            {
                VtIntArray joint_indices;
                UT_VERIFY(skinning_query.GetJointIndicesPrimvar().Get(
                        &joint_indices));

                // Convert joint names and bind transforms to the joint
                // ordering specified on this prim, if necessary.
                VtTokenArray joint_names = skel_joint_names;
                VtMatrix4dArray inv_bind_transforms = skel_inv_bind_transforms;
                if (skinning_query.GetJointMapper())
                {
                    UT_VERIFY(skinning_query.GetJointMapper()->Remap(
                            skel_joint_names, &joint_names));
                    UT_VERIFY(skinning_query.GetJointMapper()->Remap(
                            skel_inv_bind_transforms, &inv_bind_transforms));
                }

                const int joint_idx = joint_indices[0];
                shapes[i].myTransformName = GusdUSD_Utils::TokenToStringHolder(
                        joint_names[joint_idx]);

                geom_bind_xform
                        *= GusdUT_Gf::Cast(inv_bind_transforms[joint_idx]);
            }

            // Import the geometry.
            UT_WorkBuffer primvar_pattern;
            primvar_pattern.append(
                    "* ^skel:geomBindTransform ^skel:skinningMethod");
            if (!skinning_query.HasJointInfluences() ||
                skinning_query.IsRigidlyDeformed())
            {
                primvar_pattern.append(
                        " ^skel:jointIndices ^skel:jointWeights");
            }

            if (!GusdGU_USD::ImportPrimUnpacked(
                    *gdp, skinning_query.GetPrim(), parms.myTime, parms.myLOD,
                    parms.myPurpose, primvar_pattern.buffer(),
                    UT_StringHolder::theEmptyString, true,
                    UT_StringHolder::theEmptyString, &geom_bind_xform,
                    parms.myRefineParms))
            {
                UT_WorkBuffer msg;
                msg.format(
                        "Failed to unpack prim '{0}'.",
                        skinning_query.GetPrim().GetPath().GetString());
                HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
                return false;
            }

            // Convert to polysoups for reduced memory usage.
            GEO_PolySoupParms psoup_parms;
            gdp->polySoup(psoup_parms, gdp);

            // Set up the boneCapture attribute for deforming shapes.
            UT_Optional<GU_AgentLinearSkinDeformer::Method> skinning_method;
            bool has_blendshapes = false;
            if (skinning_query.HasJointInfluences() && !is_static_shape)
            {
                if (GusdCreateCaptureAttribute(
                            *gdp, skinning_query, skel_joint_names,
                            skel_inv_bind_transforms))
                {
                    using Method = GU_AgentLinearSkinDeformer::Method;

                    if (skinning_query.GetSkinningMethod()
                        == UsdSkelTokens->dualQuaternion)
                    {
                        skinning_method = Method::DualQuat;
                    }
                    else
                        skinning_method = Method::Linear;
                }
                else
                {
                    UT_WorkBuffer msg;
                    msg.format(
                            "Failed to import boneCapture attribute for '{0}'.",
                            skinning_query.GetPrim().GetPath().GetString());
                    HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
                }
            }

            // Import blendshape geometry.
            if (skinning_query.HasBlendShapes())
            {
                if (XUSDimportAgentBlendShapes(
                            *gdp, shapes[i].myBlendShapeDetails,
                            shapes[i].myBlendShapeNames, skinning_query,
                            root_path, geom_bind_xform))
                {
                    has_blendshapes = true;
                }
                else
                {
                    UT_WorkBuffer msg;
                    msg.format(
                            "Failed to import blendshapes for '{0}'.",
                            skinning_query.GetPrim().GetPath().GetString());
                    HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
                }
            }

            // Select the appropriate deformer based on the presence of
            // (non-rigid) skinning and blendshapes.
            shapes[i].myDeformer = GU_AgentLayer::getStandardDeformer(
                    skinning_method, has_blendshapes);

            shapes[i].myDetail = gdh;
            return true;
        });

    // Add the shapes to the library and set up the layer's shape bindings.
    const GU_AgentRig &rig = layer.rig();
    UT_StringArray shape_names;
    UT_Array<exint> transforms;
    UT_Array<GU_AgentShapeDeformerConstPtr> deformers;
    UT_Array<UT_Vector3F> bounds_scales;
    for (exint i = 0, n = shapes.size(); i < n; ++i)
    {
        const GU_DetailHandle &gdh = shapes[i].myDetail;
        if (!gdh.isValid())
            continue;

        const UsdSkelSkinningQuery &skinning_query =
            binding.GetSkinningTargets()[i];
        SdfPath path = skinning_query.GetPrim().GetPath();
        UT_StringHolder name = path.MakeRelativePath(root_path).GetString();

        shapelib.addShape(name, gdh);

        shape_names.append(name);
        transforms.append(rig.findTransform(shapes[i].myTransformName));
        deformers.append(shapes[i].myDeformer);
        bounds_scales.append(layer_bounds_scale);

        // Add blendshape inputs to the library.
        const UT_Array<GU_DetailHandle> &input_shapes =
            shapes[i].myBlendShapeDetails;
        const UT_StringArray &input_names = shapes[i].myBlendShapeNames;
        for (exint j = 0; j < input_shapes.size(); ++j)
            shapelib.addShape(input_names[j], input_shapes[j]);
    }

    UT_StringArray errors;
    if (!layer.construct(
            shape_names, transforms, deformers, bounds_scales, &errors))
    {
        UT_WorkBuffer msg;
        msg.append("Failed to create layer.");
        msg.append(errors, "\n");
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    return true;
}

/// Builds the time code range from the stage's start / end time code metadata.
static bool
husdGetTimeCodeRangeFromStage(
        HUSD_AutoReadLock &readlock,
        UsdTimeCode &start_tc,
        UsdTimeCode &end_tc,
        fpreal64 &tc_per_s)
{
    HUSD_Info info(readlock);

    tc_per_s = 0;
    info.getTimeCodesPerSecond(tc_per_s);

    fpreal64 start_tc_val = 0;
    fpreal64 end_tc_val = 0;
    if (!info.getStartTimeCode(start_tc_val)
        || !info.getEndTimeCode(end_tc_val))
    {
        HUSD_ErrorScope::addWarning(
                HUSD_ERR_STRING, "Unable to determine frame range: stage does "
                                 "not specify a valid start time code "
                                 "and end time code. This metadata can be set "
                                 "with the Configure Layer LOP.");
        return false;
    }

    start_tc = UsdTimeCode(start_tc_val);
    end_tc = UsdTimeCode(end_tc_val);
    return true;
}

/// Update the start / end timecode from a particular attribute's list of time
/// samples.
static void
husdCombineTimeSamples(
        const std::vector<double> &times,
        double &start_tc,
        double &end_tc)
{
    if (times.empty())
        return;

    start_tc = SYSmin(start_tc, times.front());
    end_tc = SYSmax(end_tc, times.back());
}

/// Builds the time code range from the time samples of attributes which affect
/// the skeleton animation (the SkelAnimation attributes, and the Skeleton
/// prim's xform).
static bool
husdGetTimeCodeRangeFromSkelAnimation(
        HUSD_AutoReadLock &readlock,
        const UsdSkelSkeleton &skel,
        const UsdSkelAnimQuery &anim_query,
        UsdTimeCode &start_tc,
        UsdTimeCode &end_tc,
        fpreal64 &tc_per_s)
{
    if (!skel || !anim_query.IsValid())
        return false;

    // Use the stage's time codes per second metadata.
    HUSD_Info info(readlock);
    tc_per_s = 0;
    info.getTimeCodesPerSecond(tc_per_s);

    // Include time samples from the joint xforms and blendshape weights.
    double start_tc_val = SYS_FP64_MAX;
    double end_tc_val = SYS_FP64_MIN;

    std::vector<double> times;
    anim_query.GetJointTransformTimeSamples(&times);
    husdCombineTimeSamples(times, start_tc_val, end_tc_val);

    times.clear();
    anim_query.GetBlendShapeWeightTimeSamples(&times);
    husdCombineTimeSamples(times, start_tc_val, end_tc_val);

    // Include time samples for the skeleton prim's xform.
    for (UsdPrim p = skel.GetPrim(); !p.IsPseudoRoot(); p = p.GetParent())
    {
        const UsdGeomXformable xformable(p);
        if (!xformable)
            continue;

        const UsdGeomXformable::XformQuery query(xformable);
        times.clear();
        query.GetTimeSamples(&times);
        husdCombineTimeSamples(times, start_tc_val, end_tc_val);

        if (query.GetResetXformStack())
            break;
    }

    if (SYSisGreater(start_tc_val, end_tc_val))
    {
        HUSD_ErrorScope::addWarning(
                HUSD_ERR_STRING,
                "SkelAnimation does not contain any time samples");
        return false;
    }

    start_tc = UsdTimeCode(start_tc_val);
    end_tc = UsdTimeCode(end_tc_val);
    return true;
}

/// Determines the frame rate and time codes to sample from the stage.
static void
husdGetTimeCodesToSample(
        HUSD_AutoReadLock &readlock,
        HUSD_ClipRangeMode clip_range_mode,
        const UsdSkelSkeleton &skel,
        const UsdSkelAnimQuery &anim_query,
        UsdTimeCode custom_start_tc,
        UsdTimeCode custom_end_tc,
        fpreal64 custom_tc_per_s,
        UT_Array<UsdTimeCode> &timecodes,
        fpreal64 &tc_per_s)
{
    bool success = true;
    UsdTimeCode start_tc;
    UsdTimeCode end_tc;
    tc_per_s = 0;

    switch (clip_range_mode)
    {
        case HUSD_ClipRangeMode::Stage:
            success = husdGetTimeCodeRangeFromStage(
                    readlock, start_tc, end_tc, tc_per_s);
            break;
        case HUSD_ClipRangeMode::SkelAnimation:
            success = husdGetTimeCodeRangeFromSkelAnimation(
                    readlock, skel, anim_query, start_tc, end_tc, tc_per_s);
            break;
        case HUSD_ClipRangeMode::Custom:
            start_tc = custom_start_tc;
            end_tc = custom_end_tc;
            tc_per_s = custom_tc_per_s;
            break;
    };

    // Validate the range and frame rate.
    if (success && SYSisGreater(start_tc.GetValue(), end_tc.GetValue()))
    {
        UT_WorkBuffer msg;
        msg.format(
                "Invalid time range: {0} to {1}", start_tc.GetValue(),
                end_tc.GetValue());
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        success = false;
    }

    if (success && SYSisLessOrEqual(tc_per_s, 0.0))
    {
        UT_WorkBuffer msg;
        msg.format("Invalid number of time codes per second: {0}", tc_per_s);
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        success = false;
    }

    // If we couldn't determine the range, just import a single frame.
    if (!success)
    {
        timecodes.append(UsdTimeCode::Default());
        tc_per_s = 24.0;
        return;
    }

    const exint count = SYSrint(end_tc.GetValue() - start_tc.GetValue()) + 1;
    timecodes.setSizeNoInit(count);
    for (exint i = 0; i < count; ++i)
        timecodes[i] = UsdTimeCode(start_tc.GetValue() + i);
}

static GU_AgentClipPtr
husdImportAgentClip(
        const GU_AgentRigConstPtr &rig,
        HUSD_AutoReadLock &readlock,
        const UsdSkelSkeleton &skel,
        const UsdSkelSkeletonQuery &skelquery,
        HUSD_ClipRangeMode clip_range_mode,
        UsdTimeCode custom_start_tc,
        UsdTimeCode custom_end_tc,
        fpreal64 custom_tc_per_s)
{
    if (!skelquery.IsValid())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid skeleton query.");
        return nullptr;
    }

    const UsdSkelTopology &topology = skelquery.GetTopology();
    const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();

    UT_Array<UsdTimeCode> timecodes;
    fpreal64 tc_per_s;
    husdGetTimeCodesToSample(
            readlock, clip_range_mode, skel, animquery, custom_start_tc,
            custom_end_tc, custom_tc_per_s, timecodes, tc_per_s);

    // The rig's joint order may be different from the skeleton's joint order.
    VtTokenArray skel_joint_names;
    if (!GusdGetJointNames(skelquery.GetSkeleton(), skel_joint_names))
        return nullptr;

    UT_Array<exint> rig_to_skel;
    rig_to_skel.setSizeNoInit(rig->transformCount());
    rig_to_skel.constant(-1);
    for (exint i = 0, n = skel_joint_names.size(); i < n; ++i)
    {
        exint rig_idx = rig->findTransform(skel_joint_names[i].GetString());
        if (rig_idx >= 0)
            rig_to_skel[rig_idx] = i;
    }

    auto clip = GU_AgentClip::addClip(skel.GetPath().GetName(), rig);

    clip->setSampleRate(tc_per_s);
    const exint num_samples = timecodes.entries();
    clip->init(num_samples);

    VtTokenArray channel_names;
    if (animquery.IsValid())
        channel_names = animquery.GetBlendShapeOrder();
    UT_PackedArrayOfArrays<GU_AgentClip::FloatType> blendshape_weights;
    for (exint i = 0, n = channel_names.size(); i < n; ++i)
        blendshape_weights.appendArray(num_samples);

    // Evaluate the skeleton's transforms and blendshape weights at each sample
    // and marshal this into GU_AgentClip.
    VtFloatArray weights;
    VtMatrix4dArray local_matrices;
    GU_AgentClip::XformArray local_xforms;
    for (exint sample_i = 0; sample_i < num_samples; ++sample_i)
    {
        const UsdTimeCode &timecode = timecodes[sample_i];

        // If there aren't any joints (i.e. the rig only has the locomotion
        // transform), don't call ComputeJointLocalTransforms() which will
        // fail.
        // Note that if the animquery is invalid (no animation bound to the
        // skeleton), ComputeJointLocalTransforms() will fall back to the
        // skeleton's rest pose.
        if (rig->transformCount() > 1 &&
            !skelquery.ComputeJointLocalTransforms(&local_matrices, timecode))
        {
            HUSD_ErrorScope::addError(
                HUSD_ERR_STRING, "Failed to compute local transforms.");
            return nullptr;
        }

        const GfMatrix4d root_xform =
            skel.ComputeLocalToWorldTransform(timecode);

        // Note: rig.transformCount() might not match the number of USD joints
        // or their ordering, so we need to carefully remap the joints.
        local_xforms.setSizeNoInit(rig->transformCount());

        for (exint i = 0, n = rig->transformCount(); i < n; ++i)
        {
            const exint skel_idx = rig_to_skel[i];
            if (skel_idx < 0)
                local_xforms[i].identity();
            else
            {
                UT_Matrix4D xform = GusdUT_Gf::Cast(local_matrices[skel_idx]);

                // Apply the skeleton's transform to the root joint.
                if (topology.IsRoot(skel_idx))
                    xform *= GusdUT_Gf::Cast(root_xform);

                local_xforms[i].setMatrix4(GU_AgentClip::Matrix4(xform));
            }
        }

        clip->setLocalTransforms(sample_i, local_xforms);

        // Accumulate blendshape weights.
        if (!channel_names.empty())
        {
            if (!animquery.ComputeBlendShapeWeights(&weights, timecode))
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "Failed to compute blendshape weights.");
                return nullptr;
            }

            for (exint i = 0, n = weights.size(); i < n; ++i)
                blendshape_weights.arrayData(i)[sample_i] = weights[i];
        }
    }

    // Add blendshape channel data.
    // This will add spare channels to the clip for any blendshape channels
    // that don't exist on the rig.
    for (exint i = 0, n = channel_names.size(); i < n; ++i)
    {
        clip->addChannel(
            channel_names[i].GetString(), blendshape_weights.arrayData(i));
    }

    return clip;
}

GU_AgentClipPtr
HUSDimportAgentClip(
        const GU_AgentRigConstPtr &rig,
        HUSD_AutoReadLock &readlock,
        const UT_StringRef &skelrootpath,
        HUSD_ClipRangeMode clip_range_mode,
        HUSD_TimeCode custom_start_tc,
        HUSD_TimeCode custom_end_tc,
        fpreal64 custom_tc_per_s)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return nullptr;

    const UsdSkelBinding &binding = bindings[0];
    return husdImportAgentClip(
            rig, readlock, binding.GetSkeleton(),
            skelcache.GetSkelQuery(binding.GetSkeleton()), clip_range_mode,
            HUSDgetUsdTimeCode(custom_start_tc),
            HUSDgetUsdTimeCode(custom_end_tc), custom_tc_per_s);
}

UT_Array<GU_AgentClipPtr>
HUSDimportAgentClips(
        const GU_AgentRigConstPtr &rig,
        HUSD_AutoReadLock &readlock,
        const UT_StringRef &prim_pattern,
        HUSD_ClipRangeMode clip_range_mode,
        HUSD_TimeCode custom_start_tc,
        HUSD_TimeCode custom_end_tc,
        fpreal64 custom_tc_per_s)
{
    HUSD_FindPrims findprims(readlock);

    if (!readlock.data() || !readlock.data()->isStageValid())
        return UT_Array<GU_AgentClipPtr>();

    if (!findprims.addPattern(prim_pattern,
            readlock.dataHandle().nodeId(), HUSD_TimeCode()))
    {
        HUSD_ErrorScope::addError(
            HUSD_ERR_FAILED_TO_PARSE_PATTERN, findprims.getLastError());
        return UT_Array<GU_AgentClipPtr>();
    }

    // Allow matching against SkelRoot prims in addition to Skeleton prims, for
    // consistency with the Agent SOP.
    XUSD_PathSet skeletonpaths;
    const TfType &skeletontype = HUSDfindType("Skeleton");
    XUSD_PathSet skelrootpaths;
    const TfType &skelroottype = HUSDfindType("SkelRoot");
    UsdStageRefPtr stage = readlock.constData()->stage();

    for (auto &&path : findprims.getExpandedPathSet().sdfPathSet())
    {
        UsdPrim prim = stage->GetPrimAtPath(path);
        if (prim && prim.IsA(skeletontype))
            skeletonpaths.insert(path);
    }

    if (skeletonpaths.empty())
    {
        for (auto &&path : findprims.getExpandedPathSet().sdfPathSet())
        {
            UsdPrim prim = stage->GetPrimAtPath(path);
            if (prim && prim.IsA(skelroottype))
                skelrootpaths.insert(path);
        }
    }

    if (skeletonpaths.empty() && skelrootpaths.empty())
    {
        HUSD_ErrorScope::addError(
            HUSD_ERR_STRING, "Pattern does not specify any Skeleton prims.");
        return UT_Array<GU_AgentClipPtr>();
    }

    UT_Array<GU_AgentClipPtr> clips;
    if (!skelrootpaths.empty())
    {
        for (const auto &skelrootpath : skelrootpaths)
        {
            auto clip = HUSDimportAgentClip(
                    rig, readlock, skelrootpath.GetText(), clip_range_mode,
                    custom_start_tc, custom_end_tc, custom_tc_per_s);

            if (!clip)
                return UT_Array<GU_AgentClipPtr>();

            clips.append(clip);
        }
    }
    else
    {
        XUSD_ConstDataPtr data(readlock.data());
        UT_ASSERT(data && data->isStageValid());

        UsdSkelCache skelcache;
        for (const auto &sdfpath : skeletonpaths)
        {
            UsdPrim prim(data->stage()->GetPrimAtPath(sdfpath));
            UT_ASSERT(prim);

            UsdSkelSkeleton skel(prim);
            UT_ASSERT(skel);

            UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);

            GU_AgentClipPtr clip = husdImportAgentClip(
                    rig, readlock, skel, skelcache.GetSkelQuery(skel),
                    clip_range_mode, HUSDgetUsdTimeCode(custom_start_tc),
                    HUSDgetUsdTimeCode(custom_end_tc), custom_tc_per_s);
            if (!clip)
                return UT_Array<GU_AgentClipPtr>();

            clips.append(clip);
        }
    }

    return clips;
}
