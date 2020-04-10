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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_ObjectImport.h"
#include "HUSD_Constants.h"
#include "HUSD_CreateMaterial.h"
#include "HUSD_EditReferences.h"
#include "HUSD_FindPrims.h"
#include "HUSD_TimeCode.h"
#include "HUSD_Utils.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <gusd/UT_Gf.h>
#include <pxr/base/vt/types.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdLux/light.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/geometryLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shadowAPI.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <SOP/SOP_Node.h>
#include <SHOP/SHOP_Node.h>
#include <SYS/SYS_FormatNumber.h>
#include <UT/UT_OpUtils.h>
#include <VOP/VOP_Node.h>
#include <initializer_list>

#define FSTRSIZE 64
#define ADDPARMINDEX(index)	    \
    if (index != -1)		    \
	parmindices->insert(index);

PXR_NAMESPACE_USING_DIRECTIVE

static UT_StringHolder lopUsdLuxCylinderLight   = "UsdLuxCylinderLight";
static UT_StringHolder lopUsdLuxDiskLight     	= "UsdLuxDiskLight";
static UT_StringHolder lopUsdLuxDistantLight  	= "UsdLuxDistantLight";
static UT_StringHolder lopUsdLuxDomeLight	= "UsdLuxDomeLight";
static UT_StringHolder lopUsdLuxGeometryLight 	= "UsdLuxGeometryLight";
static UT_StringHolder lopUsdLuxRectLight     	= "UsdLuxRectLight";
static UT_StringHolder lopUsdLuxSphereLight   	= "UsdLuxSphereLight";

