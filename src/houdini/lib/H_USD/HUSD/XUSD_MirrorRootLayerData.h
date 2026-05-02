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

#ifndef __XUSD_MirrorRootLayerData_h__
#define __XUSD_MirrorRootLayerData_h__

#include "HUSD_API.h"
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>

#include <UT/UT_StringHolder.h>

PXR_NAMESPACE_OPEN_SCOPE

class HUSD_API XUSD_MirrorRootLayerData
{
public:
     XUSD_MirrorRootLayerData(const UT_StringRef &freecamsavepath = UT_StringRef());
    ~XUSD_MirrorRootLayerData();

    const SdfLayerRefPtr        &layer() const
                                 { return myLayer; }
    const SdfLayerRefPtr        &cameraLayer() const
                                 { return myCameraLayer; }
    void                         initializeLayerData();

private:
    SdfLayerRefPtr		 myCameraLayer;
    SdfLayerRefPtr		 myLayer;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

