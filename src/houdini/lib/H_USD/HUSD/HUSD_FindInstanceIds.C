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

#include "HUSD_FindInstanceIds.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_CvexCode.h"
#include "HUSD_Cvex.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <UT/UT_String.h>
#include <UT/UT_StringMMPattern.h>
#include <SYS/SYS_String.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    class husd_IdHolder
    {
    public:
	UT_IntArray     &myAvailableIds;
	std::set<int>   &myMatchedIds;
    };

    void
    runVex(HUSD_AutoAnyLock &lock,
            const HUSD_TimeCode &timecode,
            const UT_StringRef &primpath,
            const UT_StringHolder &vexpr,
            husd_IdHolder &ids,
            UT_String &error)
    {
        HUSD_Cvex        cvex;
        HUSD_CvexCode    cvexcode(vexpr, false);
        UT_ExintArray    matched_instance_indices;

        cvex.setCwdNodeId(lock.dataHandle().nodeId());
        cvex.setTimeCode(timecode);
        cvexcode.setReturnType(HUSD_CvexCode::ReturnType::BOOLEAN);
        cvex.matchInstances(lock, matched_instance_indices,
            primpath, nullptr, cvexcode);
        for (auto &&id : matched_instance_indices)
            ids.myMatchedIds.insert(id);
    }

    void
    parsePattern(HUSD_AutoAnyLock &lock,
            const HUSD_TimeCode &timecode,
            const UT_StringRef &primpath,
            char *pattern,
            husd_IdHolder &ids,
            UT_String &error)
    {
	static const char	*theNumerics = "0123456789.*:!-,^";
	char			*start, *end, end_char;
	int			 len;

	// Skip over any whitespace.
	while (*pattern && (SYSisspace(*pattern) || *pattern == ','))
	    pattern++;

	// Keep running through the pattern string until we hit the end.
	while (*pattern)
	{
	    start = pattern;
            if (*pattern == '{')
            {
                int              bracecount = 1;

                while (bracecount > 0 && *pattern)
                {
                    pattern++;
                    if (*pattern == '}')
                        bracecount--;
                    else if (*pattern == '{')
                        bracecount++;
                }
                end = pattern;

                if (!*pattern)
                {
                    error.harden("found unmatched open brace");
                    break;
                }

                // Get the string inside the braces, but without the braces.
                UT_StringHolder  vexpr(start + 1,
                    (exint)(intptr_t)(end - start - 1));

                runVex(lock, timecode, primpath, vexpr, ids, error);
                if (error.isstring())
                    break;
            }
            else
            {
                // Find a chunk of numeric characters.
                len = strspn(start, theNumerics);
                if (!len)
                    break;
                end = start + len;

                UT_String	 token;

                end_char = *end;
                *end = '\0';
                if (*start == '^')
                {
                    token = start+1;
                    token.traversePattern(ids.myAvailableIds.size(), &ids,
                        [](int num, int, void *data) {
                            husd_IdHolder *ids = (husd_IdHolder *)data;

                            if (ids->myAvailableIds.uniqueSortedFind(num) >= 0)
                                ids->myMatchedIds.erase(num);
                            return 1;
                        });
                }
                else
                {
                    token = start;
                    token.traversePattern(ids.myAvailableIds.size(), &ids,
                        [](int num, int, void *data) {
                            husd_IdHolder *ids = (husd_IdHolder *)data;

                            if (ids->myAvailableIds.uniqueSortedFind(num) >= 0)
                                ids->myMatchedIds.emplace(num);
                            return 1;
                        });
                }
                *end = end_char;
            }

	    pattern = end;
	    while (*pattern && (SYSisspace(*pattern) || *pattern == ','))
		pattern++;
	}
    }
}


class HUSD_FindInstanceIds::husd_FindInstanceIdsPrivate
{
public:
    husd_FindInstanceIdsPrivate()
	: myInstancesCalculated(false)
    { }

