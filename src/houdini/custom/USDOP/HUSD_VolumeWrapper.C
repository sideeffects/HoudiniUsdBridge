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
*/

#include "HUSD_VolumeWrapper.h"

#include "HUSD_FieldWrapper.h"

#include <pxr/usd/usdVol/fieldAsset.h>
#include <pxr/usd/usdVol/volume.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((volumePrimType, "Volume"))
);

void
HUSD_VolumeWrapper::registerForRead()
{
    static std::once_flag registered;

    std::call_once(registered, []() {
        GusdPrimWrapper::registerPrimDefinitionFuncForRead(
                _tokens->volumePrimType, &HUSD_VolumeWrapper::defineForRead);
    });
}

HUSD_VolumeWrapper::HUSD_VolumeWrapper(
        const UsdVolVolume &usd_volume,
        UsdTimeCode time,
        GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), myUsdVolume(usd_volume)
{
}

HUSD_VolumeWrapper::~HUSD_VolumeWrapper() {}

const char *
HUSD_VolumeWrapper::className() const
{
    return "HUSD_VolumeWrapper";
}

void
HUSD_VolumeWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "HUSD_VolumeWrapper::enlargeBounds not implemented");
}

int
HUSD_VolumeWrapper::getMotionSegments() const
{
    return 1;
}

int64
HUSD_VolumeWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
HUSD_VolumeWrapper::doSoftCopy() const
{
    return UTmakeIntrusive<HUSD_VolumeWrapper>(*this);
}

bool
HUSD_VolumeWrapper::isValid() const
{
    return static_cast<bool>(myUsdVolume);
}

bool
HUSD_VolumeWrapper::unpack(
        UT_Array<GU_DetailHandle> &details,
        const UT_StringRef &fileName,
        const SdfPath &primPath,
        const UT_Matrix4D &xform,
        fpreal frame,
        const char *viewportLod,
        GusdPurposeSet purposes,
        const GT_RefineParms &rparms) const
{
    if (!isValid())
    {
        TF_WARN("Invalid prim");
        return false;
    }

    // Directly unpack each of the field primitives.
    UsdStagePtr stage = myUsdVolume.GetPrim().GetStage();
    for (auto &&field : myUsdVolume.GetFieldPaths())
    {
        UsdVolFieldAsset field_prim(stage->GetPrimAtPath(field.second));
        if (!field_prim)
        {
            TF_WARN("Invalid field '%s' for volume '%s'",
                    field.second.GetAsString().c_str(),
                    myUsdVolume.GetPath().GetAsString().c_str());
            continue;
        }

        auto field_wrapper = UTmakeIntrusive<HUSD_FieldWrapper>(
                field_prim, m_time, m_purposes);
        field_wrapper->unpack(
                details, fileName, field_prim.GetPath(), xform, frame,
                viewportLod, purposes, rparms);
    }

    return true;
}

GT_PrimitiveHandle
HUSD_VolumeWrapper::defineForRead(
        const UsdGeomImageable &source_prim,
        UsdTimeCode time,
        GusdPurposeSet purposes)
{
    return UTmakeIntrusive<HUSD_VolumeWrapper>(
            UsdVolVolume(source_prim.GetPrim()), time, purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
