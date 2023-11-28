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

#include "HUSD_Imaging.h"
#include "HUSD_Compositor.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_HydraGeoPrim.h"
#include "HUSD_HydraMaterial.h"
#include "HUSD_Info.h"
#include "HUSD_LightingMode.h"
#include "HUSD_Overrides.h"
#include "HUSD_Preferences.h"
#include "HUSD_Scene.h"
#include "HUSD_TimeCode.h"

#include "XUSD_Data.h"
#include "XUSD_Format.h"
#include "XUSD_ImagingEngine.h"
#include "XUSD_PathSet.h"
#include "XUSD_RenderSettings.h"
#include "XUSD_Tokens.h"
#include "XUSD_Utils.h"

#include <gusd/UT_Gf.h>
#include <OP/OP_Director.h>
#include <GVEX/GVEX_GeoCache.h>
#include <PXL/PXL_OCIO.h>
#include <PXL/PXL_Fill.h>
#include <PXL/PXL_Raster.h>
#include <TIL/TIL_TextureMap.h>
#include <UT/UT_Array.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Defines.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_ErrorManager.h>
#include <UT/UT_Exit.h>
#include <UT/UT_Signal.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_PerfMonAutoEvent.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_SysClone.h>
#include <UT/UT_TaskGroup.h>
#include <UT/UT_Tracing.h>
#include <tools/henv.h>

#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/gf/size2.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/imaging/hd/rprim.h>

#include <algorithm>
#include <iostream>
#include <initializer_list>
#include <thread>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    // Count of the number of render engines that use the texture cache. The
    // cache can only be cleared if there are no active renders.
    SYS_AtomicInt<int>       theTextureCacheRenders(0);
    // Track active HUSD_Imaging objects so we can clean up any running
    // renderers during Houdini shutdown.
    UT_Set<HUSD_Imaging *>	 theActiveRenders;
    UT_Lock			 theActiveRenderLock;
    HUSD_RendererInfoMap	 theRendererInfoMap;

    bool
    renderUsesTextureCache(const UT_StringRef &name)
    {
        return name == HUSD_Constants::getKarmaRendererPluginName();
    }

    PXL_DataFormat
    HdToPXL(HdFormat df)
    {
        switch(HdGetComponentFormat(df))
        {
        case HdFormatUNorm8:
            return PXL_INT8;
        case HdFormatSNorm8:
            return PXL_INT8; // We don't have a format for this.
        case HdFormatFloat16:
            return PXL_FLOAT16;
        case HdFormatFloat32:
            return PXL_FLOAT32;
        case HdFormatInt32:
            return PXL_INT32;
        default:
            break;
        }
        // bad format?
        return PXL_INT8;
    }

    void
    backgroundRenderExitCB(void *data)
    {
        UT_Lock::Scope               lock(theActiveRenderLock);
        for (auto &&item : theActiveRenders)
            item->terminateRender(true);
    }

    void
    backgroundRenderState(bool converged, HUSD_Imaging *ptr)
    {
        // We don't want to run our cleanup code if we are here because we
        // are running the exit callbacks. No need to keep static data
        // structures up to date, or de-register exit callbacks. Both of
        // these operations would trigger crashes during shutdown.
        if (!UT_Exit::isExiting())
        {
            UT_Lock::Scope lock(theActiveRenderLock);
            if (converged)
            {
                theActiveRenders.erase(ptr);
                if (theActiveRenders.size() == 0)
                    UT_Exit::removeExitCallback(backgroundRenderExitCB);
            }
            else
            {
                UT_ASSERT(theActiveRenders.count(ptr) == 0);
                if (theActiveRenders.size() == 0)
                    UT_Exit::addExitCallback(backgroundRenderExitCB, nullptr);
                theActiveRenders.insert(ptr);
            }
        }
    }
}       // End namespace

class husd_DefaultRenderSettingContext : public XUSD_RenderSettingsContext
{
public: 
    TfToken	renderer() const override
        { return TfToken(""); }
    fpreal      startFrame() const override
        { return myFrame; }
    UsdTimeCode evalTime() const override
        { return UsdTimeCode(myFrame); }
    GfVec2i	defaultResolution() const override
        { return GfVec2i(myW,myH); }
    SdfPath	overrideCamera() const override
        { return myCameraPath; }

    HdAovDescriptor
    defaultAovDescriptor(const TfToken &aov) const override
        { return HdAovDescriptor(); }

    bool getAovDescriptor(TfToken &aov, HdAovDescriptor &desc) const
        {
            auto entry = myAOVs.find(aov.GetText());
            if(entry != myAOVs.end())
            {
                desc = entry->second;
                return true;
            }
            if(aov == HdAovTokens->depth)
            {
                VtValue zero((float)0);
                desc = HdAovDescriptor(HdFormatFloat32, false, zero);
                return true;
            }
            if(aov == HdAovTokens->primId || aov == HdAovTokens->instanceId)
            {
                VtValue zero((int)0);
                desc = HdAovDescriptor(HdFormatInt32, false, zero);
                return true;
            }
            return false;
        }

    bool hasAOV(const UT_StringRef &name) const
        { return (myAOVs.find(name) !=  myAOVs.end()); }

    void setFrame(fpreal frame)
        { myFrame = frame; }

    void setRes(int w, int h)
        { myW = w; myH = h; }

    void setAOVs(const TfTokenVector &aov_names,
                 const HdAovDescriptorList &aov_desc)
        {
            myAOVs.clear();
            for(int i=0; i<aov_names.size(); i++)
                myAOVs[ aov_names[i].GetText() ] = aov_desc[i];
        }

    GfVec2i overrideResolution(const GfVec2i &res) const override
        { return (myW > 0) ? GfVec2i(myW, myH) : res; }
    
    bool    allowCameraless() const override
        { return true; }
    void    setCamera(const SdfPath &campath)
        { myCameraPath = campath; }

private:
    UT_StringMap<HdAovDescriptor> myAOVs;
    SdfPath myCameraPath;
    fpreal myFrame = 1.0;
    int myW = 0;
    int myH = 0;
};

struct HUSD_Imaging::husd_ImagingPrivate
{
public:
    UT_UniquePtr<XUSD_ImagingEngine>	 myImagingEngine;
    UT_TaskGroup			 myUpdateTask;
    XUSD_ImagingRenderParams		 myRenderParams;
    XUSD_ImagingRenderParams		 myLastRenderParams;
    std::map<TfToken, VtValue>           myCurrentRenderSettings;
    std::map<TfToken, VtValue>           myCurrentCameraSettings;
    std::string				 myRootLayerIdentifier;
    HdRenderSettingsMap                  myPrimRenderSettingMap;
    HdRenderSettingsMap                  myOldPrimRenderSettingMap;
};


HUSD_Imaging::HUSD_Imaging()
    : myPrivate(UTmakeUnique<husd_ImagingPrivate>()),
      myDepthStyle(HUSD_DEPTH_OPENGL),
      myLastCompositedBufferSet(BUFFER_NONE),
      myIsPaused(false),
      myAllowUpdates(true)
{
    myPrivate->myRenderParams.myShowProxy = true;
    myPrivate->myRenderParams.myShowGuides = true;
    myPrivate->myRenderParams.myShowRender = true;
    myPrivate->myRenderParams.myHighlight = true;
    myPrivate->myLastRenderParams = myPrivate->myRenderParams;

    myWantsHeadlight = false;
    myHasHeadlight = false;
    myWantsDomelight = false;
    myHasDomelight = false;
    myDoLighting = true;
    myDoMaterials = true;
    myConverged = true;
    mySettingsChanged = true;
    myValidRenderSettingsPrim = false;
    myCameraSynced = true;
    myConformPolicy = HUSD_Scene::EXPAND_APERTURE;
    myFrame = -1e30;
    myScene = nullptr;
    myCompositor = nullptr;
    myOutputPlane = HdAovTokens->color.GetText();
    myRenderSettings = UTmakeUnique<XUSD_RenderSettings>(
            UT_StringHolder::theEmptyString,
            UT_StringHolder::theEmptyString,
            0);
    myRenderSettingsContext = UTmakeUnique<husd_DefaultRenderSettingContext>();
    myHeadlightIntensity = 114450 * 0.5;
}

HUSD_Imaging::~HUSD_Imaging()
{
    UT_Lock::Scope	lock(theActiveRenderLock);
    theActiveRenders.erase(this);

    if (isUpdateRunning() && UT_Exit::isExiting())
    {
	// We're currently running an update.  If we delete our private data,
	// this will cause the delegate to be deleted, causing all sorts of
	// problems while we Sync().  So, in this case, since we're exiting, we
	// can just let the unique pointer float (and not be cleaned up here)
        myPrivate.release();
    }
    else
    {
        // Make sure to clear the imaging engine since we're doing reference
        // counting for clearing the texture cache.
        resetImagingEngine();
    }
}

