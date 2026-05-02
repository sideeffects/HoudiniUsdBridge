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

#ifndef __XUSD_FindPrimsTask_h__
#define __XUSD_FindPrimsTask_h__

#include "HUSD_API.h"
#include "HUSD_TimeCode.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <UT/UT_PathPattern.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <SYS/SYS_Deprecated.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_OPEN_SCOPE
SYS_DEPRECATED_PUSH_DISABLE()

class XUSD_SimpleAutoCollection;

// Generic base class for gathering per-prim information during a multithreaded
// traversal of a stage using an XUSD_PathPatthern to restrict the traversal.
class HUSD_API XUSD_FindPrimsTaskData
{
public:
    XUSD_FindPrimsTaskData() = default;
    virtual ~XUSD_FindPrimsTaskData();

    // Called when a matching prim is found.
    virtual void         addToThreadData(const UsdPrim &prim,
                            bool *prune) = 0;
    // Called when a match is made to point instancer instances.
    virtual void         addInstanceIdsToThreadData(const UsdPrim &prim,
                            const UT_Array<int64> &&instance_ids)
                         { }

    /// Generally speaking, HUSD_FindPrims will never return the
    /// HoudiniLayerInfo prim. But there are some circumstances where we
    /// may wish to allow it.
    void                 setAllowHoudiniLayerInfo(bool allow)
                         { myAllowHoudiniLayerInfo = allow; }
    bool                 allowHoudiniLayerInfo() const
                         { return myAllowHoudiniLayerInfo; }
    /// Instance ID collection for point instancers. When enabled,
    /// the traversal task stores IDs returned by matchPrimitive.
    void                 setCollectInstanceIds(bool collect)
                         { myCollectInstanceIds = collect; }
    bool                 collectsInstanceIds() const
                         { return myCollectInstanceIds; }

private:
    bool                 myAllowHoudiniLayerInfo = false;
    bool                 myCollectInstanceIds = false;
};

// Subclass of XUSD_FindPrimsTaskData that specifically collects the SdfPaths
// of all USD prims found in the traversal into an XUSD_PathSet.
class HUSD_API XUSD_FindPrimPathsTaskData : public XUSD_FindPrimsTaskData
{
public:
    XUSD_FindPrimPathsTaskData(const HUSD_TimeCode &timecode)
        : myTimeCode(timecode)
    { }
    ~XUSD_FindPrimPathsTaskData() override;

    XUSD_FindPrimPathsTaskData(const XUSD_FindPrimPathsTaskData &) = delete;
    XUSD_FindPrimPathsTaskData &operator=(const XUSD_FindPrimPathsTaskData &)
            = delete;

    // If we are collecting instance ids and the prim is a point instancer,
    // all ids will be added from this instancer.
    void                 addToThreadData(const UsdPrim &prim,
                            bool *prune) override;
    // Record the point instancer instance ids matching a collection.
    void                 addInstanceIdsToThreadData(const UsdPrim &prim,
                            const UT_Array<int64> &&instance_ids) override;
    void                 gatherDataFromThreads(XUSD_PathSet &paths,
                            UT_StringMap<UT_Array<int64>> *instance_ids);

private:
    class FindPrimPathsTaskThreadData
    {
    public:
        SdfPathVector                       myPaths;
        UT_StringMap<UT_Array<int64>>       myInstanceIds;
    };
    using FindPrimPathsTaskThreadDataTLS =
        UT_ThreadSpecificValue<FindPrimPathsTaskThreadData *>;

    FindPrimPathsTaskThreadDataTLS  myThreadData;
    HUSD_TimeCode                   myTimeCode;
};

// Subclass of XUSD_FindPrimsTaskData that specifically collects the UsdPrims
// of all USD prims found in the traversal into a UT_Array<UsdPrim>.
class HUSD_API XUSD_FindUsdPrimsTaskData : public XUSD_FindPrimsTaskData
{
public:
    XUSD_FindUsdPrimsTaskData() = default;
    ~XUSD_FindUsdPrimsTaskData() override;

    XUSD_FindUsdPrimsTaskData(const XUSD_FindUsdPrimsTaskData &) = delete;
    XUSD_FindUsdPrimsTaskData &operator=(const XUSD_FindUsdPrimsTaskData &)
            = delete;

    void addToThreadData(const UsdPrim &prim, bool *prune) override;

    void gatherPrimsFromThreads(UT_Array<UsdPrim> &prims);
    void gatherPrimsFromThreads(std::vector<UsdPrim> &prims);

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

// Perform a multithreaded traversal of a stage guided by a UT_PathPattern.
// Data is collected into an XUSD_FindPrimsTaskData object by calling its
// addToThreadData method with all matching prims.
HUSD_API void
XUSDfindPrims(
        const UsdPrim& prim,
        XUSD_FindPrimsTaskData &data,
        const Usd_PrimFlagsPredicate &predicate);
HUSD_API void
XUSDfindPrims(
        const UsdPrim& prim,
        XUSD_FindPrimsTaskData &data,
        const Usd_PrimFlagsPredicate &predicate,
        const UT_PathPattern *pattern);
HUSD_API void
XUSDfindPrims(
        const UsdPrim& prim,
        XUSD_FindPrimsTaskData &data,
        const Usd_PrimFlagsPredicate &predicate,
        const XUSD_SimpleAutoCollection *autocollection);

SYS_DEPRECATED_POP_DISABLE()
PXR_NAMESPACE_CLOSE_SCOPE

#endif

