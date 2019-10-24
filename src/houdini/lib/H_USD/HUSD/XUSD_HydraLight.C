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
 * NAME:	XUSD_HydraLight.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for a hydra light prim (HdRprim)
 */
#include "XUSD_HydraLight.h"
#include "XUSD_HydraUtils.h"
#include "XUSD_Tokens.h"

#include "HUSD_HydraLight.h"
#include "HUSD_Scene.h"

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
    else if(typeId == HusdHdPrimTypeTokens()->sprimGeometryLight)
	myLight.setType(HUSD_HydraLight::LIGHT_GEOMETRY);
}

XUSD_HydraLight::~XUSD_HydraLight()
{
}
   
void
XUSD_HydraLight::Sync(HdSceneDelegate *del,
                      HdRenderParam *renderParam,
                      HdDirtyBits *dirtyBits)
{
    if (!TF_VERIFY(del))
        return;

    UT_AutoLock alock(myLight.lock());
    
    SdfPath const &id = GetId();
    myLight.Active(del->GetVisible(id));
    
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
	
	fpreal32 v = 1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id, HdLightTokens->intensity);
	myLight.Intensity(v);

	v=1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id, HusdHdLightTokens()->diffuse);
	myLight.Diffuse(v);
	
	v=1.0;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id,HusdHdLightTokens()->specular);
	myLight.Specular(v);

	v=0.1;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id,HusdHdLightTokens()->clipNear);
	myLight.ClipNear(v);
	
	v=10000;
	XUSD_HydraUtils::evalLightAttrib(
	    v, del, id,HusdHdLightTokens()->clipFar);
	myLight.ClipFar(v);

	// Shaping
	v=180.0;
	if(XUSD_HydraUtils::evalLightAttrib(v, del, id,
					    HusdHdLightTokens()->coneAngle))
	{
            v*=2.0;
	    myLight.IsCone(v < 360.0);
	    myLight.Angle(v);

	    v=0.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del, id,
					     HusdHdLightTokens()->coneSoftness);
	    myLight.Softness(v);
	}
	else
	{
	    myLight.Angle(180.0);
	    myLight.Softness(0.0);
	    myLight.IsCone(false);
	}

	// Attenuation
	if(myLight.type() != HUSD_HydraLight::LIGHT_DISTANT &&
	   myLight.type() != HUSD_HydraLight::LIGHT_DOME)
	{
	    // Default to physical attenuation
	    HUSD_HydraLight::Attenuation atten = HUSD_HydraLight::ATTEN_PHYS;

	    TfToken attentype;
	    bool hastype = XUSD_HydraUtils::evalLightAttrib(attentype, del,id,
					     HusdHdLightTokens()->attenType);

	    if (hastype)
	    {
		if (attentype == HusdHdLightTokens()->none)
		    atten = HUSD_HydraLight::ATTEN_NONE;
		else if(attentype == HusdHdLightTokens()->halfDistance)
		{
		    atten = HUSD_HydraLight::ATTEN_HALF;
		    v = 1.0;
		    XUSD_HydraUtils::evalLightAttrib(v, del,id,
					     HusdHdLightTokens()->attenDist);
		    myLight.AttenDist(v);
		}
	    }

	    if(atten != HUSD_HydraLight::ATTEN_NONE)
	    {
		v = 0.0;
		XUSD_HydraUtils::evalLightAttrib(v, del,id,
					 HusdHdLightTokens()->attenStart);
		myLight.AttenStart(v);
	    }

	    myLight.AttenType(atten);

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
		myLight.HasActiveRadius(false);

	}
	// specific light parms
	if(myLight.type() == HUSD_HydraLight::LIGHT_RECTANGLE)
	{
	    v = 1.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id,
					     UsdLuxTokens->width);
	    myLight.Width(v);
	    v = 1.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id,
					     UsdLuxTokens->height);
	    myLight.Height(v);
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
						 UsdLuxTokens->radius);
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
						 UsdLuxTokens->radius);
		myLight.Radius(v);

		// TODO: natively support tube lights.
		myLight.AttenStart( myLight.AttenStart() + v );
	    }
	    
	    v = 1.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id,
					     UsdLuxTokens->length);
	    myLight.Width(v);
	}
	
	if(myLight.type() == HUSD_HydraLight::LIGHT_DISK)
	{
	    v = 1.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id, UsdLuxTokens->radius);
	    myLight.Radius(v);
	}

	if(myLight.type() == HUSD_HydraLight::LIGHT_SPHERE ||
	   myLight.type() == HUSD_HydraLight::LIGHT_RECTANGLE ||
	   myLight.type() == HUSD_HydraLight::LIGHT_CYLINDER ||
	   myLight.type() == HUSD_HydraLight::LIGHT_DISK ||
	   myLight.type() == HUSD_HydraLight::LIGHT_GEOMETRY)
	{
	    bool norm = false;
	    XUSD_HydraUtils::evalLightAttrib(norm, del,id,
					     UsdLuxTokens->normalize);
	    myLight.Normalize(norm);
	}

	if(myLight.type() == HUSD_HydraLight::LIGHT_GEOMETRY)
	{
	    UT_StringHolder path;
#if 0
	    // TMP: until we can query relationships through Hydra
	    // NOTE: this doesn't work either.
	    UT_StringHolder rpath(id.GetText());
	    auto handle = myLight.scene().getPrim(rpath);
	    UT_StringHolder geotk(UsdLuxTokens->geometry.GetText());
	    UT_StringArray paths;
	    if(handle.getRelationships(geotk, paths) &&
	       paths.entries() > 0)
	    {
		path = paths(0);
		if(path.isstring())
		{
		    auto ghandle = myLight.scene().getPrim(path);
		    UT_ArrayStringSet names;
		    UT_Options attribs;
		    ghandle.extractAttributes(names, HUSD_TimeCode(), attribs);
		    attribs.saveAsJSON("foo", std::cerr, false);
		    
		    SdfPath sdfpath(path.toStdString());
		    auto range = del->GetExtent(sdfpath);
		    if(!range.IsEmpty())
		    {
			auto size = range.GetSize();
			myLight.Width(size[0]);
			myLight.Height(size[1]);
			myLight.Radius(size[2]);
		    }
		    else
		    {
			myLight.Width(1.0);
			myLight.Height(1.0);
			myLight.Radius(1.0);
		    }
		}
	    }
	    // #else
	    // This doesn't actually work since Get() ignores relationships.
	    SdfAssetPath geo_path;
	    if(XUSD_HydraUtils::evalLightAttrib(geo_path, del,id,
						UsdLuxTokens->geometry))
	    {
		path = geo_path.GetAssetPath();
		if(path.isstring())
		{
		    SdfPath sdfpath(path.toStdString());
		    auto range = del->GetExtent(sdfpath);
		    if(!range.IsEmpty())
		    {
			auto size = range.GetSize();
			myLight.Width(size[0]);
			myLight.Height(size[1]);
			myLight.Radius(size[2]);
		    }
		    else
		    {
			myLight.Width(1.0);
			myLight.Height(1.0);
			myLight.Radius(1.0);
		    }
		}
	    }
#endif
	}
	    

	SdfAssetPath texpath;
	if(XUSD_HydraUtils::evalLightAttrib(texpath, del,id,
					    UsdLuxTokens->textureFile))
	{
	    myLight.TextureFile(texpath.GetAssetPath());
	}

	if(XUSD_HydraUtils::evalLightAttrib(texpath, del,id,
					    HusdHdLightTokens()->projectMap))
	{
	    myLight.HasProjectMap(true);
	    myLight.TextureFile(texpath.GetAssetPath());
	    
	    v = 45.0;
	    XUSD_HydraUtils::evalLightAttrib(v, del,id,
					     HusdHdLightTokens()->projectAngle);
	    myLight.ProjectAngle(v);
	}
	else
	    myLight.HasProjectMap(false);


	bool is_shadow = true;
	TfToken shadowed;
	if(XUSD_HydraUtils::evalLightAttrib(shadowed, del,id,
					    HusdHdLightTokens()->shadowType))
	{
	    if(shadowed == HusdHdLightTokens()->shadowOff)
		is_shadow = false;
	}
	myLight.IsShadowed(is_shadow);
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
                myLight.scene().removeCategory(myLightLink,
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
}
    
HdDirtyBits
XUSD_HydraLight::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}


PXR_NAMESPACE_CLOSE_SCOPE
