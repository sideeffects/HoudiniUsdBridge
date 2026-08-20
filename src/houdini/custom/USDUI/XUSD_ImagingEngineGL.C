//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "pxr/imaging/garch/glApi.h"

#include "XUSD_ImagingEngineGL.h"
#include "RE_Wrapper.h"
#include <HUSD/HUSD_Constants.h>
#include <UT/UT_WorkBuffer.h>

#include <pxr/usdImaging/usdImaging/delegate.h>
#include "pxr/usdImaging/usdImaging/drawModeSceneIndex.h"
#include "pxr/usdImaging/usdImaging/flattenedDataSourceProviders.h"
#include "pxr/usdImaging/usdImaging/selectionSceneIndex.h"
#include "pxr/usdImaging/usdImaging/stageSceneIndex.h"
#include "pxr/usdImaging/usdImaging/niPrototypePropagatingSceneIndex.h"
#include "pxr/usdImaging/usdImaging/piPrototypePropagatingSceneIndex.h"
#include "pxr/usdImaging/usdImaging/renderSettingsFlatteningSceneIndex.h"
#include "pxr/imaging/hd/flatteningSceneIndex.h"

#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/camera.h>

#include <pxr/imaging/glf/diagnostic.h>
#include <pxr/imaging/glf/contextCaps.h>
#include <pxr/imaging/glf/glContext.h>
#include <pxr/imaging/glf/info.h>

#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hdx/taskController.h>
#include <pxr/imaging/hdx/tokens.h>

#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/texture.h>
#include <pxr/imaging/hgi/tokens.h>

#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/stl.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3d.h>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

class NullHgi : public Hgi
{
public:
    NullHgi()
    { }
    ~NullHgi() override
    { }

    bool IsBackendSupported() const override
    { return true; }

    HgiGraphicsCmdsUniquePtr CreateGraphicsCmds(
        HgiGraphicsCmdsDesc const& desc) override
    { return HgiGraphicsCmdsUniquePtr(); }

    HgiBlitCmdsUniquePtr CreateBlitCmds() override
    { return HgiBlitCmdsUniquePtr(); }

    HgiComputeCmdsUniquePtr CreateComputeCmds(
        HgiComputeCmdsDesc const& desc) override
    { return HgiComputeCmdsUniquePtr(); }

    HgiTextureHandle CreateTexture(HgiTextureDesc const & desc) override
    { return HgiTextureHandle(); }

    void DestroyTexture(HgiTextureHandle* texHandle) override
    { }

    HgiTextureViewHandle CreateTextureView(
        HgiTextureViewDesc const & desc) override
    { return HgiTextureViewHandle(); }

    void DestroyTextureView(HgiTextureViewHandle* viewHandle) override
    { }

    HgiSamplerHandle CreateSampler(HgiSamplerDesc const & desc) override
    { return HgiSamplerHandle(); }

    void DestroySampler(HgiSamplerHandle* smpHandle) override
    { }

    HgiBufferHandle CreateBuffer(HgiBufferDesc const & desc) override
    { return HgiBufferHandle(); }

    void DestroyBuffer(HgiBufferHandle* bufHandle) override
    { }

    HgiShaderFunctionHandle CreateShaderFunction(
        HgiShaderFunctionDesc const& desc) override
    { return HgiShaderFunctionHandle(); }

    void DestroyShaderFunction(
        HgiShaderFunctionHandle* shaderFunctionHandle) override
    { }

    HgiShaderProgramHandle CreateShaderProgram(
        HgiShaderProgramDesc const& desc) override
    { return HgiShaderProgramHandle(); }

    void DestroyShaderProgram(
        HgiShaderProgramHandle* shaderProgramHandle) override
    { }

    HgiResourceBindingsHandle CreateResourceBindings(
        HgiResourceBindingsDesc const& desc) override
    { return HgiResourceBindingsHandle(); }

    void DestroyResourceBindings(
        HgiResourceBindingsHandle* resHandle) override
    { }

    HgiGraphicsPipelineHandle CreateGraphicsPipeline(
        HgiGraphicsPipelineDesc const& pipeDesc) override
    { return HgiGraphicsPipelineHandle(); }

    void DestroyGraphicsPipeline(
        HgiGraphicsPipelineHandle* pipeHandle) override
    { }

    HgiComputePipelineHandle CreateComputePipeline(
        HgiComputePipelineDesc const& pipeDesc) override
    { return HgiComputePipelineHandle(); }

    void DestroyComputePipeline(HgiComputePipelineHandle* pipeHandle) override
    { }

    TfToken const& GetAPIName() const override
    { static TfToken theAPIName("Null"); return theAPIName; }

    HgiCapabilities const* GetCapabilities() const override
    { return nullptr; }

    HgiIndirectCommandEncoder* GetIndirectCommandEncoder() const override
    { return nullptr; }

    void StartFrame() override
    { }

    void EndFrame() override
    { }
};

void
_InitGL()
{
    static std::once_flag initFlag;

    std::call_once(initFlag, []{

        // Initialize Glew library for GL Extensions if needed
        GarchGLApiLoad();

        // Initialize if needed and switch to shared GL context.
        GlfSharedGLContextScopeHolder sharedContext;

        // Initialize GL context caps based on shared context
        GlfContextCaps::InitInstance();

    });
}

