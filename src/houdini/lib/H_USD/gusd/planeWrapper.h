/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef GUSD_PLANEWRAPPER_H
#define GUSD_PLANEWRAPPER_H

#include "primWrapper.h"

#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/plane.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a USD plane prim and refines it to a GT plane for the viewport or
/// conversion back to GU primitives.
class GusdPlaneWrapper : public GusdPrimWrapper
{
public:
    GusdPlaneWrapper(const UsdGeomPlane &usd_plane, UsdTimeCode t,
                     GusdPurposeSet purposes);

    ~GusdPlaneWrapper() override;

    const UsdGeomImageable getUsdPrim() const override { return myUsdPlane; }

    const char* className() const override;

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int getMotionSegments() const override;

    int64 getMemoryUsage() const override;

    GT_PrimitiveHandle doSoftCopy() const override;

    bool isValid() const override;

    bool refine(GT_Refine& refiner,
                const GT_RefineParms* parms = nullptr) const override;

public:
    static GT_PrimitiveHandle
    defineForRead(const UsdGeomImageable &source_prim, UsdTimeCode time,
                  GusdPurposeSet purposes);

private:
    bool initUsdPrim(const UsdStagePtr& stage,
                     const SdfPath& path,
                     bool as_override);

    UsdGeomPlane myUsdPlane;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
