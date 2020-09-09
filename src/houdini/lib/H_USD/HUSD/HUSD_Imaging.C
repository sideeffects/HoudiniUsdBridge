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
#include "XUSD_PathSet.h"
#include "XUSD_RenderSettings.h"
#include "XUSD_Utils.h"

#include <gusd/UT_Gf.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Exit.h>
#include <UT/UT_PerfMonAutoEvent.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_ErrorManager.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_SysClone.h>

#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/size2.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdLux/light.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/glf/contextCaps.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hdx/taskController.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImagingGL/engine.h>
#include <pxr/usdImaging/usdImagingGL/renderParams.h>
#include <pxr/imaging/hd/rprim.h>

#include <algorithm>
#include <iostream>
#include <initializer_list>
#include <thread>

PXR_NAMESPACE_USING_DIRECTIVE

PXL_DataFormat HdToPXL(HdFormat df)
{
    switch(HdGetComponentFormat(df))
    {
    case HdFormatUNorm8: return PXL_INT8;
    case HdFormatSNorm8: return PXL_INT8; // We don't have a format for this.
    case HdFormatFloat16: return PXL_FLOAT16;
    case HdFormatFloat32: return PXL_FLOAT32;
    case HdFormatInt32: return PXL_INT32;
    default:
	break;
    }
    // bad format?
    return PXL_INT8;
}


class husd_DefaultRenderSettingContext : public XUSD_RenderSettingsContext
{
public: 
    TfToken	renderer() const override
        { return TfToken(""); }
    fpreal      startFrame() const override
        { return 1.0; }
    UsdTimeCode evalTime() const override
        { return UsdTimeCode::EarliestTime(); }
    GfVec2i	defaultResolution() const override
        { return GfVec2i(myW,myH); }
    SdfPath	overrideCamera() const override
        { return myCameraPath; }

    HdAovDescriptor
    defaultAovDescriptor(const PXR_NS::TfToken &aov) const override
        {
            return HdAovDescriptor();
        }

    bool getAovDescriptor(TfToken &aov, HdAovDescriptor &desc) const
        {
            auto entry = myAOVs.find(aov.GetText());
            if(entry != myAOVs.end())
            {
                desc = entry->second;
                return true;
            }
            return false;
        }

    bool hasAOV(const UT_StringRef &name) const
        {
            return (myAOVs.find(name) !=  myAOVs.end());
        }
            
    void setRes(int w, int h)
        {
            myW = w;
            myH = h;
        }

    void setAOVs(const TfTokenVector &aov_names,
                 const HdAovDescriptorList &aov_desc)
        {
            myAOVs.clear();
            for(int i=0; i<aov_names.size(); i++)
                myAOVs[ aov_names[i].GetText() ] = aov_desc[i];

            VtValue zero((int)0);
            HdAovDescriptor desc(HdFormatInt32, false, zero);

            myAOVs[ HdAovTokens->primId.GetText() ] = desc;
            myAOVs[ HdAovTokens->instanceId.GetText() ] = desc;
        }
    GfVec2i overrideResolution(const PXR_NS::GfVec2i &res) const override
    {
	return (myW > 0) ? GfVec2i(myW,myH) : res;
    }
    
    bool    allowCameraless() const override { return true; }
    void    setCamera(const SdfPath &campath) { myCameraPath = campath; }
private:
    UT_StringMap<PXR_NS::HdAovDescriptor> myAOVs;
    PXR_NS::SdfPath myCameraPath;
    int myW = 0;
    int myH = 0;
};


class HUSD_ImagingEngine : public UsdImagingGLEngine
{
public:
		 HUSD_ImagingEngine()
		 { }
    virtual	~HUSD_ImagingEngine()
		 {
		 }

    bool	 SetRendererAovs(TfTokenVector const &ids)
    {
	if (ARCH_UNLIKELY(_legacyImpl))
	    return false;

	TF_VERIFY(_renderIndex);
	if (_renderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer))
	{
	    // For color, render straight to the viewport instead of rendering
	    // to an AOV and colorizing (which is the same, but more work).
	    if (ids.size() == 1 && ids[0] == HdAovTokens->color) {
		_taskController->SetRenderOutputs(TfTokenVector());
	    } else {
		_taskController->SetRenderOutputs(ids);
	    }

	    return true;
	}

