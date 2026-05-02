/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 */

#ifndef GUSD_GSPLATWRAPPER_H
#define GUSD_GSPLATWRAPPER_H

#include "primWrapper.h"

#include <pxr/pxr.h>
#include <pxr/usd/usdVol/particleField3DGaussianSplat.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a USD ParticleField3DGaussianSplat prim and refines it to GT points
/// for the viewport or conversion back to Houdini geometry.
class GusdGSplatWrapper : public GusdPrimWrapper
{
public:
    GusdGSplatWrapper(
            const UsdVolParticleField3DGaussianSplat &usd_gsplat,
            UsdTimeCode t,
            GusdPurposeSet purposes);

    ~GusdGSplatWrapper() override;

    const UsdGeomImageable getUsdPrim() const override { return myUsdGSplat; }

    const char *className() const override;

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int getMotionSegments() const override;

    int64 getMemoryUsage() const override;

    GT_PrimitiveHandle doSoftCopy() const override;

    bool isValid() const override;

    bool refine(GT_Refine& refiner,
                const GT_RefineParms *parms = nullptr) const override;

public:
    static GT_PrimitiveHandle defineForRead(
            const UsdGeomImageable &source_prim,
            UsdTimeCode time,
            GusdPurposeSet purposes);

private:
    bool initUsdPrim(
            const UsdStagePtr &stage,
            const SdfPath &path,
            bool as_override);

    UsdVolParticleField3DGaussianSplat myUsdGSplat;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
