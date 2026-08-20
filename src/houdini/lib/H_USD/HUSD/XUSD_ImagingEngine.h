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
#include <UT/UT_Vector3.h>
#include <pxr/pxr.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/base/tf/declarePtrs.h>

PXR_NAMESPACE_OPEN_SCOPE

class UsdPrim;
class XUSD_ImagingEngine;

TF_DECLARE_WEAK_AND_REF_PTRS(GlfSimpleLightingContext);

class HUSD_API XUSD_GLSimpleLight
{
public:
    float        myIntensity;
    float        myAngle;
    UT_Vector3F  myColor;
    bool         myIsDomeLight;
};

class HUSD_API XUSD_ImagingRenderParams
{
public:
    XUSD_ImagingRenderParams()
        : myFrame(0.0),
          myComplexity(1.0),
          myDrawMode(DRAW_SHADED_SMOOTH),
          myCullStyle(CULL_STYLE_NOTHING),
          myShowProxy(true),
          myShowGuides(false),
          myShowRender(false),
          myHighlight(false),
          myEnableUsdDrawModes(true),
          myEnableLighting(true),
          myEnableSceneLights(true),
          myEnableSceneMaterials(true),
          myEnableSampleAlphaToCoverage(true)
    {}
    bool operator==(const XUSD_ImagingRenderParams &other) const
    {
        return myFrame == other.myFrame
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
    XUSD_ImagingEngine();
    virtual ~XUSD_ImagingEngine();

    // Static function for creating XUSD_ImagingeEngine objects.
    // The real implementation of this class is in $SHC/USDUI.
    static UT_UniquePtr<XUSD_ImagingEngine> createImagingEngine(
            const UT_StringRef& renderer,
            bool force_null_hgi, bool use_scene_indices);

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
    virtual void SetWindowPolicy(CameraUtilConformWindowPolicy policy) = 0;
    
    /// Scene camera API
    /// Set the scene camera path to use for rendering.
    virtual void SetCameraPath(SdfPath const& id) = 0;

    /// Free camera API
    /// Set camera framing state directly (without pointing to a camera on the 
    /// USD stage). The projection matrix is expected to be pre-adjusted for the
    /// window policy.
    virtual void SetCameraState(const GfMatrix4d& viewMatrix,
                        const GfMatrix4d& projectionMatrix) = 0;

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
    virtual TfToken GetCurrentRendererId() const = 0;

    /// Set the current render-graph delegate to \p id.
    /// the plugin will be loaded if it's not yet.
    virtual bool SetRendererPlugin(TfToken const &id) = 0;

    /// @}
    
    // ---------------------------------------------------------------------
    /// \name AOVs and Renderer Settings
    /// @{
    // ---------------------------------------------------------------------

    /// Return the vector of available renderer AOV settings.
    virtual TfTokenVector GetRendererAovs(
        const TfTokenVector &candidates) const = 0;

    /// Set the current renderer AOV to \p id.
    virtual bool SetRendererAovs(TfTokenVector const& ids) = 0;

    /// Returns an AOV texture handle for the given token.
    virtual HgiTextureHandle GetAovTexture(TfToken const& name) const = 0;

    /// Gets a renderer setting's current value.
    virtual VtValue GetRendererSetting(TfToken const& id) const = 0;

    /// Sets a renderer setting's value.
    virtual void SetRendererSetting(TfToken const& id,
                                    VtValue const& value) = 0;

    /// Set up camera and renderer output settings. These mostly expose
    /// functions from the Scene Delegate.
    virtual void SetRenderOutputSettings(TfToken const &name,
                                        HdAovDescriptor const& desc) = 0;
    virtual void SetDisplayUnloadedPrimsWithBounds(bool displayUnloaded) = 0;
    virtual void SetUsdDrawModesEnabled(bool enableUsdDrawModes) = 0;

    /// @}

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
            UT_StringArray &command_descriptions) const = 0;

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
    virtual VtDictionary GetRenderStats() const = 0;

    /// @}
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