UsdImagingGLDrawMode
_ConvertDrawModeEnum(XUSD_ImagingRenderParams::XUSD_ImagingDrawMode drawmode)
{
    switch (drawmode)
    {
        case XUSD_ImagingRenderParams::DRAW_WIREFRAME:
            return UsdImagingGLDrawMode::DRAW_WIREFRAME;
        case XUSD_ImagingRenderParams::DRAW_GEOM_ONLY:
            return UsdImagingGLDrawMode::DRAW_GEOM_ONLY;
        case XUSD_ImagingRenderParams::DRAW_SHADED_FLAT:
            return UsdImagingGLDrawMode::DRAW_SHADED_FLAT;
        case XUSD_ImagingRenderParams::DRAW_SHADED_SMOOTH:
            return UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
        case XUSD_ImagingRenderParams::DRAW_WIREFRAME_ON_SURFACE:
            return UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE;
    };

    return UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
}

UsdImagingGLCullStyle
_ConvertCullStyleEnum(XUSD_ImagingRenderParams::XUSD_ImagingCullStyle cullstyle)
{
    switch (cullstyle)
    {
        case XUSD_ImagingRenderParams::CULL_STYLE_NOTHING:
            return UsdImagingGLCullStyle::CULL_STYLE_NOTHING;
        case XUSD_ImagingRenderParams::CULL_STYLE_BACK:
            return UsdImagingGLCullStyle::CULL_STYLE_BACK;
    };

    return UsdImagingGLCullStyle::CULL_STYLE_NOTHING;
}

void
_CopyRenderParams(const XUSD_ImagingRenderParams &src,
        UsdImagingGLRenderParams &dest)
{
    dest.frame = src.myFrame;
    dest.complexity = src.myComplexity;
    dest.drawMode = _ConvertDrawModeEnum(src.myDrawMode);
    dest.cullStyle = _ConvertCullStyleEnum(src.myCullStyle);
    dest.showProxy = src.myShowProxy;
    dest.showGuides = src.myShowGuides;
    dest.showRender = src.myShowRender;
    dest.highlight = src.myHighlight;
    dest.enableUsdDrawModes = src.myEnableUsdDrawModes;
    dest.enableLighting = src.myEnableLighting;
    dest.enableSceneLights = src.myEnableSceneLights;
    dest.enableSceneMaterials = src.myEnableSceneMaterials;
    dest.enableSampleAlphaToCoverage = src.myEnableSampleAlphaToCoverage;

}

} // anonymous namespace

//----------------------------------------------------------------------------
// Construction
//----------------------------------------------------------------------------

XUSD_ImagingEngineGL::XUSD_ImagingEngineGL(const TfToken& rendererPluginId,
        bool force_null_hgi, bool use_scene_indices)
    : _hgi()
    , _hgiDriver()
    , _domeLightCameraVisibility(true)
    , _rootPath(SdfPath::AbsoluteRootPath())
    , _excludedPrimPaths()
    , _invisedPrimPaths()
    , _displayUnloaded(true)
    , _enableUsdDrawModes(true)
    , _useSceneIndices(use_scene_indices)
    , _sceneDelegateId(SdfPath::AbsoluteRootPath())

{
    RE_Wrapper wrapper(true);

    // Hgi must be initialized before the renderer plugin is set.
    _InitializeHgiIfNecessary(force_null_hgi);

    // _renderIndex, _taskController, and _sceneDelegate are initialized
    // by the plugin system.
    if (!SetRendererPlugin(!rendererPluginId.IsEmpty() ?
            rendererPluginId : _GetDefaultRendererPluginId())) {
        TF_CODING_ERROR("No renderer plugins found! "
                        "Check before creation.");
    }

    // GL must be initialized after the renderer plugin has been set, because
    // the renderer plugin updates the GlfGLContextRegistry.
    if (wrapper.isOpenGLAvailable())
        _InitGL();
}

bool
XUSD_ImagingEngineGL::isUsingGLCoreProfile() const
{
    return GlfContextCaps::GetInstance().coreProfile;
}

void
XUSD_ImagingEngineGL::_DestroyHydraObjects()
{
    // Destroy objects in opposite order of construction.
    _engine = nullptr;
    _taskController = nullptr;
    if (_GetUseSceneIndices()) {
        if (_renderIndex && _sceneIndex) {
            _renderIndex->RemoveSceneIndex(_sceneIndex);
            _stageSceneIndex = nullptr;
            _sceneIndex = nullptr;
        }
    } else {
        _sceneDelegate = nullptr;
    }
    _renderIndex = nullptr;
    _renderDelegate = nullptr;
}

XUSD_ImagingEngineGL::~XUSD_ImagingEngineGL()
{
    RE_Wrapper wrapper(true);

    TF_PY_ALLOW_THREADS_IN_SCOPE();

    _DestroyHydraObjects();
}

//----------------------------------------------------------------------------
// Rendering
//----------------------------------------------------------------------------

void
XUSD_ImagingEngineGL::_PrepareBatch(
    const UsdPrim& root,
    const UsdImagingGLRenderParams& params)
{
    HD_TRACE_FUNCTION();

    if (_CanPrepare(root)) {
        if (!_isPopulated) {
            if (_GetUseSceneIndices()) {
                TF_VERIFY(_stageSceneIndex);
                _stageSceneIndex->SetStage(root.GetStage());

                // XXX(USD-7113): Add pruning based on _rootPath,
                // _excludedPrimPaths

                // XXX(USD-7114): Add draw mode support based on
                // params.enableUsdDrawModes.

                // XXX(USD-7115): Add invis overrides from _invisedPrimPaths.
            } else {
                TF_VERIFY(_sceneDelegate);
                _sceneDelegate->SetUsdDrawModesEnabled(
                        params.enableUsdDrawModes);
                _sceneDelegate->Populate(
                        root.GetStage()->GetPrimAtPath(_rootPath),
                        _excludedPrimPaths);
                _sceneDelegate->SetInvisedPrimPaths(_invisedPrimPaths);
            }
            _isPopulated = true;
        }

        _PreSetTime(params);
        // SetTime will only react if time actually changes.
        if (_GetUseSceneIndices()) {
            _stageSceneIndex->SetTime(params.frame);
        } else {
            _sceneDelegate->SetTime(params.frame);
        }
        _PostSetTime(params);
    }
}

