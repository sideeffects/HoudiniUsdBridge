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

#ifndef __XUSD_PathPattern_h__
#define __XUSD_PathPattern_h__

#include "HUSD_API.h"
#include "HUSD_PathPattern.h"
#include "HUSD_TimeCode.h"
#include "XUSD_AutoCollection.h"
#include "XUSD_PathSet.h"
#include <UT/UT_Array.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

// Match payload carrying the set of matched point instancer instance ids for
// a single instancer path. These are combined by the path pattern's set
// operators (union, intersect, difference) so that instance selections support
// the same algebra as primitive selections.
//
// INVARIANT: myInstanceIds is always sorted in ascending order and free of
// duplicates. This follows from the INSTANCE ID CONTRACT documented on the
// auto-collection match methods (see XUSD_AutoCollection.h) - every producer of
// an instance id set (auto collections, the [...] leaf/whole-instancer helpers,
// and combineMatchData itself) yields sorted, duplicate-free ids - and
// XUSD_PathPattern::combineMatchData relies on it to combine two payloads with a
// linear sorted merge rather than building intermediate sets.
class XUSD_InstanceMatchData : public UT_PathPatternMatchData
{
public:
                         XUSD_InstanceMatchData()
                         { }
                        ~XUSD_InstanceMatchData() override
                         { }

    UT_Array<int64>      myInstanceIds;
};

class XUSD_SpecialTokenData : public UT_SpecialTokenData
{
public:
                         XUSD_SpecialTokenData()
                             : myInitialized(false),
                               myMayBeTimeVarying(false)
                         { }
                        ~XUSD_SpecialTokenData() override
                         { }

    XUSD_PathSet	                 myCollectionPathSet;
    XUSD_PathSet	                 myCollectionExpandedPathSet;
    XUSD_PathSet	                 myCollectionlessPathSet;
    UT_UniquePtr<XUSD_AutoCollection>    myRandomAccessAutoCollection;
    // Per-path instance ids precomputed at construction by non-random-access
    // auto collections (via matchPrimitives). Written once at construction,
    // then read-only during matching, so no thread-specific storage is needed.
    UT_StringMap<UT_Array<int64>>        myMatchedInstanceIds;
    bool                                 myInitialized;
    bool                                 myMayBeTimeVarying;
};

// Match an instance-id pattern against a point instancer prim, returning the
// matched semantic instance ids (from the instancer's 'ids' attribute). The
// pattern supports numeric ranges, '*', '^' exclusions, and '{vexpr}' blocks.
// Returns true if any ids matched.
HUSD_API bool	         XUSDmatchPointInstanceIds(HUSD_AutoAnyLock &lock,
				const UT_StringRef &pattern,
				const UsdPrim &instancer_prim,
				const HUSD_TimeCode &timecode,
				UT_Array<int64> &matched_ids);

class HUSD_API XUSD_PathPattern : public HUSD_PathPattern
{
public:
    // Clone-only constructor (see the HUSD_PathPattern equivalent): no lock or
    // time code, so the result is for boolean path matching only and must not
    // be used to collect point-instance ids.
                         XUSD_PathPattern(bool case_sensitive,
                                bool assume_wildcards,
                                bool allow_instance_indices);
			 XUSD_PathPattern(const UT_StringRef &pattern,
				HUSD_AutoAnyLock &lock,
				HUSD_PrimTraversalDemands demands,
                                bool case_sensitive,
                                bool assume_wildcards,
                                bool allow_instance_indices,
				int nodeid,
				const HUSD_TimeCode &timecode);
			~XUSD_PathPattern() override;

    void		 getSpecialTokenPaths(SdfPathSet &collection_paths,
				SdfPathSet &collection_expanded_paths,
                                SdfPathSet &collectionless_paths) const;

    const UT_Array<Token> &getTokens() const
			 { return myTokens; }

    const HUSD_TimeCode	&timeCode() const
			 { return myTimeCode; }

private:
    HUSD_TimeCode	 myTimeCode;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

