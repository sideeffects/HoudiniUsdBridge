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
#include "HUSD_PathSet.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <GA/GA_Names.h>
#include <GEO/GEO_PolyCounts.h>
#include <GEO/GEO_PrimPoly.h>
#include <GEO/GEO_PrimPolySoup.h>
#include <GT/GT_RefineParms.h>
#include <GU/GU_AgentBlendShapeUtils.h>
#include <GU/GU_AttributeSwap.h>
#include <GU/GU_Detail.h>
#include <GU/GU_MergeUtils.h>
#include <GU/GU_MotionClipUtil.h>
#include <GU/GU_PackedGeometry.h>
#include <GU/GU_PrimPacked.h>
#include <gusd/USD_Utils.h>
#include <gusd/GU_USD.h>
#include <gusd/UT_Gf.h>
#include <gusd/agentUtils.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/blendShapeQuery.h>
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

static bool
husdImportBlendShapes(
        GU_Detail &detail,
        const UsdSkelSkinningQuery &skinning_query,
        const SdfPath &root_path);

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
                // primitive, unless they have blendshapes.
                GU_DetailHandle packed_gdh;
                bool rigidly_deformed = skinning_query.HasJointInfluences()
                                        && skinning_query.IsRigidlyDeformed()
                                        && !skinning_query.HasBlendShapes();
                if (rigidly_deformed)
                {
                    packed_gdh.allocateAndSet(new GU_Detail);
                    skin_gdp = packed_gdh.gdpNC();
                }

                // Import the geometry.
                UT_WorkBuffer primvar_pattern;
                primvar_pattern.append("* ^skel:geomBindTransform");
                if (!skinning_query.HasJointInfluences() || rigidly_deformed)
                {
                    primvar_pattern.append(
                        " ^skel:jointIndices ^skel:jointWeights");
                }

                if (!GusdGU_USD::ImportPrimUnpacked(
                        *skin_gdp, skinning_query.GetPrim(), parms.myTime,
                        parms.myLOD, parms.myPurpose, primvar_pattern.buffer(),
                        UT_StringHolder::theEmptyString, true,
                        UT_StringHolder::theEmptyString,
                        &GusdUT_Gf::Cast(skinning_query.GetGeomBindTransform()),
                        parms.myRefineParms))
                {
                    return false;
                }

                // This should match what we do in SOP_FbxSkinImport.C, which
                // has also been disabled.
#if 0
                // Convert to polysoups for reduced memory usage.
                GEO_PolySoupParms psoup_parms;
                skin_gdp->polySoup(psoup_parms, skin_gdp);
