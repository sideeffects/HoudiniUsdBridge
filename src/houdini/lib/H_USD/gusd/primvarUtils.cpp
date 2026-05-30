/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "primvarUtils.h"

#include "GT_VtArray.h"
#include "USD_Utils.h"
#include "UT_Gf.h"
#include "UT_TypeTraits.h"

#include <GEO/GEO_AttributeCaptureRegion.h>
#include <GT/GT_DAConstantValue.h>
#include <GT/GT_DAIndexPair.h>
#include <GT/GT_DAIndexedString.h>
#include <UT/UT_ParallelUtil.h>
#include <SYS/SYS_Types.h>

#include <pxr/base/gf/numericCast.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Convert a value to a GT_DataArray.
/// The value is either a POD type or a tuple of PODs.
template <class USD_ELEM_T, class GT_ELEM_T>
static GT_DataArrayHandle
gusdConvertTupleToGt(const GusdPrimvarInfo &attr)
{
    const VtValue &val = attr.myFlattenedValue;
    TF_DEV_AXIOM(val.IsHolding<USD_ELEM_T>());

    USD_ELEM_T held_val = val.UncheckedGet<USD_ELEM_T>();
    if constexpr (SYS_IsFloatingPoint_v<GT_ELEM_T>)
    {
        if (attr.myValueScale)
            held_val *= *attr.myValueScale;
    }

    constexpr int tuple_size = GusdGetTupleSize<USD_ELEM_T>();

    return UTmakeIntrusive<GT_DANumeric<GT_ELEM_T>>(
            reinterpret_cast<const GT_ELEM_T *>(&held_val), /*array_size=*/1,
            tuple_size, attr.myTypeInfo);
}

/// Convert a VtArray to a GT_DataArray.
/// The elements of the array are either PODs, or tuples of PODs (eg., vectors).
template <class USD_ELEM_T, class GT_ELEM_T>
static GT_DataArrayHandle
gusdConvertTupleArrayToGt(const GusdPrimvarInfo &attr)
{
    if (attr.myElementSize <= 0)
    {
        TF_WARN("<%s> invalid primvar <%s>: illegal elementSize [%d].",
                attr.myPrimPath.GetText(), attr.myOrigName.GetText(),
                attr.myElementSize);
        return nullptr;
    }

    const VtValue &val = attr.myFlattenedValue;
    TF_DEV_AXIOM(val.IsHolding<VtArray<USD_ELEM_T>>());

    const auto &array = val.UncheckedGet<VtArray<USD_ELEM_T>>();
    if (array.empty())
        return nullptr;

    // Simple case with elementSize of 1 and no scaling required - can just wrap
    // the VtArray directly.
    if (attr.myElementSize == 1 && !attr.myValueScale)
    {
        return UTmakeIntrusive<GusdGT_VtArray<USD_ELEM_T>>(
                array, attr.myTypeInfo);
    }

    // Otherwise, we need to make a GT array with the appropriate tuple size and
    // length.
    constexpr int tuple_size = GusdGetTupleSize<USD_ELEM_T>();
    const size_t num_tuples = array.size() / attr.myElementSize;
    const int gt_tuple_size = attr.myElementSize * tuple_size;

    if (num_tuples * attr.myElementSize != array.size())
    {
        TF_WARN("<%s> invalid primvar <%s>: array size [%zu] is not a "
                "multiple of the elementSize [%d].",
                attr.myPrimPath.GetText(), attr.myOrigName.GetText(),
                array.size(), attr.myElementSize);
        return nullptr;
    }

    auto gt_attr = UTmakeIntrusive<GT_DANumeric<GT_ELEM_T>>(
            reinterpret_cast<const GT_ELEM_T *>(array.cdata()), num_tuples,
            gt_tuple_size, attr.myTypeInfo);

    if constexpr (SYS_IsFloatingPoint_v<GT_ELEM_T>)
    {
        if (attr.myValueScale)
        {
            GT_ELEM_T *data = gt_attr->data();
            const exint n = gt_attr->entries() * gt_attr->getTupleSize();

            UTparallelForLightItems(UT_BlockedRange<exint>(0, n),
                                    [&](const UT_BlockedRange<exint> &range)
            {
                for (exint i : range.items())
                    data[i] *= *attr.myValueScale;
            });
        }
    }

    return gt_attr;
}