	return false;
    }

    void        SetSamplingCamera(const SdfPath &camera)
        {
            _GetSceneDelegate()->
                SetCameraForSampling(camera);
        }

    void        SetRenderOutputSettings(TfToken const &name,
                                        HdAovDescriptor const& desc)
        {
            _taskController->SetRenderOutputSettings(name, desc);
        }

    void        SetDisplayUnloadedPrimsWithBounds(bool displayUnloaded)
        {
            _GetSceneDelegate()->
                SetDisplayUnloadedPrimsWithBounds(displayUnloaded);
        }

    void        SetUsdDrawModesEnabled(bool enableUsdDrawModes)
        {
            _GetSceneDelegate()->
                SetUsdDrawModesEnabled(enableUsdDrawModes);
        }

    // This method was copied from UsdImagingGLEngine::Render and
    // UsdImagingGLEngine::RenderBatch, but has the final _Execute
    // call removed.
    void	 DispatchRender(const UsdPrim &root,
			const UsdImagingGLRenderParams &params)
    {
	TF_VERIFY(_taskController);

	PrepareBatch(root, params);

	// XXX(UsdImagingPaths): Is it correct to map USD root path directly
	// to the cachePath here?
	SdfPath cachePath = root.GetPath();
	SdfPathVector paths(1,
	    _GetSceneDelegate()->ConvertCachePathToIndexPath(cachePath));

	_taskController->SetFreeCameraClipPlanes(params.clipPlanes);
	_UpdateHydraCollection(&_renderCollection, paths, params);
	_taskController->SetCollection(_renderCollection);

	TfTokenVector renderTags;
	_MakeRenderTags(params, &renderTags);
	_taskController->SetRenderTags(renderTags);

	HdxRenderTaskParams hdParams = _MakeHdxRenderTaskParams(params);
	_taskController->SetRenderParams(hdParams);
	_taskController->SetEnableSelection(params.highlight);

	SetColorCorrectionSettings(params.colorCorrectionMode);

	// Forward scene materials enable option to delegate
	_GetSceneDelegate()->
            SetSceneMaterialsEnabled(params.enableSceneMaterials);

	VtValue selectionValue(_selTracker);
        _GetHdEngine()->SetTaskContextData(
            HdxTokens->selectionState, selectionValue);

	// This chunk of code comes from HdEngine::Execute, which is called
	// from _Execute. The _Execute call was moved to a separate
	// CompleteRender method, but the SyncAll is the most expensive part
	// generally, and can be done early. When it gets called again as
	// part of _Execute, it will essentially be a no-op because the
	// Sync is complete and the task context will be unchanged.
	HdTaskContext taskContext;

        // Add this call to SyncAll, which actually comes from the
        // HdEngine::Execute method. But we want to rearrange the ordering of
        // these calls a bit to keep all the expensive stuff in this one
        // method. The _Render call will also call SyncAll, but because we're
        // calling it here right before that subsequent call, it will basically
        // be a no-op the second time.
	auto tasks = _taskController->GetRenderingTasks();
	_renderIndex->SyncAll(&tasks, &taskContext);
    }

    // This method complements the DispatchRender method above by making the
    // final _Render call from UsdImagingGLEngine::Render.
    void	 CompleteRender(const UsdImagingGLRenderParams &params)
    {
	_Execute(params, _taskController->GetRenderingTasks());
    }

    HdRenderBuffer *GetRenderOutput(TfToken const &name)
    {
	if (_taskController)
	    return _taskController->GetRenderOutput(name);

	return nullptr;
    }

    VtDictionary GetRenderStats()
    {
	TF_VERIFY(_renderIndex);
	return _renderIndex->GetRenderDelegate()->GetRenderStats();
    }

