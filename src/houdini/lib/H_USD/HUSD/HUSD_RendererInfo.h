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
#include <UT/UT_OptionEntry.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_StringSet.h>

class UT_Options;
class UT_JSONValue;

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
    using StatsDataPaths = UT_StringMap<UT_StringHolder>;
    using HuskMetadata = UT_StringMap<UT_StringHolder>;

    // The renderer plugin name as registered with HUSD. Something like
    // HdStreamRendererPlugin.
    const UT_StringHolder &name() const
			 { UT_ASSERT(myIsValid); return myName; }
    // The display name registered with USD for this plugin. This may not be
    // the name we want to use in the menu.
    const UT_StringHolder &displayName() const
			 { UT_ASSERT(myIsValid); return myDisplayName; }
    // The name we use in the menu to describe this plugin.
    const UT_StringHolder &menuLabel() const
			 { UT_ASSERT(myIsValid); return myMenuLabel; }
    // Indicates the priority for this plugin to control its location in the
    // renderer menu. Higher numbers show up higher in the menu.
    int			 menuPriority() const
			 { UT_ASSERT(myIsValid); return myMenuPriority; }
    // Specifies a multiplier to use on the Hydra draw complexity calculated
    // from the Display Options Level of Detail.
    fpreal		 drawComplexityMultiplier() const
			 { UT_ASSERT(myIsValid); return myDrawComplexityMultiplier; }
    // Should be true for all plugins. Only false if the default constructor
    // was used and none of the other data in this structure is valid.
    bool		 isValid() const
			 { return myIsValid; }
    // True for the Houdini GL native renderer plugin only.
    bool		 isNativeRenderer() const
			 { UT_ASSERT(myIsValid); return myIsNativeRenderer; }
    // Describes the range used when returning depth information.
    HUSD_DepthStyle	 depthStyle() const
			 { UT_ASSERT(myIsValid); return myDepthStyle; }
    // An array of the render purposes that should be enabled by default for
    // this render plugin.
    const UT_StringArray &defaultPurposes() const
			 { UT_ASSERT(myIsValid); return myDefaultPurposes; }
    // Names of render settings that should force the renderer to restart
    // when they are changed.
    const UT_StringArray &restartRenderSettings() const
			 { UT_ASSERT(myIsValid); return myRestartRenderSettings; }
    // Names of camera settings that should force the renderer to restart
    // when they are changed.
    const UT_StringArray &restartCameraSettings() const
			 { UT_ASSERT(myIsValid); return myRestartCameraSettings; }
    // Names of render statistics printed in the viewport when view stats is on
    const UT_StringArray &renderViewStats() const
			 { UT_ASSERT(myIsValid); return myRenderViewStats; }
    // True if this plugin needs the native GL renderer to provide a depth
    // map for the render.
    bool		 needsNativeDepthPass() const
			 { UT_ASSERT(myIsValid); return myNeedsNativeDepthPass; }
    // True if this plugin needs the native GL renderer to provide an overlay
    // to highlight selected primitives.
    bool		 needsNativeSelectionPass() const
			 { UT_ASSERT(myIsValid); return myNeedsNativeSelectionPass; }
    // True if this plugin allows Houdini to run scene graph update processing
    // on a background thread.
    bool		 allowBackgroundUpdate() const
			 { UT_ASSERT(myIsValid); return myAllowBackgroundUpdate; }
    // True if this plugin requires Houdini to tear it down if the viewport
    // switches to a different active renderer.
    bool		 destroyIfDeactivated() const
                         { UT_ASSERT(myIsValid); return myDestroyIfDeactivated; }
    // True if this plugin should pause while processing update. Should be set
    // if the renderer can read COP textures to avoid potential deadlocks.
    bool		 pauseOnUpdate() const
                         { UT_ASSERT(myIsValid); return myPauseOnUpdate; }
    // True if this plugin is able to generate AOV buffers.
    bool		 aovSupport() const
			 { UT_ASSERT(myIsValid); return myAovSupport; }
    // True if this plugin does its own viewport rendering.
    bool		 viewportRenderer() const
			 { UT_ASSERT(myIsValid); return myViewportRenderer; }
    // True if this plugin supports USD draw modes.
    bool		 drawModeSupport() const
			 { UT_ASSERT(myIsValid); return myDrawModeSupport; }
    // Return whether husk.fast-exit is set
    bool	         huskFastExit() const
			 { UT_ASSERT(myIsValid); return myHuskFastExit; }
    // Return whether this renderer should appear in the viewport renderer menu.
    bool	         showInViewportMenu() const
                         { UT_ASSERT(myIsValid); return myShowInViewportMenu; }
    // Return whether the viewport should mute Hydra Generative Procedurals
    // for this render delegate.
    bool	         showHydraProcedurals() const
                         { UT_ASSERT(myIsValid); return myShowHydraProcedurals; }
    // Product types which write to the file system.  When husk encounters
    // these product types, it will check for writeable directories and use the
    // --make-output-path option automatically.
    const UT_StringSet &diskProductTypes() const
                         { UT_ASSERT(myIsValid); return myDiskProductTypes; }

    /// Return the husk.metadata map.  This map is used by husk to add metadata
    /// when saving images.  The metadata keys are specific to the format (see
    /// "iconvert --help").  When using the multi-part EXR writer, arbitrary
    /// typed metadata can also be saved (see the HDK documentation for more
    /// details), but examples might be "string OpenEXR:Software" or "mat4d
    /// OpenEXR:custom_matrix".
    ///
    /// Husk provides a JSON dictionary of metadata values which can be
    /// referenced in the value of the metadata map.  The JSON dictionary will
    /// look something like: @code
    /// {
    ///   "frame" : 42,
    ///   "command_line" : "husk -f 42 foo.usd",
    ///   "render_stats" : { "render_time" : [3.42, 0.24, 1.32] },
    ///    ...
    /// }
    /// @endcode
    /// A delegate can specify metadata as either verbatim text or by expanding
    /// data referenced in the JSON dictionary (using the JSON Path syntax).
    /// For example:
    /// - "float OpenEXR:frame" : "${frame}"
    /// - "float OpenEXR:load_time_cpu" : "${render_stats.render_time[0]}" @n
    ///    Extracts the first time from the render_time array
    /// - "float OpenEXR:load_time_sys" : "${render_stats.render_time[1]}"
    /// - "float OpenEXR:load_time_wall" : "${render_stats.render_time[2]}"
    /// - "string OpenEXR:stats_json" : "${render_stats}"
    ///    Encodes all the render_stats as a string in JSON format
    ///
    /// @note that the render stats mapping is not used when performing render
    /// stat lookup.
    const HuskMetadata      &huskMetadata() const
                             { UT_ASSERT(myIsValid); return myHuskMetadata; }

    /// Some delegates prefer to pass metadata from GetRenderStats() directly
    /// to image metadata.  This option passes a string pattern (see
    /// UT_String::multiMatch) for render stats which should be stored as
    /// metadata directly.  This defaults to `*` (meaning all the stats from
    /// GetRenderStats will be stored as metadata).  If you set
    /// `husk.metadata`, you probably want to set this to an empty string.
    const UT_StringHolder   &huskStatsMetadata() const
                             { UT_ASSERT(myIsValid); return myHuskStatsMetadata; }

    /// Similar to the husk metadata, this returns the statsdatapaths, which
    /// gives the JSON path to the render stat required by the viewer or husk.
    /// Currently thses are:
    /// - int peakMemory:  The peak memory usage
    /// - float percentDone: The percent complete (0 to 100)
    /// - float totalClockTime: The wall clock time taken to render
    /// - float totalUTime: The CPU time taken to render
    /// - float totalSTime: The system time taken to render
    /// - string renderProgressAnnotation: multi-line renderer status
    /// - string renderStatsAnnotation: multi-line renderer status
    /// - string rendererStage: The current stage of rendering for the
    ///     delegate.  This might be something like "displacing", "loading
    ///     textures", "rendering", etc.
    /// - string rendererName: The name of the delegate (defaults to menuLabel())
    ///
    /// In addition, each delegate may also specify a list of custom labels in
    /// the "viewstats" item.
    ///
    /// One major difference between this and the husk.metadata is that for
    /// this setting, the value in the pair is a direct JSON Path (rather than
    /// being a string that undergoes variable expansion.
    const StatsDataPaths    &statsDataPaths() const
                             { UT_ASSERT(myIsValid); return myStatsDataPaths; }

    /// Python script used by husk for verbose callbacks
    const UT_StringHolder   &huskVerboseScript() const
                             { UT_ASSERT(myIsValid); return myHuskVerboseScript; }
    fpreal                   huskVerboseInterval() const
                             { UT_ASSERT(myIsValid); return myHuskVerboseInterval; }

    /// Default output name for husk (if there are no render settings)
    const UT_StringHolder   &huskDefaultOutput() const
                             { UT_ASSERT(myIsValid); return myHuskDefaultOutput; }

    /// Get standard renderer info for a particular render delegate. Either
    /// the internal renderer name or the display name can be provided. The
    /// other parameter can be an empty string.
    static HUSD_RendererInfo getRendererInfo(
				    const UT_StringHolder &name,
				    const UT_StringHolder &displayname);
    /// Get renderer info for a particular render delegate, and also extract
    /// custom data. The "custom" map on input should contain empty entries
    /// for all extra data of interest. On output, the map will be filled with
    /// the values associated with these keys extracted from the
    /// UsdRenderers.json file.
    static HUSD_RendererInfo getRendererInfo(
                                    const UT_StringHolder &name,
                                    const UT_StringHolder &displayname,
                                    UT_StringMap<UT_OptionEntryPtr> &custom);

    /// Convenience method to fill out a UT_Options with all the stats data
    /// required for the delegate
    void                     extractStatsData(UT_Options &options,
                                    const UT_JSONValue &stats_dictionary) const;

    /// Convenience method to find a JSON Value for a given key
    const UT_JSONValue      *findStatsData(const UT_JSONValue &stats_dict,
                                    const char *key) const;

     /// Before doing anything that may cause this render delegate's library
     /// to be loaded, call this method to make sure any required libraries
     /// (most likely libpxr libraries) are already loaded. This saves third
     /// party libraries from having to worry about LD_LIBRARY_PATH.
     void                    preloadLibraries() const;

