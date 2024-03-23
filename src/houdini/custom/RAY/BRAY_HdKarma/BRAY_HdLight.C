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
#include "BRAY_HdMaterialNetwork.h"
#include "BRAY_HdFormat.h"
#include "BRAY_HdTokens.h"
#include <UT/UT_SmallArray.h>
#include <UT/UT_JSONWriter.h>

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdLux/tokens.h>

#include <pxr/imaging/hd/material.h>

#include <UT/UT_Debug.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_ErrorLog.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    // Parameters for the default light shader
    static constexpr UT_StringLit   lightcolorName("lightcolor");

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

        VtValue         val = BRAY_HdUtil::evalLightVt(sd, id, theName);
        if (val.IsHolding<S>())
        {
            lprops.set(PROP, val.UncheckedGet<S>());
            return;
        }
        else if (val.IsHolding<D>())
        {
            lprops.set(PROP, val.UncheckedGet<D>());
            return;
        }
        else if (val.IsHolding<TfToken>() || val.IsHolding<std::string>())
        {
            // Some integers can be set from their meny values
            UT_StringHolder     s = BRAY_HdUtil::toStr(val);
            if (s)
            {
                lprops.set(PROP, s);
                return;
            }
        }
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
    static bool
    setBool(BRAY::OptionSet &lprops, HdSceneDelegate *sd,
	    const SdfPath &id, bool def)
    {
	static const TfToken theName(fullPropertyName(PROP), TfToken::Immortal);
	bool val;
	if (!BRAY_HdUtil::evalLight(val, sd, id, theName))
            val = def;
        lprops.set(PROP, val);
        return val;
    }

#if 0
    static void
    dump(const char *style, const HdMaterialNetworkMap &mat)
    {
        HdMaterialNetwork2      m2;
        HdMaterialNetwork2ConvertFromHdMaterialNetworkMap(mat, &m2);
        BRAY_HdMaterialNetwork::dump(m2);
    }

    static void
    debugFilter(const BRAY_HdParam &rparm,
            HdSceneDelegate *sd,
            const SdfPath &filter,
            int nsegs,
            bool autoseg)
    {
        UTdebugFormat("LightFilter: {}", filter);
        UT_SmallArray<VtValue>  filterType;
        UT_SmallArray<VtValue>  exposure;
        UT_StackBuffer<float>   times(nsegs);

        rparm.fillShutterTimes(times, nsegs);
        BRAY_HdUtil::dformLight(sd, filterType, filter,
                TfToken("lightFilterType"), times, nsegs, autoseg);
        BRAY_HdUtil::dformLight(sd, exposure, filter,
                TfToken("inputs:karma:exposure"), times, nsegs, autoseg);

        for (auto &&v : filterType)
            UTdebugFormat(" FilterType: {}", v);
        for (auto &&v : exposure)
            UTdebugFormat(" Exposure: {}", v);

        VtValue mat = sd->GetMaterialResource(filter);
        if (!mat.IsHolding<HdMaterialNetworkMap>())
        {
            UTdebugFormat("No network map for light filter");
            return;
        }
        dump("Filter", mat.UncheckedGet<HdMaterialNetworkMap>());
    }
#endif
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

    rparm->eraseLightFilter(this);
    if (myLight)
	scene.updateLight(myLight, BRAY_EVENT_DEL);

    myLight = BRAY::LightPtr();
}

namespace
{
    template <typename T> static void
    argValue(UT_StringArray &shader, T value)
    {
	UT_WorkBuffer	tmp;
	tmp.format("{}", value);
	shader.append(tmp);
    }

    static bool
    lightShader(HdSceneDelegate *sd, const SdfPath &id, UT_StringArray &args)
    {
	static const UT_StringHolder	default_shader(
		UT_EnvControl::getString(ENV_HOUDINI_DEFAULT_LIGHTSURFACE));

        VtValue shv = BRAY_HdUtil::evalLightVt(sd, id,
                                BRAY_HdUtil::lightToken(BRAY_LIGHT_SHADER));
        UT_StringHolder shader = BRAY_HdUtil::toStr(shv);
        if (!shader)
        {
	    args.append(default_shader);
            return false;
	}
        UT_String	buffer(shader);
        UT_WorkArgs work_args;
        buffer.parse(work_args);
        for (int i = 0, n = work_args.getArgc(); i < n; ++i)
            args.append(work_args.getArg(i));
        return true;
    }

