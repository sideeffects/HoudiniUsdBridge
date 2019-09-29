/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "cubeWrapper.h"

#include <GT/GT_PrimitiveBuilder.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_Refine.h>

PXR_NAMESPACE_OPEN_SCOPE

GusdCubeWrapper::GusdCubeWrapper(const UsdGeomCube &usdCube, UsdTimeCode time,
                                 GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), m_usdCube(usdCube)
{
}

GusdCubeWrapper::~GusdCubeWrapper() {}

const char *
GusdCubeWrapper::className() const
{
    return "GusdCubeWrapper";
}

void
GusdCubeWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "GusdCubeWrapper::enlargeBounds not implemented");
}

int
GusdCubeWrapper::getMotionSegments() const
{
    return 1;
}

int64
GusdCubeWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
GusdCubeWrapper::doSoftCopy() const
{
    return GT_PrimitiveHandle(new GusdCubeWrapper(*this));
}

bool
GusdCubeWrapper::isValid() const
{
    return static_cast<bool>(m_usdCube);
}

bool
GusdCubeWrapper::refine(GT_Refine &refiner, const GT_RefineParms *parms) const
{
    if (!isValid())
    {
        TF_WARN("Invalid prim");
        return false;
    }

    UsdAttribute sizeAttr = m_usdCube.GetSizeAttr();
    double size = 2.0;
    if (sizeAttr)
        TF_VERIFY(sizeAttr.Get(&size, m_time));

    // The size attribute describes the entire edge length.
    size *= 0.5;
    UT_BoundingBox box(-size, -size, -size, size, size, size);

    // Create a polygonal box, since there isn't a cube primitive in Houdini.
    GT_BuilderStatus status;
    GT_PrimitiveHandle mesh_prim = GT_PrimitiveBuilder::box(status, box);

    GT_AttributeListHandle attribs =
        new GT_AttributeList(new GT_AttributeMap());
    loadPrimvars(m_time, parms, 0, 0, 0, m_usdCube.GetPath().GetString(),
                 nullptr, nullptr, nullptr, &attribs);

    auto mesh = UTverify_cast<GT_PrimPolygonMesh *>(mesh_prim.get());
    mesh->setPrimitiveTransform(getPrimitiveTransform());

    // Add in our attributes, since we can't easily do that through
    // GT_PrimitiveBuilder::box().
    mesh->init(mesh->getFaceCountArray(), mesh->getVertexList(),
               mesh->getShared(), mesh->getVertex(), mesh->getUniform(),
               attribs);

    refiner.addPrimitive(mesh_prim);
    return true;
}

GT_PrimitiveHandle
GusdCubeWrapper::defineForRead(const UsdGeomImageable &sourcePrim,
                               UsdTimeCode time, GusdPurposeSet purposes)
{
    return new GusdCubeWrapper(UsdGeomCube(sourcePrim.GetPrim()), time,
                               purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
