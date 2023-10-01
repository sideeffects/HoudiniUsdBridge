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

#ifndef __HUSD_Imaging_h__
#define __HUSD_Imaging_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_RenderBuffer.h"
#include "HUSD_RendererInfo.h"
#include "HUSD_Scene.h"
#include <UT/UT_BoundingBox.h>
#include <UT/UT_Function.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_Options.h>
#include <UT/UT_Rect.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_UniquePtr.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE
class VtValue;
class XUSD_RenderSettings;
class XUSD_RenderSettingsContext;
PXR_NAMESPACE_CLOSE_SCOPE

class HUSD_Compositor;
class HUSD_Scene;
class husd_DefaultRenderSettingContext;

class HUSD_API HUSD_Imaging : public UT_NonCopyable
{
public:
			 HUSD_Imaging();
			~HUSD_Imaging();

    // The scene is not owned by this class.
    void setScene(HUSD_Scene *scene_ref);

    // only the USD modes that map to ours
    enum DrawMode
    {
	DRAW_WIRE,
	DRAW_SHADED_NO_LIGHTING,
	DRAW_SHADED_FLAT,
	DRAW_SHADED_SMOOTH,
	DRAW_WIRE_SHADED_SMOOTH
    };

    void		 showPurposeRender(bool enable);
    void		 showPurposeProxy(bool enable);
    void		 showPurposeGuide(bool enable);

    void		 setDrawMode(DrawMode mode);
    void		 setDrawComplexity(float complexity);
    void		 setBackfaceCull(bool cull);
    void		 setStage(const HUSD_DataHandle &data_handle,
				const HUSD_ConstOverridesPtr &overrides,
				const HUSD_ConstPostLayersPtr &postlayers);
    bool		 setFrame(fpreal frame);
    bool		 setDefaultLights(bool doheadlight, bool dodomelight);
    void                 setHeadlightIntensity(fpreal intensity);
    void		 setLighting(bool enable);
    void		 setMaterials(bool enable);
    void                 setAspectPolicy(HUSD_Scene::ConformPolicy p);
    void                 setDepthStyle(HUSD_DepthStyle depth)
                            { myDepthStyle = depth; }

    enum BufferSet
    {
        BUFFER_COLOR_DEPTH,
        BUFFER_COLOR,
        BUFFER_NONE,
    };
    BufferSet            hasAOVBuffers() const;
    
    // This callback is run after the UsdImagineEngineGL::_Execute method.
    // This method will clear the current VAO when it exits when running in
    // a core profile OpenGL context (i.e. always on Mac). So we need a
    // chance ot notify the RE_OGLRender that the VAO has been unbound.
    typedef UT_Function<void (HUSD_Imaging *imaging)> PostRenderCallback;
    void		 setPostRenderCallback(const PostRenderCallback &cb);
    bool		 getUsingCoreProfile();

    bool                 canBackgroundRender(const UT_StringRef &name) const;

    // Fire off a render and return immediately.
    // Only call if canBackgroundRender() returns true.
    bool                 launchBackgroundRender(const UT_Matrix4D &view_matrix,
                                                const UT_Matrix4D &proj_matrix,
                                                const UT_DimRect  &viewport_rect,
                                                const UT_StringRef &renderer,
                                                const UT_Options *render_opts,
                                                bool cam_effects);
    // Wait for the BG update to be finished.
    void                 waitForUpdateToComplete();
    // Check if the BG update is finished, and optionally do a render if it is.
    bool                 checkRender(bool do_render);

    void                 updateComposite(bool free_buffers_if_missing);

    HUSD_RenderBuffer    getAOVBuffer(const UT_StringRef &name) const;

    // Fire off a render and block until done. It may return false if the
    // render delegate fails to initialize, it which case another delegate
    // should be chosen.
    bool		 render(const UT_Matrix4D &view_matrix,
				const UT_Matrix4D &proj_matrix,
				const UT_DimRect  &viewport_rect,
				const UT_StringRef &renderer,
				const UT_Options *render_opts,
                                bool cam_effects);
    
    // Set the camera being viewed through (can be null for no camera);
    void                 setCameraPath(const UT_StringRef &path,
                                       bool camera_synced = true)
                         {
                             if(path != myCameraPath)
                             {
                                 myCameraPath = path;
                                 mySettingsChanged = true;
                             }
                             // use camera for sampling parms, not frustum.
                             if(camera_synced != myCameraSynced)
                             {
                                 myCameraSynced = camera_synced;
                                 mySettingsChanged = true;
                             }
                         }
    
    void		 setAOVCompositor(HUSD_Compositor *comp)
			 { myCompositor = comp; }

    HUSD_Scene		&scene()
			 { return *myScene; }
    bool		 isConverged() const
			 { return !isUpdateRunning() && myConverged; }
    void		 terminateRender(bool hard_halt = true);

    bool		 getBoundingBox(UT_BoundingBox &bbox,
				const UT_Matrix3R *rot) const;