void
XUSD_ImagingEngineGL::_PrepareRender(
        const UsdImagingGLRenderParams& params)
{
    TF_VERIFY(_taskController);

    _taskController->SetFreeCameraClipPlanes(params.clipPlanes);

    TfTokenVector renderTags;
    _ComputeRenderTags(params, &renderTags);
    _taskController->SetRenderTags(renderTags);

    _taskController->SetRenderParams(
        _MakeHydraUsdImagingGLRenderParams(params));

    // Forward scene materials enable option to delegate
    if (_GetUseSceneIndices()) {
        // XXX(USD-7116): params.enableSceneMaterials, params.enableSceneLights
    } else {
        _sceneDelegate->SetSceneMaterialsEnabled(params.enableSceneMaterials);
        _sceneDelegate->SetSceneLightsEnabled(params.enableSceneLights);
    }
}

void
XUSD_ImagingEngineGL::_UpdateDomeLightCameraVisibility()
{
    // Check to see if the dome light camera visibility has changed, and mark
    // the dome light prim as dirty if it has.
    //
    // Note: The dome light camera visibility setting is handled via the
    // HdRenderSettingsMap on the HdRenderDelegate because this ensures all
    // backends can access this setting when they need to.

    // The absence of a setting in the map is the same as camera visibility
    // being on.
    const bool domeLightCamVisSetting = _renderDelegate->
        GetRenderSetting<bool>(
            HdRenderSettingsTokens->domeLightCameraVisibility,
            true);
    if (_domeLightCameraVisibility != domeLightCamVisSetting) {
        // Camera visibility state changed, so we need to mark any dome lights
        // as dirty to ensure they have the proper state on all backends.
        _domeLightCameraVisibility = domeLightCamVisSetting;

        SdfPathVector domeLights = _renderIndex->GetSprimSubtree(
            HdPrimTypeTokens->domeLight, SdfPath::AbsoluteRootPath());
        for (SdfPathVector::iterator domeLightIt = domeLights.begin();
                                     domeLightIt != domeLights.end();
                                     ++domeLightIt) {
            _renderIndex->GetChangeTracker().MarkSprimDirty(
                *domeLightIt, HdLight::DirtyParams);
        }
    }
}

void 
XUSD_ImagingEngineGL::DispatchRender(
        const UsdPrim& root,
        const XUSD_ImagingRenderParams &params)
{
    if (ARCH_UNLIKELY(!_renderDelegate)) {
        return;
    }

    // Set the lighting state if we have any fake lights. This coverse the
    // case where the camera view has changed, but the number and kind of
    // fake lights has not changed. We still need to call SetLightingState
    // to update the headlight transform (if there is a headlight).
    if (_lightingContextForOpenGLState &&
        !_lightingContextForOpenGLState->GetLights().empty())
        _taskController->SetLightingState(_lightingContextForOpenGLState);

    UsdImagingGLRenderParams imagingGLRenderParams;
    _CopyRenderParams(params, imagingGLRenderParams);
    _PrepareBatch(root, imagingGLRenderParams);

    // XXX(UsdImagingPaths): Is it correct to map USD root path directly
    // to the cachePath here?
    SdfPath cachePath = root.GetPath();
    SdfPathVector paths = {
        root.GetPath().ReplacePrefix(
            SdfPath::AbsoluteRootPath(), _sceneDelegateId)
    };

    _UpdateHydraCollection(&_renderCollection, paths, imagingGLRenderParams);
    _taskController->SetCollection(_renderCollection);

    _PrepareRender(imagingGLRenderParams);

    // This chunk of code comes from HdEngine::Execute, which is called
    // from _Execute. The _Execute call was moved to a separate
    // CompleteRender method, but the SyncAll is the most expensive part
    // generally, and can be done early. When it gets called again as
    // part of _Execute, it will essentially be a no-op because the
    // Sync is complete and the task context will be unchanged.
    HdTaskContext taskContext;
    // Like in HdEngine::Execute, make the Hgi drivers accessible to any
    // HdTasks that are about to get Synced. HdxSkydomeTask, for example,
    // Needs to set its Hgi pointer on its first Sync call so it doesn't
    // crash when calling its Execute method (from CompleteRender, via
    // HdEngine::Execute).
    taskContext[HdTokens->drivers] = VtValue(_renderIndex->GetDrivers());
    _taskController->SetEnableSelection(imagingGLRenderParams.highlight);
    _UpdateDomeLightCameraVisibility();

    // Add this call to SyncAll, which actually comes from the
    // HdEngine::Execute method. But we want to rearrange the ordering of
    // these calls a bit to keep all the expensive stuff in this one
    // method. The _Render call will also call SyncAll, but because we're
    // calling it here right before that subsequent call, it will basically
    // be a no-op the second time.
    auto tasks = _taskController->GetRenderingTasks();
    _renderIndex->SyncAll(&tasks, &taskContext);
}

void 
XUSD_ImagingEngineGL::CompleteRender(
    const XUSD_ImagingRenderParams &params,
    bool renderer_uses_gl)
{
    UsdImagingGLRenderParams imagingGLRenderParams;
    _CopyRenderParams(params, imagingGLRenderParams);
    _Execute(imagingGLRenderParams,
        _taskController->GetRenderingTasks(),
        renderer_uses_gl);
}

bool
XUSD_ImagingEngineGL::IsConverged() const
{
    return _taskController->IsConverged();
}

