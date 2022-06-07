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

#ifndef __HUSD_RendererInfo_h__
#define __HUSD_RendererInfo_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringMap.h>

enum HUSD_DepthStyle
{
    HUSD_DEPTH_NONE,
    HUSD_DEPTH_NORMALIZED,
    HUSD_DEPTH_LINEAR,
    HUSD_DEPTH_OPENGL
};

/// Parse and provide information from UsdRenderers.json
class HUSD_API HUSD_RendererInfo
{
public:
    // Constructs a default invalid renderer info.
			 HUSD_RendererInfo()
			     : myMenuPriority(0),
			       myDrawComplexityMultiplier(1.0),
			       myIsValid(false),
			       myIsNativeRenderer(false),
			       myDepthStyle(HUSD_DEPTH_NORMALIZED),
			       myNeedsNativeDepthPass(false),
			       myNeedsNativeSelectionPass(false),
			       myAllowBackgroundUpdate(false),
			       myAovSupport(false),
                               myViewportRenderer(false),
                               myDrawModeSupport(false),
			       myHuskFastExit(false)
			 { }
    // Constructs a renderer info with all required information.
			 HUSD_RendererInfo(const UT_StringHolder &name,
				 const UT_StringHolder &displayname,
				 const UT_StringHolder &menulabel,
				 int menupriority,
				 fpreal complexitymultiplier,
				 bool isnative,
				 HUSD_DepthStyle depth_style,
				 const UT_StringArray &defaultpurposes,
				 const UT_StringArray &restartrendersettings,
				 const UT_StringArray &restartcamerasettings,
				 const UT_StringArray &renderstats,
				 bool needsnativedepth,
				 bool needsnativeselection,
				 bool allowbackgroundupdate,
                                 bool aovsupport,
                                 bool viewportrenderer,
                                 bool drawmodesupport,
				 bool husk_fastexit)
			     : myName(name),
			       myDisplayName(displayname),
			       myMenuLabel(menulabel),
			       myMenuPriority(menupriority),
			       myDrawComplexityMultiplier(complexitymultiplier),
			       myIsValid(true),
			       myIsNativeRenderer(isnative),
			       myDepthStyle(depth_style),
			       myDefaultPurposes(defaultpurposes),
                               myRestartRenderSettings(restartrendersettings),
                               myRestartCameraSettings(restartcamerasettings),
                               myRenderViewStats(renderstats),
			       myNeedsNativeDepthPass(needsnativedepth),
			       myNeedsNativeSelectionPass(needsnativeselection),
			       myAllowBackgroundUpdate(allowbackgroundupdate),
			       myAovSupport(aovsupport),
                               myViewportRenderer(viewportrenderer),
			       myDrawModeSupport(drawmodesupport),
			       myHuskFastExit(husk_fastexit)
			 { }

    // The renderer plugin name as registered with HUSD. Something like
    // HdStreamRendererPlugin.
    const UT_StringHolder &name() const
			 { return myName; }
    // The display name registered with USD for this plugin. This may not be
    // the name we want to use in the menu.
    const UT_StringHolder &displayName() const
			 { return myDisplayName; }
    // The name we use in the menu to describe this plugin.
    const UT_StringHolder &menuLabel() const
			 { return myMenuLabel; }
    // Indicates the priority for this plugin to control its location in the
    // renderer menu. Higher numbers show up higher in the menu.
    int			 menuPriority() const
			 { return myMenuPriority; }
    // Specifies a multiplier to use on the Hydra draw complexity calculated
    // from the Display Options Level of Detail.
    fpreal		 drawComplexityMultiplier() const
			 { return myDrawComplexityMultiplier; }
    // Should be true for all plugins. Only false if the default constructor
    // was used and none of the other data in this structure is valid.
    bool		 isValid() const
			 { return myIsValid; }
    // True for the Houdini GL native renderer plugin only.
    bool		 isNativeRenderer() const
			 { return myIsNativeRenderer; }
    // Describes the range used when returning depth information.
    HUSD_DepthStyle	 depthStyle() const
			 { return myDepthStyle; }
    // An array of the render purposes that should be enabled by deafult for
    // this render plugin.
    const UT_StringArray &defaultPurposes() const
			 { return myDefaultPurposes; }
    // Names of render settings that should force the renderer to restart
    // when they are changed.
    const UT_StringArray &restartRenderSettings() const
			 { return myRestartRenderSettings; }
    // Names of camera settings that should force the renderer to restart
    // when they are changed.
    const UT_StringArray &restartCameraSettings() const
			 { return myRestartCameraSettings; }
    // Names of render statistics printed in the viewport when view stats is on
    const UT_StringArray &renderViewStats() const
			 { return myRenderViewStats; }
    // True if this plugin needs the native GL renderer to provide a depth
    // map for the render.
    bool		 needsNativeDepthPass() const
			 { return myNeedsNativeDepthPass; }
    // True if this plugin needs the native GL renderer to provide an overlay
    // to highlight selected primitives.
    bool		 needsNativeSelectionPass() const
			 { return myNeedsNativeSelectionPass; }
    // True if this plugin allows Houdini to run scene graph update processing
    // on a background thread.
    bool		 allowBackgroundUpdate() const
			 { return myAllowBackgroundUpdate; }
    // True if this plugin is able to generate AOV buffers.
    bool		 aovSupport() const
			 { return myAovSupport; }
    // True if this plugin does its own viewport rendering.
    bool		 viewportRenderer() const
			 { return myViewportRenderer; }
    // True if this plugin supports USD draw modes.
    bool		 drawModeSupport() const
			 { return myDrawModeSupport; }
    // Return whether husk.fast-exit is set
    bool		 huskFastExit() const
			 { return myHuskFastExit; }

    static HUSD_RendererInfo getRendererInfo(
				const UT_StringHolder &name,
				const UT_StringHolder &displayname);

private:
    UT_StringHolder	 myName;
    UT_StringHolder	 myDisplayName;
    UT_StringHolder	 myMenuLabel;
    int			 myMenuPriority;
    fpreal		 myDrawComplexityMultiplier;
    HUSD_DepthStyle	 myDepthStyle;
    UT_StringArray	 myDefaultPurposes;
    UT_StringArray       myRestartRenderSettings;
    UT_StringArray       myRestartCameraSettings;
    UT_StringArray       myRenderViewStats;
    bool		 myIsValid;
    bool		 myIsNativeRenderer;
    bool		 myNeedsNativeDepthPass;
    bool		 myNeedsNativeSelectionPass;
    bool		 myAllowBackgroundUpdate;
    bool		 myAovSupport;
    bool		 myViewportRenderer;
    bool		 myDrawModeSupport;
    bool		 myHuskFastExit;
};

typedef UT_StringMap<HUSD_RendererInfo> HUSD_RendererInfoMap;

#endif

