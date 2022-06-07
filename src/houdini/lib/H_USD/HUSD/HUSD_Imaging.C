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
#include "HUSD_Info.h"
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
#include <UT/UT_Array.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Defines.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_ErrorManager.h>
#include <UT/UT_Exit.h>
#include <UT/UT_Signal.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_PerfMonAutoEvent.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_SysClone.h>
#include <UT/UT_TaskGroup.h>
#include <TIL/TIL_TextureMap.h>
#include <OP/OP_Director.h>

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
// Count of the number of render engines that use the texture cache.  The
// cache can only be cleared if there are no active renders.
static SYS_AtomicInt<int> theTextureCacheRenders(0);

static bool
renderUsesTextureCache(const UT_StringRef &name)
{
    return name == HUSD_Constants::getKarmaRendererPluginName();
}

static PXL_DataFormat
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

static UT_Set<HUSD_Imaging *>	 theActiveRenders;
static UT_Lock			 theActiveRenderLock;
static HUSD_RendererInfoMap	 theRendererInfoMap;

static void
backgroundRenderExitCB(void *data)
{
    UT_Lock::Scope	lock(theActiveRenderLock);
    for (auto &&item : theActiveRenders)
	item->terminateRender(true);
}

static void
backgroundRenderState(bool converged, HUSD_Imaging *ptr)
{
    UT_Lock::Scope	lock(theActiveRenderLock);
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
    defaultAovDescriptor(const PXR_NS::TfToken &aov) const override
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

    GfVec2i overrideResolution(const PXR_NS::GfVec2i &res) const override
        { return (myW > 0) ? GfVec2i(myW, myH) : res; }
    
    bool    allowCameraless() const override
        { return true; }
    void    setCamera(const SdfPath &campath)
        { myCameraPath = campath; }

private:
    UT_StringMap<PXR_NS::HdAovDescriptor> myAOVs;
    PXR_NS::SdfPath myCameraPath;
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
    : myPrivate(new husd_ImagingPrivate),
      myRenderSettings(nullptr),
      myRenderSettingsContext(nullptr),
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
    myRenderSettings = new XUSD_RenderSettings(
            UT_StringHolder::theEmptyString,
            UT_StringHolder::theEmptyString,
            0);
    myRenderSettingsContext = new husd_DefaultRenderSettingContext;
}

