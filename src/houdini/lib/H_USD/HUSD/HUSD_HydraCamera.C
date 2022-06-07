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
 * NAME:	HUSD_HydraGeoPrim.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for GT prim repr of a hydro geometry (R) prim
 */
#include "HUSD_HydraCamera.h"
#include "XUSD_HydraCamera.h"
#include "HUSD_Path.h"

#include <GT/GT_Primitive.h>

HUSD_HydraCamera::HUSD_HydraCamera(PXR_NS::TfToken const& typeId,
				   PXR_NS::SdfPath const& primId,
				   HUSD_Scene &scene)
    : HUSD_HydraPrim(scene, HUSD_Path(primId).pathStr()),
      myApertureW(41.4214),
      myApertureH(23.3),
      myExposure(0.0),
      myFocusDistance(50.0),
      myFocalLength(50.0),
      myFStop(0.0),
      myNearClip(0.1),
      myFarClip(10000.0),
      myApertureOffsets(0.0, 0.0),
      myShowInMenu(true),
      myIsDirty(true),
      myGuideScale(1.0),
      myProjection(HUSD_HydraCamera::ProjectionType::Perspective)
{
    myHydraCamera = new PXR_NS::XUSD_HydraCamera(typeId, primId, *this);
}

HUSD_HydraCamera::~HUSD_HydraCamera()
{
    delete myHydraCamera;
}
