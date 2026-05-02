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

#include "GEO_FilePrimInstancerUtils.h"

#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_TransformArray.h>
#include <GU/GU_PackedDisk.h>
#include <GU/GU_PackedFragment.h>

PXR_NAMESPACE_OPEN_SCOPE

const GT_PackedInstanceKey GTnotInstancedKey(-1);

TF_DEFINE_PUBLIC_TOKENS(GEO_PointInstancerPrimTokens,
                        GEO_POINTINSTANCER_PRIM_TOKENS);

GT_PackedFragmentId::GT_PackedFragmentId(exint geometry_id,
                                         const UT_StringHolder &attrib_name,
                                         const UT_StringHolder &attrib_value)
    : myGeometryId(geometry_id),
      myAttribName(attrib_name),
      myAttribValue(attrib_value)
{
}

bool
GT_PackedFragmentId::operator==(const GT_PackedFragmentId &other) const
{
    return myGeometryId == other.myGeometryId &&
           myAttribName == other.myAttribName &&
           myAttribValue == other.myAttribValue;
}

size_t
GT_PackedFragmentId::hash() const
{
    size_t hash_val = SYShash(myGeometryId);
    SYShashCombine(hash_val, myAttribName);
    SYShashCombine(hash_val, myAttribValue);
    return hash_val;
}

GT_PackedInstanceKey
GTpackedInstanceKey(const GT_GEOPrimPacked &prototype_prim)
{
    GA_PrimitiveTypeId packed_type = prototype_prim.getPrim()->getTypeId();
    if (packed_type == GU_PackedDisk::typeId())
    {
        auto packed_disk = UTverify_cast<const GU_PackedDisk *>(
            prototype_prim.getImplementation());
        return GT_PackedDiskId(packed_disk->filename());
    }
    else if (packed_type == GU_PackedFragment::typeId())
    {
        auto fragment = UTverify_cast<const GU_PackedFragment *>(
            prototype_prim.getImplementation());
        return GT_PackedFragmentId(fragment->geometryId(),
                                   fragment->attribute(), fragment->name());
    }
    else
    {
        GU_ConstDetailHandle gdh = prototype_prim.getPackedDetail();
        return gdh.isValid() ? GT_PackedGeometryId(gdh.gdp()->getUniqueId()) :
                               GTnotInstancedKey;
    }
}

int
GT_PrimPointInstancer::findPrototype(
    const GT_GEOPrimPacked &prototype_prim) const
{
    auto it = myPrototypeIndex.find(GTpackedInstanceKey(prototype_prim));
    return it != myPrototypeIndex.end() ? it->second : -1;
}

int
GT_PrimPointInstancer::addPrototype(const GT_GEOPrimPacked &prototype_prim,
                                    const GEO_PathHandle &path)
{
    const int idx = myPrototypePaths.size();
    myPrototypePaths.append(path);

    // If the prototype cannot be identified as an instance, omit it from the
    // prototype index.
    GT_PackedInstanceKey key = GTpackedInstanceKey(prototype_prim);
    if (key != GTnotInstancedKey)
        myPrototypeIndex[key] = idx;

    return idx;
}

SdfPathVector
GT_PrimPointInstancer::getPrototypePaths() const
{
    SdfPathVector paths;
    for (const GEO_PathHandle &path : myPrototypePaths)
        paths.push_back(*path);

    return paths;
}

void
GT_PrimPointInstancer::addInstances(
    int proto_index, const GT_TransformArray &xforms,
    const UT_Array<exint> &invisible_instances,
    const GT_AttributeListHandle &instance_attribs,
    const GT_AttributeListHandle &detail_attribs)
{
    const exint start_idx = myProtoIndices.entries();

    UT_ASSERT(proto_index >= 0 && proto_index < myPrototypePaths.size());
    myProtoIndices.appendMultiple(proto_index, xforms.entries());

    // Renumber and record the invisible instances.
    for (exint id : invisible_instances)
        myInvisibleInstances.append(id + start_idx);

    myInstanceAttribLists.append(instance_attribs);
    // The concatenated list is lazily evaluated, so if this happens before
    // adding all instances there could be n^2 behaviour.
    UT_ASSERT(!myInstanceAttribs);

    if (myDetailAttribs)
        myDetailAttribs = myDetailAttribs->mergeNewAttributes(detail_attribs);
    else
        myDetailAttribs = detail_attribs;

    for (GT_Size i = 0, n = xforms.entries(); i < n; ++i)
    {
        UT_Matrix4D xform;
        xforms.get(i)->getMatrix(xform);
        myInstanceXforms.append(xform);
    }
}

const GT_AttributeListHandle &
GT_PrimPointInstancer::getPointAttributes() const
{
    if (!myInstanceAttribs && myInstanceAttribLists.entries())
    {
        // Delay the concatenation to prevent n^2 behaviour when appending
        // instances (this could also be avoided by changing GT_DAList to allow
        // incremental updates).
        // Since GT prims are not shared between threads in SOP Import, there
        // should not be any thread safety issues here.
        myInstanceAttribs
                = GT_AttributeList::concatenateLists(myInstanceAttribLists);
    }

    return myInstanceAttribs;
}

int
GT_PrimPointInstancer::getStaticPrimitiveType()
{
    static const int thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
    return thePrimitiveType;
}

GT_PrimPackedInstance::GT_PrimPackedInstance(
        const UT_IntrusivePtr<const GT_GEOPrimPacked> &packed_prim,
        const GT_TransformHandle &xform,
        const GT_AttributeListHandle &attribs,
        bool visible,
        bool draw_bounds)
    : myPackedPrim(packed_prim)
    , myAttribs(attribs)
    , myIsVisible(visible)
    , myDrawBounds(draw_bounds)
    , myIsPrototype(false)
{
    UT_ASSERT(myPackedPrim);
    UT_ASSERT(myPackedPrim->getPrimitiveType() == GT_GEO_PACKED);
    setPrimitiveTransform(xform);
}

const GU_PackedImpl *
GT_PrimPackedInstance::getPackedImpl() const
{
    auto packed = UTverify_cast<const GT_GEOPrimPacked *>(myPackedPrim.get());
    UT_ASSERT(packed->getPrim());
    return packed->getImplementation();
}

void
GT_PrimPackedInstance::enlargeBounds(UT_BoundingBox boxes[],
                                     int nsegments) const
{
    myPackedPrim->enlargeBounds(boxes, nsegments);
}

int
GT_PrimPackedInstance::getStaticPrimitiveType()
{
    static const int thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
    return thePrimitiveType;
}

PXR_NAMESPACE_CLOSE_SCOPE