private:
    // Copy this method from
    // UsdImagingGLEngine::_ComputeRenderTags, which is
    // not exported from the UsdImagingGL library. We need access to this
    // method from within DispatchRender.
    void
    _MakeRenderTags(UsdImagingGLRenderParams const& params,
	TfTokenVector *renderTags)
    {
	// Calculate the rendertags needed based on the parameters passed by
	// the application
	renderTags->clear();
	renderTags->reserve(4);
	renderTags->push_back(HdTokens->geometry);
	if (params.showGuides) {
	    renderTags->push_back(HdRenderTagTokens->guide);
	}
	if (params.showProxy) {
	    renderTags->push_back(UsdGeomTokens->proxy);
	}
	if (params.showRender) {
	    renderTags->push_back(UsdGeomTokens->render);
	}
    }

    // Copy this method from
    // UsdImagingGLEngine::_MakeHydraUsdImagingGLRenderParams, which is
    // not exported from the UsdImagingGL library. We need access to this
    // method from within DispatchRender.
    HdxRenderTaskParams
    _MakeHdxRenderTaskParams(UsdImagingGLRenderParams const &renderParams)
    {
	// Note this table is dangerous and making changes to the order of the
	// enums in UsdImagingGLCullStyle, will affect this with no compiler
	// help.
	static const HdCullStyle USD_2_HD_CULL_STYLE[] =
	{
	    HdCullStyleDontCare,
	    HdCullStyleNothing,
	    HdCullStyleBack,
	    HdCullStyleFront,
	    HdCullStyleBackUnlessDoubleSided,
	};
	static_assert(((sizeof(USD_2_HD_CULL_STYLE) / 
			sizeof(USD_2_HD_CULL_STYLE[0])) 
		  == (size_t)UsdImagingGLCullStyle::CULL_STYLE_COUNT),
	    "enum size mismatch");

	HdxRenderTaskParams params;

	params.overrideColor       = renderParams.overrideColor;
	params.wireframeColor      = renderParams.wireframeColor;

	if (renderParams.drawMode == UsdImagingGLDrawMode::DRAW_GEOM_ONLY ||
	    renderParams.drawMode == UsdImagingGLDrawMode::DRAW_POINTS) {
	    params.enableLighting = false;
	} else {
	    params.enableLighting =  renderParams.enableLighting &&
				    !renderParams.enableIdRender;
	}

	params.enableIdRender      = renderParams.enableIdRender;
	params.depthBiasUseDefault = true;
	params.depthFunc           = HdCmpFuncLess;
	params.cullStyle           = USD_2_HD_CULL_STYLE[
	    (size_t)renderParams.cullStyle];

	// Decrease the alpha threshold if we are using sample alpha to
	// coverage.
	if (renderParams.alphaThreshold < 0.0) {
	    params.alphaThreshold =
		renderParams.enableSampleAlphaToCoverage ? 0.1f : 0.5f;
	} else {
	    params.alphaThreshold =
		renderParams.alphaThreshold;
	}

	params.enableSceneMaterials = renderParams.enableSceneMaterials;

	// We don't provide the following because task controller ignores them:
	// - params.camera
	// - params.viewport

	return params;
    }

    VtValue mySelection;
};

class HUSD_Imaging::husd_ImagingPrivate
{
public:
    UT_SharedPtr<HUSD_ImagingEngine>	 myImagingEngine;
    UT_TaskGroup			 myUpdateTask;
    UsdImagingGLRenderParams		 myRenderParams;
    UsdImagingGLRenderParams		 myLastRenderParams;
    std::map<TfToken, VtValue>           myCurrentRenderSettings;
    std::map<TfToken, VtValue>           myCurrentCameraSettings;
    std::string				 myRootLayerIdentifier;
    HdRenderSettingsMap                  myPrimRenderSettingMap;
};

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

