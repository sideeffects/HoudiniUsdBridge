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

#include "HUSD_Preferences.h"
#include <CH/CH_Manager.h>
#include <FS/UT_PathFile.h>
#include <UT/UT_OptionFile.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_WorkBuffer.h>
#include <tools/henv.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

#define HUSD_PREFERENCES_FILE "solaris.pref"

static UT_StringHolder	 theFactoryDefaultNewPrimPath = "/$OS";
static UT_StringHolder	 theFactoryDefaultCollectionsPrimPath="/collections";
static UT_StringHolder	 theFactoryDefaultCollectionsPrimType="";
static UT_StringHolder	 theFactoryDefaultLightsPrimPath="/lights";
static UT_StringHolder	 theFactoryDefaultCamerasPrimPath="/cameras";
static UT_StringHolder	 theFactoryDefaultTransformSuffix = "$OS";

UT_StringHolder	 HUSD_Preferences::theDefaultNewPrimPath =
                         theFactoryDefaultNewPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultCollectionsPrimPath =
                         theFactoryDefaultCollectionsPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultCollectionsPrimType =
                         theFactoryDefaultCollectionsPrimType;
UT_StringHolder	 HUSD_Preferences::theDefaultLightsPrimPath =
                         theFactoryDefaultLightsPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultCamerasPrimPath =
                         theFactoryDefaultCamerasPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultTransformSuffix =
                         theFactoryDefaultTransformSuffix;
bool		 HUSD_Preferences::theShowResolvedPaths = false;
bool		 HUSD_Preferences::thePanesFollowCurrentNode = true;
bool		 HUSD_Preferences::thePanesShowViewportStage = false;
bool		 HUSD_Preferences::thePanesShowPostLayers = true;
bool		 HUSD_Preferences::theAutoSetAssetResolverContext = false;
bool		 HUSD_Preferences::theUpdateRendererInBackground = true;
bool		 HUSD_Preferences::theLoadPayloadsByDefault = true;
bool		 HUSD_Preferences::theUseSimplifiedLinkerUi = false;
double           HUSD_Preferences::theDefaultMetersPerUnit = 0.0;
UT_StringHolder  HUSD_Preferences::theDefaultUpAxis = "";
bool		 HUSD_Preferences::theAllowViewportOnlyPayloads = true;
UT_Map<int, HUSD_Preferences::PrefChangeCallback>
                 HUSD_Preferences::thePrefChangeCallbacks;
int              HUSD_Preferences::thePrefChangeCallbackId = 0;

#define HUSD_PREF_SHOWRESOLVEDPATHS "showresolvedpaths"
#define HUSD_PREF_PANESFOLLOWCURRENTNODE "panesfollowcurrentnode"
#define HUSD_PREF_PANESSHOWVIEWPORTSTAGE "panesshowviewportstage"
#define HUSD_PREF_PANESSHOWPOSTLAYERS "panesshowpostlayers"
#define HUSD_PREF_USESIMPLIFIEDLINKERUI "usesimplifiedlinkerui"
#define HUSD_PREF_AUTOSETASSETRESOLVERCONTEXT "autosetassetresolvercontext"
#define HUSD_PREF_LOADPAYLOADSBYDEFAULT "loadpayloadsbydefault"
#define HUSD_PREF_ALLOWVIEWPORTONLYPAYLOADS "allowviewportonlypayloads"
#define HUSD_PREF_DEFAULTNEWPRIMPATH "defaultnewprimpath"
#define HUSD_PREF_DEFAULTCOLLECTIONSPRIMPATH "defaultcollectionsprimpath"
#define HUSD_PREF_DEFAULTCOLLECTIONSPRIMTYPE "defaultcollectionsprimtype"
#define HUSD_PREF_DEFAULTLIGHTSPRIMPATH "defaultlightsprimpath"
#define HUSD_PREF_DEFAULTCAMERASPRIMPATH "defaultcamerasprimpath"
#define HUSD_PREF_DEFAULTTRANSFORMSUFFIX "defaulttransformsuffix"
#define HUSD_PREF_DEFAULTMETERSPERUNIT "defaultmetersperunit"
#define HUSD_PREF_DEFAULTUPAXIS "defaultupaxis"

const UT_StringHolder
HUSD_Preferences::defaultCollectionsSearchPath()
{
    UT_WorkBuffer    buf;
    UT_StringHolder  result;

    buf.append(defaultCollectionsPrimPath());
    buf.append(' ');
    buf.append(defaultLightsPrimPath());

    result = std::move(buf);

    return result;
}

const UT_StringHolder &
HUSD_Preferences::defaultNewPrimPath()
{
    return theDefaultNewPrimPath;
}

