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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#include "BRAY_HdLightFilter.h"
#include "BRAY_HdFormat.h"
#include "BRAY_HdParam.h"
#include <pxr/imaging/hd/changeTracker.h>

PXR_NAMESPACE_OPEN_SCOPE

BRAY_HdLightFilter::BRAY_HdLightFilter(const TfToken &typeId, const SdfPath &id)
    : HdSprim(id)
    , myTypeId(typeId)
{
    //UTdebugFormat("NewFilter: {} {}", myTypeId, id);
}

BRAY_HdLightFilter::~BRAY_HdLightFilter()
{
}

void
BRAY_HdLightFilter::Finalize(HdRenderParam *renderParam)
{
    //UTdebugFormat("Finalize: {}", GetId());
    BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
    rparm->finalizeLightFilter(GetId());
}

void
BRAY_HdLightFilter::Sync(HdSceneDelegate *sceneDelegate,
        HdRenderParam *renderParam,
        HdDirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    //UTdebugFormat("Sync: {}", GetId());
    BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
    rparm->updateLightFilter(sceneDelegate, GetId());

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits
BRAY_HdLightFilter::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
