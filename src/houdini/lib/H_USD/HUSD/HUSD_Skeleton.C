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
#include "HUSD_Info.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"

#include <GA/GA_Names.h>
#include <GEO/GEO_PolyCounts.h>
#include <GEO/GEO_PrimPoly.h>
#include <GEO/GEO_PrimPolySoup.h>
#include <GT/GT_RefineParms.h>
#include <GU/GU_AttributeSwap.h>
#include <GU/GU_Detail.h>
#include <GU/GU_MergeUtils.h>
#include <GU/GU_PackedGeometry.h>
#include <gusd/USD_Utils.h>
#include <gusd/GU_USD.h>
#include <gusd/UT_Gf.h>
#include <gusd/agentUtils.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/utils.h>

static constexpr UT_StringLit theSkelPathAttrib("usdskelpath");
static constexpr UT_StringLit theAnimPathAttrib("usdanimpath");

PXR_NAMESPACE_USING_DIRECTIVE

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
husdFindSkelBindings(const HUSD_AutoReadLock &readlock,
                     const UT_StringRef &skelrootpath,
                     UsdSkelCache &skelcache,
                     std::vector<UsdSkelBinding> &bindings)
{
    XUSD_ConstDataPtr data(readlock.data());

    if (!data || !data->isStageValid())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid stage.");
        return false;
    }

    SdfPath sdfpath = HUSDgetSdfPath(skelrootpath);
    UsdPrim prim(data->stage()->GetPrimAtPath(sdfpath));
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

    skelcache.Populate(skelroot);

    bindings.clear();
    if (!skelcache.ComputeSkelBindings(skelroot, &bindings) || bindings.empty())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                  "Could not find any skeleton bindings.");
        return false;
    }

    return true;
}

bool
HUSDimportSkinnedGeometry(GU_Detail &gdp, const HUSD_AutoReadLock &readlock,
                          const UT_StringRef &skelrootpath,
                          const UT_StringHolder &shapeattrib)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    const SdfPath root_path = HUSDgetSdfPath(skelrootpath);
    GT_RefineParms refine_parms = husdShapeRefineParms();

    for (const UsdSkelBinding &binding : bindings)
    {
        UT_Array<GU_DetailHandle> details;
        details.setSize(binding.GetSkinningTargets().size());

        GusdSkinImportParms parms;
        parms.myRefineParms = &refine_parms;

        bool success = GusdForEachSkinnedPrim(
            binding, parms,
            [&binding, &details, &root_path, &shapeattrib](
                exint i, const GusdSkinImportParms &parms,
                const VtTokenArray &joint_names,
                const VtMatrix4dArray &inv_bind_transforms) {

                const UsdSkelSkinningQuery &skinning_query =
                    binding.GetSkinningTargets()[i];

                GU_DetailHandle &gdh = details[i];
                gdh.allocateAndSet(new GU_Detail);
                GU_Detail *gdp = gdh.gdpNC();
                GU_Detail *skin_gdp = gdp;

                // Rigidly deformed shapes will be imported as a packed
                // primitive.
                GU_DetailHandle packed_gdh;
                if (skinning_query.IsRigidlyDeformed())
                {
                    packed_gdh.allocateAndSet(new GU_Detail);
                    skin_gdp = packed_gdh.gdpNC();
                }

                // Import the geometry.
                UT_WorkBuffer primvar_pattern;
                primvar_pattern.append("* ^skel:geomBindTransform");
                if (!skinning_query.HasJointInfluences() ||
                    skinning_query.IsRigidlyDeformed())
                {
                    primvar_pattern.append(
                        " ^skel:jointIndices ^skel:jointWeights");
                }

                if (!GusdGU_USD::ImportPrimUnpacked(
                        *skin_gdp, skinning_query.GetPrim(), parms.myTime,
                        parms.myLOD, parms.myPurpose, primvar_pattern.buffer(),
                        true, UT_StringHolder::theEmptyString,
                        &GusdUT_Gf::Cast(skinning_query.GetGeomBindTransform()),
                        parms.myRefineParms))
                {
                    gdh.clear();
                }

                // Convert to polysoups for reduced memory usage.
                GEO_PolySoupParms psoup_parms;
                skin_gdp->polySoup(psoup_parms, skin_gdp);

                // Create the shapename attribute.
                SdfPath path = skinning_query.GetPrim().GetPath();
                UT_StringHolder shape_name =
                    path.MakeRelativePath(root_path).GetString();
                GA_RWHandleS shapeattrib_h(skin_gdp->addStringTuple(
                    GA_ATTRIB_PRIMITIVE, shapeattrib, 1));

                for (GA_Offset primoff : skin_gdp->getPrimitiveRange())
                    shapeattrib_h.set(primoff, shape_name);

                // Create a packed primitive for rigidly deformed shapes.
                if (skinning_query.IsRigidlyDeformed())
                    GU_PackedGeometry::packGeometry(*gdp, packed_gdh);

                // Set up the boneCapture attribute on the shape geometry or
                // packed primitive.
                if (skinning_query.HasJointInfluences() &&
                    !GusdCreateCaptureAttribute(
                        *gdp, skinning_query, joint_names, inv_bind_transforms))
                {
                    gdh.clear();
                }
            });

        if (!success)
        {
            HUSD_ErrorScope::addError(
                HUSD_ERR_STRING, "Failed to load shapes.");
            return false;
        }

        // Merge all the shapes together.
        UT_Array<GU_Detail *> gdps;
        for (GU_DetailHandle &gdh : details)
        {
            if (gdh.isValid())
                gdps.append(gdh.gdpNC());
        }

        GUmatchAttributesAndMerge(gdp, gdps);
    }

    // Bump all data ids since we've created new geometry.
    gdp.bumpAllDataIds();

    return true;
}

