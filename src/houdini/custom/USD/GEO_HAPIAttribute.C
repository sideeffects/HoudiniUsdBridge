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
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DANumeric.h>
#include <UT/UT_Assert.h>

GEO_HAPIAttribute::GEO_HAPIAttribute()
    : myOwner(HAPI_ATTROWNER_INVALID), myDataType(HAPI_STORAGETYPE_INVALID)
{
}

GEO_HAPIAttribute::GEO_HAPIAttribute(UT_StringRef name,
                                     HAPI_AttributeOwner owner,
                                     HAPI_StorageType dataType,
                                     const GT_DataArrayHandle &data,
                                     HAPI_AttributeTypeInfo typeInfo)
    : myName(name),
      myOwner(owner),
      myDataType(dataType),
      myTypeInfo(typeInfo),
      myData(data)
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
    myOwner = owner;
    myDataType = attribInfo.storage;
    myTypeInfo = attribInfo.typeInfo;

    int count = attribInfo.count;
    int tupleSize = attribInfo.tupleSize;
    const GT_Type type = GEOhapiAttribType(myTypeInfo);

    if (count > 0)
    {
        // Put the attribute data into myData
        switch (myDataType)
        {
        case HAPI_STORAGETYPE_INT:
        {
            GT_DANumeric<int> *data = new GT_DANumeric<int>(
                count, tupleSize, type);
            myData.reset(data);

            ENSURE_SUCCESS(HAPI_GetAttributeIntData(
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

            // Ensure that the HAPI_Int64 we are given are of an expected size
            SYS_STATIC_ASSERT(sizeof(HAPI_Int64) == sizeof(int64));
            HAPI_Int64 *hData = reinterpret_cast<HAPI_Int64 *>(data->data());

            ENSURE_SUCCESS(HAPI_GetAttributeInt64Data(
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

            ENSURE_SUCCESS(HAPI_GetAttributeFloatData(
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

            ENSURE_SUCCESS(HAPI_GetAttributeFloat64Data(
                               &session, geo.nodeId, part.id, myName.c_str(),
                               &attribInfo, -1, data->data(), 0, count),
                           session);

            break;
        }

        case HAPI_STORAGETYPE_STRING:
        {
            HAPI_StringHandle *handles =
                new HAPI_StringHandle[count * tupleSize];

            ENSURE_SUCCESS(HAPI_GetAttributeStringData(
                               &session, geo.nodeId, part.id, myName.c_str(),
                               &attribInfo, handles, 0, count),
                           session);

            GT_DAIndexedString *data = new GT_DAIndexedString(count, tupleSize);

            myData.reset(data);

            for (exint i = 0; i < count; i++)
            {
                for (exint j = 0; j < tupleSize; j++)
                {
                    exint ind = (i * tupleSize) + j;
                    CHECK_RETURN(
                        GEOhapiExtractString(session, handles[ind], buf));

                    data->setString(i, j, buf.buffer());
                }
            }

            delete[] handles;
            break;
        }

        default:
            return false;
        }
    }

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

template <class DT>
static GT_DataArrayHandle
concatNumericArrays(UT_Array<GEO_HAPIAttributeHandle> &attribs)
{
    typedef GT_DANumeric<DT> DADataType;

    int tupleSize = attribs(0)->getTupleSize();
    exint sizeSum = 0;

    for (exint i = 0; i < attribs.entries(); i++)
    {
        sizeSum += attribs(i)->entries();
    }

    DADataType *concat = new DADataType(sizeSum, tupleSize);
    DT *concatData = concat->data();
    exint offset = 0;

    for (exint i = 0; i < attribs.entries(); i++)
    {
        GT_DataArrayHandle &temp = attribs(i)->myData;
        temp->fillArray(concatData + offset, 0, temp->entries(), tupleSize);
        offset += temp->entries() * tupleSize;
    }

    return concat;
}

static GT_DataArrayHandle
concatStringArrays(UT_Array<GEO_HAPIAttributeHandle> &attribs)
{

    int tupleSize = attribs(0)->getTupleSize();
    exint sizeSum = 0;

    for (exint i = 0; i < attribs.entries(); i++)
    {
        sizeSum += attribs(i)->entries();
    }

    GT_DAIndexedString *out = new GT_DAIndexedString(sizeSum, tupleSize);
    exint offset = 0;

    for (exint i = 0; i < attribs.entries(); i++)
    {
        GT_DataArrayHandle &temp = attribs(i)->myData;
	
	for (exint s = 0; s < temp->entries(); s++)
	{
	    for (int t = 0; t < tupleSize; t++)
	    {
		out->setString(s + offset, t, temp->getS(s, t));
	    }
	}

	offset += temp->entries();
    }

    return out;
}

static bool
checkCompatibility(UT_Array<GEO_HAPIAttributeHandle> &attribs)
{
    GEO_HAPIAttributeHandle &lhs = attribs(0);
    for (int i = 0; i < attribs.entries(); i++)
    {
        GEO_HAPIAttributeHandle &rhs = attribs(i);

        if (lhs->myName != rhs->myName || 
	    lhs->myDataType != rhs->myDataType ||
            lhs->myName != rhs->myName || 
	    lhs->myOwner != rhs->myOwner ||
            lhs->getTupleSize() != rhs->getTupleSize())
            return false;
    }
    return true;
}

GEO_HAPIAttribute *
GEO_HAPIAttribute::concatAttribs(UT_Array<GEO_HAPIAttributeHandle> &attribs)
{
    if (attribs.entries() == 0)
    {
        return nullptr;
    }
    else if (attribs.entries() == 1)
    {
        return new GEO_HAPIAttribute(*attribs(0));
    }

    UT_ASSERT(checkCompatibility(attribs));

    GT_DataArrayHandle outData;

    switch (attribs(0)->myDataType)
    {
    case HAPI_STORAGETYPE_FLOAT:
        outData = concatNumericArrays<float>(attribs);
        break;

    case HAPI_STORAGETYPE_FLOAT64:
        outData = concatNumericArrays<double>(attribs);
        break;

    case HAPI_STORAGETYPE_INT:
        outData = concatNumericArrays<int>(attribs);
        break;

    case HAPI_STORAGETYPE_INT64:
        outData = concatNumericArrays<int64>(attribs);
        break;

    case HAPI_STORAGETYPE_STRING:
        outData = concatStringArrays(attribs);

    default:
        UT_ASSERT(false && "Unexpected data type");
    }

    GEO_HAPIAttribute *out = new GEO_HAPIAttribute(*(attribs(0)));
    out->myData = outData;

    return out;
}
