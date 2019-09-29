/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 */

#ifndef __GEO_FilePrimVolumeUtils_h__
#define __GEO_FilePrimVolumeUtils_h__

#include <GT/GT_Primitive.h>
#include <UT/UT_ArrayStringSet.h>
#include <pxr/pxr.h>
#include <pxr/base/vt/types.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

#define GEO_VOLUME_PRIM_TOKENS  \
    ((volume, "volume"))

TF_DECLARE_PUBLIC_TOKENS(GEO_VolumePrimTokens, GEO_VOLUME_PRIM_TOKENS);

/// GT equivalent to UsdVolVolume. Stores a set of references to field
/// primitives (VDB or Houdini volumes).
class GT_PrimVolumeCollection : public GT_Primitive
{
public:
    GT_PrimVolumeCollection() = default;

    /// @{
    /// The path to the USD volume prim.
    const SdfPath &getPath() const { return myPath; }
    void setPath(const SdfPath &path) { myPath = path; }
    /// @}

    /// @{
    /// Paths to the volume's field prims.
    const SdfPathVector &getFields() const { return myFields; }
    void addField(const SdfPath &path, const UT_StringHolder &name)
    {
        myFields.push_back(path);
        myFieldNames.insert(name);
    }
    /// @}

    /// Returns whether the volume has a field with the specified name.
    bool hasField(const UT_StringRef &name) const
    {
        return myFieldNames.contains(name);
    }

    static int getStaticPrimitiveType();

    virtual int getPrimitiveType() const override
    {
        return getStaticPrimitiveType();
    }

    virtual const char *className() const override
    {
        return "GT_PrimVolumeCollection";
    }

    virtual void
    enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override
    {
    }

    virtual int getMotionSegments() const override { return 1; }

    virtual int64 getMemoryUsage() const override { return sizeof(*this); }

    virtual GT_PrimitiveHandle doSoftCopy() const override
    {
        return new GT_PrimVolumeCollection(*this);
    }

private:
    SdfPath myPath;
    SdfPathVector myFields;
    UT_ArrayStringSet myFieldNames;

    static int thePrimitiveType;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
