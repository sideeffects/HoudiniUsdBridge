/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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
#include <UT/UT_WorkBuffer.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/metrics.h>

PXR_NAMESPACE_USING_DIRECTIVE

static UT_StringHolder	 theFactoryDefaultNewPrimPath = "/$OS";
static UT_StringHolder	 theFactoryDefaultCollectionsPrimPath="/collections";
static UT_StringHolder	 theFactoryDefaultLightsPrimPath="/lights";
static UT_StringHolder	 theFactoryDefaultTransformSuffix = "$OS";

UT_StringHolder	 HUSD_Preferences::theDefaultNewPrimPath =
                         theFactoryDefaultNewPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultCollectionsPrimPath =
                         theFactoryDefaultCollectionsPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultLightsPrimPath =
                         theFactoryDefaultLightsPrimPath;
UT_StringHolder	 HUSD_Preferences::theDefaultTransformSuffix =
                         theFactoryDefaultTransformSuffix;
bool		 HUSD_Preferences::theShowResolvedPaths = false;
bool		 HUSD_Preferences::thePanesFollowCurrentNode = true;
bool		 HUSD_Preferences::theAutoSetAssetResolverContext = false;
bool		 HUSD_Preferences::theUpdateRendererInBackground = true;
bool		 HUSD_Preferences::theLoadPayloadsByDefault = true;
bool		 HUSD_Preferences::theUseSimplifiedLinkerUi = false;
double           HUSD_Preferences::theDefaultMetersPerUnit = 0.0;
UT_StringHolder  HUSD_Preferences::theDefaultUpAxis = "";

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
        return 1.0 / CHgetManager()->getUnitLength();

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