private:
    bool             myIsValid                   = false;
    UT_StringHolder  myName;
    UT_StringHolder  myDisplayName;
    UT_StringHolder  myMenuLabel;
    int              myMenuPriority              = 0;
    fpreal           myDrawComplexityMultiplier  = 1.f;
    HUSD_DepthStyle  myDepthStyle                = HUSD_DEPTH_OPENGL;
    UT_StringArray   myDefaultPurposes           = UT_StringArray({ "render" });
    UT_StringArray   myRestartRenderSettings;
    UT_StringArray   myRestartCameraSettings;
    UT_StringArray   myRenderViewStats;
    StatsDataPaths   myStatsDataPaths;
    HuskMetadata     myHuskMetadata;
    UT_StringHolder  myHuskStatsMetadata;
    UT_StringHolder  myHuskDefaultOutput;
    UT_StringHolder  myHuskVerboseScript;
    fpreal           myHuskVerboseInterval       = 0.f;
    UT_StringSet     myDiskProductTypes;
    UT_StringArray   myPreloadLibraries;
    bool             myIsNativeRenderer          = false;
    bool             myNeedsNativeDepthPass      = false;
    bool             myNeedsNativeSelectionPass  = false;
    bool             myAllowBackgroundUpdate     = true;
    bool             myDestroyIfDeactivated      = false;
    bool             myPauseOnUpdate             = true;
    bool             myAovSupport                = true;
    bool             myViewportRenderer          = false;
    bool             myDrawModeSupport           = false;
    bool             myHuskFastExit              = false;
    bool             myShowInViewportMenu        = true;
    bool             myShowHydraProcedurals      = true;
};

typedef UT_StringMap<HUSD_RendererInfo> HUSD_RendererInfoMap;

#endif

