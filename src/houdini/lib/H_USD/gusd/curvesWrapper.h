//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#ifndef __GUSD_CURVESWRAPPER_H__
#define __GUSD_CURVESWRAPPER_H__

#include "primWrapper.h"

#include "pxr/pxr.h"
#include "pxr/usd/usdGeom/basisCurves.h"

PXR_NAMESPACE_OPEN_SCOPE


class GusdCurvesWrapper : public GusdPrimWrapper
{
public:
    GusdCurvesWrapper( 
            const GT_PrimitiveHandle& sourcePrim,
            const UsdStagePtr& stage,
            const SdfPath& path,
            bool isOverride = false );
    GusdCurvesWrapper( 
            const UsdGeomBasisCurves&   usdCurves, 
            UsdTimeCode                 time,
            GusdPurposeSet              purposes );  

    ~GusdCurvesWrapper() override;

    const UsdGeomImageable getUsdPrim() const override { return m_usdCurves; }

    bool redefine( 
           const UsdStagePtr& stage,
           const SdfPath& path,
           const GusdContext& ctxt,
           const GT_PrimitiveHandle& sourcePrim ) override;
    
    const char* className() const override;

    void enlargeBounds(UT_BoundingBox boxes[], int nsegments) const override;

    int getMotionSegments() const override;

    int64 getMemoryUsage() const override;

    GT_PrimitiveHandle doSoftCopy() const override;

    bool
    updateFromGTPrim(const GT_PrimitiveHandle&  sourcePrim,
                     const UT_Matrix4D&         houXform,
                     const GusdContext&         ctxt,
                     GusdSimpleXformCache&      xformCache ) override;

    bool isValid() const override;

    bool refine(GT_Refine& refiner,
                const GT_RefineParms* parms=NULL) const override;
public:

    static GT_PrimitiveHandle
    defineForWrite( const GT_PrimitiveHandle& sourcePrim,
                    const UsdStagePtr& stage,
                    const SdfPath& path,
                    const GusdContext& ctxt);

    static GT_PrimitiveHandle
    defineForRead( const UsdGeomImageable&  sourcePrim, 
                   UsdTimeCode              time,
                   GusdPurposeSet           purposes );

private:
    bool initUsdPrim( const UsdStagePtr& stage,
                     const SdfPath& path,
                     bool asOverride);

    UsdGeomBasisCurves  m_usdCurves;
    bool                m_forceCreateNewGeo;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GUSD_CURVESWRAPPER_H__
