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

#include "HDParam.h"
#include "HDUtil.h"

PXR_NAMESPACE_OPEN_SCOPE

HDParam::HDParam(HdRenderThread &thread)
    : myMaterialLock()
{
    HDEBUG_CTOR();
}

HDParam::~HDParam()
{
    HDEBUG_DTOR();
}

void
HDParam::updateMaterial(const SdfPath &path)
{
    // Called on all material syncs
    UT_Lock::Scope      lock(myMaterialLock);
    myMaterials.insert(path);
}

void
HDParam::eraseMaterial(const SdfPath &path)
{
    UT_Lock::Scope      lock(myMaterialLock);
    if (!myMaterials.contains(path))
        UTformat("ERROR: Missing material - '{}'\n", path);
    myMaterials.erase(path);
}

bool
HDParam::hasMaterial(const SdfPath &path)
{
    UT_Lock::Scope      lock(myMaterialLock);
    return myMaterials.contains(path);
}

PXR_NAMESPACE_CLOSE_SCOPE