    const UT_StringHolder &rendererName() const
			  { return myRendererName; }
    void                  getRendererCommands(
                                UT_StringArray &command_names,
                                UT_StringArray &command_descriptions) const;
    void                  invokeRendererCommand(
                                const UT_StringHolder &command_name) const;

    enum RunningStatus {
	RUNNING_UPDATE_NOT_STARTED = 0,
	RUNNING_UPDATE_IN_BACKGROUND,
	RUNNING_UPDATE_COMPLETE,
        RUNNING_UPDATE_FATAL
    };
    bool		 isUpdateRunning() const;
    bool                 isUpdateComplete() const;

    // Control the pause state of the render. Return true if it is paused.
    // Track pausing invoked by the user separately from "automatic" pausing
    // which happens when switching between Houdini GL and another renderer.
    void                 pauseRender();
    void                 resumeRender();
    bool                 canPause() const;
    bool                 isPausedByUser() const;
    bool                 isStoppedByUser() const;
    bool                 rendererCreated() const;

    // Track whether this object should process updates from the stage. Also
    // controls whether the renderer can be unpaused. We want to prevent the
    // automatic unpausing of the render when the user explicitly pauses it.
    bool                 allowUpdates() const
                         { return myAllowUpdates; }
    void                 setAllowUpdates(bool allow_updates)
                         { myAllowUpdates = allow_updates; }

    static void          initializeAvailableRenderers();
    static bool		 getAvailableRenderers(HUSD_RendererInfoMap &info_map);

    void                 setRenderSettings(const UT_StringRef &settings_path,
                                           int w=0, int h=0);

    const UT_StringArray &rendererPlanes() const { return myPlaneList; }
    bool                 setOutputPlane(const UT_StringRef &name);
    const UT_StringRef  &outputPlane() const { return myOutputPlane; }

    void                 getRenderStats(UT_Options &stats);

    void                 setRenderFocus(int x, int y) const;
    void                 clearRenderFocus() const;

    // Returns the paths associated with render keys from the primid and instid
    // buffers. Stores the result in myRenderKeyToPathMap so future lookups
    // are fast.

    void                 getPrimPathsFromRenderKeys(
                                const UT_Set<HUSD_RenderKey> &keys,
                                HUSD_RenderKeyPathMap &outkeypathmap);

    void		 updateDeferredPrims();

private:
    struct husd_ImagingPrivate;

    void                 resetImagingEngine();
    const HUSD_DataHandle &viewerLopDataHandle() const;
    bool                 updateRestartCameraSettings(bool cam_effects) const;
    bool                 anyRestartRenderSettingsChanged() const;
    bool		 setupRenderer(const UT_StringRef &renderer_name,
                                       const UT_Options *render_opts,
                                       bool cam_effects);
    void                 updateSettingIfRequired(const UT_StringRef &key,
                                const PXR_NS::VtValue &value,
                                bool from_usd_prim = false);
    void                 updateSettingsIfRequired(HUSD_AutoReadLock &lock);
    RunningStatus	 updateRenderData(const UT_Matrix4D &view_matrix,
                                          const UT_Matrix4D &proj_matrix,
                                          const UT_DimRect &viewport_rect,
                                          bool cam_effects);
    void		 finishRender(bool do_render);

    UT_UniquePtr<husd_ImagingPrivate>	 myPrivate;
    fpreal				 myFrame;
    HUSD_DataHandle			 myDataHandle;
    UT_UniquePtr<HUSD_AutoReadLock>      myReadLock;
    HUSD_ConstOverridesPtr		 myOverrides;
    HUSD_ConstPostLayersPtr              myPostLayers;
    unsigned				 myWantsHeadlight : 1,
					 myHasHeadlight : 1,
                                         myWantsDomelight : 1,
                                         myHasDomelight : 1,
					 myDoLighting : 1,
					 myDoMaterials : 1,
					 myConverged : 1,
                                         mySettingsChanged : 1,
                                         myCameraSynced : 1,
                                         myValidRenderSettingsPrim : 1;
    bool                                 myIsPaused;
    bool                                 myAllowUpdates;
    HUSD_Scene				*myScene;
    UT_StringHolder			 myRendererName;
    HUSD_Compositor			*myCompositor;
    PostRenderCallback			 myPostRenderCallback;
    UT_Options				 myCurrentDisplayOptions;
    SYS_AtomicInt32			 myRunningInBackground;
    UT_StringArray                       myPlaneList;
    UT_StringHolder                      myOutputPlane;
    UT_StringHolder                      myCurrentAOV;
    UT_StringHolder                      myCameraPath;
    UT_UniquePtr<PXR_NS::XUSD_RenderSettings> myRenderSettings;
    UT_UniquePtr<husd_DefaultRenderSettingContext> myRenderSettingsContext;
    int                                  myConformPolicy;
    HUSD_DepthStyle                      myDepthStyle;
    BufferSet                            myLastCompositedBufferSet;
    UT_Map<HUSD_RenderKey, UT_StringHolder> myRenderKeyToPathMap;
    fpreal                               myHeadlightIntensity;
};

#endif