/// Convert a uint or uint64 array to an int64 array.
template <typename UnsignedArray>
static GusdPrimvarInfo
gusdConvertUnsignedArray(
        const GusdPrimvarInfo &attr,
        const UnsignedArray& src_array)
{
    VtInt64Array dst_array;
    dst_array.reserve(src_array.size());

    bool reported_warning = false;
    for (size_t i = 0, n = src_array.size(); i < n; ++i)
    {
        std::optional<int64> result = GfNumericCast<int64>(src_array[i]);
        dst_array.push_back(result.value_or(0));

        if (!result && !reported_warning)
        {
            TF_WARN("<%s> %s: Cannot convert element %" SYS_PRIu64
                    " (value %" SYS_PRIu64 ") to int64",
                    attr.myPrimPath.GetText(), attr.myOrigName.GetText(), i,
                    static_cast<uint64>(src_array[i]));
            reported_warning = true;
        }
    }

    GusdPrimvarInfo converted_attr = attr;
    converted_attr.myFlattenedValue = VtValue(dst_array);
    return converted_attr;
}

// Conversions from USD string-like types to UT_StringHolder.

static UT_StringHolder
gusdConvertToStringHolder(const std::string &s)
{
    return UT_StringHolder(s);
}

static UT_StringHolder
gusdConvertToStringHolder(const TfToken &s)
{
    return GusdUSD_Utils::TokenToStringHolder(s);
}

static UT_StringHolder
gusdConvertToStringHolder(const SdfAssetPath &s)
{
    return UT_StringHolder(s.GetAssetPath());
}

/// Convert a string-like value to a GT_DataArray.
template <class USD_ELEM_T>
static GT_DataArrayHandle
gusdConvertStringToGt(const GusdPrimvarInfo &attr)
{
    const VtValue &val = attr.myFlattenedValue;
    TF_DEV_AXIOM(val.IsHolding<USD_ELEM_T>());

    auto gt_string = UTmakeIntrusive<GT_DAIndexedString>(1);
    gt_string->setString(
            0, 0, gusdConvertToStringHolder(val.UncheckedGet<USD_ELEM_T>()));
    return gt_string;
}

/// Convert a VtArray of string-like values to a GT_DataArray.
template <typename USD_ELEM_T>
GT_DataArrayHandle
gusdConvertStringArrayToGt(const GusdPrimvarInfo &attr)
{
    if (attr.myElementSize <= 0)
    {
        TF_WARN("<%s> invalid primvar <%s>: illegal elementSize [%d].",
                attr.myPrimPath.GetText(), attr.myOrigName.GetText(),
                attr.myElementSize);
        return nullptr;
    }

    const VtValue &val = attr.myFlattenedValue;
    TF_DEV_AXIOM(val.IsHolding<VtArray<USD_ELEM_T>>());
    const auto &array = val.UncheckedGet<VtArray<USD_ELEM_T>>();
    if (array.empty())
        return nullptr;

    const size_t num_tuples = array.size() / attr.myElementSize;

    if (num_tuples * attr.myElementSize != array.size())
    {
        TF_WARN("<%s> invalid primvar <%s>: array size [%zu] is not a "
                "multiple of the elementSize [%d].",
                attr.myPrimPath.GetText(), attr.myOrigName.GetText(),
                array.size(), attr.myElementSize);
        return nullptr;
    }

    const USD_ELEM_T *values = array.cdata();

    auto gt_strings = UTmakeIntrusive<GT_DAIndexedString>(
            num_tuples, attr.myElementSize);

    for (size_t i = 0; i < num_tuples; ++i)
    {
        for (int cmp = 0; cmp < attr.myElementSize; ++cmp, ++values)
            gt_strings->setString(i, cmp, gusdConvertToStringHolder(*values));
    }

    return gt_strings;
}

