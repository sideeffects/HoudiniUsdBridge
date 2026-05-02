/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	XUSD_ImagingEngineHusk.h ( LIB Library, C++)
 *
 * COMMENTS:
 */

#ifndef __XUSD_ImagingEngineHusk__
#define __XUSD_ImagingEngineHusk__

#include "XUSD_ImagingEngine.h"
#include <UT/UT_NonCopyable.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/stagePopulationMask.h>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_HuskTaskManager;
class UsdImagingDelegate;

class HUSD_API XUSD_ImagingEngineHusk final
    : public XUSD_ImagingEngine, UT_NonCopyable
{
public:
    XUSD_ImagingEngineHusk(const Parameters &params);
    ~XUSD_ImagingEngineHusk() override;

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
    /// Set the current render-graph delegate to \p id.
    /// the plugin will be loaded if it's not yet.
    bool SetRendererPlugin(TfToken const &id,
                    const HdRenderSettingsMap &settings) override;

    /// Delete Hydra resources
    void DestroyHydraResources() override;
    /// @}

    // ---------------------------------------------------------------------
    /// \name AOVs and Renderer Settings
    /// @{
    // ---------------------------------------------------------------------

    /// Set the current renderer AOV to \p id.
    bool SetRendererAovs(TfTokenVector const& ids) override;
    bool SetRendererAovsDescs(TfTokenVector const& ids,
                                    HdAovDescriptorList const& descs) override;

    /// Returns an AOV texture handle for the given token.
    HgiTextureHandle GetAovTexture(TfToken const& name) const override;

    /// Sets a renderer setting's value.
    void SetRendererSetting(TfToken const& id,
                                    VtValue const& value) override;

    /// Set up camera and renderer output settings. These mostly expose
    /// functions from the Scene Delegate.
    void SetRenderOutputSettings(TfToken const &name,
                                        HdAovDescriptor const& desc) override;

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

    /// Invoke a renderer command with one of the command_names provided by
    /// GetRendererCommands().
    void InvokeRendererCommand(
            const UT_StringHolder &command_name) const override;

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
    bool PollForAsynchronousUpdates() const override;

    /// @}

protected:
    //---------------------------------------------------------------------
    // Protected methods
    //---------------------------------------------------------------------
    //HdPluginRenderDelegateUniqueHandle  _NewPlugin(const TfToken &id) override;
    void        prepareBatch(const UsdPrim &root, fpreal frame);
    bool        updateHydraCollection(HdRprimCollection &,
                            SdfPathVector const& roots);
    void	doRender();
    void	preSetTime(const UsdPrim &root);
    void	postSetTime(const UsdPrim &root);

    //---------------------------------------------------------------------
    // Protected member data
    //---------------------------------------------------------------------
    UT_UniquePtr<XUSD_HuskTaskManager>  myTaskManager;

    TfTokenVector	myRenderTags;
    int			myComplexity;
    bool		mySceneMaterials;
    bool		mySceneLights;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