HUSD_Imaging::HUSD_Imaging()
    : myPrivate(new husd_ImagingPrivate),
      myDataHandle(HUSD_FOR_MIRRORING),
      myRenderSettings(nullptr),
      myRenderSettingsContext(nullptr),
      myDepthStyle(HUSD_DEPTH_OPENGL)
{
    myPrivate->myRenderParams.showProxy = true;
    myPrivate->myRenderParams.showGuides = true;
    myPrivate->myRenderParams.showRender = true;
    myPrivate->myRenderParams.highlight = true;
    myPrivate->myLastRenderParams = myPrivate->myRenderParams;

    myWantsHeadlight = false;
    myHasHeadlight = false;
    myDoLighting = true;
    myHasGeomPrims = false;
    myHasLightCamPrims = false;
    mySelectionNeedsUpdate = false;
    myConverged = true;
    mySettingsChanged = true;
    myIsPaused = false;
    myValidRenderSettings = false;
    myCameraSynced = true;
    myConformPolicy = HUSD_Scene::EXPAND_APERTURE;
    myFrame = -1e30;
    myScene = nullptr;
    myCompositor = nullptr;
    myOutputPlane = HdAovTokens->color.GetText();
    myRenderSettings = new XUSD_RenderSettings();
    myRenderSettingsContext = new husd_DefaultRenderSettingContext;
}

HUSD_Imaging::~HUSD_Imaging()
{
    UT_Lock::Scope	lock(theActiveRenderLock);
    theActiveRenders.erase(this);

    delete myRenderSettingsContext;
    delete myRenderSettings; 

    if (running() && UT_Exit::isExiting())
    {
	// We're currently running an update.  If we delete our private data,
	// this will cause the delegate to be deleted, causing all sorts of
	// problems while we Sync().  So, in this case, since we're exiting, we
	// can just let the unique pointer float (and not be cleaned up here)
	myPrivate.release();
    }
}

bool
HUSD_Imaging::running() const
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());

    return (status != RUNNING_UPDATE_NOT_STARTED);
}

bool
HUSD_Imaging::isComplete() const
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
        myPrivate->myImagingEngine.reset();
    }
    else if(myPrivate && myPrivate->myImagingEngine)
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        myPrivate->myImagingEngine->DispatchRender(
            stage->GetPseudoRoot(),
            myPrivate->myRenderParams);
    }
}

void
HUSD_Imaging::setDrawMode(DrawMode mode)
{
    UsdImagingGLDrawMode	 usdmode;

    switch(mode)
    {
    case DRAW_WIRE:
	usdmode = UsdImagingGLDrawMode::DRAW_WIREFRAME;
	break;
    case DRAW_SHADED_NO_LIGHTING:
	usdmode = UsdImagingGLDrawMode::DRAW_GEOM_ONLY;
	break;
    case DRAW_SHADED_FLAT:
	usdmode = UsdImagingGLDrawMode::DRAW_SHADED_FLAT;
	break;
    case DRAW_SHADED_SMOOTH:
	usdmode = UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
	break;
    case DRAW_WIRE_SHADED_SMOOTH:
	usdmode = UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE;
	break;
    default:
	UT_ASSERT(!"Unhandled draw mode");
	usdmode = UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
	break;
    }
    myPrivate->myRenderParams.drawMode = usdmode;
}

void
HUSD_Imaging::showPurposeRender(bool enable)
{
    myPrivate->myRenderParams.showRender = enable;
}

void
HUSD_Imaging::showPurposeProxy(bool enable)
{
    myPrivate->myRenderParams.showProxy = enable;
}

void
HUSD_Imaging::showPurposeGuide(bool enable)
{
    myPrivate->myRenderParams.showGuides = enable;
}

void
HUSD_Imaging::setDrawComplexity(float complexity)
{
    myPrivate->myRenderParams.complexity = complexity;
}

void
HUSD_Imaging::setBackfaceCull(bool bf)
{
    auto style = bf ? UsdImagingGLCullStyle::CULL_STYLE_BACK
		    : UsdImagingGLCullStyle::CULL_STYLE_NOTHING;
    myPrivate->myRenderParams.cullStyle = style;
}

void
HUSD_Imaging::setScene(HUSD_Scene *scene)
{
    myScene = scene;
}

