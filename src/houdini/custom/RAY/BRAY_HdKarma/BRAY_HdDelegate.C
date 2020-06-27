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

#include <pxr/imaging/hd/rprim.h>
#include "BRAY_HdDelegate.h"

#include "BRAY_HdAOVBuffer.h"
#include "BRAY_HdCamera.h"
#include "BRAY_HdInstancer.h"
#include "BRAY_HdPass.h"
#include "BRAY_HdCurves.h"
#include "BRAY_HdField.h"
#include "BRAY_HdPointPrim.h"
#include "BRAY_HdMesh.h"
#include "BRAY_HdMaterial.h"
#include "BRAY_HdLight.h"
#include "BRAY_HdVolume.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdIO.h"

#include <iostream>
#include <UT/UT_Debug.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_ErrorLog.h>
#include <SYS/SYS_Pragma.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_Tokens.h>
#include <FS/UT_DSO.h>

#include <pxr/base/gf/size2.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/bprim.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/extComputation.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdRender/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

SYS_PRAGMA_PUSH_WARN();
SYS_PRAGMA_DISABLE_DEPRECATED();

std::mutex BRAY_HdDelegate::_mutexResourceRegistry;
std::atomic_int BRAY_HdDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr BRAY_HdDelegate::_resourceRegistry;

namespace
{
    #define PARAMETER_PREFIX	"karma:"	// See BRAY_HdUtil.h

static constexpr UT_StringLit	theDenoise(R"(["denoise", { )"
    R"("engine": "any",)"
    R"("use_n_input": true,)"
    R"("use_albedo_input": true,)"
    R"("use_gl_output": false }])");

static constexpr UT_StringLit	theUniformOracle("\"uniform\"");

static TfTokenVector SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->points,
    HdPrimTypeTokens->mesh,
    HdPrimTypeTokens->basisCurves,
    HdPrimTypeTokens->volume,
};

static TfTokenVector SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->material,
    HdPrimTypeTokens->distantLight,
    HdPrimTypeTokens->rectLight,
    HdPrimTypeTokens->sphereLight,
    HdPrimTypeTokens->diskLight,
    HdPrimTypeTokens->cylinderLight,
    HdPrimTypeTokens->domeLight,
    HdPrimTypeTokens->extComputation
};

static TfTokenVector SUPPORTED_BPRIM_TYPES =
{
    HdPrimTypeTokens->renderBuffer,
    HusdHdPrimTypeTokens()->openvdbAsset,
    HusdHdPrimTypeTokens()->bprimHoudiniFieldAsset,
};

static void
initScene(BRAY::ScenePtr &bscene, const HdRenderSettingsMap &settings)
{
    //UTdebugFormat("RenderSettings");
    //for (auto &&it : settings) BRAY_HdUtil::dumpValue(it.second, it.first);
    BRAY_HdUtil::updateSceneOptions(bscene, settings);

    bscene.commitOptions();
}

/// If any of these settings change, then we need to tell the scene to redice
/// geometry.
static const UT_Set<TfToken> &
rediceSettings()
{
    static UT_Set<TfToken>	theRediceSettings({
	TfToken(PARAMETER_PREFIX "global:dicingcamera", TfToken::Immortal),
	TfToken(PARAMETER_PREFIX "global:resolution", TfToken::Immortal),
	TfToken(PARAMETER_PREFIX "global:offscreenquality", TfToken::Immortal),
	TfToken(PARAMETER_PREFIX "object:dicingquality", TfToken::Immortal),
	TfToken(PARAMETER_PREFIX "object:mblur", TfToken::Immortal),
	TfToken(PARAMETER_PREFIX "object:vblur", TfToken::Immortal),
	TfToken(PARAMETER_PREFIX "object:geosamples", TfToken::Immortal),
	TfToken(PARAMETER_PREFIX "object:xformsamples", TfToken::Immortal),
    });
    return theRediceSettings;
}

static bool
bray_stopRequested(void *p)
{
    if (!p)
	return false;
    return ((PXR_INTERNAL_NS::HdRenderThread*)p)->IsStopRequested();
}

