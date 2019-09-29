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

    virtual ~GusdCubeWrapper();

    virtual const UsdGeomImageable getUsdPrim() const override
    {
        return m_usdCube;
    }

    virtual const char* className() const override;

    virtual void
    enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    virtual int getMotionSegments() const override;

    virtual int64 getMemoryUsage() const override;

    virtual GT_PrimitiveHandle doSoftCopy() const override;

    virtual bool isValid() const override;

    virtual bool refine(GT_Refine& refiner,
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
