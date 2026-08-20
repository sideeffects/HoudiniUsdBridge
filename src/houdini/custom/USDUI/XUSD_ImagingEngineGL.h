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

/// \file usdImagingGL/engine.h

#ifndef __XUSD_IMAGING_ENGINE_GL_H__
#define __XUSD_IMAGING_ENGINE_GL_H__

#include <HUSD/XUSD_ImagingEngine.h>

#include <pxr/usdImaging/usdImagingGL/renderParams.h>
#include <pxr/usdImaging/usdImagingGL/rendererSettings.h>

#include <pxr/imaging/hdx/selectionTracker.h>
#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>

#include "pxr/base/tf/declarePtrs.h"

PXR_NAMESPACE_OPEN_SCOPE

class UsdPrim;
class HdRenderIndex;
class HdxTaskController;
class UsdImagingDelegate;

TF_DECLARE_WEAK_AND_REF_PTRS(GlfSimpleLightingContext);
TF_DECLARE_REF_PTRS(UsdImagingStageSceneIndex);
TF_DECLARE_REF_PTRS(HdSceneIndexBase);

///
/// The XUSD_ImagingEngine is the main entry point API for rendering USD scenes.
///
class XUSD_ImagingEngineGL : public XUSD_ImagingEngine
{
public:
    XUSD_ImagingEngineGL(const TfToken& rendererPluginId,
                bool force_null_hgi, bool use_scene_indices);
    ~XUSD_ImagingEngineGL() override;

    // Check if the GL being used by USD imaging is running in core profile.
    bool isUsingGLCoreProfile() const override;

    // ---------------------------------------------------------------------
    /// \name Rendering
    /// @{
    // ---------------------------------------------------------------------

    /// Entry point for kicking off a render
    void DispatchRender(const UsdPrim& root,
                const XUSD_ImagingRenderParams &params) override;
    void CompleteRender(const XUSD_ImagingRenderParams &params,
                bool renderer_uses_gl) override;

    /// Returns true if the resulting image is fully converged.
    /// (otherwise, caller may need to call Render() again to refine the result)
    bool IsConverged() const override;

    /// Get an output AOV buffer from the render delegate.
    HdRenderBuffer *GetRenderOutput(TfToken const &name) override;

    /// Try to get the Raw Resource id (OGL texture id) from the HdRenderBuffer.
    /// The id, width, and height are output parameters. Return true if these
    /// values have been successfully populated.
    bool GetRawResource(HdRenderBuffer *buffer,
                exint &id, exint &width, exint &height) override;

    /// @}
    
    // ---------------------------------------------------------------------
    /// \name Camera State
    /// @{
    // ---------------------------------------------------------------------
    
    /// Set the viewport to use for rendering as (x,y,w,h), where (x,y)
    /// represents the lower left corner of the viewport rectangle, and (w,h)
    /// is the width and height of the viewport in pixels.
    void SetRenderViewport(GfVec4d const& viewport) override;

    /// Set the window policy to use.
    /// XXX: This is currently used for scene cameras set via SetCameraPath.
    /// See comment in SetCameraState for the free cam.
    void SetWindowPolicy(CameraUtilConformWindowPolicy policy) override;
    
    /// Scene camera API
    /// Set the scene camera path to use for rendering.
    void SetCameraPath(SdfPath const& id) override;

    /// Free camera API
    /// Set camera framing state directly (without pointing to a camera on the 
    /// USD stage). The projection matrix is expected to be pre-adjusted for the
    /// window policy.
    void SetCameraState(const GfMatrix4d& viewMatrix,
                        const GfMatrix4d& projectionMatrix) override;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Light State
    /// @{
    // ---------------------------------------------------------------------
    
    /// Set lighting state
    /// Derived classes should ensure that passing an empty lights
    /// vector disables lighting.
    /// \param lights is the set of lights to use, or empty to disable lighting.
    void SetLightingState(UT_Array<XUSD_GLSimpleLight> const &lights,
                          GfVec4f const &sceneAmbient) override;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Picking
    /// @{
    // ---------------------------------------------------------------------
    
    /// Decodes an array of pick results given hydra prim ID/instance ID (like
    /// you'd get from an ID render).
    bool DecodeIntersections(
        UT_Array<HUSD_RenderKey> &inOutKeys,
        SdfPathVector &outHitPrimPaths,
        std::vector<HdInstancerContext> &outHitInstancerContexts) override;

    /// @}
    
    // ---------------------------------------------------------------------
    /// \name Renderer Plugin Management
    /// @{
    // ---------------------------------------------------------------------

    /// Return the id of the currently used renderer plugin.
    TfToken GetCurrentRendererId() const override;

    /// Set the current render-graph delegate to \p id.
    /// the plugin will be loaded if it's not yet.
    bool SetRendererPlugin(TfToken const &id) override;

    /// @}
    
    // ---------------------------------------------------------------------
    /// \name AOVs and Renderer Settings
    /// @{
    // ---------------------------------------------------------------------

    /// Return the vector of available renderer AOV settings.
    TfTokenVector GetRendererAovs(
        const TfTokenVector &candidates) const override;

    /// Set the current renderer AOV to \p id.
    bool SetRendererAovs(TfTokenVector const& ids) override;

