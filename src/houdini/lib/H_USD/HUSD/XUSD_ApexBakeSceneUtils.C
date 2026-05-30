/*
 * Copyright 2025 Side Effects Software Inc.
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

#include "XUSD_ApexBakeSceneUtils.h"

#include "HUSD_ErrorScope.h"
#include "XUSD_Format.h" // IWYU pragma: keep (for format())

#include <GA/GA_Handle.h>
#include <GA/GA_IndexMap.h>
#include <GA/GA_Iterator.h>
#include <GA/GA_Names.h>
#include <GA/GA_Range.h>
#include <GA/GA_SplittableRange.h>
#include <GA/GA_Types.h>
#include <GU/GU_AttribValueLookupTable.h>
#include <GU/GU_Detail.h>
#include <UT/UT_IteratorRange.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_WorkBuffer.h>
#include <gusd/UT_Gf.h>

#include <pxr/base/gf/vec3h.h>
#include <pxr/base/tf/span.h>

PXR_NAMESPACE_OPEN_SCOPE

bool
XUSD_ApexBakeSceneUtils::getSkelJointXform(
        const SdfPath &prim_path,
        const GU_Detail &skel_geo,
        const UT_StringRef &joint_name,
        UT_Matrix4D &joint_xform)
{
    GA_ROHandleS name_attrib = skel_geo.findStringTuple(
            GA_ATTRIB_POINT, GA_Names::name);
    if (!name_attrib.isValid())
    {
        UT_WorkBuffer msg;
        msg.format(
                "{0}: missing required point attribute '{1}' for skeleton "
                "geometry",
                prim_path, GA_Names::name);
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    const GU_Detail::AttribSingleValueLookupTable *name_lookup
            = skel_geo.getSingleLookupTable(name_attrib.getAttribute());
    UT_ASSERT(name_lookup);

    const GA_Offset skel_pt = name_lookup->getStringOffset(joint_name);
    if (!GAisValid(skel_pt))
    {
        UT_WorkBuffer msg;
        msg.format(
                "{0}: joint '{1}' not found in skeleton geometry", prim_path,
                joint_name);
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    GA_ROHandleM3D xform_attrib = skel_geo.findFloatTuple(
            GA_ATTRIB_POINT, GA_Names::transform, 9, 9);
    if (!xform_attrib.isValid())
    {
        UT_WorkBuffer msg;
        msg.format(
                "{0}: missing required point attribute '{1}' for skeleton "
                "geometry",
                prim_path, GA_Names::transform);
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    joint_xform = xform_attrib.get(skel_pt);
    joint_xform.setTranslates(skel_geo.getPos3D(skel_pt));
    return true;
}

namespace
{
/// Functor for computeExtentFromPoints().
template <typename Vec3T>
struct xusdComputeExtentTask
{
    xusdComputeExtentTask(TfSpan<const Vec3T> positions)
        : myPositions(positions)
    {
    }

    xusdComputeExtentTask(const xusdComputeExtentTask &src, UT_Split)
        : myPositions(src.myPositions)
    {
    }

    void operator()(const UT_BlockedRange<exint> &range)
    {
        for (exint i : range.items())
            myExtent.UnionWith(myPositions[i]);
    }

    void join(const xusdComputeExtentTask &other)
    {
        myExtent.UnionWith(other.myExtent);
    }

    TfSpan<const Vec3T> myPositions;
    GfRange3f myExtent;
};
} // namespace

GfRange3f
XUSD_ApexBakeSceneUtils::computeExtentFromPoints(
        TfSpan<const GfVec3f> positions)
{
    xusdComputeExtentTask task(positions);
    UTparallelReduceLightItems(
            UT_BlockedRange<exint>(0, positions.size()), task);

    return task.myExtent;
}

GfRange3f
XUSD_ApexBakeSceneUtils::computeExtentFromPoints(
        TfSpan<const GfVec3h> positions)
{
    xusdComputeExtentTask task(positions);
    UTparallelReduceLightItems(
            UT_BlockedRange<exint>(0, positions.size()), task);

    return task.myExtent;
}

template <typename UtT, typename GfT, typename XformFunc>
static void
xusdConvertAttribute(
        const GU_Detail &detail,
        const GA_ROHandleT<UtT> &attrib,
        XformFunc apply_xform,
        VtArray<GfT> &result_array)
{
    const GA_IndexMap &index_map = detail.getIndexMap(attrib->getOwner());

    // Use the resize() overload which allows us to fill in the elements
    // directly (instead of first default constructing each element). Note this
    // requires placement new because we are given pointers to uninitialized
    // memory.
    result_array.clear();
    result_array.resize(index_map.indexSize(),
                        [&](GfT *result_data, GfT *result_end)
    {
        UTparallelFor(GA_SplittableRange(GA_Range(index_map)),
                      [&](const GA_SplittableRange &r)
        {
            GA_Offset start, end;
            for (GA_Iterator it(r); it.blockAdvance(start, end); )
            {
                for (GA_Offset offset = start; offset < end; ++offset)
                {
                    const GA_Index index = index_map.indexFromOffset(offset);
                    UtT value = attrib.get(offset);

                    if constexpr (GusdUT_Gf::Castable<UtT>::value)
                    {
                        ::new (result_data + index)
                                GfT(GusdUT_Gf::Cast(apply_xform(value)));
                    }
                    else
                    {
                        ::new (result_data + index) GfT();
                        GusdUT_Gf::Convert(
                                apply_xform(value), result_data[index]);
                    }
                }
            }
        });
    });
}

template <typename UtT, typename GfT>
void
XUSD_ApexBakeSceneUtils::convertAttribute(
        const GU_Detail &detail,
        const GA_ROHandleT<UtT> &attrib,
        const UT_Matrix4D *world_to_prim_xform,
        VtArray<GfT> &result_array)
{
    const GA_TypeInfo type_info = attrib->getTypeInfo();

    // No transformation necessary.
    if (world_to_prim_xform == nullptr)
    {
        auto identity_xform = [](const UtT &v) { return v; };
        xusdConvertAttribute(detail, attrib, identity_xform, result_array);
        return;
    }

    // vec3 types
    if constexpr (UtT::tuple_size == 3)
    {
        if (type_info == GA_TypeInfo::GA_TYPE_NORMAL)
        {
            UT_Matrix4D inv_xform;
            world_to_prim_xform->invert(inv_xform);

            auto apply_xform = [&](const UtT &n)
            {
                return colVecMult3(inv_xform, n);
            };

            xusdConvertAttribute(detail, attrib, apply_xform, result_array);
        }
        else if (type_info == GA_TYPE_POINT)
        {
            auto apply_xform = [&](const UtT &v)
            {
                return v * (*world_to_prim_xform);
            };

            xusdConvertAttribute(detail, attrib, apply_xform, result_array);
        }

        return;
    }
    // vec4 types
    else if constexpr (UtT::tuple_size == 4)
    {
        if (type_info == GA_TYPE_QUATERNION)
        {
            UtT xform_q;
            xform_q.updateFromArbitraryMatrix(
                    UT_Matrix3D(*world_to_prim_xform));

            auto apply_xform = [&](const UtT &v)
            {
                return xform_q * v;
            };

            xusdConvertAttribute(detail, attrib, apply_xform, result_array);
        }
        
        return;
    }

    UT_ASSERT_MSG(false, "Unsupported attribute type info");
}

#define INSTANTIATE_CONVERT_ATTRIB(UtT, GfT)                                   \
    template void XUSD_ApexBakeSceneUtils::convertAttribute<UtT, GfT>(         \
            const GU_Detail &detail, const GA_ROHandleT<UtT> &attrib,          \
            const UT_Matrix4D *world_to_prim_xform,                            \
            VtArray<GfT> &result_array);

// Explicit instantiations for our supported types.
INSTANTIATE_CONVERT_ATTRIB(UT_Vector3F, GfVec3f)
INSTANTIATE_CONVERT_ATTRIB(UT_Vector3H, GfVec3h)
INSTANTIATE_CONVERT_ATTRIB(UT_QuaternionF, GfQuatf)
INSTANTIATE_CONVERT_ATTRIB(UT_QuaternionH, GfQuath)

#undef INSTANTIATE_CONVERT_ATTRIB

PXR_NAMESPACE_CLOSE_SCOPE