HUSD_Imaging::~HUSD_Imaging()
{
    UT_Lock::Scope	lock(theActiveRenderLock);
    theActiveRenders.erase(this);

    delete myRenderSettingsContext;
    delete myRenderSettings;

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
    if (clear_cache)
    {
        TIL_TextureCache::clearCache(1);        // Clear out of date textures from cache

        // Clear VEX geometry file cache as well.
        OP_Director *dir = OPgetDirector();
        if (dir)
        {
            CMD_Manager *cmd = dir->getCommandManager();
            if (cmd && cmd->isCommandDefined("geocache"))
                cmd->execute("geocache -n");
        }
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
            UT_StringHolder(), stage->GetPseudoRoot(),
            myPrivate->myRenderParams);
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
HUSD_Imaging::setStages(const HUSD_DataHandleMap &data_handles,
        const HUSD_ConstOverridesPtr &overrides,
        const HUSD_ConstPostLayersPtr &postlayers)
{
    myDataHandles = data_handles;
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
HUSD_Imaging::setHeadlight(bool doheadlight)
{
    if (doheadlight != myWantsHeadlight)
    {
	mySettingsChanged = true;
	myWantsHeadlight = doheadlight;
	return true;
    }

    return false;
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
    static const HUSD_DataHandle theEmptyDataHandle;
    auto it = myDataHandles.find(UT_StringHolder());

    if (it != myDataHandles.end())
        return it->second;

    return theEmptyDataHandle;
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
                            bool force_null_hgi /*=false*/)
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
	if(*render_opts != myCurrentOptions)
	{
	    myCurrentOptions = *render_opts;
	    mySettingsChanged = true;
	}
    }
    else if(myCurrentOptions.getNumOptions() > 0)
    {
	myCurrentOptions.clear();
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
    if (updateRestartCameraSettings() ||
        (myPrivate->myImagingEngine && anyRestartRenderSettingsChanged()))
    {
        resetImagingEngine();
    }

    bool do_lighting = false;
    auto &&draw_mode = myPrivate->myRenderParams.myDrawMode;
    if (draw_mode == XUSD_ImagingRenderParams::DRAW_SHADED_FLAT ||
        draw_mode == XUSD_ImagingRenderParams::DRAW_SHADED_SMOOTH ||
        draw_mode == XUSD_ImagingRenderParams::DRAW_WIREFRAME_ON_SURFACE)
	do_lighting = myDoLighting;

    myPrivate->myRenderParams.myEnableLighting = do_lighting;
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
        bool drawmode = theRendererInfoMap[myRendererName].drawModeSupport();

	myPrivate->myImagingEngine =
            XUSD_ImagingEngine::createImagingEngine(force_null_hgi);
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

                myPlaneList.append( t.GetText());
                if(myOutputPlane.isstring() &&
                   myOutputPlane == myPlaneList.last())
                {
                    has_aov = true;
                    myCurrentAOV = myOutputPlane;
                }
            }
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
        list.push_back(HdAovTokens->color);
        list.push_back(HdAovTokens->depth);
        list.push_back(HdAovTokens->primId);
        list.push_back(HdAovTokens->instanceId);

        auto aov_list = myPrivate->myImagingEngine->GetRendererAovs();
        for(auto &t : aov_list)
        {
            if(t == HdAovTokens->primId)
                continue;
        
            myPlaneList.append( t.GetText());
            if(myOutputPlane.isstring() && myOutputPlane == myPlaneList.last())
            {
                has_aov = true;
                myCurrentAOV = myOutputPlane;
            }
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
static const UT_StringHolder theHoudiniDoLightingToken("houdini:dolighting");
static const UT_StringHolder theHoudiniHeadlightToken("houdini:headlight");

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
HUSD_Imaging::updateRestartCameraSettings() const
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

        if(!myCameraPath.isstring() || !myCameraSynced)
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
            isRestartSettingChanged(theHoudiniDoLightingToken,
                VtValue(myDoLighting), restart_render_settings,
                myPrivate->myCurrentRenderSettings) ||
            isRestartSettingChanged(theHoudiniHeadlightToken,
                VtValue(myWantsHeadlight), restart_render_settings,
                myPrivate->myCurrentRenderSettings) ||
            isRestartSettingChanged("renderCameraPath",
                VtValue(campath), restart_render_settings,
                myPrivate->myCurrentRenderSettings))
            return true;

        for(auto opt : myPrivate->myOldPrimRenderSettingMap)
        {
            const auto &key = opt.first;
            auto &&it = myPrivate->myPrimRenderSettingMap.find(key);
            if (it == myPrivate->myPrimRenderSettingMap.end() &&
                isRestartSetting(key.GetText(), restart_render_settings))
                return true;
        }

        for(auto opt = myCurrentOptions.begin();
            opt != myCurrentOptions.end(); ++opt)
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
        const VtValue &vtvalue)
{
    TfToken       tfkey(key.toStdString());
    auto        &&it = myPrivate->myCurrentRenderSettings.find(tfkey);

    if (it == myPrivate->myCurrentRenderSettings.end() || it->second != vtvalue)
    {
        myPrivate->myImagingEngine->SetRendererSetting(tfkey, vtvalue);
        myPrivate->myCurrentRenderSettings[tfkey] = vtvalue;
        UT_ErrorLog::format(4, "Render setting from Houdini: {} = {}",
            tfkey, vtvalue);
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

    if (myPrivate->myRenderParams != myPrivate->myLastRenderParams ||
        mySettingsChanged)
    {
        myPrivate->myLastRenderParams = myPrivate->myRenderParams;
        mySettingsChanged = false;

        updateSettingIfRequired(theHoudiniViewportToken, VtValue(true));
        updateSettingIfRequired(theHoudiniFrameToken, VtValue(myFrame));
        updateSettingIfRequired(theHoudiniFPSToken, VtValue(getFPS()));
        // These should soon be replaced by the render_options
        // below
        updateSettingIfRequired(theHoudiniDoLightingToken,
            VtValue(myDoLighting));
        updateSettingIfRequired(theHoudiniHeadlightToken,
            VtValue(myWantsHeadlight));

        SdfPath campath;
        if(!myCameraPath.isstring() || !myCameraSynced)
            campath = HUSDgetHoudiniFreeCameraSdfPath();
        else if(myCameraPath)
            campath = SdfPath(myCameraPath.toStdString());

        updateSettingIfRequired("renderCameraPath", VtValue(campath));

        for(auto opt : myPrivate->myOldPrimRenderSettingMap)
        {
            auto &&it = myPrivate->myPrimRenderSettingMap.find(opt.first);
            if (it == myPrivate->myPrimRenderSettingMap.end())
            {
                myPrivate->myImagingEngine->
                    SetRendererSetting(opt.first, VtValue());
                myPrivate->myCurrentRenderSettings.erase(opt.first);
                UT_ErrorLog::format(4,
                    "Render setting from USD: {} removed",
                    opt.first);
            }
        }

        for(auto opt = myCurrentOptions.begin();
            opt != myCurrentOptions.end(); ++opt)
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
            {
                auto &&it = myPrivate->myCurrentRenderSettings.find(opt.first);
                if (it == myPrivate->myCurrentRenderSettings.end() ||
                    it->second != opt.second)
                {
                    myPrivate->myImagingEngine->
                        SetRendererSetting(opt.first, opt.second);
                    myPrivate->myCurrentRenderSettings[opt.first] = opt.second;
                    UT_ErrorLog::format(4,
                        "Render setting from USD: {} = {}",
                        opt.first, opt.second);
                }
            }
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
                               const UT_DimRect  &viewport_rect,
                               bool               use_camera)
{
    // If we have been told not to render, our engine may be null, but we
    // still want to report the requested update as being complete.
    if (!myAllowUpdates)
        return RUNNING_UPDATE_COMPLETE;

    auto &&engine = myPrivate->myImagingEngine;
    bool success = true;

    myRenderKeyToPathMap.clear();
    for (auto it = myDataHandles.begin(); it != myDataHandles.end(); ++it)
    {
        HUSD_AutoReadLock *lock =
            new HUSD_AutoReadLock(it->second, myOverrides, myPostLayers);

        myReadLocks.emplace(it->first, lock);
        if (lock->data() && lock->data()->isStageValid())
        {
            if (!it->first.isstring())
            {
                UT_Array<XUSD_GLSimpleLight> lights;

                if (myPrivate->myRenderParams.myEnableLighting)
                {
                    myHasHeadlight = false;
                    if(myWantsHeadlight)
                    {
                        XUSD_GLSimpleLight	 light;

                        light.myIsCameraSpaceLight = true;
                        light.myDiffuse = UT_Vector4F(0.8, 0.8, 0.8, 1.0);
                        lights.append(light);
                        myHasHeadlight = true;
                    }
                }

                engine->SetLightingState(lights, GfVec4f(0.0, 0.0, 0.0, 0.0));

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
                if(use_camera)
                {
                    if(myCameraPath && myCameraSynced)
                        campath = SdfPath(myCameraPath.toStdString());
                    else
                        campath = HUSDgetHoudiniFreeCameraSdfPath();
                }
                
                if(!campath.IsEmpty())
                {
                    engine->SetCameraPath(campath);
                    engine->SetWindowPolicy(
                        (CameraUtilConformWindowPolicy)myConformPolicy);
                }
                else
                    engine->SetCameraState(gf_view_matrix, gf_proj_matrix);
                myRenderSettingsContext->setCamera(campath);

                updateSettingsIfRequired(*lock);
            }

            try
            {
                engine->DispatchRender(it->first,
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
        {
            success = false;
            break;
        }
    }

    // Remove any delegates that no longer exist.
    for (auto &&id : engine->GetSceneDelegateIds())
    {
        if (!myDataHandles.contains(id))
            engine->DispatchRender(id,
                UsdPrim(), myPrivate->myRenderParams);
    }

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

void
HUSD_Imaging::updateComposite(bool free_if_missing)
{
    bool     missing = true;

    if(myCompositor && myPrivate && myPrivate->myImagingEngine)
    {
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
	    color_buf->Resolve();
	    auto color_map = color_buf->Map();
	    auto w = color_buf->GetWidth();
	    auto h = color_buf->GetHeight();

	    if (w && h)
	    {
		myCompositor->setResolution(w, h);

		auto df = color_buf->GetFormat();
		myCompositor->updateColorBuffer(color_map,
						HdToPXL(df),
						HdGetComponentCount(df));
	    }
	    color_buf->Unmap();
	    color_map = nullptr;

	    if (w && h)
	    {
		depth_buf->Resolve();
		auto depth_map = depth_buf->Map();
		if(depth_buf->GetWidth()  == w && depth_buf->GetHeight() == h)
		{
		    auto df = depth_buf->GetFormat();
		    myCompositor->updateDepthBuffer(depth_map,
						   HdToPXL(df),
						   HdGetComponentCount(df));
		}
		else
		    myCompositor->updateDepthBuffer(nullptr, PXL_FLOAT32, 0);
		depth_buf->Unmap();
	    }

            if(w && h && prim_id)
            {
                prim_id->Resolve();
		auto id_map = prim_id->Map();
		if(prim_id->GetWidth()  == w && prim_id->GetHeight() == h)
		{
		    auto df = prim_id->GetFormat();
		    myCompositor->updatePrimIDBuffer(id_map, HdToPXL(df));
		}
		else
		    myCompositor->updatePrimIDBuffer(nullptr, PXL_INT32);
		prim_id->Unmap();
	    }
            else
                myCompositor->updatePrimIDBuffer(nullptr, PXL_INT32);

            if(w && h && inst_id)
            {
                inst_id->Resolve();
		auto id_map = inst_id->Map();
		if(inst_id->GetWidth()  == w && inst_id->GetHeight() == h)
		{
		    auto df = inst_id->GetFormat();
		    myCompositor->updateInstanceIDBuffer(id_map, HdToPXL(df));
		}
		else
		    myCompositor->updateInstanceIDBuffer(nullptr, PXL_INT32);
		inst_id->Unmap();
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
                                     const UT_DimRect  &viewport_rect,
                                     const UT_StringRef &renderer,
                                     const UT_Options  *render_opts,
                                     bool /*update_deferred =false*/,
                                     bool use_cam /*=true*/,
                                     bool force_null_hgi /*=false*/)
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
    if(!setupRenderer(renderer, render_opts, force_null_hgi))
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
	myPrivate->myUpdateTask.run([this, view_matrix,
                                     proj_matrix, viewport_rect, use_cam]()
            {
                UT_PerfMonAutoViewportDrawEvent perfevent("LOP Viewer",
                    "Background Update USD Stage", UT_PERFMON_3D_VIEWPORT);

                RunningStatus status
                    = updateRenderData(view_matrix, proj_matrix, 
                                       viewport_rect, use_cam);

                if (status == RUNNING_UPDATE_NOT_STARTED ||
                    status == RUNNING_UPDATE_FATAL)
                    myReadLocks.clear();
                myRunningInBackground.store(status);
            });
    }
    else
    {
	status = updateRenderData(view_matrix, proj_matrix, viewport_rect,
				  use_cam);

	 if (status == RUNNING_UPDATE_NOT_STARTED ||
	     status == RUNNING_UPDATE_FATAL)
	     myReadLocks.clear();
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

    if(redo_pause)
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
	myReadLocks.clear();
        myRunningInBackground.store(RUNNING_UPDATE_NOT_STARTED);
        return true;
    }

    if (status == RUNNING_UPDATE_COMPLETE)
    {
	myReadLocks.clear();
	myRunningInBackground.store(RUNNING_UPDATE_NOT_STARTED);
        status = RUNNING_UPDATE_NOT_STARTED;
    }

    // Call finishRender in a loop. The render delegate may be using the tasks
    // in the task controller to update its render buffers with image data
    // (as prman does).
    if (status == RUNNING_UPDATE_NOT_STARTED)
        finishRender(do_render);

    return (status == RUNNING_UPDATE_NOT_STARTED);
}

bool
HUSD_Imaging::render(const UT_Matrix4D  &view_matrix,
                     const UT_Matrix4D  &proj_matrix,
                     const UT_DimRect   &viewport_rect,
                     const UT_StringRef &renderer_name,
                     const UT_Options   *render_opts,
                     bool                /*update_deferred =false*/,
                     bool                use_cam /*=true*/,
                     bool                force_null_hgi /*=false*/)
{
    // An empty renderer name means clear out our imaging data and exit.
    if (!renderer_name.isstring())
    {
        waitForUpdateToComplete();
        resetImagingEngine();
	return false;
    }

    // UTdebugPrint("RENDER & WAIT");
    if(!setupRenderer(renderer_name, render_opts, force_null_hgi))
        return false;
    
    // Run the update in the foreground. We never enter any running
    // in background status other than "not started".
    RunningStatus status = 
        updateRenderData(view_matrix, proj_matrix, viewport_rect, use_cam);

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
    myReadLocks.clear();

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
            
	    PXR_NS::SdfPath path(it.first.c_str());
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

    VtDictionary dict= myPrivate->myImagingEngine->GetRenderStats();

    for(auto itr : dict)
    {
        auto &name = itr.first;
        VtValue &val = itr.second;

        if(val.IsHolding<int>())
        {
            opts.setOptionI(name, val.UncheckedGet<int>());
        }
        if(val.IsHolding<exint>())
        {
            opts.setOptionI(name, val.UncheckedGet<exint>());
        }
        else if(val.IsHolding<float>())
        {
            opts.setOptionF(name, val.UncheckedGet<float>());
        }
        else if(val.IsHolding<double>())
        {
            opts.setOptionF(name, val.UncheckedGet<double>());
        }
        else if(val.IsHolding<std::string>())
        {
            opts.setOptionS(name, val.UncheckedGet<std::string>());
        }
        else if(val.IsHolding<GfSize2>())
        {
            auto gvec2 = val.UncheckedGet<GfSize2>();
            uint64 vals[] = { gvec2[0], gvec2[1] };
            opts.setOptionIArray(name, (int64*)vals, 2);
        }
    }
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
        PXR_NS::SdfPath path(spath.toStdString());

        myRenderSettingsContext->setRes(w,h);
        // Our render settings are "valid" only if we have managed to set a
        // valid render settings USD prim into myRenderSettings.
        if (myRenderSettings->init(lock.data()->stage(), path,
                                  *myRenderSettingsContext) &&
            myRenderSettings->prim())
        {
            myRenderSettings->resolveProducts(lock.data()->stage(),
                                              *myRenderSettingsContext);
            
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
