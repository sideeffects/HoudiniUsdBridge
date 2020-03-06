/*
 * Copyright 2019 Side Effects Software Inc.
 *
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
 *
 * Produced by:
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#ifndef __BRAY_HdCamera__
#define __BRAY_HdCamera__

#include <pxr/pxr.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/matrix4f.h>
#include <BRAY/BRAY_Interface.h>
#include <UT/UT_SmallArray.h>

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdCamera : public HdCamera
{
public:
    BRAY_HdCamera(const SdfPath &id);
    virtual ~BRAY_HdCamera();

    virtual void	Finalize(HdRenderParam *renderParam) override final;
    virtual void	Sync(HdSceneDelegate *sceneDelegate,
				HdRenderParam *renderParam,
				HdDirtyBits *dirtyBits) override final;
    virtual HdDirtyBits	GetInitialDirtyBitsMask() const override final;

    // Update aperture for the given rendering parameters.  This needs to be
    // updated each time the image aspect ratio changes.
    void	updateAperture(HdRenderParam *rparm,
			const GfVec2i &resolution,
			bool lock_camera=true);

private:
    BRAY::CameraPtr		myCamera;

    UT_SmallArray<VtValue>	myHAperture;
    UT_SmallArray<VtValue>	myVAperture;
    GfVec2i			myResolution;
    bool			myNeedConforming;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

