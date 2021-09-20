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
 *
 */

#include "GEO_FilePrimAgentUtils.h"

#include <HUSD/HUSD_Utils.h>
#include <GU/GU_AgentBlendShapeDeformer.h>
#include <GU/GU_AgentBlendShapeUtils.h>
#include <UT/UT_WorkBuffer.h>
#include <gusd/UT_Gf.h>
#include <pxr/usd/usdSkel/topology.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_AgentPrimTokens, GEO_AGENT_PRIM_TOKENS);

void
GEObuildJointList(const GU_AgentRig &rig, VtTokenArray &joint_paths,
                  UT_Array<exint> &joint_order)
{
    joint_order.setSizeNoInit(rig.transformCount());
    joint_paths.reserve(rig.transformCount());

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
            buf.append(joint_paths[joint_order[parent_idx]].GetString());
            buf.append('/');
        }

        buf.append(rig.transformName(xform_idx));
        joint_paths.push_back(TfToken(buf.toStdString()));
        joint_order[xform_idx] = ordered_idx;
    }

#ifdef UT_DEBUG
    // Validate the hierarchy.
    UsdSkelTopology topo(joint_paths);
    std::string errors;
    if (!topo.Validate(&errors))
        UT_ASSERT_MSG(false, errors.c_str());
#endif
}

VtMatrix4dArray
GEOconvertXformArray(
        const GU_Agent::Matrix4Array &agent_xforms,
        const UT_Array<exint> &joint_order)
{
    VtMatrix4dArray usd_xforms;
    usd_xforms.resize(agent_xforms.entries());

    for (exint i = 0, n = agent_xforms.entries(); i < n; ++i)
    {
        usd_xforms[joint_order[i]] =
            GfMatrix4d(GusdUT_Gf::Cast(agent_xforms[i]));
    }

    return usd_xforms;
}

UT_Array<UT_Matrix4D>
GEOreorderXformArray(
        const GU_Agent::Matrix4Array &agent_xforms,
        const UT_Array<exint> &joint_order)
{
    UT_Array<UT_Matrix4D> xforms;
    xforms.setSizeNoInit(agent_xforms.size());

    for (exint i = 0, n = agent_xforms.entries(); i < n; ++i)
        xforms[joint_order[i]] = agent_xforms[i];

    return xforms;
}

UT_StringArray
GEOfindShapesToImport(const GU_AgentDefinition &defn)
{
    UT_StringArray shape_names;

    const GU_AgentRigConstPtr &rig = defn.rig();
    if (!rig)
        return shape_names;

    const GU_AgentShapeLibConstPtr &shapelib = defn.shapeLibrary();
    if (!shapelib)
        return shape_names;

    UT_ArraySet<exint> blendshape_inputs;
    UT_ArraySet<exint> bound_shapes;

    UT_StringArray inbetween_names;
    GU_AgentBlendShapeUtils::FloatArray inbetween_weights;

    for (const GU_AgentLayerConstPtr &layer : defn.layers())
    {
        for (const GU_AgentLayer::ShapeBinding &binding : *layer)
        {
            bound_shapes.insert(binding.shapeId());

            if (!binding.isDeforming())
                continue;

            if (!dynamic_cast<const GU_AgentBlendShapeDeformer *>(
                        binding.deformer().get()))
            {
                continue;
            }

            GU_DetailHandleAutoReadLock base_shape_gdp(
                    binding.shape()->shapeGeometry(*shapelib));

            GU_AgentBlendShapeUtils::InputCache input_cache;
            if (!input_cache.reset(*base_shape_gdp, *shapelib, rig.get()))
                continue;

            for (exint i = 0, n = input_cache.numInputs(); i < n; ++i)
            {
                auto shape =
                        shapelib->findShape(input_cache.primaryShapeName(i));
                if (!shape)
                    continue;

                blendshape_inputs.insert(shape->uniqueId());

                // Check for any in-between shapes.
                input_cache.getInBetweenShapes(
                        i, inbetween_names, inbetween_weights);
                for (const UT_StringHolder &inbetween_name : inbetween_names)
                {
                    auto shape = shapelib->findShape(inbetween_name);
                    if (shape)
                        blendshape_inputs.insert(shape->uniqueId());
                }
            }
        }
    }

    shape_names.setCapacity(shapelib->entries());
    for (auto &&entry : *shapelib)
    {
        const exint shape_id = entry.second->uniqueId();
        if (blendshape_inputs.contains(shape_id) &&
            !bound_shapes.contains(shape_id))
        {
            continue;
        }

        shape_names.append(entry.first);
    }

    shape_names.sort();
    return shape_names;
}

SdfPath
GEObuildUsdShapePath(const UT_StringHolder &shape_name)
{
    UT_String usd_shape_name(shape_name);
    HUSDmakeValidUsdPath(usd_shape_name, false);
    SdfPath path(usd_shape_name.toStdString());
    return path.MakeRelativePath(SdfPath::AbsoluteRootPath());
}

