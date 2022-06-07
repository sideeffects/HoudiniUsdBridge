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

#include "BRAY_HdCoordSys.h"
#include "BRAY_HdFormat.h"
#include "BRAY_HdParam.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdCamera.h"
#include <UT/UT_ErrorLog.h>
#include <UT/UT_SmallArray.h>
#include <pxr/imaging/hd/changeTracker.h>

PXR_NAMESPACE_OPEN_SCOPE

BRAY_HdCoordSys::BRAY_HdCoordSys(const SdfPath &id)
    : HdCoordSys(id)
{
    //UTdebugFormat("NewCoordSys: {}", id);
}

BRAY_HdCoordSys::~BRAY_HdCoordSys()
{
    UT_ASSERT(!myCoordSys);
}

void
BRAY_HdCoordSys::Finalize(HdRenderParam *renderParam)
{
    //UTdebugFormat("Finalize: {}", GetId());
    if (myCoordSys)
    {
        BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
        BRAY::ScenePtr  &scene = rparm->getSceneForEdit();
        scene.updateCoordSys(myCoordSys, BRAY_EVENT_DEL);
        myCoordSys = BRAY::CoordSysPtr();
    }
}

void
BRAY_HdCoordSys::Sync(HdSceneDelegate *sd,
        HdRenderParam *renderParam,
        HdDirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    BRAY_HdParam        *rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr      &scene = rparm->getSceneForEdit();

    if (!myCoordSys)
    {
        UT_StringHolder name = BRAY_HdUtil::toStr(GetId());
        myCoordSys = scene.createCoordSys(name);
        UT_ErrorLog::format(8, "Create coord-sys {}", name);
    }

    if (*dirtyBits)
    {
        BRAY_HdCameraProps      cpropset;
        cpropset.init<BRAY_HdUtil::EVAL_GENERIC>(sd, *rparm, GetId(),
                myCoordSys.objectProperties());
        cpropset.setProperties(scene, myCoordSys);
    }

    myCoordSys.commit(scene);

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits
BRAY_HdCoordSys::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