    static bool
    isSkyLight(
        const TfToken &lightType,
        const TfToken &shaderid)
    {
        return (lightType == HdPrimTypeTokens->light &&
                shaderid == "KMAskyDomeLight");
    }

    static BRAY_LightType
    computeLightType(
            HdSceneDelegate *sd,
            const TfToken &lightType,
            const TfToken &shaderid,
            const SdfPath &id)
    {
	BRAY_LightType	ltype = BRAY_LIGHT_UNDEFINED;

	if (lightType == HdPrimTypeTokens->sphereLight)
	{
	    bool bval = false;
	    ltype = BRAY_LIGHT_SPHERE;
	    if (BRAY_HdUtil::evalLight(bval, sd, id, UsdLuxTokens->treatAsPoint))
	    {
		if (bval)
		    ltype = BRAY_LIGHT_POINT;
	    }
	}
	else if (lightType == HdPrimTypeTokens->diskLight)
	    ltype = BRAY_LIGHT_DISK;
	else if (lightType == HdPrimTypeTokens->rectLight)
	    ltype = BRAY_LIGHT_RECT;
	else if (lightType == HdPrimTypeTokens->cylinderLight)
	{
	    bool bval = false;
	    ltype = BRAY_LIGHT_CYLINDER;
	    if (BRAY_HdUtil::evalLight(bval, sd, id, UsdLuxTokens->treatAsLine))
	    {
		if (bval)
		    ltype = BRAY_LIGHT_LINE;
	    }
	}
	else if (lightType == HdPrimTypeTokens->domeLight)
	    ltype = BRAY_LIGHT_ENVIRONMENT;
	else if (lightType == HdPrimTypeTokens->distantLight)
	    ltype = BRAY_LIGHT_DISTANT;
        else if (isSkyLight(lightType, shaderid))
            ltype = BRAY_LIGHT_ENVIRONMENT;

        // Now that we accept "light" sprims, we may end up here with no
        // defined light type if we are sent a light with a renderer-specific
        // shader for a non-larma renderer.
        return ltype;
    }

    using usdTokenAlias = BRAY_HdMaterialNetwork::usdTokenAlias;
    using usdTokenMappingArray = UT_Array<usdTokenAlias>;

    static const BRAY_HdMaterialNetwork::ParmNameMap *
    lightMaterialTokens()
    {
        static BRAY_HdMaterialNetwork::ParmNameMapCreator     theMap({
            usdTokenAlias(UsdLuxTokens->inputsTextureFile, "textureFile"),
            usdTokenAlias(UsdLuxTokens->inputsTextureFormat, "textureFormat"),

#if 0
            usdTokenAlias(UsdLuxTokens->inputsShadowColor, "shadowColor"),
            usdTokenAlias(UsdLuxTokens->inputsShadowDistance, "shadowDistance"),
            usdTokenAlias(UsdLuxTokens->inputsShadowEnable, "shadowEnable"),
            usdTokenAlias(UsdLuxTokens->inputsShadowFalloff, "shadowFalloff"),
            usdTokenAlias(UsdLuxTokens->inputsShadowFalloffGamma, "shadowFalloffGamma"),
#endif

            // Usd Shaping tokens
            usdTokenAlias(UsdLuxTokens->inputsShapingFocus, "focus"),
            usdTokenAlias(UsdLuxTokens->inputsShapingFocusTint, "focustint"),
            usdTokenAlias(UsdLuxTokens->inputsShapingConeAngle, "coneangle"),
            usdTokenAlias(UsdLuxTokens->inputsShapingConeSoftness, "conesoftness"),
            usdTokenAlias(UsdLuxTokens->inputsShapingIesFile, "iesfile"),
            usdTokenAlias(UsdLuxTokens->inputsShapingIesAngleScale, "iesAngleScale"),
            usdTokenAlias(UsdLuxTokens->inputsShapingIesNormalize, "iesNormalize"),
        });
        return &theMap.map();
    }

