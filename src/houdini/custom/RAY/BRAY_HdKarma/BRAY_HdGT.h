/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdGT.h (BRAY Library, C++)
 *
 * COMMENTS:
 */

#ifndef __BRAY_HdGT__
#define __BRAY_HdGT__

#include <pxr/pxr.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/quaternion.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/vt/array.h>

#include <SYS/SYS_TypeTraits.h>
#include <UT/UT_VectorTypes.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_XXHash.h>
#include <GT/GT_DataArray.h>
#include <GT/GT_DANumeric.h>

/// @file BRAY_HdGT.h
/// Class to wrap Vt data in GT types (similar to gusd)

// We need to be able to handle arrays of bool.  However, at the current time,
// GT doesn't have a specialization for bool storage.
SYS_STATIC_ASSERT(sizeof(bool) == sizeof(uint8));
template <> constexpr GT_Storage GTstorage<bool>() { return GT_STORE_UINT8; }
template <> constexpr GT_Storage GTstorage<PXR_NS::pxr_half::half>() { return GT_STORE_REAL16; }

PXR_NAMESPACE_OPEN_SCOPE

namespace BRAY_HdGT
{
    template <typename T>
    struct PodTypeTraits
    {
        using value_type = void;
        static const int tuple_size = 1;
    };

    #define DECL_POD_VECTOR(TYPE, UTTYPE) \
        template <> struct PodTypeTraits<TYPE> { \
            using value_type = typename UTTYPE::value_type; \
            static const int tuple_size = UTTYPE::tuple_size; \
        }; \
        /* end macro */
    #define DECL_POD_SCALAR(UTTYPE) \
        template <> struct PodTypeTraits<UTTYPE> { \
            using value_type = UTTYPE; \
            static const int tuple_size = 1; \
        }; \
        /* end macro */
    DECL_POD_SCALAR(bool)
    DECL_POD_SCALAR(uint8)
    DECL_POD_SCALAR(uint16)
    DECL_POD_SCALAR(uint32)
    DECL_POD_SCALAR(uint64)
    DECL_POD_SCALAR(int8)
    DECL_POD_SCALAR(int16)
    DECL_POD_SCALAR(int32)
    DECL_POD_SCALAR(int64)
    DECL_POD_SCALAR(fpreal16)
    DECL_POD_SCALAR(pxr_half::half)
    DECL_POD_SCALAR(fpreal32)
    DECL_POD_SCALAR(fpreal64)

    template <class GF_OR_UT_TYPE>
    struct TypeEquivalence
    {
        static const bool isSpecialized = false;
    };

    #define DECL_UT_GF_EQUIV_ONE_WAY(TYPE, GFTYPE, UTTYPE) \
    template <> \
    struct TypeEquivalence<TYPE> { \
        static const bool isSpecialized = true; \
        using GfType = GFTYPE; \
        using UtType = UTTYPE; \
    }; \
    /* end macro */
    #define DECL_UT_GF_EQUIV(GFTYPE, UTTYPE) \
        DECL_UT_GF_EQUIV_ONE_WAY(GFTYPE, GFTYPE, UTTYPE) \
        DECL_POD_VECTOR(GFTYPE, UTTYPE) \
        DECL_POD_VECTOR(UTTYPE, UTTYPE) \
    /* end macro */
    DECL_UT_GF_EQUIV(class GfVec2h, UT_Vector2T<fpreal16>);
    DECL_UT_GF_EQUIV(class GfVec2f, UT_Vector2T<fpreal32>);
    DECL_UT_GF_EQUIV(class GfVec2d, UT_Vector2T<fpreal64>);
    DECL_UT_GF_EQUIV(class GfVec3h, UT_Vector3T<fpreal16>);
    DECL_UT_GF_EQUIV(class GfVec3f, UT_Vector3T<fpreal32>);
    DECL_UT_GF_EQUIV(class GfVec3d, UT_Vector3T<fpreal64>);
    DECL_UT_GF_EQUIV(class GfVec4h, UT_Vector4T<fpreal16>);
    DECL_UT_GF_EQUIV(class GfVec4f, UT_Vector4T<fpreal32>);
    DECL_UT_GF_EQUIV(class GfVec4d, UT_Vector4T<fpreal64>);
    DECL_UT_GF_EQUIV(class GfQuath, UT_QuaternionT<fpreal16>);
    DECL_UT_GF_EQUIV(class GfQuatf, UT_QuaternionT<fpreal32>);
    DECL_UT_GF_EQUIV(class GfQuatd, UT_QuaternionT<fpreal64>);
    DECL_UT_GF_EQUIV(class GfMatrix2f, UT_Matrix2T<fpreal32>);
    DECL_UT_GF_EQUIV(class GfMatrix2d, UT_Matrix2T<fpreal64>);
    DECL_UT_GF_EQUIV(class GfMatrix3f, UT_Matrix3T<fpreal32>);
    DECL_UT_GF_EQUIV(class GfMatrix3d, UT_Matrix3T<fpreal64>);
    DECL_UT_GF_EQUIV(class GfMatrix4f, UT_Matrix4T<fpreal32>);
    DECL_UT_GF_EQUIV(class GfMatrix4d, UT_Matrix4T<fpreal64>);
    DECL_UT_GF_EQUIV(class GfVec2i, UT_Vector2T<int32>);
    DECL_UT_GF_EQUIV(class GfVec3i, UT_Vector3T<int32>);
    DECL_UT_GF_EQUIV(class GfVec4i, UT_Vector4T<int32>);