HUSD_ObjectImport::HUSD_ObjectImport(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_ObjectImport::~HUSD_ObjectImport()
{
}

namespace
{

const PRM_Parm* husdGetParm(
	const PRM_ParmList *parmlist,
	const UT_StringHolder &parmname,
	UT_Set<int> *parmindices)
{
    int index = parmlist->getParmIndex(parmname);
    if (index == -1)
    {
	//UTformat("Parm \"{}\" not found\n", parmname);

	return nullptr;
    }

    const PRM_Parm *parm = parmlist->getParmPtr(index);

    if (parmindices != nullptr)
	parmindices->insert(index);

    return parm;
}

template<typename UT_VALUE_TYPE>
void husdGetParmValue(
	const PRM_Parm *parm,
	const fpreal time,
	UT_VALUE_TYPE &value)
{
    exint		d = UT_VALUE_TYPE::tuple_size;

    if (parm == nullptr)
    {
	for(int i = 0; i < d; i++)
	    value[i] = 0.0;
	return;
    }

    exint		n = SYSmax(parm->getVectorSize(), d);

    UT_Array<fpreal64> data(n, n);
    parm->getValues(
	    time, data.data(), SYSgetSTID());


    for(int i = 0; i < d; i++)
	value[i] = data[i];
}

template<>
void husdGetParmValue<fpreal>(
	const PRM_Parm *parm,
	const fpreal time,
	fpreal &value)
{
    if (parm != nullptr)
	parm->getValue(
		time, value, 0, SYSgetSTID());
    else
	value = 0.0;
}

template<>
void husdGetParmValue<int>(
	const PRM_Parm *parm,
	const fpreal time,
	int &value)
{
    if (parm != nullptr)
	parm->getValue(
		time, value, 0, SYSgetSTID());
    else
	value = 0.0;
}

template<>
void husdGetParmValue<bool>(
	const PRM_Parm *parm,
	const fpreal time,
	bool &value)
{
    int intvalue;
    husdGetParmValue(parm, time, intvalue);
    value = intvalue != 0;
}

template<>
void husdGetParmValue<UT_StringHolder>(
	const PRM_Parm *parm,
	const fpreal time,
	UT_StringHolder &value)
{
    if (parm != nullptr)
	parm->getValue(
		time, value, 0, true, SYSgetSTID());
    else
	value = "";
}

template<typename UT_VALUE_TYPE>
int husdGetParmValue(
	const PRM_ParmList *parmlist,
	const UT_StringHolder &parmname,
	const fpreal time,
	UT_VALUE_TYPE &value)
{
    int index = parmlist->getParmIndex(parmname);

    if (index == -1)
	return -1;

    const PRM_Parm	*parm = parmlist->getParmPtr(parmname);

    husdGetParmValue(parm, time, value);

    return index;
}

UsdTimeCode husdGetTimeCode(
    bool timedep,
    const UsdTimeCode &timecode)
{
    return timedep ? timecode : UsdTimeCode::Default();
}

template<typename SCHEMA, typename UT_VALUE_TYPE>
void husdSetAttributeIfNeeded(
	const UsdAttribute &attr,
	const UT_VALUE_TYPE &value,
	const UsdTimeCode &usdtimecode)
{
    bool setvalue = true;
    if (SCHEMA().IsTyped() && usdtimecode.IsDefault())
    {
	// Don't set a value for typed schemas when the value
	// to be set matches the attribute default.
	const UsdPrimDefinition *primdef =
            UsdSchemaRegistry::GetInstance().FindConcretePrimDefinition(
                UsdSchemaRegistry::GetInstance().GetSchemaTypeName<SCHEMA>());
	SdfPrimSpecHandle primspechandle =
            primdef ? primdef->GetSchemaPrimSpec() : SdfPrimSpecHandle();
	if (primspechandle)
	{
	    auto attrspechandle = primspechandle->GetAttributes().get(
		    attr.GetName());
	    if (attrspechandle)
	    {
		UT_VALUE_TYPE defvalue;
		HUSDgetAttributeSpecDefault(attrspechandle.GetSpec(), defvalue);
		if (value == defvalue)
		    setvalue = false;
	    }
	}
    }

    if (setvalue)
	HUSDsetAttribute(attr, value, usdtimecode);
}

template<typename SCHEMA, typename UT_VALUE_TYPE>
void husdSetAttributeToParmValue(
	const UsdAttribute &attr,
	const UsdTimeCode &usdtimecode,
	const PRM_Parm *parm,
	const fpreal time,
	bool firsttime,
	void transform_value(UT_VALUE_TYPE &value))
{
    if (parm == nullptr)
	return;

    bool timedep = parm->isTimeDependent();

    if (firsttime || timedep)
    {
	UT_VALUE_TYPE parmvalue;
	husdGetParmValue(parm, time, parmvalue);

	transform_value(parmvalue);

	husdSetAttributeIfNeeded<SCHEMA, UT_VALUE_TYPE>(
		attr,
		parmvalue,
		husdGetTimeCode(timedep, usdtimecode));
    }
}

template<typename SCHEMA, typename UT_VALUE_TYPE>
int husdSetAttributeToParmValue(
	const UsdAttribute &attr,
	const UsdTimeCode &usdtimecode,
	const PRM_ParmList *parmlist,
	const UT_StringHolder &parmname,
	const fpreal time,
	bool firsttime,
	void transform_value(UT_VALUE_TYPE &value))
{
    int index = parmlist->getParmIndex(parmname);
    if (index == -1)
	return -1;

    const PRM_Parm	*parm = parmlist->getParmPtr(index);

    husdSetAttributeToParmValue<SCHEMA, UT_VALUE_TYPE>(
	    attr,
	    usdtimecode,
	    parm,
	    time,
	    firsttime,
	    transform_value);

    return index;
}

template<typename SCHEMA, typename UT_VALUE_TYPE>
void husdSetAttributeToParmValue(
	const UsdAttribute &attr,
	const UsdTimeCode usdtimecode,
	const PRM_Parm *parm,
	const fpreal time,
	bool firsttime)
{
    return husdSetAttributeToParmValue<SCHEMA, UT_VALUE_TYPE>(
	    attr,
	    usdtimecode,
	    parm,
	    time,
	    firsttime,
	    [](UT_VALUE_TYPE &value){});
}

template<typename SCHEMA, typename UT_VALUE_TYPE>
int husdSetAttributeToParmValue(
	const UsdAttribute &attr,
	const UsdTimeCode usdtimecode,
	const PRM_ParmList *parmlist,
	const UT_StringHolder &parmname,
	const fpreal time,
	bool firsttime)
{
    return husdSetAttributeToParmValue<SCHEMA, UT_VALUE_TYPE>(
	    attr,
	    usdtimecode,
	    parmlist,
	    parmname,
	    time,
	    firsttime,
	    [](UT_VALUE_TYPE &value){});
}

bool husdAnyParmTimeDependent(
    const std::initializer_list<const PRM_Parm*> parms)
{
    bool timedep = false;
    for (auto &&parm : parms)
    {
	if (!parm)
	    continue;

	if (parm->isTimeDependent())
	{
	    timedep = true;
	}
    }

    return timedep;
}

bool husdSetRelationship(
	const UsdRelationship &rel,
	const UT_StringHolder &value,
	const UsdTimeCode &usdtimecode)
{
    SdfPathVector	 targets;

    targets.push_back(HUSDgetSdfPath(value.toStdString()));
    return rel.SetTargets(targets);
}

int husdSetRelationshipToParmValue(
	const UsdRelationship &attr,
	const UsdTimeCode &usdtimecode,
	const PRM_ParmList *parmlist,
	const UT_StringHolder &parmname,
	const fpreal time,
	bool firsttime)
{
    int			 index = parmlist->getParmIndex(parmname);
    if (index == -1)
	return -1;

    const PRM_Parm	*parm = parmlist->getParmPtr(parmname);

    bool timedep = parm->isTimeDependent();

    if (firsttime || timedep)
    {
	UT_StringHolder parmvalue;
	husdGetParmValue(parm, time, parmvalue);

	if (parmvalue.isstring())
	{
	    husdSetRelationship(
		    attr,
		    parmvalue,
		    husdGetTimeCode(timedep, usdtimecode));
	}
    }

    return index;
}

enum LightType
{
    INVALID,
    POINT,
    LINE,
    GRID,
    DISK,
    SPHERE,
    TUBE,
    GEO,
    DISTANT,
    SUN,
    ENV
};

LightType
husdGetHoudiniLightType(const OP_Node *light, UT_Set<int> *parmindices)
{
    UT_String		 primtype;
    UT_StringHolder	 light_type;

    OBJ_Node		*object = light->castToOBJNode();
    LOP_Node		*lop = light->castToLOPNode();

    UT_StringHolder opfullname = light->getOperator()->getName();
    UT_String opscope, opnamespace, opbasename, opversion;

    UT_OpUtils::getComponentsFromFullName(opfullname,
			&opscope, &opnamespace, &opbasename, &opversion);

    if ((object && opbasename == "envlight") ||
	(lop && opbasename == "mantradomelight"))
    {
	return LightType::ENV;
    }
    else if ((object && opbasename == "hlight") ||
	     (lop && opbasename == "mantralight"))
    {
	auto *parmlist = light->getParmList();

	int lighttype_parmindex = parmlist->getParmIndex("light_type");
	if (lighttype_parmindex == -1)
	    return INVALID;

	if (parmindices != nullptr)
	    parmindices->insert(lighttype_parmindex);

	const PRM_Parm *lighttype_parm =
	    parmlist->getParmPtr(lighttype_parmindex);

	husdGetParmValue(lighttype_parm, 0.0, light_type);

	if (light_type == "point")
	{
	    return LightType::POINT;
	}
	else if (light_type == "line")
	{
	    return LightType::LINE;
	}
	else if (light_type == "grid")
	{
	    return LightType::GRID;
	}
	else if (light_type == "disk")
	{
	    return LightType::DISK;
	}
	else if (light_type == "sphere")
	{
	    return LightType::SPHERE;
	}
	else if (light_type == "tube")
	{
	    return LightType::TUBE;
	}
	else if (light_type == "geo")
	{
	    return LightType::GEO;
	}
	else if (light_type == "distant")
	{
	    return LightType::DISTANT;
	}
	else if (light_type == "sun")
	{
	    return LightType::SUN;
	}
	return INVALID;
    }

    return INVALID;
}

UT_StringHolder
husdGetUsdLightType(LightType light_type)
{
    if (light_type == LightType::POINT)
    {
	return lopUsdLuxSphereLight;
    }
    else if (light_type == LightType::LINE)
    {
	return lopUsdLuxCylinderLight;
    }
    else if (light_type == LightType::GRID)
    {
	return lopUsdLuxRectLight;
    }
    else if (light_type == LightType::DISK)
    {
	return lopUsdLuxDiskLight;
    }
    else if (light_type == LightType::SPHERE)
    {
	return lopUsdLuxSphereLight;
    }
    else if (light_type == LightType::TUBE)
    {
	return lopUsdLuxCylinderLight;
    }
    else if (light_type == LightType::GEO)
    {
	return lopUsdLuxGeometryLight;
    }
    else if (light_type == LightType::DISTANT)
    {
	return lopUsdLuxDistantLight;
    }
    else if (light_type == LightType::SUN)
    {
	return lopUsdLuxDistantLight;
    }
    else if (light_type == LightType::ENV)
    {
	return lopUsdLuxDomeLight;
    }
    else
    {
	return "";
    }
}

template<typename SCHEMA>
void husdSetStandardLightAttrs(
    const PRM_ParmList *parmlist, SCHEMA light, const UsdTimeCode &usdtimecode,
    fpreal time, bool firsttime, UT_Set<int> *parmindices)
{
    int index = -1;
    index = husdSetAttributeToParmValue<SCHEMA, UT_Vector3>(
	    light.CreateColorAttr(), usdtimecode,
	    parmlist, "light_color", time, firsttime);
    ADDPARMINDEX(index)

    index = husdSetAttributeToParmValue<SCHEMA, fpreal>(
	    light.CreateIntensityAttr(), usdtimecode,
	    parmlist, "light_intensity", time, firsttime);
    ADDPARMINDEX(index)

    index = husdSetAttributeToParmValue<SCHEMA, fpreal>(
	    light.CreateExposureAttr(), usdtimecode,
	    parmlist, "light_exposure", time, firsttime);
    ADDPARMINDEX(index)

    HUSDsetAttribute(light.CreateNormalizeAttr(), true,
            UsdTimeCode::Default());
}

template<typename SCHEMA>
void husdSetStandardShadowAttrs(
    const PRM_ParmList *parmlist, SCHEMA shadow, const UsdTimeCode &usdtimecode,
    fpreal time, bool firsttime, UT_Set<int> *parmindices)
{
    int index = -1;
    index = husdSetAttributeToParmValue<SCHEMA, UT_Vector3>(
	    shadow.CreateColorAttr(), usdtimecode,
	    parmlist, "shadow_color", time, firsttime);
    ADDPARMINDEX(index)
}

bool
husdIsAreaLight(LightType lighttype)
{
    return lighttype == LightType::LINE || lighttype == LightType::GRID ||
	   lighttype == LightType::DISK || lighttype == LightType::SPHERE ||
	   lighttype == LightType::GEO || lighttype == LightType::SUN ||
	   lighttype == LightType::TUBE;
}

bool
husdIsDistantLight(LightType lighttype)
{
    return lighttype == LightType::DISTANT || lighttype == LightType::SUN;
}

bool
husdIsGeoLight(LightType lighttype)
{
    return lighttype == LightType::GEO;
}

bool
husdSetLightGeometry(
	UsdLuxGeometryLight &geolight,
	const UT_StringHolder &geoprimpath)
{
    auto &&prim = geolight.GetPrim();
    if (!prim.IsValid())
	return false;

    UsdRelationship georel = geolight.GetGeometryRel();
    return husdSetRelationship(georel, geoprimpath, UsdTimeCode::Default());
}

void
husdCreateLightProperties(
	const UsdPrim &prim,
	const UsdTimeCode &usdtimecode,
	const LightType lighttype,
	const OP_Node *node,
	fpreal time,
	bool firsttime,
	UT_Set<int> *parmindices)
{
    bool timedep = false;
    bool isarealight = husdIsAreaLight(lighttype);
    bool isdistantlight = husdIsDistantLight(lighttype);

    // If this is false we're importing from a lop node.
    bool isobj = node->castToOBJNode() != nullptr;

    auto parmlist = node->getParmList();

    UT_Vector2R areasize(0, 0);
    auto areasize_parm = husdGetParm(parmlist, "areasize", parmindices);
    if (areasize_parm)
    {
	husdGetParmValue(areasize_parm, time, areasize);
	timedep = areasize_parm->isTimeDependent();
    }

    if (lighttype == LightType::LINE)
    {
	UsdLuxCylinderLight cylinderlight(prim);

	HUSDsetAttribute(
		cylinderlight.CreateLengthAttr(),
		areasize[0], husdGetTimeCode(timedep, usdtimecode));
	HUSDsetAttribute(
		cylinderlight.CreateTreatAsLineAttr(),
		1, UsdTimeCode::Default());
    }
    else if (lighttype == LightType::TUBE)
    {
	UsdLuxCylinderLight cylinderlight(prim);

	HUSDsetAttribute(
		cylinderlight.CreateLengthAttr(),
		areasize[0], husdGetTimeCode(timedep, usdtimecode));
	// Factor in weird internal scaling factor for tube lights
	HUSDsetAttribute(
		cylinderlight.CreateRadiusAttr(),
		0.075 * areasize[1], husdGetTimeCode(timedep, usdtimecode));
    }
    else if (lighttype == LightType::SPHERE)
    {
	UsdLuxSphereLight spherelight(prim);

	HUSDsetAttribute(
		spherelight.CreateRadiusAttr(),
		0.5 * areasize[0], husdGetTimeCode(timedep, usdtimecode));
    }
    else if (lighttype == LightType::DISK)
    {
	UsdLuxDiskLight disklight(prim);

	HUSDsetAttribute(
		disklight.CreateRadiusAttr(),
		0.5 * areasize[0], husdGetTimeCode(timedep, usdtimecode));
    }
    else if (lighttype == LightType::GRID)
    {
	UsdLuxRectLight rectlight(prim);

	HUSDsetAttribute(
		rectlight.CreateWidthAttr(),
		areasize[0], husdGetTimeCode(timedep, usdtimecode));
	HUSDsetAttribute(
		rectlight.CreateHeightAttr(),
		areasize[1], husdGetTimeCode(timedep, usdtimecode));
    }
    else if (lighttype == LightType::DISTANT || lighttype == LightType::SUN)
    {
	UsdLuxDistantLight distantlight(prim);

	auto angle_parm = husdGetParm(parmlist, "vm_envangle", parmindices);
	fpreal angle;
	husdGetParmValue(angle_parm, time, angle);

	HUSDsetAttribute(
		distantlight.CreateAngleAttr(),
		angle,
		husdGetTimeCode(angle_parm->isTimeDependent(),
		    usdtimecode));
    }
    else if (lighttype == LightType::GEO)
    {
	UsdLuxGeometryLight geolight(prim);

	if (!isobj)
	{
	    auto areageo_parm = husdGetParm(parmlist, "areageometry", parmindices);
	    UT_StringHolder areageo;
	    husdGetParmValue(areageo_parm, time, areageo);

	    husdSetRelationship(geolight.GetGeometryRel(),
		    areageo, UsdTimeCode::Default());
	}
    }
    else if (lighttype == LightType::POINT)
    {
	UsdLuxSphereLight spherelight(prim);

	HUSDsetAttribute(
		spherelight.CreateTreatAsPointAttr(),
		1, UsdTimeCode::Default());
    }

    UsdLuxLight light(prim);
    husdSetStandardLightAttrs(parmlist, light,
	    usdtimecode, time, firsttime, parmindices);

    int index;

    bool activeradiusenable;
    auto activeradiusenable_parm =
	husdGetParm(parmlist, "activeradiusenable", parmindices);
    husdGetParmValue(activeradiusenable_parm, time, activeradiusenable);

    if (isarealight)
    {
	if (lighttype == LightType::GRID)
	{
	    UsdLuxRectLight rectlight(prim);
	    index = husdSetAttributeToParmValue
                <UsdLuxRectLight, UT_StringHolder>(
		rectlight.CreateTextureFileAttr(), usdtimecode,
		parmlist, "light_texture", time, firsttime);
	    ADDPARMINDEX(index);
	}
    }

    if (!isdistantlight)
    {
	bool coneenable;
	auto coneenable_parm = husdGetParm(parmlist, "coneenable", parmindices);
	husdGetParmValue(coneenable_parm, time, coneenable);

	if (coneenable)
	{
	    auto shapingAPI = UsdLuxShapingAPI::Apply(prim);

	    index = husdSetAttributeToParmValue<UsdLuxShapingAPI, fpreal>(
		shapingAPI.CreateShapingConeAngleAttr(), usdtimecode,
		parmlist, "coneangle", time, firsttime);
	    ADDPARMINDEX(index);
	}
	//bool iesmapenable;
	//husdGetParmValue(parmlist, "areamapenable", time, iesmapenable);
	//if (iesmapenable)
	{
	    UT_StringHolder iesmap;
	    auto iesmap_parm = husdGetParm(parmlist, "areamap", parmindices);
	    husdGetParmValue(iesmap_parm, time, iesmap);
	    if (iesmap.isstring())
	    {
		auto shapingAPI = UsdLuxShapingAPI::Apply(prim);

		husdSetAttributeToParmValue
		    <UsdLuxShapingAPI, UT_StringHolder>(
			shapingAPI.CreateShapingIesFileAttr(),
			usdtimecode, iesmap_parm,
			time,
			firsttime);

		index = husdSetAttributeToParmValue
		    <UsdLuxShapingAPI, fpreal>(
			shapingAPI.CreateShapingIesAngleScaleAttr(),
			usdtimecode, parmlist, "areamapscale",
			time,
			firsttime);
		ADDPARMINDEX(index);
	    }
	}

	UT_StringHolder shadowtype;
	auto shadowtype_parm = husdGetParm(parmlist, "shadow_type", parmindices);
	husdGetParmValue(shadowtype_parm, time, shadowtype);
	if (shadowtype != "off")
	{
	    auto shadowAPI = UsdLuxShadowAPI::Apply(prim);

	    auto intensity_parm =
                husdGetParm(parmlist, "shadow_intensity", parmindices);
	    auto color_parm =
                husdGetParm(parmlist, "shadow_color", parmindices);

	    timedep = husdAnyParmTimeDependent(
		    {intensity_parm, color_parm});
	    if (firsttime || timedep)
	    {
		fpreal intensity;
		UT_Vector3R color;
		husdGetParmValue(intensity_parm, time, intensity);
		husdGetParmValue(color_parm, time, color);

		if (color.maxComponent() > 0)
		{
		    HUSDsetAttribute(
			    shadowAPI.CreateShadowColorAttr(),
			    color,
			    husdGetTimeCode(timedep, usdtimecode));
		}
	    }
	}
    }
}

void
husdCreateEnvLightProperties(
	const UsdPrim &prim,
	const UsdTimeCode &usdtimecode,
	const LightType lighttype,
	const OP_Node *node,
	fpreal time,
	bool firsttime,
	UT_Set<int> *parmindices)
{
    int index;
    bool timedep = false;

    // If this is false we're importing from a lop node.
    bool isobj = node->castToOBJNode() != nullptr;

    auto parmlist = node->getParmList();

    UsdLuxDomeLight domelight(prim);
    UsdLuxLight light(prim);

    husdSetStandardLightAttrs(parmlist, light,
	    usdtimecode, time, firsttime, parmindices);

    if (!isobj)
    {
	bool portalenable;
	auto portalenable_parm =
	    husdGetParm(parmlist, "env_portalenable", parmindices);
	husdGetParmValue(portalenable_parm, time, portalenable);

	UT_StringHolder portal("");
	if (portalenable)
	{
	    index = husdGetParmValue(parmlist, "env_portal", time, portal);
	    ADDPARMINDEX(index);
	}

	if (portal.isstring())
	{
	    husdSetRelationship(domelight.GetPortalsRel(), portal,
		    husdGetTimeCode(timedep, usdtimecode));
	}
    }
}

void
husdCreateCameraProperties(
	const UsdPrim &prim,
	const UsdTimeCode &usdtimecode,
	const PRM_ParmList *parmlist,
	fpreal time,
	bool firsttime,
	UT_Set<int> *parmindices)
{
    bool timedep;

    UsdGeomCamera cam(prim);

    int index = husdSetAttributeToParmValue<UsdGeomCamera, UT_StringHolder>(
	cam.CreateProjectionAttr(), usdtimecode,
	parmlist, "projection", time, firsttime,
	[](UT_StringHolder &proj)
	{
	    if (proj == "ortho")
		proj = "orthographic";
	});
    ADDPARMINDEX(index)

    index = husdSetAttributeToParmValue<UsdGeomCamera, fpreal>(
	    cam.CreateFocalLengthAttr(), usdtimecode,
	    parmlist, "focal", time, firsttime);
    ADDPARMINDEX(index)

    auto res_parm = husdGetParm(parmlist, "res", parmindices);
    auto aperture_parm = husdGetParm(parmlist, "aperture", parmindices);
    auto win_parm = husdGetParm(parmlist, "win", parmindices);
    auto winsize_parm = husdGetParm(parmlist, "winsize", parmindices);

    timedep = husdAnyParmTimeDependent(
	    {res_parm, aperture_parm, win_parm, winsize_parm});
    if (firsttime || timedep)
    {
	fpreal haperture, vaperture;
	UT_Vector2i res;
	husdGetParmValue(res_parm, time, res);

	husdGetParmValue(aperture_parm, time, haperture);

	UT_Vector2R winoffset;
	husdGetParmValue(win_parm, time, winoffset);

	UT_Vector2R winsize;
	husdGetParmValue(winsize_parm, time, winsize);

	fpreal aspect = fpreal(res.y()) / res.x();
	vaperture = aspect * haperture;

	HUSDsetAttribute(
		cam.CreateHorizontalApertureAttr(),
		winsize.x() * haperture,
		husdGetTimeCode(timedep, usdtimecode));
	HUSDsetAttribute(
		cam.CreateVerticalApertureAttr(),
		winsize.y() * vaperture,
		husdGetTimeCode(timedep, usdtimecode));
	husdSetAttributeIfNeeded<UsdGeomCamera, fpreal>(
		cam.CreateHorizontalApertureOffsetAttr(),
		winoffset.x() * haperture,
		husdGetTimeCode(timedep, usdtimecode));
	husdSetAttributeIfNeeded<UsdGeomCamera, fpreal>(
		cam.CreateVerticalApertureOffsetAttr(),
		winoffset.y() * vaperture,
		husdGetTimeCode(timedep, usdtimecode));

    }

    auto near_parm = husdGetParm(parmlist, "near", parmindices);
    auto far_parm = husdGetParm(parmlist, "far", parmindices);
    timedep = husdAnyParmTimeDependent({near_parm, far_parm});
    if (firsttime || timedep)
    {
	UT_Vector2R cliprange;
	husdGetParmValue(near_parm, time, cliprange.x());
	husdGetParmValue(far_parm, time, cliprange.y());
	HUSDsetAttribute(
		cam.CreateClippingRangeAttr(),
		cliprange, husdGetTimeCode(timedep, usdtimecode));
    }

    auto shutter_parm = husdGetParm(parmlist, "shutter", parmindices);
    auto shutteroffset_parm =
	husdGetParm(parmlist, "shutteroffset", parmindices);
    timedep = husdAnyParmTimeDependent( {shutter_parm});
    if (firsttime || timedep)
    {
	fpreal shutter, shutteroffset;

	husdGetParmValue(shutter_parm, time, shutter);
	husdGetParmValue(shutteroffset_parm, time, shutteroffset);

	HUSDsetAttribute(
		cam.CreateShutterOpenAttr(),
		(-0.5 + 0.5 * shutteroffset) * shutter,
		husdGetTimeCode(timedep, usdtimecode));
	HUSDsetAttribute(
		cam.CreateShutterCloseAttr(),
		(0.5 + 0.5 * shutteroffset) * shutter,
		husdGetTimeCode(timedep, usdtimecode));
    }

    index = husdSetAttributeToParmValue<UsdGeomCamera, fpreal>(
	    cam.CreateFocusDistanceAttr(), usdtimecode,
	    parmlist, "focus", time, firsttime);
    ADDPARMINDEX(index);

    index = husdSetAttributeToParmValue<UsdGeomCamera, fpreal>(
	    cam.CreateFStopAttr(), usdtimecode,
	    parmlist, "fstop", time, firsttime);
    ADDPARMINDEX(index);
}

}

UT_StringHolder
HUSD_ObjectImport::getPrimTypeForObject(const OP_Node *node,
	UT_Set<int> *parmindices)
{
    OBJ_Node		*object = node->castToOBJNode();
    LOP_Node		*lop = node->castToLOPNode();

    if (!object)
    {
	UT_StringHolder opfullname = node->getOperator()->getName();
	UT_String opscope, opnamespace, opbasename, opversion;

	UT_OpUtils::getComponentsFromFullName(opfullname,
			    &opscope, &opnamespace, &opbasename, &opversion);

	if (lop && opbasename == "camera")
	    return "UsdGeomCamera";

	auto light_type = husdGetHoudiniLightType(node, parmindices);

	if (light_type != LightType::INVALID)
	    return husdGetUsdLightType(light_type);

	return UT_StringHolder::theEmptyString;
    }

    auto objtype = object->getObjectType();
    if (objtype & OBJ_NULL)
    {
	return HUSD_Constants::getXformPrimType();
    }
    else if (objtype & OBJ_SUBNET)
    {
	return HUSD_Constants::getXformPrimType();
    }
    else if (objtype == OBJ_GEOMETRY)
    {
	return HUSD_Constants::getXformPrimType();
    }
    else if (objtype & OBJ_LIGHT)
    {
	auto light_type = husdGetHoudiniLightType(node, parmindices);

	return husdGetUsdLightType(light_type);
    }
    else if (objtype & OBJ_CAMERA)
    {
	return "UsdGeomCamera";
    }

    return UT_StringHolder::theEmptyString;
}

UT_StringHolder
HUSD_ObjectImport::getPrimKindForObject(const OP_Node *node)
{
    OBJ_Node		*object = node->castToOBJNode();

    if (!object)
	return UT_StringHolder::theEmptyString;

    auto objtype = object->getObjectType();
    if (objtype & OBJ_NULL)
    {
	return HUSD_Constants::getKindGroup();
    }
    else if (objtype & OBJ_SUBNET)
    {
	return HUSD_Constants::getKindGroup();
    }

    return UT_StringHolder::theEmptyString;
}


bool
HUSD_ObjectImport::importPrim(
	const OBJ_Node *object,
	const UT_StringHolder &primpath,
	const UT_StringHolder &primtype,
	const UT_StringHolder &primkind) const
{
    auto		 outdata = myWriteLock.data();

    if (!outdata || !outdata->isStageValid())
	return false;

    HUSD_AutoLayerLock	 layerlock(myWriteLock);
    HUSD_CreatePrims	 creator(layerlock);

    if (!creator.createPrim(primpath, primtype, primkind,
	    HUSD_Constants::getPrimSpecifierDefine(),
	    HUSD_Constants::getXformPrimType()))
	return false;

    auto prim = outdata->stage()->GetPrimAtPath(HUSDgetSdfPath(primpath));

    HUSDsetSourceNode(prim, object->getUniqueId());

    return true;
}

void
HUSD_ObjectImport::importParameters(
	const UT_StringHolder &primpath,
	const OP_Node *node,
	const HUSD_TimeCode &timecode,
	const fpreal time,
	bool firsttime,
	UT_Set<int> *parmindices) const
{
    auto	 outdata = myWriteLock.data();

    if (!outdata || !outdata->isStageValid())
	return;

    auto stage = outdata->stage();
    auto usdtimecode = HUSDgetUsdTimeCode(timecode);
    auto parmlist = node->getParmList();
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(primpath.c_str()));
    auto primtype = getPrimTypeForObject(node);

    if (primtype.startsWith("UsdLux"))
    {
	auto lighttype = husdGetHoudiniLightType(node, parmindices);
	if (lighttype == LightType::ENV)
	    husdCreateEnvLightProperties(
		    prim, usdtimecode, lighttype, node, time, firsttime,
		    parmindices);
	else
	    husdCreateLightProperties(
		    prim, usdtimecode, lighttype, node, time, firsttime,
		    parmindices);
    }
    else if (primtype == "UsdGeomCamera")
    {
	husdCreateCameraProperties(
		prim, usdtimecode, parmlist, time, firsttime,
		parmindices);
    }
}