HdRenderBuffer *
XUSD_ImagingEngineGL::GetRenderOutput(TfToken const &name)
{
    if (_taskController)
        return _taskController->GetRenderOutput(name);

    return nullptr;
}

bool
XUSD_ImagingEngineGL::GetRawResource(HdRenderBuffer *buffer,
        exint &id, exint &width, exint &height)
{
    if (buffer)
    {
        VtValue resource = buffer->GetResource(false);

        if (resource.CanCast<HgiTextureHandle>())
        {
            HgiTextureHandle t = resource.UncheckedGet<HgiTextureHandle>();
            HgiTextureDesc d = t->GetDescriptor();
            id = t->GetRawResource();
            width = d.dimensions[0];
            height = d.dimensions[1];

            return true;
        }

    }

    return false;
}

//----------------------------------------------------------------------------
// Camera and Light State
//----------------------------------------------------------------------------

void
XUSD_ImagingEngineGL::SetRenderViewport(GfVec4d const& viewport)
{
    _taskController->SetRenderViewport(viewport);
}

void
XUSD_ImagingEngineGL::SetWindowPolicy(CameraUtilConformWindowPolicy policy)
{
    // Note: Free cam uses SetCameraState, which expects the frustum to be
    // pre-adjusted for the viewport size.

    if (_GetUseSceneIndices()) {
        // XXX(USD-7115): window policy
    } else {
        // The usdImagingDelegate manages the window policy for scene cameras.
        _sceneDelegate->SetWindowPolicy(policy);
    }
}

void
XUSD_ImagingEngineGL::SetCameraPath(SdfPath const& id)
{
    _cameraPath = id;
    _taskController->SetCameraPath(id);

    // The camera that is set for viewing will also be used for
    // time sampling.
    // XXX(HYD-2304): motion blur shutter window.
    if (!_GetUseSceneIndices()) {
        _sceneDelegate->SetCameraForSampling(id);
    }
}

void 
XUSD_ImagingEngineGL::SetCameraState(const GfMatrix4d& viewMatrix,
                                   const GfMatrix4d& projectionMatrix)
{
    _taskController->SetFreeCameraMatrices(viewMatrix, projectionMatrix);
}

void
XUSD_ImagingEngineGL::SetLightingState(
    UT_Array<XUSD_GLSimpleLight> const &lights,
    GfVec4f const &sceneAmbient)
{
    GlfSimpleLightVector glflights;

    for (auto &&light : lights)
    {
        GlfSimpleLight glflight;
        glflight.SetIsDomeLight(light.myIsDomeLight);
        glflight.SetIsCameraSpaceLight(!light.myIsDomeLight);
        glflight.SetHasExtendedAttributes(true);
        glflight.SetExtendedIntensity(light.myIntensity);
        glflight.SetExtendedAngle(light.myAngle);
        glflight.SetExtendedColor(
            GfVec3f(light.myColor.x(), light.myColor.y(), light.myColor.z()));
        glflights.push_back(glflight);
    }

    // we still use _lightingContextForOpenGLState for convenience, but
    // set the values directly.
    if (!_lightingContextForOpenGLState)
        _lightingContextForOpenGLState = GlfSimpleLightingContext::New();
    _lightingContextForOpenGLState->SetLights(glflights);
    _lightingContextForOpenGLState->SetMaterial(GlfSimpleMaterial());
    _lightingContextForOpenGLState->SetSceneAmbient(sceneAmbient);
    _lightingContextForOpenGLState->SetUseLighting(lights.size() > 0);

    _taskController->SetLightingState(_lightingContextForOpenGLState);
}

//----------------------------------------------------------------------------
// Picking
//----------------------------------------------------------------------------

bool
XUSD_ImagingEngineGL::DecodeIntersections(
    UT_Array<HUSD_RenderKey> &inOutKeys,
    SdfPathVector &outHitPrimPaths,
    std::vector<HdInstancerContext> &outHitInstancerContexts)
{
    UsdImagingDelegate *sdptr = nullptr;
    std::map<SdfPath, UT_Array<HUSD_RenderKey> > instancerMap;
    UT_Array<HUSD_RenderKey> nonInstanceKeys;
    SdfPathVector nonInstancePaths;

    if (_GetUseSceneIndices()) {
        // XXX(HYD-2299): picking
        return false;
    }

    for (int keyidx = 0; keyidx < inOutKeys.size(); keyidx++)
    {
        SdfPath primPath = _sceneDelegate->GetRenderIndex().
            GetRprimPathFromPrimId(inOutKeys[keyidx].myPickId);
        if (!primPath.IsEmpty())
        {
            sdptr = _sceneDelegate.get();
            if (inOutKeys[keyidx].myInstId == -1)
            {
                nonInstanceKeys.append(inOutKeys[keyidx]);
                nonInstancePaths.push_back(
                    _sceneDelegate->ConvertIndexPathToCachePath(primPath));
            }
            else
                instancerMap[primPath].append(inOutKeys[keyidx]);
        }
    }
    if (!sdptr)
        return false;

    // Create output for all the non-instance prims.
    inOutKeys.swap(nonInstanceKeys);
    outHitPrimPaths = std::move(nonInstancePaths);
    outHitInstancerContexts.insert(outHitInstancerContexts.end(),
        outHitPrimPaths.size(), HdInstancerContext());

    // Add outputs for each instancer prim.
    for (auto it : instancerMap)
    {
        SdfPath primPath = it.first;
        SdfPath delegateId, instancerId;
        SdfPathVector primPaths;
        std::vector<HdInstancerContext> instancerContexts;
        std::vector<int> instanceIds;

        sdptr->GetRenderIndex().GetSceneDelegateAndInstancerIds(
            primPath, &delegateId, &instancerId);

        instanceIds.reserve(it.second.size());
        for (auto &&key : it.second)
            instanceIds.push_back(key.myInstId);
        primPaths = sdptr->GetScenePrimPaths(
            primPath, instanceIds, &instancerContexts);
        if (primPaths.size() > 0)
        {
            inOutKeys.concat(it.second);
            outHitPrimPaths.insert(outHitPrimPaths.end(),
                primPaths.begin(), primPaths.end());
            if (instancerContexts.size() > 0)
                outHitInstancerContexts.insert(outHitInstancerContexts.end(),
                    instancerContexts.begin(), instancerContexts.end());
            else
                outHitInstancerContexts.insert(outHitInstancerContexts.end(),
                    primPaths.size(), HdInstancerContext());
        }
    }

    return true;
}