    template <class FROM, class TO>
    void
    Convert(const FROM &from, TO &to)
    {
        using FromPodType = typename PodTypeTraits<FROM>::value_type;
        using ToPodType = typename PodTypeTraits<TO>::value_type;
        static constexpr int tuple_size = PodTypeTraits<TO>::tuple_size;
        const auto *src = reinterpret_cast<const FromPodType *>(&from);
        auto *dst = reinterpret_cast<ToPodType *>(&to);
        for (int i = 0; i < tuple_size; ++i)
            dst[i] = src[i];
    }

template <class T>
class BRAY_VtArray final : public GT_DataArray
{
public:
    using This = BRAY_VtArray<T>;
    using array_type = VtArray<T>;
    using PODType = typename PodTypeTraits<T>::value_type;
    static constexpr int tuple_size = PodTypeTraits<T>::tuple_size;
    static constexpr GT_Storage storage = GTstorage<PODType>();

    SYS_STATIC_ASSERT(storage != GT_STORE_INVALID);

    BRAY_VtArray(const array_type &array, GT_Type type=GT_TYPE_NONE)
        : myArray(array)
        , mySize(array.size())
        , myType(type)
    {
        myData = reinterpret_cast<const PODType *>(myArray.cdata());
        UT_ASSERT(mySize == 0 || myData != nullptr);
    }
    ~BRAY_VtArray() override = default;

    const char  *className() const override { return "BRAY_VtArray"; }
    GT_DataArrayHandle  harden() const override
    {
        return UTmakeIntrusive<This>(myArray, myType);
    }
    GT_Storage   getStorage() const override { return storage; }
    GT_Size      entries() const override { return mySize; }
    GT_Size      getTupleSize() const override { return tuple_size; }
    int64        getMemoryUsage() const override { return sizeof(*this)+sizeof(T)*mySize; }
    GT_Type      getTypeInfo() const override { return myType; }
    const void  *getBackingData() const override { return myData; }
    bool         isEqual(const GT_DataArray &src) const override
    {
        if (&src == this)
            return true;
        if (src.entries() != mySize)
            return false;
        if (src.getTupleSize() != tuple_size)
            return false;
        if (src.getStorage() != storage)
            return false;
        const auto *other = dynamic_cast<const This *>(&src);
        if (!other)
            return GT_DataArray::isEqual(src);
        if (myData == other->myData)
            return true;
        // If we use std::equal and the arrays have matching Nan's, std::equal
        // failes while memcmp succeeds.
        return !::memcmp(myData, other->myData,
                sizeof(PODType)*tuple_size*mySize);
    }
    SYS_HashType        hashRange(exint b, exint e) const override
    {
        return UT_XXH64(myData+b*tuple_size,
                sizeof(PODType)*tuple_size*(e-b), 0);
    }