#endif

                // Import blendshape inputs.
                if (skinning_query.HasBlendShapes()
                    && !husdImportBlendShapes(
                            *skin_gdp, skinning_query, root_path))
                {
                    return false;
                }

                // Create the shapename attribute.
                SdfPath path = skinning_query.GetPrim().GetPath();
                UT_StringHolder shape_name =
                    path.MakeRelativePath(root_path).GetString();
                GA_RWBatchHandleS shapeattrib_h(skin_gdp->addStringTuple(
                    GA_ATTRIB_PRIMITIVE, shapeattrib, 1));
                shapeattrib_h.set(skin_gdp->getPrimitiveRange(), shape_name);

                // Create a packed primitive for rigidly deformed shapes.
                if (skinning_query.IsRigidlyDeformed())
                {
                    GU_PrimPacked *packed_prim =
                            GU_PackedGeometry::packGeometry(*gdp, packed_gdh);

                    // Also add the name and usdprimpath attribs on the outer
                    // packed prim.
                    GA_RWHandleS packed_shapeattrib = gdp->addStringTuple(
                            GA_ATTRIB_PRIMITIVE, shapeattrib, 1);
                    packed_shapeattrib.set(
                            packed_prim->getMapOffset(), shape_name);

                    GA_RWHandleS prim_path_attr = gdp->addStringTuple(
                            GA_ATTRIB_PRIMITIVE, GUSD_PRIMPATH_ATTR, 1);
                    prim_path_attr.set(
                            packed_prim->getMapOffset(), path.GetString());
                }

                // Set up the boneCapture attribute on the shape geometry or
                // packed primitive.
                if (skinning_query.HasJointInfluences() &&
                    !GusdCreateCaptureAttribute(
                        *gdp, skinning_query, joint_names, inv_bind_transforms))
                {
                    return false;
                }

                return true;
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

    GU_MotionClipChannelMap channel_map;
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

        // Add attributes for blendshape channels (unless we're just generating
        // the bind pose).
        const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();
        if (pose_type != HUSD_SkeletonPoseType::BindPose && animquery.IsValid())
        {
            for (const TfToken &channel_token : animquery.GetBlendShapeOrder())
            {
                UT_StringHolder channel_name
                        = GusdUSD_Utils::TokenToStringHolder(channel_token);
                UT_StringHolder attrib_name
                        = channel_name.forceValidVariableName();

                gdp.addFloatTuple(GA_ATTRIB_DETAIL, attrib_name, 1);
                channel_map.addDetailAttrib(channel_name, attrib_name);
            }
        }

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
                UT_WorkBuffer msg;
                msg.format(
                        "Skeleton '{0}' does not have an animation binding.",
                        skelpath);
                HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
                return false;
            }

            const UT_StringHolder animpath =
                animquery.GetPrim().GetPath().GetString();
            for (exint i = 0, n = poly_sizes.getNumPolygons(); i < n; ++i)
                animpath_attrib.set(start_primoff + i, animpath);
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
        VtTokenArray channel_names;
        VtFloatArray channel_values;

        switch (pose_type)
        {
        case HUSD_SkeletonPoseType::Animation:
        {
            VtMatrix4dArray local_xforms;
            const UsdTimeCode timecode =
                HUSDgetUsdTimeCode(HUSD_TimeCode(time, HUSD_TimeCode::TIME));
            if (!skelquery.ComputeJointLocalTransforms(&local_xforms, timecode))
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

            // Evaluate the blend shape channels.
            UsdSkelAnimQuery animquery = skelquery.GetAnimQuery();
            if (animquery.IsValid())
            {
                channel_names = animquery.GetBlendShapeOrder();

                if (!channel_names.empty()
                    && !animquery.ComputeBlendShapeWeights(
                            &channel_values, timecode))
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
HUSDimportAgentRig(const HUSD_AutoReadLock &readlock,
                   const UT_StringRef &skelrootpath,
                   const UT_StringHolder &rig_name,
                   bool create_locomotion_joint)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return nullptr;

    const UsdSkelBinding &binding = bindings[0];

    const UsdSkelSkeleton &skel = binding.GetSkeleton();
    UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);
    GU_AgentRigPtr rig = GusdCreateAgentRig(rig_name, skelquery,
                                            create_locomotion_joint);
    if (!rig)
        return nullptr;

    // Add blendshape channels to the rig.
    const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();
    if (animquery.IsValid())
    {
        for (const TfToken &channel_name : animquery.GetBlendShapeOrder())
        {
            rig->addChannel(
                GusdUSD_Utils::TokenToStringHolder(channel_name), 0.0, -1);
        }
    }

    return rig;
}

static bool
husdGetOffsets(const UsdSkelBlendShape &blendshape, VtVec3fArray &offsets)
{
    return blendshape.GetOffsetsAttr().Get(&offsets);
}

static bool
husdGetOffsets(const UsdSkelInbetweenShape &inbetween, VtVec3fArray &offsets)
{
    return inbetween.GetOffsets(&offsets);
}

/// Import the geometry for a blendshape input or in-between shape, which
/// consists of point positions and an id attribute (for sparse blendshapes).
/// In-between shapes use the point indices from the primary shape, if
/// authored.
template <typename BlendshapeT>
static bool
husdImportBlendShape(GU_Detail &detail,
                     const BlendshapeT &blendshape_or_inbetween,
                     const UsdSkelBlendShape &blendshape,
                     const GU_Detail &base_shape)
{
    VtVec3fArray offsets;
    if (!husdGetOffsets(blendshape_or_inbetween, offsets))
    {
        HUSD_ErrorScope::addError(
            HUSD_ERR_STRING, "'offsets' attribute was not authored.");
        return false;
    }

    bool has_indices = false;
    VtIntArray indices;
    if (blendshape.GetPointIndicesAttr().Get(&indices))
    {
        has_indices = true;
        if (indices.size() != offsets.size())
        {
            HUSD_ErrorScope::addError(
                HUSD_ERR_STRING, "Mismatched number of indices and offsets.");
            return false;
        }
    }
    else if (base_shape.getNumPoints() != offsets.size())
    {
        // If this isn't sparse, we should have the same number of points as
        // the base shape!
        HUSD_ErrorScope::addError(
            HUSD_ERR_STRING,
            "Blendshape has a different number of points than the base shape");
        return false;
    }

    // Translate the pointIndices attr back to an 'id' attribute for GU_Blend
    // to match up points by id.
    GA_ROHandleI base_id_attrib;
    GA_RWHandleI id_attrib;
    if (has_indices)
    {
        id_attrib = detail.addIntTuple(GA_ATTRIB_POINT, GA_Names::id, 1);
        base_id_attrib = base_shape.findIntTuple(
            GA_ATTRIB_POINT, GA_Names::id, 1);
    }

    GA_Offset ptoff = detail.appendPointBlock(offsets.size());
    for (exint i = 0, n = offsets.size(); i < n; ++i, ++ptoff)
    {
        GA_Index base_ptidx;
        if (has_indices)
        {
            base_ptidx = indices[i];
            if (base_ptidx < 0 || base_ptidx >= base_shape.getNumPoints())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "Invalid point index.");
                return false;
            }
        }
        else
            base_ptidx = i;

        const GA_Offset base_ptoff = base_shape.pointOffset(base_ptidx);

        // USD blendshapes store offsets from the base shape's positions, but
        // for agents we need the actual point positions.
        UT_Vector3 pos = base_shape.getPos3(base_ptoff);
        pos += GusdUT_Gf::Cast(offsets[i]);
        detail.setPos3(ptoff, pos);

        // Record the id point attribute for sparse blendshapes.
        if (has_indices)
        {
            id_attrib.set(ptoff, base_id_attrib.isValid() ?
                                     base_id_attrib.get(base_ptoff) :
                                     static_cast<int>(base_ptidx));
        }
    }

    return true;
}

