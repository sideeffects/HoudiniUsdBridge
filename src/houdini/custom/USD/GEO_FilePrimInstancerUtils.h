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

#ifndef __GEO_FilePrimInstancerUtils_h__
#define __GEO_FilePrimInstancerUtils_h__

#include "GEO_Boost.h"
#include "GEO_FileUtils.h"
#include <UT/UT_Map.h>
#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_Primitive.h>
#include <SYS/SYS_Hash.h>
#include BOOST_HEADER(variant.hpp)
#include <pxr/pxr.h>
#include <pxr/base/vt/types.h>
#include <pxr/usd/sdf/path.h>

class GU_PackedImpl;

PXR_NAMESPACE_OPEN_SCOPE

#define GEO_POINTINSTANCER_PRIM_TOKENS  \
    ((instances, "instances")) \
    ((Prototypes, "Prototypes"))

TF_DECLARE_PUBLIC_TOKENS(GEO_PointInstancerPrimTokens,
                         GEO_POINTINSTANCER_PRIM_TOKENS);

/// Decompose into translates / rotates / scales for the PointInstancer schema.
void GEOdecomposeTransforms(const UT_Array<UT_Matrix4D> &xforms,
                            VtVec3fArray &positions, VtQuathArray &orientations,
                            VtVec3fArray &scales);

/// Packed fragment instances can be identified by the attribute name and
/// value.
struct GT_PackedFragmentId
{
    GT_PackedFragmentId(exint geometry_id, const UT_StringHolder &attrib_name,
                        const UT_StringHolder &attrib_value);

    bool operator==(const GT_PackedFragmentId &other) const;
    size_t hash() const;

    /// For unordered_map.
    friend size_t hash_value(const GT_PackedFragmentId &id)
    {
        return id.hash();
    }

    exint myGeometryId;
    UT_StringHolder myAttribName;
    UT_StringHolder myAttribValue;
};

using GT_PackedGeometryId = exint;
using GT_PackedDiskId = UT_StringHolder;

/// Instances are either identified by a GU_Detail unique id (plus
/// additional data for packed fragments) or by file path (so that packed
/// disk primitives aren't forced to be loaded).
using GT_PackedInstanceKey =
    BOOST_NS::variant<GT_PackedGeometryId, GT_PackedDiskId,
                      GT_PackedFragmentId>;

/// Key for packed primitives which cannot be identified as instances.
extern const GT_PackedInstanceKey GTnotInstancedKey;

/// Returns the instance key for the packed primitive.
GT_PackedInstanceKey
GTpackedInstanceKey(const GT_GEOPrimPacked &prototype_prim);

/// GT equivalent to UsdGeomPointInstancer. Stores a set of references to the
/// prototype primitives, along with the point data (prototype id, transform,
/// attributes, etc).
class GT_PrimPointInstancer : public GT_Primitive
{
public:
    GT_PrimPointInstancer() = default;

    /// @{
    /// The path to the point instancer prim.
    const GEO_PathHandle &getPath() const { return myPath; }
    void setPath(const GEO_PathHandle &path) { myPath = path; }
    /// @}

    /// Returns the packed prims's prototype index, or -1 if it has not been
    /// registered.
    int findPrototype(const GT_GEOPrimPacked &prototype_prim) const;
    /// Registers a prototype with the given packed primitive. Typically this
    /// will be a child of the instancer prim.
    int addPrototype(const GT_GEOPrimPacked &prototype_prim,
                     const GEO_PathHandle &path);
    /// Returns the list of prototypes.
    SdfPathVector getPrototypePaths() const;

    /// Adds a list of instances of the specified prototype.
    /// Call finishAddingInstances() when all instances have been added.
    void addInstances(int proto_index, const GT_TransformArray &xforms,
                      const UT_Array<exint> &invisible_instances,
                      const GT_AttributeListHandle &instance_attribs,
                      const GT_AttributeListHandle &detail_attribs);

    /// Build the concatenated attribute lists (GA_DAList) after adding all
    /// instances.
    void finishAddingInstances();

    const UT_Array<int> &getProtoIndices() const { return myProtoIndices; }
    const UT_Array<UT_Matrix4D> &getInstanceXforms() const
    {
        return myInstanceXforms;
    }

