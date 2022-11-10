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

#ifndef __GEO_HAPI_ATTRIBUTE_H__
#define __GEO_HAPI_ATTRIBUTE_H__

#include <GT/GT_DAIndexedString.h>
#include <GT/GT_DataArray.h>
#include <HAPI/HAPI.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_WorkBuffer.h>

#include "GEO_FilePrimUtils.h"

class GEO_HAPIAttribute;
typedef UT_UniquePtr<GEO_HAPIAttribute> GEO_HAPIAttributeHandle;

/// \class GEO_HAPIAttribute
///
/// Wrapper class for Houdini Engine Attributes
///
class GEO_HAPIAttribute
{
public:
    GEO_HAPIAttribute();
    ~GEO_HAPIAttribute();

    // Convenience constructor
    GEO_HAPIAttribute(
            const UT_StringHolder &name,
            HAPI_AttributeOwner owner,
            HAPI_StorageType dataType,
            const GT_DataArrayHandle &data,
            HAPI_AttributeTypeInfo typeInfo = HAPI_ATTRIBUTE_TYPE_INVALID);

    bool loadAttrib(
            const HAPI_Session &session,
            HAPI_GeoInfo &geo,
            HAPI_PartInfo &part,
            HAPI_AttributeOwner owner,
            HAPI_AttributeInfo &attribInfo,
            UT_StringHolder &attribName,
            UT_WorkBuffer &buf);

    // Creates an attribute that points to a single element in this data array
    void createElementIndirect(exint index, GEO_HAPIAttributeHandle &attrOut);

    // Accessors for convenience
    SYS_FORCE_INLINE
    GT_Size entries() const { return myData->entries(); }
    SYS_FORCE_INLINE
    GT_Size getTupleSize() const { return myData->getTupleSize(); }

    // Increase or decrease the tuple size, which is useful if the tuple size
    // of a standard attribute is unexpected
    void convertTupleSize(
            int newSize,
            GEO_FillMethod method = GEO_FillMethod::Zero)
    {
        myData = GEOconvertTupleSize(myData, newSize, method);
    }

    // allocates a new attribute that holds concatenated data from all
    // attributes in attribs
    static GEO_HAPIAttributeHandle concatAttribs(
            const UT_Array<GEO_HAPIAttributeHandle> &attribs);

    int64 getMemoryUsage(bool inclusive) const;

    UT_StringHolder myName;
    UT_StringHolder myDecodedName;

    HAPI_AttributeOwner myOwner;
    HAPI_AttributeTypeInfo myTypeInfo;
    HAPI_StorageType myDataType;
    GT_DataArrayHandle myData;

private:
    bool loadArrayAttrib(
            const HAPI_Session &session,
            HAPI_GeoInfo &geo,
            HAPI_PartInfo &part,
            HAPI_AttributeOwner owner,
            HAPI_AttributeInfo &attribInfo,
            UT_StringHolder &attribName,
            UT_WorkBuffer &buf);
};

#endif // __GEO_HAPI_ATTRIBUTE_H__
