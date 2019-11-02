
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
#include "HUSD_HydraLight.h"
#include "HUSD_Scene.h"
#include "XUSD_HydraLight.h"

#include <GT/GT_Primitive.h>

using namespace UT::Literal;

HUSD_HydraLight::HUSD_HydraLight(PXR_NS::TfToken const& typeId,
				 PXR_NS::SdfPath const& primId,
				 HUSD_Scene &scene)
    : HUSD_HydraPrim(scene, primId.GetText()),
      myLightType(LIGHT_POINT),
      myExposure(0.0),
      myIntensity(1.0),
      myClipNear(0.1),
      myClipFar(10000),
      myStart(0.0),
      myAngle(180.0),
      mySoftness(0.0),
      myDiffuse(1.0),
      mySpecular(1.0),
      myColor(1.0, 1.0, 1.0),
      myAttenType(ATTEN_PHYS),
      myAttenStart(0.0),
      myAttenDist(1.0),
      myWidth(1.0),
      myHeight(1.0),
      myRadius(1.0),
      myProjectAngle(45.0),
      myActiveRadius(1.0),
      myHasActiveRadius(false),
      myIsCone(false),
      myIsShadowed(false),
      myNormalize(true),
      myHasProjectMap(false),
      myActive(false),
      myLeftBarn(0.0),
      myLeftBarnEdge(0.0),
      myRightBarn(0.0),
      myRightBarnEdge(0.0),
      myTopBarn(0.0),
      myTopBarnEdge(0.0),
      myBottomBarn(0.0),
      myBottomBarnEdge(0.0)
{
    myHydraLight = new PXR_NS::XUSD_HydraLight(typeId, primId, *this);
}

HUSD_HydraLight::~HUSD_HydraLight()
{
    delete myHydraLight;
}

bool
HUSD_HydraLight::hasBarnDoors() const
{
    return (myLeftBarn > 0.0  ||
            myLeftBarnEdge > 0.0 ||
            myRightBarn > 0.0 ||
            myRightBarnEdge > 0.0 ||
            myTopBarn > 0.0 ||
            myTopBarnEdge > 0.0 ||
            myBottomBarn > 0.0 ||
            myBottomBarnEdge > 0.0);
}

