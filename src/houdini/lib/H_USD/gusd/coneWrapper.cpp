/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "coneWrapper.h"

#include "cylinderWrapper.h"

#include <GT/GT_PrimTube.h>
#include <GT/GT_Refine.h>

PXR_NAMESPACE_OPEN_SCOPE

GusdConeWrapper::GusdConeWrapper(const UsdGeomCone &usdCone, UsdTimeCode time,
                                 GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), m_usdCone(usdCone)
{
}

GusdConeWrapper::~GusdConeWrapper() {}

const char *
GusdConeWrapper::className() const
{
    return "GusdConeWrapper";
}

void
GusdConeWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "GusdConeWrapper::enlargeBounds not implemented");
}

int
GusdConeWrapper::getMotionSegments() const
{
    return 1;
}

int64
GusdConeWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
GusdConeWrapper::doSoftCopy() const
{
    return GT_PrimitiveHandle(new GusdConeWrapper(*this));
}

bool
GusdConeWrapper::isValid() const
{
    return static_cast<bool>(m_usdCone);
}

bool
GusdConeWrapper::refine(GT_Refine &refiner, const GT_RefineParms *parms) const
{
    if (!isValid())
    {
        TF_WARN("Invalid prim");
        return false;
    }

    UT_Matrix4D xform = GusdBuildTubeXform(m_usdCone, m_time);
    GT_TransformHandle primXform = getPrimitiveTransform()->preMultiply(xform);

    GT_AttributeListHandle attribs =
        new GT_AttributeList(new GT_AttributeMap());
    loadPrimvars(m_usdCone.GetPrim().GetTypeName(), m_time, parms, 0, 0,
                 0, m_usdCone.GetPath().GetString(), nullptr, nullptr, nullptr,
                 &attribs);

    // Represent a cone with a tube that is fully tapered at one end.
    GT_PrimitiveHandle tube =
        new GT_PrimTube(attribs, primXform, /* taper */ 0.0, /* caps */ true);
    refiner.addPrimitive(tube);
    return true;
}

GT_PrimitiveHandle
GusdConeWrapper::defineForRead(const UsdGeomImageable &sourcePrim,
                               UsdTimeCode time, GusdPurposeSet purposes)
{
    return new GusdConeWrapper(UsdGeomCone(sourcePrim.GetPrim()), time,
                               purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