static bool
husdFindBlendShapes(
        const UsdSkelSkinningQuery &skinning_query,
        VtTokenArray &channel_names,
        UT_Array<UsdSkelBlendShape> &blendshapes)
{
    UsdSkelBlendShapeQuery blendshape_query(
            UsdSkelBindingAPI(skinning_query.GetPrim()));
    UT_ASSERT(blendshape_query.IsValid());

    if (!skinning_query.GetBlendShapeOrder(&channel_names))
    {
        UT_WorkBuffer msg;
        msg.format(
                "Failed to compute blendshape order for '{}'",
                skinning_query.GetPrim().GetPath().GetString());
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    UT_ASSERT(channel_names.size() == blendshape_query.GetNumBlendShapes());

    blendshapes.setCapacity(blendshape_query.GetNumBlendShapes());
    for (exint i = 0, n = blendshape_query.GetNumBlendShapes(); i < n; ++i)
        blendshapes.append(blendshape_query.GetBlendShape(i));

    return true;
}

/// Import blendshapes for USD Skin Import.
static bool
husdImportBlendShapes(
        GU_Detail &detail,
        const UsdSkelSkinningQuery &skinning_query,
        const SdfPath &root_path)
{
    VtTokenArray channel_names;
    UT_Array<UsdSkelBlendShape> blendshapes;
    if (!husdFindBlendShapes(skinning_query, channel_names, blendshapes))
        return false;

    // Import the blendshape points.
    UT_Array<GU_DetailHandle> shape_details;
    for (exint i = 0, n = blendshapes.entries(); i < n; ++i)
    {
        const UsdSkelBlendShape &blendshape = blendshapes[i];

        GU_DetailHandle shape_gdh;
        shape_gdh.allocateAndSet(new GU_Detail());

        GU_DetailHandleAutoWriteLock shape_detail(shape_gdh);
        if (!husdImportBlendShape(
                    *shape_detail, blendshape, blendshape, detail))
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Failed to import blendshape '{}'",
                    blendshape.GetPath().GetString());
            HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
            return false;
        }

        shape_details.append(shape_gdh);
    }

    GA_RWHandleS channel_attrib = detail.addStringTuple(
            GA_ATTRIB_PRIMITIVE, GU_MotionClipNames::blendshape_channel, 1);
    GA_RWHandleS shape_name_attrib = detail.addStringTuple(
            GA_ATTRIB_PRIMITIVE, GU_MotionClipNames::blendshape_name, 1);
    GA_PrimitiveGroup *hidden_group
            = detail.newPrimitiveGroup(GA_Names::_3d_hidden_primitives);

    // Add packed primitives for each shape.
    for (exint i = 0, n = blendshapes.entries(); i < n; ++i)
    {
        const UsdSkelBlendShape &blendshape = blendshapes[i];

        UT_StringHolder channel_name
                = GusdUSD_Utils::TokenToStringHolder(channel_names[i]);
        SdfPath path
                = blendshape.GetPrim().GetPath().MakeRelativePath(root_path);
        const UT_StringHolder shape_name = path.GetString();

        const GU_DetailHandle &shape_gdh = shape_details[i];

        auto packed = GU_PackedGeometry::packGeometry(detail, shape_gdh);
        const GA_Offset primoff = packed->getMapOffset();

        channel_attrib.set(primoff, channel_name);
        shape_name_attrib.set(primoff, shape_name);
        hidden_group->addOffset(primoff);
    }

    return true;
}

/// Import the geometry for all blendshape inputs (including in-between
/// shapes), and record the necessary detail attributes on the base shape for
/// the agent blendshape deformer.
static bool
husdImportAgentBlendShapes(
        GU_Detail &base_shape,
        UT_Array<GU_DetailHandle> &all_shape_details,
        UT_StringArray &all_shape_names,
        const UsdSkelSkinningQuery &skinning_query,
        const SdfPath &root_path)
{
    VtTokenArray channel_names_attr;
    UT_Array<UsdSkelBlendShape> blendshapes;
    if (!husdFindBlendShapes(skinning_query, channel_names_attr, blendshapes))
        return false;

    UT_StringArray shape_names;
    shape_names.setCapacity(blendshapes.size());
    UT_StringArray channel_names;
    channel_names.setCapacity(blendshapes.size());

    static constexpr UT_StringLit theInbetweensPrefix("inbetweens:");
    UT_WorkBuffer inbetween_name;
    UT_StringArray inbetween_names;
    UT_Array<fpreal> inbetween_weights;

    for (exint i = 0, n = blendshapes.entries(); i < n; ++i)
    {
        const UsdSkelBlendShape &blendshape = blendshapes[i];

        channel_names.append(channel_names_attr[i].GetString());

        SdfPath path =
            blendshape.GetPrim().GetPath().MakeRelativePath(root_path);
        UT_StringHolder name = path.GetString();
        shape_names.append(name);

        GU_DetailHandle gdh;
        gdh.allocateAndSet(new GU_Detail());

        GU_DetailHandleAutoWriteLock detail(gdh);
        if (!husdImportBlendShape(*detail, blendshape, blendshape, base_shape))
        {
            UT_WorkBuffer msg;
            msg.format("Failed to import blendshape '{}'",
                       blendshape.GetPath().GetString());
            HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
            return false;
        }

        all_shape_details.append(gdh);
        all_shape_names.append(name);

        // Import in-between shapes.
        inbetween_names.clear();
        inbetween_weights.clear();
        for (const UsdSkelInbetweenShape &inbetween :
             blendshape.GetInbetweens())
        {
            inbetween_name = inbetween.GetAttr().GetName().GetString();

            // Strip the "inbetweens:" prefix.
            if (!inbetween_name.strncmp(
                    theInbetweensPrefix.c_str(), theInbetweensPrefix.length()))
            {
                inbetween_name.eraseHead(theInbetweensPrefix.length());
            }

            float weight = 0;
            if (!inbetween.GetWeight(&weight))
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING,
                    "Weight is not authored for in-between shape");
                return false;
            }

            GU_DetailHandle inbetween_gdh =
                gdh.duplicateGeometry(GA_DATA_ID_BUMP);
            inbetween_gdh.allocateAndSet(new GU_Detail());
            GU_DetailHandleAutoWriteLock inbetween_detail(inbetween_gdh);

            if (!husdImportBlendShape(
                    *inbetween_detail, inbetween, blendshape, base_shape))
            {
                UT_WorkBuffer msg;
                msg.format("Failed to import in-between '{}' for '{}'",
                           inbetween.GetAttr().GetName().GetString(),
                           blendshape.GetPath().GetString());
                HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
                return false;
            }

            all_shape_names.append(
                path.AppendChild(TfToken(inbetween_name.buffer())).GetString());
            all_shape_details.append(inbetween_gdh);

            inbetween_names.append(all_shape_names.last());
            inbetween_weights.append(weight);
        }

        GU_AgentBlendShapeUtils::addInBetweenShapes(
            *detail, inbetween_names, inbetween_weights);
    }

    // Record the blendshape inputs as detail attributes on the base shape.
    GU_AgentBlendShapeUtils::addInputsToBaseShape(
        base_shape, shape_names, channel_names);

    return true;
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
    parms.myRefineParms = &refine_parms;

    // Convert the shapes to Houdini geometry.
    bool success = GusdForEachSkinnedPrim(
        binding, parms,
        [&binding, &shapes, &root_path](
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
                    parms.myPurpose, primvar_pattern.buffer(),
                    UT_StringHolder::theEmptyString, true,
                    UT_StringHolder::theEmptyString, &geom_bind_xform,
                    parms.myRefineParms))
            {
                return false;
            }

            // Convert to polysoups for reduced memory usage.
            GEO_PolySoupParms psoup_parms;
            gdp->polySoup(psoup_parms, gdp);

            // Set up the boneCapture attribute for deforming shapes.
            if (skinning_query.HasJointInfluences() && !is_static_shape &&
                !GusdCreateCaptureAttribute(
                    *gdp, skinning_query, joint_names, inv_bind_transforms))
            {
                return false;
            }

            // Import blendshape geometry and switch to the correct shape
            // deformer.
            if (skinning_query.HasBlendShapes())
            {
                if (!husdImportAgentBlendShapes(
                            *gdp, shapes[i].myBlendShapeDetails,
                            shapes[i].myBlendShapeNames, skinning_query,
                            root_path))
                {
                    return false;
                }

                shapes[i].myDeformer =
                    skinning_query.HasJointInfluences() ?
                        GU_AgentLayer::getBlendShapeAndSkinDeformer() :
                        GU_AgentLayer::getBlendShapeDeformer();
            }

            return true;
        });

    if (!success)
        return false;

    // Add the shapes to the library and set up the layer's shape bindings.
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

        // Add blendshape inputs to the library.
        const UT_Array<GU_DetailHandle> &input_shapes =
            shapes[i].myBlendShapeDetails;
        const UT_StringArray &input_names = shapes[i].myBlendShapeNames;
        for (exint j = 0; j < input_shapes.size(); ++j)
            shapelib.addShape(input_names[j], input_shapes[j]);
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

static GU_AgentClipPtr
husdImportAgentClip(const GU_AgentRigConstPtr &rig,
                    const UsdSkelSkeletonQuery &skelquery,
                    fpreal64 start_time,
                    fpreal64 end_time,
                    fpreal64 tc_per_s)
{
    if (!skelquery.IsValid())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid skeleton query.");
        return nullptr;
    }

    const UsdSkelSkeleton &skel = skelquery.GetSkeleton();

    // The rig's joint order may be different from the skeleton's joint order.
    VtTokenArray skel_joint_names;
    if (!GusdGetJointNames(skel, skel_joint_names))
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

    const UsdSkelTopology &topology = skelquery.GetTopology();
    const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();

    auto clip = GU_AgentClip::addClip(skel.GetPath().GetName(), rig);

    const exint num_samples = SYSrint(end_time - start_time) + 1;
    clip->setSampleRate(tc_per_s);
    clip->init(num_samples);

    const UT_XformOrder xord(UT_XformOrder::SRT, UT_XformOrder::XYZ);

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
    UT_Vector3F r, s, t;
    for (exint sample_i = 0; sample_i < num_samples; ++sample_i)
    {
        const UsdTimeCode timecode(start_time + sample_i);

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

                xform.explode(xord, r, s, t);
                local_xforms[i].setTransform(t.x(), t.y(), t.z(), r.x(), r.y(),
                                             r.z(), s.x(), s.y(), s.z());
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

/// Determines the frame range and framerate from the stage.
static bool
husdGetFrameRange(HUSD_AutoReadLock &readlock,
                  fpreal64 &start_time,
                  fpreal64 &end_time,
                  fpreal64 &tc_per_s)
{
    HUSD_Info info(readlock);
    start_time = 0;
    end_time = 0;
    tc_per_s = 0;
    info.getTimeCodesPerSecond(tc_per_s);
    if (!info.getStartTimeCode(start_time) || !info.getEndTimeCode(end_time) ||
        SYSisGreater(start_time, end_time))
    {
        HUSD_ErrorScope::addError(
            HUSD_ERR_STRING, "Stage does not specify a valid start time code "
                             "and end time code.");
        return false;
    }

    return true;
}

GU_AgentClipPtr
HUSDimportAgentClip(const GU_AgentRigConstPtr &rig,
                    HUSD_AutoReadLock &readlock,
                    const UT_StringRef &skelrootpath)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return nullptr;

    const UsdSkelBinding &binding = bindings[0];

    fpreal64 start_time = 0;
    fpreal64 end_time = 0;
    fpreal64 tc_per_s = 0;
    if (!husdGetFrameRange(readlock, start_time, end_time, tc_per_s))
        return nullptr;

    return husdImportAgentClip(rig,
                               skelcache.GetSkelQuery(binding.GetSkeleton()),
                               start_time, end_time, tc_per_s);
}