    const array_type    &operator*() const { return myArray; }
    const PODType       *data() const { return myData; }
#define DECL_GETTERS(SUFFIX, PODT) \
        PODT    get##SUFFIX(GT_Offset o, int idx) const override \
                    { return getT<PODT>(o, idx); } \
        const PODT      *get##SUFFIX##Array(GT_DataArrayHandle &buf) const override \
                    { return getArrayT<PODT>(buf); } \
        /* end macro */
DECL_GETTERS(I8, int8)
DECL_GETTERS(U8, uint8)
DECL_GETTERS(I16, int16)
DECL_GETTERS(I32, int32)
DECL_GETTERS(I64, int64)
DECL_GETTERS(F16, fpreal16)
DECL_GETTERS(F32, fpreal32)
DECL_GETTERS(F64, fpreal64)
#undef DECL_GETTERS
private:
    template <typename PODT>
    PODT        getT(GT_Offset o, int idx) const
    {
        UT_ASSERT_P(o >= 0 && o < mySize);
        UT_ASSERT_P(idx >= 0 && idx < tuple_size);
        return static_cast<PODT>(myData[tuple_size*o + idx]);
    }
    template <typename PODT>
    const PODT  *getArrayT(GT_DataArrayHandle &buf) const
    {
        if (SYS_IsSame<PODType, PODT>::value)
            return reinterpret_cast<const PODT *>(myData);

        auto num = UTmakeIntrusive<GT_DANumeric<PODT>>(mySize, tuple_size, myType);
        std::copy(myData, myData+tuple_size*mySize, num->data());
        buf = num;
        return num->data();
    }

    GT_String   getS(GT_Offset, int) const override { return nullptr; }
    GT_Size     getStringIndexCount() const override { return -1; }
    GT_Offset   getStringIndex(GT_Offset, int) const override { return -1; }
    void        getIndexedStrings(UT_StringArray &,
                            UT_IntArray &) const override {};

    GT_Size     getDictIndexCount() const override { return -1; }
    GT_Offset   getDictIndex(GT_Offset, int) const override { return -1; }
    void        getIndexedDicts(UT_Array<UT_OptionsHolder> &,
                            UT_IntArray &) const override {};

    array_type           myArray;
    const PODType       *myData;
    GT_Size              mySize;
    const GT_Type        myType;
};

class BRAY_VtStringArray final : public GT_DataArray
{
public:
    using This = BRAY_VtStringArray;
    using value_type = std::string;
    using array_type = VtArray<std::string>;
    static constexpr int tuple_size = 1;
    static constexpr GT_Storage storage = GT_STORE_STRING;

    BRAY_VtStringArray(const array_type &array)
        : myArray(array)
        , mySize(array.size())
    {
        myData = myArray.cdata();
        UT_ASSERT(mySize == 0 || myData != nullptr);
    }
    ~BRAY_VtStringArray() override = default;

    const char  *className() const override { return "BRAY_VtStringArray"; }
    GT_DataArrayHandle  harden() const override
    {
        return UTmakeIntrusive<This>(myArray);
    }
    GT_Storage   getStorage() const override { return storage; }
    GT_Size      entries() const override { return mySize; }
    GT_Size      getTupleSize() const override { return tuple_size; }
    int64        getMemoryUsage() const override
    {
        return sizeof(*this)+sizeof(std::string)*mySize;
    }
    bool         isEqual(const GT_DataArray &src) const override
    {
        if (&src == this)
            return true;
        if (src.entries() != mySize)
            return false;
        if (src.getTupleSize() != tuple_size)
            return false;
        if (src.getStorage() != storage)
            return false;
        const auto *other = dynamic_cast<const This *>(&src);
        if (!other)
            return GT_DataArray::isEqual(src);
        if (myData == other->myData)
            return true;
        return std::equal(myData, myData+tuple_size*mySize, other->myData);
    }

    // VtStringArray is not indirect, so there are no indices
    GT_Size     getStringIndexCount() const override { return -1; }
    GT_Offset   getStringIndex(GT_Offset, int) const override { return -1;}
    void        getIndexedStrings(UT_StringArray &,
                        UT_IntArray &) const override {}
    GT_String   getS(GT_Offset o, int) const override
                {
                    UT_ASSERT(o >= 0 && o < mySize);
                    return GT_String(myData[o]);
                }

    GT_Size     getDictIndexCount() const override { return -1; }
    GT_Offset   getDictIndex(GT_Offset, int) const override { return -1;}
    void        getIndexedDicts(UT_Array<UT_OptionsHolder> &,
                        UT_IntArray &) const override {}

private:
    // No numeric accessors supported
    uint8       getU8(GT_Offset, int) const override { return 0; }
    int32       getI32(GT_Offset, int) const override { return 0; }
    fpreal32    getF32(GT_Offset, int) const override { return 0; }

    array_type           myArray;
    const std::string   *myData;
    GT_Size              mySize;
};

} // end namespace BRAY_HdGT

PXR_NAMESPACE_CLOSE_SCOPE

#endif