void
HUSD_Imaging::setStage(const HUSD_DataHandle &data_handle,
		       const HUSD_ConstOverridesPtr &overrides)
{
    myDataHandle = data_handle;
    myOverrides = overrides;
    myHasGeomPrims = false;
    myHasLightCamPrims = false;
}

void
HUSD_Imaging::setSelection(const UT_StringArray &paths)
{
    mySelection = paths;
    mySelectionNeedsUpdate = true;
}

bool
HUSD_Imaging::setFrame(fpreal frame)
{
    if (frame != myFrame)
    {
	myFrame = frame;
	myPrivate->myRenderParams.frame = frame;
	mySettingsChanged = true;

	// Likely need to redo these guides.
	myHasGeomPrims = false;
	myHasLightCamPrims = false;

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

// Start of anonymous namespace
namespace
{
    static bool
    isSupported(const TfToken &id)
    {
	auto			&reg = HdRendererPluginRegistry::GetInstance();
	HdRendererPlugin	*plugin = reg.GetRendererPlugin(id);
	bool			 supported = false;

	if (plugin)
	{
	    supported = plugin->IsSupported();
	    reg.ReleasePlugin(plugin);
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
                            const UT_Options *render_opts)
{
    UT_StringHolder	 new_renderer_name = renderer_name;

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
	    theRendererInfoMap.erase(new_renderer_name);
            myPrivate->myImagingEngine.reset();
            myRendererName.clear();
            if(myScene)
                HUSD_Scene::popScene(myScene);

            return false;
	}

        myRendererName = new_renderer_name;
        myPrivate->myImagingEngine.reset();
        if(myScene)
            myScene->clearRenderIDs();
    }

    if (myDataHandle.rootLayerIdentifier() != myPrivate->myRootLayerIdentifier)
    {
	myPrivate->myImagingEngine.reset();
	myPrivate->myRootLayerIdentifier = myDataHandle.rootLayerIdentifier();
	mySelectionNeedsUpdate = true;
    }

    // Check for restart settings changes even if the imaging engine is
    // already null, because this method also initializes the camera settings
    // map with the current values.
    if (updateRestartCameraSettings() ||
        (myPrivate->myImagingEngine && anyRestartRenderSettingsChanged()))
	myPrivate->myImagingEngine.reset();

    bool do_lighting = false;
    auto &&draw_mode = myPrivate->myRenderParams.drawMode;
    if(draw_mode == UsdImagingGLDrawMode::DRAW_SHADED_FLAT ||
       draw_mode == UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH ||
       draw_mode == UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE)
	do_lighting = myDoLighting;
    myPrivate->myRenderParams.enableLighting = do_lighting;
    // Setting this value to true causes the "automatic" Alpha Threshold
    // setting to be set to 0.1 instead of 0.5 (which is the value used if
    // this flag is left at its default value of false).
    myPrivate->myRenderParams.enableSampleAlphaToCoverage = true;

    // Create myImagingEngine inside a render call.  Otherwise
    // we can't initialize glew, so USD won't detect it is
    // running in a GL4 context, so it will use the terrible
    // reference renderer.
    if (!myPrivate->myImagingEngine)
    {
        bool drawmode = theRendererInfoMap[myRendererName].drawModeSupport();

	myPrivate->myImagingEngine.reset(
	    new HUSD_ImagingEngine());
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
            myPrivate->myImagingEngine.reset();
            myRendererName.clear();
            return false;
        }

        myPrivate->myCurrentRenderSettings.clear();
        myPrivate->myImagingEngine->SetUsdDrawModesEnabled(drawmode);
        myPrivate->myRenderParams.enableUsdDrawModes = drawmode;
        myPrivate->myImagingEngine->SetDisplayUnloadedPrimsWithBounds(drawmode);

	// Currently, we don't use HdAovTokens->primId, which
	// would be HdAovDescriptor(HdFormatInt32, false, VtValue(0)),

        // Update the render delegate's render settings before setting up
        // the AOVs.
        HUSD_AutoReadLock    lock(myDataHandle, myOverrides);
        updateSettingsIfRequired(lock);
    }

    myPlaneList.clear();
    bool has_aov = false;

    TfTokenVector list;
    bool aovs_specified = false;
    
    // TODO: use cameraDepth when linear is requested
    // const TfToken &depth_token = (myDepthStyle == HUSD_DEPTH_LINEAR)
    //                            ? HdAovTokens->cameraDepth : HdAovTokens->depth;
    const TfToken &depth_token = HdAovTokens->depth;
    if(myValidRenderSettings)
    {
        bool has_depth = false;
        HdAovDescriptorList descs;
        myRenderSettings->collectAovs(list, descs);

        if(list.size())
        {
            for(auto &t : list)
            {
                if(t == depth_token)
                    has_depth = true;

                myPlaneList.append( t.GetText());
                if(myOutputPlane.isstring() &&
                   myOutputPlane == myPlaneList.last())
                {
                    has_aov = true;
                    myCurrentAOV = myOutputPlane;
                }
            }
            if(!has_depth)
                list.push_back(depth_token);

            aovs_specified = true;
        }
    }
    if(!aovs_specified)
    {
        list.push_back(HdAovTokens->color);
        list.push_back(depth_token);

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
        myValidRenderSettings)
    {
        for(auto &aov_name  : list)
        {
            HdAovDescriptor aov_desc;
            if(myRenderSettingsContext->getAovDescriptor(aov_name, aov_desc))
                myPrivate->myImagingEngine->SetRenderOutputSettings(aov_name,
                                                                    aov_desc);
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
    
    if (myValidRenderSettings &&
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
        HUSD_AutoReadLock lock(myDataHandle, myOverrides);
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

        if(myCurrentOptions.getNumOptions() > 0)
        {
            for(auto opt = myCurrentOptions.begin();
                opt != myCurrentOptions.end(); ++opt)
            {
                if(myValidRenderSettings)
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
        }

        if(myValidRenderSettings)
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
    }
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

        if(myCurrentOptions.getNumOptions() > 0)
        {
            for(auto opt = myCurrentOptions.begin();
                opt != myCurrentOptions.end(); ++opt)
            {
                if(myValidRenderSettings)
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
        }

        if(myValidRenderSettings)
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
                }
            }
        }
    }
}

HUSD_Imaging::RunningStatus
HUSD_Imaging::updateRenderData(const UT_Matrix4D &view_matrix,
                               const UT_Matrix4D &proj_matrix,
                               const UT_DimRect  &viewport_rect,
                               bool               update_deferred,
                               bool               use_camera)
{
    myReadLock.reset(new HUSD_AutoReadLock(myDataHandle, myOverrides));
    if (myReadLock->data() && myReadLock->data()->isStageValid())
    {
	if (myReadLock->data()->stage()->GetPseudoRoot())
	{
	    GlfSimpleLightVector	 lights;
            auto &&engine = myPrivate->myImagingEngine;

	    if (myPrivate->myRenderParams.enableLighting)
	    {
		myHasHeadlight = false;
		if(myWantsHeadlight)
		{
		    GlfSimpleLight	 light;

		    light.SetIsCameraSpaceLight(true);
		    light.SetDiffuse(GfVec4f(0.8, 0.8, 0.8, 1.0));
		    lights.push_back(light);
		    myHasHeadlight = true;
		}
	    }

	    engine->SetLightingState(lights,
                                     GlfSimpleMaterial(),
                                     GfVec4f(0.0, 0.0, 0.0, 0.0));

	    UT_Vector4D ut_viewport;

	    ut_viewport.assign(viewport_rect.x(),
                               viewport_rect.y(),
                               viewport_rect.w(),
                               viewport_rect.h());

            // UTdebugPrint("\n\n\n\n****************************\nSet Window",
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
                engine->SetSamplingCamera(campath);
                engine->SetWindowPolicy(
                    (CameraUtilConformWindowPolicy)myConformPolicy);
                
                myRenderSettingsContext->setCamera(campath);
            }
            else
            {
                engine->SetCameraState(gf_view_matrix, gf_proj_matrix);
                myRenderSettingsContext->setCamera(campath);
            }
                
            
	    if(update_deferred && myScene)
	         updateDeferredPrims();
            updateSettingsIfRequired(*myReadLock);

	    engine->DispatchRender(
		myReadLock->data()->stage()->GetPseudoRoot(),
		myPrivate->myRenderParams);

            // Other renderers need to return to executing on
            // the main thread now. This is where the actual
            // GL calls happen.
            return RUNNING_UPDATE_COMPLETE;
	}
    }
    
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
    }
    return BUFFER_NONE;
}