static bool
bray_ChangeBool(const VtValue &value, bool &org)
{
    bool	bval;

    if (value.IsHolding<bool>())
	bval = value.UncheckedGet<bool>();
    else if (value.IsHolding<int32>())
	bval = value.UncheckedGet<int32>() != 0;
    else if (value.IsHolding<uint32>())
	bval = value.UncheckedGet<uint32>() != 0;
    else if (value.IsHolding<int64>())
	bval = value.UncheckedGet<int64>() != 0;
    else if (value.IsHolding<uint64>())
	bval = value.UncheckedGet<uint64>() != 0;
    else if (value.IsHolding<int8>())
	bval = value.UncheckedGet<int8>() != 0;
    else if (value.IsHolding<uint8>())
	bval = value.UncheckedGet<uint8>() != 0;
    else
    {
	UT_ASSERT(0 && "Unhandled bool type");
	return false;
    }
    if (bval == org)
	return false;
    org = bval;
    return true;
}

template <typename INT_TYPE>
static bool
bray_ChangeInt(const VtValue &value, INT_TYPE &org)
{
    INT_TYPE	bval;

    if (value.IsHolding<int32>())
	bval = value.UncheckedGet<int32>();
    else if (value.IsHolding<uint32>())
	bval = value.UncheckedGet<uint32>();
    else if (value.IsHolding<int64>())
	bval = value.UncheckedGet<int64>();
    else if (value.IsHolding<uint64>())
	bval = value.UncheckedGet<uint64>();
    else if (value.IsHolding<int8>())
	bval = value.UncheckedGet<int8>();
    else if (value.IsHolding<uint8>())
	bval = value.UncheckedGet<uint8>();
    else
    {
	UT_ASSERT(0 && "Unhandled int type");
	return false;
    }
    if (bval == org)
	return false;
    org = bval;
    return true;
}

template <typename FLT_TYPE>
static bool
bray_ChangeReal(const VtValue &value, FLT_TYPE &org)
{
    FLT_TYPE	bval;

    if (value.IsHolding<fpreal32>())
	bval = value.UncheckedGet<fpreal32>();
    else if (value.IsHolding<fpreal64>())
	bval = value.UncheckedGet<fpreal64>();
    else if (value.IsHolding<fpreal16>())
	bval = value.UncheckedGet<fpreal16>();
    else if (value.IsHolding<int32>())		// TODO: Just call changeInt()?
	bval = value.UncheckedGet<int32>();
    else if (value.IsHolding<uint32>())
	bval = value.UncheckedGet<uint32>();
    else if (value.IsHolding<int64>())
	bval = value.UncheckedGet<int64>();
    else if (value.IsHolding<uint64>())
	bval = value.UncheckedGet<uint64>();
    else if (value.IsHolding<int8>())
	bval = value.UncheckedGet<int8>();
    else if (value.IsHolding<uint8>())
	bval = value.UncheckedGet<uint8>();
    else
    {
	UT_ASSERT(0 && "Unhandled int type");
	return false;
    }
    if (bval == org)
	return false;
    org = bval;
    return true;
}

enum BRAY_HD_RENDER_SETTING
{
    BRAY_HD_DATAWINDOW,
    BRAY_HD_RESOLUTION,
    BRAY_HD_SHUTTER_OPEN,
    BRAY_HD_SHUTTER_CLOSE,
    BRAY_HD_PIXELASPECT,
    BRAY_HD_CONFORMPOLICY,
    BRAY_HD_INSTANTSHUTTER,
};

static UT_Map<TfToken, BRAY_HD_RENDER_SETTING>	theSettingsMap({
    { UsdRenderTokens->dataWindowNDC,		BRAY_HD_DATAWINDOW },
    { UsdRenderTokens->resolution,		BRAY_HD_RESOLUTION },
    { UsdGeomTokens->shutterOpen,		BRAY_HD_SHUTTER_OPEN },
    { UsdGeomTokens->shutterClose,		BRAY_HD_SHUTTER_CLOSE },
    { UsdRenderTokens->pixelAspectRatio,	BRAY_HD_PIXELASPECT },
    { UsdRenderTokens->aspectRatioConformPolicy, BRAY_HD_CONFORMPOLICY },
    { UsdRenderTokens->instantaneousShutter,	BRAY_HD_INSTANTSHUTTER },
});

