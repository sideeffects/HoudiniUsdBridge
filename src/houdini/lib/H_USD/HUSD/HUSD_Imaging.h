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
#include "HUSD_RendererInfo.h"
#include "HUSD_Scene.h"
#include <UT/UT_NonCopyable.h>
#include <UT/UT_BoundingBox.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Options.h>
#include <UT/UT_Rect.h>
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
				const HUSD_ConstOverridesPtr &overrides);
    void		 setSelection(const UT_StringArray &paths);
    bool		 setFrame(fpreal frame);
    bool		 setHeadlight(bool doheadlight);
    void		 setLighting(bool enable);

    enum BufferSet
    {
        BUFFER_COLOR_DEPTH,
        BUFFER_COLOR,
        BUFFER_NONE,
    };
    BufferSet            hasAOVBuffers() const;
    
    bool                 canBackgroundRender(const UT_StringRef &name) const;

    // Fire off a render and return immediately.
    // Only call if canBackgroundRender() returns true.
    bool                 launchBackgroundRender(const UT_Matrix4D &view_matrix,
                                                const UT_Matrix4D &proj_matrix,
                                                const UT_DimRect  &viewport_rect,
                                                const UT_StringRef &renderer,
                                                const UT_Options *render_opts,
                                                bool update_deferred = false);
    // Wait for the BG update to be finished.
    void                 waitForUpdateToComplete();
    // Check if the BG update is finished, and optionally do a render if it is.
    bool                 checkRender(bool do_render);

    void                 updateComposite(bool free_buffers_if_missing);


    // Fire off a render and block until done. It may return false if the
    // render delegate fails to initialize, it which case another delegate
    // should be chosen.
    bool		 render(const UT_Matrix4D &view_matrix,
				const UT_Matrix4D &proj_matrix,
				const UT_DimRect  &viewport_rect,
				const UT_StringRef &renderer,
				const UT_Options *render_opts,
                                bool update_deferred);
    
    // Set the camera being viewed through (can be null for no camera);
    void                 setCameraPath(const UT_StringRef &path,
                                       bool sampling_only = false)
                         {
                             if(path != myCameraPath)
                             {
                                 myCameraPath = path;
                                 mySettingsChanged = true;
                             }
                             // use camera for sampling parms, not frustum.
                             if(sampling_only != myCameraSamplingOnly)
                             {
                                 myCameraSamplingOnly = sampling_only;
                                 mySettingsChanged = true;
                             }
                         }
    
    void		 setAOVCompositor(HUSD_Compositor *comp)
			 { myCompositor = comp; }

    HUSD_Scene		&scene()
			 { return *myScene; }
    bool		 isConverged() const
			 { return !running() && myConverged; }
    void		 terminateRender(bool hard_halt = true);

    bool		 getBoundingBox(UT_BoundingBox &bbox,
				const UT_Matrix3R *rot) const;

    const UT_StringHolder &rendererName() const
			 { return myRendererName; }

    enum RunningStatus {
	RUNNING_UPDATE_NOT_STARTED = 0,
	RUNNING_UPDATE_IN_BACKGROUND,
	RUNNING_UPDATE_COMPLETE,
        RUNNING_UPDATE_FATAL
    };
    bool		 running() const;
    bool                 isComplete() const;

    // Pause render. Return true if it is paused.
    bool                 pauseRender();
    // Resume a paused render.
    void                 resumeRender();
    bool                 canPause() const;
    bool                 isPaused() const;

    static bool		 getAvailableRenderers(HUSD_RendererInfoMap &info_map);

    void                 setRenderSettings(const UT_StringRef &settings_path,
                                           int w=0, int h=0);

    const UT_StringArray &rendererPlanes() const { return myPlaneList; }
    bool                 setOutputPlane(const UT_StringRef &name);
    const UT_StringRef  &outputPlane() const { return myOutputPlane; }

    void                 getRenderStats(UT_Options &stats);

    // Returns the path associated with a ID from a primId buffer.
    UT_StringHolder      lookupID(int path_id,
                                  int inst_id,
                                  bool pick_instance) const;

private:
    class husd_ImagingPrivate;

    bool                 isRestartSetting(const UT_StringRef &key,
                                const UT_StringArray &restartsettings) const;
    bool                 isRestartSettingChanged(const UT_StringRef &key,
                                const PXR_NS::VtValue &vtvalue,
                                const UT_StringArray &restartsettings) const;
    bool                 anyRestartRenderSettingsChanged() const;
    void		 updateLightsAndCameras();
    void		 updateDeferredPrims();
    bool		 setupRenderer(const UT_StringRef &renderer_name,
                                const UT_Options *render_opts);
    void                 updateSettingIfRequired(const UT_StringRef &key,
                                const PXR_NS::VtValue &value);
    void                 updateSettingsIfRequired();
    RunningStatus	 updateRenderData(const UT_Matrix4D &view_matrix,
                                          const UT_Matrix4D &proj_matrix,
                                          const UT_DimRect &viewport_rect,
                                          bool update_deferred);
    void		 finishRender(bool do_render);

    UT_UniquePtr<husd_ImagingPrivate>	 myPrivate;
    fpreal				 myFrame;
    HUSD_DataHandle			 myDataHandle;
    HUSD_ConstOverridesPtr		 myOverrides;
    UT_StringArray			 mySelection;
    unsigned				 myWantsHeadlight : 1,
					 myHasHeadlight : 1,
					 myDoLighting : 1,
					 myHasLightCamPrims : 1,
					 myHasGeomPrims : 1,
					 mySelectionNeedsUpdate : 1,
					 myConverged : 1,
                                         mySettingsChanged : 1,
                                         myIsPaused : 1,
                                         myCameraSamplingOnly : 1,
                                         myValidRenderSettings : 1;
    HUSD_Scene				*myScene;
    UT_StringHolder			 myRendererName;
    HUSD_Compositor			*myCompositor;
    UT_Options				 myCurrentOptions;
    SYS_AtomicInt32			 myRunningInBackground;
    UT_UniquePtr<HUSD_AutoReadLock>	 myReadLock;
    UT_StringArray                       myPlaneList;
    UT_StringHolder                      myOutputPlane;
    UT_StringHolder                      myCurrentAOV;
    UT_StringHolder                      myCameraPath;
    PXR_NS::XUSD_RenderSettings         *myRenderSettingsPtr;
    PXR_NS::XUSD_RenderSettings         *myRenderSettings;
    husd_DefaultRenderSettingContext    *myRenderSettingsContext;
};

#endif