    /// List of the indices of any invisible instances. Note that these are the
    /// point numbers, not ids, so some extra work is required to author the
    /// invisibleIds attribute when ids are also being authored.
    const UT_Array<exint> &getInvisibleInstances() const
    {
        return myInvisibleInstances;
    }

    const GT_AttributeListHandle &getPointAttributes() const override
    {
        // Primvars with a value per instance should have vertex / varying /
        // faceVarying interpolation.
        return myInstanceAttribs;
    }
    const GT_AttributeListHandle &getDetailAttributes() const override
    {
        return myDetailAttribs;
    }

    static int getStaticPrimitiveType();
    int getPrimitiveType() const override
    {
        return getStaticPrimitiveType();
    }

    const char *className() const override
    {
        return "GT_PrimPointInstancer";
    }

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override
    {
    }

    int getMotionSegments() const override { return 1; }

    int64 getMemoryUsage() const override { return sizeof(*this); }

    GT_PrimitiveHandle doSoftCopy() const override
    {
        return new GT_PrimPointInstancer(*this);
    }

private:
    GEO_PathHandle myPath;
    UT_Array<GEO_PathHandle> myPrototypePaths;
    /// Map from GA_Detail::uniqueId() to index in myPrototypePaths.
    UT_Map<GT_PackedInstanceKey, int> myPrototypeIndex;

    UT_Array<UT_Matrix4D> myInstanceXforms;
    UT_Array<int> myProtoIndices;
    UT_Array<exint> myInvisibleInstances;
    UT_Array<GT_AttributeListHandle> myInstanceAttribLists;
    GT_AttributeListHandle myInstanceAttribs;
    GT_AttributeListHandle myDetailAttribs;

    static int thePrimitiveType;
};

/// Represents an instance of a packed primitive.
/// The USD representation can have several different forms:
/// - Xform with the geometry unpacked underneath
///   - With the prim's xform & attribs (packed geometry, no instancing)
///   - No xform or attribs, defining the prototype for use with native
///     instancing.
/// - Xform prim with payload (from packed disk prim) and the prim's xform &
///   attribs
/// - Xform prim with a reference to the prototype Xform prim (packed geometry
///   prim, native instancing) and the prim's xform & attribs
class GT_PrimPackedInstance : public GT_Primitive
{
public:
    GT_PrimPackedInstance(
        const UT_IntrusivePtr<const GT_GEOPrimPacked> &packed_prim,
        const GT_TransformHandle &xform = GT_Transform::identity(),
        const GT_AttributeListHandle &attribs = GT_AttributeListHandle(),
        bool visible = true);

    /// @{
    /// Optional path to the prototype prim that should be instanced.
    const SdfPath &getPrototypePath() const
    {
        return myPrototypePath ? *myPrototypePath : SdfPath::EmptyPath();
    }
    void setPrototypePath(const GEO_PathHandle &path)
    {
        myPrototypePath = path;
    }
    /// @}

    /// Returns the packed primitive's impl.
    const GU_PackedImpl *getPackedImpl() const;

    /// @{
    /// Whether this prim is defining the shared prototype for native
    /// instancing.
    bool isPrototype() const { return myIsPrototype; }
    void setIsPrototype(bool prototype) { myIsPrototype = prototype; }
    /// @}

    /// @{
    /// Whether the instance should be visible.
    bool isVisible() const { return myIsVisible; }
    void setIsVisible(bool visible) { myIsVisible = visible; }
    /// @}

    static int getStaticPrimitiveType();

    int getPrimitiveType() const override
    {
        return getStaticPrimitiveType();
    }

    const GT_AttributeListHandle &getDetailAttributes() const override
    {
        return myAttribs;
    }

    const char *className() const override
    {
        return "GT_PrimPackedInstance";
    }

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int getMotionSegments() const override { return 1; }

    int64 getMemoryUsage() const override { return sizeof(*this); }

    GT_PrimitiveHandle doSoftCopy() const override
    {
        return new GT_PrimPackedInstance(*this);
    }

private:
    GEO_PathHandle myPrototypePath;
    UT_IntrusivePtr<const GT_GEOPrimPacked> myPackedPrim;
    GT_AttributeListHandle myAttribs;
    bool myIsVisible;
    bool myIsPrototype;
    static int thePrimitiveType;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