bool
updateRenderParam(BRAY_HdParam &rparm, BRAY_HD_RENDER_SETTING type,
	const VtValue &value)
{
    switch (type)
    {
	case BRAY_HD_DATAWINDOW:
	    return rparm.setDataWindow(value);
	case BRAY_HD_RESOLUTION:
	    return rparm.setResolution(value);
	case BRAY_HD_SHUTTER_OPEN:
	    return rparm.setShutter<0>(value);
	case BRAY_HD_SHUTTER_CLOSE:
	    return rparm.setShutter<1>(value);
	case BRAY_HD_PIXELASPECT:
	    return rparm.setPixelAspect(value);
	case BRAY_HD_CONFORMPOLICY:
	    return rparm.setConformPolicy(value);
	case BRAY_HD_INSTANTSHUTTER:
	    return rparm.setInstantShutter(value);
    }
    return false;
}

}


BRAY_HdDelegate::BRAY_HdDelegate(const HdRenderSettingsMap &settings)
    : myScene()
    , mySDelegate(nullptr)
    , myInteractionMode(BRAY_INTERACTION_NORMAL)
    , mySceneVersion(0)
    , myVariance(0.001)
    , myDisableLighting(false)
    , myEnableDenoise(false)
{
    myScene = BRAY::ScenePtr::allocScene();
    myRenderer = BRAY::RendererPtr::allocRenderer(myScene);

    initScene(myScene, settings);

    myScene.sceneOptions().import(BRAY_OPT_DISABLE_LIGHTING,
	    &myDisableLighting, 1);

    // Initialize the proxy depth from the initial scene value
    myRenderParam = UTmakeUnique<BRAY_HdParam>(myScene,
		myRenderer,
		myThread,
		mySceneVersion);

    // Now, handle special render settings
    for (auto &&item : theSettingsMap)
    {
	auto it = settings.find(item.first);
	if (it != settings.end())
	    updateRenderParam(*myRenderParam, item.second, it->second);
    }

    // TODO: need to get FPS from somewhere
    BRAY::OptionSet options = myScene.sceneOptions();
    options.set(BRAY_OPT_FPS, 24);
    myRenderParam->setFPS(24);

    myThread.SetRenderCallback(
	    std::bind(&BRAY::RendererPtr::render,
		&myRenderer,
		&bray_stopRequested,
		&myThread));
    myThread.StartThread();

    // Initialize one resource registry for all karma plugins
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);

    if (_counterResourceRegistry.fetch_add(1) == 0)
    {
        _resourceRegistry.reset( new HdResourceRegistry() );
    }
}

BRAY_HdDelegate::~BRAY_HdDelegate()
{
    stopRender(false);
    myThread.StopThread();	// Now actually shut down the thread

    // Clean the resource registry only when it is the last Karma delegate
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);

    if (_counterResourceRegistry.fetch_sub(1) == 1) {
        _resourceRegistry.reset();
    }
}

HdRenderParam *
BRAY_HdDelegate::GetRenderParam() const
{
    return myRenderParam.get();
}

void
BRAY_HdDelegate::CommitResources(HdChangeTracker *tracker)
{
    //UTdebugFormat("Commit resources: {}", tracker);
    // CommitResources() is called after prim sync has finished, but before any
    // tasks (such as draw tasks) have run. HdKarma primitives have already
    // updated buffer pointers and dirty state in prim Sync(), but we
    // still need to rebuild acceleration datastructures ...
    //
    // During task execution, the scene is treated as read-only by the
    // drawing code; the BVH won't be updated until the next time through
    // HdEngine::Execute().
    // TODO: Update scene graph
    //myScene.scene()->sceneGraph()->dump();
}

TfToken
BRAY_HdDelegate::GetMaterialBindingPurpose() const
{
    return HdTokens->full;
}

TfToken
BRAY_HdDelegate::GetMaterialNetworkSelector() const
{
    static TfToken theKarmaToken("karma", TfToken::Immortal);

    return theKarmaToken;
}

