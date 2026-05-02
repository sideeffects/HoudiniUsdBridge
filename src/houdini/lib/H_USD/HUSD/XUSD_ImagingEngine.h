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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_ImagingEngine.h (HUSD Library, C++)
 */

#ifndef __XUSD_IMAGING_ENGINE_H__
#define __XUSD_IMAGING_ENGINE_H__

#include "HUSD_API.h"
#include "HUSD_Imaging.h"
#include <UT/UT_StringArray.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_Vector3.h>
#include <pxr/pxr.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hdsi/legacyDisplayStyleOverrideSceneIndex.h>
#include <pxr/imaging/hdsi/primTypeAndPathPruningSceneIndex.h>
#include <pxr/imaging/hdsi/sceneGlobalsSceneIndex.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImaging/rootOverridesSceneIndex.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/base/tf/declarePtrs.h>

class GU_ConstDetailHandle;
class UT_StringRef;

PXR_NAMESPACE_OPEN_SCOPE

class UsdPrim;
class XUSD_ImagingEngine;

TF_DECLARE_WEAK_AND_REF_PTRS(GlfSimpleLightingContext);

class HUSD_API XUSD_GLSimpleLight
{
public:
    UT_Matrix4D  myXform;
    float        myIntensity;
    float        myAngle;
    UT_Vector3F  myColor;
    bool         myIsDomeLight;
    UT_StringHolder myDomeMapPath;
};

class HUSD_API XUSD_ImagingRenderParams
{
public:
    XUSD_ImagingRenderParams()
        : myFrame(0.0)
        , myRenderTags()
        , myComplexity(1.0)
        , myDrawMode(DRAW_SHADED_SMOOTH)
        , myCullStyle(CULL_STYLE_NOTHING)
        , myShowProxy(true)
        , myShowGuides(false)
        , myShowRender(false)
        , myHighlight(false)
        , myEnableUsdDrawModes(true)
        , myEnableLighting(true)
        , myEnableSceneLights(true)
        , myEnableSceneMaterials(true)
        , myEnableSampleAlphaToCoverage(true)
    {}
    bool operator==(const XUSD_ImagingRenderParams &other) const
    {
        return myFrame == other.myFrame
            && myRenderTags == other.myRenderTags
            && myComplexity == other.myComplexity
            && myDrawMode == other.myDrawMode
            && myCullStyle == other.myCullStyle
            && myShowProxy == other.myShowProxy
            && myShowGuides == other.myShowGuides
            && myShowRender == other.myShowRender
            && myHighlight == other.myHighlight
            && myEnableUsdDrawModes == other.myEnableUsdDrawModes
            && myEnableLighting == other.myEnableLighting
            && myEnableSceneLights == other.myEnableSceneLights
            && myEnableSceneMaterials == other.myEnableSceneMaterials
            && myEnableSampleAlphaToCoverage == other.myEnableSampleAlphaToCoverage;
    }
    bool operator!=(const XUSD_ImagingRenderParams &other) const
    { return !(*this == other); }

    enum XUSD_ImagingCullStyle {
        CULL_STYLE_BACK,
        CULL_STYLE_NOTHING
    };

    enum XUSD_ImagingDrawMode
    {
        DRAW_WIREFRAME,
        DRAW_GEOM_ONLY,
        DRAW_SHADED_FLAT,
        DRAW_SHADED_SMOOTH,
        DRAW_WIREFRAME_ON_SURFACE
    };

    fpreal64 myFrame;
    TfTokenVector myRenderTags;
    float myComplexity;
    XUSD_ImagingDrawMode myDrawMode;
    XUSD_ImagingCullStyle myCullStyle;
    bool myShowProxy;
    bool myShowGuides;
    bool myShowRender;
    bool myHighlight;
    bool myEnableUsdDrawModes;
    bool myEnableLighting;
    bool myEnableSceneLights;
    bool myEnableSceneMaterials;
    bool myEnableSampleAlphaToCoverage;
};

