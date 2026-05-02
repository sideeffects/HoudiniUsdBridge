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

#ifndef __HUSD_CameraWrapper__
#define __HUSD_CameraWrapper__

#include <gusd/primWrapper.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/camera.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a USD camera prim and refines it to a GT mesh for the viewport or
/// conversion back to GU primitives.
class HUSD_CameraWrapper : public GusdPrimWrapper
{
public:
    HUSD_CameraWrapper(const UsdGeomCamera &usdCamera, UsdTimeCode t,
                    GusdPurposeSet purposes);

    ~HUSD_CameraWrapper() override;

    const UsdGeomImageable getUsdPrim() const override
    {
        return m_usdCamera;
    }

    static void registerForRead();

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

    UsdGeomCamera m_usdCamera;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
