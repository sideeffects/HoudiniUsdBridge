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

#ifndef __GEO_FilePrimAgentUtils_h__
#define __GEO_FilePrimAgentUtils_h__

#include "GEO_FileUtils.h"

#include <GT/GT_Primitive.h>
#include <GU/GU_Agent.h>
#include <GU/GU_AgentDefinition.h>
#include <GU/GU_AgentRig.h>
#include <GU/GU_DetailHandle.h>
#include <pxr/pxr.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

#define GEO_AGENT_PRIM_TOKENS  \
    ((agentdefinitions, "agentdefinitions")) \
    ((animation, "animation")) \
    ((geometry, "geometry")) \
    ((layers, "layers")) \
    ((skeleton,	"skeleton")) \
    ((shapelibrary, "shapelibrary"))

TF_DECLARE_PUBLIC_TOKENS(GEO_AgentPrimTokens, GEO_AGENT_PRIM_TOKENS);

/// Build a list of the joint names in the format required by UsdSkel (i.e.
/// full paths such as "A/B/C"), and ordered so that parents appear before
/// children.
/// This will also replace any characters that are not valid for an SdfPath.
void GEObuildJointList(const GU_AgentRig &rig, VtTokenArray &joint_paths,
                       UT_Array<exint> &joint_order);

/// Convert a list of joint transforms from GU_Agent::Matrix4Type to
/// GfMatrix4d, and switch to the USD joint order.
VtMatrix4dArray GEOconvertXformArray(
        const GU_Agent::Matrix4Array &agent_xforms,
        const UT_Array<exint> &joint_order);

/// Convert a list of joint transforms from GU_Agent::Matrix4Type to
/// UT_Matrix4D, and switch to the USD joint order.
UT_Array<UT_Matrix4D> GEOreorderXformArray(
        const GU_Agent::Matrix4Array &agent_xforms,
        const UT_Array<exint> &joint_order);

/// Builds a list of the shapes to import from the agent definition's shape
/// library. Shapes that are only used as blendshape inputs are omitted to
/// avoid redundant data being generated, since the blendshape inputs have
/// special handling to convert them to BlendShape prims attached to the base
/// shape's mesh.
UT_StringArray GEOfindShapesToImport(const GU_AgentDefinition &defn);

/// Build a valid USD path for the shape name, which can be appended to the
/// root prim of the shape library.
SdfPath GEObuildUsdShapePath(const UT_StringHolder &shape_name);

/// Represents a USD skeleton, with an agent's rig as the source.
class GT_PrimSkeleton : public GT_Primitive
{
public:
    GT_PrimSkeleton(
            const GU_AgentRig &rig,
            const GU_Agent::Matrix4Array &bind_pose,
            const GU_Agent::Matrix4Array &rest_pose);

    const VtTokenArray &getJointPaths() const { return myJointPaths; }
    const VtTokenArray &getJointNames() const { return myJointNames; }

    /// Maps the agent's joint order to the USD joint order.
    const UT_Array<exint> &getJointOrder() const { return myJointOrder; }

    /// @{
    /// The bind pose is stored in the order of the agent's rig. Use
    /// getJointOrder() for remapping to the USD joint order.
    const GU_Agent::Matrix4Array &getBindPose() const { return myBindPose; }
    GU_Agent::Matrix4Array &getBindPose() { return myBindPose; }
    /// @}

    /// The rest pose is stored in the order of the agent's rig. Use
    /// getJointOrder() for remapping to the USD joint order.
    /// These transforms are in local space.
    const GU_Agent::Matrix4Array &getRestPose() const { return myRestPose; }

    /// @{
    /// The path of the USD skeleton prim.
    const GEO_PathHandle &getPath() const { return myPath; }
    void setPath(const GEO_PathHandle &path) { myPath = path; }
    /// @}