///
/// The XUSD_ImagingEngine is the main entry point API for rendering USD scenes.
///
class HUSD_API XUSD_ImagingEngine
{
public:
    struct _AppSceneIndices;
    using _AppSceneIndicesSharedPtr = std::shared_ptr<struct _AppSceneIndices>;

    /// Parameters to construct XUSD_ImagingEngineGL.
    // NOTE: This struct is, with the exception of documented modifications,
    //       copied directly from (and should ideally be kept in-sync with)
    //       UsdImagingGLEngine::Parameters (pxr/usdImaging/usdImagingGL/engine.h)
    //       Just because a field appears here does not *necessarily* mean we
    //       have implemented support for it yet.
    struct Parameters
    {
        SdfPath rootPath = SdfPath::AbsoluteRootPath();
        SdfPathVector excludedPaths;
        SdfPathVector invisedPaths;
        SdfPath sceneDelegateID = SdfPath::AbsoluteRootPath();
        /// An HdDriver, containing the Hgi of your choice, can be optionally passed
        /// in during construction. This can be helpful if your application creates
        /// multiple UsdImagingGLEngine's that wish to use the same HdDriver / Hgi.
        HdDriver driver;
        /// The \p rendererPluginId argument indicates the renderer plugin that
        /// Hydra should use. If the empty token is passed in, a default renderer
        /// plugin will be chosen depending on the value of \p gpuEnabled.
        TfToken rendererPluginId;
        /// The \p gpuEnabled argument determines if this instance will allow Hydra
        /// to use the GPU to produce images.
        bool gpuEnabled = true;
        /// \p displayUnloadedPrimsWithBounds draws bounding boxes for unloaded
        /// prims if they have extents/extentsHint authored.
        bool displayUnloadedPrimsWithBounds = false;
        /// \p allowAsynchronousSceneProcessing indicates to constructed hydra
        /// scene indices that asynchronous processing is allowow. Applications
        /// should perodically call PollForAsynchronousUpdates on the engine.
        bool allowAsynchronousSceneProcessing = false;
        /// The \p enableUsdDrawModes parameter indicates if the UsdGeomModelAPI
        /// draw mode feature is enabled or not.
        bool enableUsdDrawMode = true;

        /// \name Custom Houdini parameters
        /// @ {
        // bool enable_usd_draw_modes = true;
        bool use_scene_indices = false;
        bool enable_gpu_context = true;
        bool force_null_hgi = false;
        bool fast_path_color = true;    // Direct render color buffer
        /// @}
    };

    XUSD_ImagingEngine(const Parameters &params);
    virtual ~XUSD_ImagingEngine();

    // Static function for creating XUSD_ImagingEngine objects.
    // The real implementation of this class is in $SHC/USDUI.
    static UT_UniquePtr<XUSD_ImagingEngine> createImagingEngine(
            const Parameters &params);

    // Disallow copies
    XUSD_ImagingEngine(const XUSD_ImagingEngine&) = delete;
    XUSD_ImagingEngine& operator=(const XUSD_ImagingEngine&) = delete;

    // Check if the GL being used by USD imaging is running in core profile.
    virtual bool isUsingGLCoreProfile() const = 0;

    // ---------------------------------------------------------------------
    /// \name Rendering
    /// @{
    // ---------------------------------------------------------------------

    /// Entry point for kicking off a render
    virtual void DispatchRender(const UsdPrim& root,
                const XUSD_ImagingRenderParams &params) = 0;
    virtual void CompleteRender(const XUSD_ImagingRenderParams &params,
                bool renderer_uses_gl) = 0;

    /// Returns true if the resulting image is fully converged.
    /// (otherwise, caller may need to call Render() again to refine the result)
    virtual bool IsConverged() const = 0;

    /// Get an output AOV buffer from the render delegate.
    virtual HdRenderBuffer *GetRenderOutput(TfToken const &name) = 0;