TfTokenVector
BRAY_HdDelegate::GetShaderSourceTypes() const
{
    static TfTokenVector theSourceTypes({
            TfToken("VEX", TfToken::Immortal)
    });

    return theSourceTypes;
}

TfTokenVector const&
BRAY_HdDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const&
BRAY_HdDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
BRAY_HdDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr
BRAY_HdDelegate::GetResourceRegistry() const
{
    return _resourceRegistry;
}

void
BRAY_HdDelegate::stopRender(bool inc_version)
{
    myRenderer.prepareForStop();
    myThread.StopRender();
    UT_ASSERT(!myRenderer.isRendering());
    if (inc_version)
	mySceneVersion.add(1);
}

bool
BRAY_HdDelegate::headlightSetting(const TfToken &key, const VtValue &value)
{
    static const TfToken	renderCameraPath("renderCameraPath",
				    TfToken::Immortal);
    static const TfToken        karmaGlobalCamera("karma:global:rendercamera",
                                    TfToken::Immortal);
    static const TfToken	hydraDisableLighting(
				    PARAMETER_PREFIX "hydra:disablelighting",
				    TfToken::Immortal);
    static const TfToken	hydraDenoise(
				    PARAMETER_PREFIX "hydra:denoise",
				    TfToken::Immortal);
    static const TfToken	hydraVariance(
				    PARAMETER_PREFIX "hydra:variance",
				    TfToken::Immortal);
    static const TfToken	theStageUnits("stageMetersPerUnit",
				    TfToken::Immortal);

    if (key == renderCameraPath || key == karmaGlobalCamera)
    {
	// We need to stop the render before changing any global settings
	stopRender();
	myRenderParam->setCameraPath(value);
	return true;
    }
    if (key == theStageUnits)
    {
	fpreal64	prev = myScene.sceneUnits();
	fpreal64	units = prev;
	if (!bray_ChangeReal(value, units))
	    return true;
	// We can be more tolerand, so check 32-bit values are almost equal.
	if (SYSalmostEqual(fpreal32(prev), fpreal32(units)))
	    return true;

	// Stop render before changing scene units
	stopRender();
	myScene.setSceneUnits(units);
	return true;
    }

    if (key == hydraDisableLighting)
    {
	if (!bray_ChangeBool(value, myDisableLighting))
	    return true;	// Nothing changed, but lighting option
    }
    else if (key == hydraDenoise)
    {
	if (!bray_ChangeBool(value, myEnableDenoise))
	    return true;	// Nothing changed, but dnoise option
    }
    else if (key == hydraVariance)
    {
	if (!bray_ChangeReal(value, myVariance))
	    return true;	// Nothing changed, but dnoise option
    }
    else
    {
	// Not a headlight option
	return false;
    }

    // Something has changed with the headlight mode -- We need to stop the
    // render before changing global options.
    stopRender();

    BRAY::OptionSet	options = myScene.sceneOptions();
    if (myEnableDenoise)
	options.set(BRAY_OPT_IMAGEFILTER, theDenoise.asHolder());
    else
	options.set(BRAY_OPT_IMAGEFILTER, UT_StringHolder::theEmptyString);

    if (myVariance > 0)
    {
	UT_WorkBuffer	tmp;
	tmp.sprintf("[\"variance\", {\"variance\":%g}]", myVariance);
	options.set(BRAY_OPT_PIXELORACLE, UT_StringHolder(tmp));
    }
    else
	options.set(BRAY_OPT_PIXELORACLE, theUniformOracle.asHolder());

    if (!myDisableLighting)
    {
	options.set(BRAY_OPT_DISABLE_LIGHTING, false);	// Don't force headlight
    }
    else
    {
	options.set(BRAY_OPT_DISABLE_LIGHTING, true);
    }

    return true;
}

namespace
{
    const char *
    valueAsString(const VtValue &v)
    {
	if (v.IsHolding<std::string>())
	    return v.UncheckedGet<std::string>().c_str();
	if (v.IsHolding<TfToken>())
	    return v.UncheckedGet<TfToken>().GetText();
	if (v.IsHolding<UT_StringHolder>())
	    return v.UncheckedGet<UT_StringHolder>().c_str();
	return "";
    }
}

