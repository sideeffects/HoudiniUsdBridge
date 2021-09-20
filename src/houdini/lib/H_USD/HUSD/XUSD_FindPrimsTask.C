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

#include "XUSD_FindPrimsTask.h"
#include "XUSD_AutoCollection.h"
#include "HUSD_Path.h"
#include <UT/UT_TaskGroup.h>

PXR_NAMESPACE_OPEN_SCOPE

XUSD_FindPrimsTaskData::~XUSD_FindPrimsTaskData()
{
}

XUSD_FindPrimPathsTaskData::~XUSD_FindPrimPathsTaskData()
{
    for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
    {
        if(auto* tdata = it.get())
            delete tdata;
    }
}

void
XUSD_FindPrimPathsTaskData::addToThreadData(const UsdPrim &prim, bool *)
{
    auto *&threadData = myThreadData.get();
    if(!threadData)
        threadData = new FindPrimPathsTaskThreadData;
    threadData->myPaths.push_back(prim.GetPath());
}

void
XUSD_FindPrimPathsTaskData::gatherPathsFromThreads(XUSD_PathSet &paths)
{
    for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
    {
        if(const auto* tdata = it.get())
        {
            for (auto &&path : tdata->myPaths)
                paths.insert(path);
        }
    }
}

XUSD_FindUsdPrimsTaskData::~XUSD_FindUsdPrimsTaskData()
{
    for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
    {
        if(auto* tdata = it.get())
            delete tdata;
    }
}

void
XUSD_FindUsdPrimsTaskData::addToThreadData(const UsdPrim &prim, bool *)
{
    auto *&threadData = myThreadData.get();
    if(!threadData)
        threadData = new FindUsdPrimsTaskThreadData;
    threadData->myPrims.append(prim);
}

void
XUSD_FindUsdPrimsTaskData::gatherPrimsFromThreads(UT_Array<UsdPrim> &prims)
{
    for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
    {
        if(const auto* tdata = it.get())
        {
            prims.concat(tdata->myPrims);
        }
    }
}

void
XUSD_FindUsdPrimsTaskData::gatherPrimsFromThreads(std::vector<UsdPrim> &prims)
{
    for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
    {
        if(const auto* tdata = it.get())
        {
            prims.insert(prims.end(),
                tdata->myPrims.begin(), tdata->myPrims.end());
        }
    }
}

namespace
{

class xusd_FindPrimsTask
{
public:
    xusd_FindPrimsTask(
            const UsdPrim &prim,
            XUSD_FindPrimsTaskData &data,
            const Usd_PrimFlagsPredicate &predicate,
            const UT_PathPattern *pattern,
            const XUSD_SimpleAutoCollection *autocollection,
            UT_TaskGroup &task_group)
        : myPrim(prim)
        , myData(data)
        , myPredicate(predicate)
        , myPattern(pattern)
        , myAutoCollection(autocollection)
        , myTaskGroup(task_group)
    {
    }

    void operator()() const;

private:
    UsdPrim                          myPrim;
    XUSD_FindPrimsTaskData          &myData;
    const Usd_PrimFlagsPredicate    &myPredicate;
    const UT_PathPattern            *myPattern;
    const XUSD_SimpleAutoCollection *myAutoCollection;
    UT_TaskGroup                    &myTaskGroup;
};

void
xusd_FindPrimsTask::operator()() const
{
    // Ignore the HoudiniLayerInfo prim and all of its children.
    if (myPrim.GetPath() == HUSDgetHoudiniLayerInfoSdfPath())
        return;

    // Don't ever add the pseudoroot prim to the list of matches.
    if (myPrim.GetPath() != SdfPath::AbsoluteRootPath())
    {
        bool prune = false;

        if (myPattern)
        {
            HUSD_Path   primpath(myPrim.GetPath());

            if (myPattern->matches(primpath.pathStr(), &prune))
                myData.addToThreadData(myPrim, &prune);
        }
        else if (myAutoCollection)
        {
            if (myAutoCollection->matchPrimitive(myPrim, &prune))
                myData.addToThreadData(myPrim, &prune);
        }
        else
            myData.addToThreadData(myPrim, &prune);

        if (prune)
            return;
    }

    for (const auto &child : myPrim.GetFilteredChildren(myPredicate))
    {
        myTaskGroup.run(xusd_FindPrimsTask(
                child, myData, myPredicate, myPattern, myAutoCollection,
                myTaskGroup));
    }
}

} // unnamed namespace

void
XUSDfindPrims(
        const UsdPrim& prim,
        XUSD_FindPrimsTaskData &data,
        const Usd_PrimFlagsPredicate &predicate,
        const UT_PathPattern *pattern,
        const XUSD_SimpleAutoCollection *autocollection)
{
    UT_TaskGroup tg;
    tg.runAndWait(xusd_FindPrimsTask(prim, data, predicate, pattern,
                                     autocollection, tg));
}

PXR_NAMESPACE_CLOSE_SCOPE

