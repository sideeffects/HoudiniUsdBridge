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

#include "HDRPrim.h"
#include "HDUtil.h"
#include <UT/UT_Assert.h>
#include <SYS/SYS_TypeTraits.h>

#include <pxr/imaging/hd/changeTracker.h>

PXR_NAMESPACE_OPEN_SCOPE        // [

static HdInterpolation  theCurveInterp[] = {
    HdInterpolationVarying,
    HdInterpolationVertex,
    HdInterpolationFaceVarying,
    HdInterpolationConstant,
    HdInterpolationUniform,
};

static HdInterpolation  theMeshInterp[] = {
    HdInterpolationVarying,
    HdInterpolationVertex,
    HdInterpolationFaceVarying,
    HdInterpolationConstant,
    HdInterpolationUniform,
};

static HdInterpolation  thePointInterp[] = {
    HdInterpolationVertex,
    HdInterpolationConstant,
};

static HdInterpolation  theVolumeInterp[] = {
    HdInterpolationConstant,
};

static void
dumpGeo(HdSceneDelegate &sd,
        const char *name,
        const SdfPath &id,
        const HDUtil::Primvars &main,
        const HDUtil::Primvars *extra)
{
    UT_WorkBuffer       msg;
    int64               mem = main.myMemory;
    if (extra)
        mem += extra->myMemory;
    UT_WorkBuffer       membuf;
    membuf.printMemory(mem);
    msg.format("Sync {} {} ({}):\n    SharingId: {}\n    Primvars:\n{}",
            name, id, membuf, sd.GetDataSharingId(id), main.toString());
    if (extra)
        msg.appendFormat("{}", extra->toString());
    UTformat("{}", msg);
}

#define IMPL_COMMON_RPRIM(CLASS, INTERPTYPES) \
    void CLASS::Finalize(HdRenderParam *param) { HDEBUG_TRACE_FUNCTION(); } \
    void CLASS::Sync(HdSceneDelegate *sd, \
                HdRenderParam *param, \
                HdDirtyBits *dirtyBits, \
                const TfToken &repr) { \
        HDEBUG_TRACE_FUNCTION(); \
        HDUtil::load(myPrimvars, true, *sd, *(UTverify_cast<HDParam *>(param)), \
                GetId(), INTERPTYPES, SYSarraySize(INTERPTYPES)); \
        HDUtil::resolveMaterial(*sd, *(UTverify_cast<HDParam *>(param)), \
                GetId()); \
        syncSpecific(sd, param); \
        if (HDOptions::geo()) \
            dumpGeo(*sd, class_name, GetId(), myPrimvars, extraPrimvars()); \
        *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits; \
    } \
    HdDirtyBits CLASS::GetInitialDirtyBitsMask() const { \
        return HdChangeTracker::AllSceneDirtyBits; \
    } \
    void CLASS::UpdateRenderTag(HdSceneDelegate *sd, \
                HdRenderParam *param) { \
        HDEBUG_TRACE_FUNCTION(); \
        CLASS::BaseClass::UpdateRenderTag(sd, param); \
    } \
    HdDirtyBits CLASS::_PropagateDirtyBits(HdDirtyBits bits) const {\
        HDEBUG_TRACE_FUNCTION(); \
        return bits; \
    } \
    void CLASS::_InitRepr(const TfToken &repr, HdDirtyBits *dirtyBits) { \
        HDEBUG_TRACE_FUNCTION(); \
        TF_UNUSED(repr); \
        TF_UNUSED(dirtyBits); \
    } \
    /* end macro */

IMPL_COMMON_RPRIM(HDCurves, theCurveInterp)
IMPL_COMMON_RPRIM(HDMesh, theMeshInterp)
IMPL_COMMON_RPRIM(HDPoints, thePointInterp)
IMPL_COMMON_RPRIM(HDVolume, theVolumeInterp)

HDCurves::HDCurves(const SdfPath &id)
    : HdBasisCurves(id)
{
    HDEBUG_CTOR();
}

HDCurves::~HDCurves()
{
    HDEBUG_DTOR();
}

HDMesh::HDMesh(const SdfPath &id)
    : HdMesh(id)
{
    HDEBUG_CTOR();
}

HDMesh::~HDMesh()
{
    HDEBUG_DTOR();
}

void
HDMesh::syncSpecific(HdSceneDelegate *sd, HdRenderParam *param)
{
    auto refineLevel = sd->GetDisplayStyle(GetId()).refineLevel;
    load(myTopology, GetId(), HdMeshTopology(GetMeshTopology(sd), refineLevel));
}

HDPoints::HDPoints(const SdfPath &id)
    : HdPoints(id)
{
    HDEBUG_CTOR();
}

HDPoints::~HDPoints()
{
    HDEBUG_DTOR();
}

HDVolume::HDVolume(const SdfPath &id)
    : HdVolume(id)
{
    HDEBUG_CTOR();
}

HDVolume::~HDVolume()
{
    HDEBUG_DTOR();
}

PXR_NAMESPACE_CLOSE_SCOPE       // ]