bool
HUSD_Preferences::setDefaultNewPrimPath(const UT_StringHolder &path)
{
    if (theDefaultNewPrimPath == path)
        return false;
    if (path.isEmpty() &&
        theDefaultNewPrimPath == theFactoryDefaultNewPrimPath)
        return false;

    if (path.isstring())
        theDefaultNewPrimPath = path;
    else
        theDefaultNewPrimPath = theFactoryDefaultNewPrimPath;

    runPrefChangeCallbacks();
    return true;
}

const UT_StringHolder &
HUSD_Preferences::defaultCollectionsPrimPath()
{
    return theDefaultCollectionsPrimPath;
}

bool
HUSD_Preferences::setDefaultCollectionsPrimPath(const UT_StringHolder &path)
{
    if (theDefaultCollectionsPrimPath == path)
        return false;
    if (path.isEmpty() &&
        theDefaultCollectionsPrimPath == theFactoryDefaultCollectionsPrimPath)
        return false;

    if (path.isstring())
        theDefaultCollectionsPrimPath = path;
    else
        theDefaultCollectionsPrimPath = theFactoryDefaultCollectionsPrimPath;

    runPrefChangeCallbacks();
    return true;
}

const UT_StringHolder &
HUSD_Preferences::defaultCollectionsPrimType()
{
    return theDefaultCollectionsPrimType;
}

bool
HUSD_Preferences::setDefaultCollectionsPrimType(const UT_StringHolder &primtype)
{
    if (theDefaultCollectionsPrimType == primtype)
        return false;

    theDefaultCollectionsPrimType = primtype;

    runPrefChangeCallbacks();
    return true;
}

const UT_StringHolder &
HUSD_Preferences::defaultLightsPrimPath()
{
    return theDefaultLightsPrimPath;
}

bool
HUSD_Preferences::setDefaultLightsPrimPath(const UT_StringHolder &path)
{
    if (theDefaultLightsPrimPath == path)
        return false;
    if (path.isEmpty() &&
        theDefaultLightsPrimPath == theFactoryDefaultLightsPrimPath)
        return false;

    if (path.isstring())
        theDefaultLightsPrimPath = path;
    else
        theDefaultLightsPrimPath = theFactoryDefaultLightsPrimPath;

    runPrefChangeCallbacks();
    return true;
}

const UT_StringHolder &
HUSD_Preferences::defaultCamerasPrimPath()
{
    return theDefaultCamerasPrimPath;
}

bool
HUSD_Preferences::setDefaultCamerasPrimPath(const UT_StringHolder &path)
{
    if (theDefaultCamerasPrimPath == path)
        return false;
    if (path.isEmpty() &&
        theDefaultCamerasPrimPath == theFactoryDefaultCamerasPrimPath)
        return false;

    if (path.isstring())
        theDefaultCamerasPrimPath = path;
    else
        theDefaultCamerasPrimPath = theFactoryDefaultCamerasPrimPath;

    runPrefChangeCallbacks();
    return true;
}

const UT_StringHolder &
HUSD_Preferences::defaultTransformSuffix()
{
    return theDefaultTransformSuffix;
}