    /// Try to get the Raw Resource id (OGL texture id) from the HdRenderBuffer.
    /// The id, width, and height are output parameters. Return true if these
    /// values have been successfully populated.
    virtual bool GetRawResource(HdRenderBuffer *buffer,
                exint &id, exint &width, exint &height) = 0;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Camera State
    /// @{
    // ---------------------------------------------------------------------

    /// Set the viewport to use for rendering as (x,y,w,h), where (x,y)
    /// represents the lower left corner of the viewport rectangle, and (w,h)
    /// is the width and height of the viewport in pixels.
    virtual void SetRenderViewport(GfVec4d const& viewport) = 0;

    /// Set the window policy to use.
    /// XXX: This is currently used for scene cameras set via SetCameraPath.
    /// See comment in SetCameraState for the free cam.
    virtual void SetWindowPolicy(CameraUtilConformWindowPolicy policy);

    /// Scene camera API
    /// Set the scene camera path to use for rendering.
    virtual void SetCameraPath(SdfPath const& id) = 0;

    /// Free camera API
    /// Set camera framing state directly (without pointing to a camera on the
    /// USD stage). The projection matrix is expected to be pre-adjusted for the
    /// window policy.
    virtual void SetCameraState(const GfMatrix4d& viewMatrix,
                        const GfMatrix4d& projectionMatrix) = 0;

    const SdfPath       &GetCameraPath() const { return _cameraPath; }

    /// @}

    // ---------------------------------------------------------------------
    /// \name Light State
    /// @{
    // ---------------------------------------------------------------------

    /// Set lighting state
    /// Derived classes should ensure that passing an empty lights
    /// vector disables lighting.
    /// \param lights is the set of lights to use, or empty to disable lighting.
    virtual void SetLightingState(UT_Array<XUSD_GLSimpleLight> const &lights,
                          GfVec4f const &sceneAmbient) = 0;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Picking
    /// @{
    // ---------------------------------------------------------------------

    /// Decodes an array of pick results given hydra prim ID/instance ID (like
    /// you'd get from an ID render).
    virtual bool DecodeIntersections(
        UT_Array<HUSD_RenderKey> &inOutKeys,
        SdfPathVector &outHitPrimPaths,
        std::vector<HdInstancerContext> &outHitInstancerContexts) = 0;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Renderer Plugin Management
    /// @{
    // ---------------------------------------------------------------------

    /// Return the vector of available render-graph delegate plugins.
    static TfTokenVector GetRendererPlugins();

    /// Return the user-friendly description of a renderer plugin.
    static std::string GetRendererDisplayName(TfToken const &id);

    /// Return the id of the currently used renderer plugin.
    virtual TfToken GetCurrentRendererId() const;

    /// Set the current render-graph delegate to \p id.
    /// the plugin will be loaded if it's not yet.
    virtual bool SetRendererPlugin(TfToken const &id,
                            const HdRenderSettingsMap &settingsMap = {}) = 0;

    /// Delete Hydra resources
    virtual void DestroyHydraResources() = 0;
    /// @}

    // ---------------------------------------------------------------------
    /// \name AOVs and Renderer Settings
    /// @{
    // ---------------------------------------------------------------------

    /// Return the vector of available renderer AOV settings.
    virtual TfTokenVector GetRendererAovs(
        const TfTokenVector &candidates) const;

    /// Set the current renderer AOV to \p id.
    virtual bool SetRendererAovs(TfTokenVector const& ids) = 0;
    virtual bool SetRendererAovsDescs(TfTokenVector const& ids,
                                    HdAovDescriptorList const& descs);

    /// Returns an AOV texture handle for the given token.
    virtual HgiTextureHandle GetAovTexture(TfToken const& name) const = 0;

    /// Returns default AOV descriptor for the given token
    virtual HdAovDescriptor GetDefaultAovDescriptor(const TfToken &token);

    /// Gets a renderer setting's current value.
    virtual VtValue GetRendererSetting(TfToken const& id) const;

    /// Sets a renderer setting's value.
    virtual void SetRendererSetting(TfToken const& id,
                                    VtValue const& value) = 0;

