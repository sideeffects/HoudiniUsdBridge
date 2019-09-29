/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 */

#include "GEO_FilePrimAgentUtils.h"

#include <HUSD/HUSD_Utils.h>
#include <HUSD/XUSD_Format.h>
#include <UT/UT_WorkBuffer.h>
#include <gusd/UT_Gf.h>
#include <pxr/usd/usdSkel/topology.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_AgentPrimTokens, GEO_AGENT_PRIM_TOKENS);

void
GEObuildJointList(const GU_AgentRig &rig, VtTokenArray &joint_list,
                  UT_Array<exint> &joint_order)
{
    joint_order.setSizeNoInit(rig.transformCount());
    joint_list.reserve(rig.transformCount());

    UT_WorkBuffer buf;
    exint ordered_idx = 0;
    for (GU_AgentRig::const_iterator it(rig); !it.atEnd();
         it.advance(), ++ordered_idx)
    {
        buf.clear();

        const exint xform_idx = *it;
        const exint parent_idx = rig.parentIndex(xform_idx);
        if (parent_idx >= 0)
        {
            buf.append(joint_list[joint_order[parent_idx]].GetString());
            buf.append('/');
        }

        buf.append(rig.transformName(xform_idx));
        joint_list.push_back(TfToken(buf.toStdString()));
        joint_order[xform_idx] = ordered_idx;
    }

#ifdef UT_DEBUG
    // Validate the hierarchy.
    UsdSkelTopology topo(joint_list);
    std::string errors;
    if (!topo.Validate(&errors))
        UT_ASSERT_MSG(false, errors.c_str());
#endif
}

VtMatrix4dArray
GEOconvertXformArray(const GU_AgentRig &rig,
                     const GU_Agent::Matrix4Array &agent_xforms,
                     const UT_Array<exint> &joint_order)
{
    VtMatrix4dArray usd_xforms;
    usd_xforms.resize(agent_xforms.entries());

    for (exint i = 0, n = rig.transformCount(); i < n; ++i)
    {
        usd_xforms[joint_order[i]] =
            GfMatrix4d(GusdUT_Gf::Cast(agent_xforms[i]));
    }

    return usd_xforms;
}

void
GEObuildUsdShapeNames(const GU_AgentShapeLib &shapelib,
                      UT_Map<exint, TfToken> &usd_shape_names)
{
    for (auto &&entry: shapelib)
    {
        const UT_StringHolder &shape_name = entry.first;
        const GU_AgentShapeLib::ShapePtr &shape = entry.second;

        UT_String usd_shape_name(shape_name);
        HUSDmakeValidUsdName(usd_shape_name, false);
        usd_shape_names[shape->uniqueId()] = TfToken(usd_shape_name);
    }
}

static bool
geoIsEligibleSkeleton(const GEO_AgentSkeleton &skeleton,
                      const GU_Agent::Matrix4Array &bind_pose,
                      const UT_BitArray &joint_mask)
{
    for (exint xform_idx : joint_mask)
    {
        if (skeleton.myMask.getBitFast(xform_idx) &&
            skeleton.myBindPose[xform_idx] != bind_pose[xform_idx])
        {
            return false;
        }
    }

    return true;
}

/// Finds an existing skeleton where the bind pose matches for all joints that
/// are shared with the query shape. Shapes with disjoint bind poses can
/// trivially share the same skeleton.
static exint
geoFindEligibleSkeleton(const UT_Array<GEO_AgentSkeleton> &skeletons,
                        const GU_Agent::Matrix4Array &bind_pose,
                        const UT_BitArray &joint_mask)
{
    for (exint i = 0, n = skeletons.entries(); i < n; ++i)
    {
        const GEO_AgentSkeleton &skeleton = skeletons[i];
        if (geoIsEligibleSkeleton(skeleton, bind_pose, joint_mask))
            return i;
    }

    return -1;
}

