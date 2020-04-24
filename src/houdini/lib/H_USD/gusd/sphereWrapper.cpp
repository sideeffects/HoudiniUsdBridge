/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "sphereWrapper.h"

#include <GT/GT_PrimSphere.h>
#include <GT/GT_Refine.h>

PXR_NAMESPACE_OPEN_SCOPE

GusdSphereWrapper::GusdSphereWrapper(const UsdGeomSphere &usdSphere,
                                     UsdTimeCode time, GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), m_usdSphere(usdSphere)
{
}

GusdSphereWrapper::~GusdSphereWrapper()
{
}

const char *
GusdSphereWrapper::className() const
{
    return "GusdSphereWrapper";
}

void
GusdSphereWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "GusdSphereWrapper::enlargeBounds not implemented");
}

int
GusdSphereWrapper::getMotionSegments() const
{
    return 1;
}

int64
GusdSphereWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
GusdSphereWrapper::doSoftCopy() const
{
    return GT_PrimitiveHandle(new GusdSphereWrapper( *this ));
}

bool
GusdSphereWrapper::isValid() const
{
    return static_cast<bool>(m_usdSphere);
}

bool
GusdSphereWrapper::refine(GT_Refine &refiner, const GT_RefineParms *parms) const
{
    if (!isValid())
    {
        TF_WARN("Invalid prim");
        return false;
    }

    GT_TransformHandle primXform = getPrimitiveTransform();
    UsdAttribute radiusAttr = m_usdSphere.GetRadiusAttr();
    if (radiusAttr)
    {
        // Houdini spheres have a radius of 1 along with a transform, so the
        // radius attribute needs to be applied to the prim transform.
        double radius = 0;
        if (radiusAttr.Get(&radius, m_time))
        {
            UT_Matrix4D sphereXform(1.0);
            sphereXform.scale(radius);
            primXform = primXform->preMultiply(sphereXform);
        }
    }

    GT_AttributeListHandle attribs =
        new GT_AttributeList(new GT_AttributeMap());
    loadPrimvars(*m_usdSphere.GetSchemaClassPrimDefinition(), m_time, parms, 0,
                 0, 0, m_usdSphere.GetPath().GetString(), nullptr, nullptr,
                 nullptr, &attribs);

    GT_PrimitiveHandle sphere = new GT_PrimSphere(attribs, primXform);
    refiner.addPrimitive(sphere);
    return true;
}

GT_PrimitiveHandle
GusdSphereWrapper::defineForRead(const UsdGeomImageable &sourcePrim,
                                 UsdTimeCode time, GusdPurposeSet purposes)
{
    return new GusdSphereWrapper(UsdGeomSphere(sourcePrim.GetPrim()), time,
                                 purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