UT_Array<GU_AgentClipPtr>
HUSDimportAgentClips(const GU_AgentRigConstPtr &rig,
                     HUSD_AutoReadLock &readlock,
                     const UT_StringRef &prim_pattern)
{
    HUSD_FindPrims findprims(readlock);

    if (!readlock.data() || !readlock.data()->isStageValid())
        return UT_Array<GU_AgentClipPtr>();

    if (!findprims.addPattern(prim_pattern,
            OP_INVALID_NODE_ID, HUSD_TimeCode()))
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
            auto clip = HUSDimportAgentClip(rig, readlock,
                skelrootpath.GetText());

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
        fpreal64 start_time = 0;
        fpreal64 end_time = 0;
        fpreal64 tc_per_s = 0;
        if (!husdGetFrameRange(readlock, start_time, end_time, tc_per_s))
            return UT_Array<GU_AgentClipPtr>();

        for (const auto &sdfpath : skeletonpaths)
        {
            UsdPrim prim(data->stage()->GetPrimAtPath(sdfpath));
            UT_ASSERT(prim);

            UsdSkelSkeleton skel(prim);
            UT_ASSERT(skel);

            UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);

            auto clip = husdImportAgentClip(rig, skelcache.GetSkelQuery(skel),
                                            start_time, end_time, tc_per_s);
            if (!clip)
                return UT_Array<GU_AgentClipPtr>();

            clips.append(clip);
        }
    }

    return clips;
}
