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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraCamera.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for a hydra scene prim (HdRprim), light or camera
 */
#ifndef XUSD_HydraCamera_h
#define XUSD_HydraCamera_h

#include <pxr/pxr.h>
#include <pxr/imaging/hd/camera.h>
#include <SYS/SYS_Types.h>
#include <GT/GT_Handles.h>

class HUSD_HydraCamera;

PXR_NAMESPACE_OPEN_SCOPE

/// Container for a hydra scene prim (HdSprim) representing a camera
class XUSD_HydraCamera : public HdCamera
{
public:
	     XUSD_HydraCamera(TfToken const& typeId,
			      SdfPath const& primId,
			      HUSD_HydraCamera &cam);
            ~XUSD_HydraCamera() override;

    void Sync(HdSceneDelegate *sceneDelegate,
              HdRenderParam *renderParam,
              HdDirtyBits *dirtyBits) override;
    
protected:
    HdDirtyBits GetInitialDirtyBitsMask() const override;
   
private:
    HUSD_HydraCamera	&myCamera;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
