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
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/metrics.h>

PXR_NAMESPACE_USING_DIRECTIVE

#define HUSD_PREFERENCES_FILE "solaris.pref"

static UT_StringHolder	 theFactoryDefaultNewPrimPath = "/$OS";
static UT_StringHolder	 theFactoryDefaultCollectionsPrimPath="/collections";
static UT_StringHolder	 theFactoryDefaultCollectionsPrimType="";
static UT_StringHolder	 theFactoryDefaultLightsPrimPath="/lights";
static UT_StringHolder	 theFactoryDefaultTransformSuffix = "$OS";

UT_StringHolder	 HUSD_Preferences::theDefaultNewPrimPath =
                         theFactoryDefaultNewPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultCollectionsPrimPath =
                         theFactoryDefaultCollectionsPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultCollectionsPrimType =
                         theFactoryDefaultCollectionsPrimType;
UT_StringHolder	 HUSD_Preferences::theDefaultLightsPrimPath =
                         theFactoryDefaultLightsPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultTransformSuffix =
                         theFactoryDefaultTransformSuffix;
bool		 HUSD_Preferences::theShowResolvedPaths = false;
bool		 HUSD_Preferences::thePanesFollowCurrentNode = true;
bool		 HUSD_Preferences::thePanesShowViewportStage = false;
bool		 HUSD_Preferences::theAutoSetAssetResolverContext = false;
bool		 HUSD_Preferences::theUpdateRendererInBackground = true;
bool		 HUSD_Preferences::theLoadPayloadsByDefault = true;
bool		 HUSD_Preferences::theUseSimplifiedLinkerUi = false;
double           HUSD_Preferences::theDefaultMetersPerUnit = 0.0;
UT_StringHolder  HUSD_Preferences::theDefaultUpAxis = "";

#define HUSD_PREF_SHOWRESOLVEDPATHS "showresolvedpaths"
#define HUSD_PREF_PANESFOLLOWCURRENTNODE "panesfollowcurrentnode"
#define HUSD_PREF_PANESSHOWVIEWPORTSTAGE "panesshowviewportstage"
#define HUSD_PREF_USESIMPLIFIEDLINKERUI "usesimplifiedlinkerui"
#define HUSD_PREF_AUTOSETASSETRESOLVERCONTEXT "autosetassetresolvercontext"
#define HUSD_PREF_LOADPAYLOADSBYDEFAULT "loadpayloadsbydefault"
#define HUSD_PREF_DEFAULTNEWPRIMPATH "defaultnewprimpath"
#define HUSD_PREF_DEFAULTCOLLECTIONSPRIMPATH "defaultcollectionsprimpath"
#define HUSD_PREF_DEFAULTCOLLECTIONSPRIMTYPE "defaultcollectionsprimtype"
#define HUSD_PREF_DEFAULTLIGHTSPRIMPATH "defaultlightsprimpath"
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

    buf.stealIntoStringHolder(result);

    return result;
}

const UT_StringHolder &
HUSD_Preferences::defaultNewPrimPath()
{
    return theDefaultNewPrimPath;
}

void
HUSD_Preferences::setDefaultNewPrimPath(const UT_StringHolder &path)
{
    if (path.isstring())
        theDefaultNewPrimPath = path;
    else
        theDefaultNewPrimPath = theFactoryDefaultNewPrimPath;
}

const UT_StringHolder &
HUSD_Preferences::defaultCollectionsPrimPath()
{
    return theDefaultCollectionsPrimPath;
}

void
HUSD_Preferences::setDefaultCollectionsPrimPath(const UT_StringHolder &path)
{
    if (path.isstring())
        theDefaultCollectionsPrimPath = path;
    else
        theDefaultCollectionsPrimPath = theFactoryDefaultCollectionsPrimPath;
}

const UT_StringHolder &
HUSD_Preferences::defaultCollectionsPrimType()
{
    return theDefaultCollectionsPrimType;
}

void
HUSD_Preferences::setDefaultCollectionsPrimType(const UT_StringHolder &primtype)
{
    theDefaultCollectionsPrimType = primtype;
}

const UT_StringHolder &
HUSD_Preferences::defaultLightsPrimPath()
{
    return theDefaultLightsPrimPath;
}

void
HUSD_Preferences::setDefaultLightsPrimPath(const UT_StringHolder &path)
{
    if (path.isstring())
        theDefaultLightsPrimPath = path;
    else
        theDefaultLightsPrimPath = theFactoryDefaultLightsPrimPath;
}

const UT_StringHolder &
HUSD_Preferences::defaultTransformSuffix()
{
    return theDefaultTransformSuffix;
}

void
HUSD_Preferences::setDefaultTransformSuffix(const UT_StringHolder &suffix)
{
    if (suffix.isstring())
        theDefaultTransformSuffix = suffix;
    else
        theDefaultTransformSuffix = theFactoryDefaultTransformSuffix;
}

bool
HUSD_Preferences::showResolvedPaths()
{
    return theShowResolvedPaths;
}

void
HUSD_Preferences::setShowResolvedPaths(bool show_resolved_paths)
{
    theShowResolvedPaths = show_resolved_paths;
}

bool
HUSD_Preferences::panesFollowCurrentNode()
{
    return thePanesFollowCurrentNode;
}

void
HUSD_Preferences::setPanesFollowCurrentNode(bool follow_current_node)
{
    thePanesFollowCurrentNode = follow_current_node;
}

