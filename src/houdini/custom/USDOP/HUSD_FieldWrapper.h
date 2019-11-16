/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef __HUSD_FieldWrapper__
#define __HUSD_FieldWrapper__

#include <gusd/primWrapper.h>
#include <pxr/usd/usdVol/fieldAsset.h>

PXR_NAMESPACE_OPEN_SCOPE

/// GusdPrimWrapper implementation for converting USD fields back to
/// GT_PrimVolume or GT_PrimVDB primitives.
class HUSD_FieldWrapper : public GusdPrimWrapper
{
public:
    HUSD_FieldWrapper(const UsdVolFieldAsset &usd_field, UsdTimeCode t,
                      GusdPurposeSet purposes);
    virtual ~HUSD_FieldWrapper();

    static void registerForRead();

    virtual const UsdGeomImageable getUsdPrim() const override
    {
        return myUsdField;
    }

    virtual const char *className() const override;

    virtual void
    enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    virtual int getMotionSegments() const override;

    virtual int64 getMemoryUsage() const override;

    virtual GT_PrimitiveHandle doSoftCopy() const override;

    virtual bool isValid() const override;

    virtual bool refine(GT_Refine &refiner,
                        const GT_RefineParms *parms = nullptr) const override;

public:
    static GT_PrimitiveHandle
    defineForRead(const UsdGeomImageable &source_prim, UsdTimeCode time,
                  GusdPurposeSet purposes);

private:
    bool initUsdPrim(const UsdStagePtr &stage, const SdfPath &path,
                     bool as_override);

    UsdVolFieldAsset myUsdField;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
