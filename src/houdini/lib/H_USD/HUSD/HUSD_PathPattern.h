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