void
HUSD_Imaging::resetImagingEngine()
{
    bool        clear_cache = false;
    myIsPaused = false;
    if (myPrivate->myImagingEngine && renderUsesTextureCache(myRendererName))
    {
        int     now = theTextureCacheRenders.add(-1);
        UT_ASSERT(now >= 0);
        clear_cache = (now == 0);
    }
    myPrivate->myImagingEngine.reset();
    // After a restart, we need to re-create the fake domelight and headlight
    // if they are needed, because they are owned by the imaging engine.
    myHasHeadlight = false;
    myHasDomelight = false;
    if (clear_cache)
    {
        // Clear out of date textures from cache
        TIL_TextureCache::clearCache(1);
        // Equivalent to "geocache -n" but avoids locking on the global eval
        // lock as would be required to use CMD_Manager::execute.
        GVEX_GeoCache::clearCache(1);
    }
}

bool
HUSD_Imaging::isUpdateRunning() const
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());

    return (status != RUNNING_UPDATE_NOT_STARTED);
}

bool
HUSD_Imaging::isUpdateComplete() const
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());

    return (status != RUNNING_UPDATE_IN_BACKGROUND);
}

void
HUSD_Imaging::getRendererCommands(UT_StringArray &command_names,
        UT_StringArray &command_descriptions) const
{
    if (myPrivate && myPrivate->myImagingEngine)
        myPrivate->myImagingEngine->GetRendererCommands(
            command_names, command_descriptions);
}

void
HUSD_Imaging::invokeRendererCommand(const UT_StringHolder &command_name) const
{
    if (myPrivate && myPrivate->myImagingEngine)
    {
        myPrivate->myImagingEngine->InvokeRendererCommand(command_name);
    }
}

void
HUSD_Imaging::terminateRender(bool hard_halt)
{
    waitForUpdateToComplete();
    mySettingsChanged = true;
    if(hard_halt)
    {
        resetImagingEngine();
    }
    else if(myPrivate && myPrivate->myImagingEngine)
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        myPrivate->myImagingEngine->DispatchRender(
            stage->GetPseudoRoot(), myPrivate->myRenderParams);
    }
}

void
HUSD_Imaging::setDrawMode(DrawMode mode)
{
    XUSD_ImagingRenderParams::XUSD_ImagingDrawMode usdmode;

    switch(mode)
    {
    case DRAW_WIRE:
	usdmode = XUSD_ImagingRenderParams::DRAW_WIREFRAME;
	break;
    case DRAW_SHADED_NO_LIGHTING:
	usdmode = XUSD_ImagingRenderParams::DRAW_GEOM_ONLY;
	break;
    case DRAW_SHADED_FLAT:
	usdmode = XUSD_ImagingRenderParams::DRAW_SHADED_FLAT;
	break;
    case DRAW_SHADED_SMOOTH:
	usdmode = XUSD_ImagingRenderParams::DRAW_SHADED_SMOOTH;
	break;
    case DRAW_WIRE_SHADED_SMOOTH:
	usdmode = XUSD_ImagingRenderParams::DRAW_WIREFRAME_ON_SURFACE;
	break;
    default:
	UT_ASSERT(!"Unhandled draw mode");
	usdmode = XUSD_ImagingRenderParams::DRAW_SHADED_SMOOTH;
	break;
    }
    myPrivate->myRenderParams.myDrawMode = usdmode;
}

void
HUSD_Imaging::showPurposeRender(bool enable)
{
    myPrivate->myRenderParams.myShowRender = enable;
}

void
HUSD_Imaging::showPurposeProxy(bool enable)
{
    myPrivate->myRenderParams.myShowProxy = enable;
}

void
HUSD_Imaging::showPurposeGuide(bool enable)
{
    myPrivate->myRenderParams.myShowGuides = enable;
}

void
HUSD_Imaging::setDrawComplexity(float complexity)
{
    myPrivate->myRenderParams.myComplexity = complexity;
}

void
HUSD_Imaging::setBackfaceCull(bool bf)
{
    auto style = bf ? XUSD_ImagingRenderParams::CULL_STYLE_BACK
		    : XUSD_ImagingRenderParams::CULL_STYLE_NOTHING;
    myPrivate->myRenderParams.myCullStyle = style;
}

void
HUSD_Imaging::setScene(HUSD_Scene *scene)
{
    myScene = scene;
}

void
HUSD_Imaging::setStage(const HUSD_DataHandle &data_handle,
        const HUSD_ConstOverridesPtr &overrides,
        const HUSD_ConstPostLayersPtr &postlayers)
{
    myDataHandle = data_handle;
    myOverrides = overrides;
    myPostLayers = postlayers;
}

bool
HUSD_Imaging::setFrame(fpreal frame)
{
    if (frame != myFrame)
    {
	myFrame = frame;
        myRenderSettingsContext->setFrame(myFrame);
	myPrivate->myRenderParams.myFrame = frame;
	mySettingsChanged = true;

	return true;
    }

    return false;
}

void
HUSD_Imaging::setAspectPolicy(HUSD_Scene::ConformPolicy p)
{
    if(p == HUSD_Scene::EXPAND_APERTURE)
        myConformPolicy = CameraUtilFit;
    else if(p == HUSD_Scene::CROP_APERTURE)
        myConformPolicy = CameraUtilCrop;
    else if(p == HUSD_Scene::ADJUST_HORIZONTAL_APERTURE)
        myConformPolicy = CameraUtilMatchHorizontally;
    else if(p == HUSD_Scene::ADJUST_VERTICAL_APERTURE)
        myConformPolicy = CameraUtilMatchVertically;
    else if(p == HUSD_Scene::ADJUST_PIXEL_ASPECT)
        myConformPolicy = CameraUtilDontConform;
}

bool
HUSD_Imaging::setDefaultLights(bool doheadlight, bool dodomelight)
{
    bool     changed = false;

    if (doheadlight != myWantsHeadlight)
    {
	mySettingsChanged = true;
	myWantsHeadlight = doheadlight;
	changed = true;
    }

    if (dodomelight != myWantsDomelight)
    {
        mySettingsChanged = true;
        myWantsDomelight = dodomelight;
        changed = true;
    }

    return changed;
}

void
HUSD_Imaging::setHeadlightIntensity(fpreal intensity)
{
    const fpreal conversion = 9.34941e4;
    intensity *= conversion;
    if(myHeadlightIntensity != intensity)
    {
        myHeadlightIntensity = intensity;
        mySettingsChanged = true;
    }
}

void
HUSD_Imaging::setLighting(bool do_lighting)
{
    if (myDoLighting != do_lighting)
	mySettingsChanged = true;
    myDoLighting = do_lighting;
}

void
HUSD_Imaging::setMaterials(bool do_materials)
{
    if (myDoMaterials != do_materials)
	mySettingsChanged = true;
    myDoMaterials = do_materials;
}

const HUSD_DataHandle &
HUSD_Imaging::viewerLopDataHandle() const
{
    return myDataHandle;
}

// Start of anonymous namespace
namespace
{
    static void
    warnAboutBadDelegate(int signal)
    {
        fprintf(stderr, "WARNING: Crashing creating delegate, this might happen\n");
        fprintf(stderr, "\tif the TfType template name doesn't match the string\n");
        fprintf(stderr, "\tin the .json file\n");
    }

    static bool
    isSupported(const TfToken &id)
    {
        UT_Signal               trap(SIGSEGV, warnAboutBadDelegate, true);
	auto			&reg = HdRendererPluginRegistry::GetInstance();
	HdRendererPlugin	*plugin = reg.GetRendererPlugin(id);
	bool			 supported = false;

	if (plugin)
	{
	    supported = plugin->IsSupported();
	    reg.ReleasePlugin(plugin);
	}
        if (!supported && UT_EnvControl::getInt(ENV_HOUDINI_DSO_ERROR))
        {
            static UT_Set<TfToken>      map;
            if (!map.contains(id))
            {
                map.insert(id);
                UTformat(stderr,
                        "Unable to create Usd Render Plugin: {}\n", id);
            }
        }

	return supported;
    }

    static UT_StringHolder
    getDefaultRendererName()
    {
	auto &&reg = HdRendererPluginRegistry::GetInstance();
	return UT_StringHolder(reg.GetDefaultPluginId().GetText());
    }
}
// End of anonymous namespace

