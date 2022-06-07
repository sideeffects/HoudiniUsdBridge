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

#ifndef __HUSD_Preferences_h__
#define __HUSD_Preferences_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_Color.h>
#include <UT/UT_Map.h>
#include <functional>

class HUSD_API HUSD_Preferences
{
public:
    // Returns a string containing the concatenation of all the default paths
    // that are likely to end up containing collections. This string is built
    // by combining other preferences.
    static const UT_StringHolder defaultCollectionsSearchPath();

    static const UT_StringHolder &defaultNewPrimPath();
    static bool			 setDefaultNewPrimPath(
					const UT_StringHolder &path);

    static const UT_StringHolder &defaultCollectionsPrimPath();
    static bool			 setDefaultCollectionsPrimPath(
					const UT_StringHolder &path);

    static const UT_StringHolder &defaultCollectionsPrimType();
    static bool			 setDefaultCollectionsPrimType(
					const UT_StringHolder &primtype);

    static const UT_StringHolder &defaultLightsPrimPath();
    static bool			 setDefaultLightsPrimPath(
					const UT_StringHolder &path);

    static const UT_StringHolder &defaultCamerasPrimPath();
    static bool			 setDefaultCamerasPrimPath(
                                        const UT_StringHolder &path);

    static const UT_StringHolder &defaultTransformSuffix();
    static bool			 setDefaultTransformSuffix(
					const UT_StringHolder &suffix);

    static bool			 showResolvedPaths();
    static bool			 setShowResolvedPaths(
					bool show_resolved_paths);

    static bool			 panesFollowCurrentNode();
    static bool			 setPanesFollowCurrentNode(
					bool follow_current_node);

    static bool			 panesShowViewportStage();
    static bool			 setPanesShowViewportStage(
					bool show_viewport_stage);

    static bool			 panesShowPostLayers();
    static bool			 setPanesShowPostLayers(
                                        bool show_post_layers);

    static bool			 autoSetAssetResolverContext();
    static bool			 setAutoSetAssetResolverContext(
					bool auto_set_context);

    static bool			 updateRendererInBackground();
    static bool			 setUpdateRendererInBackground(
					bool update_in_background);

    static bool			 loadPayloadsByDefault();
    static bool			 setLoadPayloadsByDefault(
					bool load_payloads);

    static bool			 useSimplifiedLinkerUi();
    static bool			 setUseSimplifiedLinkerUi(
					bool use_simplified_linker_ui);

    static bool			 usingHoudiniMetersPerUnit();
    static double		 defaultMetersPerUnit();
    static bool			 setDefaultMetersPerUnit(
					double metersperunit);

    static bool	                 usingUsdUpAxis();
    static UT_StringHolder	 defaultUpAxis();
    static bool			 setDefaultUpAxis(
					const UT_StringHolder &upaxis);

    static bool                  allowViewportOnlyPayloads();
    static bool                  setAllowViewportOnlyPayloads(
                                        bool allow_viewport_only_payloads);

    static bool                  savePrefs();
    static bool                  loadPrefs();

    typedef std::function<void(void)> PrefChangeCallback;
    static int                   addPrefChangeCallback(
                                        PrefChangeCallback callback);
    static void                  removePrefChangeCallback(int id);

private:
    static void                  runPrefChangeCallbacks();

    static UT_StringHolder	 theDefaultNewPrimPath;
    static UT_StringHolder	 theDefaultCollectionsPrimPath;
    static UT_StringHolder	 theDefaultCollectionsPrimType;
    static UT_StringHolder	 theDefaultLightsPrimPath;
    static UT_StringHolder	 theDefaultCamerasPrimPath;
    static UT_StringHolder	 theDefaultTransformSuffix;
    static bool			 theShowResolvedPaths;
    static bool			 thePanesFollowCurrentNode;
    static bool			 thePanesShowViewportStage;
    static bool			 thePanesShowPostLayers;
    static bool			 theAutoSetAssetResolverContext;
    static bool			 theUpdateRendererInBackground;
    static bool			 theLoadPayloadsByDefault;
    static bool			 theUseSimplifiedLinkerUi;
    static double                theDefaultMetersPerUnit;
    static UT_StringHolder       theDefaultUpAxis;
    static bool                  theAllowViewportOnlyPayloads;
    static UT_Map<int, PrefChangeCallback>   thePrefChangeCallbacks;
    static int                               thePrefChangeCallbackId;
};

#endif
