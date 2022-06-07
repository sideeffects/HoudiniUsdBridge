//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#ifndef _GUSD_THREADEDTRAVERSE_H_
#define _GUSD_THREADEDTRAVERSE_H_


#include <UT/UT_Array.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_TaskGroup.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <SYS/SYS_Deprecated.h>

#include "gusd/UT_Assert.h"
#include "gusd/USD_Traverse.h"
#include "gusd/USD_Utils.h"

#include "pxr/pxr.h"
#include "pxr/base/arch/hints.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usdGeom/imageable.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace GusdUSD_ThreadedTraverse {


template <class Visitor>
bool    ParallelFindPrims(const UsdPrim& root,
                          UsdTimeCode time,
                          GusdPurposeSet purposes,
                          UT_Array<UsdPrim>& prims,
                          const Visitor& visitor,
                          bool skipRoot=true);

template <class Visitor>
bool    ParallelFindPrims(const UT_Array<UsdPrim>& roots,
                          const GusdDefaultArray<UsdTimeCode>& times,
                          const GusdDefaultArray<GusdPurposeSet>& purposes,
                          UT_Array<GusdUSD_Traverse::PrimIndexPair>& prims,
                          const Visitor& visitor,
                          bool skipRoot=true);


/** Visitor for default-imageable prims.
    This takes @a Visitor as a child visitor to exec on each
    default-imageable prim.*/
template <class Visitor, bool Recursive=false>
struct DefaultImageablePrimVisitorT
{
    bool                    AcceptPrim(const UsdPrim& prim,
                                       UsdTimeCode time,
                                       GusdPurposeSet purposes,
                                       GusdUSD_TraverseControl& ctl) const;
    
    Usd_PrimFlagsPredicate  TraversalPredicate(bool allow_abstract) const
                            {
                                return allow_abstract
                                     ? UsdTraverseInstanceProxies(
                                        UsdPrimIsActive &&
                                        UsdPrimIsDefined &&
                                        UsdPrimIsLoaded)
                                     : UsdTraverseInstanceProxies(
                                        UsdPrimIsActive &&
                                        UsdPrimIsDefined &&
                                        UsdPrimIsLoaded &&
                                        !UsdPrimIsAbstract);
                            }
};


template <class Visitor, bool Recursive>
bool
DefaultImageablePrimVisitorT<Visitor,Recursive>::AcceptPrim(
    const UsdPrim& prim,
    UsdTimeCode time,
    GusdPurposeSet purposes,
    GusdUSD_TraverseControl& ctl) const
{
    UsdGeomImageable ip(prim);
    if(ip) {
        TfToken purpose;
        ip.GetPurposeAttr().Get(&purpose);
        if( GusdPurposeInSet( purpose, purposes )) {
            if(ARCH_UNLIKELY(Visitor()(prim, time, ctl))) {
                if(!Recursive)
                    ctl.PruneChildren();
                return true;
            }
        } else {
            ctl.PruneChildren();
        }
    } else {
        ctl.PruneChildren();
    }
    return false;
}


struct TaskThreadData
{
    UT_Array<GusdUSD_Traverse::PrimIndexPair>   prims;
};

typedef UT_ThreadSpecificValue<TaskThreadData*> TaskThreadDataTLS;


struct GUSD_API TaskData
{
    ~TaskData();

    TaskThreadDataTLS   threadData;

    /** Collect all of the prims from the numerous threads.
        The resulting prims are sorted (for determinism) */
    bool    GatherPrimsFromThreads(UT_Array<UsdPrim>& prims);

    bool    GatherPrimsFromThreads(
                UT_Array<GusdUSD_Traverse::PrimIndexPair>& prims);
};


/** Task for traversing a prim tree in parallel.

    See DefaultImageablePrimVisitorT<> for an example of the structure
    expected for visitors. */
template <class Visitor>
struct TraverseTaskT
{
    TraverseTaskT(UT_TaskGroup& taskgroup,
                  const UsdPrim& prim,  exint idx, UsdTimeCode time,
                  GusdPurposeSet purposes,
                  TaskData& data, const Visitor& visitor, bool skipPrim)
        :  _taskgroup(taskgroup), _prim(prim), _idx(idx), _time(time),
           _purposes(purposes), _data(data),
           _visitor(visitor), _skipPrim(skipPrim) {}