bool
HUSD_Imaging::setupRenderer(const UT_StringRef &renderer_name,
                            const UT_Options *render_opts,
                            bool cam_effects)
{
    UT_StringHolder	 new_renderer_name = renderer_name;

    // At this point we are ready to create our new imaging engine if we
    // need one. But first, make sure that we are allowed to render...
    if (!myAllowUpdates)
    {
        if (!myPrivate->myImagingEngine)
        {
            myConverged = true;
            backgroundRenderState(myConverged, this);
        }
        return true;
    }

    if(render_opts)
    {
	if(*render_opts != myCurrentDisplayOptions)
	{
            myCurrentDisplayOptions = *render_opts;
	    mySettingsChanged = true;
	}
    }
    else if(myCurrentDisplayOptions.getNumOptions() > 0)
    {
        myCurrentDisplayOptions.clear();
	mySettingsChanged = true;
    }

    if(myScene)
	HUSD_Scene::pushScene(myScene);

    if (myRendererName != new_renderer_name)
    {
	if (!isSupported(TfToken(new_renderer_name.c_str())))
	{
            // We can never use this renderer because it isn't supported.
            // Remove it from our map of choices, and return false to reject
            // the requested change of renderer.
            if (UT_EnvControl::getInt(ENV_HOUDINI_DSO_ERROR))
            {
                static UT_Set<UT_StringHolder>  badGuys;
                if (!badGuys.contains(new_renderer_name))
                {
                    UTformat("{} not supported - removing from renderer list\n",
                        new_renderer_name);
                    badGuys.insert(new_renderer_name);
                }
            }
	    theRendererInfoMap.erase(new_renderer_name);
            resetImagingEngine();
            myRendererName.clear();
            if(myScene)
                HUSD_Scene::popScene(myScene);

            return false;
	}

        // Reset the engine before changing the renderer name so that we
        // do the proper cleanup for the _old_ renderer, not the cleanup that
        // would be appropriate for the _new_ renderer.
        resetImagingEngine();
        myRendererName = new_renderer_name;
    }

    const HUSD_DataHandle &maindata = viewerLopDataHandle();

    if (maindata.rootLayerIdentifier() != myPrivate->myRootLayerIdentifier)
    {
        resetImagingEngine();
	myPrivate->myRootLayerIdentifier = maindata.rootLayerIdentifier();
    }

    // Check for restart settings changes even if the imaging engine is
    // already null, because this method also initializes the camera settings
    // map with the current values.
    if (updateRestartCameraSettings(cam_effects) ||
        (myPrivate->myImagingEngine && anyRestartRenderSettingsChanged()))
    {
        resetImagingEngine();
    }

    HUSD_LightingMode lighting_mode = render_opts
        ? (HUSD_LightingMode)render_opts->getOptionI("lighting_mode")
        : HUSD_LIGHTING_MODE_NORMAL;
    bool do_lighting = (lighting_mode != HUSD_LIGHTING_MODE_NO_LIGHTING);
    auto &&draw_mode = myPrivate->myRenderParams.myDrawMode;
    if (draw_mode == XUSD_ImagingRenderParams::DRAW_SHADED_FLAT ||
        draw_mode == XUSD_ImagingRenderParams::DRAW_SHADED_SMOOTH ||
        draw_mode == XUSD_ImagingRenderParams::DRAW_WIREFRAME_ON_SURFACE)
	do_lighting = myDoLighting;

    myPrivate->myRenderParams.myEnableLighting = do_lighting;
    myPrivate->myRenderParams.myEnableSceneLights = do_lighting &&
        (lighting_mode != HUSD_LIGHTING_MODE_HEADLIGHT_ONLY &&
         lighting_mode != HUSD_LIGHTING_MODE_DOMELIGHT_ONLY);
    myPrivate->myRenderParams.myEnableSceneMaterials = myDoMaterials;
    
    // Setting this value to true causes the "automatic" Alpha Threshold
    // setting to be set to 0.1 instead of 0.5 (which is the value used if
    // this flag is left at its default value of false).
    myPrivate->myRenderParams.myEnableSampleAlphaToCoverage = true;

    // Create myImagingEngine inside a render call. Otherwise
    // we can't initialize OpenGL, so USD won't detect it is
    // running in a GL4 context, so it will use the terrible
    // reference renderer.
    if (!myPrivate->myImagingEngine)
    {
        static const char *theEnableSceneIndexEnvVar =
            "USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX";
        bool drawmode = theRendererInfoMap[myRendererName].drawModeSupport();

	myPrivate->myImagingEngine =
            XUSD_ImagingEngine::createImagingEngine(false,
                (HoudiniGetenv(theEnableSceneIndexEnvVar) &&
                 SYSatoi(HoudiniGetenv(theEnableSceneIndexEnvVar)) != 0));
        if (!myPrivate->myImagingEngine)
        {
            if(myScene)
                HUSD_Scene::popScene(myScene);
            return false;
        }

        if (renderUsesTextureCache(myRendererName))
        {
            UT_VERIFY(theTextureCacheRenders.add(1) > 0);
        }

	if (!myPrivate->myImagingEngine->SetRendererPlugin(
               TfToken(myRendererName.toStdString())))
        {
            if(myScene)
                HUSD_Scene::popScene(myScene);
            // We couldn't change to this renderer right now. This can
            // happen in the case where a render delegate only supports a
            // single instance of the renderer and we are asking for a
            // second instance. The renderer is supported, and this
            // request may work next time, but this time it fails.
            resetImagingEngine();
            myRendererName.clear();
            return false;
        }

        // Update the render delegate's render settings before setting up
        // the AOVs. Because we just created a new render delegate, we need
        // to send all render settings again, so make sure all our caches and
        // are cleared and the "changed" flag is set.
        mySettingsChanged = true;
        myPrivate->myCurrentRenderSettings.clear();
        myPrivate->myImagingEngine->SetUsdDrawModesEnabled(drawmode);
        myPrivate->myRenderParams.myEnableUsdDrawModes = drawmode;
        myPrivate->myImagingEngine->SetDisplayUnloadedPrimsWithBounds(drawmode);

        HUSD_AutoReadLock    lock(maindata, myOverrides, myPostLayers);
        updateSettingsIfRequired(lock);
    }

    myPlaneList.clear();
    bool has_aov = false;

    TfTokenVector list;
    bool aovs_specified = false;
    
    if(myValidRenderSettingsPrim)
    {
        // Got AOVs from a render settings prim.
        bool has_depth = false;
        bool has_primid = false;
        bool has_instid = false;
        HdAovDescriptorList descs;
        myRenderSettings->collectAovs(list, descs);

        if(list.size())
        {
            for(auto &t : list)
            {
                if(t == HdAovTokens->depth)
                    has_depth = true;
                else if(t == HdAovTokens->primId)
                    has_primid = true;
                else if(t == HdAovTokens->instanceId)
                    has_instid = true;
            }
            // Make sure depth, primId, and instanceId are in the list.
            if(!has_depth)
                list.push_back(HdAovTokens->depth);
            if(!has_primid)
                list.push_back(HdAovTokens->primId);
            if(!has_instid)
                list.push_back(HdAovTokens->instanceId);

            aovs_specified = true;
        }
    }
    if(!aovs_specified)
    {
        // Use a default set of AOVs.
        list.push_back(HdAovTokens->color);
        list.push_back(HdAovTokens->depth);
        list.push_back(HdAovTokens->normal);
        list.push_back(HdAovTokensMakePrimvar(TfToken("st")));
        list.push_back(HdAovTokens->primId);
        list.push_back(HdAovTokens->instanceId);
    }

    // Figure out which AOVs the renderer actually supports.
    auto aov_list = myPrivate->myImagingEngine->GetRendererAovs(list);
    for(auto &t : aov_list)
    {
        myPlaneList.append(t.GetText());
        if(myOutputPlane.isstring() && myOutputPlane == myPlaneList.last())
        {
            has_aov = true;
            myCurrentAOV = myOutputPlane;
        }
    }

    if(has_aov)
    {
        TfToken outputplane_token(myOutputPlane.toStdString());

        if(std::find(list.begin(), list.end(), outputplane_token) == list.end())
            list.push_back(outputplane_token);
    }
    else
        myCurrentAOV = list[0].GetText();

    if(myPrivate->myImagingEngine->SetRendererAovs( list ) &&
        myValidRenderSettingsPrim)
    {
        for(auto &aov_name  : list)
        {
            HdAovDescriptor aov_desc;
            if(myRenderSettingsContext->getAovDescriptor(aov_name, aov_desc))
                myPrivate->myImagingEngine->
                    SetRenderOutputSettings(aov_name, aov_desc);
        }
    }

    if(myScene)
	HUSD_Scene::popScene(myScene);

    return true;
}

bool
HUSD_Imaging::setOutputPlane(const UT_StringRef &name)
{
    myOutputPlane = name;
    
    if (myValidRenderSettingsPrim &&
        myRenderSettingsContext->hasAOV(name))
    {
        myCurrentAOV = name;
        return true;
    }

    return false;
}

static const UT_StringHolder theStageMetersPerUnit("stageMetersPerUnit");
static const UT_StringHolder theHoudiniViewportToken("houdini:viewport");
static const UT_StringHolder theHoudiniFrameToken("houdini:frame");
static const UT_StringHolder theHoudiniFPSToken("houdini:fps");
static const UT_StringHolder theRenderCameraPathToken("renderCameraPath");
static const UT_StringSet    theAlwaysAvailableSettings({
    theStageMetersPerUnit,
    theHoudiniViewportToken,
    theHoudiniFrameToken,
    theHoudiniFPSToken,
    theRenderCameraPathToken
});
static const UT_StringHolder theUseRenderSettingsPrim("houdini:use_render_settings_prim");

