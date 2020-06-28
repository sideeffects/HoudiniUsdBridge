/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef GUSD_CUBEWRAPPER_H
#define GUSD_CUBEWRAPPER_H

#include "primWrapper.h"

#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/cube.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a USD cube prim and refines it to a GT mesh for the viewport or
/// conversion back to GU primitives.
class GusdCubeWrapper : public GusdPrimWrapper
{
public:
    GusdCubeWrapper(const UsdGeomCube &usdCube, UsdTimeCode t,
                    GusdPurposeSet purposes);

    ~GusdCubeWrapper() override;

    const UsdGeomImageable getUsdPrim() const override
    {
        return m_usdCube;
    }

    const char* className() const override;

    void
    enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int getMotionSegments() const override;

    int64 getMemoryUsage() const override;

    GT_PrimitiveHandle doSoftCopy() const override;

    bool isValid() const override;

    bool refine(GT_Refine& refiner,
                const GT_RefineParms* parms=NULL) const override;

public:
    static GT_PrimitiveHandle
    defineForRead(const UsdGeomImageable &sourcePrim, UsdTimeCode time,
                  GusdPurposeSet purposes);

private:
    bool initUsdPrim(const UsdStagePtr& stage,
                     const SdfPath& path,
                     bool asOverride);

    UsdGeomCube m_usdCube;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
