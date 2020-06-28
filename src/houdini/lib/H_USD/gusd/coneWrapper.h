/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef GUSD_CONEWRAPPER_H
#define GUSD_CONEWRAPPER_H

#include "primWrapper.h"

#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/cone.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a USD cone prim and refines it to a GT tube for the viewport or
/// conversion back to GU primitives.
class GusdConeWrapper : public GusdPrimWrapper
{
public:
    GusdConeWrapper(const UsdGeomCone &usdCone, UsdTimeCode t,
                    GusdPurposeSet purposes);

    ~GusdConeWrapper() override;

    const UsdGeomImageable getUsdPrim() const override
    {
        return m_usdCone;
    }

    const char* className() const override;

    void
    enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int getMotionSegments() const override;

    int64 getMemoryUsage() const override;

    GT_PrimitiveHandle doSoftCopy() const override;

    bool isValid() const override;

    bool refine(GT_Refine& refiner,
                const GT_RefineParms *parms=nullptr) const override;

public:
    static GT_PrimitiveHandle
    defineForRead(const UsdGeomImageable &sourcePrim, UsdTimeCode time,
                  GusdPurposeSet purposes);

private:
    bool initUsdPrim(const UsdStagePtr& stage,
                     const SdfPath& path,
                     bool asOverride);

    UsdGeomCone m_usdCone;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