    /// Set up camera and renderer output settings. These mostly expose
    /// functions from the Scene Delegate.
    virtual void SetRenderOutputSettings(TfToken const &name,
                                        HdAovDescriptor const& desc) = 0;
    /// @}

    // ---------------------------------------------------------------------
    /// \name Scene-defined Render Pass and Render Settings
    /// \note Support is WIP.
    /// @{
    // ---------------------------------------------------------------------

    /// Returns the active render pass prim path by querying the terminal scene
    /// index. Returns an empty path if none was found.
    virtual SdfPath GetActiveRenderPassPrimPath() const;

    /// Returns the active render settings prim path by querying the terminal
    /// scene index. Returns an empty path if none was found.
    virtual SdfPath GetActiveRenderSettingsPrimPath() const;

    /// Utility method to query available render settings prims.
    virtual SdfPathVector GetAvailableRenderSettingsPrimPaths(UsdPrim const &root);

    /// Set active render pass prim to use to drive rendering.
    virtual void SetActiveRenderPassPrimPath(SdfPath const &);

    /// Set active render settings prim to use to drive rendering.
    virtual void SetActiveRenderSettingsPrimPath(SdfPath const &);

    /// @}

    /// Set Dome Light Camera Visibility (vis Scene Index)
    virtual void SetDomeLightCameraVisibility(bool);

    // ---------------------------------------------------------------------
    /// \name Control of background rendering threads.
    /// @{
    // ---------------------------------------------------------------------

    /// Query the renderer as to whether it supports pausing and resuming.
    virtual bool IsPauseRendererSupported() const = 0;

    /// Pause the renderer.
    ///
    /// Returns \c true if successful.
    virtual bool PauseRenderer() = 0;

    /// Resume the renderer.
    ///
    /// Returns \c true if successful.
    virtual bool ResumeRenderer() = 0;

    /// Query the renderer as to whether it supports stopping and restarting.
    virtual bool IsStopRendererSupported() const = 0;

    /// Stop the renderer.
    ///
    /// Returns \c true if successful.
    virtual bool StopRenderer() = 0;

    /// Restart the renderer.
    ///
    /// Returns \c true if successful.
    virtual bool RestartRenderer() = 0;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Renderer Commands
    /// @{
    // ---------------------------------------------------------------------

    /// Query the renderer for a list of available command descriptors, and
    /// return the information into the provided data structures.
    virtual void GetRendererCommands(UT_StringArray &command_names,
            UT_StringArray &command_descriptions) const;

    /// Invoke a renderer command with one of the command_names provided by
    /// GetRendererCommands().
    virtual void InvokeRendererCommand(
            const UT_StringHolder &command_name) const = 0;

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
    virtual VtDictionary GetRenderStats() const;

    /// @}

    // ---------------------------------------------------------------------
    /// \name Asynchronous
    /// @{
    // ---------------------------------------------------------------------

    /// If \p allowAsynchronousSceneProcessing is true within the Parameters
    /// provided to the UsdImagingGLEngine constructor, an application can
    /// periodically call this from the main thread.
    ///
    /// A return value of true indicates that the scene has changed and the
    /// render should be updated.
    virtual bool PollForAsynchronousUpdates() const = 0;

    /// @}

    void        getRoot(GfMatrix4d &xform, bool &visible) const;
    void        setRoot(const GfMatrix4d &xform, bool visible);
    void        updateApexAnimateSceneIndex(
                    const UT_StringRef &scene_prim_path,
                    const UT_StringMap<GU_ConstDetailHandle> &graph_outputs) const;
    void        showHdGpProcedurals(bool show) const;

protected:
    // ------------------  protected methods ------------------------------
    // These functions factor batch preparation into separate steps so they
    // can be reused by both the vectorized and non-vectorized API.
    bool _CanPrepare(const UsdPrim& root);
    void _SetActiveRenderSettingsPrimFromStageMetadata(UsdStageWeakPtr stage);
    void _SetSceneGlobalsCurrentFrame(UsdTimeCode const &time);