//----------------------------------------------------------------------------
// Renderer Plugin Management
//----------------------------------------------------------------------------

TfToken
XUSD_ImagingEngineGL::GetCurrentRendererId() const
{
    return _renderDelegate.GetPluginId();
}

void
XUSD_ImagingEngineGL::_InitializeHgiIfNecessary(bool force_null_hgi)
{
// In pxr/imaging/hgi/hgi.cpp there is a hard-coded reference to "HgiMetal"
// when using Hgi::CreatePlatformDefaultHgi() on Mac, but we don't build
// support for Metal at present, so we need to ensure we don't make this call
// (and, instead, force usage of our NullHgi).
// We still need to use "HgiGL" on Windows & Linux, however, to support Storm.
#ifndef MBSD
    if (!force_null_hgi)
    {
        RE_Wrapper wrapper(true);
        if (wrapper.isOpenGLAvailable())
        {
            // If the client of XUSD_ImagingEngine does not provide a HdDriver,
            // we construct a default one that is owned by XUSD_ImagingEngine.
            // The cleanest pattern is for the client app to provide this since
            // you may have multiple XUSD_ImagingEngine in one app that ideally
            // all use the same HdDriver and Hgi to share GPU resources.
            if (_hgiDriver.driver.IsEmpty())
            {
                _hgi = Hgi::CreatePlatformDefaultHgi();
                _hgiDriver.name = HgiTokens->renderDriver;
                _hgiDriver.driver = VtValue(_hgi.get());
            }
        }
    }
#endif
    if (_hgiDriver.driver.IsEmpty())
    {
        _hgi = HgiUniquePtr(new NullHgi());
        _hgiDriver.name = HgiTokens->renderDriver;
        _hgiDriver.driver = VtValue(_hgi.get());
    }
}

bool
XUSD_ImagingEngineGL::SetRendererPlugin(TfToken const &id)
{
    HdRendererPluginRegistry &registry =
        HdRendererPluginRegistry::GetInstance();

    // Special case: id = TfToken() selects the first plugin in the list.
    const TfToken resolvedId =
        id.IsEmpty() ? registry.GetDefaultPluginId() : id;

    if (_renderDelegate && _renderDelegate.GetPluginId() == resolvedId) {
        return true;
    }

    TF_PY_ALLOW_THREADS_IN_SCOPE();

    HdPluginRenderDelegateUniqueHandle renderDelegate =
        registry.CreateRenderDelegate(resolvedId);
    if (!renderDelegate) {
        return false;
    }

    _SetRenderDelegateAndRestoreState(std::move(renderDelegate));

    return true;
}

void
XUSD_ImagingEngineGL::_SetRenderDelegateAndRestoreState(
    HdPluginRenderDelegateUniqueHandle &&renderDelegate)
{
    // Pull old scene/task controller state. Note that the scene index/delegate
    // may not have been created, if this is the first time through this
    // function, so we guard for null and use default values for xform/vis.
    GfMatrix4d rootTransform = GfMatrix4d(1.0);
    bool rootVisibility = true;

    if (_GetUseSceneIndices()) {
        // XXX(USD-7115): root transform, visibility...
    } else {
        if (_sceneDelegate) {
            rootTransform = _sceneDelegate->GetRootTransform();
            rootVisibility = _sceneDelegate->GetRootVisibility();
        }
    }

    // Rebuild the imaging stack
    _SetRenderDelegate(std::move(renderDelegate));

    // Reload saved state.
    if (_GetUseSceneIndices()) {
        // XXX(USD-7115): root transform, visibility...
    } else {
        _sceneDelegate->SetRootVisibility(rootVisibility);
        _sceneDelegate->SetRootTransform(rootTransform);
    }
}

SdfPath
XUSD_ImagingEngineGL::_ComputeControllerPath(
    const HdPluginRenderDelegateUniqueHandle &renderDelegate)
{
    const std::string pluginId =
        TfMakeValidIdentifier(renderDelegate.GetPluginId().GetText());
    const TfToken rendererName(
        TfStringPrintf("_UsdImaging_%s_%p", pluginId.c_str(), this));

    return _sceneDelegateId.AppendChild(rendererName);
}

