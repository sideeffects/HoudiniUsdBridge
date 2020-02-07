/*
 * Copyright 2020 Side Effects Software Inc.
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

#ifndef __GEO_HAPI_PART_H__
#define __GEO_HAPI_PART_H__

#include "GEO_FilePrim.h"
#include "GEO_FilePrimUtils.h"
#include "GEO_HAPIAttribute.h"
#include "pxr/base/tf/declarePtrs.h"
#include "pxr/pxr.h"
#include "pxr/usd/sdf/abstractData.h"
#include "pxr/usd/sdf/data.h"
#include "pxr/usd/sdf/fileFormat.h"
#include <UT/UT_StringMap.h>
#include <UT/UT_UniquePtr.h>

PXR_NAMESPACE_USING_DIRECTIVE

typedef UT_UniquePtr<GEO_HAPIAttribute> GEO_HAPIAttributeHandle;

/// \class GEO_HAPIPart
///
/// Reads and stores Houdini Engine Primitive Data.
/// It also contains functions for converting its data
/// to USD.
///
class GEO_HAPIPart
{
public:

    GEO_HAPIPart();
    ~GEO_HAPIPart();

    // Copy constructor so this may be stored in UT_Arrays
    // Do not use this
    explicit GEO_HAPIPart(const GEO_HAPIPart &part) {}

    bool loadPartData(const HAPI_Session &session,
                      HAPI_GeoInfo &geo,
                      HAPI_PartInfo &part,
                      UT_WorkBuffer &buf);

    // Fills the given bounding box with the part's bounds
    void getBounds(UT_BoundingBoxR &bboxOut);

    // Fills the given matrix with the part's transform
    void getXForm(UT_Matrix4D &xformOut);

    HAPI_PartType getType() { return myType; }

    // USD Functions

    void setupPrimAttributes(GEO_FilePrim &filePrim,
                             const GEO_ImportOptions &options,
                             const GT_DataArrayHandle &vertexIndirect);

    // Returns false if the prim is undefined and
    // no more work should be done on it
    bool setupPrimType(GEO_FilePrim &filePrim,
                       const GEO_ImportOptions &options,
                       GT_DataArrayHandle &vertexIndirect);

private:

    // Geometry metadata structs

    // General Data
    struct PartData
    {
        virtual ~PartData() = default;

        GT_DataArrayHandle faceCounts;
        GT_DataArrayHandle vertices;
    };

    struct SphereData : PartData
    {
        UT_Vector3F center;
        float radius = 0.f;
    };

    bool checkAttrib(UT_StringHolder &attribName,
                     const GEO_ImportOptions &options);

    // USD Functions

    void setupExtraPrimAttributes(GEO_FilePrim &filePrim,
                                  UT_ArrayStringSet &processedAttribs,
                                  const GEO_ImportOptions &options,
                                  const GT_DataArrayHandle &vertexIndirect);

    template <class DT, class ComponentDT = DT>
    GEO_FileProp *applyAttrib(GEO_FilePrim &filePrim,
                              const GEO_HAPIAttributeHandle &attrib,
                              const TfToken &usdAttribName,
                              const SdfValueTypeName &usdTypeName,
                              UT_ArrayStringSet &processedAttribs,
                              bool createIndicesAttrib,
                              const GEO_ImportOptions &options,
                              const GT_DataArrayHandle &vertexIndirect);

    void convertExtraAttrib(GEO_FilePrim &filePrim,
                            GEO_HAPIAttributeHandle &attrib,
                            const TfToken &usdAttribName,
                            UT_ArrayStringSet &processedAttribs,
                            bool createIndicesAttrib,
                            const GEO_ImportOptions &options,
                            const GT_DataArrayHandle &vertexIndirect);

    HAPI_PartType myType;
    UT_StringArray myAttribNames;
    UT_StringMap<GEO_HAPIAttributeHandle> myAttribs;

    // This can be a PartData or any inheriting struct
    // The actual type of myData can be determined with myType
    typedef UT_UniquePtr<PartData> PartDataHandle;
    PartDataHandle myData;
};

#endif // __GEO_HAPI_PART_H__