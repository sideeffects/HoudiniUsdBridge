/*
 * Copyright 2026 Side Effects Software Inc.
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

#include "HD_HdGpHapiPrimvarsDataSource.h"
#include "HD_HdGpHapiUtils.h"

#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{

// Resolve a HAPI string handle to std::string. Caller must hold the
// HAPI mutex.
std::string
_ResolveHapiString(const HAPI_Session *session, HAPI_StringHandle handle)
{
    int buf_length = 0;
    if (HAPI_GetStringBufLength(session, handle, &buf_length)
            != HAPI_RESULT_SUCCESS || buf_length <= 0)
        return std::string();
    std::vector<char> buf(buf_length);
    if (HAPI_GetString(session, handle, buf.data(), buf_length)
            != HAPI_RESULT_SUCCESS)
        return std::string();
    return std::string(buf.data());
}

// Lazy HdSampledDataSource that reads one HAPI attribute on demand.
class _PrimvarValueDataSource : public HdSampledDataSource
{
public:
    HD_DECLARE_DATASOURCE(_PrimvarValueDataSource);

    VtValue GetValue(Time /* shutterOffset */) override
    {
        std::lock_guard<std::mutex> lock(*myMutex);

        // attr_info passed in by Get(name) on the parent already
        // resolved storage / tupleSize / count / owner. We re-pass it
        // to the data getters as HAPI requires.
        HAPI_AttributeInfo info = myAttrInfo;
        const int count = info.count;
        const int tuple = info.tupleSize;
        if (count <= 0 || tuple <= 0)
            return VtValue();

        switch (info.storage)
        {
        case HAPI_STORAGETYPE_FLOAT:
        {
            if (tuple == 1)
            {
                VtFloatArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeFloatData(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        arr.data(), 0, count),
                    VtValue(),
                    "Failed to read float attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            else if (tuple == 2)
            {
                VtVec2fArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeFloatData(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        reinterpret_cast<float *>(arr.data()), 0, count),
                    VtValue(),
                    "Failed to read vec2f attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            else if (tuple == 3)
            {
                VtVec3fArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeFloatData(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        reinterpret_cast<float *>(arr.data()), 0, count),
                    VtValue(),
                    "Failed to read vec3f attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            else if (tuple == 4)
            {
                VtVec4fArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeFloatData(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        reinterpret_cast<float *>(arr.data()), 0, count),
                    VtValue(),
                    "Failed to read vec4f attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            // Tuple > 4 or other: fall through as flat float array.
            VtFloatArray arr(count * tuple);
            HDGP_HAPI_CHECK_RETURN(
                HAPI_GetAttributeFloatData(mySession, myNodeId,
                    myPartId, myAttrName.c_str(), &info, -1,
                    arr.data(), 0, count),
                VtValue(),
                "Failed to read float attribute '%s' (tuple=%d)",
                myAttrName.c_str(), tuple);
            return VtValue(arr);
        }
        case HAPI_STORAGETYPE_FLOAT64:
        {
            if (tuple == 1)
            {
                VtDoubleArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeFloat64Data(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        arr.data(), 0, count),
                    VtValue(),
                    "Failed to read float64 attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            else if (tuple == 2)
            {
                VtVec2dArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeFloat64Data(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        reinterpret_cast<double *>(arr.data()), 0, count),
                    VtValue(),
                    "Failed to read vec2d attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            else if (tuple == 3)
            {
                VtVec3dArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeFloat64Data(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        reinterpret_cast<double *>(arr.data()), 0, count),
                    VtValue(),
                    "Failed to read vec3d attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            else if (tuple == 4)
            {
                VtVec4dArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeFloat64Data(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        reinterpret_cast<double *>(arr.data()), 0, count),
                    VtValue(),
                    "Failed to read vec4d attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            // Tuple > 4 or other: fall through as flat double array.
            VtDoubleArray arr(count * tuple);
            HDGP_HAPI_CHECK_RETURN(
                HAPI_GetAttributeFloat64Data(mySession, myNodeId,
                    myPartId, myAttrName.c_str(), &info, -1,
                    arr.data(), 0, count),
                VtValue(),
                "Failed to read float64 attribute '%s' (tuple=%d)",
                myAttrName.c_str(), tuple);
            return VtValue(arr);
        }
        case HAPI_STORAGETYPE_INT:
        {
            if (tuple == 1)
            {
                VtIntArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeIntData(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        arr.data(), 0, count),
                    VtValue(),
                    "Failed to read int attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            else if (tuple == 2)
            {
                VtVec2iArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeIntData(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        reinterpret_cast<int *>(arr.data()), 0, count),
                    VtValue(),
                    "Failed to read vec2i attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            else if (tuple == 3)
            {
                VtVec3iArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeIntData(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        reinterpret_cast<int *>(arr.data()), 0, count),
                    VtValue(),
                    "Failed to read vec3i attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            else if (tuple == 4)
            {
                VtVec4iArray arr(count);
                HDGP_HAPI_CHECK_RETURN(
                    HAPI_GetAttributeIntData(mySession, myNodeId,
                        myPartId, myAttrName.c_str(), &info, -1,
                        reinterpret_cast<int *>(arr.data()), 0, count),
                    VtValue(),
                    "Failed to read vec4i attribute '%s'",
                    myAttrName.c_str());
                return VtValue(arr);
            }
            // Tuple > 4 or other: fall through as flat int array.
            VtIntArray arr(count * tuple);
            HDGP_HAPI_CHECK_RETURN(
                HAPI_GetAttributeIntData(mySession, myNodeId,
                    myPartId, myAttrName.c_str(), &info, -1,
                    arr.data(), 0, count),
                VtValue(),
                "Failed to read int attribute '%s' (tuple=%d)",
                myAttrName.c_str(), tuple);
            return VtValue(arr);
        }
        case HAPI_STORAGETYPE_STRING:
        {
            if (tuple != 1)
                break;
            std::vector<HAPI_StringHandle> handles(count);
            HDGP_HAPI_CHECK_RETURN(
                HAPI_GetAttributeStringData(mySession, myNodeId,
                    myPartId, myAttrName.c_str(), &info,
                    handles.data(), 0, count),
                VtValue(),
                "Failed to read string attribute '%s'",
                myAttrName.c_str());
            VtStringArray arr(count);
            for (int i = 0; i < count; ++i)
                arr[i] = _ResolveHapiString(mySession, handles[i]);
            return VtValue(arr);
        }
        default:
            break;
        }

        TF_WARN("Unsupported HAPI storage type %d (tuple=%d) for "
                "attribute '%s'", int(info.storage), tuple,
                myAttrName.c_str());
        return VtValue();
    }

    bool GetContributingSampleTimesForInterval(
            Time /* startTime */, Time /* endTime */,
            std::vector<Time> * /* outSampleTimes */) override
    {
        return false;
    }

private:
    _PrimvarValueDataSource(
            const HAPI_Session *session,
            HAPI_NodeId         node_id,
            HAPI_PartId         part_id,
            std::string         attr_name,
            HAPI_AttributeInfo  attr_info,
            std::shared_ptr<std::mutex> mutex)
        : mySession(session)
        , myNodeId(node_id)
        , myPartId(part_id)
        , myAttrName(std::move(attr_name))
        , myAttrInfo(attr_info)
        , myMutex(std::move(mutex))
    {
    }

    const HAPI_Session         *mySession;
    HAPI_NodeId                 myNodeId;
    HAPI_PartId                 myPartId;
    std::string                 myAttrName;
    HAPI_AttributeInfo          myAttrInfo;
    std::shared_ptr<std::mutex> myMutex;
};

} // namespace

///////////////////////////////////////////////////////////////////////////////
// HD_HdGpHapiPrimvarsDataSource

HD_HdGpHapiPrimvarsDataSource::HD_HdGpHapiPrimvarsDataSource(
        const HAPI_Session *session,
        HAPI_NodeId         node_id,
        HAPI_PartId         part_id,
        const TfToken      &point_interpolation_type,
        const TfTokenSet   &excluded_names)
    : mySession(session)
    , myNodeId(node_id)
    , myPartId(part_id)
    , myPointInterpolationType(point_interpolation_type)
    , myExcludedNames(excluded_names)
    , myPartInfo(HAPI_PartInfo_Create())
    , myPartInfoValid(false)
    , myMutex(std::make_shared<std::mutex>())
    , myNamesCached(false)
{
    if (HAPI_GetPartInfo(mySession, myNodeId, myPartId, &myPartInfo)
            == HAPI_RESULT_SUCCESS)
    {
        myPartInfoValid = true;
    }
}

bool
HD_HdGpHapiPrimvarsDataSource::isExcluded(const TfToken &name) const
{
    // Names starting with "__" are reserved-internal by convention.
    // NOTE: We don't need to check string length before reading
    //       (we only check the second char if the first char is '_')
    if (const char *text = name.GetText();
        text && text[0] == '_' && text[1] == '_')
        return true;
    return myExcludedNames.find(name) != myExcludedNames.end();
}

TfTokenVector
HD_HdGpHapiPrimvarsDataSource::GetNames()
{
    std::lock_guard<std::mutex> lock(*myMutex);
    if (myNamesCached)
        return myNames;
    myNamesCached = true;

    if (!myPartInfoValid)
        return myNames;

    static constexpr HAPI_AttributeOwner theOwners[] = {
        HAPI_ATTROWNER_DETAIL,
        HAPI_ATTROWNER_PRIM,
        HAPI_ATTROWNER_POINT,
        HAPI_ATTROWNER_VERTEX,
    };
    for (HAPI_AttributeOwner owner : theOwners)
    {
        const int count = myPartInfo.attributeCounts[owner];
        if (count <= 0)
            continue;
        std::vector<HAPI_StringHandle> handles(count);
        if (HAPI_GetAttributeNames(mySession, myNodeId, myPartId, owner,
                handles.data(), count) != HAPI_RESULT_SUCCESS)
        {
            TF_WARN("HAPI_GetAttributeNames failed for owner %d", int(owner));
            continue;
        }
        for (int i = 0; i < count; ++i)
        {
            std::string name = _ResolveHapiString(mySession, handles[i]);
            if (name.empty())
                continue;
            TfToken primvar_name = HD_HdGpHydraPrimvarName(name.c_str());
            if (isExcluded(primvar_name))
                continue;
            myNames.push_back(primvar_name);
        }
    }
    return myNames;
}

HdDataSourceBaseHandle
HD_HdGpHapiPrimvarsDataSource::Get(const TfToken &name)
{
    if (isExcluded(name))
        return nullptr;

    if (!myPartInfoValid)
        return nullptr;

    // Hydra name -> HAPI attribute name.
    const char *attr_name = HD_HdGpHapiAttribName(name);

    // Probe all owners for an existing attribute under attr_name.
    HAPI_AttributeInfo attr_info = HAPI_AttributeInfo_Create();
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(*myMutex);
        static constexpr HAPI_AttributeOwner theOwners[] = {
            HAPI_ATTROWNER_DETAIL,
            HAPI_ATTROWNER_PRIM,
            HAPI_ATTROWNER_POINT,
            HAPI_ATTROWNER_VERTEX,
        };
        for (HAPI_AttributeOwner owner : theOwners)
        {
            if (HAPI_GetAttributeInfo(mySession, myNodeId, myPartId,
                    attr_name, owner, &attr_info) == HAPI_RESULT_SUCCESS
                && attr_info.exists)
            {
                found = true;
                break;
            }
        }
    }
    if (!found)
        return nullptr;

    TfToken interp = HD_HdGpHydraPrimvarInterpolation(
            attr_info.owner, myPointInterpolationType);
    TfToken role = HD_HdGpHydraPrimvarRole(attr_info.typeInfo);

    HdSampledDataSourceHandle value_ds = _PrimvarValueDataSource::New(
            mySession, myNodeId, myPartId, std::string(attr_name),
            attr_info, myMutex);

    HdTokenDataSourceHandle interp_ds = !interp.IsEmpty()
            ? HdRetainedTypedSampledDataSource<TfToken>::New(interp)
            : HdTokenDataSourceHandle();
    HdTokenDataSourceHandle role_ds = !role.IsEmpty()
            ? HdRetainedTypedSampledDataSource<TfToken>::New(role)
            : HdTokenDataSourceHandle();

    return HdPrimvarSchema::Builder()
            .SetPrimvarValue(value_ds)
            .SetInterpolation(interp_ds)
            .SetRole(role_ds)
            .Build();
}

PXR_NAMESPACE_CLOSE_SCOPE