static bool
geoIsEligibleSkeleton(
        const GT_PrimSkeleton &skeleton,
        const UT_BitArray &skel_pose_mask,
        const GU_Agent::Matrix4Array &bind_pose,
        const UT_BitArray &joint_mask)
{
    for (exint xform_idx : joint_mask)
    {
        if (skel_pose_mask.getBitFast(xform_idx)
            && skeleton.getBindPose()[xform_idx] != bind_pose[xform_idx])
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
geoFindEligibleSkeleton(
        const UT_Array<GT_PrimSkeletonPtr> &skeletons,
        const UT_Array<UT_BitArray> &skel_pose_masks,
        const GU_Agent::Matrix4Array &bind_pose,
        const UT_BitArray &joint_mask)
{
    for (exint i = 0, n = skeletons.entries(); i < n; ++i)
    {
        if (geoIsEligibleSkeleton(
                    *skeletons[i], skel_pose_masks[i], bind_pose, joint_mask))
        {
            return i;
        }
    }

    return -1;
}

void
GEObuildUsdSkeletons(const GU_AgentDefinition &defn,
                     const GU_Agent::Matrix4Array &fallback_bind_pose,
                     const bool import_shapes,
                     UT_Array<GT_PrimSkeletonPtr> &skeletons,
                     UT_Map<exint, exint> &shape_to_skeleton)
{
    UT_ASSERT(defn.rig());
    UT_ASSERT(defn.shapeLibrary());

    const GU_AgentRig &rig = *defn.rig();
    const GU_AgentShapeLib &shapelib = *defn.shapeLibrary();

    UT_BitArray joint_mask(rig.transformCount());
    UT_Array<exint> static_shapes;

    // Tracks the indices in each skeleton's bind pose that are used by any
    // meshes that reference the skeleton. Used for determining which shapes
    // can use the same Skeleton prim.
    UT_Array<UT_BitArray> skel_pose_masks;

    for (auto &&entry : shapelib)
    {
        const GU_AgentShapeLib::ShapePtr &shape = entry.second;

        const GU_LinearSkinDeformerSourceWeights &source_weights
                = shape->getLinearSkinDeformerSourceWeights(shapelib);
        if (!source_weights.numRegions())
        {
            static_shapes.append(shape->uniqueId());
            continue;
        }

        // Build a bind pose for the skeleton. The capture weights might
        // only reference a subset of the joints.
        GU_Agent::Matrix4Array bind_pose;
        bind_pose.appendMultiple(
                GU_Agent::Matrix4Type::getIdentityMatrix(),
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

        exint skel_idx = geoFindEligibleSkeleton(
                skeletons, skel_pose_masks, bind_pose, joint_mask);
        if (skel_idx >= 0)
        {
            // If this shape can safely share an existing skeleton, update
            // the bind pose with the joints referenced by this shape.
            GT_PrimSkeleton &skeleton = *skeletons[skel_idx];
            for (exint xform_idx : joint_mask)
                skeleton.getBindPose()[xform_idx] = bind_pose[xform_idx];

            skel_pose_masks[skel_idx] |= joint_mask;
        }
        else
        {
            // Otherwise, set up a new Skeleton prim.
            skel_idx = skeletons.entries();

            skeletons.append(UTmakeIntrusive<GT_PrimSkeleton>(rig, bind_pose));
            skel_pose_masks.append(joint_mask);
        }

        shape_to_skeleton[shape->uniqueId()] = skel_idx;
    }

    // Only need one skeleton if we're not importing the geometry. However, we
    // can still go through the above code path to build a reasonable bind pose
    // from the shapes.
    if (!import_shapes && skeletons.entries() > 0)
        skeletons.setSize(1);

    // Ensure there is a skeleton (with a default bind pose) if there aren't
    // any deforming shapes.
    if (skeletons.entries() == 0)
    {
        joint_mask.setAllBits(true);
        skeletons.append(
                UTmakeIntrusive<GT_PrimSkeleton>(rig, fallback_bind_pose));
    }

    // Shapes without skinning weights can use any skeleton, since they don't
    // rely on the bind pose.
    UT_ASSERT(skeletons.entries());
    for (exint shape_id : static_shapes)
        shape_to_skeleton[shape_id] = 0;
}

GT_PrimAgentDefinition::GT_PrimAgentDefinition(
        const GU_AgentDefinitionConstPtr &defn,
        const SdfPath &path,
        const UT_Array<GT_PrimSkeletonPtr> &skeletons,
        const UT_Map<exint, exint> &shape_to_skel)
    : myDefinition(defn)
    , myPath(path)
    , mySkeletons(skeletons)
    , myShapeToSkel(shape_to_skel)
{
}

int
GT_PrimAgentDefinition::getStaticPrimitiveType()
{
    static const int thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
    return thePrimitiveType;
}

GT_PrimAgentInstance::GT_PrimAgentInstance(
        const GU_ConstDetailHandle &detail,
        const GU_Agent *agent,
        const GT_AttributeListHandle &attribs)
    : myDetail(detail), myAgent(agent), myAttributeList(attribs)
{
}

int
GT_PrimAgentInstance::getStaticPrimitiveType()
{
    static const int thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
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

GT_PrimSkeleton::GT_PrimSkeleton(
        const GU_AgentRig &rig,
        const GU_Agent::Matrix4Array &bind_pose)
    : myBindPose(bind_pose)
{
    // Build the skeleton's joint list, which expresses the hierarchy through
    // the joint names and must be ordered so that parents appear before
    // children (unlike GU_AgentRig).
    GEObuildJointList(rig, myJointPaths, myJointOrder);

    // Also record the original unique joint names from GU_AgentRig.
    // These can be used instead of the full paths when importing into another
    // format (e.g. back to SOPs).
    myJointNames.resize(rig.transformCount());
    for (exint i = 0, n = rig.transformCount(); i < n; ++i)
    {
        myJointNames[myJointOrder[i]]
                = TfToken(rig.transformName(i).toStdString());
    }
}

int
GT_PrimSkeleton::getStaticPrimitiveType()
{
    static const int thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
    return thePrimitiveType;
}

GT_PrimSkelAnimation::GT_PrimSkelAnimation(
        const GU_Agent *agent,
        const GT_PrimSkeletonPtr &skel)
    : myAgent(agent), mySkelPrim(skel)
{
}

int
GT_PrimSkelAnimation::getStaticPrimitiveType()
{
    static const int thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
    return thePrimitiveType;
}

PXR_NAMESPACE_CLOSE_SCOPE
