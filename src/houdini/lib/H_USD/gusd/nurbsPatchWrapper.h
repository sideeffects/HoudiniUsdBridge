/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef GUSD_NURBSPATCHWRAPPER_H
#define GUSD_NURBSPATCHWRAPPER_H

#include "primWrapper.h"

#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/nurbsPatch.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a USD NurbsPatch prim and refines it to a GT prim for the viewport or
/// conversion back to GU primitives.
class GusdNurbsPatchWrapper : public GusdPrimWrapper
{
public:
    GusdNurbsPatchWrapper(
            const UsdGeomNurbsPatch& usdPatch,
            UsdTimeCode t,
            GusdPurposeSet purposes);

    ~GusdNurbsPatchWrapper() override;

    const UsdGeomImageable getUsdPrim() const override
    {
        return m_usdPatch;
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

    UsdGeomNurbsPatch m_usdPatch;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
