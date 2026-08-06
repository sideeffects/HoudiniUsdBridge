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

#include "HDSPrim.h"
#include "HDUtil.h"

#include <pxr/imaging/hd/changeTracker.h>

PXR_NAMESPACE_OPEN_SCOPE        // [

#define IMPL_COMMON_SPRIM(CLASS, BASE, MATERIAL) \
    CLASS::CLASS(const SdfPath &id) : BASE(id) { HDEBUG_CTOR(); } \
    CLASS::~CLASS() { HDEBUG_DTOR(); } \
    void CLASS::Finalize(HdRenderParam *param) { \
        HDEBUG_TRACE_FUNCTION(); \
        if (MATERIAL) { UTverify_cast<HDParam *>(param)->eraseMaterial(GetId()); } \
    } \
    void CLASS::Sync(HdSceneDelegate *sd, \
                HdRenderParam *param, \
                HdDirtyBits *dirtyBits) { \
        HDEBUG_TRACE_FUNCTION(); \
        if (MATERIAL) { UTverify_cast<HDParam *>(param)->updateMaterial(GetId()); } \
        *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits; \
    } \
    HdDirtyBits CLASS::GetInitialDirtyBitsMask() const { \
        HDEBUG_TRACE_FUNCTION(); \
        return HdChangeTracker::AllSceneDirtyBits; \
    } \
    /* end macro */

IMPL_COMMON_SPRIM(HDCamera, HdCamera, false)
IMPL_COMMON_SPRIM(HDCoordSys, HdCoordSys, false)
IMPL_COMMON_SPRIM(HDLight, HdLight, false)
IMPL_COMMON_SPRIM(HDLightFilter, HdSprim, false)
IMPL_COMMON_SPRIM(HDMaterial, HdMaterial, true)

PXR_NAMESPACE_CLOSE_SCOPE       // ]