bool
HUSD_Preferences::setDefaultTransformSuffix(const UT_StringHolder &suffix)
{
    if (theDefaultTransformSuffix == suffix)
        return false;
    if (suffix.isEmpty() &&
        theDefaultTransformSuffix == theFactoryDefaultTransformSuffix)
        return false;

    if (suffix.isstring())
        theDefaultTransformSuffix = suffix;
    else
        theDefaultTransformSuffix = theFactoryDefaultTransformSuffix;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::showResolvedPaths()
{
    return theShowResolvedPaths;
}

bool
HUSD_Preferences::setShowResolvedPaths(bool show_resolved_paths)
{
    if (theShowResolvedPaths == show_resolved_paths)
        return false;

    theShowResolvedPaths = show_resolved_paths;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::panesFollowCurrentNode()
{
    return thePanesFollowCurrentNode;
}

bool
HUSD_Preferences::setPanesFollowCurrentNode(bool follow_current_node)
{
    if (thePanesFollowCurrentNode == follow_current_node)
        return false;

    thePanesFollowCurrentNode = follow_current_node;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::panesShowViewportStage()
{
    return thePanesShowViewportStage;
}

bool
HUSD_Preferences::setPanesShowViewportStage(bool show_viewport_stage)
{
    if (thePanesShowViewportStage == show_viewport_stage)
        return false;

    thePanesShowViewportStage = show_viewport_stage;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::panesShowPostLayers()
{
    return thePanesShowPostLayers;
}

bool
HUSD_Preferences::setPanesShowPostLayers(bool show_post_layers)
{
    if (thePanesShowPostLayers == show_post_layers)
        return false;

    thePanesShowPostLayers = show_post_layers;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::autoSetAssetResolverContext()
{
    return theAutoSetAssetResolverContext;
}

bool
HUSD_Preferences::setAutoSetAssetResolverContext(bool auto_set_context)
{
    if (theAutoSetAssetResolverContext == auto_set_context)
        return false;

    theAutoSetAssetResolverContext = auto_set_context;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::updateRendererInBackground()
{
    return theUpdateRendererInBackground;
}

bool
HUSD_Preferences::setUpdateRendererInBackground(bool update_in_background)
{
    if (theUpdateRendererInBackground == update_in_background)
        return false;

    theUpdateRendererInBackground = update_in_background;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::loadPayloadsByDefault()
{
    return theLoadPayloadsByDefault;
}

bool
HUSD_Preferences::setLoadPayloadsByDefault(bool load_payloads)
{
    if (theLoadPayloadsByDefault == load_payloads)
        return false;

    theLoadPayloadsByDefault = load_payloads;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::useSimplifiedLinkerUi()
{
    return theUseSimplifiedLinkerUi;
}

bool
HUSD_Preferences::setUseSimplifiedLinkerUi(bool use_simplified_linker_ui)
{
    if (theUseSimplifiedLinkerUi == use_simplified_linker_ui)
        return false;

    theUseSimplifiedLinkerUi = use_simplified_linker_ui;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::usingHoudiniMetersPerUnit()
{
    // A value of zero means "use the value set in the Houdini length unit".
    return (theDefaultMetersPerUnit == 0.0);
}

double
HUSD_Preferences::defaultMetersPerUnit()
{
    if (usingHoudiniMetersPerUnit())
        return CHgetManager()->getUnitLength();

    return theDefaultMetersPerUnit;
}

bool
HUSD_Preferences::setDefaultMetersPerUnit(double metersperunit)
{
    if (theDefaultMetersPerUnit == metersperunit)
        return false;

    theDefaultMetersPerUnit = metersperunit;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::usingUsdUpAxis()
{
    // An empty string means "use the value set in the USD registry".
    return !theDefaultUpAxis.isstring();
}

UT_StringHolder
HUSD_Preferences::defaultUpAxis()
{
    if (usingUsdUpAxis())
        return UsdGeomGetFallbackUpAxis().GetString();

    return theDefaultUpAxis;
}

bool
HUSD_Preferences::setDefaultUpAxis(const UT_StringHolder &upaxis)
{
    if (theDefaultUpAxis == upaxis)
        return false;

    // Only an empty string, "Y", and "Z" are acceptable up axis values.
    if (!upaxis.isstring() || upaxis == "Y" || upaxis == "Z")
    {
        theDefaultUpAxis = upaxis;
        runPrefChangeCallbacks();
        return true;
    }

    return false;
}

bool
HUSD_Preferences::allowViewportOnlyPayloads()
{
    return theAllowViewportOnlyPayloads;
}

bool
HUSD_Preferences::setAllowViewportOnlyPayloads(
        bool allow_viewport_only_payloads)
{
    if (theAllowViewportOnlyPayloads == allow_viewport_only_payloads)
        return false;

    theAllowViewportOnlyPayloads = allow_viewport_only_payloads;

    runPrefChangeCallbacks();
    return true;
}

bool
HUSD_Preferences::savePrefs()
{
    UT_OptionFile    ofile;
    UT_String        filename;

    UT_PathSearch::getHomeHoudini(filename);
    filename += "/" HUSD_PREFERENCES_FILE;

    ofile.setOption(HUSD_PREF_SHOWRESOLVEDPATHS,
        showResolvedPaths());
    ofile.setOption(HUSD_PREF_PANESFOLLOWCURRENTNODE,
        panesFollowCurrentNode());
    ofile.setOption(HUSD_PREF_PANESSHOWVIEWPORTSTAGE,
        panesShowViewportStage());
    ofile.setOption(HUSD_PREF_PANESSHOWPOSTLAYERS,
        panesShowPostLayers());
    ofile.setOption(HUSD_PREF_USESIMPLIFIEDLINKERUI,
        useSimplifiedLinkerUi());
    ofile.setOption(HUSD_PREF_AUTOSETASSETRESOLVERCONTEXT,
        autoSetAssetResolverContext());
    ofile.setOption(HUSD_PREF_LOADPAYLOADSBYDEFAULT,
        loadPayloadsByDefault());
    ofile.setOption(HUSD_PREF_ALLOWVIEWPORTONLYPAYLOADS,
        allowViewportOnlyPayloads());
    ofile.setOption(HUSD_PREF_DEFAULTNEWPRIMPATH,
        defaultNewPrimPath());
    ofile.setOption(HUSD_PREF_DEFAULTCOLLECTIONSPRIMPATH,
        defaultCollectionsPrimPath());
    ofile.setOption(HUSD_PREF_DEFAULTCOLLECTIONSPRIMTYPE,
        defaultCollectionsPrimType());
    ofile.setOption(HUSD_PREF_DEFAULTLIGHTSPRIMPATH,
        defaultLightsPrimPath());
    ofile.setOption(HUSD_PREF_DEFAULTCAMERASPRIMPATH,
        defaultCamerasPrimPath());
    ofile.setOption(HUSD_PREF_DEFAULTTRANSFORMSUFFIX,
        defaultTransformSuffix());
    ofile.setOption(HUSD_PREF_DEFAULTMETERSPERUNIT,
        usingHoudiniMetersPerUnit() ? 0.0 : defaultMetersPerUnit());
    ofile.setOption(HUSD_PREF_DEFAULTUPAXIS,
        usingUsdUpAxis() ? "" : static_cast<const char *>(defaultUpAxis()));

    return ofile.save(filename);
}

bool
HUSD_Preferences::loadPrefs()
{
    UT_OptionFile    ofile;
    UT_String        filename;
    bool             nosave = false;

    if (!UTfindPreferenceFile(UT_HOUDINI_PATH,
            HUSD_PREFERENCES_FILE, filename, nosave))
    {
        UT_PathSearch::getHomeHoudini(filename);
        filename += "/" HUSD_PREFERENCES_FILE;
    }

    if (ofile.load(filename))
    {
        ofile.getOption(HUSD_PREF_SHOWRESOLVEDPATHS,
            theShowResolvedPaths, showResolvedPaths());
        ofile.getOption(HUSD_PREF_PANESFOLLOWCURRENTNODE,
            thePanesFollowCurrentNode, panesFollowCurrentNode());
        ofile.getOption(HUSD_PREF_PANESSHOWVIEWPORTSTAGE,
            thePanesShowViewportStage, panesShowViewportStage());
        ofile.getOption(HUSD_PREF_PANESSHOWPOSTLAYERS,
            thePanesShowPostLayers, panesShowPostLayers());
        ofile.getOption(HUSD_PREF_USESIMPLIFIEDLINKERUI,
            theUseSimplifiedLinkerUi, useSimplifiedLinkerUi());
        ofile.getOption(HUSD_PREF_AUTOSETASSETRESOLVERCONTEXT,
            theAutoSetAssetResolverContext, autoSetAssetResolverContext());
        ofile.getOption(HUSD_PREF_LOADPAYLOADSBYDEFAULT,
            theLoadPayloadsByDefault, loadPayloadsByDefault());
        ofile.getOption(HUSD_PREF_ALLOWVIEWPORTONLYPAYLOADS,
            theAllowViewportOnlyPayloads, allowViewportOnlyPayloads());
        ofile.getOption(HUSD_PREF_DEFAULTNEWPRIMPATH,
            theDefaultNewPrimPath, defaultNewPrimPath());
        ofile.getOption(HUSD_PREF_DEFAULTCOLLECTIONSPRIMPATH,
            theDefaultCollectionsPrimPath, defaultCollectionsPrimPath());
        ofile.getOption(HUSD_PREF_DEFAULTCOLLECTIONSPRIMTYPE,
            theDefaultCollectionsPrimType, defaultCollectionsPrimType());
        ofile.getOption(HUSD_PREF_DEFAULTLIGHTSPRIMPATH,
            theDefaultLightsPrimPath, defaultLightsPrimPath());
        ofile.getOption(HUSD_PREF_DEFAULTCAMERASPRIMPATH,
            theDefaultCamerasPrimPath, defaultCamerasPrimPath());
        ofile.getOption(HUSD_PREF_DEFAULTTRANSFORMSUFFIX,
            theDefaultTransformSuffix, defaultTransformSuffix());
        ofile.getOption(HUSD_PREF_DEFAULTMETERSPERUNIT, theDefaultMetersPerUnit,
            usingHoudiniMetersPerUnit() ? 0.0 : defaultMetersPerUnit());
        ofile.getOption(HUSD_PREF_DEFAULTUPAXIS, theDefaultUpAxis,
            usingUsdUpAxis() ? "" : static_cast<const char *>(defaultUpAxis()));

        runPrefChangeCallbacks();
        return true;
    }

    return false;
}

void
HUSD_Preferences::runPrefChangeCallbacks()
{
    for (auto &&it : thePrefChangeCallbacks)
        it.second();
}

int
HUSD_Preferences::addPrefChangeCallback(
        HUSD_Preferences::PrefChangeCallback callback)
{
    thePrefChangeCallbacks[thePrefChangeCallbackId] = callback;

    return thePrefChangeCallbackId++;
}

void
HUSD_Preferences::removePrefChangeCallback(int id)
{
    thePrefChangeCallbacks.erase(id);
}