    /// Returns an AOV texture handle for the given token.
    HgiTextureHandle GetAovTexture(TfToken const& name) const override;

    /// Gets a renderer setting's current value.
    VtValue GetRendererSetting(TfToken const& id) const override;

    /// Sets a renderer setting's value.
    void SetRendererSetting(TfToken const& id,
                                    VtValue const& value) override;

    /// Set up camera and renderer output settings. These mostly expose
    /// functions from the Scene Delegate.
    void SetRenderOutputSettings(TfToken const &name,
                                 HdAovDescriptor const& desc) override;
    void SetDisplayUnloadedPrimsWithBounds(bool displayUnloaded) override;
    void SetUsdDrawModesEnabled(bool enableUsdDrawModes) override;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Control of background rendering threads.
    /// @{
    // ---------------------------------------------------------------------

    /// Query the renderer as to whether it supports pausing and resuming.
    bool IsPauseRendererSupported() const override;

    /// Pause the renderer.
    ///
    /// Returns \c true if successful.
    bool PauseRenderer() override;

    /// Resume the renderer.
    ///
    /// Returns \c true if successful.
    bool ResumeRenderer() override;

    /// Query the renderer as to whether it supports stopping and restarting.
    bool IsStopRendererSupported() const override;

    /// Stop the renderer.
    ///
    /// Returns \c true if successful.
    bool StopRenderer() override;

    /// Restart the renderer.
    ///
    /// Returns \c true if successful.
    bool RestartRenderer() override;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Renderer Commands
    /// @{
    // ---------------------------------------------------------------------

    /// Query the renderer for a list of available command descriptors, and
    /// return the information into the provided data structures.
    void GetRendererCommands(UT_StringArray &command_names,
            UT_StringArray &command_descriptions) const override;

    /// Invoke a renderer command with one of the command_names provided by
    /// GetRendererCommands().
    void InvokeRendererCommand(
            const UT_StringHolder &command_name) const override;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Render Statistics
    /// @{
    // ---------------------------------------------------------------------

    /// Returns render statistics.
    ///
    /// The contents of the dictionary will depend on the current render 
    /// delegate.
    ///
    VtDictionary GetRenderStats() const override;

    /// @}

protected:
    void _Execute(const UsdImagingGLRenderParams &params,
                  HdTaskSharedPtrVector tasks,
                  bool renderer_uses_gl);

    // These functions factor batch preparation into separate steps so they
    // can be reused by both the vectorized and non-vectorized API.
    bool _CanPrepare(const UsdPrim& root);
    void _PrepareBatch(const UsdPrim& root,
        const UsdImagingGLRenderParams& params);
    void _PreSetTime(const UsdImagingGLRenderParams& params);
    void _PostSetTime(const UsdImagingGLRenderParams& params);
    void _PrepareRender(const UsdImagingGLRenderParams& params);
    void _UpdateDomeLightCameraVisibility();

    // Create a hydra collection given root paths and render params.
    // Returns true if the collection was updated.
    static bool _UpdateHydraCollection(HdRprimCollection *collection,
                          SdfPathVector const& roots,
                          UsdImagingGLRenderParams const& params);
    static HdxRenderTaskParams _MakeHydraUsdImagingGLRenderParams(
                          UsdImagingGLRenderParams const& params);
    static void _ComputeRenderTags(UsdImagingGLRenderParams const& params,
                          TfTokenVector *renderTags);

    void _InitializeHgiIfNecessary(bool forceNullHgi);

    void _SetRenderDelegateAndRestoreState(
        HdPluginRenderDelegateUniqueHandle &&);

    void _SetRenderDelegate(HdPluginRenderDelegateUniqueHandle &&);

    SdfPath _ComputeControllerPath(const HdPluginRenderDelegateUniqueHandle &);

    static TfToken _GetDefaultRendererPluginId();

private:
    void _DestroyHydraObjects();
    bool _GetUseSceneIndices() const
    { return _useSceneIndices; }

    HdPluginRenderDelegateUniqueHandle _renderDelegate;
    std::unique_ptr<HdRenderIndex> _renderIndex;

    std::unique_ptr<HdxTaskController> _taskController;

    HdRprimCollection _renderCollection;
    HdRprimCollection _intersectCollection;

    GlfSimpleLightingContextRefPtr _lightingContextForOpenGLState;
    bool _domeLightCameraVisibility;

    SdfPath _rootPath;
    SdfPath _cameraPath;
    SdfPathVector _excludedPrimPaths;
    SdfPathVector _invisedPrimPaths;

    // Note that we'll only ever use one of _sceneIndex/_sceneDelegate
    // at a time...
    UsdImagingStageSceneIndexRefPtr _stageSceneIndex;
    HdSceneIndexBaseRefPtr _sceneIndex;

    bool _isPopulated;
    SdfPath _sceneDelegateId;
    std::unique_ptr<UsdImagingDelegate> _sceneDelegate;

    std::unique_ptr<HdEngine> _engine;
    HgiUniquePtr _hgi;
    HdDriver _hgiDriver;

    bool _displayUnloaded;
    bool _enableUsdDrawModes;
    bool _useSceneIndices;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif
