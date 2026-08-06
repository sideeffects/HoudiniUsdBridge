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

#ifndef __HD_HdGpHapiUtils__
#define __HD_HdGpHapiUtils__

#include <HAPI/HAPI.h>
#include <UT/UT_Options.h>

#include <pxr/pxr.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/sceneIndex.h>

#include <string>
#include <utility>

#define HDGP_HAPI_CHECK(call, ...) \
    do { \
        HAPI_Result hapi_err = (call); \
        if (hapi_err != HAPI_RESULT_SUCCESS) { \
            TF_WARN(__VA_ARGS__); \
        } \
    } while(0)

#define HDGP_HAPI_CHECK_RETURN(call, retval, ...) \
    do { \
        HAPI_Result hapi_err = (call); \
        if (hapi_err != HAPI_RESULT_SUCCESS) { \
            TF_WARN(__VA_ARGS__); \
            return retval; \
        } \
    } while(0)

#define HDGP_HAPI_CHECK_RETURN_VOID(call, ...) \
    do { \
        HAPI_Result hapi_err = (call); \
        if (hapi_err != HAPI_RESULT_SUCCESS) { \
            TF_WARN(__VA_ARGS__); \
            return; \
        } \
    } while(0)

#define HDGP_HAPI_CHECK_RESULT(call_result, ...) \
    if (call_result != HAPI_RESULT_SUCCESS) { \
        TF_WARN(__VA_ARGS__); \
    }

PXR_NAMESPACE_OPEN_SCOPE

class HD_HdGpHapiSession
{
public:
    HD_HdGpHapiSession();
    ~HD_HdGpHapiSession();

    HD_HdGpHapiSession(const HD_HdGpHapiSession &) = delete;
    HD_HdGpHapiSession &operator=(const HD_HdGpHapiSession &) = delete;

    bool isValid() const;
    const HAPI_Session *session() const { return &mySession; }
    const HAPI_CookOptions &cookOptions() const { return myCookOpts; }
    HAPI_NodeId parentNode() const { return myParentNode; }

private:
    HAPI_Session mySession;
    HAPI_CookOptions myCookOpts;
    HAPI_NodeId myParentNode;
};

bool HD_HdGpHapiLoadAsset(
        const HAPI_Session *session,
        HAPI_NodeId parent_node,
        const std::string &graphpath,
        HAPI_NodeId &out_node);

bool HD_HdGpHapiSetParms(
        const HAPI_Session *session,
        HAPI_NodeId node_id,
        const UT_Options &parms);

bool HD_HdGpHapiSetMeshFromHydra(
        const HAPI_Session *session,
        HAPI_NodeId node_id,
        HAPI_PartId part_id,
        const HdSceneIndexPrim &prim,
        HAPI_PartInfo *out_part_info = nullptr);

bool HD_HdGpHapiSetPointsFromHydra(
        const HAPI_Session *session,
        HAPI_NodeId node_id,
        HAPI_PartId part_id,
        const HdSceneIndexPrim &prim,
        HAPI_PartInfo *out_part_info = nullptr);

bool HD_HdGpHapiSetCameraFromHydra(
        const HAPI_Session *session,
        HAPI_NodeId camera_node,
        const HdSceneIndexPrim &prim);

const char *HD_HdGpHapiAttribName(const TfToken &hydra_primvar_name);
TfToken HD_HdGpHydraPrimvarName(const char *hapi_attrib_name);

HAPI_AttributeTypeInfo HD_HdGpHapiAttribType(const TfToken &hydra_primvar_role);
TfToken HD_HdGpHydraPrimvarRole(HAPI_AttributeTypeInfo hapi_attrib_type);

HAPI_AttributeOwner HD_HdGpHapiAttribOwner(const TfToken &hydra_primvar_interpolation);
TfToken HD_HdGpHydraPrimvarInterpolation(
        HAPI_AttributeOwner hapi_attrib_owner,
        const TfToken &point_interpolation_type = HdPrimvarSchemaTokens->vertex);

VtValue HD_HdGpHapiNormalizeToArray(const VtValue &val);

bool HD_HdGpHapiSetPrimvar(
        const HAPI_Session *session,
        HAPI_NodeId node_id,
        HAPI_PartId part_id,
        const char *name,
        HdPrimvarSchema &pv,
        const HAPI_PartInfo &part_info);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