bool
HUSD_Preferences::panesShowViewportStage()
{
    return thePanesShowViewportStage;
}

void
HUSD_Preferences::setPanesShowViewportStage(bool show_viewport_stage)
{
    thePanesShowViewportStage = show_viewport_stage;
}

bool
HUSD_Preferences::autoSetAssetResolverContext()
{
    return theAutoSetAssetResolverContext;
}

void
HUSD_Preferences::setAutoSetAssetResolverContext(bool auto_set_context)
{
    theAutoSetAssetResolverContext = auto_set_context;
}

bool
HUSD_Preferences::updateRendererInBackground()
{
    return theUpdateRendererInBackground;
}

void
HUSD_Preferences::setUpdateRendererInBackground(bool update_in_background)
{
    theUpdateRendererInBackground = update_in_background;
}

bool
HUSD_Preferences::loadPayloadsByDefault()
{
    return theLoadPayloadsByDefault;
}

void
HUSD_Preferences::setLoadPayloadsByDefault(bool load_payloads)
{
    theLoadPayloadsByDefault = load_payloads;
}

bool
HUSD_Preferences::useSimplifiedLinkerUi()
{
    return theUseSimplifiedLinkerUi;
}

void
HUSD_Preferences::setUseSimplifiedLinkerUi(bool use_simplified_linker_ui)
{
    theUseSimplifiedLinkerUi = use_simplified_linker_ui;
}

bool
HUSD_Preferences::usingUsdMetersPerUnit()
{
    // A value of zero means "use the value set in the Houdini length unit".
    return (theDefaultMetersPerUnit == 0.0);
}

double
HUSD_Preferences::defaultMetersPerUnit()
{
    if (usingUsdMetersPerUnit())
        return CHgetManager()->getUnitLength();

    return theDefaultMetersPerUnit;
}

void
HUSD_Preferences::setDefaultMetersPerUnit(double metersperunit)
{
    theDefaultMetersPerUnit = metersperunit;
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
    // Only an empty string, "Y", and "Z" are acceptable up axis values.
    if (!upaxis.isstring() || upaxis == "Y" || upaxis == "Z")
    {
        theDefaultUpAxis = upaxis;
        return true;
    }

    return false;
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
    ofile.setOption(HUSD_PREF_USESIMPLIFIEDLINKERUI,
        useSimplifiedLinkerUi());
    ofile.setOption(HUSD_PREF_AUTOSETASSETRESOLVERCONTEXT,
        autoSetAssetResolverContext());
    ofile.setOption(HUSD_PREF_LOADPAYLOADSBYDEFAULT,
        loadPayloadsByDefault());
    ofile.setOption(HUSD_PREF_DEFAULTNEWPRIMPATH,
        defaultNewPrimPath());
    ofile.setOption(HUSD_PREF_DEFAULTCOLLECTIONSPRIMPATH,
        defaultCollectionsPrimPath());
    ofile.setOption(HUSD_PREF_DEFAULTCOLLECTIONSPRIMTYPE,
        defaultCollectionsPrimType());
    ofile.setOption(HUSD_PREF_DEFAULTLIGHTSPRIMPATH,
        defaultLightsPrimPath());
    ofile.setOption(HUSD_PREF_DEFAULTTRANSFORMSUFFIX,
        defaultTransformSuffix());
    ofile.setOption(HUSD_PREF_DEFAULTMETERSPERUNIT,
        usingUsdMetersPerUnit() ? 0.0 : defaultMetersPerUnit());
    ofile.setOption(HUSD_PREF_DEFAULTUPAXIS,
        usingUsdUpAxis() ? "" : defaultUpAxis());

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
        ofile.getOption(HUSD_PREF_USESIMPLIFIEDLINKERUI,
            theUseSimplifiedLinkerUi, useSimplifiedLinkerUi());
        ofile.getOption(HUSD_PREF_AUTOSETASSETRESOLVERCONTEXT,
            theAutoSetAssetResolverContext, autoSetAssetResolverContext());
        ofile.getOption(HUSD_PREF_LOADPAYLOADSBYDEFAULT,
            theLoadPayloadsByDefault, loadPayloadsByDefault());
        ofile.getOption(HUSD_PREF_DEFAULTNEWPRIMPATH,
            theDefaultNewPrimPath, defaultNewPrimPath());
        ofile.getOption(HUSD_PREF_DEFAULTCOLLECTIONSPRIMPATH,
            theDefaultCollectionsPrimPath, defaultCollectionsPrimPath());
        ofile.getOption(HUSD_PREF_DEFAULTCOLLECTIONSPRIMTYPE,
            theDefaultCollectionsPrimType, defaultCollectionsPrimType());
        ofile.getOption(HUSD_PREF_DEFAULTLIGHTSPRIMPATH,
            theDefaultLightsPrimPath, defaultLightsPrimPath());
        ofile.getOption(HUSD_PREF_DEFAULTTRANSFORMSUFFIX,
            theDefaultTransformSuffix, defaultTransformSuffix());
        ofile.getOption(HUSD_PREF_DEFAULTMETERSPERUNIT, theDefaultMetersPerUnit,
            usingUsdMetersPerUnit() ? 0.0 : defaultMetersPerUnit());
        ofile.getOption(HUSD_PREF_DEFAULTUPAXIS, theDefaultUpAxis,
            usingUsdUpAxis() ? "" : defaultUpAxis());

        return true;
    }

    return false;
}