void
XUSD_ImagingEngineGL::_SetRenderDelegate(
    HdPluginRenderDelegateUniqueHandle &&renderDelegate)
{
    // This relies on SetRendererPlugin to release the GIL...

    // Destruction
    _DestroyHydraObjects();

    _isPopulated = false;

    // Creation

    // Use the new render delegate.
    _renderDelegate = std::move(renderDelegate);

    // Recreate the render index
    _renderIndex.reset(
        HdRenderIndex::New(
            _renderDelegate.Get(), {&_hgiDriver}));

    // Create the new scene API
    if (_GetUseSceneIndices()) {
        _sceneIndex = _stageSceneIndex =
            UsdImagingStageSceneIndex::New();

        _sceneIndex =
            UsdImagingPiPrototypePropagatingSceneIndex::New(_sceneIndex);

        _sceneIndex =
            UsdImagingNiPrototypePropagatingSceneIndex::New(_sceneIndex);

        _sceneIndex =
            UsdImagingSelectionSceneIndex::New(_sceneIndex);

        _sceneIndex =
            UsdImagingRenderSettingsFlatteningSceneIndex::New(_sceneIndex);

        _sceneIndex =
            HdFlatteningSceneIndex::New(
                _sceneIndex, UsdImagingFlattenedDataSourceProviders());

        _sceneIndex =
            UsdImagingDrawModeSceneIndex::New(_sceneIndex,
                                              /* inputArgs = */ nullptr);

        _renderIndex->InsertSceneIndex(_sceneIndex, _sceneDelegateId);
    } else {
        _sceneDelegate = std::make_unique<UsdImagingDelegate>(
                _renderIndex.get(), _sceneDelegateId);
        _sceneDelegate->SetDisplayUnloadedPrimsWithBounds(_displayUnloaded);
        _sceneDelegate->SetUsdDrawModesEnabled(_enableUsdDrawModes);
        _sceneDelegate->SetCameraForSampling(_cameraPath);
    }

    // Create the new task controller
    _taskController = std::make_unique<HdxTaskController>(
        _renderIndex.get(),
        _ComputeControllerPath(_renderDelegate));

    // The task context holds on to resources in the render
    // deletegate, so we want to destroy it first and thus
    // create it last.
    _engine = std::make_unique<HdEngine>();
}

//----------------------------------------------------------------------------
// AOVs and Renderer Settings
//----------------------------------------------------------------------------

TfTokenVector
XUSD_ImagingEngineGL::GetRendererAovs(const TfTokenVector &candidates) const
{
    TF_VERIFY(_renderIndex);

    if (_renderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer)) {

        TfTokenVector aovs;
        for (auto const& aov : candidates) {
            if (_renderDelegate->GetDefaultAovDescriptor(aov).format 
                    != HdFormatInvalid) {
                aovs.push_back(aov);
            }
        }
        return aovs;
    }
    return TfTokenVector();
}

bool
XUSD_ImagingEngineGL::SetRendererAovs(TfTokenVector const &ids)
{
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

HgiTextureHandle
XUSD_ImagingEngineGL::GetAovTexture(
    TfToken const& name) const
{
    VtValue aov;
    HgiTextureHandle aovTexture;

    if (_engine->GetTaskContextData(name, &aov)) {
        if (aov.IsHolding<HgiTextureHandle>()) {
            aovTexture = aov.Get<HgiTextureHandle>();
        }
    }

    return aovTexture;
}

VtValue
XUSD_ImagingEngineGL::GetRendererSetting(TfToken const& id) const
{
    TF_VERIFY(_renderDelegate);
    return _renderDelegate->GetRenderSetting(id);
}

void
XUSD_ImagingEngineGL::SetRendererSetting(TfToken const& id, VtValue const& value)
{
    TF_VERIFY(_renderDelegate);
    _renderDelegate->SetRenderSetting(id, value);
}

void
XUSD_ImagingEngineGL::SetRenderOutputSettings(TfToken const &name,
        HdAovDescriptor const& desc)
{
    _taskController->SetRenderOutputSettings(name, desc);
}

void
XUSD_ImagingEngineGL::SetDisplayUnloadedPrimsWithBounds(bool displayUnloaded)
{
    if (!_GetUseSceneIndices()) {
        _sceneDelegate->SetDisplayUnloadedPrimsWithBounds(displayUnloaded);
    }
    _displayUnloaded = displayUnloaded;
}

void
XUSD_ImagingEngineGL::SetUsdDrawModesEnabled(bool enableUsdDrawModes)
{
    if (!_GetUseSceneIndices()) {
        _sceneDelegate->SetUsdDrawModesEnabled(enableUsdDrawModes);
    }
    _enableUsdDrawModes = enableUsdDrawModes;
}

// ---------------------------------------------------------------------
// Control of background rendering threads.
// ---------------------------------------------------------------------
bool
XUSD_ImagingEngineGL::IsPauseRendererSupported() const
{
    TF_VERIFY(_renderDelegate);
    return _renderDelegate->IsPauseSupported();
}

bool
XUSD_ImagingEngineGL::PauseRenderer()
{
    TF_PY_ALLOW_THREADS_IN_SCOPE();

    TF_VERIFY(_renderDelegate);
    return _renderDelegate->Pause();
}

bool
XUSD_ImagingEngineGL::ResumeRenderer()
{
    TF_PY_ALLOW_THREADS_IN_SCOPE();

    TF_VERIFY(_renderDelegate);
    return _renderDelegate->Resume();
}

bool
XUSD_ImagingEngineGL::IsStopRendererSupported() const
{
    TF_VERIFY(_renderDelegate);
    return _renderDelegate->IsStopSupported();
}

bool
XUSD_ImagingEngineGL::StopRenderer()
{
    TF_PY_ALLOW_THREADS_IN_SCOPE();

    TF_VERIFY(_renderDelegate);
    return _renderDelegate->Stop();
}

bool
XUSD_ImagingEngineGL::RestartRenderer()
{
    TF_PY_ALLOW_THREADS_IN_SCOPE();

    TF_VERIFY(_renderDelegate);
    return _renderDelegate->Restart();
}

//----------------------------------------------------------------------------
// Renderer Commands
//----------------------------------------------------------------------------

void
XUSD_ImagingEngineGL::GetRendererCommands(UT_StringArray &command_names,
        UT_StringArray &command_descriptions) const
{
    command_names.clear();
    command_descriptions.clear();

    if (_renderDelegate)
    {
        auto commands = _renderDelegate->GetCommandDescriptors();
        for (auto &&command : commands)
        {
            command_names.append(command.commandName.GetString());
            command_descriptions.append(command.commandDescription);
        }
    }
}

void
XUSD_ImagingEngineGL::InvokeRendererCommand(
        const UT_StringHolder &command_name) const
{
    if (_renderDelegate)
        _renderDelegate->InvokeCommand(TfToken(command_name.toStdString()));
}

//----------------------------------------------------------------------------
// Resource Information
//----------------------------------------------------------------------------

VtDictionary
XUSD_ImagingEngineGL::GetRenderStats() const
{
    TF_VERIFY(_renderDelegate);
    VtDictionary stats = _renderDelegate->GetRenderStats();
    auto delegate_key = HUSD_Constants::getRenderStatsDelegateKey().toStdString();
    if (!VtDictionaryIsHolding<TfToken>(stats, delegate_key))
        stats[delegate_key] = _renderDelegate.GetPluginId();
    return stats;
}

//----------------------------------------------------------------------------
// Private/Protected
//----------------------------------------------------------------------------

void 
XUSD_ImagingEngineGL::_Execute(const UsdImagingGLRenderParams &params,
                             HdTaskSharedPtrVector tasks,
                             bool renderer_uses_gl)
{
    GLuint vao = 0;

    GLF_GROUP_FUNCTION();
    if (renderer_uses_gl)
    {
         if (GlfContextCaps::GetInstance().coreProfile) {
            // We must bind a VAO (Vertex Array Object) because core profile
            // contexts do not have a default vertex array object. VAO objects
            // are container objects which are not shared between contexts, so
            // we create and bind a VAO here so that core rendering code does
            // not have to explicitly manage per-GL context state.
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
        } else {
            glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT | GL_DEPTH_BUFFER_BIT);
        }

        // hydra orients all geometry during topological processing so that
        // front faces have ccw winding.
        if (params.flipFrontFacing) {
            glFrontFace(GL_CW); // < State is pushed via GL_POLYGON_BIT
        } else {
            glFrontFace(GL_CCW); // < State is pushed via GL_POLYGON_BIT
        }

        if (params.applyRenderState) {
            glDisable(GL_BLEND);
        }

        // for points width
        glEnable(GL_PROGRAM_POINT_SIZE);
    }

    {
        // Release the GIL before calling into hydra, in case any hydra plugins
        // call into python.
        TF_PY_ALLOW_THREADS_IN_SCOPE();
        _engine->Execute(_renderIndex.get(), &tasks);
    }

    if (renderer_uses_gl)
    {
        if (GlfContextCaps::GetInstance().coreProfile) {
            glBindVertexArray(0);
            // XXX: We should not delete the VAO on every draw call, but we
            // currently must because it is GL Context state and we do not
            // control the context.
            glDeleteVertexArrays(1, &vao);
        } else {
            // GL_ENABLE_BIT | GL_POLYGON_BIT | GL_DEPTH_BUFFER_BIT
            glPopAttrib();
        }
    }
}