    static const usdTokenMappingArray &
    commonLuxTokens()
    {
        static usdTokenMappingArray     theTokens({
            // UsdLux tokens
            UsdLuxTokens->inputsIntensity,
            UsdLuxTokens->inputsExposure,
            UsdLuxTokens->inputsDiffuse,
            UsdLuxTokens->inputsSpecular,
            UsdLuxTokens->inputsNormalize,
            UsdLuxTokens->inputsColor,
            UsdLuxTokens->inputsEnableColorTemperature,
            UsdLuxTokens->inputsColorTemperature,

            // Usd Shaping tokens
            usdTokenAlias(UsdLuxTokens->inputsShapingFocus, "focus"),
            usdTokenAlias(UsdLuxTokens->inputsShapingFocusTint, "focustint"),
            usdTokenAlias(UsdLuxTokens->inputsShapingConeAngle, "coneangle"),
            usdTokenAlias(UsdLuxTokens->inputsShapingConeSoftness, "conesoftness"),
            usdTokenAlias(UsdLuxTokens->inputsShapingIesFile, "iesfile"),
            usdTokenAlias(UsdLuxTokens->inputsShapingIesAngleScale, "iesAngleScale"),
            usdTokenAlias(UsdLuxTokens->inputsShapingIesNormalize, "iesNormalize"),

            // Houdini shaping tokens
            "barndoorleft",
            "barndoorleftedge",
            "barndoorright",
            "barndoorrightedge",
            "barndoortop",
            "barndoortopedge",
            "barndoorbottom",
            "barndoorbottomedge",
        });
        return theTokens;
    }

    // TODO: UsdLux Shadow Controls

    class SpecialShaderArgs
    {
    public:
        SpecialShaderArgs()
        {
            // Some arguments are handled as a special case
            mySet.insert(UsdLuxTokens->inputsIntensity);
            mySet.insert(UsdLuxTokens->inputsExposure);
            mySet.insert(UsdLuxTokens->inputsColor);
        }
        bool    contains(const TfToken &t) const
        {
            return mySet.contains(t);
        }
    private:
        UT_Set<TfToken> mySet;
    };

    static void
    addShaderArgs(UT_StringArray &args,
            HdSceneDelegate *sd,
            const SdfPath &id,
            const usdTokenMappingArray &tokens)
    {
        static SpecialShaderArgs        special;
        UT_SmallArray<VtValue>  values;
        float                   time = 0;
        for (const auto &t : tokens)
        {
            if (!special.contains(t.token()))
            {
                if (BRAY_HdUtil::dformLight(sd, values, id, t.token(),
                            &time, 1, false))
                {
                    BRAY_HdUtil::appendVexArg(args, t.alias(), values[0]);
                }
            }
        }
    }

    static void
    barndoorFilter(BRAY::ScenePtr &scene,
            UT_Array<BRAY::ShaderGraphPtr> &filterList,
            const SdfPath &light_id,
            HdSceneDelegate *sd)
    {
        BRAY_ShaderInstance     *node = nullptr;
        float                    fval;
        BRAY::OptionSet          oset;

        for (const TfToken &parm : {
                BRAYHdTokens->barndoorleft,
                BRAYHdTokens->barndoorleftedge,
                BRAYHdTokens->barndoorright,
                BRAYHdTokens->barndoorrightedge,
                BRAYHdTokens->barndoortop,
                BRAYHdTokens->barndoortopedge,
                BRAYHdTokens->barndoorbottom,
                BRAYHdTokens->barndoorbottomedge,
            })
        {
            if (BRAY_HdUtil::evalLight(fval, sd, light_id, parm) && fval > 0)
            {
                if (!node)
                {
                    UT_WorkBuffer       path;
                    path.format("{}/__private_barndoor",
                            BRAY_HdUtil::toStr(light_id));
                    UT_StringHolder     pstr(path);
                    BRAY::ShaderGraphPtr sg = scene.createShaderGraph(pstr);
                    node = sg.createNode("kma_lfilter_barndoor", "a");
                    if (!node)
                    {
                        UT_ASSERT(0 && "No barn door light filter");
                        return;
                    }
                    oset = sg.nodeParams(node);
                    filterList.append(sg);
                }
                const char      *name = parm.GetText();
                // The Karma parameter doesn't have the "barndoor" smurf typing
                // so we can just skip over the first 8 characters
                int idx = oset.find(UTmakeUnsafeRef(name+8));
                UT_ASSERT(idx >= 0);
                oset.set(idx, &fval, 1);
            }
        }
    }

