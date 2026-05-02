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
#include "XUSD_HydraInstancer.h"
#include "XUSD_HydraUtils.h"
#include "XUSD_Tokens.h"
#include "HUSD_HydraLight.h"
#include "HUSD_Scene.h"
#include "UsdHoudini/tokens.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/usdLux/tokens.h>
#include <pxr/imaging/hd/material.h>
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
    else if(typeId == HdPrimTypeTokens->meshLight)
	myLight.setType(HUSD_HydraLight::LIGHT_MESH);
    else
        myLight.setType(HUSD_HydraLight::LIGHT_UNKNOWN);
}

#define BARNDOOR(FUNC, TOKEN)                   \
    v=0.0;                                      \
    if(myLight.IsCone())                        \
        XUSD_HydraUtils::evalLightAttrib(       \
            v, del, id,HusdHdLightTokens->TOKEN); \
    myLight.FUNC(v)
            

   
void
XUSD_HydraLight::Sync(HdSceneDelegate *del,
                      HdRenderParam *render_parm,
                      HdDirtyBits *dirty_bits)
{
    if (!TF_VERIFY(del))
        return;

    UT_AutoLock alock(myLight.lock());
    
    SdfPath const &id = GetId();
    SdfPath const &inst_id = GetInstancerId();
    
    myLight.Active(del->GetVisible(id));

    const bool dirty_indices=HdChangeTracker::IsInstanceIndexDirty(*dirty_bits,
                                                                   id);
    //UTdebugPrint("Sync", id.GetText(), inst_id.GetText());
    
    // Change tracking
    HdDirtyBits bits = *dirty_bits;

    // Make sure our instancer and it's parent instancers are synced.
    _UpdateInstancer(del, dirty_bits);
    HdInstancer::_SyncInstancerAndParents(del->GetRenderIndex(), inst_id);
    
    if (bits & (DirtyTransform | DirtyParams)) 
    {
        UT_Matrix4D space(1.0);
        VtValue val = del->GetLightParamValue(id, HdLightTokens->domeOffset);
        if (val.IsHolding<GfMatrix4d>())
            space = GusdUT_Gf::Cast(val.UncheckedGet<GfMatrix4d>());
        
	myLight.Transform(XUSD_HydraUtils::fullTransform(del, id) * space);
    
    }
       
    if(HdChangeTracker::IsInstancerDirty(*dirty_bits, id) ||
        dirty_indices)
    {
	auto xinst = UTverify_cast<XUSD_HydraInstancer *>(
	    del->GetRenderIndex().GetInstancer(inst_id));
        
        myLight.ids().entries(0);
        if(xinst)
        {
            auto array =
                xinst->computeTransformsAndIDs(id, true, 0, myLight.ids(),
                                               &myLight.scene(),
                                               myLight.id(), true);
            UT_Matrix4D tr;
            const int n = array.size();
            myLight.transforms().entries(n);
            for(exint i=0; i<n; i++)
            {
                memcpy(tr.data(), array[i].GetArray(), sizeof(UT_Matrix4D));
                myLight.transforms()(i) = UT_Matrix4F(tr);
            }
            const VtValue &light_color =
                xinst->primvarValue(HusdHdLightTokens->lightColor);
            if(!light_color.IsEmpty() && light_color.GetArraySize() != 0)
            {
                UT_Vector4FArray &colors = myLight.colors();
                if(light_color.IsHolding<VtVec3fArray>())
                {
                    const VtVec3fArray &array =
                        light_color.UncheckedGet<const VtVec3fArray>();
                    const int n = array.size();
                    const int step = myLight.transforms().entries() / n;
                    if(step > 1)
                    {
                        UT_ASSERT(myLight.transforms().entries() % n == 0);
                        colors.entries(myLight.transforms().entries());
                        for(int i=0; i<n; i++)
                        {
                            auto c = UT_Vector4F(GusdUT_Gf::Cast(array[i]),1.0);
                            for(int s=0; s<step; s++)
                                colors(i + s*n) = c;
                        }
                    }
                    else
                    {
                        colors.entries(n);
                        for(int i=0; i<n; i++)
                            colors(i) = UT_Vector4F(GusdUT_Gf::Cast(array[i]),1.0);
                    }
                }
                else if(light_color.IsHolding<VtVec3dArray>())
                {
                    const VtVec3dArray &array =
                        light_color.UncheckedGet<const VtVec3dArray>();
                    const int n = array.size();
                    const int step = myLight.transforms().entries() / n;
                    if(step > 1)
                    {
                        UT_ASSERT(myLight.transforms().entries() % n == 0);
                        colors.entries(myLight.transforms().entries());
                        for(int i=0; i<n; i++)
                        {
                            auto c = UT_Vector4F(GusdUT_Gf::Cast(array[i]),1.0);
                            for(int s=0; s<step; s++)
                                colors(i + s*n) = c;
                        }
                    }
                    else
                    {
                        colors.entries(n);
                        for(int i=0; i<n; i++)
                            colors(i) = UT_Vector4F(GusdUT_Gf::Cast(array[i]),1.0);
                    }
                }
            }
            else
                myLight.colors().entries(0);
        }
        else
        {
            myLight.transforms().entries(0);
            myLight.ids().entries(0);
        }
    }

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
	    cr, del, id,HusdHdLightTokens->clippingRange);
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
	    v, del, id,HusdHdLightTokens->fogIntensity);
	myLight.FogIntensity(v);
	v=-1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id,HusdHdLightTokens->fogScatterPara);
	myLight.FogScatterPara(v);
	v=-1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id,HusdHdLightTokens->fogScatterPerp);
	myLight.FogScatterPerp(v);

	// Attenuation
	if(myLight.type() != HUSD_HydraLight::LIGHT_DISTANT &&
	   myLight.type() != HUSD_HydraLight::LIGHT_DOME)
	{
	    // Default to physical attenuation
	    HUSD_HydraLight::Attenuation atten = HUSD_HydraLight::ATTEN_PHYS;

            std::string attentype;
	    bool hastype = XUSD_HydraUtils::evalLightAttrib(attentype, del,id,
					     HusdHdLightTokens->attentype);

	    if (hastype)
	    {
		if (attentype == HusdHdLightTokens->none)
		    atten = HUSD_HydraLight::ATTEN_NONE;
		else if(attentype == HusdHdLightTokens->half)
		{
		    atten = HUSD_HydraLight::ATTEN_HALF;
		    v = 1.0;
		    XUSD_HydraUtils::evalLightAttrib(v, del,id,
					     HusdHdLightTokens->atten);
		    myLight.AttenDist(v);
		}
	    }

	    if(atten != HUSD_HydraLight::ATTEN_NONE)
	    {
		v = 0.0;
		XUSD_HydraUtils::evalLightAttrib(v, del,id,
					 HusdHdLightTokens->attenstart);
		myLight.AttenStart(v);
	    }

	    myLight.AttenType(atten);

#if 0
	    bool actrad = false;
	    XUSD_HydraUtils::evalLightAttrib(actrad, del,id,
				     HusdHdLightTokens->activeRadiusEnable);
	    if(actrad)
	    {
		v = 1.0;
		XUSD_HydraUtils::evalLightAttrib(v, del,id,
					     HusdHdLightTokens->activeRadius);
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
                                             HusdHdLightTokens->singleSided);
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
                HusdHdLightTokens->singleSided);
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

        // Dome light for Karma Physical Sky - update if the type is ever
        // changed to something other than 'light'.
        if(myLight.type() == HUSD_HydraLight::LIGHT_UNKNOWN)
        {
            bool sky_light = false;
            GfVec3f gnd_albedo = { 0.2f, 0.2f, 0.2f };
            GfVec3f gnd_color =  { 0.2f, 0.2f, 0.2f };
            fpreal32 horizon_blur = 0.5f;
            fpreal32 turbidity = 3.0f;
            // Note: These have to be float here
            fpreal32 altitude = 45.0f;
            fpreal32 azimuth = 0.0f;
            
	    if(XUSD_HydraUtils::evalLightAttrib(
                   gnd_albedo, del, id, TfToken("inputs:ground_albedo")))
            {
                sky_light = true;
            }
	    if(XUSD_HydraUtils::evalLightAttrib(
                   gnd_color, del, id, TfToken("inputs:ground_color")))
            {
                sky_light = true;
            }
	    if(XUSD_HydraUtils::evalLightAttrib(
                   horizon_blur, del, id, TfToken("inputs:horizon_blur")))
            {
                sky_light = true;
            }
	    if(XUSD_HydraUtils::evalLightAttrib(
                   turbidity, del, id, TfToken("inputs:turbidity")))
            {
                sky_light = true;
            }
	    if(XUSD_HydraUtils::evalLightAttrib(
                   altitude, del, id, TfToken("inputs:solar_altitude")))
            {
                sky_light = true;
            }
	    if(XUSD_HydraUtils::evalLightAttrib(
                   azimuth, del, id, TfToken("inputs:solar_azimuth")))
            {
                sky_light = true;
            }

            if(sky_light)
            {
                myLight.IsSky(true);
                myLight.GroundAlbedo(GusdUT_Gf::Cast(gnd_albedo));
                myLight.GroundColor(GusdUT_Gf::Cast(gnd_color));
                myLight.HorizonBlur(horizon_blur);
                myLight.Turbidity(turbidity);
                myLight.Altitude(altitude);
                myLight.Azimuth(azimuth);
            }
            else
                myLight.IsSky(false);
        }
        else
            myLight.IsSky(false);
        
	myLight.setShaderId(nullptr);
	if(myLight.type() == HUSD_HydraLight::LIGHT_UNKNOWN)
	{
	    // TODO: make this more generic, supporting other renderers
	    TfToken shaderid;
	    if(XUSD_HydraUtils::evalLightAttrib(
		    shaderid, del, id, TfToken("kma:light:shaderId")))
            {
		myLight.setShaderId(shaderid.GetText());
            }
	    else if(XUSD_HydraUtils::evalLightAttrib(
		    shaderid, del, id, UsdLuxTokens->lightShaderId))
            {
		myLight.setShaderId(shaderid.GetText());
            }
            else
            {
                HdMaterialNetwork matnet;
                VtValue           matval = del->GetMaterialResource(id);
                if (matval.IsHolding<HdMaterialNetworkMap>())
                {
                    auto netmap = matval.UncheckedGet<HdMaterialNetworkMap>();
                    matnet = netmap.map[HdMaterialTerminalTokens->light];
                    if (matnet.nodes.size() > 0)
                    {
                        shaderid = matnet.nodes[0].identifier;
                        myLight.setShaderId(shaderid.GetText());
                    }
                }
            }
            
            static UT_StringHolder portal("PortalLight");
            if(myLight.shaderId() == portal)
                myLight.setType(HUSD_HydraLight::LIGHT_PORTAL);
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
					    HusdHdLightTokens->shadowType))
	{
	    if(shadowed == HusdHdLightTokens->shadowOff)
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

        fpreal32 focus = 0.0;
        XUSD_HydraUtils::evalLightAttrib(focus, del, id,
                                         HdLightTokens->shapingFocus);
        myLight.Focus(focus);
        if(focus != 0.0)
        {
           GfVec3f ftint(1.0,1.0,1.0);
            XUSD_HydraUtils::evalLightAttrib(ftint, del, id,
                HdLightTokens->shapingFocusTint);
            myLight.FocusTint(GusdUT_Gf::Cast(ftint));
        }
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
	

    *dirty_bits = Clean;
    myLight.setInitialized();
    myLight.dirty();
}
    
HdDirtyBits
XUSD_HydraLight::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}


PXR_NAMESPACE_CLOSE_SCOPE
