/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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

#include <GT/GT_Primitive.h>

HUSD_HydraCamera::HUSD_HydraCamera(PXR_NS::TfToken const& typeId,
				   PXR_NS::SdfPath const& primId,
				   HUSD_Scene &scene)
    : HUSD_HydraPrim(scene, primId.GetText()),
      myAperture(41.4214),
      myAspectRatio(1.0),
      myFocusDistance(50.0),
      myFocalLength(50.0),
      myNearClip(0.1),
      myFarClip(10000.0),
      myApertureOffsets(0.0, 0.0)
{
    myHydraCamera = new PXR_NS::XUSD_HydraCamera(typeId, primId, *this);
}

HUSD_HydraCamera::~HUSD_HydraCamera()
{
    delete myHydraCamera;
}