    static void
    buildFilters(BRAY::ScenePtr &scene,
            UT_Array<BRAY::ShaderGraphPtr> &filterList,
            UT_Array<SdfPath> &filterPaths,
            const SdfPath &light_id,
            HdSceneDelegate *sd)
    {
        // Handle the "custom" barndoor parameters on a light since these don't
        // make it through the material network interface (unless prefixed with
        // "inputs:").
        barndoorFilter(scene, filterList, light_id, sd);

        VtValue vfilter = BRAY_HdUtil::evalLightVt(sd, light_id, HdTokens->filters);
        if (vfilter.IsHolding<SdfPathVector>())
        {
            const auto &filters = vfilter.UncheckedGet<SdfPathVector>();
            for (auto &&path : filters)
            {
                VtValue mat = sd->GetMaterialResource(path);
                if (!mat.IsHolding<HdMaterialNetworkMap>())
                {
                    UT_ErrorLog::error("Light {} - filter {} is not a material",
                            light_id, path);
                    continue;
                }
                auto                    netmap = mat.UncheckedGet<HdMaterialNetworkMap>();
                HdMaterialNetwork       net = netmap.map[HdMaterialTerminalTokens->lightFilter];
                // BRAY_HdMaterial::dump(netmap);
                if (!net.nodes.size())
                {
                    UT_ErrorLog::error("Empty light filter {} ({})",
                            path, "missing shaderId?");
                }
                else
                {
                    BRAY::ShaderGraphPtr sg = scene.createShaderGraph(
                                                    BRAY_HdUtil::toStr(path));
                    if (BRAY_HdMaterialNetwork::convert(scene, sg, net,
                            BRAY_HdMaterial::LIGHT_FILTER))
                    {
                        filterList.append(sg);
                        filterPaths.append(path);
                    }
                }
            }
        }
    }
}       // End namespace

void
BRAY_HdLight::updateLightFilter(HdSceneDelegate *sd,
		    BRAY_HdParam *rparm,
                    const SdfPath &filter)
{
    BRAY::ScenePtr	&scene = rparm->getSceneForEdit();

    UT_SmallArray<BRAY::ShaderGraphPtr> filterList;
    UT_SmallArray<SdfPath>              filterPaths;
    buildFilters(scene, filterList, filterPaths, GetId(), sd);
    myLight.updateFilters(scene, filterList);

    scene.updateLight(myLight, BRAY_EVENT_PROPERTIES);
}