bool
HUSDimportSkeleton(GU_Detail &gdp, const HUSD_AutoReadLock &readlock,
                   const UT_StringRef &skelrootpath,
                   HUSD_SkeletonPoseType pose_type)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    GA_RWHandleS name_attrib =
        gdp.addStringTuple(GA_ATTRIB_POINT, GA_Names::name, 1);

    GA_RWHandleM3D xform_attrib =
        gdp.addFloatTuple(GA_ATTRIB_POINT, GA_Names::transform, 9);
    xform_attrib->setTypeInfo(GA_TypeInfo::GA_TYPE_TRANSFORM);

    GA_RWHandleS skelpath_attrib = gdp.addStringTuple(
        GA_ATTRIB_PRIMITIVE, theSkelPathAttrib.asHolder(), 1);

    GA_RWHandleS animpath_attrib;
    if (pose_type == HUSD_SkeletonPoseType::Animation)
    {
        animpath_attrib = gdp.addStringTuple(GA_ATTRIB_PRIMITIVE,
                                             theAnimPathAttrib.asHolder(), 1);
    }

    for (const UsdSkelBinding &binding : bindings)
    {
        const UsdSkelSkeleton &skel = binding.GetSkeleton();
        UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);
        if (!skelquery.IsValid())
        {
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                      "Invalid skeleton query.");
            return false;
        }

        const UsdSkelTopology &topology = skelquery.GetTopology();

        VtTokenArray joints;
        if (!skel.GetJointsAttr().Get(&joints))
        {
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                      "'joints' attribute is invalid.");
            return false;
        }

        // Prefer the jointNames attribute if it was authored, since it
        // provides nicer unique names than the full paths.
        VtTokenArray joint_names;
        if (skel.GetJointNamesAttr().Get(&joint_names))
        {
            if (joint_names.size() != joints.size())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "'jointNames' attribute does not match "
                                     "the size of the 'joints' attribute.");
                return false;
            }
        }
        else
            joint_names = joints;

        // Create a point for each joint, and connect each point to its parent
        // with a polygon.
        GA_Offset start_ptoff = gdp.appendPointBlock(topology.GetNumJoints());
        UT_Array<int> poly_ptnums;
        for (exint i = 0, n = topology.GetNumJoints(); i < n; ++i)
        {
            GA_Offset ptoff = start_ptoff + i;
            name_attrib.set(ptoff,
                            GusdUSD_Utils::TokenToStringHolder(joint_names[i]));

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
            if (!animquery.IsValid())
            {
                HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                          "Invalid animation query.");
                return false;
            }

            const UT_StringHolder animpath =
                animquery.GetPrim().GetPath().GetString();
            for (exint i = 0, n = poly_sizes.getNumPolygons(); i < n; ++i)
                animpath_attrib.set(start_primoff + i, animpath);
        }
    }

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
HUSDimportSkeletonPose(GU_Detail &gdp, const HUSD_AutoReadLock &readlock,
                       const UT_StringRef &skelrootpath,
                       HUSD_SkeletonPoseType pose_type, fpreal time)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    GA_RWHandleM3D xform_attrib =
        gdp.findFloatTuple(GA_ATTRIB_POINT, GA_Names::transform, 9);
    UT_ASSERT(xform_attrib.isValid());

    GA_Index ptidx = 0;
    for (const UsdSkelBinding &binding : bindings)
    {
        const UsdSkelSkeleton &skel = binding.GetSkeleton();
        UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);
        if (!skelquery.IsValid())
        {
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                      "Invalid skeleton query.");
            return false;
        }

        const UsdSkelTopology &topology = skelquery.GetTopology();

        VtMatrix4dArray world_xforms;
        switch (pose_type)
        {
        case HUSD_SkeletonPoseType::Animation:
        {
            const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();
            if (!animquery.IsValid())
            {
                HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                          "Invalid animation query.");
                return false;
            }

            VtMatrix4dArray local_xforms;
            const UsdTimeCode timecode =
                HUSDgetUsdTimeCode(HUSD_TimeCode(time, HUSD_TimeCode::TIME));
            if (!animquery.ComputeJointLocalTransforms(&local_xforms, timecode))
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "Failed to compute local transforms.");
                return false;
            }

            if (!husdComputeWorldTransforms(skel, topology, timecode,
                                            local_xforms, world_xforms))
            {
                return false;
            }

            // TODO - output time range detail attribute.
        }
        break;

        case HUSD_SkeletonPoseType::BindPose:
        {
            if (!skel.GetBindTransformsAttr().Get(&world_xforms))
            {
                HUSD_ErrorScope::addError(
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
                HUSD_ErrorScope::addError(
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

            const UsdTimeCode timecode =
                HUSDgetUsdTimeCode(HUSD_TimeCode(time, HUSD_TimeCode::TIME));
            if (!husdComputeWorldTransforms(skel, topology, timecode,
                                            local_xforms, world_xforms))
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
    }

    gdp.getP()->bumpDataId();
    xform_attrib.bumpDataId();

    return true;
}

GU_AgentRigPtr
HUSDimportAgentRig(const HUSD_AutoReadLock &readlock,
                   const UT_StringRef &skelrootpath,
                   const UT_StringHolder &rig_name)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return nullptr;

    const UsdSkelBinding &binding = bindings[0];

    const UsdSkelSkeleton &skel = binding.GetSkeleton();
    UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);
    GU_AgentRigPtr rig = GusdCreateAgentRig(rig_name, skelquery);
    if (!rig)
        return nullptr;

    // TODO - add channels for blendshapes.
#if 0
    const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();
    if (!animquery.IsValid())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid animation query.");
        return nullptr;
    }
