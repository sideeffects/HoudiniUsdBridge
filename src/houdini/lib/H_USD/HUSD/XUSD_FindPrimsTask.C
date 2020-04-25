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
XUSD_FindPrimPathsTaskData::addToThreadData(UsdPrim &prim)
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
            for (auto path : tdata->myPaths)
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
XUSD_FindUsdPrimsTaskData::addToThreadData(UsdPrim &prim)
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

XUSD_FindPrimsTask::XUSD_FindPrimsTask(const UsdPrim& prim,
        XUSD_FindPrimsTaskData &data,
        const Usd_PrimFlagsPredicate &predicate,
        const UT_PathPattern *pattern)
    : UT_Task(),
      myPrim(prim),
      myData(data),
      myPredicate(predicate),
      myPattern(pattern),
      myVisited(false)
{
}

UT_Task *
XUSD_FindPrimsTask::run()
{
    // This is the short circuit exit for when we are executed a
    // second time after being recycled as a continuation.
    if (myVisited)
        return NULL;
    myVisited = true;

    // Ignore the HoudiniLayerInfo prim and all of its children.
    if (myPrim.GetPath() == HUSDgetHoudiniLayerInfoSdfPath())
        return NULL;

    // Don't ever add the pseudoroot prim to the list of matches.
    if (myPrim.GetPath() != SdfPath::AbsoluteRootPath())
    {
        bool prune = false;

        if (!myPattern ||
            myPattern->matches(myPrim.GetPath().GetText(), &prune))
        {
            // Matched. Add it to the thread-specific list.
            myData.addToThreadData(myPrim);
        }
        else if (prune)
            return NULL;
    }

    // Count the children so we can increment the ref count.
    int count = 0;
    auto children = myPrim.GetFilteredChildren(myPredicate);
    for (auto i = children.begin(); i != children.end(); ++i, ++count)
    { }

    if(count == 0)
        return NULL;

    setRefCount(count);
    recycleAsContinuation();

    const int last = count - 1;
    int idx = 0;
    for (const auto &child : myPrim.GetFilteredChildren(myPredicate))
    {
        auto& task = *new(allocate_child())
            XUSD_FindPrimsTask(child, myData, myPredicate, myPattern);

        if(idx == last)
            return &task;
        else
            spawnChild(task);
        ++idx;
    }

    // We should never get here.
    return NULL;
}

PXR_NAMESPACE_CLOSE_SCOPE