void
HUSD_ObjectImport::importSOP(
	SOP_Node *sop,
	OP_Context &context,
	const UT_StringRef &refprimpath,
	const UT_StringRef &pathattr,
	const UT_StringRef &primpath,
	const UT_StringRef &pathprefix,
	bool polygonsassubd,
        const UT_StringRef &subdgroup) const
{
    UT_String				 sopfilepath;
    UT_StringMap<UT_StringHolder>	 args;
    GU_DetailHandle			 gdh;
    DEP_ContextOptionsReadHandle	 options;
    UT_String				 optstr;
    char				 tstr[FSTRSIZE];

    sopfilepath.sprintf("%s%s.sop", OPREF_PREFIX, sop->getFullPath().c_str());
    tstr[SYSformatFloat(tstr, FSTRSIZE, context.getTime())] = '\0';

    gdh = sop->getCookedGeoHandle(context);
    options = sop->dataMicroNode().getLastUsedContextOptions();
    args["t"] = tstr;
    if (pathattr.isstring())
	args["pathattr"] = pathattr;
    if (pathprefix.isstring())
	args["pathprefix"] = pathprefix;
    if (polygonsassubd)
	args["polygonsassubd"] = "1";
    if (polygonsassubd && subdgroup.isstring())
	args["subdgroup"] = subdgroup;
    if (!options.isNull())
    {
	for (auto opt = options->begin(); !opt.atEnd(); ++opt)
	{
	    opt.entry()->getOptionString(UT_OPTFMT_PYTHON, optstr);
	    args[opt.name()] = optstr;
	}
    }

    HUSD_EditReferences		 addref(myWriteLock);

    addref.setRefType(HUSD_Constants::getReferenceTypePayload());
    addref.addReference(primpath, sopfilepath, refprimpath,
	HUSD_LayerOffset(), args, gdh);
}

