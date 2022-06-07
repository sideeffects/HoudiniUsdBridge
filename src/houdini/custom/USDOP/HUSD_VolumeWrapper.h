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

#ifndef __HUSD_VolumeWrapper__
#define __HUSD_VolumeWrapper__

#include <gusd/primWrapper.h>
#include <pxr/usd/usdVol/volume.h>

PXR_NAMESPACE_OPEN_SCOPE

/// GusdPrimWrapper implementation for refining a USD volume into a SOP volume
/// for each field.
class HUSD_VolumeWrapper : public GusdPrimWrapper
{
public:
    HUSD_VolumeWrapper(
            const UsdVolVolume &usd_volume,
            UsdTimeCode t,
            GusdPurposeSet purposes);
    ~HUSD_VolumeWrapper() override;

    static void registerForRead();

    const UsdGeomImageable getUsdPrim() const override { return myUsdVolume; }

    const char *className() const override;

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int getMotionSegments() const override;

    int64 getMemoryUsage() const override;

    GT_PrimitiveHandle doSoftCopy() const override;

    bool isValid() const override;

    bool unpack(
            UT_Array<GU_DetailHandle> &details,
            const UT_StringRef &fileName,
            const SdfPath &primPath,
            const UT_Matrix4D &xform,
            fpreal frame,
            const char *viewportLod,
            GusdPurposeSet purposes,
            const GT_RefineParms &rparms) const override;

    static GT_PrimitiveHandle defineForRead(
            const UsdGeomImageable &source_prim,
            UsdTimeCode time,
            GusdPurposeSet purposes);

private:
    bool initUsdPrim(
            const UsdStagePtr &stage,
            const SdfPath &path,
            bool as_override);

    UsdVolVolume myUsdVolume;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
