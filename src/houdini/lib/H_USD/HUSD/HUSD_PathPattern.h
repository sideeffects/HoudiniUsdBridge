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
#include "HUSD_TimeCode.h"
#include "HUSD_Utils.h"
#include <UT/UT_PathPattern.h>

class UT_AutoInterrupt;

class HUSD_API HUSD_PathPattern : public UT_PathPattern
{
public:
			 HUSD_PathPattern(const UT_StringRef &pattern,
				HUSD_AutoAnyLock &lock,
				HUSD_PrimTraversalDemands demands,
                                bool case_sensitive,
                                bool assume_wildcards,
                                bool allow_instance_indices,
				int nodeid,
				const HUSD_TimeCode &timecode);
			~HUSD_PathPattern() override;

    bool                 getMayBeTimeVarying() const;

protected:
    // Clone-only constructor used by createEmptyClone() to build the reduced
    // patterns for createPruningPattern()/createPrecedingGroupPattern(). It has
    // no lock or time code, so the resulting pattern MUST only be used for
    // boolean (path) matching, never for point-instance id collection: with a
    // null lock, instanceMatchData() produces no instance payload. If a clone
    // is ever used in a collectInstanceIds context (e.g. to add instance id
    // support to preceding-group tokens), the lock and time code must be
    // threaded through createEmptyClone() and the pruning/preceding-group
    // builders first.
                         HUSD_PathPattern(bool case_sensitive,
                                bool assume_wildcards,
                                bool allow_instance_indices);

    UT_PathPattern      *createEmptyClone() const override;
    bool	         matchSpecialToken(
				const UT_StringRef &path,
				const Token &token,
                                bool *excludes_branch) const override;
    bool	         matchSpecialTokenWithData(
				const UT_StringRef &path,
				const Token &token,
				bool *excludes_branch,
				UT_PathPatternMatchDataPtr &match_data)
				const override;
    UT_PathPatternMatchDataPtr makeLeafMatchData(
				const UT_StringRef &path,
				const Token &token) const override;
    UT_PathPatternMatchDataPtr combineMatchData(
				MatchOp op,
				const UT_PathPatternMatchDataPtr &lhs,
				const UT_PathPatternMatchDataPtr &rhs)
				const override;
    bool	         matchDataIsEmpty(
				const UT_PathPatternMatchDataPtr &match_data)
				const override;

private:
    void		 initializeSpecialTokens(HUSD_AutoAnyLock &lock,
				HUSD_PrimTraversalDemands demands,
				int nodeid,
				const HUSD_TimeCode &timecode,
                                UT_AutoInterrupt &boss);

    // Returns the set of matched instance ids for an instancer prim as a match
    // payload: the ids selected by an explicit instance-id pattern, or all of
    // the instancer's ids when no pattern is supplied. Returns null when the
    // path is not a point instancer (or we have no lock for stage access), so
    // that non-instancer matches stay purely boolean.
    UT_PathPatternMatchDataPtr instanceMatchData(const UT_StringRef &path,
				const UT_StringRef &instance_id_pattern) const;

    // Lock and time code retained for stage access during matching (the match
    // pass runs within the same lock scope that built this pattern). Null for
    // the lightweight clones used to build pruning/preceding-group patterns,
    // which never perform instance matching.
    HUSD_AutoAnyLock    *myLock;
    HUSD_TimeCode        myMatchTimeCode;
};

#endif

