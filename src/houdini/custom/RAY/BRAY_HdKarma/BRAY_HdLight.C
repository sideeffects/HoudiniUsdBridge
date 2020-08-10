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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#include "BRAY_HdLight.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdParam.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdLux/tokens.h>

#include <UT/UT_Debug.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_WorkArgs.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_HydraUtils.h>
#include <HUSD/XUSD_Tokens.h>

using namespace UT::Literal;

namespace
{
    // Parameters for the default light shader
    static constexpr UT_StringLit   lightcolorName("lightcolor");
    static constexpr UT_StringLit   uselightcolortempName("uselightcolortemp");
    static constexpr UT_StringLit   lightcolortempName("lightcolortemp");
    static constexpr UT_StringLit   attentypeName("attentype");
    static constexpr UT_StringLit   attenName("atten");
    static constexpr UT_StringLit   attenstartName("attenstart");
    static constexpr UT_StringLit   doconeName("docone");
    static constexpr UT_StringLit   coneangleName("coneangle");
    static constexpr UT_StringLit   conesoftnessName("conesoftness");
    static constexpr UT_StringLit   conedeltaName("conedelta");
    static constexpr UT_StringLit   conerolloffName("conerolloff");
    static constexpr UT_StringLit   barndoorleftName("barndoorleft");
    static constexpr UT_StringLit   barndoorleftedgeName("barndoorleftedge");
    static constexpr UT_StringLit   barndoorrightName("barndoorright");
    static constexpr UT_StringLit   barndoorrightedgeName("barndoorrightedge");
    static constexpr UT_StringLit   barndoortopName("barndoortop");
    static constexpr UT_StringLit   barndoortopedgeName("barndoortopedge");
    static constexpr UT_StringLit   barndoorbottomName("barndoorbottom");
    static constexpr UT_StringLit   barndoorbottomedgeName("barndoorbottomedge");
    static constexpr UT_StringLit   focusName("focus");
    static constexpr UT_StringLit   focustintName("focusTint");
    static constexpr UT_StringLit   envmapName("envmap");

}

PXR_NAMESPACE_OPEN_SCOPE

using namespace XUSD_HydraUtils;

namespace
{
    static std::string
    fullPropertyName(BRAY_LightProperty p)
    {
	UT_WorkBuffer	tmp;
	return std::string(BRAYproperty(tmp, BRAY_LIGHT_PROPERTY, p,
		    BRAY_HdUtil::parameterPrefix()));
    }

    template <BRAY_LightProperty PROP, typename S, typename D>
    static void
    setScalar(BRAY::OptionSet &lprops, HdSceneDelegate *sd,
	    const SdfPath &id, D def)
    {
	static const TfToken theName(fullPropertyName(PROP), TfToken::Immortal);
	S	sval;
	D	dval;
	if (evalLightAttrib(sval, sd, id, theName))
	    lprops.set(PROP, sval);
	else if (evalLightAttrib(dval, sd, id, theName))
	    lprops.set(PROP, dval);
	else
	    lprops.set(PROP, def);
    }

    template <BRAY_LightProperty PROP>
    static void
    setFloat(BRAY::OptionSet &lprops, HdSceneDelegate *sd,
	    const SdfPath &id, fpreal def)
    {
	setScalar<PROP, fpreal32, fpreal64>(lprops, sd, id, def);
    }

    template <BRAY_LightProperty PROP>
    static void
    setInt(BRAY::OptionSet &lprops, HdSceneDelegate *sd,
	    const SdfPath &id, int64 def)
    {
	setScalar<PROP, int32, int64>(lprops, sd, id, def);
    }

    template <BRAY_LightProperty PROP>
    static void
    setBool(BRAY::OptionSet &lprops, HdSceneDelegate *sd,
	    const SdfPath &id, bool def)
    {
	static const TfToken theName(fullPropertyName(PROP), TfToken::Immortal);
	bool val;
	if (evalLightAttrib(val, sd, id, theName))
	    lprops.set(PROP, val);
	else
	    lprops.set(PROP, def);
    }
}	// End namespace