    /// @{
    /// Optional path to a SkelAnimation prim that is the skeleton's animation
    /// source. This is only used for non-instanced import modes (for
    /// instancing, the animation binding is done on the skeleton instance).
    const GEO_PathHandle &getAnimPath() const { return myAnimPath; }
    void setAnimPath(const GEO_PathHandle &path) { myAnimPath = path; }
    /// @}

    static int getStaticPrimitiveType();

    int getPrimitiveType() const override
    {
        return getStaticPrimitiveType();
    }

    const char *className() const override
    {
        return "GT_PrimSkeleton";
    }

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override {}

    int getMotionSegments() const override { return 1; }

    int64 getMemoryUsage() const override { return sizeof(*this); }

    GT_PrimitiveHandle doSoftCopy() const override
    {
        return new GT_PrimSkeleton(*this);
    }

private:
    GEO_PathHandle myPath;
    GEO_PathHandle myAnimPath;
    VtTokenArray myJointPaths;
    VtTokenArray myJointNames;
    UT_Array<exint> myJointOrder;
    GU_Agent::Matrix4Array myBindPose;
    GU_Agent::Matrix4Array myRestPose;
};

using GT_PrimSkeletonPtr = UT_IntrusivePtr<GT_PrimSkeleton>;

/// Represents a USD SkelAnimation prim, with an agent's pose as the source.
class GT_PrimSkelAnimation : public GT_Primitive
{
public:
    GT_PrimSkelAnimation(const GU_Agent *agent, const GT_PrimSkeletonPtr &skel);

    /// @{
    /// The path to the USD animation prim.
    const GEO_PathHandle &getPath() const { return myPath; }
    void setPath(const GEO_PathHandle &path) { myPath = path; }
    /// @}

    /// The USD Skeleton that the animation is associated with.
    const GT_PrimSkeletonPtr &getSkelPrim() const { return mySkelPrim; }

    /// The source agent primitive.
    const GU_Agent &getAgent() const { return *myAgent; }

    static int getStaticPrimitiveType();
    int getPrimitiveType() const override { return getStaticPrimitiveType(); }
    const char *className() const override { return "GT_PrimSkelAnimation"; }

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override {}
    int getMotionSegments() const override { return 1; }

    int64 getMemoryUsage() const override { return sizeof(*this); }

    GT_PrimitiveHandle doSoftCopy() const override
    {
        return new GT_PrimSkelAnimation(*this);
    }

private:
    const GU_Agent *myAgent;
    GT_PrimSkeletonPtr mySkelPrim;
    GEO_PathHandle myPath;
};

/// Represents an agent definition, which for USD will have child prims
/// containing the skeleton(s), shapes, etc.
class GT_PrimAgentDefinition : public GT_Primitive
{
public:
    GT_PrimAgentDefinition(
        const GU_AgentDefinitionConstPtr &defn,
        const SdfPath &path,
        const UT_Array<GT_PrimSkeletonPtr> &skeletons,
        const UT_Map<exint, exint> &shape_to_skel);

    const GU_AgentDefinition &getDefinition() const { return *myDefinition; }
    const SdfPath &getPath() const { return myPath; }

    /// The USD skeletons used by the agent definition.
    const UT_Array<GT_PrimSkeletonPtr> &getSkeletons() const
    {
        return mySkeletons;
    }

    /// Maps from the shape's id to the index of the skeleton prim it requires.
    const UT_Map<exint, exint> &getShapeToSkelMap() const
    {
        return myShapeToSkel;
    }

    static int getStaticPrimitiveType();

    int getPrimitiveType() const override
    {
        return getStaticPrimitiveType();
    }

    const char *className() const override
    {
        return "GT_PrimAgentDefinition";
    }

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override
    {
    }

    int getMotionSegments() const override { return 1; }

    int64 getMemoryUsage() const override { return sizeof(*this); }

    GT_PrimitiveHandle doSoftCopy() const override
    {
        return new GT_PrimAgentDefinition(*this);
    }

private:
    GU_AgentDefinitionConstPtr myDefinition;
    SdfPath myPath;
    UT_Array<GT_PrimSkeletonPtr> mySkeletons;
    UT_Map<exint, exint> myShapeToSkel;
};

