
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
#include "HUSD_HydraLight.h"
#include "HUSD_Scene.h"
#include "XUSD_HydraLight.h"
#include "HUSD_Path.h"

#include <GT/GT_Primitive.h>

using namespace UT::Literal;

HUSD_HydraLight::HUSD_HydraLight(PXR_NS::TfToken const& typeId,
				 PXR_NS::SdfPath const& primId,
				 HUSD_Scene &scene)
    : HUSD_HydraPrim(scene, primId),
      myShaderId()
{
    myHydraLight = UTmakeUnique<PXR_NS::XUSD_HydraLight>(typeId, primId, *this);
}

HUSD_HydraLight::~HUSD_HydraLight()
{
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

