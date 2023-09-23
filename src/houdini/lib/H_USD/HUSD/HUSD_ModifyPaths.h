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

#ifndef __HUSD_ModifyPaths_h__
#define __HUSD_ModifyPaths_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"

class HUSD_FindPrims;
class PY_CompiledCode;
class PY_EvaluationContext;

class UT_Regex;
class UT_StringArray;

class HUSD_API HUSD_ModifyPaths
{
public:
			 HUSD_ModifyPaths(HUSD_AutoWriteLock &lock);
			~HUSD_ModifyPaths();
    
    static bool          modifyPath(UT_StringHolder &result,
                                const UT_Array<UT_Regex> &prefixregex,
                                const UT_StringArray &replaceprefix,
                                const UT_Array<UT_Regex> &suffixregex,
                                const UT_StringArray &replacesuffix,
                                const UT_Array<UT_SharedPtr<PY_CompiledCode>>
                                    &pythonexpr,
                                PY_EvaluationContext *pycontext,
                                bool allowchained,
                                UT_WorkBuffer *pythonerrors);

    bool                 modifyPaths(const HUSD_FindPrims &findprims,
                                const UT_StringArray &findprefix,
                                const UT_StringArray &replaceprefix,
                                const UT_StringArray &findsuffix,
                                const UT_StringArray &replacesuffix,
                                const UT_StringArray &pythoncode,
                                PY_EvaluationContext *pycontext,
                                bool modifyassetpaths,
                                bool modifylayerpaths,
                                bool allowchained) const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