bool 
XUSD_ImagingEngineGL::_CanPrepare(const UsdPrim& root)
{
    HD_TRACE_FUNCTION();

    if (!TF_VERIFY(root, "Attempting to draw an invalid/null prim\n")) 
        return false;

    if (!root.GetPath().HasPrefix(_rootPath)) {
        TF_CODING_ERROR("Attempting to draw path <%s>, but engine is rooted"
                    "at <%s>\n",
                    root.GetPath().GetText(),
                    _rootPath.GetText());
        return false;
    }

    return true;
}

static int
_GetRefineLevel(float c)
{
    // TODO: Change complexity to refineLevel when we refactor UsdImaging.
    //
    // Convert complexity float to refine level int.
    int refineLevel = 0;

    // to avoid floating point inaccuracy (e.g. 1.3 > 1.3f)
    c = std::min(c + 0.01f, 2.0f);

    if (1.0f <= c && c < 1.1f) { 
        refineLevel = 0;
    } else if (1.1f <= c && c < 1.2f) { 
        refineLevel = 1;
    } else if (1.2f <= c && c < 1.3f) { 
        refineLevel = 2;
    } else if (1.3f <= c && c < 1.4f) { 
        refineLevel = 3;
    } else if (1.4f <= c && c < 1.5f) { 
        refineLevel = 4;
    } else if (1.5f <= c && c < 1.6f) { 
        refineLevel = 5;
    } else if (1.6f <= c && c < 1.7f) { 
        refineLevel = 6;
    } else if (1.7f <= c && c < 1.8f) { 
        refineLevel = 7;
    } else if (1.8f <= c && c <= 2.0f) { 
        refineLevel = 8;
    } else {
        TF_CODING_ERROR("Invalid complexity %f, expected range is [1.0,2.0]\n", 
                c);
    }
    return refineLevel;
}

void
XUSD_ImagingEngineGL::_PreSetTime(
    const UsdImagingGLRenderParams& params)
{
    HD_TRACE_FUNCTION();

    const int refineLevel = _GetRefineLevel(params.complexity);

    if (_GetUseSceneIndices()) {
        // XXX(USD-7115): fallback refine level
        _stageSceneIndex->ApplyPendingUpdates();
    } else {
        // Set the fallback refine level; if this changes from the
        // existing value, all prim refine levels will be dirtied.
        _sceneDelegate->SetRefineLevelFallback(refineLevel);

        // Apply any queued up scene edits.
        _sceneDelegate->ApplyPendingUpdates();
    }
}

