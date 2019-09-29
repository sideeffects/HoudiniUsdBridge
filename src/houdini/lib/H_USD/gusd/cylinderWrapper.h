/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef GUSD_CYLINDERWRAPPER_H
#define GUSD_CYLINDERWRAPPER_H

#include "primWrapper.h"

#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/cylinder.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Build a transform for a GT tube, given the height, radius, and axis
/// attributes from a USD cone or cylinder prim.
template <typename ConeOrCylinder>
UT_Matrix4D GusdBuildTubeXform(const ConeOrCylinder &prim, UsdTimeCode time);

/// Wraps a USD cylinder prim and refines it to a GT tube for the viewport or
/// conversion back to GU primitives.
class GusdCylinderWrapper : public GusdPrimWrapper
{
public:
    GusdCylinderWrapper(const UsdGeomCylinder &usdCylinder, UsdTimeCode t,
                        GusdPurposeSet purposes);

    virtual ~GusdCylinderWrapper();

    virtual const UsdGeomImageable getUsdPrim() const override
    {
        return m_usdCylinder;
    }

    virtual const char* className() const override;

    virtual void
    enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    virtual int getMotionSegments() const override;

    virtual int64 getMemoryUsage() const override;

    virtual GT_PrimitiveHandle doSoftCopy() const override;

    virtual bool isValid() const override;

    virtual bool refine(GT_Refine& refiner,
                        const GT_RefineParms *parms=nullptr) const override;

public:
    static GT_PrimitiveHandle
    defineForRead(const UsdGeomImageable &sourcePrim, UsdTimeCode time,
                  GusdPurposeSet purposes);

private:
    bool initUsdPrim(const UsdStagePtr& stage,
                     const SdfPath& path,
                     bool asOverride);

    UsdGeomCylinder m_usdCylinder;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