    void            operator()() const;

private:
    UT_TaskGroup&   _taskgroup;
    UsdPrim         _prim;
    exint           _idx;
    UsdTimeCode     _time;
    GusdPurposeSet  _purposes;
    TaskData&       _data;
    // _visitor is ok to be modified in operator() because we make copies of it
    // at each step. Unfortunately, functors passed into UT_TaskGroup must be
    // const so we mark it as mutable.
    mutable Visitor _visitor;
    bool            _skipPrim;
};


template <class Visitor>
void
TraverseTaskT<Visitor>::operator()() const
{
    UT_ASSERT_P(_prim);

    if(!_skipPrim) {

        GusdUSD_TraverseControl ctl;
        if(ARCH_UNLIKELY(_visitor.AcceptPrim(_prim, _time, _purposes, ctl))) {
            /* Matched. Add it to the thread-specific list.*/
            auto*& threadData = _data.threadData.get();
            if(!threadData)
                threadData = new TaskThreadData;
            threadData->prims.append(
                GusdUSD_Traverse::PrimIndexPair(_prim, _idx));
        }
        if(ARCH_UNLIKELY(!ctl.GetVisitChildren())) {
            return;
        }
    }

    auto predicate = _visitor.TraversalPredicate(_prim.IsAbstract());
    for (const auto& child : _prim.GetFilteredChildren(predicate)) {
        _taskgroup.run(TraverseTaskT(
                _taskgroup, child, _idx, _time, _purposes, _data, _visitor,
                /*skip prim*/ false));
    }
}


template <class Visitor>
bool
ParallelFindPrims(const UsdPrim& root,
                  UsdTimeCode time,
                  GusdPurposeSet purposes,
                  UT_Array<UsdPrim>& prims,
                  const Visitor& visitor,
                  bool skipRoot)
{
    TaskData data;
    bool skipPrim = skipRoot || root.GetPath() == SdfPath::AbsoluteRootPath();
    UT_TaskGroup tg;
    tg.runAndWait(TraverseTaskT<Visitor>(tg, root, -1, time, purposes,
                                         data, visitor, skipPrim));

    if(UTgetInterrupt()->opInterrupt())
        return false;
    
    return data.GatherPrimsFromThreads(prims);
}


template <class Visitor>
struct RunTasksT
{
    RunTasksT(const UT_Array<UsdPrim>& roots,
              const GusdDefaultArray<UsdTimeCode>& times,
              const GusdDefaultArray<GusdPurposeSet>& purposes,
              const Visitor& visitor, TaskData& data, bool skipRoot)
        : _roots(roots), _times(times), _purposes(purposes),
          _visitor(visitor), _data(data), _skipRoot(skipRoot) {}
    
    void    operator()(const UT_BlockedRange<std::size_t>& r) const
            {
                auto* boss = GusdUTverify_ptr(UTgetInterrupt());

                for(std::size_t i = r.begin(); i < r.end(); ++i)
                {
                    if(boss->opInterrupt())
                        return;
                    
                    if(const UsdPrim& prim = _roots(i)) {
                        bool skipPrim = _skipRoot ||
                            prim.GetPath() == SdfPath::AbsoluteRootPath();

                        UT_TaskGroup tg;
                        tg.runAndWait(TraverseTaskT<Visitor>(
                                tg, prim, i, _times(i), _purposes(i), _data,
                                _visitor, skipPrim));
                    }
                }
            }

private:
    const UT_Array<UsdPrim>&                _roots;
    const GusdDefaultArray<UsdTimeCode>&    _times;
    const GusdDefaultArray<GusdPurposeSet>& _purposes;
    const Visitor&                          _visitor;
    TaskData&                               _data;
    const bool                              _skipRoot;
};




template <class Visitor>
bool
ParallelFindPrims(const UT_Array<UsdPrim>& roots,
                  const GusdDefaultArray<UsdTimeCode>& times,
                  const GusdDefaultArray<GusdPurposeSet>& purposes,
                  UT_Array<GusdUSD_Traverse::PrimIndexPair>& prims,
                  const Visitor& visitor,
                  bool skipRoot)
{
    TaskData data;
    UTparallelFor(UT_BlockedRange<std::size_t>(0, roots.size()),
                  RunTasksT<Visitor>(roots, times, purposes,
                                     visitor, data, skipRoot));
    if(UTgetInterrupt()->opInterrupt())
        return false;

    return data.GatherPrimsFromThreads(prims);
}


} /*namespace GusdUSD_ThreadedTraverse*/

PXR_NAMESPACE_CLOSE_SCOPE

#endif /*_GUSD_THREADEDTRAVERSE_H_*/
