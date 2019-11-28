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
void GEObuildJointList(const GU_AgentRig &rig, VtTokenArray &joint_paths,
                       UT_Array<exint> &joint_order);

/// Convert a list of joint transforms from GU_Agent::Matrix4Type to
/// GfMatrix4d, and switch to the USD joint order.
VtMatrix4dArray GEOconvertXformArray(const GU_AgentRig &rig,
                                     const GU_Agent::Matrix4Array &agent_xforms,
                                     const UT_Array<exint> &joint_order);

/// Tracks information about the source agent shape when refining an entry in
/// the shape library.
struct GEO_AgentShapeInfo : public UT_IntrusiveRefCounter<GEO_AgentShapeInfo>
{
    GEO_AgentShapeInfo() = default;

    GEO_AgentShapeInfo(const GU_AgentDefinitionConstPtr &defn,
                       const UT_StringHolder &shape_name)
        : myDefinition(defn), myShapeName(shape_name)
    {
    }

    explicit operator bool() const { return myDefinition != nullptr; }

    GU_AgentDefinitionConstPtr myDefinition;
    UT_StringHolder myShapeName;
};

/// Build a valid USD name for each shape, indexed by the shape's unique id.
void GEObuildUsdShapeNames(const GU_AgentShapeLib &shapelib,
                           UT_Map<exint, TfToken> &usd_shape_names);

/// Stores the name and bind pose to be used for a USD Skeleton prim. Per-mesh
/// bind poses aren't supported, so for now we can produce multiple skeleton
/// prims per agent definition.
struct GEO_AgentSkeleton
{
    GEO_AgentSkeleton(const TfToken &name,
                      const GU_Agent::Matrix4Array &bind_pose,
                      const UT_BitArray &mask)
        : myName(name), myBindPose(bind_pose), myMask(mask)
    {
    }

    TfToken myName;
    GU_Agent::Matrix4Array myBindPose;
    /// Tracks the indices in myBindPose that are used by any meshes that
    /// reference this skeleton. Used for determining which shapes can use the
    /// same Skeleton prim.
    UT_BitArray myMask;
};

/// Determine how many unique skeletons are needed for the shapes in the agent
/// definition, and record which skeleton is needed for each shape id.
/// In the case where there aren't any deforming shapes, the provided bind pose
/// is used for the single skeleton prim.
void GEObuildUsdSkeletons(const GU_AgentDefinition &defn,
                          const GU_Agent::Matrix4Array &fallback_bind_pose,
                          UT_Array<GEO_AgentSkeleton> &skeletons,
                          UT_Map<exint, exint> &shape_to_skeleton);

/// Represents an agent definition, which for USD will have child prims
/// containing the skeleton(s), shapes, etc.
///
/// GU_AgentRig doesn't contain a bind pose or rest pose, so the caller should
/// provide a bind pose to be used if e.g. there aren't any deforming shapes. A
/// bind pose is needed for imaging skeleton primitives.
class GT_PrimAgentDefinition : public GT_Primitive
{
public:
    GT_PrimAgentDefinition(
        const GU_AgentDefinitionConstPtr &defn,
        const GU_Agent::Matrix4ArrayConstPtr &fallback_bind_pose);

    const GU_AgentDefinition &getDefinition() const { return *myDefinition; }
    const GU_Agent::Matrix4ArrayConstPtr &getFallbackBindPose() const
    {
        return myFallbackBindPose;
    }

    static int getStaticPrimitiveType();

    virtual int getPrimitiveType() const override
    {
        return getStaticPrimitiveType();
    }

    virtual const char *className() const override
    {
        return "GT_PrimAgentDefinition";
    }

    virtual void
    enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override
    {
    }

    virtual int getMotionSegments() const override { return 1; }

    virtual int64 getMemoryUsage() const override { return sizeof(*this); }

    virtual GT_PrimitiveHandle doSoftCopy() const override
    {
        return new GT_PrimAgentDefinition(*this);
    }

private:
    GU_AgentDefinitionConstPtr myDefinition;
    GU_Agent::Matrix4ArrayConstPtr myFallbackBindPose;
    static int thePrimitiveType;
};

/// Represents an instance of an agent primitive, which references an agent
/// definition at the specified path in the hierarchy.
class GT_PrimAgentInstance : public GT_Primitive
{
public:
    GT_PrimAgentInstance(const GU_ConstDetailHandle &detail,
                         const GU_Agent *agent, const SdfPath &definition_path,
                         const GT_AttributeListHandle &attribs);

    const GU_Agent &getAgent() const { return *myAgent; }
    const SdfPath &getDefinitionPath() const { return myDefinitionPath; }

    static int getStaticPrimitiveType();

    virtual int getPrimitiveType() const override
    {
        return getStaticPrimitiveType();
    }

    virtual const GT_AttributeListHandle &getDetailAttributes() const override
    {
        return myAttributeList;
    }

    virtual const char *className() const override
    {
        return "GT_PrimAgentInstance";
    }

    virtual void
    enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    virtual int getMotionSegments() const override { return 1; }

    virtual int64 getMemoryUsage() const override { return sizeof(*this); }

    virtual GT_PrimitiveHandle doSoftCopy() const override
    {
        return new GT_PrimAgentInstance(*this);
    }

private:
    GU_ConstDetailHandle myDetail;
    const GU_Agent *myAgent;
    SdfPath myDefinitionPath;
    GT_AttributeListHandle myAttributeList;
    static int thePrimitiveType;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