static bool
isRestartSetting(const UT_StringRef &key,
        const UT_StringArray &restartsettings)
{
    for (auto &&setting : restartsettings)
        if (key.multiMatch(setting.c_str()))
            return true;

    return false;
}

static bool
isRestartSettingChanged(const UT_StringRef &key,
        const VtValue &vtvalue,
        const UT_StringArray &restartsettings,
        const std::map<TfToken, VtValue> currentsettings)
{
    TfToken       tfkey(key.toStdString());
    auto        &&it = currentsettings.find(tfkey);

    if (it == currentsettings.end() || it->second != vtvalue)
        return isRestartSetting(key, restartsettings);

    return false;
}

bool
HUSD_Imaging::updateRestartCameraSettings(bool cam_effects) const
{
    if (!theRendererInfoMap.contains(myRendererName))
        return false;

    const UT_StringArray &restart_camera_settings =
        theRendererInfoMap[myRendererName].restartCameraSettings();
    bool restart_required = false;

    if (!restart_camera_settings.isEmpty())
    {
        HUSD_AutoReadLock lock(viewerLopDataHandle(), myOverrides, myPostLayers);
        SdfPath campath;

        if(!myCameraPath.isstring() || !myCameraSynced || !cam_effects)
            campath = HUSDgetHoudiniFreeCameraSdfPath();
        else if(myCameraPath)
            campath = SdfPath(myCameraPath.toStdString());

        if (lock.data() && lock.data()->isStageValid())
        {
            UsdPrim cam = lock.data()->stage()->GetPrimAtPath(campath);

            std::vector<UsdAttribute> attributes = cam
                ? cam.GetAttributes()
                : std::vector<UsdAttribute>();
            std::set<TfToken> missingsettings;

            for (auto it = myPrivate->myCurrentCameraSettings.begin();
                      it != myPrivate->myCurrentCameraSettings.end(); ++it)
                missingsettings.insert(it->first);

            for (auto &&attr : attributes)
            {
                const TfToken &attrname = attr.GetName();
                VtValue value;

                attr.Get(&value, UsdTimeCode::EarliestTime());
                if (!value.IsEmpty())
                {
                    missingsettings.erase(attrname);
                    if (isRestartSettingChanged(attrname.GetText(),
                            value, restart_camera_settings,
                            myPrivate->myCurrentCameraSettings))
                    {
                        myPrivate->myCurrentCameraSettings[attrname] = value;
                        restart_required = true;
                    }
                }
            }

            for (auto &&missingsetting : missingsettings)
            {
                myPrivate->myCurrentCameraSettings.erase(missingsetting);
                restart_required = true;
            }
        }
    }

    return restart_required;
}

bool
HUSD_Imaging::anyRestartRenderSettingsChanged() const
{
    if (!theRendererInfoMap.contains(myRendererName))
        return false;

    if (myPrivate->myRenderParams != myPrivate->myLastRenderParams ||
        mySettingsChanged)
    {
        const UT_StringArray &restart_render_settings =
            theRendererInfoMap[myRendererName].restartRenderSettings();
        SdfPath campath;

        if(!myCameraPath.isstring() || !myCameraSynced)
            campath = HUSDgetHoudiniFreeCameraSdfPath();
        else if(myCameraPath)
            campath = SdfPath(myCameraPath.toStdString());

        if (isRestartSettingChanged(theHoudiniFrameToken,
                VtValue(myFrame), restart_render_settings,
                myPrivate->myCurrentRenderSettings) ||
            isRestartSettingChanged("renderCameraPath",
                VtValue(campath), restart_render_settings,
                myPrivate->myCurrentRenderSettings))
            return true;

        for(auto opt : myPrivate->myOldPrimRenderSettingMap)
        {
            const UT_StringRef optnamestr(opt.first.GetText());
            auto it = myPrivate->myPrimRenderSettingMap.find(opt.first);

            // If the setting the has been removed is one of the special
            // "always on" settings added above, or if we will immediately
            // be setting the value from myCurrentDisplayOptions in the
            // next loop, don't bother clearing the setting here.
            if (it == myPrivate->myPrimRenderSettingMap.end() &&
                !theAlwaysAvailableSettings.contains(optnamestr) &&
                !myCurrentDisplayOptions.getOptionEntry(optnamestr) &&
                isRestartSetting(optnamestr, restart_render_settings))
                return true;
        }

        for(auto opt = myCurrentDisplayOptions.begin();
            opt != myCurrentDisplayOptions.end(); ++opt)
        {
            if(myValidRenderSettingsPrim)
            {
                // Render setting prims override display options. Skip
                // any display options in case a render setting exists
                // for that option.
                TfToken name(opt.name());
                auto it = myPrivate->myPrimRenderSettingMap.find(name);
                if(it != myPrivate->myPrimRenderSettingMap.end())
                    continue;
            }

            VtValue value(HUSDoptionToVtValue(opt.entry()));
            if (!value.IsEmpty() &&
                isRestartSettingChanged(opt.name(),
                    value, restart_render_settings,
                    myPrivate->myCurrentRenderSettings))
                return true;
        }

        if(myValidRenderSettingsPrim)
        {
            for(auto opt : myPrivate->myPrimRenderSettingMap)
            {
                const auto &key = opt.first;
                auto &&it = myPrivate->myCurrentRenderSettings.find(key);

                if ((it == myPrivate->myCurrentRenderSettings.end() ||
                     it->second != opt.second) &&
                    isRestartSetting(key.GetText(), restart_render_settings))
                    return true;
            }
        }
    }

    return false;
}

void
HUSD_Imaging::updateSettingIfRequired(const UT_StringRef &key,
        const VtValue &vtvalue,
        bool from_usd_prim)
{
    TfToken       tfkey(key.toStdString());
    auto        &&it = myPrivate->myCurrentRenderSettings.find(tfkey);

    if (it == myPrivate->myCurrentRenderSettings.end() || it->second != vtvalue)
    {
        myPrivate->myImagingEngine->SetRendererSetting(tfkey, vtvalue);
        myPrivate->myCurrentRenderSettings[tfkey] = vtvalue;
        UT_ErrorLog::format(4, "Render setting from {}: {} = {}",
            from_usd_prim ? "USD" : "Houdini", tfkey, vtvalue);
    }
}

static fpreal
getFPS()
{
    return OPgetDirector()->getChannelManager()->getSamplesPerSec();
}

void
HUSD_Imaging::updateSettingsIfRequired(HUSD_AutoReadLock &lock)
{
    // Pass the stage metrics (meter per units). We do this outside the if
    // block because we don't have any way to detect this change other than
    // fetching the value to see if it changed since our last time here.
    double metersperunit = HUSD_Preferences::defaultMetersPerUnit();
    if (lock.data() && lock.data()->isStageValid())
        metersperunit = UsdGeomGetStageMetersPerUnit(lock.data()->stage());
    updateSettingIfRequired(theStageMetersPerUnit, VtValue(metersperunit));

    // Render setting prims override display options. Pass down the flag
    // to the render delegate too. This enables the delegate to decouple/run
    // different sets of eg. image filters:
    // "karma:global:imagefilter" and "karma:hydra:denoise"
    updateSettingIfRequired(
        theUseRenderSettingsPrim, VtValue(myValidRenderSettingsPrim));

    if (myPrivate->myRenderParams != myPrivate->myLastRenderParams ||
        mySettingsChanged)
    {
        myPrivate->myLastRenderParams = myPrivate->myRenderParams;
        mySettingsChanged = false;

        updateSettingIfRequired(theHoudiniViewportToken, VtValue(true));
        updateSettingIfRequired(theHoudiniFrameToken, VtValue(myFrame));
        updateSettingIfRequired(theHoudiniFPSToken, VtValue(getFPS()));

        SdfPath campath;
        if(!myCameraPath.isstring() || !myCameraSynced)
            campath = HUSDgetHoudiniFreeCameraSdfPath();
        else if(myCameraPath)
            campath = SdfPath(myCameraPath.toStdString());

        updateSettingIfRequired(theRenderCameraPathToken, VtValue(campath));

        for(auto opt : myPrivate->myOldPrimRenderSettingMap)
        {
            const UT_StringRef optnamestr(opt.first.GetText());
            auto it = myPrivate->myPrimRenderSettingMap.find(opt.first);

            // If the setting the has been removed is one of the special
            // "always on" settings added above, or if we will immediately
            // be setting the value from myCurrentDisplayOptions in the
            // next loop, don't bother clearing the setting here.
            if (it == myPrivate->myPrimRenderSettingMap.end() &&
                !theAlwaysAvailableSettings.contains(optnamestr) &&
                !myCurrentDisplayOptions.getOptionEntry(optnamestr))
            {
                myPrivate->myImagingEngine->
                    SetRendererSetting(opt.first, VtValue());
                myPrivate->myCurrentRenderSettings.erase(opt.first);
                UT_ErrorLog::format(4, "Render setting from USD removed: {}",
                    opt.first);
            }
        }

        for(auto opt = myCurrentDisplayOptions.begin();
            opt != myCurrentDisplayOptions.end(); ++opt)
        {
            if(myValidRenderSettingsPrim)
            {
                // Render setting prims override display options. Skip any
                // display options in case a render setting exists for that
                // option.
                TfToken name(opt.name());
                auto it = myPrivate->myPrimRenderSettingMap.find(name);
                if(it != myPrivate->myPrimRenderSettingMap.end())
                    continue;
            }

            VtValue value(HUSDoptionToVtValue(opt.entry()));
            if (!value.IsEmpty())
                updateSettingIfRequired(opt.name(), value);
        }

        if(myValidRenderSettingsPrim)
        {
            for(auto opt : myPrivate->myPrimRenderSettingMap)
                updateSettingIfRequired(opt.first.GetText(), opt.second, true);
        }
    }
}