bool
BRAY_HdDelegate::Pause()
{
    if(!myRenderer.isPaused())
    {
        myRenderer.pauseRender();
        return true;
    }
    return false;
}

bool
BRAY_HdDelegate::Resume()
{
    if(myRenderer.isPaused())
    {
        myRenderer.resumeRender();
        return true;
    }
    return false;
}

void
BRAY_HdDelegate::SetRenderSetting(const TfToken &key, const VtValue &value)
{
    //BRAY_HdUtil::dumpValue(value, key);
    static TfToken	theHoudiniInteractive("houdini:interactive",
				TfToken::Immortal);
    static TfToken	thePauseRender("houdini:render_pause",
				TfToken::Immortal);
    static TfToken	theDelegateRenderProducts("delegateRenderProducts",
				TfToken::Immortal);

    auto rset = theSettingsMap.find(key);
    if (rset != theSettingsMap.end())
    {
	if (updateRenderParam(*myRenderParam, rset->second, value))
	    stopRender();
	return;
    }

    if (headlightSetting(key, value))
	return;

    if (key == theDelegateRenderProducts)
    {
        delegateRenderProducts(value);
        return;
    }

    if (key == thePauseRender)
    {
	bool	paused = myRenderer.isPaused();
	if (bray_ChangeBool(value, paused))
	{
	    if (paused)
	    {
		myRenderer.pauseRender();
	    }
	    else
	    {
		myRenderer.resumeRender();
	    }
	}
	return;	// Don't restart
    }

    if (key == theHoudiniInteractive)
    {
	const char *sval = valueAsString(value);
	UT_ASSERT(UTisstring(sval));
	BRAY_InteractionType imode = BRAYinteractionType(sval);
	if (imode != myInteractionMode)
	{
	    stopRender();
	    myScene.setOption(BRAY_OPT_IPR_INTERACTION, int(imode));
	}
	return;
    }

    if (BRAY_HdUtil::sceneOptionNeedUpdate(myScene, key, value))
    {
	stopRender();
	if (rediceSettings().contains(key))
	{
	    UTdebugFormat("Need update: {}", key);
	    myScene.forceRedice();
	}

	// Renderer cannot be running when we update options
	UT_ASSERT(!myRenderer.isRendering());
	BRAY_HdUtil::updateSceneOption(myScene, key, value);
    }
}

void
BRAY_HdDelegate::delegateRenderProducts(const VtValue &value)
{
    BRAY::OutputFile::clearFiles(myScene);
    if (value.IsEmpty())
        return;

    using delegateProduct = HdAovSettingsMap;
    using delegateVar = HdAovSettingsMap;
    using delegateProductList = VtArray<delegateProduct>;
    using delegateVarList = VtArray<delegateVar>;

    static const TfToken productName("productName", TfToken::Immortal);
    static const TfToken productType("productType", TfToken::Immortal);
    static const TfToken orderedVars("orderedVars", TfToken::Immortal);
    static const TfToken sourceName("sourceName", TfToken::Immortal);
    static const TfToken aovSettings("aovDescriptor.aovSettings", TfToken::Immortal);
    static const TfToken aovName("driver:parameters:aov:name", TfToken::Immortal);

    auto findString = [](const HdAovSettingsMap &map, const TfToken &token)
    {
        auto it = map.find(token);
        if (it == map.end())
            return "";
        return valueAsString(it->second);
    };

    UT_ASSERT(value.IsHolding<delegateProductList>());
    delegateProductList plist = value.Get<delegateProductList>();
    for (const auto &prod : plist)
    {
        UT_StringHolder type = findString(prod, productType);
        UT_StringHolder name = findString(prod, productName);
        if (!name || !BRAY::OutputFile::isKnownType(type))
            continue;           // Missing name or type
        BRAY::OutputFile        file(myScene, name, type);
        UT_Options              opts;
        for (const auto &opt : prod)
        {
            if (opt.first == orderedVars)
            {
                UT_ASSERT(opt.second.IsHolding<delegateVarList>());
                delegateVarList vlist = opt.second.Get<delegateVarList>();
                for (const auto &var : vlist)
                {
                    UT_Options          aovopt;
                    UT_StringHolder     aovname;
                    auto sit = var.find(aovSettings);
                    if (sit == var.end())
                        continue;

                    delegateVar props = sit->second.Get<delegateVar>();
                    for (auto &&p : props)
                    {
                        if (p.first == aovName)
                            aovname = valueAsString(p.second);
                        if (!BRAY_HdUtil::addOption(aovopt, p.first.GetText(), p.second))
                            UTdebugFormat("Error setting var {}", p.first);
                    }
                    if (aovname)
                    {
                        auto &&aov = file.appendAOV(aovname);
                        aov.setOptions(aovopt);
                    }
                }
            }
            else
            {
                if (!BRAY_HdUtil::addOption(opts, opt.first.GetText(), opt.second))
                    UTdebugFormat("Unable to add option {}", opt.first);
            }
        }
        file.setOptions(opts);
    }
}

