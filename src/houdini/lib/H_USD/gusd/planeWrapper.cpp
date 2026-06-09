/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "planeWrapper.h"

#include <GT/GT_PrimPlane.h>
#include <GT/GT_Refine.h>

PXR_NAMESPACE_OPEN_SCOPE

GusdPlaneWrapper::GusdPlaneWrapper(const UsdGeomPlane &usd_plane,
                                   UsdTimeCode time, GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), myUsdPlane(usd_plane)
{
}

GusdPlaneWrapper::~GusdPlaneWrapper() = default;

const char *
GusdPlaneWrapper::className() const
{
    return "GusdPlaneWrapper";
}

void
GusdPlaneWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "GusdPlaneWrapper::enlargeBounds not implemented");
}

int
GusdPlaneWrapper::getMotionSegments() const
{
    return 1;
}

int64
GusdPlaneWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
GusdPlaneWrapper::doSoftCopy() const
{
    return GT_PrimitiveHandle(new GusdPlaneWrapper( *this ));
}

bool
GusdPlaneWrapper::isValid() const
{
    return static_cast<bool>(myUsdPlane);
}

bool
GusdPlaneWrapper::refine(GT_Refine &refiner, const GT_RefineParms *parms) const
{
    if (!isValid())
        return false;

    UsdAttribute axis_attr = myUsdPlane.GetAxisAttr();
    TfToken axis = UsdGeomTokens->z;
    if (axis_attr)
        TF_VERIFY(axis_attr.Get(&axis, m_time));

    UsdAttribute length_attr = myUsdPlane.GetLengthAttr();
    double length = 2.0;
    if (length_attr)
        TF_VERIFY(length_attr.Get(&length, m_time));

    UsdAttribute width_attr = myUsdPlane.GetWidthAttr();
    double width = 2.0;
    if (width_attr)
        TF_VERIFY(width_attr.Get(&width, m_time));
   
    UT_Matrix4D xform(1.0);
    xform.scale(0.5 * width, 0.5 * length, 0.25 * (width + length));

    // Note we have extra rotations here to match how UsdGeomPlane orients the
    // grid's width/length depending on the axis.
    if (axis == UsdGeomTokens->x)
    {
        xform.rotateQuarter<UT_Axis3::YAXIS, false>();
        xform.rotateQuarter<UT_Axis3::XAXIS, true>();
    }
    else if (axis == UsdGeomTokens->y)
    {
        xform.rotateQuarter<UT_Axis3::XAXIS, true>();
        xform.rotateQuarter<UT_Axis3::YAXIS, false>();
    }

    UT_Matrix4D prim_xform;
    getPrimitiveTransform()->getMatrix(prim_xform);

    auto attribs = UTmakeIntrusive<GT_AttributeList>(
            UTmakeIntrusive<GT_AttributeMap>());
    
    loadPrimvars(
            *myUsdPlane.GetSchemaClassPrimDefinition(), m_time, parms, 1, 0, 0,
            myUsdPlane.GetPath().GetString(), nullptr, nullptr, &attribs,
            nullptr);

    auto plane = UTmakeIntrusive<GT_PrimPlane>(xform * prim_xform, 
                                               attribs);
    refiner.addPrimitive(plane);
    return true;
}

GT_PrimitiveHandle
GusdPlaneWrapper::defineForRead(const UsdGeomImageable &source_prim,
                                UsdTimeCode time, GusdPurposeSet purposes)
{
    return new GusdPlaneWrapper(UsdGeomPlane(source_prim.GetPrim()), time,
                                purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
