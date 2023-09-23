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

#include "GEO_HAPIAttribute.h"
#include "GEO_HAPIUtils.h"
#include <GT/GT_DAIndexedString.h>
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DAList.h>
#include <GT/GT_DANumeric.h>
#include <GT/GT_DAVaryingArray.h>
#include <UT/UT_Assert.h>
#include <UT/UT_VarEncode.h>

GEO_HAPIAttribute::GEO_HAPIAttribute()
    : myOwner(HAPI_ATTROWNER_INVALID)
    , myTypeInfo(HAPI_ATTRIBUTE_TYPE_INVALID)
    , myDataType(HAPI_STORAGETYPE_INVALID)
{
}

GEO_HAPIAttribute::GEO_HAPIAttribute(const UT_StringHolder &name,
                                     HAPI_AttributeOwner owner,
                                     HAPI_StorageType dataType,
                                     const GT_DataArrayHandle &data,
                                     HAPI_AttributeTypeInfo typeInfo)
    : myName(name)
    , myDecodedName(UT_VarEncode::decodeAttrib(name))
    , myOwner(owner)
    , myDataType(dataType)
    , myTypeInfo(typeInfo)
    , myData(data)
{
}

GEO_HAPIAttribute::~GEO_HAPIAttribute() {}

bool
GEO_HAPIAttribute::loadAttrib(const HAPI_Session &session,
                              HAPI_GeoInfo &geo,
                              HAPI_PartInfo &part,
                              HAPI_AttributeOwner owner,
                              HAPI_AttributeInfo &attribInfo,
                              UT_StringHolder &attribName,
                              UT_WorkBuffer &buf)
{
    if (!attribInfo.exists)
    {
        return false;
    }
    // Save relavent information
    myName = attribName;
    myDecodedName = UT_VarEncode::decodeAttrib(attribName);
    myOwner = owner;
    myDataType = attribInfo.storage;
    myTypeInfo = attribInfo.typeInfo;

    int count = attribInfo.count;

    if (count > 0)
    {
        int tupleSize = attribInfo.tupleSize;
        const GT_Type type = GEOhapiAttribType(myTypeInfo);

        switch (myDataType)
        {
        case HAPI_STORAGETYPE_INT_ARRAY:
        case HAPI_STORAGETYPE_INT64_ARRAY:
        case HAPI_STORAGETYPE_FLOAT_ARRAY:
        case HAPI_STORAGETYPE_FLOAT64_ARRAY:
        case HAPI_STORAGETYPE_STRING_ARRAY:
        {
            CHECK_RETURN(loadArrayAttrib(
                    session, geo, part, owner, attribInfo, attribName, buf));
            break;
        }

        case HAPI_STORAGETYPE_INT:
        {
            GT_DANumeric<int> *data = new GT_DANumeric<int>(
                    count, tupleSize, type);
            myData.reset(data);

            ENSURE_SUCCESS(
                    HAPI_GetAttributeIntData(
                            &session, geo.nodeId, part.id, myName.c_str(),
                            &attribInfo, -1, data->data(), 0, count),
                    session);

            break;
        }

        case HAPI_STORAGETYPE_INT64:
        {
            GT_DANumeric<int64> *data = new GT_DANumeric<int64>(
                    count, tupleSize, type);
            myData.reset(data);

            // Ensure that the HAPI_Int64 we are given are of an expected
            // size
            SYS_STATIC_ASSERT(sizeof(HAPI_Int64) == sizeof(int64));
            HAPI_Int64 *hData = reinterpret_cast<HAPI_Int64 *>(data->data());

            ENSURE_SUCCESS(
                    HAPI_GetAttributeInt64Data(
                            &session, geo.nodeId, part.id, myName.c_str(),
                            &attribInfo, -1, hData, 0, count),
                    session);

            break;
        }

        case HAPI_STORAGETYPE_FLOAT:
        {
            GT_DANumeric<float> *data = new GT_DANumeric<float>(
                    count, tupleSize, type);
            myData.reset(data);

            ENSURE_SUCCESS(
                    HAPI_GetAttributeFloatData(
                            &session, geo.nodeId, part.id, myName.c_str(),
                            &attribInfo, -1, data->data(), 0, count),
                    session);

            break;
        }

        case HAPI_STORAGETYPE_FLOAT64:
        {
            GT_DANumeric<double> *data = new GT_DANumeric<double>(
                    count, tupleSize, type);
            myData.reset(data);

            ENSURE_SUCCESS(
                    HAPI_GetAttributeFloat64Data(
                            &session, geo.nodeId, part.id, myName.c_str(),
                            &attribInfo, -1, data->data(), 0, count),
                    session);

            break;
        }

        case HAPI_STORAGETYPE_STRING:
        {
            auto handles = UTmakeUnique<HAPI_StringHandle[]>(count * tupleSize);

            ENSURE_SUCCESS(
                    HAPI_GetAttributeStringData(
                            &session, geo.nodeId, part.id, myName.c_str(),
                            &attribInfo, handles.get(), 0, count),
                    session);

            GT_DAIndexedString *data = new GT_DAIndexedString(count, tupleSize);
            myData.reset(data);

            UT_ArrayMap<HAPI_StringHandle, GT_Offset> string_indices;
            for (exint i = 0; i < count; i++)
            {
                for (exint j = 0; j < tupleSize; j++)
                {
                    HAPI_StringHandle handle = handles[(i * tupleSize) + j];

                    // The HAPI_StringHandle values tell us which strings
                    // are shared, so by recording the resulting string
                    // index in GT_DAIndexedString we can reduce calls to
                    // HAPI_GetString().
                    auto it = string_indices.find(handle);
                    if (it == string_indices.end())
                    {
                        CHECK_RETURN(
                                GEOhapiExtractString(session, handle, buf));

                        data->setString(i, j, buf);

                        const int string_idx = data->getStringIndex(i, j);
                        string_indices.emplace(handle, string_idx);
                    }
                    else
                        data->setStringIndex(i, j, it->second);
                }
            }

            break;
        }

        default:
            UT_ASSERT_MSG(false, "Unsupported attribute type");
            return false;
        }
    }

    return true;
}