void
HUSD_Imaging::setRenderFocus(int x, int y) const
{
    if(myPrivate->myImagingEngine)
    {
        auto &&token = HusdHdRenderStatsTokens->viewerMouseClick;

        GfVec2i pos(x,y);
        myPrivate->myImagingEngine->SetRendererSetting(token, VtValue(pos));
    }
}

void
HUSD_Imaging::clearRenderFocus() const
{
    if(myPrivate->myImagingEngine)
    {
        auto &&token = HusdHdRenderStatsTokens->viewerMouseClick;
        GfRect2i null_area(GfVec2i(0,0),0,0);
        myPrivate->myImagingEngine->SetRendererSetting(token,VtValue(null_area));
    }
}



HUSD_Imaging::RunningStatus
HUSD_Imaging::updateRenderData(const UT_Matrix4D &view_matrix,
                               const UT_Matrix4D &proj_matrix,
                               const UT_DimRect &viewport_rect,
                               bool cam_effects)
{
    // If we have been told not to render, our engine may be null, but we
    // still want to report the requested update as being complete.
    if (!myAllowUpdates)
        return RUNNING_UPDATE_COMPLETE;

    auto &&engine = myPrivate->myImagingEngine;
    bool success = true;

    myRenderKeyToPathMap.clear();
    myReadLock = UTmakeUnique<HUSD_AutoReadLock>(
        myDataHandle, myOverrides, myPostLayers);
    HUSD_AutoReadLock *lock = myReadLock.get();
    if (lock->data() && lock->data()->isStageValid())
    {
        UT_Vector4D ut_viewport;

        ut_viewport.assign(viewport_rect.x(),
                           viewport_rect.y(),
                           viewport_rect.w(),
                           viewport_rect.h());

        // UTdebugPrint("\n\n\n\n********************\nSet Window",
        //              viewport_rect);
        // UTdebugPrint("View", view_matrix);
        // UTdebugPrint("Proj", proj_matrix);
        GfMatrix4d gf_view_matrix = GusdUT_Gf::Cast(view_matrix);
        GfMatrix4d gf_proj_matrix = GusdUT_Gf::Cast(proj_matrix);
        GfVec4d gf_viewport = GusdUT_Gf::Cast(ut_viewport);

        engine->SetRenderViewport(gf_viewport);

        SdfPath campath;
        if(myCameraPath && myCameraSynced && cam_effects)
            campath = SdfPath(myCameraPath.toStdString());
        else
            campath = HUSDgetHoudiniFreeCameraSdfPath();

        // For "headlights" to work for all render delegates, we need
        // to tell the engine the view transforms even if we are going
        // to be looking through a real camera. But we do this before
        // setting the "look through" camera or else the view matrices
        // override the "look through" camera and settings like DOF
        // stop working.
        engine->SetCameraState(gf_view_matrix, gf_proj_matrix);
        if(!campath.IsEmpty())
        {
            engine->SetCameraPath(campath);
            engine->SetWindowPolicy(
                (CameraUtilConformWindowPolicy)myConformPolicy);
        }
        myRenderSettingsContext->setCamera(campath);

        UT_Array<XUSD_GLSimpleLight> lights;
        GfVec4f ambient(0.0, 0.0, 0.0, 0.0);

        if (myPrivate->myRenderParams.myEnableLighting)
        {
            if (myHasHeadlight != myWantsHeadlight ||
                myHasDomelight != myWantsDomelight)
            {
                // With any change, we first want to clear all the
                // existing "simple" lights, because there seems to
                // be update issues.
                engine->SetLightingState(lights, ambient);
                if(myWantsHeadlight)
                {
                    XUSD_GLSimpleLight	 light;

                    light.myIsDomeLight = false;
#if MATCH_HYDRA_DEFAULT
                    light.myIntensity = 15000.0;
#else
                    light.myIntensity = myHeadlightIntensity;
#endif
                    light.myAngle = 0.53;
                    light.myColor = UT_Vector3(1, 1, 1);
                    lights.append(light);
                }
                if(myWantsDomelight)
                {
                    XUSD_GLSimpleLight	 light;

                    light.myIsDomeLight = true;
                    light.myIntensity = 1.0;
                    light.myAngle = 0.53;
                    light.myColor = UT_Vector3(1.0, 1.0, 1.0);
                    lights.append(light);
                }
                myHasHeadlight = myWantsHeadlight;
                myHasDomelight = myWantsDomelight;
                engine->SetLightingState(lights, ambient);
            }
        }
        else if (myHasHeadlight || myHasDomelight)
        {
            myHasHeadlight = false;
            myHasDomelight = false;
            engine->SetLightingState(lights, ambient);
        }

        updateSettingsIfRequired(*lock);

        try
        {
            engine->DispatchRender(
                lock->data()->stage()->GetPseudoRoot(),
                myPrivate->myRenderParams);
        }
        catch (std::exception &err)
        {
            UT_ErrorLog::error("Render delegate exception: {}", err.what());
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                    "Render delegate threw exception during update");
            success = false;
        }
    }
    else
        success = false;

    // Other renderers need to return to executing on
    // the main thread now. This is where the actual
    // GL calls happen.
    if (success)
        return RUNNING_UPDATE_COMPLETE;
    else
        return RUNNING_UPDATE_FATAL;
}

HUSD_Imaging::BufferSet
HUSD_Imaging::hasAOVBuffers() const
{
    if(myPrivate && myPrivate->myImagingEngine && myCompositor)
    {
        TfToken aov(myCurrentAOV.c_str());
	HdRenderBuffer  *color_buf = myPrivate->myImagingEngine->
	    GetRenderOutput(aov);
	HdRenderBuffer  *depth_buf = myPrivate->myImagingEngine->
	    GetRenderOutput(HdAovTokens->depth);

        if(color_buf && depth_buf)
            return BUFFER_COLOR_DEPTH;
        else if(color_buf)
            return BUFFER_COLOR;
        else
            return BUFFER_NONE;
    }

    return myLastCompositedBufferSet;
}

void
HUSD_Imaging::setPostRenderCallback(const PostRenderCallback &cb)
{
    myPostRenderCallback = cb;
}

bool
HUSD_Imaging::getUsingCoreProfile()
{
    if (myPrivate->myImagingEngine)
        return myPrivate->myImagingEngine->isUsingGLCoreProfile();

    return false;
}

void
HUSD_Imaging::finishRender(bool do_render)
{
    // myImagingEngine may be null here if we are running updates on the
    // foreground thread, and we have updated to an empty data handle or
    // an empty stage.
    if (!myPrivate->myImagingEngine)
	return;

    if (do_render)
    {
        bool viewportrenderer =
            theRendererInfoMap[myRendererName].viewportRenderer();

        myPrivate->myImagingEngine->CompleteRender(
            myPrivate->myRenderParams, viewportrenderer);
	if (myPostRenderCallback)
	    myPostRenderCallback(this);
    }

    auto converged = myPrivate->myImagingEngine->IsConverged();
    if (converged != myConverged)
    {
	myConverged = converged;
	backgroundRenderState(converged, this);
    }
}

namespace
{
    static UT_StringHolder
    valueToString(const VtValue &val)
    {
        if (val.IsHolding<TfToken>())
            return UT_StringHolder(val.UncheckedGet<TfToken>().GetText());
        if (val.IsHolding<std::string>())
            return UT_StringHolder(val.UncheckedGet<std::string>());
        return UT_StringHolder();
    }

