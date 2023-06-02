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
 * COMMENTS:	Container for a hydra light prim (HdRprim)
 */
#include "XUSD_HydraLight.h"
#include "XUSD_HydraUtils.h"
#include "XUSD_Tokens.h"
#include "HUSD_HydraLight.h"
#include "HUSD_Scene.h"
#include "UsdHoudini/tokens.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/usdLux/tokens.h>
#include <gusd/UT_Gf.h>

#include <SYS/SYS_Math.h>
#include <UT/UT_Debug.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

XUSD_HydraLight::XUSD_HydraLight(TfToken const& typeId,
				 SdfPath const& primId,
				 HUSD_HydraLight &light)
    : HdLight(primId),
      myLight(light),
      myDirtyFlag(true)
{
    updateType(typeId);
}

XUSD_HydraLight::~XUSD_HydraLight()
{
}

void
XUSD_HydraLight::updateType(TfToken const& typeId)
{
    if(typeId == HdPrimTypeTokens->cylinderLight)
	myLight.setType(HUSD_HydraLight::LIGHT_CYLINDER);
    else if(typeId == HdPrimTypeTokens->diskLight)
	myLight.setType(HUSD_HydraLight::LIGHT_DISK);
    else if(typeId == HdPrimTypeTokens->distantLight)
	myLight.setType(HUSD_HydraLight::LIGHT_DISTANT);
    else if(typeId == HdPrimTypeTokens->domeLight)
	myLight.setType(HUSD_HydraLight::LIGHT_DOME);
    else if(typeId == HdPrimTypeTokens->rectLight)
	myLight.setType(HUSD_HydraLight::LIGHT_RECTANGLE);
    else if(typeId == HdPrimTypeTokens->sphereLight)
	myLight.setType(HUSD_HydraLight::LIGHT_SPHERE);
    else
        myLight.setType(HUSD_HydraLight::LIGHT_UNKNOWN);
}

#define BARNDOOR(FUNC, TOKEN)                   \
    v=0.0;                                      \
    if(myLight.IsCone())                        \
        XUSD_HydraUtils::evalLightAttrib(       \
            v, del, id,HusdHdLightTokens()->TOKEN); \
    myLight.FUNC(v)
            

   