#endif

    return rig;
}

bool
HUSDimportAgentShapes(GU_AgentShapeLib &shapelib,
                      GU_AgentLayer &layer,
                      const HUSD_AutoReadLock &readlock,
                      const UT_StringRef &skelrootpath,
                      fpreal layer_bounds_scale)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    const UsdSkelBinding &binding = bindings[0];

    struct ShapeInfo
    {
        GU_DetailHandle myDetail;
        GU_AgentShapeDeformerConstPtr myDeformer;
        UT_StringHolder myTransformName;
    };
    UT_Array<ShapeInfo> shapes;
    shapes.setSize(binding.GetSkinningTargets().size());

    GT_RefineParms refine_parms = husdShapeRefineParms();
    GusdSkinImportParms parms;
    parms.myRefineParms = &refine_parms;

    // Convert the shapes to Houdini geometry.
    bool success = GusdForEachSkinnedPrim(
        binding, parms,
        [&binding, &shapes](
            exint i, const GusdSkinImportParms &parms,
            const VtTokenArray &joint_names,
            const VtMatrix4dArray &inv_bind_transforms) {
            const UsdSkelSkinningQuery &skinning_query =
                binding.GetSkinningTargets()[i];

            GU_DetailHandle &gdh = shapes[i].myDetail;
            gdh.allocateAndSet(new GU_Detail);
            GU_Detail *gdp = gdh.gdpNC();

            // A static shape is equivalent to a rigid deformation with a
            // single influence.
            const bool is_static_shape =
                !skinning_query.HasBlendShapes() &&
                skinning_query.HasJointInfluences() &&
                skinning_query.IsRigidlyDeformed() &&
                (skinning_query.GetNumInfluencesPerComponent() == 1);

            // For a static shape, record the joint that it's attached to, and
            // bake in the inverse bind transform since static agent shapes are
            // simply transformed by the joint transform.
            UT_Matrix4D geom_bind_xform =
                GusdUT_Gf::Cast(skinning_query.GetGeomBindTransform());
            if (is_static_shape)
            {
                VtIntArray joint_indices;
                UT_VERIFY(skinning_query.GetJointIndicesPrimvar().Get(
                    &joint_indices));

                const int joint_idx = joint_indices[0];
                shapes[i].myTransformName = GusdUSD_Utils::TokenToStringHolder(
                    joint_names[joint_idx]);

                geom_bind_xform *=
                    GusdUT_Gf::Cast(inv_bind_transforms[joint_idx]);
            }
            else
            {
                shapes[i].myDeformer = GU_AgentLayer::getLinearSkinDeformer();
            }

            // Import the geometry.
            UT_WorkBuffer primvar_pattern;
            primvar_pattern.append("* ^skel:geomBindTransform");
            if (!skinning_query.HasJointInfluences() ||
                skinning_query.IsRigidlyDeformed())
            {
                primvar_pattern.append(
                    " ^skel:jointIndices ^skel:jointWeights");
            }

            if (!GusdGU_USD::ImportPrimUnpacked(
                    *gdp, skinning_query.GetPrim(), parms.myTime, parms.myLOD,
                    parms.myPurpose, primvar_pattern.buffer(), true,
                    UT_StringHolder::theEmptyString, &geom_bind_xform,
                    parms.myRefineParms))
            {
                gdh.clear();
            }

            // Convert to polysoups for reduced memory usage.
            GEO_PolySoupParms psoup_parms;
            gdp->polySoup(psoup_parms, gdp);

            // Set up the boneCapture attribute for deforming shapes.
            if (skinning_query.HasJointInfluences() && !is_static_shape &&
                !GusdCreateCaptureAttribute(
                    *gdp, skinning_query, joint_names, inv_bind_transforms))
            {
                gdh.clear();
            }
        });

    if (!success)
        return false;

    // Add the shapes to the library and set up the layer's shape bindings.
    const SdfPath root_path = HUSDgetSdfPath(skelrootpath);
    const GU_AgentRig &rig = layer.rig();
    UT_StringArray shape_names;
    UT_IntArray transforms;
    UT_Array<GU_AgentShapeDeformerConstPtr> deformers;
    UT_FprealArray bounds_scales;
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
    }

    UT_StringArray errors;
    if (!layer.construct(
            shape_names, transforms, deformers, &bounds_scales, &errors))
    {
        UT_WorkBuffer msg;
        msg.append("Failed to create layer.");
        msg.append(errors, "\n");
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    return true;
}