bool
GEO_HAPIAttribute::loadArrayAttrib(
    const HAPI_Session &session,
    HAPI_GeoInfo &geo,
    HAPI_PartInfo &part,
    HAPI_AttributeOwner owner,
    HAPI_AttributeInfo &attribInfo,
    UT_StringHolder &attribName,
    UT_WorkBuffer &buf)
{
    int arrayCount = attribInfo.count;
    int tupleSize = attribInfo.tupleSize;
    UT_ASSERT(tupleSize > 0);
    int totalElements = attribInfo.totalArrayElements;
    int totalTuples = totalElements / tupleSize;
    const GT_Type type = GEOhapiAttribType(myTypeInfo);

    auto lengths = UTmakeIntrusive<GT_DANumeric<int>>(arrayCount, 1);
    GT_DataArrayHandle data;

    switch (myDataType)
    {
    case HAPI_STORAGETYPE_INT_ARRAY:
    {
        auto values = UTmakeIntrusive<GT_DANumeric<int>>(
                totalTuples, tupleSize, type);
        data = values;

	ENSURE_SUCCESS(
                HAPI_GetAttributeIntArrayData(
                        &session, geo.nodeId, part.id, myName.c_str(),
                        &attribInfo, values->data(), totalElements,
                        lengths->data(), 0, arrayCount),
                session);

	break;
    }

    case HAPI_STORAGETYPE_INT64_ARRAY:
    {
        auto values = UTmakeIntrusive<GT_DANumeric<int64>>(
                totalTuples, tupleSize, type);
        data = values;

        SYS_STATIC_ASSERT(sizeof(HAPI_Int64) == sizeof(int64));
        HAPI_Int64 *hData = reinterpret_cast<HAPI_Int64 *>(values->data());

        ENSURE_SUCCESS(
                HAPI_GetAttributeInt64ArrayData(
                        &session, geo.nodeId, part.id, myName.c_str(),
                        &attribInfo, hData, totalElements,
                        lengths->data(), 0, arrayCount),
                session);

        break;
    }

    case HAPI_STORAGETYPE_FLOAT_ARRAY:
    {
        auto values = UTmakeIntrusive<GT_DANumeric<float>>(
                totalTuples, tupleSize, type);
        data = values;

        ENSURE_SUCCESS(
                HAPI_GetAttributeFloatArrayData(
                        &session, geo.nodeId, part.id, myName.c_str(),
                        &attribInfo, values->data(), totalElements,
                        lengths->data(), 0, arrayCount),
                session);

        break;
    }

    case HAPI_STORAGETYPE_FLOAT64_ARRAY:
    {
        auto values = UTmakeIntrusive<GT_DANumeric<double>>(
                totalTuples, tupleSize, type);
        data = values;

        ENSURE_SUCCESS(
                HAPI_GetAttributeFloat64ArrayData(
                        &session, geo.nodeId, part.id, myName.c_str(),
                        &attribInfo, values->data(), totalElements,
                        lengths->data(), 0, arrayCount),
                session);

        break;
    }

    case HAPI_STORAGETYPE_STRING_ARRAY:
    {
        auto handles = UTmakeUnique<HAPI_StringHandle[]>(totalElements);

        ENSURE_SUCCESS(
                HAPI_GetAttributeStringArrayData(
                        &session, geo.nodeId, part.id, myName.c_str(),
                        &attribInfo, handles.get(), totalElements,
                        lengths->data(), 0, arrayCount),
                session);

        auto values = UTmakeIntrusive<GT_DAIndexedString>(
                totalTuples, tupleSize);
        data = values;

        UT_ArrayMap<HAPI_StringHandle, GT_Offset> string_indices;
        for (exint i = 0; i < totalTuples; i++)
        {
            for (exint j = 0; j < tupleSize; j++)
            {
                HAPI_StringHandle handle = handles[(i * tupleSize) + j];

                // The HAPI_StringHandle values tell us which strings
                // are shared, so by recording the resulting string
                // index in GT_DAIndexedString we can reduce calls to
                // HAPI_GetString().
                auto it = string_indices.find(handle);
                if (it == string_indices.end())
                {
                    CHECK_RETURN(GEOhapiExtractString(session, handle, buf));

                    values->setString(i, j, buf);

                    const int string_idx = values->getStringIndex(i, j);
                    string_indices.emplace(handle, string_idx);
                }
                else
                    values->setStringIndex(i, j, it->second);
            }
        }

        break;
    }

    default:
        UT_ASSERT_MSG(false, "Unsupported array attribute type");
        return false;
    }

    myData = UTmakeIntrusive<GT_DAVaryingArray>(data, GT_CountArray(lengths));

    return true;
}