BRAY_HdLight::BRAY_HdLight(const TfToken& type, const SdfPath &id)
	: HdLight(id)
	, myLightType(type)
{
#if 0
    if (!id.IsEmpty())
	UTdebugFormat("New Light type : {} {} {} ", this, id, myLightType);
    else
	UTdebugFormat("No path for light!");
#endif
}

BRAY_HdLight::~BRAY_HdLight()
{
    //UTdebugFormat("Delete light {}", this);
}

void
BRAY_HdLight::Finalize(HdRenderParam *renderParam)
{
    BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr	&scene = rparm->getSceneForEdit();

    if (myLight)
	scene.updateLight(myLight, BRAY_EVENT_DEL);
}

namespace
{
    class TokenMaker
    {
    public:
	TokenMaker(BRAY_LightProperty prop)
	{
	    UT_WorkBuffer	tmp;
	    myString = BRAYproperty(tmp, BRAY_LIGHT_PROPERTY, prop,
						BRAY_HdUtil::parameterPrefix());
	    myToken = TfToken(myString.c_str(), TfToken::Immortal);
	}
	const TfToken	&token() const { return myToken; }
    private:
	UT_StringHolder	myString;
	TfToken		myToken;
    };
    static TokenMaker	theLightShader(BRAY_LIGHT_SHADER);

    template <typename T> static void
    argValue(UT_StringArray &shader, T value)
    {
	UT_WorkBuffer	tmp;
	tmp.format("{}", value);
	shader.append(tmp);
    }

    template <typename T> static void
    shaderArgument(UT_StringArray &shader,
	    const UT_StringLit &name, const T &value)
    {
	shader.append(name.asHolder());
	argValue(shader, value);
    }

    template <> void
    shaderArgument(UT_StringArray &shader,
	    const UT_StringLit &name, const TfToken &value)
    {
	shader.append(name.asHolder());
	shader.append(value.GetText());
    }

    template <> void
    shaderArgument(UT_StringArray &shader,
	    const UT_StringLit &name, const GfVec3f &value)
    {
	shader.append(name.asHolder());
	argValue(shader, value[0]);
	argValue(shader, value[1]);
	argValue(shader, value[2]);
    }

    static void
    lightShader(HdSceneDelegate *sd, const SdfPath &id, UT_StringArray &args)
    {
	static const UT_StringHolder	default_shader(
		UT_EnvControl::getString(ENV_HOUDINI_DEFAULT_LIGHTSURFACE));

	std::string	shader;
	if (!evalLightAttrib(shader, sd, id, theLightShader.token())
		|| !UTisstring(shader.c_str()))
	{
	    args.append(default_shader);
	}
	else
	{
	    UT_String	buffer(shader);
	    UT_WorkArgs work_args;
	    buffer.parse(work_args);
	    for (int i = 0, n = work_args.getArgc(); i < n; ++i)
		args.append(work_args.getArg(i));
	}
    }
}

