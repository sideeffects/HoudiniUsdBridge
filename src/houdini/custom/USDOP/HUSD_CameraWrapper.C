/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include "HUSD_CameraWrapper.h"


#include <GT/GT_PrimitiveBuilder.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_PrimCamera.h>
#include <GT/GT_GEODetail.h>
#include <GT/GT_RefineParms.h>
#include <GEO/GEO_PrimCamera.h>
#include <GU/GU_PrimPacked.h>
#include <GU/GU_PackedDisk.h>

#include <HUSD/XUSD_Utils.h>
#include <UT/UT_BoundingBox.h>
#include <GT/GT_Refine.h>

PXR_NAMESPACE_OPEN_SCOPE

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((cameraPrimType, "Camera"))
);
ARCH_PRAGMA_POP

HUSD_CameraWrapper::HUSD_CameraWrapper(const UsdGeomCamera &usdCamera, UsdTimeCode time,
                                 GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), m_usdCamera(usdCamera)
{
}

HUSD_CameraWrapper::~HUSD_CameraWrapper() {}

const char *
HUSD_CameraWrapper::className() const
{
    return "HUSD_CameraWrapper";
}

void
HUSD_CameraWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "HUSD_CameraWrapper::enlargeBounds not implemented");
}

int
HUSD_CameraWrapper::getMotionSegments() const
{
    return 1;
}

int64
HUSD_CameraWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
HUSD_CameraWrapper::doSoftCopy() const
{
    return GT_PrimitiveHandle(new HUSD_CameraWrapper(*this));
}

bool
HUSD_CameraWrapper::isValid() const
{
    return static_cast<bool>(m_usdCamera);
}

bool
HUSD_CameraWrapper::refine(GT_Refine &refiner, const GT_RefineParms *parms) const
{
    if (!isValid())
    {
        TF_WARN("Invalid prim");
        return false;
    }
    
    GT_PrimitiveHandle h;
    
    UT_CameraParms cparms;
    HUSDgetCameraParms(m_usdCamera, m_time, cparms);    
    
    UT_Matrix4D xform;
    getPrimitiveTransform()->getMatrix(xform);
    
    GT_AttributeListHandle attribs =
        new GT_AttributeList(new GT_AttributeMap());
    
    loadPrimvars(
        *m_usdCamera.GetSchemaClassPrimDefinition(), /*prim_defn*/ 
        m_time, /*time */
        parms, /* refine parms */
        1, /* minUniform */
        0, /* minPoint */
        0, /* minVertex */
        m_usdCamera.GetPath().GetString(), /* primPath */
        nullptr, /* vertex */
        nullptr, /* point */
        &attribs, /* primitive */
        nullptr /* constant */
    );

    h = new GT_PrimCamera(xform, cparms, attribs);
        
    refiner.addPrimitive(h);

    return true;
}


GT_PrimitiveHandle
HUSD_CameraWrapper::defineForRead(const UsdGeomImageable &sourcePrim,
                               UsdTimeCode time, GusdPurposeSet purposes)
{
    return new HUSD_CameraWrapper(UsdGeomCamera(sourcePrim.GetPrim()), time,
                               purposes);
}

void
HUSD_CameraWrapper::registerForRead()
{
    static std::once_flag registered;

    std::call_once(registered, []() {
        GusdPrimWrapper::registerPrimDefinitionFuncForRead(
                _tokens->cameraPrimType, &HUSD_CameraWrapper::defineForRead);
    });
    
}

PXR_NAMESPACE_CLOSE_SCOPE