void
XUSD_HydraLight::Sync(HdSceneDelegate *del,
                      HdRenderParam *renderParam,
                      HdDirtyBits *dirtyBits)
{
    if (!TF_VERIFY(del))
        return;

    UT_AutoLock alock(myLight.lock());
    
    SdfPath const &id = GetId();
    myLight.Active(del->GetVisible(id) &&
        myLight.type() != HUSD_HydraLight::LIGHT_UNKNOWN);
    
    // Change tracking
    HdDirtyBits bits = *dirtyBits;

    if (bits & DirtyTransform)
	myLight.Transform(XUSD_HydraUtils::fullTransform(del, id));

    if (bits & DirtyParams)
    {
#if 0
	UT_ArrayStringSet parms;
	UT_StringHolder path(id.GetText());
	auto handle = myLight.scene().getPrim(path);
	handle.getAttributeNames(parms);

	UTdebugPrint("Parms:\n");
	for(auto p : parms)
	    UTdebugPrint(" ", p);
#endif
	
	// Get other attributes from the USD prim through the scene delegate.
	// Then store the resulting values on this object.

	fpreal32 exp = 0.0;
	XUSD_HydraUtils::evalLightAttrib(exp, del, id, HdLightTokens->exposure);
	myLight.Exposure(exp);

	GfVec3f col;
	if(XUSD_HydraUtils::evalLightAttrib(col, del, id, HdLightTokens->color))
	    myLight.Color(GusdUT_Gf::Cast(col));
	else
	    myLight.Color(UT_Vector3F(1.0));

        bool ct = false;
        if(XUSD_HydraUtils::evalLightAttrib(ct, del, id,
                                        HdLightTokens->enableColorTemperature))
        {
	    myLight.UseColorTemp(ct);
            if(ct)
            {
                fpreal32 t = 6500.0;
                XUSD_HydraUtils::evalLightAttrib(t, del, id,
                                                HdLightTokens->colorTemperature);
                myLight.ColorTemp(t);
            }
        }
	else
	    myLight.UseColorTemp(false);
	
	fpreal32 v = 1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id, HdLightTokens->intensity);
	myLight.Intensity(v);

	v=1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id, HdLightTokens->diffuse);
	myLight.Diffuse(v);

        v = 0.05;
        XUSD_HydraUtils::evalLightAttrib(v, del, id, HdLightTokens->angle);
        myLight.DistantAngle(v);
        
 	v=1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id, HdLightTokens->specular);
	myLight.Specular(v);

	GfVec2f cr{0.001, 10000};

	XUSD_HydraUtils::evalLightAttrib(
	    cr, del, id,HusdHdLightTokens()->clippingRange);
	myLight.ClipNear(cr[0]);
	myLight.ClipFar(cr[1]);

	// Shaping
	v=180.0;
	if(XUSD_HydraUtils::evalLightAttrib(v, del, id,
					    HdLightTokens->shapingConeAngle))
	{
            v*=2.0;
	    myLight.IsCone(v < 360.0);
	    myLight.Angle(v);

	    v=0.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del, id,
					     HdLightTokens->shapingConeSoftness);
	    myLight.Softness(v);
	}
	else
	{
	    myLight.Angle(180.0);
	    myLight.Softness(0.0);
	    myLight.IsCone(false);
	}

        BARNDOOR(LeftBarn, barndoorleft);
        BARNDOOR(LeftBarnEdge, barndoorleftedge);
        BARNDOOR(RightBarn, barndoorright);
        BARNDOOR(RightBarnEdge, barndoorrightedge);
        BARNDOOR(TopBarn, barndoortop);
        BARNDOOR(TopBarnEdge, barndoortopedge);
        BARNDOOR(BottomBarn, barndoorbottom);
        BARNDOOR(BottomBarnEdge, barndoorbottomedge);

        // Fog parms
	v=-1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id,HusdHdLightTokens()->fogIntensity);
	myLight.FogIntensity(v);
	v=-1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id,HusdHdLightTokens()->fogScatterPara);
	myLight.FogScatterPara(v);
	v=-1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id,HusdHdLightTokens()->fogScatterPerp);
	myLight.FogScatterPerp(v);
        
	// Attenuation
	if(myLight.type() != HUSD_HydraLight::LIGHT_DISTANT &&
	   myLight.type() != HUSD_HydraLight::LIGHT_DOME)
	{
	    // Default to physical attenuation
	    HUSD_HydraLight::Attenuation atten = HUSD_HydraLight::ATTEN_PHYS;

            std::string attentype;
	    bool hastype = XUSD_HydraUtils::evalLightAttrib(attentype, del,id,
					     HusdHdLightTokens()->attentype);

	    if (hastype)
	    {
		if (attentype == HusdHdLightTokens()->none)
		    atten = HUSD_HydraLight::ATTEN_NONE;
		else if(attentype == HusdHdLightTokens()->halfDistance)
		{
		    atten = HUSD_HydraLight::ATTEN_HALF;
		    v = 1.0;
		    XUSD_HydraUtils::evalLightAttrib(v, del,id,
					     HusdHdLightTokens()->atten);
		    myLight.AttenDist(v);
		}
	    }

	    if(atten != HUSD_HydraLight::ATTEN_NONE)
	    {
		v = 0.0;
		XUSD_HydraUtils::evalLightAttrib(v, del,id,
					 HusdHdLightTokens()->attenstart);
		myLight.AttenStart(v);
	    }

	    myLight.AttenType(atten);

#if 0
	    bool actrad = false;
	    XUSD_HydraUtils::evalLightAttrib(actrad, del,id,
				     HusdHdLightTokens()->activeRadiusEnable);
	    if(actrad)
	    {
		v = 1.0;
		XUSD_HydraUtils::evalLightAttrib(v, del,id,
					     HusdHdLightTokens()->activeRadius);
		myLight.HasActiveRadius(true);
		myLight.ActiveRadius(v);
	    }
	    else
#endif
		myLight.HasActiveRadius(false);

	}
	// specific light parms
	if(myLight.type() == HUSD_HydraLight::LIGHT_RECTANGLE)
	{
	    v = 1.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id,
                HdLightTokens->width);
	    myLight.Width(v);
	    v = 1.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id,
                HdLightTokens->height);
	    myLight.Height(v);

            bool single = true;
	    XUSD_HydraUtils::evalLightAttrib(single, del,id,
                HusdHdLightTokens()->singleSided);
            myLight.SingleSided(single);
	}
	
	if(myLight.type() == HUSD_HydraLight::LIGHT_SPHERE ||
	   myLight.type() == HUSD_HydraLight::LIGHT_POINT)
	{
	    bool pnt = false;
	    XUSD_HydraUtils::evalLightAttrib(pnt, del,id,
                UsdLuxTokens->treatAsPoint);
	    if(pnt)
	    {
		myLight.setType(HUSD_HydraLight::LIGHT_POINT);
	    }
	    else
	    {
		myLight.setType(HUSD_HydraLight::LIGHT_SPHERE);
		v = 1.0;
		XUSD_HydraUtils::evalLightAttrib(v, del,id,
                    HdLightTokens->radius);
		myLight.Radius(v);
	    }

	}
	
	if(myLight.type() == HUSD_HydraLight::LIGHT_CYLINDER ||
	   myLight.type() == HUSD_HydraLight::LIGHT_LINE)
	{
	    bool pnt = false;
	    XUSD_HydraUtils::evalLightAttrib(pnt, del,id,
                UsdLuxTokens->treatAsLine);
	    if(pnt)
		myLight.setType(HUSD_HydraLight::LIGHT_LINE);
	    else
	    {
		myLight.setType(HUSD_HydraLight::LIGHT_CYLINDER);
		
		v = 1.0;
		XUSD_HydraUtils::evalLightAttrib(v, del,id,
                    HdLightTokens->radius);
		myLight.Radius(v);

		// TODO: natively support tube lights.
		myLight.AttenStart( myLight.AttenStart() + v );
	    }
	    
	    v = 1.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id,
                HdLightTokens->length);
	    myLight.Width(v);
	}
	
	if(myLight.type() == HUSD_HydraLight::LIGHT_DISK)
	{
	    v = 1.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id,
                HdLightTokens->radius);
	    myLight.Radius(v);
            
            bool single = true;
	    XUSD_HydraUtils::evalLightAttrib(single, del,id,
                HusdHdLightTokens()->singleSided);
            myLight.SingleSided(single);
	}

	if(myLight.type() == HUSD_HydraLight::LIGHT_SPHERE ||
	   myLight.type() == HUSD_HydraLight::LIGHT_RECTANGLE ||
	   myLight.type() == HUSD_HydraLight::LIGHT_CYLINDER ||
	   myLight.type() == HUSD_HydraLight::LIGHT_DISK ||
	   myLight.type() == HUSD_HydraLight::LIGHT_DISTANT)
	{
	    bool norm = false;
	    XUSD_HydraUtils::evalLightAttrib(norm, del,id,
                HdLightTokens->normalize);
	    myLight.Normalize(norm);
	}


	SdfAssetPath texpath;
	if(XUSD_HydraUtils::evalLightAttrib(texpath, del,id,
	        HdLightTokens->textureFile))
	{
	    myLight.HasProjectMap(true);
	    myLight.TextureFile(texpath.GetResolvedPath());
	    if (!myLight.TextureFile().isstring())
	        myLight.TextureFile(texpath.GetAssetPath());

	    v = 45.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id,
                HdLightTokens->shapingConeAngle);
	    myLight.ProjectAngle(v);
	}
	else
	    myLight.HasProjectMap(false);


	bool is_shadow = true;
