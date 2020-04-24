/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "cylinderWrapper.h"

#include <GT/GT_PrimTube.h>
#include <GT/GT_Refine.h>

#include <pxr/usd/usdGeom/cone.h>

PXR_NAMESPACE_OPEN_SCOPE

template <typename ConeOrCylinder>
UT_Matrix4D
GusdBuildTubeXform(const ConeOrCylinder &prim, UsdTimeCode time)
{
    UsdAttribute axisAttr = prim.GetAxisAttr();
    TfToken axis = UsdGeomTokens->z;
    if (axisAttr)
        TF_VERIFY(axisAttr.Get(&axis, time));

    UsdAttribute radiusAttr = prim.GetRadiusAttr();
    double radius = 1.0;
    if (radiusAttr)
        TF_VERIFY(radiusAttr.Get(&radius, time));

    UsdAttribute heightAttr = prim.GetHeightAttr();
    double height = 2.0;
    if (heightAttr)
        TF_VERIFY(heightAttr.Get(&height, time));

    UT_Matrix4D xform(1.0);
    // GT tubes are aligned along Z, but reversed (and the direction matters
    // when the tube is configured to represent a cone).
    xform.rotateHalf<UT_Axis3::XAXIS>();

    int primary_axis;
    if (axis == UsdGeomTokens->x)
    {
        xform.rotateQuarter<UT_Axis3::YAXIS, false>();
        primary_axis = 0;
    }
    else if (axis == UsdGeomTokens->y)
    {
        xform.rotateQuarter<UT_Axis3::XAXIS, true>();
        primary_axis = 1;
    }
    else if (axis == UsdGeomTokens->z)
    {
        // GT tubes are already aligned along Z.
        primary_axis = 2;
    }
    else
    {
        TF_WARN("Invalid axis");
        return UT_Matrix4D::getIdentityMatrix();
    }

    UT_Vector3D scales(radius, radius, radius);
    scales[primary_axis] = height;
    xform.scale(scales);

    return xform;
}

// Explicit instantiations.
template UT_Matrix4D
GusdBuildTubeXform<UsdGeomCone>(const UsdGeomCone &, UsdTimeCode);
template UT_Matrix4D
GusdBuildTubeXform<UsdGeomCylinder>(const UsdGeomCylinder &, UsdTimeCode);

GusdCylinderWrapper::GusdCylinderWrapper(const UsdGeomCylinder &usdCylinder,
                                         UsdTimeCode time,
                                         GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), m_usdCylinder(usdCylinder)
{
}

GusdCylinderWrapper::~GusdCylinderWrapper() {}

const char *
GusdCylinderWrapper::className() const
{
    return "GusdCylinderWrapper";
}

void
GusdCylinderWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "GusdCylinderWrapper::enlargeBounds not implemented");
}

int
GusdCylinderWrapper::getMotionSegments() const
{
    return 1;
}

int64
GusdCylinderWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
GusdCylinderWrapper::doSoftCopy() const
{
    return GT_PrimitiveHandle(new GusdCylinderWrapper(*this));
}

bool
GusdCylinderWrapper::isValid() const
{
    return static_cast<bool>(m_usdCylinder);
}

bool
GusdCylinderWrapper::refine(GT_Refine &refiner,
                            const GT_RefineParms *parms) const
{
    if (!isValid())
    {
        TF_WARN("Invalid prim");
        return false;
    }

    UT_Matrix4D xform = GusdBuildTubeXform(m_usdCylinder, m_time);
    GT_TransformHandle primXform = getPrimitiveTransform()->preMultiply(xform);

    GT_AttributeListHandle attribs =
        new GT_AttributeList(new GT_AttributeMap());
    loadPrimvars(*m_usdCylinder.GetSchemaClassPrimDefinition(), m_time, parms,
                 0, 0, 0, m_usdCylinder.GetPath().GetString(), nullptr, nullptr,
                 nullptr, &attribs);

    GT_PrimitiveHandle tube =
        new GT_PrimTube(attribs, primXform, /* taper */ 1.0, /* caps */ true);
    refiner.addPrimitive(tube);
    return true;
}

GT_PrimitiveHandle
GusdCylinderWrapper::defineForRead(const UsdGeomImageable &sourcePrim,
                                   UsdTimeCode time, GusdPurposeSet purposes)
{
    return new GusdCylinderWrapper(UsdGeomCylinder(sourcePrim.GetPrim()), time,
                                   purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
