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

#ifndef __HUSD_PathPattern_h__
#define __HUSD_PathPattern_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Utils.h"
#include <UT/UT_PathPattern.h>

class HUSD_TimeCode;

class HUSD_API HUSD_PathPattern : public UT_PathPattern
{
public:
			 HUSD_PathPattern(const UT_StringArray &pattern_tokens,
				HUSD_AutoAnyLock &lock,
				HUSD_PrimTraversalDemands demands);
			 HUSD_PathPattern(const UT_StringRef &pattern,
				HUSD_AutoAnyLock &lock,
				HUSD_PrimTraversalDemands demands,
				int nodeid,
				const HUSD_TimeCode &timecode);
			~HUSD_PathPattern();

protected:
    virtual bool	 matchSpecialToken(
				const UT_StringRef &path,
				const Token &token) const;

private:
    void		 initializeSpecialTokens(HUSD_AutoAnyLock &lock,
				HUSD_PrimTraversalDemands demands,
				int nodeid,
				const HUSD_TimeCode &timecode);
};

#endif