void
BRAY_HdLight::finalizeLightFilter(BRAY_HdParam *rparm, const SdfPath &filter)
{
    BRAY::ScenePtr	&scene = rparm->getSceneForEdit();

    myLight.eraseFilter(scene, BRAY_HdUtil::toStr(filter));

    scene.updateLight(myLight, BRAY_EVENT_PROPERTIES);
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
    UT_ErrorLog::format(8, "Sync Light {}", id);
    BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr	&scene = rparm->getSceneForEdit();

    const HdDirtyBits	&bits = *dirtyBits;
    BRAY::OptionSet	lprops;
    BRAY_EventType	event = BRAY_NO_EVENT;
    bool                enabled = sd->GetVisible(id);
    if (!myLight)
	myLight = scene.createLight(BRAY_HdUtil::toStr(id));

    BRAY::OptionSet	oprops = myLight.objectProperties();
    {
        // Apparently DirtyPrimvar bit only gets set for RPrims. For now just
        // fake dirty bit and evaluate every time.
        HdDirtyBits fake = HdChangeTracker::DirtyPrimvar;
        BRAY_HdUtil::updateObjectPrimvarProperties(oprops, *sd, &fake, id,
            HdPrimTypeTokens->light);
    }

    if (bits & DirtyTransform)
    {
	UT_SmallArray<GfMatrix4d>	xforms;
	BRAY_HdUtil::xformBlur(sd, *rparm, id, xforms, oprops);
        if (UT_ErrorLog::isMantraVerbose(8))
        {
            for (int i = 0, n = xforms.size(); i < n; ++i)
            {
                UT_ErrorLog::format(8, "Light {} xform[{}]: {}",
                        id, i, xforms[i]);
            }
        }
	myLight.setTransform(BRAY_HdUtil::makeSpace(xforms.data(),
		    xforms.size(), oprops));
	event = event | BRAY_EVENT_XFORM;
    }

    lprops = myLight.lightProperties();
    if (bits & DirtyParams)
    {
        HdMaterialNetwork       matnet;
        VtValue                 matval = sd->GetMaterialResource(id);

        if (matval.IsHolding<HdMaterialNetworkMap>())
        {
            auto netmap = matval.UncheckedGet<HdMaterialNetworkMap>();
            matnet = netmap.map[HdMaterialTerminalTokens->light];
        }
        else
        {
            // When enableSceneLights is set to false, the scene delegate
            // returns an empty material network.
            enabled = false;
        }

        // Get the light shader id, which affects the "light type" used by
        // karma to represent this light.
        TfToken shaderid;
        if (matnet.nodes.size() > 0)
            shaderid = matnet.nodes[0].identifier;

        // Since the shape can be controlled by parameters other than the type
        // (i.e. sphere render as a point), we need to compute the shape every
        // time we Sync.
        BRAY_LightType ltype = computeLightType(sd, myLightType, shaderid, id);
        lprops.set(BRAY_LIGHT_AREA_SHAPE, int(ltype));
        lprops.set(BRAY_LIGHT_SKY_LIGHT,
            bool(isSkyLight(myLightType, shaderid)));

	// Determine the VEX light shader
	UT_StringArray	shader_args;
	if (enabled &&
            (lightShader(sd, id, shader_args) ||
             !matval.IsHolding<HdMaterialNetworkMap>()))
        {
            GfVec3f     color;
            fpreal32    fval;

            if (!BRAY_HdUtil::evalLight(color, sd, id, HdLightTokens->color))
                color = GfVec3f(1.0);
            if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->intensity))
                color *= fval;
            if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->exposure))
                color *= SYSpow(2, fval);

            // Store the color arguments
            shader_args.append(lightcolorName.asHolder());
            argValue(shader_args, color[0]);
            argValue(shader_args, color[1]);
            argValue(shader_args, color[2]);

            // Set the rest of the arguments in case the shader can use them
            addShaderArgs(shader_args, sd, id, commonLuxTokens());

	    myLight.setShader(scene, shader_args);
        }
        else if (enabled && matnet.nodes.size() > 0)
        {
            UT_StringHolder             name = BRAY_HdUtil::toStr(id);
            BRAY::ShaderGraphPtr        sgraph = scene.createShaderGraph(name);

#if 0
            {
                UT_AutoJSONWriter j(std::cerr, false);
                BRAY_HdMaterial::dump(*j, matnet);
            }
#endif
            if (!BRAY_HdMaterialNetwork::convert(scene, sgraph, matnet,
                        BRAY_HdMaterial::LIGHT,
                        lightMaterialTokens()))
            {
                UT_ErrorLog::error("Unable to convert light shader: {}", id);
            }
            else
            {
                UT_SmallArray<BRAY::ShaderGraphPtr> filterList;
                UT_SmallArray<SdfPath>              filterPaths;
                rparm->eraseLightFilter(this);
                buildFilters(scene, filterList, filterPaths, id, sd);
                for (auto &&path : filterPaths)
                    rparm->addLightFilter(this, path);
                myLight.updateShaderGraph(scene, sgraph, filterList);
            }
        }

	// sampling quality
	setFloat<BRAY_LIGHT_SAMPLING_QUALITY>(lprops, sd, id, 1);
        setScalar<BRAY_LIGHT_SAMPLING_MODE, int>(lprops, sd, id, 0);
	setFloat<BRAY_LIGHT_MIS_BIAS>(lprops, sd, id, 0);
	setFloat<BRAY_LIGHT_ACTIVE_RADIUS>(lprops, sd, id, -1);
	setFloat<BRAY_LIGHT_POINT_RADIUS>(lprops, sd, id, 0);
	setInt<BRAY_LIGHT_HDRI_MAX_ISIZE>(lprops, sd, id, 2048);
	setFloat<BRAY_LIGHT_PORTAL_MIS_BIAS>(lprops, sd, id, 0);
	setBool<BRAY_LIGHT_ILLUM_BACKGROUND>(lprops, sd, id, false);
	setFloat<BRAY_LIGHT_SPREAD>(lprops, sd, id, 1.f);
        if (*lprops.ival(BRAY_LIGHT_AREA_SHAPE) == BRAY_LIGHT_DISTANT)
        {
            fpreal32    fval;
            if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->angle))
                lprops.set(BRAY_LIGHT_DISTANT_ANGLE, fval);
        }

	// The order of evaluation is *very* important.  For spherical lights,
	// we need to evaluate @c radius *after* the width/height, but for tube
	// lights, we need to evaluate length *after* radius.
        fpreal32        width, height, fval;
	if (!BRAY_HdUtil::evalLight(width, sd, id, HdLightTokens->width))
	    width = 1;
	if (!BRAY_HdUtil::evalLight(height, sd, id, HdLightTokens->height))
	    height = 1;
	if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->radius))
	{
	    // Set both width and height to radius
	    width = height = fval;
	}
	if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->length))
	{
	    width = fval;
	}
	{
	    float	res[2];
	    res[0] = width;
	    res[1] = height;
	    lprops.set(BRAY_LIGHT_AREA_SIZE, res, 2);
	}

        bool    bval;
	if (BRAY_HdUtil::evalLight(bval, sd, id, HdLightTokens->normalize))
	    lprops.set(BRAY_LIGHT_NORMALIZE_AREA, bval);

	setBool<BRAY_LIGHT_SINGLE_SIDED>(lprops, sd, id, true);
        setBool<BRAY_LIGHT_RENDER_LIGHT_GEO>(lprops, sd, id, false);
        setBool<BRAY_LIGHT_LIGHT_GEO_CASTS_SHADOW>(lprops, sd, id, false);

        // custom LPE tag
        std::string lpetag;
	TfToken lpetoken(fullPropertyName(BRAY_LIGHT_LPE_TAG), TfToken::Immortal);
	if (BRAY_HdUtil::evalLight(lpetag, sd, id, lpetoken))
            lprops.set(BRAY_LIGHT_LPE_TAG, lpetag.c_str());

	// Shadow tokens
	GfVec3f		color;
	if (BRAY_HdUtil::evalLight(bval, sd, id, HdLightTokens->shadowEnable) && !bval)
            color = GfVec3f(1.0);
	else if (!BRAY_HdUtil::evalLight(color, sd, id, HdLightTokens->shadowColor))
	    color = GfVec3f(0.0);