HdAovDescriptor
BRAY_HdDelegate::GetDefaultAovDescriptor(const TfToken &name) const
{
    if (name == HdAovTokens->color)
    {
	return HdAovDescriptor(HdFormatFloat16Vec4, true,
		VtValue(GfVec4h(0)));
    }
    if (name == HdAovTokens->normal || name == HdAovTokens->Neye)
    {
	return HdAovDescriptor(HdFormatFloat16Vec3, false,
		VtValue(GfVec3f(-1)));
    }
    if (name == HdAovTokens->depth)
	return HdAovDescriptor(HdFormatFloat32, false, VtValue(1e17f));
    if (name == HdAovTokens->primId)
	return HdAovDescriptor(HdFormatInt32, false, VtValue(0));
    if (name == HdAovTokens->elementId)
	return HdAovDescriptor(HdFormatInt32, false, VtValue(0));
    if (name == HdAovTokens->instanceId)
	return HdAovDescriptor(HdFormatInt32, false, VtValue(0));

    HdParsedAovToken	aov(name);
    if (aov.isLpe)
    {
	return HdAovDescriptor(HdFormatFloat16Vec3, true, VtValue(GfVec3f(0)));
    }
    if (aov.isPrimvar)
    {
	return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0)));
    }
    return HdAovDescriptor();
}

class RenderNameGetter
{
public:
    RenderNameGetter(const BRAY::OptionSet &opts)
    {
	UT_StringHolder	rname;
	if (opts.import(BRAY_OPT_RENDERER, &rname, 1))
	    myString = rname.toStdString();
	std::fill(myVersion, myVersion+3, 0);
	opts.import(BRAY_OPT_VERSION, myVersion, 3);
    }
    const std::string	&str() const { return myString; }
    void		getVersion(int v[3]) const
    {
	std::copy(myVersion, myVersion+3, v);
    }
private:
    std::string	myString;
    int		myVersion[3];
};

const std::string &
getRendererName(const BRAY::OptionSet &opts, int version[3])
{
    static RenderNameGetter renderer(opts);
    renderer.getVersion(version);
    return renderer.str();;
}

static GfMatrix4d
convertM4(const UT_Matrix4D &m)
{
    GfMatrix4d	gm;
    std::copy(m.data(), m.data()+16, gm.GetArray());
    return gm;
}


