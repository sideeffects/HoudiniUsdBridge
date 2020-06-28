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

#ifndef __HUSD_FieldWrapper__
#define __HUSD_FieldWrapper__

#include <gusd/primWrapper.h>
#include <pxr/usd/usdVol/fieldAsset.h>

PXR_NAMESPACE_OPEN_SCOPE

/// GusdPrimWrapper implementation for converting USD fields back to
/// GT_PrimVolume or GT_PrimVDB primitives.
class HUSD_FieldWrapper : public GusdPrimWrapper
{
public:
    HUSD_FieldWrapper(const UsdVolFieldAsset &usd_field, UsdTimeCode t,
                      GusdPurposeSet purposes);
    ~HUSD_FieldWrapper() override;

    static void registerForRead();

    const UsdGeomImageable         getUsdPrim() const override
    {
        return myUsdField;
    }

    const char         *className() const override;

    void
    enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int         getMotionSegments() const override;

    int64         getMemoryUsage() const override;

    GT_PrimitiveHandle         doSoftCopy() const override;

    bool         isValid() const override;

    bool         refine(GT_Refine &refiner,
                        const GT_RefineParms *parms = nullptr) const override;

public:
    static GT_PrimitiveHandle
    defineForRead(const UsdGeomImageable &source_prim, UsdTimeCode time,
                  GusdPurposeSet purposes);

private:
    bool initUsdPrim(const UsdStagePtr &stage, const SdfPath &path,
                     bool as_override);

    UsdVolFieldAsset myUsdField;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