    static void
    ocioTransform(const PXL_OCIO::PHandle &proc,
            float *dst,
            const void *src,
            PXL_DataFormat df,
            exint npixels,
            int nchan)
    {
        if (df == PXL_FLOAT32)
        {
            memcpy(dst, src, sizeof(float)*npixels*nchan);
        }
        else
        {
            // Convert source data to float
            PXL_FillParms           fill;
            fill.setSourceType(df);
            fill.setDestType(PXL_FLOAT32);
            fill.mySource = src;
            fill.myDest = dst;
            fill.mySInc = 1;
            fill.myDInc = 1;
            fill.setSourceArea(0, 0, npixels*nchan - 1, 0);
            fill.setDestArea(0, 0, npixels*nchan - 1, 0);
            PXL_Fill::fill(fill);
        }
        PXL_OCIO::transform(proc, dst, npixels, nchan);
    }
}       // end namespace

void
HUSD_Imaging::updateComposite(bool free_if_missing)
{
    bool     missing = true;
    PXL_OCIO::PHandle   cxform;

    if(myCompositor && myPrivate && myPrivate->myImagingEngine)
    {
        if (myRenderSettings)
        {
            const HdRenderSettingsMap   &map = myRenderSettings->renderSettings();
            auto it = map.find(HdRenderSettingsPrimTokens->renderingColorSpace);
            if (it != map.end())
            {
                UT_StringHolder              name = valueToString(it->second);
                const PXL_OCIO::ColorSpace  *src = PXL_OCIO::lookupSpace(name);
                if (src)
                {
                    const PXL_OCIO::ColorSpace  *dst = PXL_OCIO::lookupSpace(
                                                PXL_OCIO::getSceneLinearRole());
                    cxform = PXL_OCIO::lookupProcessor(src, dst,
                                            UT_StringHolder());
                    if (cxform.isValid() && cxform.isNoOp())
                        cxform.clear();
                }
            }
        }
        TfToken aov(myCurrentAOV);
	HdRenderBuffer  *color_buf = myPrivate->myImagingEngine->
	    GetRenderOutput(aov);
	HdRenderBuffer  *depth_buf = myPrivate->myImagingEngine->
	    GetRenderOutput(HdAovTokens->depth);

	HdRenderBuffer  *prim_id = myPrivate->myImagingEngine->
	    GetRenderOutput(HdAovTokens->primId);
	HdRenderBuffer  *inst_id = myPrivate->myImagingEngine->
	    GetRenderOutput(HdAovTokens->instanceId);

        if(color_buf && depth_buf)
            myLastCompositedBufferSet = BUFFER_COLOR_DEPTH;
        else if(color_buf)
            myLastCompositedBufferSet = BUFFER_COLOR;
        else
            myLastCompositedBufferSet = BUFFER_NONE;

	if (color_buf && depth_buf)
	{
            HdFormat df = color_buf->GetFormat();
            exint nchan = HdGetComponentCount(df);
            exint id = 0;
            exint w = 0;
            exint h = 0;

            color_buf->Resolve();

            if (myPrivate->myImagingEngine->
                GetRawResource(color_buf, id, w, h))
            {
                myCompositor->setResolution(w, h);
                myCompositor->updateColorTexture(id);
            }
            else
            {
                void *color_map = color_buf->Map();
                w = color_buf->GetWidth();
                h = color_buf->GetHeight();

                if (w && h)
                {
                    myCompositor->setResolution(w, h);

                    if (nchan >= 3 && !cxform.isNoOp())
                    {
                        // We need to transform the color to scene linear before
                        // updating the compositor.
                        UT_StackBuffer<float> tmp(w * h * nchan);
                        ocioTransform(cxform, tmp.array(), color_map,
                            HdToPXL(df), w * h, nchan);
                        myCompositor->updateColorBuffer(
                            tmp.array(), PXL_FLOAT32, nchan);
                    }
                    else
                    {
                        myCompositor->updateColorBuffer(
                            color_map, HdToPXL(df), nchan);
                    }
                }
                color_buf->Unmap();
                color_map = nullptr;
            }
            if (w && h)
	    {
                depth_buf->Resolve();

                if (myPrivate->myImagingEngine->
                    GetRawResource(depth_buf, id, w, h))
                {
                    myCompositor->updateDepthTexture(id);
                }
                else
                {
                    auto depth_map = depth_buf->Map();
                    if(depth_buf->GetWidth() == w && depth_buf->GetHeight() == h)
                    {
                        df = depth_buf->GetFormat();
                        myCompositor->updateDepthBuffer(depth_map,
                                                       HdToPXL(df),
                                                       HdGetComponentCount(df));
                    }
                    else
                        myCompositor->updateDepthBuffer(nullptr, PXL_FLOAT32, 0);
                    depth_buf->Unmap();
                }
	    }

            
            if(w && h && prim_id)
            {
                prim_id->Resolve();

                if (myPrivate->myImagingEngine->
                    GetRawResource(prim_id, id, w, h))
                {
                    myCompositor->updatePrimIDTexture(id);
                }
                else
                {
                    auto id_map = prim_id->Map();
                    if(prim_id->GetWidth() == w && prim_id->GetHeight() == h)
                    {
                        auto df = prim_id->GetFormat();
                        myCompositor->updatePrimIDBuffer(id_map, HdToPXL(df));
                    }
                    else
                        myCompositor->updatePrimIDBuffer(nullptr, PXL_INT32);
                    prim_id->Unmap();
                }
	    }
            else
                myCompositor->updatePrimIDBuffer(nullptr, PXL_INT32);

            if(w && h && inst_id)
            {
                inst_id->Resolve();

                if (myPrivate->myImagingEngine->
                    GetRawResource(inst_id, id, w, h))
                {
                    myCompositor->updateInstIDTexture(id);
                }
                else
                {
                    auto id_map = inst_id->Map();
                    if(inst_id->GetWidth()  == w && inst_id->GetHeight() == h)
                    {
                        auto df = inst_id->GetFormat();
                        myCompositor->updateInstanceIDBuffer(id_map,HdToPXL(df));
                    }
                    else
                        myCompositor->updateInstanceIDBuffer(nullptr, PXL_INT32);
                    inst_id->Unmap();
                }
	    }
            else
                myCompositor->updateInstanceIDBuffer(nullptr, PXL_INT32);
            

            missing = false;
#if UT_ASSERT_LEVEL > 0
	    // Uncomment to save AOV buffers to disk for debugging.
	    //myCompositor->saveBuffers("colorbuf.pic", "depthbuf.pic");
#endif
	}
    }
    else if (myCompositor)
    {
        missing = (myLastCompositedBufferSet == BUFFER_NONE);
    }

    if(myCompositor && free_if_missing && missing)
    {
        myCompositor->updateColorBuffer(nullptr, PXL_FLOAT32, 0);
        myCompositor->updateDepthBuffer(nullptr, PXL_FLOAT32, 0);
    }
}

HUSD_RenderBuffer
HUSD_Imaging::getAOVBuffer(const UT_StringRef &name) const
{
    return HUSD_RenderBuffer(
        myPrivate->myImagingEngine->GetRenderOutput(TfToken(name)));
}

bool
HUSD_Imaging::canBackgroundRender(const UT_StringRef &renderer) const
{
    bool pref = HUSD_Preferences::updateRendererInBackground();
    UT_StringHolder rname = renderer.isstring() ? renderer : myRendererName;

    // myRendererName should either be something in our map, or the empty
    // string.
    initializeAvailableRenderers();
    if (!theRendererInfoMap.contains(rname))
    {
        UT_ASSERT(!rname.isstring());
        return false;
    }

    return (pref && theRendererInfoMap[rname].allowBackgroundUpdate());
}

bool
HUSD_Imaging::launchBackgroundRender(const UT_Matrix4D &view_matrix,
                                     const UT_Matrix4D &proj_matrix,
                                     const UT_DimRect &viewport_rect,
                                     const UT_StringRef &renderer,
                                     const UT_Options *render_opts,
                                     bool cam_effects)
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());
    
    // An empty renderer name means clear out our imaging data and exit.
    if (!renderer.isstring())
    {
        waitForUpdateToComplete();
        resetImagingEngine();
	return false;
    }

    if(status != RUNNING_UPDATE_NOT_STARTED)
        return false;

    // If we aren't running in the background, we are free to start a new
    // update/redraw sequence.
    if(!setupRenderer(renderer, render_opts, cam_effects))
        return false;

    // Run the update in the background. Set our running in
    // background status, and spin up the background thread.
    // TODO: Make this a reusable thread instead of creating
    //       a new one every time.
    myRunningInBackground.store(RUNNING_UPDATE_IN_BACKGROUND);

    // If we don't run in the background, handles take a long time to update in
    // the kitchen scene while transforming a large selection of geometry.
    // When we run in the background, the handles are much more interactive.
    if (UT_Thread::getNumProcessors() > 1)
    {
	myPrivate->myUpdateTask.run([this, view_matrix, proj_matrix,
                                     viewport_rect, cam_effects]()
            {
                UT_PerfMonAutoViewportDrawEvent perfevent("LOP Viewer",
                    "Background Update USD Stage", UT_PERFMON_3D_VIEWPORT);
                utTraceViewportDrawEvent("LOP Viewer", "Background Update USD Stage");
                // Make sure nobody calls Reload on any layers while we are
                // performing our update/sync from the viewport stage. This
                // is the only way in which code on the main thread might try
                // to write to/modify any layers referenced by the viewport
                // stage during this update.
                UT_AutoLock lockscope(HUSDgetLayerReloadLock());

                RunningStatus status = updateRenderData(
                    view_matrix, proj_matrix, viewport_rect, cam_effects);

                if (status == RUNNING_UPDATE_NOT_STARTED ||
                    status == RUNNING_UPDATE_FATAL)
                    myReadLock.reset();
                myRunningInBackground.store(status);
            });
    }
    else
    {
        status = updateRenderData(
            view_matrix, proj_matrix, viewport_rect, cam_effects);

	if (status == RUNNING_UPDATE_NOT_STARTED ||
	    status == RUNNING_UPDATE_FATAL)
	    myReadLock.reset();
        myRunningInBackground.store(status);
    }

    //UTdebugPrint("Finish launch");
    return true;
}