void
GEObuildUsdSkeletons(const GU_AgentDefinition &defn,
                     const GU_Agent::Matrix4Array &fallback_bind_pose,
                     UT_Array<GEO_AgentSkeleton> &skeletons,
                     UT_Map<exint, exint> &shape_to_skeleton)
{
    UT_ASSERT(defn.rig());
    UT_ASSERT(defn.shapeLibrary());

    const GU_AgentRig &rig = *defn.rig();
    const GU_AgentShapeLib &shapelib = *defn.shapeLibrary();

    UT_BitArray joint_mask(rig.transformCount());
    UT_Array<exint> static_shapes;
    for (auto &&entry : shapelib)
    {
        const GU_AgentShapeLib::ShapePtr &shape = entry.second;

        const GU_LinearSkinDeformerSourceWeights &source_weights =
            shape->getLinearSkinDeformerSourceWeights(shapelib);
        if (!source_weights.numRegions())
        {
            static_shapes.append(shape->uniqueId());
            continue;
        }

        // Build a bind pose for the skeleton. The capture weights might
        // only reference a subset of the joints.
        GU_Agent::Matrix4Array bind_pose;
        bind_pose.appendMultiple(GU_Agent::Matrix4Type::getIdentityMatrix(),
                                 rig.transformCount());

        joint_mask.setAllBits(false);
        for (int i = 0, n = source_weights.numRegions(); i < n; ++i)
        {
            // Ignore regions that aren't referenced by any points.
            if (!source_weights.usesRegion(i))
                continue;

            exint xform_idx = rig.findTransform(source_weights.regionName(i));
            UT_ASSERT(xform_idx >= 0);
            if (xform_idx < 0)
                continue;

            // The capture attribute stores the inverse world transform,
            // whereas USD stores the world transform.
            UT_Matrix4F xform = source_weights.regionXform(i);
            xform.invert();
            bind_pose[xform_idx] = xform;
            joint_mask.setBitFast(xform_idx, true);
        }

        exint skel_idx =
            geoFindEligibleSkeleton(skeletons, bind_pose, joint_mask);
        if (skel_idx >= 0)
        {
            // If this shape can safely share an existing skeleton, update
            // the bind pose with the joints referenced by this shape.
            GEO_AgentSkeleton &skeleton = skeletons[skel_idx];
            for (exint xform_idx : joint_mask)
                skeleton.myBindPose[xform_idx] = bind_pose[xform_idx];

            skeleton.myMask |= joint_mask;
        }
        else
        {
            // Otherwise, set up a new Skeleton prim.
            skel_idx = skeletons.entries();

            TfToken skel_name;
            if (skel_idx == 0)
                skel_name = GEO_AgentPrimTokens->skeleton;
            else
            {
                UT_WorkBuffer buf;
                buf.format("{}_{}", GEO_AgentPrimTokens->skeleton,
                           skel_idx + 1);
                skel_name = TfToken(buf.buffer());
            }

            skeletons.append(
                GEO_AgentSkeleton(skel_name, bind_pose, joint_mask));
        }

        shape_to_skeleton[shape->uniqueId()] = skel_idx;
    }

    // Ensure there is a skeleton (with a default bind pose) if there aren't
    // any deforming shapes.
    if (skeletons.entries() == 0)
    {
        joint_mask.setAllBits(true);
        skeletons.append(GEO_AgentSkeleton(GEO_AgentPrimTokens->skeleton,
                                           fallback_bind_pose, joint_mask));
    }

    // Shapes without skinning weights can use any skeleton, since they don't
    // rely on the bind pose.
    UT_ASSERT(skeletons.entries());
    for (exint shape_id : static_shapes)
        shape_to_skeleton[shape_id] = 0;
}

int GT_PrimAgentDefinition::thePrimitiveType = GT_PRIM_UNDEFINED;

GT_PrimAgentDefinition::GT_PrimAgentDefinition(
    const GU_AgentDefinitionConstPtr &defn,
    const GU_Agent::Matrix4ArrayConstPtr &fallback_bind_pose)
    : myDefinition(defn), myFallbackBindPose(fallback_bind_pose)
{
}

int
GT_PrimAgentDefinition::getStaticPrimitiveType()
{
    if (thePrimitiveType == GT_PRIM_UNDEFINED)
        thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
    return thePrimitiveType;
}

int GT_PrimAgentInstance::thePrimitiveType = GT_PRIM_UNDEFINED;

GT_PrimAgentInstance::GT_PrimAgentInstance(
    const GU_ConstDetailHandle &detail, const GU_Agent *agent,
    const SdfPath &definition_path, const GT_AttributeListHandle &attribs)
    : myDetail(detail),
      myAgent(agent),
      myDefinitionPath(definition_path),
      myAttributeList(attribs)
{
}

int
GT_PrimAgentInstance::getStaticPrimitiveType()
{
    if (thePrimitiveType == GT_PRIM_UNDEFINED)
        thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
    return thePrimitiveType;
}

void
GT_PrimAgentInstance::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_BoundingBox box;
    if (myAgent->getBounds(box))
    {
        for (int i = 0; i < nsegments; ++i)
            boxes[i].enlargeBounds(box);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