#if 0
	if (BRAY_HdUtil::evalLight(fval, sd, id, hLightTokens->shadowIntensity))
	    color = color * fval + GfVec3f(1-fval);
#endif
	lprops.set(BRAY_LIGHT_SHADOW_COLOR, color.data(), 3);

	if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->shadowDistance))
	    lprops.set(BRAY_LIGHT_SHADOW_DISTANCE, fval);
        if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->shadowFalloff))
            lprops.set(BRAY_LIGHT_SHADOW_FALLOFF, fval);
        if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->shadowFalloffGamma))
            lprops.set(BRAY_LIGHT_SHADOW_FALLOFF_GAMMA, fval);

	// Diffuse/specular multiplier tokens
	if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->diffuse))
	    lprops.set(BRAY_LIGHT_DIFFUSE_SCALE, fval);
	if (BRAY_HdUtil::evalLight(fval, sd, id, HdLightTokens->specular))
	    lprops.set(BRAY_LIGHT_SPECULAR_SCALE, fval);

        // Contributions
        std::string contribs;
	if (BRAY_HdUtil::evalLight(contribs, sd, id, BRAYHdTokens->karma_light_contribs))
            lprops.set(BRAY_LIGHT_CONTRIBUTIONS, contribs.c_str());

        setBool<BRAY_LIGHT_CONTRIBUTES_CAUSTIC>(lprops, sd, id, true);

        // If the light type is undefined (probably due to an unrecognized
        // shader id), we want to disable the light.
        if (ltype == BRAY_LIGHT_UNDEFINED)
            enabled = false;

	need_lock = true;
    }

    if (*lprops.bval(BRAY_LIGHT_ENABLE) != enabled)
    {
	lprops.set(BRAY_LIGHT_ENABLE, enabled);
	need_lock = true;
    }

    if (bits & DirtyCollection)
    {
	VtValue val = BRAY_HdUtil::evalLightVt(sd, id, HdTokens->lightLink);
	if (val.IsHolding<TfToken>())
	{
	    TfToken tok = val.UncheckedGet<TfToken>();
	    if (!tok.IsEmpty())
	    {
                const UT_StringHolder *prevcat =
                    lprops.sval(BRAY_LIGHT_CATEGORY);

                UT_ASSERT(prevcat);
                if (!prevcat || *prevcat != tok)
                {
                    rparm->addLightCategory(tok.GetText());
                    if (*prevcat)
                        rparm->eraseLightCategory(*prevcat);
                }
		lprops.set(BRAY_LIGHT_CATEGORY, tok.GetText());
	    }
	}
	val = BRAY_HdUtil::evalLightVt(sd, id, HdTokens->shadowLink);
	if (val.IsHolding<TfToken>())
	{
	    TfToken tok = val.UncheckedGet<TfToken>();
	    if (!tok.IsEmpty())
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

    // AND with ~AllDirty will no longer clear all the dirty bits which could
    // lead to karma getting stuck in render restart loop. Maybe it's got to do
    // with usd 21.11 update but haven't verified it.
    *dirtyBits = Clean;
}

HdDirtyBits
BRAY_HdLight::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