#if 0
	TfToken shadowed;
	if(XUSD_HydraUtils::evalLightAttrib(shadowed, del,id,
					    HusdHdLightTokens()->shadowType))
	{
	    if(shadowed == HusdHdLightTokens()->shadowOff)
		is_shadow = false;
	}
#endif
	myLight.IsShadowed(is_shadow);

        bool in_menu = true;
        XUSD_HydraUtils::evalLightAttrib(in_menu, del, id,
            UsdHoudiniTokens->houdiniInviewermenu);
        if(in_menu != myLight.ShowInMenu())
        {
            myLight.ShowInMenu(in_menu);
            myLight.scene().dirtyLightNames();
        }
        fpreal32 scale = 1.0;
        XUSD_HydraUtils::evalLightAttrib(scale, del, id,
            UsdHoudiniTokens->houdiniGuidescale);
        myLight.GuideScale(scale);
    }
    
    if (bits & DirtyCollection)
    {
	VtValue val = del->GetLightParamValue(id, HdTokens->lightLink);
	if (!val.IsEmpty() && val.IsHolding<TfToken>())
        {
            UT_StringHolder link = val.UncheckedGet<TfToken>().GetText();
            if(link != myLightLink)
            {
                myLight.scene().addCategory(link, HUSD_Scene::CATEGORY_LIGHT);
                myLight.scene().removeCategory(myLightLink,
                                               HUSD_Scene::CATEGORY_LIGHT);
                myLightLink = link;
                myLight.LightLink(link);
            }
        }
        else
            myLight.LightLink(UT_StringHolder());

	val = del->GetLightParamValue(id, HdTokens->shadowLink);
	if (!val.IsEmpty() && val.IsHolding<TfToken>())
        {
            UT_StringHolder link = val.UncheckedGet<TfToken>().GetText();
            if(link != myShadowLink)
            {
                myLight.scene().addCategory(link, HUSD_Scene::CATEGORY_SHADOW);
                myLight.scene().removeCategory(myShadowLink,
                                               HUSD_Scene::CATEGORY_SHADOW);
                myShadowLink = link;
                myLight.ShadowLink(link);
            }
        }
        else
            myLight.ShadowLink(UT_StringHolder());
    }

    if(bits)
    {
	myDirtyFlag = true;
	myLight.bumpVersion();
    }
	

    *dirtyBits = Clean;
    myLight.setInitialized();
    myLight.dirty();
}
    
HdDirtyBits
XUSD_HydraLight::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}


PXR_NAMESPACE_CLOSE_SCOPE