void
HUSD_Imaging::waitForUpdateToComplete()
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());
    bool redo_pause = false;

    if(myIsPaused)
    {
	// If the render is paused, it's possible that it was paused in the
	// middle of doing an update, and the renderer may be respecting that
	// and stopping the update. If the update isn't resumed, the loop below
	// will wait forever for an update that never finishes.
	myPrivate->myImagingEngine->ResumeRenderer();
        myIsPaused = false;
        redo_pause = true;
    }

    // Loop as long as the background thread is still updating.
    while (status == RUNNING_UPDATE_IN_BACKGROUND)
    {
        UTnap(1);
        status = RunningStatus(myRunningInBackground.relaxedLoad());
    }

    // Advance from any error state or the RUNNING_UPDATE_COMPLETE state to
    // the RUNNING_UPDATE_NOT_STARTED state, and free our lock on the stage.
    // But don't do any actual rendering.
    checkRender(false);

    // The checkRender call may delete myImagingEngine if there is an error,
    // so test that this pointer is still valid before redoing the pause.
    if(redo_pause && myPrivate->myImagingEngine)
    {
	myPrivate->myImagingEngine->PauseRenderer();
        myIsPaused = true;
    }
}

bool
HUSD_Imaging::checkRender(bool do_render)
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());

    if(status == RUNNING_UPDATE_FATAL)
    {
        // Serious error, or updating to a completely empty stage.
        // Delete our render delegate and free our stage.
        resetImagingEngine();
	myReadLock.reset();
        myRunningInBackground.store(RUNNING_UPDATE_NOT_STARTED);
        return true;
    }

    if (status == RUNNING_UPDATE_COMPLETE)
    {
	myReadLock.reset();
	myRunningInBackground.store(RUNNING_UPDATE_NOT_STARTED);
        status = RUNNING_UPDATE_NOT_STARTED;
        // If we end up here after running an update, but before finishRender
        // has ever been called, we need to force the do_Render flag to true
        // here so that we call CompleteRender at least once before doing a
        // "convergence" test. The CompleteRender call runs HdPass::_Execute
        // which is where the render pass picks up its new set of AOVs, which
        // may have been altered (and so may point to deleted memory) by our
        // most recent update.
        do_render = true;
    }

    // Call finishRender in a loop. The render delegate may be using the tasks
    // in the task controller to update its render buffers with image data
    // (as prman does).
    if (status == RUNNING_UPDATE_NOT_STARTED)
        finishRender(do_render);

    return (status == RUNNING_UPDATE_NOT_STARTED);
}

bool
HUSD_Imaging::render(const UT_Matrix4D &view_matrix,
                     const UT_Matrix4D &proj_matrix,
                     const UT_DimRect &viewport_rect,
                     const UT_StringRef &renderer_name,
                     const UT_Options *render_opts,
                     bool cam_effects)
{
    // An empty renderer name means clear out our imaging data and exit.
    if (!renderer_name.isstring())
    {
        waitForUpdateToComplete();
        resetImagingEngine();
	return false;
    }

    // UTdebugPrint("RENDER & WAIT");
    if(!setupRenderer(renderer_name, render_opts, cam_effects))
        return false;
    
    // Run the update in the foreground. We never enter any running
    // in background status other than "not started".
    RunningStatus status = updateRenderData(
        view_matrix, proj_matrix, viewport_rect, cam_effects);

    if(status == RUNNING_UPDATE_FATAL)
    {
        // Serious error, or updating to a completely empty stage.
        // Delete our render delegate.
        resetImagingEngine();
    }
    else
    {
        finishRender(true);
        updateComposite(false);
    }
    myReadLock.reset();

    return true;
}

struct prim_data
{
    HdRprim *prim;
    HdSceneDelegate *del;
    uint64 bits;
};

class husd_UpdatePrims
{
public:
    husd_UpdatePrims(const UT_Array<prim_data> &prims,
		     HdChangeTracker &change_tracker,
		     HdRenderParam *render_parm,
		     const HdReprSelector &repr)
	: myPrims(prims),
	  myChangeTracker(change_tracker),
	  myRenderParm(render_parm),
	  myRepr(repr)
	{}

    void operator()(const UT_BlockedRange<exint> &range) const
	{
	    for(int i=range.begin(); i<range.end(); i++)
	    {
		HdDirtyBits bits = HdDirtyBits(myPrims(i).bits);

		// Call Rprim::Sync(..) on each valid repr of the
		// resolved  repr selector.
		for (size_t ridx = 0;
		     ridx < HdReprSelector::MAX_TOPOLOGY_REPRS; ++ridx) {

		    if (myRepr.IsActiveRepr(ridx)) {
			TfToken const& reprToken = myRepr[ridx];

			myPrims(i).prim->Sync(myPrims(i).del,
				   myRenderParm,
				   &bits,
				   reprToken);
		    }
		}

		// Once we finish our updates, mark the prim as clean in the
		// change tracker, or future edits will not mark this prim as
		// dirty. The HdChangeTracker function to mark a prim as
		// dirty will ignore dirtying of bits that are already dirty.
		myChangeTracker.MarkRprimClean(myPrims(i).prim->GetId(), bits);
	    }
	}

private:
    const UT_Array<prim_data>	&myPrims;
    HdChangeTracker		&myChangeTracker;
    HdRenderParam		*myRenderParm;
    const HdReprSelector	&myRepr;
};

void
HUSD_Imaging::updateDeferredPrims()
{
    auto ridx  = myScene->renderIndex();
    auto rparm = myScene->renderParam();

    UT_Array<prim_data> deferred_prims;

    bool shown[HUSD_HydraPrim::NumRenderTags];
    shown[HUSD_HydraPrim::TagDefault] = true; // always shown.
    shown[HUSD_HydraPrim::TagRender]  = myPrivate->myRenderParams.myShowRender;
    shown[HUSD_HydraPrim::TagProxy]   = myPrivate->myRenderParams.myShowProxy;
    shown[HUSD_HydraPrim::TagGuide]   = myPrivate->myRenderParams.myShowGuides;
    shown[HUSD_HydraPrim::TagInvisible] = false;

    for( auto it : myScene->geometry())
    {
	if(it.second->deferredBits()!= 0)
	{
            if(!shown[it.second->renderTag()])
                continue;
            if(it.second->isPendingDelete())
                continue;
            
	    SdfPath path(it.first.sdfPath());
	    HdRprim *prim = const_cast<HdRprim *>(ridx->GetRprim(path));
	    HdSceneDelegate *del = ridx->GetSceneDelegateForRprim(path);
	    if(prim && del)
            {
		deferred_prims.append({ prim, del, it.second->deferredBits()} );
            }
	}
    }

    if(deferred_prims.entries() > 0)
    {
	// This is ignored, but here for completeness.
	static HdReprSelector	 theRepr(HdReprTokens->smoothHull);
	husd_UpdatePrims	 prim_update(deferred_prims,
				    ridx->GetChangeTracker(),
				    rparm, theRepr);

	UTparallelFor(UT_BlockedRange<exint>(0, deferred_prims.entries()),
		      prim_update);
    }

    deferred_prims.clear();
    for(auto it : myScene->materials())
	if(it.second->deferredBits()!= 0)
        {
	    PXR_NS::SdfPath path(it.first.sdfPath());
	    HdSprim *prim = 
                ridx->GetSprim(HdPrimTypeTokens->material,path);
            
            auto sdel = ridx->GetSceneDelegateForRprim(path);
	    if(prim && sdel)
            {
                HdDirtyBits bits = it.second->deferredBits();
                prim->Sync(sdel, rparm, &bits);
                ridx->GetChangeTracker().MarkSprimClean(path);
            }
        }
}