GT_DataArrayHandle
GusdConvertPrimvarData(const GusdPrimvarInfo &attr)
{
    const VtValue &value = attr.myFlattenedValue;

#define GUSD_CONVERT_TUPLE(ELEM_T, GT_ELEM_T)                                  \
    if (value.IsHolding<ELEM_T>())                                             \
    {                                                                          \
        return gusdConvertTupleToGt<ELEM_T, GT_ELEM_T>(attr);                  \
    }                                                                          \
    else if (value.IsHolding<VtArray<ELEM_T>>())                               \
    {                                                                          \
        return gusdConvertTupleArrayToGt<ELEM_T, GT_ELEM_T>(attr);             \
    }

    // Check for most common value types first.
    GUSD_CONVERT_TUPLE(GfVec3f, fpreal32);
    GUSD_CONVERT_TUPLE(GfVec2f, fpreal32);
    GUSD_CONVERT_TUPLE(float, fpreal32);
    GUSD_CONVERT_TUPLE(int, int32);

    // Scalars
    GUSD_CONVERT_TUPLE(double, fpreal64);
    GUSD_CONVERT_TUPLE(GfHalf, fpreal16);
    GUSD_CONVERT_TUPLE(int64, int64);
    GUSD_CONVERT_TUPLE(unsigned char, uint8);
    GUSD_CONVERT_TUPLE(bool, uint8);

    // Vec2
    GUSD_CONVERT_TUPLE(GfVec2d, fpreal64);
    GUSD_CONVERT_TUPLE(GfVec2h, fpreal16);
    GUSD_CONVERT_TUPLE(GfVec2i, int32);

    // Vec3
    GUSD_CONVERT_TUPLE(GfVec3d, fpreal64);
    GUSD_CONVERT_TUPLE(GfVec3h, fpreal16);
    GUSD_CONVERT_TUPLE(GfVec3i, int32);

    // Vec4
    GUSD_CONVERT_TUPLE(GfVec4d, fpreal64);
    GUSD_CONVERT_TUPLE(GfVec4f, fpreal32);
    GUSD_CONVERT_TUPLE(GfVec4h, fpreal16);
    GUSD_CONVERT_TUPLE(GfVec4i, int32);

    // Quat
    GUSD_CONVERT_TUPLE(GfQuatd, fpreal64);
    GUSD_CONVERT_TUPLE(GfQuatf, fpreal32);
    GUSD_CONVERT_TUPLE(GfQuath, fpreal16);

    // Matrices
    GUSD_CONVERT_TUPLE(GfMatrix3d, fpreal64);
    GUSD_CONVERT_TUPLE(GfMatrix4d, fpreal64);
    GUSD_CONVERT_TUPLE(GfMatrix2d, fpreal64);

#undef GUSD_CONVERT_TUPLE

    // Convert uint and uint64 to int64 (matching the Alembic importer)
    if (value.IsHolding<uint32>() || value.IsHolding<uint64>())
    {
        VtValue converted_val = VtValue::Cast<int64>(value);
        if (converted_val.IsEmpty())
        {
            TF_WARN("<%s> %s: Cannot convert value %" SYS_PRIu64 " to int64",
                    attr.myPrimPath.GetText(), attr.myOrigName.GetText(),
                    value.Get<uint64>());

            converted_val = VtValue(int64(0));
        }

        GusdPrimvarInfo converted_attr = attr;
        converted_attr.myFlattenedValue = converted_val;

        return gusdConvertTupleToGt<int64, int64>(converted_attr);
    }
    else if (value.IsHolding<VtUIntArray>() || value.IsHolding<VtUInt64Array>())
    {
        GusdPrimvarInfo converted_attr;
        if (value.IsHolding<VtUIntArray>())
        {
            converted_attr = gusdConvertUnsignedArray(
                    attr, value.UncheckedGet<VtUIntArray>());
        }
        else
        {
            converted_attr = gusdConvertUnsignedArray(
                    attr, value.UncheckedGet<VtUInt64Array>());
        }

        UT_ASSERT(converted_attr);
        return gusdConvertTupleArrayToGt<int64, int64>(converted_attr);
    }

    // Convert string types.
#define GUSD_CONVERT_STRING(ELEM_T)                                            \
    if (value.IsHolding<ELEM_T>())                                             \
    {                                                                          \
        return gusdConvertStringToGt<ELEM_T>(attr);                            \
    }                                                                          \
    else if (value.IsHolding<VtArray<ELEM_T>>())                               \
    {                                                                          \
        return gusdConvertStringArrayToGt<ELEM_T>(attr);                       \
    }

    GUSD_CONVERT_STRING(std::string);
    GUSD_CONVERT_STRING(TfToken);
    GUSD_CONVERT_STRING(SdfAssetPath);

#undef GUSD_CONVERT_STRING

    return nullptr;
}