    UT_IntArray			 myInstances;
    UsdTimeCode			 myTimeCode;
    bool			 myInstancesCalculated;
};

HUSD_FindInstanceIds::HUSD_FindInstanceIds(HUSD_AutoAnyLock &lock,
	const UT_StringRef &primpath,
	const UT_StringRef &instanceidpattern)
    : myPrivate(new husd_FindInstanceIdsPrivate()),
      myAnyLock(lock),
      myPrimPath(primpath),
      myInstanceIdPattern(instanceidpattern)
{
}

HUSD_FindInstanceIds::~HUSD_FindInstanceIds()
{
}

void
HUSD_FindInstanceIds::setInstanceIdPattern(const UT_StringHolder &pattern)
{
    myInstanceIdPattern = pattern;
    myPrivate->myInstancesCalculated = false;
}

void
HUSD_FindInstanceIds::setPrimPath(const UT_StringHolder &primpath)
{
    myPrimPath = primpath;
    myPrivate->myInstancesCalculated = false;
}

const UT_IntArray &
HUSD_FindInstanceIds::getInstanceIds(const HUSD_TimeCode &tc) const
{
    UsdTimeCode		 usdtc = HUSDgetUsdTimeCode(tc);

    if ((myPrivate->myInstancesCalculated &&
	 myPrivate->myTimeCode == usdtc) ||
	!myInstanceIdPattern.isstring())
	return myPrivate->myInstances;

    auto		 outdata = myAnyLock.constData();

    myPrivate->myInstances.clear();
    if (outdata && outdata->isStageValid())
    {
	auto	 stage(outdata->stage());
	UsdPrim	 prim = stage->GetPrimAtPath(HUSDgetSdfPath(myPrimPath));

	if (prim)
	{
	    UsdGeomPointInstancer	 instancer(prim);

	    if (instancer)
	    {
		UsdAttribute	 idsattr = instancer.GetIdsAttr();
		UT_IntArray	 availableids;
		VtArray<int>	 ids;

		if (idsattr && idsattr.Get(&ids, usdtc))
		{
		    availableids.setSizeNoInit(ids.size());
		    for (int i = 0, n = ids.size(); i < n; i++)
			availableids(i) = ids[i];
		    UTsortAndRemoveDuplicates(availableids);
		}
		else
		{
		    UsdAttribute protoindices = instancer.GetProtoIndicesAttr();
		    VtArray<int> indices;

		    if (protoindices)
		    {
			if (protoindices.Get(&indices, usdtc))
			{
			    availableids.setSizeNoInit(indices.size());
			    for (int i = 0, n = indices.size(); i < n; i++)
				availableids(i) = i;
			}
		    }
		}

		if (availableids.size() > 0)
		{
		    std::set<int>	 matchedids;
		    husd_IdHolder	 ids = { availableids, matchedids };
		    UT_String		 pattern(myInstanceIdPattern.c_str(),1);
                    UT_String            error;

		    parsePattern(myAnyLock, tc, myPrimPath,
                        pattern, ids, error);
                    if (error.isstring())
                    {
                        HUSD_ErrorScope::addError(
                            HUSD_ERR_FAILED_TO_PARSE_PATTERN,
                            error.c_str());
                    }
                    else
                    {
                        for (auto &&id : matchedids)
                            myPrivate->myInstances.append(id);
                    }
		}
	    }
            else
                HUSD_ErrorScope::addError(
                    HUSD_ERR_NOT_INSTANCER_PRIM,
                    myPrimPath.c_str());
	}
        else if (myPrimPath.isstring())
            HUSD_ErrorScope::addError(
                HUSD_ERR_CANT_FIND_PRIM,
                myPrimPath.c_str());
    }

    myPrivate->myInstancesCalculated = true;
    myPrivate->myTimeCode = usdtc;

    return myPrivate->myInstances;
}