void
HUSD_Imaging::setPostRenderCallback(const PostRenderCallback &cb)
{
    myPostRenderCallback = cb;
}

bool
HUSD_Imaging::getUsingCoreProfile()
{
    return GlfContextCaps::GetInstance().coreProfile;
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
        myPrivate->myImagingEngine->CompleteRender(myPrivate->myRenderParams);
	if (myPostRenderCallback)
	    myPostRenderCallback();
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
#if UT_ASSERT_LEVEL > 0
#if 0
    pref = true;
#endif
#endif
    UT_StringHolder rname = renderer.isstring() ? renderer : myRendererName;
    
    // myRendererName should either be something in our map, or the empty
    // string.
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
                                     bool update_deferred,
                                     bool use_cam)
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());
    
    // An empty renderer name means clear out our imaging data and exit.
    if (!renderer.isstring())
    {
        waitForUpdateToComplete();
	myPrivate->myImagingEngine.reset();
	return false;
    }

    if(status != RUNNING_UPDATE_NOT_STARTED)
    {
        return false;
    }

    // If we aren't running in the background, we are free to start a new
    // update/redraw sequence.
    if(!setupRenderer(renderer, render_opts))
    {
        return false;
    }

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
                                     proj_matrix, viewport_rect, update_deferred,
                                     use_cam]()
		{
		    UT_PerfMonAutoViewportDrawEvent perfevent("LOP Viewer",
			"Background Update USD Stage", UT_PERFMON_3D_VIEWPORT);

		    RunningStatus status
			= updateRenderData(view_matrix, proj_matrix, 
                                           viewport_rect, update_deferred,
                                           use_cam);

		    if (status == RUNNING_UPDATE_NOT_STARTED ||
			status == RUNNING_UPDATE_FATAL)
			myReadLock.reset();
		    myRunningInBackground.store(status);
		 });
    }
    else
    {
	status = updateRenderData(view_matrix, proj_matrix, viewport_rect,
				  update_deferred, use_cam);

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
}