bool
HUSD_ObjectImport::importMaterial(
	VOP_Node *vop,
	const UT_StringHolder &primpath) const
{
    auto	 outdata = myWriteLock.data();

    if (!outdata || !outdata->isStageValid())
	return false;

    HUSD_CreateMaterial husdmat(myWriteLock);
    if (!husdmat.createMaterial(*vop, primpath, /*gen_preview_shader=*/ true))
	return false;

    auto prim = outdata->stage()->GetPrimAtPath(HUSDgetSdfPath(primpath));

    HUSDsetSourceNode(prim, vop->getUniqueId());

    return true;
}

bool
HUSD_ObjectImport::setLightGeometry(
	const UT_StringHolder &lightprimpath,
	const UT_StringHolder &geoprimpath) const
{
    auto	 outdata = myWriteLock.data();

    if (!outdata || !outdata->isStageValid())
	return false;

    auto stage = outdata->stage();

    UsdLuxGeometryLight geolight = UsdLuxGeometryLight::Get(stage,
	    HUSDgetSdfPath(lightprimpath));

    return husdSetRelationship(geolight.GetGeometryRel(),
	    geoprimpath, UsdTimeCode::Default());
}

bool
HUSD_ObjectImport::setLightPortal(
	const UT_StringHolder &lightprimpath,
	const UT_StringHolder &geoprimpath) const
{
    auto	 outdata = myWriteLock.data();

    if (!outdata || !outdata->isStageValid())
	return false;

    auto stage = outdata->stage();

    UsdLuxDomeLight domelight = UsdLuxDomeLight::Get(stage,
	    HUSDgetSdfPath(lightprimpath));

    return husdSetRelationship(domelight.GetPortalsRel(),
	    geoprimpath, UsdTimeCode::Default());
}