VtDictionary
BRAY_HdDelegate::GetRenderStats() const
{
    VtDictionary	stats;
    if (myRenderer)
    {
	const auto &s = myRenderer.renderStats();
	const auto &stokens = HusdHdRenderStatsTokens();
#define SET_ITEM(KEY, ITEM) \
	    stats[stokens->KEY] = VtValue(ITEM); \
	    /* end macro */
#define SET_ITEM2(KEY, ITEM) \
	    stats[stokens->KEY] = VtValue(GfSize2(ITEM.x(), ITEM.y())); \
	    /* end macro */

	GfVec3i	version;
	const std::string &rname = getRendererName(myScene.sceneOptions(),
					version.data());
	if (rname.size())
	{
	    SET_ITEM(rendererName, rname);
	    SET_ITEM(rendererVersion, version);
	}

	SET_ITEM(percentDone, s.myPercentDone);

	SET_ITEM(worldToCamera, convertM4(s.myWorldToCamera));
	SET_ITEM(worldToScreen, convertM4(s.myWorldToScreen));

	SET_ITEM(cameraRays, s.myCameraRays);
	SET_ITEM(indirectRays, s.myIndirectRays);
	SET_ITEM(occlusionRays, s.myOcclusionRays);
	SET_ITEM(lightGeoRays, s.myLightGeoRays);
	SET_ITEM(probeRays, s.myProbeRays);

	SET_ITEM2(polyCounts, s.myPolyCount);
	SET_ITEM2(curveCounts, s.myCurveCount);
	SET_ITEM2(pointCounts, s.myPointCount);
	SET_ITEM2(pointMeshCounts, s.myPointMeshCount);
	SET_ITEM2(volumeCounts, s.myVolumeCount);
	SET_ITEM2(proceduralCounts, s.myProceduralCount);
	SET_ITEM(lightCounts, s.myLightCount);
	SET_ITEM(lightTreeCounts, s.myLightTreeCount);
	SET_ITEM(cameraCounts, s.myCameraCount);

	SET_ITEM(octreeBuildTime, s.myOctreeBuildTime);
	SET_ITEM(loadClockTime, s.myLoadWallClock);
	SET_ITEM(loadUTime, s.myLoadCPU);
	SET_ITEM(loadSTime, s.myLoadSystem);
	SET_ITEM(loadMemory, s.myLoadMemory);

	SET_ITEM(totalClockTime, s.myTotalWallClock);
	SET_ITEM(totalUTime, s.myTotalCPU);
	SET_ITEM(totalSTime, s.myTotalSystem);
	SET_ITEM(totalMemory, s.myCurrentMemory);

	SET_ITEM(peakMemory, s.myPeakMemory);
#undef SET_ITEM
#undef SET_ITEM2

	// Extra, tokens, just for Karma
	static const TfToken	primvarStats("primvarStats");
	static const TfToken	filterErrors("filterErrors");
	static const TfToken	detailedTimes("detailedTimes");
	if (s.myPrimvar)
	    stats[primvarStats] = VtValue(s.myPrimvar);
	if (s.myFilterErrors.size())
	    stats[filterErrors] = VtValue(s.myFilterErrors);
	if (s.myDetailedTimes)
	    stats[detailedTimes] = VtValue(s.myDetailedTimes);
    }
    return stats;
}

HdRenderPassSharedPtr
BRAY_HdDelegate::CreateRenderPass(HdRenderIndex *index,
                            HdRprimCollection const& collection)
{
    UT_ASSERT(myScene.isValid());
    UT_ASSERT(myRenderer);
    return HdRenderPassSharedPtr(
        new BRAY_HdPass(index,
	    collection,
	    *myRenderParam,
	    myRenderer,
	    myThread,
	    mySceneVersion,
	    myScene));
}

HdInstancer *
BRAY_HdDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                        SdfPath const& id,
                                        SdfPath const& instancerId)
{
    UT_ASSERT(!mySDelegate || delegate == mySDelegate);
    if (delegate)
	mySDelegate = delegate;
#if 0
    UTdebugFormat("Create Instancer: {} '{}'", id, instancerId);
    HdInstancer	*inst = findInstancer(instancerId);
    if (inst)
	UTverify_cast<BRAY_HdInstancer *>(inst)->AddPrototype(id);
#endif

    return new BRAY_HdInstancer(delegate, id, instancerId);
}

void
BRAY_HdDelegate::DestroyInstancer(HdInstancer *instancer)
{
    UT_ASSERT(instancer);
    auto minst = UTverify_cast<BRAY_HdInstancer *>(instancer);
    minst->eraseFromScenegraph(myScene);
    delete instancer;
}

HdInstancer *
BRAY_HdDelegate::findInstancer(const SdfPath &id) const
{
    if (!mySDelegate || id.IsEmpty())
	return nullptr;
    return mySDelegate->GetRenderIndex().GetInstancer(id);
}