void
BRAY_HdLight::Sync(HdSceneDelegate *sd,
		    HdRenderParam *renderParam,
		    HdDirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    const SdfPath	&id = GetId();
    bool		 need_lock = false;

    if (id.IsEmpty())	// Not a real light?
	return;

    //UTdebugFormat("Sync Light: {} {}", this, id);
    BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr	&scene = rparm->getSceneForEdit();

    const HdDirtyBits	&bits = *dirtyBits;
    BRAY::OptionSet	lprops;
    BRAY_EventType	event = BRAY_NO_EVENT;

    if (!myLight)
    {
	myLight = scene.createLight(BRAY_HdUtil::toStr(id));
	BRAY_LightType	ltype = BRAY_LIGHT_UNDEFINED;

	if (myLightType == HdPrimTypeTokens->sphereLight)
	{
	    bool bval = false;
	    ltype = BRAY_LIGHT_SPHERE;
	    if (evalLightAttrib(bval, sd, id, UsdLuxTokens->treatAsPoint))
	    {
		if (bval)
		    ltype = BRAY_LIGHT_POINT;
	    }
	}
	else if (myLightType == HdPrimTypeTokens->diskLight)
	    ltype = BRAY_LIGHT_DISK;
	else if (myLightType == HdPrimTypeTokens->rectLight)
	    ltype = BRAY_LIGHT_RECT;
	else if (myLightType == HdPrimTypeTokens->cylinderLight)
	{
	    bool bval = false;
	    ltype = BRAY_LIGHT_CYLINDER;
	    if (evalLightAttrib(bval, sd, id, UsdLuxTokens->treatAsLine))
	    {
		if (bval)
		    ltype = BRAY_LIGHT_LINE;
	    }
	}
	else if (myLightType == HdPrimTypeTokens->domeLight)
	    ltype = BRAY_LIGHT_ENVIRONMENT;
	else if (myLightType == HdPrimTypeTokens->distantLight)
	    ltype = BRAY_LIGHT_DISTANT;
	else
	    UT_ASSERT(0);	// We should never end up here!

	myLight.lightProperties().set(BRAY_LIGHT_AREA_SHAPE, int(ltype));

    }
    BRAY::OptionSet	oprops = myLight.objectProperties();
    if (*dirtyBits & HdChangeTracker::DirtyParams)
	BRAY_HdUtil::updateObjectPrimvarProperties(oprops, *sd, dirtyBits, id);

    if (bits & DirtyTransform)
    {
	UT_SmallArray<GfMatrix4d>	xforms;
	BRAY_HdUtil::xformBlur(sd, *rparm, id, xforms, oprops);
	myLight.setTransform(BRAY_HdUtil::makeSpace(xforms.data(),
		    xforms.size()));
	event = event | BRAY_EVENT_XFORM;
    }

    lprops = myLight.lightProperties();
    if (bits & DirtyParams)
    {
	auto		&&hLightTokens = HusdHdLightTokens();
	UT_StringArray	shader_args;
	GfVec3f		color;
	float		width, height, radius, length;
	fpreal32	fval;
	TfToken		sval;
	bool		bval;
	std::string	stringVal;
	SdfAssetPath	envmapFilePath;

	// Determine the VEX light shader
	lightShader(sd, id, shader_args);

	if (!evalLightAttrib(color, sd, id, HdLightTokens->color))
	    color = GfVec3f(1.0);
	if (evalLightAttrib(fval, sd, id, HdLightTokens->intensity))
	    color *= fval;
	if (evalLightAttrib(fval, sd, id, HdLightTokens->exposure))
	    color *= SYSpow(2, fval);
	shaderArgument(shader_args, lightcolorName, color);

	if (evalLightAttrib(bval, sd, id,
		HdLightTokens->enableColorTemperature) && bval)
	{
	    if (evalLightAttrib(fval, sd, id, HdLightTokens->colorTemperature))
	    {
		shaderArgument(shader_args, uselightcolortempName, 1);
		shaderArgument(shader_args, lightcolortempName, fval);
	    }
	}

	// Check for attenuation
	if (*lprops.ival(BRAY_LIGHT_AREA_SHAPE) == BRAY_LIGHT_ENVIRONMENT)
	{
	    // Force "physical" attenuation for environment lights
	    shaderArgument(shader_args, attentypeName, hLightTokens->physical);
	}
	else if (evalLightAttrib(sval, sd, id, hLightTokens->attenType))
	{
	    shaderArgument(shader_args, attentypeName, sval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->attenDist))
		shaderArgument(shader_args, attenName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->attenStart))
		shaderArgument(shader_args, attenstartName, fval);
	}

	// sampling quality
	setFloat<BRAY_LIGHT_SAMPLING_QUALITY>(lprops, sd, id, 1);
	setBool<BRAY_LIGHT_FORCE_UNIFORM_SAMPLING>(lprops, sd, id, false);
	setFloat<BRAY_LIGHT_MIS_BIAS>(lprops, sd, id, 0);
	setFloat<BRAY_LIGHT_ACTIVE_RADIUS>(lprops, sd, id, -1);
	setInt<BRAY_LIGHT_HDRI_MAX_ISIZE>(lprops, sd, id, 2048);

        if (*lprops.ival(BRAY_LIGHT_AREA_SHAPE) == BRAY_LIGHT_DISTANT)
        {
            if (evalLightAttrib(fval, sd, id, UsdLuxTokens->angle))
                lprops.set(BRAY_LIGHT_DISTANT_ANGLE, fval);
        }

	if (evalLightAttrib(fval, sd, id, UsdLuxTokens->shapingConeAngle))
	{
	    shaderArgument(shader_args, doconeName, 1);
	    shaderArgument(shader_args, coneangleName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->coneSoftness))
		shaderArgument(shader_args, conesoftnessName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->coneDelta))
		shaderArgument(shader_args, conedeltaName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->coneRolloff))
		shaderArgument(shader_args, conerolloffName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->barndoorleft))
		shaderArgument(shader_args, barndoorleftName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->barndoorleftedge))
		shaderArgument(shader_args, barndoorleftedgeName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->barndoorright))
		shaderArgument(shader_args, barndoorrightName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->barndoorrightedge))
		shaderArgument(shader_args, barndoorrightedgeName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->barndoortop))
		shaderArgument(shader_args, barndoortopName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->barndoortopedge))
		shaderArgument(shader_args, barndoortopedgeName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->barndoorbottom))
		shaderArgument(shader_args, barndoorbottomName, fval);
	    if (evalLightAttrib(fval, sd, id, hLightTokens->barndoorbottomedge))
		shaderArgument(shader_args, barndoorbottomedgeName, fval);
	}

	if (evalLightAttrib(fval, sd, id, UsdLuxTokens->shapingFocus) &&
	    evalLightAttrib(color, sd, id, UsdLuxTokens->shapingFocusTint))
	{
	    shaderArgument(shader_args, focusName, fval);
	    shaderArgument(shader_args, focustintName, color);
	}

	// The order of evaluation is *very* important.  For spherical lights,
	// we need to evaluate @c radius *after* the width/height, but for tube
	// lights, we need to evaluate length *after* radius.
	if (!evalLightAttrib(width, sd, id, UsdLuxTokens->width))
	    width = 1;
	if (!evalLightAttrib(height, sd, id, UsdLuxTokens->height))
	    height = 1;
	if (evalLightAttrib(radius, sd, id, UsdLuxTokens->radius))
	{
	    // Set both width and height to radius
	    width = height = radius;
	}
	if (evalLightAttrib(length, sd, id, UsdLuxTokens->length))
	{
	    width = length;
	}
	if (evalLightAttrib(envmapFilePath, sd, id, UsdLuxTokens->textureFile))
	{
	    const std::string &path = BRAY_HdUtil::resolvePath(envmapFilePath);
            if (!path.empty())
            {
                UT_ASSERT(*lprops.ival(BRAY_LIGHT_AREA_SHAPE)
                        == BRAY_LIGHT_ENVIRONMENT);
                lprops.set(BRAY_LIGHT_AREA_MAP, path.c_str());
                shaderArgument(shader_args, envmapName, path);
                // TODO: shaping:ies:angleScale
                // TODO: shaping:ies:blur
            }
	}
	if (evalLightAttrib(bval, sd, id, UsdLuxTokens->normalize))
	    lprops.set(BRAY_LIGHT_NORMALIZE_AREA, bval);

	{
	    float	res[2];
	    res[0] = width;
	    res[1] = height;
	    lprops.set(BRAY_LIGHT_AREA_SIZE, res, 2);
	}
	setBool<BRAY_LIGHT_SINGLE_SIDED>(lprops, sd, id, true);
        setBool<BRAY_LIGHT_RENDER_LIGHT_GEO>(lprops, sd, id, false);
        setBool<BRAY_LIGHT_LIGHT_GEO_CASTS_SHADOW>(lprops, sd, id, false);

        // custom LPE tag
        std::string lpetag;
	TfToken lpetoken(fullPropertyName(BRAY_LIGHT_LPE_TAG),
            TfToken::Immortal);
	if (evalLightAttrib(lpetag, sd, id, lpetoken))
            lprops.set(BRAY_LIGHT_LPE_TAG, lpetag.c_str());

	// Shadow tokens
	if (!evalLightAttrib(color, sd, id, UsdLuxTokens->shadowColor))
	    color = GfVec3f(0.0);
	if (evalLightAttrib(fval, sd, id, hLightTokens->shadowIntensity))
	    color = color * fval + GfVec3f(1-fval);
	lprops.set(BRAY_LIGHT_SHADOW_COLOR, color.data(), 3);

	if (evalLightAttrib(fval, sd, id, UsdLuxTokens->shadowDistance))
	    lprops.set(BRAY_LIGHT_SHADOW_DISTANCE, fval);

	// Diffuse/specular multiplier tokens
	if (evalLightAttrib(fval, sd, id, UsdLuxTokens->diffuse))
	    lprops.set(BRAY_LIGHT_DIFFUSE_SCALE, fval);
	if (evalLightAttrib(fval, sd, id, UsdLuxTokens->specular))
	    lprops.set(BRAY_LIGHT_SPECULAR_SCALE, fval);

	// Geometry tokens

	// Set the light prototype for geometric lights prior to setting the
	// shader.  This allows proper handling of attribute bindings.
	{
	    myLight.setShader(scene, shader_args);
	    //UTdebugFormat("Set light: {}", shader_args);
	}

	need_lock = true;
    }
    if (*lprops.bval(BRAY_LIGHT_ENABLE) != sd->GetVisible(id))
    {
	// Toggle enabled
	lprops.set(BRAY_LIGHT_ENABLE, sd->GetVisible(id));
	need_lock = true;
    }

    if (bits & DirtyCollection)
    {
	VtValue val = sd->GetLightParamValue(id, HdTokens->lightLink);
	if (!val.IsEmpty() && val.GetTypeName() == "TfToken")
	{
	    TfToken tok = val.UncheckedGet<TfToken>();
	    if (tok != "")
	    {
		BRAY_HdParam *rparm =
		    UTverify_cast<BRAY_HdParam *>(renderParam);
		rparm->addLightCategory(tok.GetText());

		const UT_StringHolder *prevcat =
		    lprops.sval(BRAY_LIGHT_CATEGORY);
		// XXX: fairly certain that lightlink category names are unique
		// per-light?
		if (prevcat && prevcat->isstring())
		    rparm->eraseLightCategory(*prevcat);

		lprops.set(BRAY_LIGHT_CATEGORY, tok.GetText());
	    }
	}
	val = sd->GetLightParamValue(id, HdTokens->shadowLink);
	if (!val.IsEmpty() && val.GetTypeName() == "TfToken")
	{
	    TfToken tok = val.UncheckedGet<TfToken>();
	    if (tok != "")
	    {
		scene.addTraceset(tok.GetText());

		lprops = myLight.lightProperties();
		lprops.set(BRAY_LIGHT_SHADOW_TRACESET, tok.GetText());
	    }
	}


	need_lock = true;
    }

    if (need_lock)
	myLight.commitOptions(scene);

    if ((*dirtyBits) & (~DirtyTransform & AllDirty))
	event = event | BRAY_EVENT_PROPERTIES;
    if (event != BRAY_NO_EVENT)
	scene.updateLight(myLight, event);

    *dirtyBits &= ~AllDirty;
}

HdDirtyBits
BRAY_HdLight::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
