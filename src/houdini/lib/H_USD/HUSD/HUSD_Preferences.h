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
    static const UT_StringHolder &defaultNewPrimPath();
    static void			 setDefaultNewPrimPath(
					const UT_StringHolder &path);

    static const UT_StringHolder &defaultCollectionsPrimPath();
    static void			 setDefaultCollectionsPrimPath(
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

private:
    static UT_StringHolder	 theDefaultNewPrimPath;
    static UT_StringHolder	 theDefaultCollectionsPrimPath;
    static UT_StringHolder	 theDefaultTransformSuffix;
    static bool			 theShowResolvedPaths;
    static bool			 thePanesFollowCurrentNode;
    static bool			 theAutoSetAssetResolverContext;
    static bool			 theUpdateRendererInBackground;
    static bool			 theLoadPayloadsByDefault;
};

#endif