using GT_PrimAgentDefinitionPtr = UT_IntrusivePtr<GT_PrimAgentDefinition>;

/// Represents an instance of an agent primitive, which references an agent
/// definition at the specified path in the hierarchy.
class GT_PrimAgentInstance : public GT_Primitive
{
public:
    GT_PrimAgentInstance(
            const GU_ConstDetailHandle &detail,
            const GU_Agent *agent,
            const GT_AttributeListHandle &attribs);

    const GU_Agent &getAgent() const { return *myAgent; }

    /// @{
    /// Pointer to the agent definition prim, if the agent is imported as an
    /// instance.
    const GT_PrimAgentDefinitionPtr &getDefinitionPrim() const
    {
        return myDefnPrim;
    }
    void setDefinitionPrim(const GT_PrimAgentDefinitionPtr &prim)
    {
        myDefnPrim = prim;
    }
    /// @}

    /// @{
    /// Path to the animation prim.
    const GEO_PathHandle &getAnimPath() const { return myAnimPath; }
    void setAnimPath(const GEO_PathHandle &path) { myAnimPath = path; }
    /// @}

    static int getStaticPrimitiveType();

    int getPrimitiveType() const override
    {
        return getStaticPrimitiveType();
    }

    const GT_AttributeListHandle &getDetailAttributes() const override
    {
        return myAttributeList;
    }

    const char *className() const override
    {
        return "GT_PrimAgentInstance";
    }

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int getMotionSegments() const override { return 1; }

    int64 getMemoryUsage() const override { return sizeof(*this); }

    GT_PrimitiveHandle doSoftCopy() const override
    {
        return new GT_PrimAgentInstance(*this);
    }

private:
    GU_ConstDetailHandle myDetail;
    const GU_Agent *myAgent;
    GT_PrimAgentDefinitionPtr myDefnPrim;
    GEO_PathHandle myAnimPath;
    GT_AttributeListHandle myAttributeList;
};

/// Determine how many unique skeletons are needed for the shapes in the agent
/// definition, and record which skeleton is needed for each shape id.
/// Per-mesh bind poses aren't supported, so it might be required to have
/// multiple skeleton prims per agent definition.
/// In the case where there aren't any deforming shapes, the provided bind pose
/// is used for the single skeleton prim.
void GEObuildUsdSkeletons(const GU_AgentDefinition &defn,
                          const GU_Agent::Matrix4Array &fallback_bind_pose,
                          bool import_shapes,
                          UT_Array<GT_PrimSkeletonPtr> &skeletons,
                          UT_Map<exint, exint> &shape_to_skeleton);

/// Tracks information about the source agent shape when refining an entry in
/// the shape library.
struct GEO_AgentShapeInfo : public UT_IntrusiveRefCounter<GEO_AgentShapeInfo>
{
    GEO_AgentShapeInfo(
            const GU_AgentDefinitionConstPtr &defn,
            const UT_StringHolder &shape_name,
            const GT_PrimSkeletonPtr &skel,
            const GU_AgentLayer::ShapeBinding *binding)
        : myDefinition(defn)
        , myShapeName(shape_name)
        , mySkeleton(skel)
        , myBinding(binding)
    {
    }

    GU_AgentDefinitionConstPtr myDefinition;
    UT_StringHolder myShapeName;
    GT_PrimSkeletonPtr mySkeleton;
    /// Optional shape binding - when in GEO_AGENT_SKELROOTS mode, the shape is
    /// not separately instanced by a layer,  and needs to be bound to the
    /// correct skeleton (and joint, for rigid shapes).
    const GU_AgentLayer::ShapeBinding *myBinding;
};

using GEO_AgentShapeInfoPtr = UT_IntrusivePtr<GEO_AgentShapeInfo>;

PXR_NAMESPACE_CLOSE_SCOPE

#endif
