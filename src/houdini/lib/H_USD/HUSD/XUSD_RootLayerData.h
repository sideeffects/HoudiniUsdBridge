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

#ifndef __XUSD_RootLayerData_h__
#define __XUSD_RootLayerData_h__

#include "HUSD_API.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <map>

PXR_NAMESPACE_OPEN_SCOPE

class HUSD_API XUSD_RootLayerData
{
public:
			 XUSD_RootLayerData();
			 XUSD_RootLayerData(const UsdStageRefPtr &stage);
			 XUSD_RootLayerData(const SdfLayerRefPtr &layer);
			~XUSD_RootLayerData();

    bool                 isMetadataValueSet(const TfToken &field,
                                const VtValue &value) const;
    bool                 setMetadataValue(const TfToken &field,
                                const VtValue &value);

    // Stores data from a layer, or the root layer of a stage.
    void                 fromStage(const UsdStageRefPtr &stage);
    void                 fromLayer(const SdfLayerRefPtr &layer);
    // Sets data into a layer or the root layer of a stage. Returns true
    // if any values were changed, otherwise returns false.
    bool                 toStage(const UsdStageRefPtr &stage) const;
    bool                 toLayer(const SdfLayerRefPtr &layer) const;

private:
    std::map<TfToken, VtValue>   myRootMetadata;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

