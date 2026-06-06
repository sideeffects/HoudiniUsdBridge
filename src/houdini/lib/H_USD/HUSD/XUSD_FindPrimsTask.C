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
#include "XUSD_PathPattern.h"
#include "HUSD_Path.h"
#include <UT/UT_SysClone.h>
#include <UT/UT_Interrupt.h>
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
    // Note: instance ids are no longer collected here. They are supplied
    // explicitly by the caller via addInstanceIdsToThreadData() - either from
    // a pattern match payload (which already resolved the set operators on the
    // instance selections) or from an auto collection's matchPrimitive(). This
    // avoids unioning in the full instance set of every matched instancer,
    // which would defeat instance-level intersection/difference.
    threadData->myPaths.push_back(prim.GetPath());
}

void
XUSD_FindPrimPathsTaskData::addInstanceIdsToThreadData(const UsdPrim &prim,
        const UT_Array<int64> &&instance_ids)
{
    auto *&threaddata = myThreadData.get();
    if (!threaddata)
        threaddata = new FindPrimPathsTaskThreadData;
    threaddata->myInstanceIds[prim.GetPath().GetAsString()].concat(instance_ids);
}

void
XUSD_FindPrimPathsTaskData::gatherDataFromThreads(XUSD_PathSet &paths,
        UT_StringMap<UT_Array<int64>> *instance_ids)
{
    for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
    {
        if(const auto* tdata = it.get())
        {
            for (auto &&path : tdata->myPaths)
                paths.insert(path);
            if (instance_ids)
            {
                for (auto &&entry : tdata->myInstanceIds)
                {
                    auto it = instance_ids->find(entry.first);
                    if (it != instance_ids->end())
                    {
                        it->second.concat(entry.second);
                        it->second.sortAndRemoveDuplicates();
                    }
                    else
                        instance_ids->emplace(entry.first, entry.second);
                }
            }
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
            UT_TaskGroup &task_group,
            UT_AutoInterrupt &boss,
            UT_ThreadSpecificValue<int> &counters)
        : myPrim(prim)
        , myData(data)
        , myPredicate(predicate)
        , myPattern(pattern)
        , myAutoCollection(autocollection)
        , myTaskGroup(task_group)
        , myBoss(boss)
        , myCounters(counters)
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
    UT_AutoInterrupt                &myBoss;
    UT_ThreadSpecificValue<int>     &myCounters;
};

void
xusd_FindPrimsTask::operator()() const
{
    int &counter = myCounters.get();
    // Every 1000 prims on each thread, check if we have been interrupted.
    if (++counter > 1000)
    {
        if (myBoss.wasInterrupted())
            return;
        counter = 0;
    }

    // Ignore the HoudiniLayerInfo prim and all of its children.
    if (myPrim.GetPath() == HUSDgetHoudiniLayerInfoSdfPath() &&
        !myData.allowHoudiniLayerInfo())
        return;

    // Don't ever add the pseudoroot prim to the list of matches.
    if (myPrim.GetPath() != SdfPath::AbsoluteRootPath())
    {
        bool prune = false;

        if (myPattern)
        {
            HUSD_Path   primpath(myPrim.GetPath());

            if (myData.collectsInstanceIds())
            {
                // Carry a match payload so that set operators (intersect,
                // difference, ...) apply to the matched instance ids of a
                // point instancer, not just to whole prim paths.
                UT_PathPatternMatchDataPtr   match_data;

                if (myPattern->matches(primpath.pathStr(), &prune, match_data))
                {
                    myData.addToThreadData(myPrim, &prune);

                    const XUSD_InstanceMatchData *idsdata =
                        static_cast<const XUSD_InstanceMatchData *>(
                            match_data.get());
                    if (idsdata && !idsdata->myInstanceIds.isEmpty())
                        myData.addInstanceIdsToThreadData(myPrim,
                            UT_Array<int64>(idsdata->myInstanceIds));
                }
            }
            else if (myPattern->matches(primpath.pathStr(), &prune))
                myData.addToThreadData(myPrim, &prune);
        }
        else if (myAutoCollection)
        {
            if (myData.collectsInstanceIds())
            {
                UT_Array<int64> ids;
                if (myAutoCollection->matchPrimitive(myPrim, &prune, &ids))
                    myData.addToThreadData(myPrim, &prune);
                if (!ids.isEmpty())
                    myData.addInstanceIdsToThreadData(myPrim, std::move(ids));
            }
            else
            {
                if (myAutoCollection->matchPrimitive(
                        myPrim, &prune, nullptr))
                    myData.addToThreadData(myPrim, &prune);
            }
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
                myTaskGroup, myBoss, myCounters));
    }
}

void
findPrims(const UsdPrim& prim,
        XUSD_FindPrimsTaskData &data,
        const Usd_PrimFlagsPredicate &predicate,
        const UT_PathPattern *pattern,
        const XUSD_SimpleAutoCollection *autocollection)
{
    UT_TaskGroup tg;
    UT_AutoInterrupt boss("Finding primitives.");
    if (boss.wasInterrupted())
        return;
    UT_ThreadSpecificValue<int> counters;
    tg.runAndWait(xusd_FindPrimsTask(prim, data, predicate, pattern,
        autocollection, tg, boss, counters));
}

} // unnamed namespace

void
XUSDfindPrims(
        const UsdPrim& prim,
        XUSD_FindPrimsTaskData &data,
        const Usd_PrimFlagsPredicate &predicate)
{
    findPrims(prim, data, predicate, nullptr, nullptr);
}

void
XUSDfindPrims(
        const UsdPrim& prim,
        XUSD_FindPrimsTaskData &data,
        const Usd_PrimFlagsPredicate &predicate,
        const UT_PathPattern *pattern)
{
    findPrims(prim, data, predicate, pattern, nullptr);
}

void
XUSDfindPrims(
        const UsdPrim& prim,
        XUSD_FindPrimsTaskData &data,
        const Usd_PrimFlagsPredicate &predicate,
        const XUSD_SimpleAutoCollection *autocollection)
{
    findPrims(prim, data, predicate, nullptr, autocollection);
}

PXR_NAMESPACE_CLOSE_SCOPE
