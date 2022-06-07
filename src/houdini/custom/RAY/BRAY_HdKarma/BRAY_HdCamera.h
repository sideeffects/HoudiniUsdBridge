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
#include "BRAY_HdParam.h"
#include "BRAY_HdUtil.h"

PXR_NAMESPACE_OPEN_SCOPE

struct BRAY_HdCameraProps
{
    template <BRAY_HdUtil::EvalStyle STYLE>
    void        init(HdSceneDelegate *sd,
                    BRAY_HdParam &rparm,
                    const SdfPath &id,
                    const BRAY::OptionSet &objectProps);


    template <typename T>
    UT_Array<BRAY::OptionSet>   setProperties(BRAY::ScenePtr &s, T &obj) const;

    int         xformSegments() const { return myXform.size(); }
    int         propSegments() const;
    int         segments() const
    {
        return SYSmax(xformSegments(), propSegments());
    }

    VtValue                     myProjection;
    UT_SmallArray<GfMatrix4d>   myXform;
    UT_SmallArray<VtValue>      myFocal;
    UT_SmallArray<VtValue>      myFocusDistance;
    UT_SmallArray<VtValue>      myExposure;
    UT_SmallArray<VtValue>      myTint;
    UT_SmallArray<VtValue>      myFStop;
    UT_SmallArray<VtValue>      myScreenWindow;
    UT_SmallArray<VtValue>      myHAperture;
    UT_SmallArray<VtValue>      myVAperture;
    UT_SmallArray<VtValue>      myHOffset;
    UT_SmallArray<VtValue>      myVOffset;
};

class BRAY_HdCamera : public HdCamera
{
    using ConformPolicy = BRAY_HdParam::ConformPolicy;
public:
    BRAY_HdCamera(const SdfPath &id);
    ~BRAY_HdCamera() override;

    void	Finalize(HdRenderParam *renderParam) override final;
    void	Sync(HdSceneDelegate *sceneDelegate,
                     HdRenderParam *renderParam,
                     HdDirtyBits *dirtyBits) override final;
    HdDirtyBits	GetInitialDirtyBitsMask() const override final;

    // Update aperture for the given rendering parameters.  This needs to be
    // updated each time the image aspect ratio changes.
    void	updateAperture(HdRenderParam *rparm,
			const GfVec2i &resolution,
			bool lock_camera=true);

private:
    BRAY::CameraPtr		myCamera;

    UT_SmallArray<VtValue>	myHAperture;
    UT_SmallArray<VtValue>	myVAperture;
    SYS_HashType                myAperturesHash;
    GfVec2i			myResolution;
    BRAY_HdParam::ConformPolicy myAspectConformPolicy;
    bool			myNeedConforming;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

