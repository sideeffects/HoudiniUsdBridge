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
#include <UT/UT_WorkBuffer.h>


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
    // data is not copied, only the pointer is saved by the object
    GEO_HAPIAttribute(UT_StringRef name,
                      HAPI_AttributeOwner owner,
                      int count,
                      int tupleSize,
                      HAPI_StorageType dataType,
                      GT_DataArray *data);

    bool loadAttrib(const HAPI_Session &session,
                    HAPI_GeoInfo &geo,
                    HAPI_PartInfo &part,
                    HAPI_AttributeOwner owner,
                    HAPI_AttributeInfo &attribInfo,
                    UT_StringHolder &attribName,
                    UT_WorkBuffer &buf);

    // Stuff zeros into the data array if the tuple size is increased
    // Truncate tuples if the tuple size is decreased
    // This is useful if the tuple size of a standard attribute is unexpected
    void convertTupleSize(int newSize);

    template <class DT>
    void updateTupleData(int newSize);

    UT_StringHolder myName;

    HAPI_AttributeOwner myOwner;
    int myCount;
    int myTupleSize;

    HAPI_AttributeTypeInfo myTypeInfo;
    HAPI_StorageType myDataType;
    GT_DataArrayHandle myData;

};

#endif // __GEO_HAPI_ATTRIBUTE_H__