bool
HUSD_Imaging::checkRender(bool do_render)
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());

    if(status == RUNNING_UPDATE_FATAL)
    {
        // Serious error, or updating to a completely empty stage.
        // Delete our render delegate and free our stage.
        myPrivate->myImagingEngine.reset();
	myReadLock.reset();
        myRunningInBackground.store(RUNNING_UPDATE_NOT_STARTED);
        return true;
    }

    if (status == RUNNING_UPDATE_COMPLETE)
    {
	myReadLock.reset();
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
                     bool                update_deferred,
                     bool                use_cam)
{
    // An empty renderer name means clear out our imaging data and exit.
    if (!renderer_name.isstring())
    {
        waitForUpdateToComplete();
	myPrivate->myImagingEngine.reset();
	return false;
    }

    // UTdebugPrint("RENDER & WAIT");
    if(!setupRenderer(renderer_name, render_opts))
        return false;
    
    // Run the update in the foreground. We never enter any running
    // in background status other than "not started".
    RunningStatus status = 
        updateRenderData(view_matrix, proj_matrix, viewport_rect,
                         update_deferred, use_cam);

    if(status == RUNNING_UPDATE_FATAL)
    {
        // Serious error, or updating to a completely empty stage.
        // Delete our render delegate.
	myPrivate->myImagingEngine.reset();
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
    shown[HUSD_HydraPrim::TagRender]  = myPrivate->myRenderParams.showRender;
    shown[HUSD_HydraPrim::TagProxy]   = myPrivate->myRenderParams.showProxy;
    shown[HUSD_HydraPrim::TagGuide]   = myPrivate->myRenderParams.showGuides;
    shown[HUSD_HydraPrim::TagInvisible] = false;

    for( auto it : myScene->geometry())
    {
	if(it.second->deferredBits()!= 0)
	{
            if(!shown[it.second->renderTag()])
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
    HUSD_AutoReadLock    lock(myDataHandle, myOverrides);

    if (lock.data() && lock.data()->isStageValid())
    {
	auto		 prim = lock.data()->stage()->GetPseudoRoot();
	UsdTimeCode	 t = myPrivate->myRenderParams.frame;
	TfTokenVector	 purposes;

	purposes.push_back(UsdGeomTokens->default_);
	purposes.push_back(UsdGeomTokens->proxy);
	purposes.push_back(UsdGeomTokens->render);
	if (prim)
	{
	    UsdGeomBBoxCache	 bboxcache(t, purposes);
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

bool
HUSD_Imaging::getAvailableRenderers(HUSD_RendererInfoMap &info_map)
{
    // The list of available renderers shouldn't change, so just generate the
    // list once, and remember it.
    if (theRendererInfoMap.size() == 0)
    {
	HfPluginDescVector	 plugins;

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

    info_map = theRendererInfoMap;

    return (info_map.size() > 0);
}

bool
HUSD_Imaging::canPause() const
{
    if( myPrivate->myImagingEngine)
        return myPrivate->myImagingEngine->IsPauseRendererSupported();

    return false;
}

bool
HUSD_Imaging::pauseRender()
{
    if( canPause() && !myIsPaused)
    {
        myIsPaused = true;
        myPrivate->myImagingEngine->PauseRenderer();
    }

    return myIsPaused;
}

void
HUSD_Imaging::resumeRender()
{
    if( canPause() && myIsPaused)
    {
        myIsPaused = false;
        myPrivate->myImagingEngine->ResumeRenderer();
    }
}

    
bool
HUSD_Imaging::isPaused() const
{
    return myIsPaused;
}

void
HUSD_Imaging::getRenderStats(UT_Options &opts)
{
    if(!myPrivate || !myPrivate->myImagingEngine)
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
    HUSD_AutoReadLock lock(myDataHandle, myOverrides);

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
        if(myRenderSettings->init(lock.data()->stage(), path,
                                  *myRenderSettingsContext))
        {
            myRenderSettings->resolveProducts(lock.data()->stage(),
                                              *myRenderSettingsContext);
            
            HdAovDescriptorList descs;
            TfTokenVector aov_names;
            
            if(myRenderSettings->collectAovs(aov_names, descs))
                myRenderSettingsContext->setAOVs(aov_names, descs);

            myPrivate->myPrimRenderSettingMap =
                myRenderSettings->renderSettings();

            mySettingsChanged = true;
            myValidRenderSettings = true;
            valid = true;
        }
    }

    if(!valid)
    {
        if(myValidRenderSettings)
            mySettingsChanged = true;
        myValidRenderSettings = false;
    }
}

UT_StringHolder
HUSD_Imaging::lookupID(int path_id, int inst_id, bool pick_instance) const
{
    UT_StringHolder path;

    if(myPrivate->myImagingEngine)
    {
        unsigned char path_id_char[sizeof(int)];
        unsigned char inst_id_char[sizeof(int)];
        SdfPath primpath;
        SdfPath instpath;
        int instindex;

        memcpy(path_id_char, &path_id, sizeof(int));
        memcpy(inst_id_char, &inst_id, sizeof(int));
        myPrivate->myImagingEngine->DecodeIntersection(
            path_id_char, inst_id_char,
            &primpath, &instpath, &instindex);

        if (!instpath.IsEmpty())
        {
            path = instpath.GetText();
            if(pick_instance)
            {
                UT_WorkBuffer index_string;
                index_string.sprintf("[%d]", instindex);
                path += UT_StringRef(index_string.buffer());
            }
        }
        else
            path = primpath.GetText();
    }

    return path;
}
