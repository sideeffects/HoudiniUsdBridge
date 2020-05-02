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
 * NAME:	XUSD_HydraLight.h (HUSD Library, C++)
 *
 * COMMENTS:	A hydra light prim (HdSprim)
 */
#ifndef XUSD_HydraLight_h
#define XUSD_HydraLight_h

#include <pxr/pxr.h>
#include <pxr/imaging/hd/light.h>
#include <UT/UT_StringHolder.h>

class HUSD_HydraLight;

PXR_NAMESPACE_OPEN_SCOPE
class UsdTimeCode;
class TfToken;

class XUSD_HydraLight : public HdLight
{
public:
	     XUSD_HydraLight(TfToken const& typeId,
			     SdfPath const& primId,
			     HUSD_HydraLight &light);
            ~XUSD_HydraLight() override;
    
    void         Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam *renderParam,
                      HdDirtyBits *dirtyBits) override;

    void         updateType(TfToken const& typeId);
protected:
    HdDirtyBits GetInitialDirtyBitsMask() const override;

private:
    HUSD_HydraLight &myLight;
    UT_StringHolder  myLightLink;
    UT_StringHolder  myShadowLink;
    bool	     myDirtyFlag;
};

PXR_NAMESPACE_CLOSE_SCOPE
#endif
