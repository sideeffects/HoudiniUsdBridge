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
 * NAME:	HUSD_HydraLight.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for GT prim repr of a hydro geometry  prim (HdRprim)
 */
#ifndef HUSD_HydraLight_h
#define HUSD_HydraLight_h

#include <pxr/pxr.h>

#include "HUSD_API.h"
#include "HUSD_HydraPrim.h"

#include <GT/GT_Handles.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Vector3.h>
#include <SYS/SYS_Types.h>

class HUSD_Scene;

PXR_NAMESPACE_OPEN_SCOPE
class XUSD_HydraLight;
class TfToken;
class SdfPath;
PXR_NAMESPACE_CLOSE_SCOPE

/// Container for hydra camera (hdSprim)
class HUSD_API HUSD_HydraLight : public HUSD_HydraPrim
{
public:
     HUSD_HydraLight(PXR_NS::TfToken const& typeId,
		     PXR_NS::SdfPath const& primId,
		     HUSD_Scene &scene);
    ~HUSD_HydraLight() override;

    PXR_NS::XUSD_HydraLight     *hydraLight() const { return myHydraLight; }

    enum LightType
    {
	LIGHT_POINT,
	LIGHT_LINE,
	LIGHT_RECTANGLE,
	LIGHT_SPHERE,
	LIGHT_DISK,
	LIGHT_DISTANT,
	LIGHT_CYLINDER,
	LIGHT_GEOMETRY,
	LIGHT_DOME
    };
    LightType	type() const	     { return myLightType; }
    void	setType(LightType t) { myLightType = t; }

    enum Attenuation
    {
	ATTEN_NONE,
	ATTEN_HALF,
	ATTEN_PHYS
    };

    HUSD_PARM(Active,   bool)
    HUSD_PARM(Exposure, fpreal);
    HUSD_PARM(Intensity,fpreal);
    HUSD_PARM(Diffuse,  fpreal);
    HUSD_PARM(Specular, fpreal);
    HUSD_PARM(ClipNear, fpreal);
    HUSD_PARM(ClipFar,  fpreal);
    HUSD_PARM(Start,	fpreal);
    HUSD_PARM(Angle,	fpreal);
    HUSD_PARM(Softness,	fpreal);
    HUSD_PARM(Color,	UT_Vector3F);
    HUSD_PARM(AttenType,Attenuation);
    HUSD_PARM(AttenStart,fpreal);
    HUSD_PARM(AttenDist,fpreal);
    HUSD_PARM(Width,    fpreal);
    HUSD_PARM(Height,   fpreal);
    HUSD_PARM(Radius,   fpreal);
    HUSD_PARM(ProjectAngle,fpreal);
    HUSD_PARM(IsCone,   bool);
    HUSD_PARM(IsShadowed,bool);
    HUSD_PARM(HasProjectMap,bool);
    HUSD_PARM(Normalize,bool);
    HUSD_PARM(HasActiveRadius,bool);
    HUSD_PARM(ActiveRadius,fpreal);
    HUSD_PARM(TextureFile,UT_StringHolder);
    HUSD_PARM(LeftBarn,      fpreal);
    HUSD_PARM(LeftBarnEdge,  fpreal);
    HUSD_PARM(RightBarn,     fpreal);
    HUSD_PARM(RightBarnEdge, fpreal);
    HUSD_PARM(TopBarn,       fpreal);
    HUSD_PARM(TopBarnEdge,   fpreal);
    HUSD_PARM(BottomBarn,    fpreal);
    HUSD_PARM(BottomBarnEdge,fpreal);
    
    bool hasBarnDoors() const;
    
    HUSD_PARM(LightLink,UT_StringHolder);
    HUSD_PARM(ShadowLink,UT_StringHolder);
    
private:
    LightType			 myLightType;
    fpreal			 myExposure;
    fpreal			 myIntensity;
    fpreal			 myClipNear;
    fpreal			 myClipFar;
    fpreal			 myStart;
    fpreal			 myAngle;
    fpreal			 mySoftness;
    fpreal			 myDiffuse;
    fpreal			 mySpecular;
    UT_Vector3F			 myColor;
    Attenuation			 myAttenType;
    fpreal			 myAttenStart;
    fpreal			 myAttenDist;
    fpreal			 myWidth;
    fpreal			 myHeight;
    fpreal			 myRadius;
    fpreal			 myProjectAngle;
    fpreal			 myActiveRadius;
    fpreal                       myLeftBarn;
    fpreal                       myLeftBarnEdge;
    fpreal                       myRightBarn;
    fpreal                       myRightBarnEdge;
    fpreal                       myTopBarn;
    fpreal                       myTopBarnEdge;
    fpreal                       myBottomBarn;
    fpreal                       myBottomBarnEdge;
    bool			 myHasActiveRadius;
    UT_StringHolder		 myTextureFile;
    UT_StringHolder		 myLightLink;
    UT_StringHolder		 myShadowLink;
    bool			 myIsCone;
    bool			 myNormalize;
    bool			 myIsShadowed;
    bool			 myHasProjectMap;
    bool                         myActive;
    
    PXR_NS::XUSD_HydraLight     *myHydraLight;
};

#endif
