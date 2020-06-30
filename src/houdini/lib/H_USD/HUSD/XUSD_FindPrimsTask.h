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

#include "HUSD_API.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <UT/UT_PathPattern.h>
#include <UT/UT_Task.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_SimpleAutoCollection;

// Generic base class for gathering per-prim information during a multithreaded
// traversal of a stage using an XUSD_PathPatthern to restrict the traversal.
class HUSD_API XUSD_FindPrimsTaskData
{
public:
    virtual ~XUSD_FindPrimsTaskData();
    virtual void addToThreadData(UsdPrim &prim) = 0;
};

// Subclass of XUSD_FindPrimsTaskData that specifically collects the SdfPaths
// of all USD prims found in the traversal into an XUSD_PathSet.
class HUSD_API XUSD_FindPrimPathsTaskData : public XUSD_FindPrimsTaskData
{
public:
    ~XUSD_FindPrimPathsTaskData() override;
    void addToThreadData(UsdPrim &prim) override;

    void gatherPathsFromThreads(XUSD_PathSet &paths);

private:
    class FindPrimPathsTaskThreadData
    {
    public:
        SdfPathVector    myPaths;
    };
    typedef UT_ThreadSpecificValue<FindPrimPathsTaskThreadData *>
        FindPrimPathsTaskThreadDataTLS;

    FindPrimPathsTaskThreadDataTLS    myThreadData;
};

// Subclass of XUSD_FindPrimsTaskData that specifically collects the UsdPrims
// of all USD prims found in the traversal into a UT_Array<UsdPrim>.
class HUSD_API XUSD_FindUsdPrimsTaskData : public XUSD_FindPrimsTaskData
{
public:
    ~XUSD_FindUsdPrimsTaskData() override;
    void addToThreadData(UsdPrim &prim) override;

    void gatherPrimsFromThreads(UT_Array<UsdPrim> &prims);

private:
    class FindUsdPrimsTaskThreadData
    {
    public:
        UT_Array<UsdPrim>    myPrims;
    };
    typedef UT_ThreadSpecificValue<FindUsdPrimsTaskThreadData *>
        FindUsdPrimsTaskThreadDataTLS;

    FindUsdPrimsTaskThreadDataTLS    myThreadData;
};

// Class for performing a multithreaded traversal of a stage guided by a
// UT_PathPattern. Data is collected into an XUSD_FindPrimsTaskData object
// by calling its addToThreadData method with all matching prims.
class HUSD_API XUSD_FindPrimsTask : public UT_Task
{
public:
    XUSD_FindPrimsTask(const UsdPrim& prim,
            XUSD_FindPrimsTaskData &data,
            const Usd_PrimFlagsPredicate &predicate,
            const UT_PathPattern *pattern,
            const XUSD_SimpleAutoCollection *autocollection);

    UT_Task *run() override;

private:
    UsdPrim                          myPrim;
    XUSD_FindPrimsTaskData          &myData;
    const Usd_PrimFlagsPredicate    &myPredicate;
    const UT_PathPattern            *myPattern;
    const XUSD_SimpleAutoCollection *myAutoCollection;
    bool                             myVisited;
};

PXR_NAMESPACE_CLOSE_SCOPE

