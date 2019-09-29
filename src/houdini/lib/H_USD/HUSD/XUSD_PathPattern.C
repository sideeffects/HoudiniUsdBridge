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

#include "XUSD_PathPattern.h"

PXR_NAMESPACE_OPEN_SCOPE

XUSD_PathPattern::XUSD_PathPattern(const UT_StringArray &pattern_tokens,
	HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands)
    : HUSD_PathPattern(pattern_tokens, lock, demands)
{
}

XUSD_PathPattern::XUSD_PathPattern(const UT_StringRef &pattern,
	HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
	int nodeid,
	const HUSD_TimeCode &timecode)
    : HUSD_PathPattern(pattern, lock, demands, nodeid, timecode)
{
}

XUSD_PathPattern::~XUSD_PathPattern()
{
}

void
XUSD_PathPattern::getSpecialTokenPaths(SdfPathSet &collection_paths,
	SdfPathSet &expanded_collection_paths,
	SdfPathSet &vexpression_paths) const
{
    for (auto &&token : myTokens)
    {
	if (token.mySpecialTokenDataPtr)
	{
	    XUSD_SpecialTokenData *xusddata =
		static_cast<XUSD_SpecialTokenData *>(
		    token.mySpecialTokenDataPtr.get());

	    collection_paths.insert(
		xusddata->myCollectionPathSet.begin(),
		xusddata->myCollectionPathSet.end());
	    expanded_collection_paths.insert(
		xusddata->myExpandedCollectionPathSet.begin(),
		xusddata->myExpandedCollectionPathSet.end());
	    vexpression_paths.insert(
		xusddata->myVexpressionPathSet.begin(),
		xusddata->myVexpressionPathSet.end());
	}
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