bool
HUSDimportAgentClip(GU_AgentClip &clip,
                    HUSD_AutoReadLock &readlock,
                    const UT_StringRef &skelrootpath)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    const UsdSkelBinding &binding = bindings[0];
    const UsdSkelSkeleton &skel = binding.GetSkeleton();

    UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);
    if (!skelquery.IsValid())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid skeleton query.");
        return false;
    }

    const UsdSkelTopology &topology = skelquery.GetTopology();
    const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();
    if (!animquery.IsValid())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid animation query.");
        return false;
    }

    // Determine the frame range and framerate.
    HUSD_Info info(readlock);
    fpreal64 start_time = 0;
    fpreal64 end_time = 0;
    fpreal64 tc_per_s = 0;
    info.getTimeCodesPerSecond(tc_per_s);
    if (!info.getStartTimeCode(start_time) || !info.getEndTimeCode(end_time) ||
        SYSisGreater(start_time, end_time))
    {
        HUSD_ErrorScope::addError(
            HUSD_ERR_STRING, "Stage does not specify a valid start time code "
                             "and end time code.");
        return false;
    }

    const exint num_samples = SYSrint(end_time - start_time);
    clip.setSampleRate(tc_per_s);
    clip.init(num_samples);

    const GU_AgentRig &rig = clip.rig();
    const UT_XformOrder xord(UT_XformOrder::SRT, UT_XformOrder::XYZ);

    // Evaluate the skeleton's transforms at each sample and marshal this into
    // GU_AgentClip.
    VtMatrix4dArray local_matrices;
    GU_AgentClip::XformArray local_xforms;
    UT_Vector3F r, s, t;
    for (exint sample_i = 0; sample_i < num_samples; ++sample_i)
    {
        UsdTimeCode timecode(start_time + sample_i);
        if (!animquery.ComputeJointLocalTransforms(&local_matrices, timecode))
        {
            HUSD_ErrorScope::addError(
                HUSD_ERR_STRING, "Failed to compute local transforms.");
            return false;
        }

        const GfMatrix4d root_xform =
            skel.ComputeLocalToWorldTransform(timecode);

        // Note: rig.transformCount() might not match the number of USD joints
        // due to the added __locomotion__ transform, but the indices should
        // match otherwise.
        local_xforms.setSizeNoInit(rig.transformCount());

        for (exint i = 0, n = rig.transformCount(); i < n; ++i)
        {
            if (i >= local_matrices.size())
                local_xforms[i].identity();
            else
            {
                UT_Matrix4D xform = GusdUT_Gf::Cast(local_matrices[i]);

                // Apply the skeleton's transform to the root joint.
                if (topology.IsRoot(i))
                    xform *= GusdUT_Gf::Cast(root_xform);

                xform.explode(xord, r, s, t);
                local_xforms[i].setTransform(t.x(), t.y(), t.z(), r.x(), r.y(),
                                             r.z(), s.x(), s.y(), s.z());
            }
        }

        clip.setLocalTransforms(sample_i, local_xforms);
    }

    return true;
}