void
XUSD_ImagingEngineGL::_PostSetTime(
    const UsdImagingGLRenderParams& params)
{
    HD_TRACE_FUNCTION();
}


/* static */
bool
XUSD_ImagingEngineGL::_UpdateHydraCollection(
    HdRprimCollection *collection,
    SdfPathVector const& roots,
    UsdImagingGLRenderParams const& params)
{
    if (collection == nullptr) {
        TF_CODING_ERROR("Null passed to _UpdateHydraCollection");
        return false;
    }

    // choose repr
    HdReprSelector reprSelector = HdReprSelector(HdReprTokens->smoothHull);
    const bool refined = params.complexity > 1.0;
    
    if (params.drawMode == UsdImagingGLDrawMode::DRAW_POINTS) {
        reprSelector = HdReprSelector(HdReprTokens->points);
    } else if (params.drawMode == UsdImagingGLDrawMode::DRAW_GEOM_FLAT ||
        params.drawMode == UsdImagingGLDrawMode::DRAW_SHADED_FLAT) {
        // Flat shading
        reprSelector = HdReprSelector(HdReprTokens->hull);
    } else if (
        params.drawMode == UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE) {
        // Wireframe on surface
        reprSelector = HdReprSelector(refined ?
            HdReprTokens->refinedWireOnSurf : HdReprTokens->wireOnSurf);
    } else if (params.drawMode == UsdImagingGLDrawMode::DRAW_WIREFRAME) {
        // Wireframe
        reprSelector = HdReprSelector(refined ?
            HdReprTokens->refinedWire : HdReprTokens->wire);
    } else {
        // Smooth shading
        reprSelector = HdReprSelector(refined ?
            HdReprTokens->refined : HdReprTokens->smoothHull);
    }

    // By default our main collection will be called geometry
    const TfToken colName = HdTokens->geometry;

    // Check if the collection needs to be updated (so we can avoid the sort).
    SdfPathVector const& oldRoots = collection->GetRootPaths();

    // inexpensive comparison first
    bool match = collection->GetName() == colName &&
                 oldRoots.size() == roots.size() &&
                 collection->GetReprSelector() == reprSelector;

    // Only take the time to compare root paths if everything else matches.
    if (match) {
        // Note that oldRoots is guaranteed to be sorted.
        for(size_t i = 0; i < roots.size(); i++) {
            // Avoid binary search when both vectors are sorted.
            if (oldRoots[i] == roots[i])
                continue;
            // Binary search to find the current root.
            if (!std::binary_search(oldRoots.begin(), oldRoots.end(), roots[i])) 
            {
                match = false;
                break;
            }
        }

        // if everything matches, do nothing.
        if (match) return false;
    }

    // Recreate the collection.
    *collection = HdRprimCollection(colName, reprSelector);
    collection->SetRootPaths(roots);

    return true;
}

/* static */
HdxRenderTaskParams
XUSD_ImagingEngineGL::_MakeHydraUsdImagingGLRenderParams(
    UsdImagingGLRenderParams const& renderParams)
{
    // Note this table is dangerous and making changes to the order of the 
    // enums in UsdImagingGLCullStyle, will affect this with no compiler help.
    static const HdCullStyle USD_2_HD_CULL_STYLE[] =
    {
        HdCullStyleDontCare,              // Cull No Opinion (unused)
        HdCullStyleNothing,               // CULL_STYLE_NOTHING,
        HdCullStyleBack,                  // CULL_STYLE_BACK,
        HdCullStyleFront,                 // CULL_STYLE_FRONT,
        HdCullStyleBackUnlessDoubleSided, // CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED
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

    if (renderParams.alphaThreshold < 0.0) {
        // If no alpha threshold is set, use default of 0.1.
        params.alphaThreshold = 0.1f;
    } else {
        params.alphaThreshold = renderParams.alphaThreshold;
    }

    params.enableSceneMaterials = renderParams.enableSceneMaterials;
    params.enableSceneLights = renderParams.enableSceneLights;

    // We don't provide the following because task controller ignores them:
    // - params.camera
    // - params.viewport

    return params;
}

//static
void
XUSD_ImagingEngineGL::_ComputeRenderTags(UsdImagingGLRenderParams const& params,
                                       TfTokenVector *renderTags)
{
    // Calculate the rendertags needed based on the parameters passed by
    // the application
    renderTags->clear();
    renderTags->reserve(4);
    renderTags->push_back(HdRenderTagTokens->geometry);
    if (params.showGuides) {
        renderTags->push_back(HdRenderTagTokens->guide);
    }
    if (params.showProxy) {
        renderTags->push_back(HdRenderTagTokens->proxy);
    }
    if (params.showRender) {
        renderTags->push_back(HdRenderTagTokens->render);
    }
}

/* static */
TfToken
XUSD_ImagingEngineGL::_GetDefaultRendererPluginId()
{
    static const std::string defaultRenderer = TfGetenv("HD_DEFAULT_RENDERER",
        HUSD_Constants::getHoudiniRendererPluginName().toStdString());

    if (defaultRenderer.empty())
        return TfToken();

    HfPluginDescVector pluginDescs;
    HdRendererPluginRegistry::GetInstance().GetPluginDescs(&pluginDescs);

    // Look for the one with the matching display name
    for (size_t i = 0; i < pluginDescs.size(); ++i) {
        if (pluginDescs[i].id == defaultRenderer ||
            pluginDescs[i].displayName == defaultRenderer) {
            return pluginDescs[i].id;
        }
    }

    TF_WARN("Failed to find default renderer '%s'.",
            defaultRenderer.c_str());

    return TfToken();
}

PXR_NAMESPACE_CLOSE_SCOPE

