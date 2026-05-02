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

#ifndef __XUSD_ExistenceTracker_h__
#define __XUSD_ExistenceTracker_h__

#include "HUSD_API.h"
#include "XUSD_PathSet.h"
#include <UT/UT_Assert.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/layer.h>

PXR_NAMESPACE_OPEN_SCOPE

class HUSD_API XUSD_ExistenceTracker
{
public:
                     XUSD_ExistenceTracker();
                    ~XUSD_ExistenceTracker();

    void             collectNewStageData(const UsdStageRefPtr &newstage);
    void             authorVisibility(const UsdStageRefPtr &combinedstage,
                            const UsdTimeCode &timecode);

    SdfLayerRefPtr   getVisibilityLayer() const
                     { return myVisibilityLayer; }
    void             setVisibilityLayer(SdfLayerRefPtr layer)
                     {
                         UT_ASSERT(!myVisibilityLayer);
                         myVisibilityLayer = layer;
                     }

private:
    SdfLayerRefPtr           myVisibilityLayer;
    XUSD_PathSet             myOldPaths;
    XUSD_PathSet             myNewPaths;
    std::map<SdfPath, bool>  myModifiedPaths;
    UsdTimeCode              myOldTimeCode;
    UsdTimeCode              myNewTimeCode;
    bool                     myFirstUse;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