bool
HUSD_Imaging::getBoundingBox(UT_BoundingBox &bbox, const UT_Matrix3R *rot) const
{
    HUSD_AutoReadLock    lock(viewerLopDataHandle(), myOverrides, myPostLayers);

    if (lock.data() && lock.data()->isStageValid())
    {
	auto		 prim = lock.data()->stage()->GetPseudoRoot();
	UsdTimeCode	 t = myPrivate->myRenderParams.myFrame;
	TfTokenVector	 purposes;

	purposes.push_back(UsdGeomTokens->default_);
	purposes.push_back(UsdGeomTokens->proxy);
	purposes.push_back(UsdGeomTokens->render);
	if (prim)
	{
	    UsdGeomBBoxCache	 bboxcache(t, purposes, true);
	    GfBBox3d		 gfbbox;

	    gfbbox = bboxcache.ComputeWorldBound(prim);
	    if (!gfbbox.GetRange().IsEmpty())
	    {
		const GfRange3d	 range = gfbbox.ComputeAlignedRange();

		bbox = UT_BoundingBox(
		    range.GetMin()[0],
		    range.GetMin()[1],
		    range.GetMin()[2],
		    range.GetMax()[0],
		    range.GetMax()[1],
		    range.GetMax()[2]);

		return true;
	    }
	}
    }

    return false;
}

void
HUSD_Imaging::initializeAvailableRenderers()
{
    static bool theRendererInfoMapGenerated = false;

    // The list of available renderers shouldn't change, so just generate the
    // list once, and remember it.
    if (!theRendererInfoMapGenerated)
    {
        HfPluginDescVector	 plugins;

        theRendererInfoMapGenerated = true;
        HdRendererPluginRegistry::GetInstance().GetPluginDescs(&plugins);
        for (int i = 0, n = plugins.size(); i < n; i++)
        {
            HUSD_RendererInfo	 info =
                HUSD_RendererInfo::getRendererInfo(plugins[i].id.GetText(),
                    plugins[i].displayName);

            if (info.isValid())
                theRendererInfoMap.emplace(plugins[i].id.GetText(), info);
        }
    }
}

bool
HUSD_Imaging::getAvailableRenderers(HUSD_RendererInfoMap &info_map)
{
    initializeAvailableRenderers();

    info_map = theRendererInfoMap;

    return (info_map.size() > 0);
}

bool
HUSD_Imaging::canPause() const
{
    if (myPrivate->myImagingEngine)
        return myPrivate->myImagingEngine->IsPauseRendererSupported();

    return false;
}

void
HUSD_Imaging::pauseRender()
{
    if (!myIsPaused && canPause())
    {
        myPrivate->myImagingEngine->PauseRenderer();
        myIsPaused = true;
    }
}

void
HUSD_Imaging::resumeRender()
{
    // If updates aren't allowed, then resuming rendering also isn't allowed.
    // This is the difference between a user-imposed "pause" from a menu and
    // the automatic pause/resume that happens when tumbling or performing an
    // update to the scene.
    if (myIsPaused && myAllowUpdates && canPause())
    {
        myPrivate->myImagingEngine->ResumeRenderer();
        myIsPaused = false;
    }
}

bool
HUSD_Imaging::isPausedByUser() const
{
    // This tests if we have been paused by the user, which involves setting
    // both the paused flag and preventing updates.
    return (myIsPaused && !myAllowUpdates);
}

bool
HUSD_Imaging::isStoppedByUser() const
{
    // This tests if we have been stopped by the user, which involves
    // deleting the render delegate and also preventing updates.
    return (myPrivate->myImagingEngine.get() == nullptr && !myAllowUpdates);
}

bool
HUSD_Imaging::rendererCreated() const
{
    return (myPrivate->myImagingEngine.get() != nullptr);
}

void
HUSD_Imaging::getRenderStats(UT_Options &opts)
{
    if (!myPrivate->myImagingEngine)
        return;
    
    opts.clear();

    UT_JSONValue            jdict;
    {
        // Convert in a scope so that the JSON writer is flushed to the value
        UT_AutoJSONWriter       jw(jdict);
        HUSDconvertDictionary(*jw, myPrivate->myImagingEngine->GetRenderStats());
    }
    
    UT_JSONValueMap *jsonStatsMap = jdict.getMap();
    if (jsonStatsMap)
        opts.load(*jsonStatsMap, false, true, true);
    
    UT_OptionsHolder vp_opts;
    vp_opts.update([&](UT_Options &opt) {
        theRendererInfoMap[myRendererName].extractStatsData(opt, jdict);
    });

    opts.setOptionDict("__viewport", vp_opts);
    opts.setOptionS("__json", jdict.toString());
}

void
HUSD_Imaging::setRenderSettings(const UT_StringRef &settings_path,
                                int w, int h)
{
    HUSD_AutoReadLock lock(viewerLopDataHandle(), myOverrides, myPostLayers);

    UT_StringHolder spath;
    if(settings_path.isstring())
    {
        if(settings_path != HUSD_Scene::viewportRenderPrimToken())
            spath = settings_path;
    }
    else
    {
        HUSD_Info info(lock);
        spath = info.getCurrentRenderSettings();
        if(!spath.isstring())
        {
            UT_StringArray paths;
            if(info.getAllRenderSettings(paths) && paths.entries() > 0)
                spath = paths(0);
        }
    }

    bool valid = spath.isstring() && lock.data();
    if(valid)
    {
        SdfPath path(spath.toStdString());

        myRenderSettingsContext->setRes(w,h);
        // Our render settings are "valid" only if we have managed to set a
        // valid render settings USD prim into myRenderSettings.
        if (myRenderSettings->init(lock.data()->stage(), path,
                *myRenderSettingsContext) &&
            myRenderSettings->prim())
        {
            // If there are only delegate render products, we want to create a
            // dummy raster product so we can get AOVs.
            myRenderSettings->resolveProducts(lock.data()->stage(),
                *myRenderSettingsContext, true);

            HdAovDescriptorList descs;
            TfTokenVector aov_names;

            if(myRenderSettings->collectAovs(aov_names, descs))
                myRenderSettingsContext->setAOVs(aov_names, descs);

            myPrivate->myOldPrimRenderSettingMap =
                myPrivate->myPrimRenderSettingMap;
            myPrivate->myPrimRenderSettingMap =
                myRenderSettings->renderSettings();

            mySettingsChanged = true;
            myValidRenderSettingsPrim = true;
            valid = true;
        }
        else
            valid = false;
    }

    if(!valid)
    {
        if (myValidRenderSettingsPrim)
        {
            myRenderSettings = UTmakeUnique<XUSD_RenderSettings>(
                    UT_StringHolder::theEmptyString,
                    UT_StringHolder::theEmptyString,
                    0);
        }
        if(myValidRenderSettingsPrim)
            mySettingsChanged = true;
        myPrivate->myOldPrimRenderSettingMap =
            myPrivate->myPrimRenderSettingMap;
        myPrivate->myPrimRenderSettingMap.clear();
        myValidRenderSettingsPrim = false;
    }
}

void
HUSD_Imaging::getPrimPathsFromRenderKeys(
        const UT_Set<HUSD_RenderKey> &keys,
        HUSD_RenderKeyPathMap &outkeypathmap)
{
    if(myPrivate->myImagingEngine)
    {
        UT_Array<HUSD_RenderKey> decode_keys;

        for (auto &&key : keys)
        {
            auto it = myRenderKeyToPathMap.find(key);

            if (it == myRenderKeyToPathMap.end())
                decode_keys.append(key);
            else
                outkeypathmap.emplace(key, it->second);
        }

        SdfPathVector primpaths;
        std::vector<HdInstancerContext> instancer_contexts;
        UT_WorkBuffer index_string;
        char numstr[UT_NUMBUF];

        if (myPrivate->myImagingEngine->DecodeIntersections(
                decode_keys, primpaths, instancer_contexts))
        {
            for (int i = 0; i < decode_keys.size(); i++)
            {
                UT_StringHolder path;

                // The instancer context will only be populated if the
                // instancer is a point instancer rather than a native
                // instancer. For point instancers, the path should be of
                // the form "/inst[0]", whereas native instancers should
                // return the instance proxy path, and so we bypass the
                // indexed path construction.
                if (!instancer_contexts[i].empty())
                {
                    index_string.strcpy(
                        instancer_contexts[i][0].first.GetAsString());

                    for (int j = 0; j < instancer_contexts[i].size(); j++)
                    {
                        UT_String::itoa(numstr,instancer_contexts[i][j].second);
                        index_string.append('[');
                        index_string.append(numstr);
                        index_string.append(']');
                    }
                    path = index_string;
                }
                else
                    path = primpaths[i].GetAsString();

                myRenderKeyToPathMap.emplace(decode_keys[i], path);
                outkeypathmap.emplace(decode_keys[i], path);
            }
        }
    }
}