    bool _HasRenderer() const;
    HdSceneIndexBaseRefPtr _GetTerminalSceneIndex() const;

    SdfPath _ComputeControllerPath(const HdPluginRenderDelegateUniqueHandle &);
    SdfPath _ComputeControllerPath(const TfToken &pluginId);
    HdSceneIndexBaseRefPtr _AppendOverridesSceneIndices(
                                const HdSceneIndexBaseRefPtr &inputScene);
    static HdSceneIndexBaseRefPtr _AppendSceneGlobalsSceneIndexCallback(
                                const std::string &renderInstanceId,
                                const HdSceneIndexBaseRefPtr &inputScene,
                                const HdContainerDataSourceHandle &inputArgs);
    static HdSceneIndexBaseRefPtr _AppendApexAnimateSceneIndexCallback(
                                const std::string &renderInstanceId,
                                const HdSceneIndexBaseRefPtr &inputScene,
                                const HdContainerDataSourceHandle &inputArgs);
    static HdSceneIndexBaseRefPtr _AppendHdGpResolverSceneIndexCallback(
                                const std::string &renderInstanceId,
                                const HdSceneIndexBaseRefPtr &inputScene,
                                const HdContainerDataSourceHandle &inputArgs);
    static void _RegisterApplicationSceneIndices();

    // Houdini convenience methods
    void	enableMaterialsLights(bool enable_materials, bool enable_lights);
    void	setRefineLevel(int refineLevel);
    void	applyPendingUpdates();
    // Returns a human-readable unique identifier for this instance.  In
    // particular, useful for a Hydra Scene Index Browser.
    std::string createAppSceneIndices(
                            const HdPluginRenderDelegateUniqueHandle &delegate);
    void        createSceneAPI(bool display_unloaded,
                            bool enable_usd_drawmodes);
    void        destroyCommonHydraResources();
    void        populateScene(const UsdPrim &root, bool enable_usd_draw_modes);
    void        setTime(fpreal frame);
    void        setCameraForSampling(SdfPath const &id);

    // Preload the required libraries and create a delegate.  This will fall
    // back and use the delegate label (displayName) if the token doesn't match
    // the token exactly.
    HdPluginRenderDelegateUniqueHandle	createDelegate(const TfToken &token,
                                            const HdRenderSettingsMap &map = {});

    // ------------------  protected member data ---------------------------
    HdPluginRenderDelegateUniqueHandle _renderDelegate;
    std::unique_ptr<HdRenderIndex> _renderIndex;

    HdRprimCollection _renderCollection;
    HdRprimCollection _intersectCollection;

    SdfPath _rootPath;
    SdfPath _cameraPath;
    SdfPathVector _excludedPrimPaths;
    SdfPathVector _invisedPrimPaths;

    // Note that we'll only ever use one of _sceneIndex/_sceneDelegate
    // at a time...
    UsdImagingStageSceneIndexRefPtr _stageSceneIndex;
    HdNoticeBatchingSceneIndexRefPtr _postInstancingNoticeBatchingSceneIndex;
    UsdImagingSelectionSceneIndexRefPtr _selectionSceneIndex;
    UsdImagingRootOverridesSceneIndexRefPtr _rootOverridesSceneIndex;
    HdsiLegacyDisplayStyleOverrideSceneIndexRefPtr _displayStyleSceneIndex;
    HdsiPrimTypeAndPathPruningSceneIndexRefPtr _lightPruningSceneIndex;
    // State of the _lightPruningSceneIndex.
    bool _lightPruningSceneIndexEnableSceneLights;
    HdSceneIndexBaseRefPtr _sceneIndex;

    _AppSceneIndicesSharedPtr   _appSceneIndices;

    bool _isPopulated;
    SdfPath _sceneDelegateId;
    std::unique_ptr<UsdImagingDelegate> _sceneDelegate;

    std::unique_ptr<HdEngine> _engine;
    HgiUniquePtr _hgi;
    HdDriver _hgiDriver;

    bool _useSceneIndices;

    bool _enableUsdDrawModes;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