void
GEO_HAPIAttribute::createElementIndirect(exint index, GEO_HAPIAttributeHandle &attrOut)
{
    UT_ASSERT(index >= 0 && index < myData->entries());

    GT_Int32Array *element = new GT_Int32Array(1, getTupleSize());
    element->data()[0] = index;

    attrOut.reset(new GEO_HAPIAttribute(myName, myOwner, myDataType,
                                        new GT_DAIndirect(element, myData),
                                        myTypeInfo));
}

static bool
checkCompatibility(const UT_Array<GEO_HAPIAttributeHandle> &attribs)
{
    const GEO_HAPIAttributeHandle &lhs = attribs(0);
    for (int i = 0; i < attribs.entries(); i++)
    {
        const GEO_HAPIAttributeHandle &rhs = attribs(i);

        if (lhs->myName != rhs->myName ||
	    lhs->myDataType != rhs->myDataType ||
	    lhs->myOwner != rhs->myOwner ||
	    lhs->myData->hasArrayEntries() != rhs->myData->hasArrayEntries() ||
            lhs->getTupleSize() != rhs->getTupleSize())
            return false;
    }
    return true;
}

GEO_HAPIAttributeHandle
GEO_HAPIAttribute::concatAttribs(
        const UT_Array<GEO_HAPIAttributeHandle> &attribs)
{
    if (attribs.entries() == 0)
        return nullptr;
    else if (attribs.entries() == 1)
        return UTmakeUnique<GEO_HAPIAttribute>(*attribs[0]);

    if (!checkCompatibility(attribs))
    {
        UT_ASSERT_MSG(false, "Cannot concatenate attributes");
        return nullptr;
    }

    UT_Array<GT_DataArrayHandle> data_array(attribs.size());
    for (const GEO_HAPIAttributeHandle &attrib : attribs)
        data_array.append(attrib->myData);

    auto out = UTmakeUnique<GEO_HAPIAttribute>(*attribs[0]);
    out->myData = UTmakeIntrusive<GT_DAList>(data_array);

    return out;
}

int64
GEO_HAPIAttribute::getMemoryUsage(bool inclusive) const
{
    int64 usage = inclusive ? sizeof(*this) : 0;
    usage += myName.getMemoryUsage(false);
    usage += myDecodedName.getMemoryUsage(false);
    usage = myData ? myData->getMemoryUsage() : 0;

    return usage;
}
