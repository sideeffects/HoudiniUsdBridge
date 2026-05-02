/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef GUSD_TETMESHWRAPPER_H
#define GUSD_TETMESHWRAPPER_H

#include "primWrapper.h"

#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/tetMesh.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a USD TetMesh prim and refines it to a GT prim for the viewport or
/// conversion back to GU primitives.
class GusdTetMeshWrapper : public GusdPrimWrapper
{
public:
    GusdTetMeshWrapper(
            const UsdGeomTetMesh& tet_mesh,
            UsdTimeCode t,
            GusdPurposeSet purposes);

    const UsdGeomImageable getUsdPrim() const override { return myUsdTetMesh; }

    const char* className() const override;

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int getMotionSegments() const override;

    int64 getMemoryUsage() const override;

    GT_PrimitiveHandle doSoftCopy() const override;

    bool isValid() const override;

    bool refine(GT_Refine& refiner, const GT_RefineParms* parms = nullptr)
            const override;

    static GT_PrimitiveHandle defineForRead(
            const UsdGeomImageable& source_prim,
            UsdTimeCode time,
            GusdPurposeSet purposes);

private:
    UsdGeomTetMesh myUsdTetMesh;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
