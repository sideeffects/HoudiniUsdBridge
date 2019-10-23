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

#ifndef __HUSD_Preferences_h__
#define __HUSD_Preferences_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_Color.h>

class HUSD_API HUSD_Preferences
{
public:
    // Returns a string containing the concatenation of all the default paths
    // that are likely to end up containing collections. This string is built
    // by combining other preferences.
    static const UT_StringHolder defaultCollectionsSearchPath();

    static const UT_StringHolder &defaultNewPrimPath();
    static void			 setDefaultNewPrimPath(
					const UT_StringHolder &path);

    static const UT_StringHolder &defaultCollectionsPrimPath();
    static void			 setDefaultCollectionsPrimPath(
					const UT_StringHolder &path);

    static const UT_StringHolder &defaultLightsPrimPath();
    static void			 setDefaultLightsPrimPath(
					const UT_StringHolder &path);

    static const UT_StringHolder &defaultTransformSuffix();
    static void			 setDefaultTransformSuffix(
					const UT_StringHolder &suffix);

    static bool			 showResolvedPaths();
    static void			 setShowResolvedPaths(
					bool show_resolved_paths);

    static bool			 panesFollowCurrentNode();
    static void			 setPanesFollowCurrentNode(
					bool follow_current_node);

    static bool			 autoSetAssetResolverContext();
    static void			 setAutoSetAssetResolverContext(
					bool auto_set_context);

    static bool			 updateRendererInBackground();
    static void			 setUpdateRendererInBackground(
					bool update_in_background);

    static bool			 loadPayloadsByDefault();
    static void			 setLoadPayloadsByDefault(
					bool load_payloads);

    static bool			 useSimplifiedLinkerUi();
    static void			 setUseSimplifiedLinkerUi(
					bool use_simplified_linker_ui);

    static bool	                 usingUsdMetersPerUnit();
    static double		 defaultMetersPerUnit();
    static void			 setDefaultMetersPerUnit(
					double metersperunit);

    static bool	                 usingUsdUpAxis();
    static UT_StringHolder	 defaultUpAxis();
    static bool			 setDefaultUpAxis(
					const UT_StringHolder &upaxis);

private:
    static UT_StringHolder	 theDefaultNewPrimPath;
    static UT_StringHolder	 theDefaultCollectionsPrimPath;
    static UT_StringHolder	 theDefaultLightsPrimPath;
    static UT_StringHolder	 theDefaultTransformSuffix;
    static bool			 theShowResolvedPaths;
    static bool			 thePanesFollowCurrentNode;
    static bool			 theAutoSetAssetResolverContext;
    static bool			 theUpdateRendererInBackground;
    static bool			 theLoadPayloadsByDefault;
    static bool			 theUseSimplifiedLinkerUi;
    static double                theDefaultMetersPerUnit;
    static UT_StringHolder       theDefaultUpAxis;
};

#endif