GT_DataArrayHandle
GusdConvertPrimvarsToIndexPairData(
        const GusdPrimvarInfo &indices_primvar,
        const GusdPrimvarInfo &weights_primvar,
        const VtTokenArray &joint_names)
{
    const SdfPath &prim_path = indices_primvar.myPrimPath;

    if (indices_primvar.myOwner != weights_primvar.myOwner
        || indices_primvar.myElementSize != weights_primvar.myElementSize)
    {
        TF_WARN("<%s>: '%s' and '%s' do not have the same interpolation and "
                "elementSize",
                prim_path.GetText(), indices_primvar.myOrigName.GetText(),
                weights_primvar.myOrigName.GetText());
        return nullptr;
    }

    if (!indices_primvar.myFlattenedValue.IsHolding<VtIntArray>())
    {
        TF_WARN("<%s>: invalid type for '%s'",
                prim_path.GetText(), indices_primvar.myOrigName.GetText());
        return nullptr;
    }
    if (!weights_primvar.myFlattenedValue.IsHolding<VtFloatArray>())
    {
        TF_WARN("<%s>: invalid type for '%s'",
                prim_path.GetText(), weights_primvar.myOrigName.GetText());
        return nullptr;
    }

    const VtIntArray &indices
            = indices_primvar.myFlattenedValue.Get<VtIntArray>();
    const VtFloatArray &weights
            = weights_primvar.myFlattenedValue.Get<VtFloatArray>();
    if (indices.empty() || indices.size() != weights.size())
    {
        TF_WARN("<%s>: invalid length for '%s' and '%s'", prim_path.GetText(),
                indices_primvar.myOrigName.GetText(),
                weights_primvar.myOrigName.GetText());
        return nullptr;
    }

    // Translate the joint list to GT.
    const exint num_joints = joint_names.size();
    auto gt_joints = UTmakeIntrusive<GT_DAIndexedString>(num_joints);
    for (exint i = 0; i < num_joints; ++i)
    {
        gt_joints->setString(
                i, 0, GusdUSD_Utils::TokenToStringHolder(joint_names[i]));
    }

    // We don't make use of the bind transforms (capture pose)
    // stored in the boneCapture attribute, so just set to identity
    // transforms. For APEX, it's expected that a skeleton with the
    // capture pose is wired into the joint deform verb's second
    // input.
    GEO_CaptureBoneStorage capt_data; // Default ctor sets to identity matrix.
    auto gt_capt_data = UTmakeIntrusive<GT_DAConstantValue<fpreal32>>(
            num_joints, capt_data.floatPtr(), capt_data.tuple_size);

    const exint tuple_size = indices_primvar.myElementSize;
    const exint num_elements = indices.size() / tuple_size;
    if (num_elements * tuple_size != indices.size())
    {
        TF_WARN(
                "<%s>: invalid primvar '%s': array size [%zu] is not a "
                "multiple of the elementSize [%d].",
                prim_path.GetText(), indices_primvar.myOrigName.GetText(),
                indices.size(), int(tuple_size));
        return nullptr;
    }

    auto index_pair = UTmakeIntrusive<GT_DAIndexPair<fpreal32>>(
            num_elements, 2 * tuple_size);

    index_pair->setIndexPairObjectSetCount(1);
    index_pair->setIndexPairObjects(
            0, GT_AttributeList::createAttributeList(
                       GEO_STD_ATTRIB_PNT_CAPTURE_PATH, gt_joints,
                       GEO_STD_ATTRIB_PNT_CAPTURE_DATA, gt_capt_data));

    UTparallelFor(UT_BlockedRange<exint>(0, num_elements),
                  [&](const UT_BlockedRange<exint> &range)
    {
        for (exint i : range.items())
        {
            for (exint j = 0 ; j < tuple_size; ++j)
            {
                int index = indices[i * tuple_size + j];
                float weight = weights[i * tuple_size + j];
                // Unused influences have a weight of 0.0 in USD, but
                // boneCapture expects -1.
                if (weight == 0.0)
                {
                    index = -1;
                    weight = -1;
                }

                index_pair->set(index, i, j * 2);
                index_pair->set(weight, i, j * 2 + 1);
            }
        }
    });

    return index_pair;
}

PXR_NAMESPACE_CLOSE_SCOPE