HdRprim *
BRAY_HdDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId,
                                    SdfPath const& instancerId)
{
#if 0
    HdInstancer	*inst = findInstancer(instancerId);
    UTdebugFormat("Add rprim {} to {} ({})", rprimId, instancerId, inst);
    if (inst)
	UTverify_cast<BRAY_HdInstancer *>(inst)->AddPrototype(rprimId);
#endif
    BRAYformat(9, "Create HdRprim: {} {} {}", typeId, rprimId, instancerId);

    if (typeId == HdPrimTypeTokens->points)
    {
	return new BRAY_HdPointPrim(rprimId, instancerId);
    }
    else if (typeId == HdPrimTypeTokens->mesh)
    {
        return new BRAY_HdMesh(rprimId, instancerId);
    }
    else if (typeId == HdPrimTypeTokens->basisCurves)
    {
        return new BRAY_HdCurves(rprimId, instancerId);
    }
    else if (typeId == HdPrimTypeTokens->volume)
    {
	return new BRAY_HdVolume(rprimId, instancerId);
    }
    else
    {
        TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
    }

    return nullptr;
}

void
BRAY_HdDelegate::DestroyRprim(HdRprim *rPrim)
{
#if 0
    // TODO: The instancer probably needs to have the rprim deleted in some
    // fashion.
    HdInstancer	*inst = findInstancer(rPrim->GetInstancerId());
    if (inst)
	UTverify_cast<BRAY_HdInstancer *>(inst)->RemovePrototype(rPrim->GetId());
#endif
    delete rPrim;
}

HdSprim *
BRAY_HdDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    // There will be more materials than cameras/lights, so test this first
    BRAYformat(9, "Create HdSprim: {} {}", typeId, sprimId);
    if (typeId == HdPrimTypeTokens->material)
    {
        return new BRAY_HdMaterial(sprimId);
    }
    if (typeId == HdPrimTypeTokens->extComputation)
    {
	return new HdExtComputation(sprimId);
    }

    // More lights than cameras, so test them next
    if (typeId == HdPrimTypeTokens->distantLight
	|| typeId == HdPrimTypeTokens->rectLight
	|| typeId == HdPrimTypeTokens->sphereLight
	|| typeId == HdPrimTypeTokens->diskLight
	|| typeId == HdPrimTypeTokens->cylinderLight
	|| typeId == HdPrimTypeTokens->domeLight)
    {
	return new BRAY_HdLight(typeId, sprimId);
    }

    // Test for cameras
    if (typeId == HdPrimTypeTokens->camera)
    {
	// Set default render camera
	return new BRAY_HdCamera(sprimId);
    }

    TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());

    return nullptr;
}

HdSprim *
BRAY_HdDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    BRAYformat(9, "Create Fallback Sprim: {}", typeId);
    return CreateSprim(typeId, SdfPath::EmptyPath());
}

void
BRAY_HdDelegate::DestroySprim(HdSprim *sPrim)
{
    delete sPrim;
}

HdBprim *
BRAY_HdDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    BRAYformat(9, "Create HdBprim: {} {}", typeId, bprimId);

    if (typeId == HdPrimTypeTokens->renderBuffer)
    {
	return new BRAY_HdAOVBuffer(bprimId);
    }
    else if (typeId == HusdHdPrimTypeTokens()->openvdbAsset||
	     typeId == HusdHdPrimTypeTokens()->bprimHoudiniFieldAsset)
    {
	return new BRAY_HdField(typeId, bprimId);
    }
    else
    {
	TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    }

    return nullptr;
}

HdBprim *
BRAY_HdDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    // usdview calls fallback without an SdfPath
    //UTdebugFormat("Create Fallback Bprim: '{}'", typeId);
    return CreateBprim(typeId, SdfPath::EmptyPath());
}

void
BRAY_HdDelegate::DestroyBprim(HdBprim *bPrim)
{
    delete bPrim;
}

SYS_PRAGMA_POP_WARN();

PXR_NAMESPACE_CLOSE_SCOPE
