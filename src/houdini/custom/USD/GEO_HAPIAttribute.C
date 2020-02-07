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
#include <GT/GT_DANumeric.h>
#include <UT/UT_UniquePtr.h>

GEO_HAPIAttribute::GEO_HAPIAttribute()
    : myOwner(HAPI_ATTROWNER_INVALID),
      myCount(0),
      myTupleSize(0),
      myDataType(HAPI_STORAGETYPE_INVALID),
      myData()
{
}

GEO_HAPIAttribute::GEO_HAPIAttribute(UT_StringRef name,
                                     HAPI_AttributeOwner owner,
                                     int count,
                                     int tupleSize,
                                     HAPI_StorageType dataType,
                                     GT_DataArray *data)
    : myName(name),
      myOwner(owner),
      myCount(count),
      myTupleSize(tupleSize),
      myDataType(dataType),
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
        CLEANUP(session);
        return false;
    }
    // Save relavent information
    myName = attribName;
    myOwner = owner;
    myCount = attribInfo.count;
    myTupleSize = attribInfo.tupleSize;
    myDataType = attribInfo.storage;
    myTypeInfo = attribInfo.typeInfo;

    if (myCount > 0)
    {
        // Put the attribute data into myData
        switch (myDataType)
        {
        case HAPI_STORAGETYPE_INT:
        {
            GT_DANumeric<int> *data = new GT_DANumeric<int>(
                myCount, myTupleSize);
            myData.reset(data);

            ENSURE_SUCCESS(HAPI_GetAttributeIntData(
                               &session, geo.nodeId, part.id, myName.c_str(),
                               &attribInfo, -1, data->data(), 0, myCount),
                           session);

            break;
        }

        case HAPI_STORAGETYPE_INT64:
        {
            GT_DANumeric<int64> *data = new GT_DANumeric<int64>(
                myCount, myTupleSize);
            myData.reset(data);

	    // Ensure that the HAPI_Int64 we are given are of an expected size
            SYS_STATIC_ASSERT(sizeof(HAPI_Int64) == sizeof(int64));
            HAPI_Int64 *hData = reinterpret_cast<HAPI_Int64*>(data->data());

            ENSURE_SUCCESS(
                HAPI_GetAttributeInt64Data(&session, geo.nodeId, part.id,
                                            myName.c_str(), &attribInfo, -1,
                                            hData, 0, myCount),
					    session);

            break;
        }

        case HAPI_STORAGETYPE_FLOAT:
        {
            GT_DANumeric<float> *data = new GT_DANumeric<float>(
                myCount, myTupleSize);
            myData.reset(data);

            ENSURE_SUCCESS(HAPI_GetAttributeFloatData(
                               &session, geo.nodeId, part.id, myName.c_str(),
                               &attribInfo, -1, data->data(), 0, myCount),
                           session);

            break;
        }

        case HAPI_STORAGETYPE_FLOAT64:
        {
            GT_DANumeric<double> *data = new GT_DANumeric<double>(
                myCount, myTupleSize);
            myData.reset(data);

            ENSURE_SUCCESS(HAPI_GetAttributeFloat64Data(
                               &session, geo.nodeId, part.id, myName.c_str(),
                               &attribInfo, -1, data->data(), 0, myCount),
                           session);

            break;
        }

        case HAPI_STORAGETYPE_STRING:
        {
            HAPI_StringHandle *handles =
                new HAPI_StringHandle[myCount * myTupleSize];

            ENSURE_SUCCESS(HAPI_GetAttributeStringData(
                               &session, geo.nodeId, part.id, myName.c_str(),
                               &attribInfo, handles, 0, myCount),
                           session);

            GT_DAIndexedString *data = new GT_DAIndexedString(
                myCount, myTupleSize);

            myData.reset(data);

            for (exint i = 0; i < myCount; i++)
            {
                for (exint j = 0; j < myTupleSize; j++)
                {
                    exint ind = (i * myTupleSize) + j;
                    CHECK_RETURN(
                        GEOhapiExtractString(session, handles[ind], buf));

                    data->setString(i, j, buf.buffer());
                }
            }

            delete[] handles;
            break;
        }

        default:
            CLEANUP(session);
            return false;
        }
    }

    return true;
}

void
GEO_HAPIAttribute::convertTupleSize(int newSize)
{
    if (newSize == myTupleSize || !myData.get())
    {
        return;
    }

    switch (myDataType)
    {
    case HAPI_STORAGETYPE_FLOAT:
        updateTupleData<float>(newSize);
        break;

    case HAPI_STORAGETYPE_FLOAT64:
        updateTupleData<double>(newSize);
        break;

    case HAPI_STORAGETYPE_INT:
        updateTupleData<int>(newSize);
        break;

    case HAPI_STORAGETYPE_INT64:
        updateTupleData<int64>(newSize);
        break;

    default:
        // Does not resize string sets
        return;
    }
}

template <class DT>
void
GEO_HAPIAttribute::updateTupleData(int newSize)
{
    typedef GT_DANumeric<DT> DADataType;
    DADataType *oldDataContainer = UTverify_cast<DADataType *>(myData.get());
    DADataType *newDataContainer = new DADataType(myCount, newSize);

    DT *newData = newDataContainer->data();
    DT *oldData = oldDataContainer->data();
    int smallerSize;

    if (newSize > myTupleSize)
    {
        // stuff in 0s
        memset(newData, 0, newSize * myCount * sizeof(DT));
        smallerSize = myTupleSize;
    }
    else
    {
        smallerSize = newSize;
    }

    for (int i = 0; i < myCount; i++)
    {
        // Same as commented loop code below
        memcpy(newData + (i * newSize), oldData + (i * myTupleSize),
               smallerSize * sizeof(DT));

        /*
        for (int j = 0; j < smallerSize; j++)
        {
            newData[i*newSize + j] = oldData[i*myTupleSize + j];
        }
        */
    }

    myTupleSize = newSize;
    myData.reset(newDataContainer);
}
