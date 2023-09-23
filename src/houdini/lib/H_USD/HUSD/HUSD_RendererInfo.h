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

    // Constructs a default invalid renderer info.
    HUSD_RendererInfo()
        : myMenuPriority(0)
        , myDrawComplexityMultiplier(1.0)
        , myIsValid(false)
        , myIsNativeRenderer(false)
        , myDepthStyle(HUSD_DEPTH_NORMALIZED)
        , myNeedsNativeDepthPass(false)
        , myNeedsNativeSelectionPass(false)
        , myAllowBackgroundUpdate(false)
        , myAovSupport(false)
        , myViewportRenderer(false)
        , myDrawModeSupport(false)
        , myHuskFastExit(false)
        , myHuskVerboseInterval(0)
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
                const HuskMetadata &husk_metadata,
                const StatsDataPaths &statsdatapaths,
                const UT_StringHolder &husk_verbose_script,
                fpreal husk_verbose_interval,
                bool needsnativedepth,
                bool needsnativeselection,
                bool allowbackgroundupdate,
                bool aovsupport,
                bool viewportrenderer,
                bool drawmodesupport,
                bool husk_fastexit)
         : myName(name)
         , myDisplayName(displayname)
         , myMenuLabel(menulabel)
         , myMenuPriority(menupriority)
         , myDrawComplexityMultiplier(complexitymultiplier)
         , myIsValid(true)
         , myIsNativeRenderer(isnative)
         , myDepthStyle(depth_style)
         , myDefaultPurposes(defaultpurposes)
         , myRestartRenderSettings(restartrendersettings)
         , myRestartCameraSettings(restartcamerasettings)
         , myRenderViewStats(renderstats)
         , myHuskMetadata(husk_metadata)
         , myStatsDataPaths(statsdatapaths)
         , myHuskVerboseScript(husk_verbose_script)
         , myHuskVerboseInterval(husk_verbose_interval)
         , myNeedsNativeDepthPass(needsnativedepth)
         , myNeedsNativeSelectionPass(needsnativeselection)
         , myAllowBackgroundUpdate(allowbackgroundupdate)
         , myAovSupport(aovsupport)
         , myViewportRenderer(viewportrenderer)
         , myDrawModeSupport(drawmodesupport)
         , myHuskFastExit(husk_fastexit)
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
    bool	         huskFastExit() const
			 { return myHuskFastExit; }

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
                             { return myHuskMetadata; }

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
                             { return myStatsDataPaths; }

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

    /// Python script used by husk for verbose callbacks
    const UT_StringHolder   &huskVerboseScript() const
                             { return myHuskVerboseScript; }
    fpreal                   huskVerboseInterval() const
                             { return myHuskVerboseInterval; }

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
    StatsDataPaths       myStatsDataPaths;
    HuskMetadata         myHuskMetadata;
    UT_StringHolder      myHuskVerboseScript;
    fpreal               myHuskVerboseInterval;
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

