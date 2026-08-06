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

#include "HD_HdGpHapiUtils.h"

#include <OP/OP_Node.h>
#include <UT/UT_Guid.h>
#include <UT/UT_String.h>
#include <UT/UT_StringHolder.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/layer.h>

#include <cstring>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

HD_HdGpHapiSession::HD_HdGpHapiSession()
    : myParentNode(-1)
{
    UT_Guid guid;
    UT_StringHolder guid_str;
    guid.getString(guid_str);

    HAPI_ThriftServerOptions server_opts = HAPI_ThriftServerOptions_Create();
    // Silence the HARS server's log sink so SOP cook errors from the HDA
    // don't get dumped to our stdout/stderr. 
    // The messages are still retrievable via HAPI_GetNodeCookResult.
    server_opts.verbosity = HAPI_STATUSVERBOSITY_NONE;
    HDGP_HAPI_CHECK_RETURN_VOID(
        HAPI_StartThriftNamedPipeServer(&server_opts, guid_str.c_str(),
            nullptr, nullptr),
        "Failed to start Thrift named pipe server");

    HAPI_SessionInfo session_info = HAPI_SessionInfo_Create();
    HDGP_HAPI_CHECK_RETURN_VOID(
        HAPI_CreateThriftNamedPipeSession(&mySession, guid_str.c_str(),
            &session_info),
        "Failed to create Thrift named pipe session");

    HDGP_HAPI_CHECK_RETURN_VOID(
        HAPI_Initialize(
            /*session=*/ &mySession,
            /*cook_options=*/ &myCookOpts,
            /*use_cooking_thread=*/ false,
            /*cooking_thread_stack_size=*/ -1,
            /*houdini_environment_files=*/ nullptr,
            /*otl_search_path=*/ nullptr,
            /*dso_search_path=*/ nullptr,
            /*image_dso_search_path=*/ nullptr,
            /*audio_dso_search_path=*/ nullptr),
        "Failed to initialize HAPI");

    UT_String label(guid_str);
    OP_Node::forceValidOpName(label);

    HAPI_NodeId new_node_id = -1;
    HAPI_Result result = HAPI_CreateNode(&mySession, -1, "Object/geo",
        label.c_str(), false, &new_node_id);
    HDGP_HAPI_CHECK_RESULT(result, "Failed to create geo node");

    if (result == HAPI_RESULT_SUCCESS)
        myParentNode = new_node_id;
}

HD_HdGpHapiSession::~HD_HdGpHapiSession()
{
    if (HAPI_IsSessionValid(&mySession) == HAPI_RESULT_SUCCESS &&
        HAPI_IsInitialized(&mySession) == HAPI_RESULT_SUCCESS)
    {
        HDGP_HAPI_CHECK(
            HAPI_Cleanup(&mySession), "Failed to cleanup session");
        HDGP_HAPI_CHECK(
            HAPI_CloseSession(&mySession), "Failed to close session");
    }
}

bool
HD_HdGpHapiSession::isValid() const
{
    return HAPI_IsSessionValid(&mySession) == HAPI_RESULT_SUCCESS
        && HAPI_IsInitialized(&mySession) == HAPI_RESULT_SUCCESS
        && myParentNode != -1;
}

const char *
HD_HdGpHapiAttribName(const TfToken &hydra_primvar_name)
{
    if (hydra_primvar_name == HdPrimvarsSchemaTokens->points)
        return HAPI_ATTRIB_POSITION;
    if (hydra_primvar_name == HdPrimvarsSchemaTokens->normals)
        return HAPI_ATTRIB_NORMAL;
    return hydra_primvar_name.GetText();
}

TfToken
HD_HdGpHydraPrimvarName(const char *hapi_attrib_name)
{
    if (!hapi_attrib_name)
        return TfToken();
    if (std::strcmp(hapi_attrib_name, HAPI_ATTRIB_POSITION) == 0)
        return HdPrimvarsSchemaTokens->points;
    if (std::strcmp(hapi_attrib_name, HAPI_ATTRIB_NORMAL) == 0)
        return HdPrimvarsSchemaTokens->normals;
    return TfToken(hapi_attrib_name);
}

HAPI_AttributeTypeInfo
HD_HdGpHapiAttribType(const TfToken &hydra_primvar_role)
{
    if (hydra_primvar_role == HdPrimvarRoleTokens->point)
        return HAPI_ATTRIBUTE_TYPE_POINT;
    if (hydra_primvar_role == HdPrimvarRoleTokens->normal)
        return HAPI_ATTRIBUTE_TYPE_NORMAL;
    if (hydra_primvar_role == HdPrimvarRoleTokens->vector)
        return HAPI_ATTRIBUTE_TYPE_VECTOR;
    if (hydra_primvar_role == HdPrimvarRoleTokens->color)
        return HAPI_ATTRIBUTE_TYPE_COLOR;
    if (hydra_primvar_role == HdPrimvarRoleTokens->textureCoordinate)
        return HAPI_ATTRIBUTE_TYPE_ST;
    return HAPI_ATTRIBUTE_TYPE_NONE;
}

TfToken
HD_HdGpHydraPrimvarRole(HAPI_AttributeTypeInfo hapi_attrib_type)
{
    switch (hapi_attrib_type)
    {
    case HAPI_ATTRIBUTE_TYPE_POINT:
        return HdPrimvarRoleTokens->point;
    case HAPI_ATTRIBUTE_TYPE_NORMAL:
        return HdPrimvarRoleTokens->normal;
    case HAPI_ATTRIBUTE_TYPE_VECTOR:
        return HdPrimvarRoleTokens->vector;
    case HAPI_ATTRIBUTE_TYPE_COLOR:
        return HdPrimvarRoleTokens->color;
    case HAPI_ATTRIBUTE_TYPE_ST:
        return HdPrimvarRoleTokens->textureCoordinate;
    default:
        return TfToken();
    }
}

HAPI_AttributeOwner
HD_HdGpHapiAttribOwner(const TfToken &hydra_primvar_interpolation)
{
    if (hydra_primvar_interpolation == HdPrimvarSchemaTokens->constant)
        return HAPI_ATTROWNER_DETAIL;
    if (hydra_primvar_interpolation == HdPrimvarSchemaTokens->uniform)
        return HAPI_ATTROWNER_PRIM;
    if (hydra_primvar_interpolation == HdPrimvarSchemaTokens->varying)
        return HAPI_ATTROWNER_POINT;
    if (hydra_primvar_interpolation == HdPrimvarSchemaTokens->vertex)
        return HAPI_ATTROWNER_POINT;
    if (hydra_primvar_interpolation == HdPrimvarSchemaTokens->faceVarying)
        return HAPI_ATTROWNER_VERTEX;
    if (hydra_primvar_interpolation == HdPrimvarSchemaTokens->instance)
        return HAPI_ATTROWNER_POINT;
    return HAPI_ATTROWNER_INVALID;
}

TfToken
HD_HdGpHydraPrimvarInterpolation(
        HAPI_AttributeOwner hapi_attrib_owner,
        const TfToken      &point_interpolation_type)
{
    switch (hapi_attrib_owner)
    {
        case HAPI_ATTROWNER_DETAIL:
            return HdPrimvarSchemaTokens->constant;
        case HAPI_ATTROWNER_PRIM:
            return HdPrimvarSchemaTokens->uniform;
        // HAPI_ATTROWNER_POINT is ambiguous when converting in this direction
        // (Hydra's `vertex`, `varying`, and `instance` all map to this in the
        // other direction), so the caller passes the desired token via
        // `point_interpolation_type`
        case HAPI_ATTROWNER_POINT:
            return point_interpolation_type;
        case HAPI_ATTROWNER_VERTEX:
            return HdPrimvarSchemaTokens->faceVarying;
        default:
            return TfToken();
    }
}

VtValue
HD_HdGpHapiNormalizeToArray(const VtValue &val)
{
    if (val.IsHolding<float>())
        return VtValue(VtFloatArray({val.UncheckedGet<float>()}));
    if (val.IsHolding<double>())
        return VtValue(VtDoubleArray({val.UncheckedGet<double>()}));
    if (val.IsHolding<int>())
        return VtValue(VtIntArray({val.UncheckedGet<int>()}));
    if (val.IsHolding<GfVec2f>())
        return VtValue(VtVec2fArray({val.UncheckedGet<GfVec2f>()}));
    if (val.IsHolding<GfVec3f>())
        return VtValue(VtVec3fArray({val.UncheckedGet<GfVec3f>()}));
    if (val.IsHolding<GfVec4f>())
        return VtValue(VtVec4fArray({val.UncheckedGet<GfVec4f>()}));
    if (val.IsHolding<GfVec2d>())
        return VtValue(VtVec2dArray({val.UncheckedGet<GfVec2d>()}));
    if (val.IsHolding<GfVec3d>())
        return VtValue(VtVec3dArray({val.UncheckedGet<GfVec3d>()}));
    if (val.IsHolding<GfVec4d>())
        return VtValue(VtVec4dArray({val.UncheckedGet<GfVec4d>()}));
    if (val.IsHolding<GfVec2i>())
        return VtValue(VtVec2iArray({val.UncheckedGet<GfVec2i>()}));
    if (val.IsHolding<GfVec3i>())
        return VtValue(VtVec3iArray({val.UncheckedGet<GfVec3i>()}));
    if (val.IsHolding<GfVec4i>())
        return VtValue(VtVec4iArray({val.UncheckedGet<GfVec4i>()}));
    if (val.IsHolding<std::string>())
        return VtValue(VtStringArray({val.UncheckedGet<std::string>()}));
    if (val.IsHolding<TfToken>())
        return VtValue(VtStringArray({val.UncheckedGet<TfToken>().GetString()}));
    return val;
}

bool
HD_HdGpHapiSetPrimvar(
        const HAPI_Session *session,
        HAPI_NodeId node_id,
        HAPI_PartId part_id,
        const char *name,
        HdPrimvarSchema &pv,
        const HAPI_PartInfo &part_info)
{
    // Hydra primvar names aren't constrained to the Houdini attribute
    // naming rules, so sanitise before handing the name to HAPI.  Keep
    // the original `name` for TF_WARN error reporting so the user sees
    // the primvar they actually authored.
    UT_String safe_name_str(name);
    safe_name_str.forceValidVariableName();
    const char *safe_name = safe_name_str.c_str();

    HdSampledDataSourceHandle val_ds = pv.GetPrimvarValue();
    if (!val_ds)
        return false;
    VtValue raw_val = val_ds->GetValue(0.0f);
    if (raw_val.IsEmpty())
        return false;
    VtValue val = HD_HdGpHapiNormalizeToArray(raw_val);

    HAPI_AttributeInfo attr_info = HAPI_AttributeInfo_Create();
    attr_info.exists = true;

    attr_info.owner = HAPI_ATTROWNER_DETAIL;
    attr_info.count = 1;
    if (HdTokenDataSourceHandle interp_ds = pv.GetInterpolation())
    {
        attr_info.owner = HD_HdGpHapiAttribOwner(interp_ds->GetTypedValue(0.0f));
        switch (attr_info.owner)
        {
            case HAPI_ATTROWNER_DETAIL:
                attr_info.count = 1;
                break;
            case HAPI_ATTROWNER_PRIM:
                attr_info.count = part_info.faceCount;
                break;
            case HAPI_ATTROWNER_POINT:
                attr_info.count = part_info.pointCount;
                break;
            case HAPI_ATTROWNER_VERTEX:
                attr_info.count = part_info.vertexCount;
                break;
            default:
                attr_info.count = 0;
        }
    }
    if (attr_info.owner == HAPI_ATTROWNER_INVALID)
        return false;

    attr_info.tupleSize = 1;
    if (HdIntDataSourceHandle es_ds = pv.GetElementSize())
    {
        attr_info.tupleSize = es_ds->GetTypedValue(0.0f);
    }

    attr_info.typeInfo = HD_HdGpHapiAttribType(pv.GetRole() ?
        pv.GetRole()->GetTypedValue(0.f) : HdPrimvarRoleTokens->none);

    if (val.IsHolding<VtFloatArray>())
    {
        const auto &arr = val.UncheckedGet<VtFloatArray>();
        attr_info.storage = HAPI_STORAGETYPE_FLOAT;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloatData(session, node_id,
                part_id, safe_name, &attr_info, arr.cdata(), 0, attr_info.count),
            false, "Failed to set float data for '%s'", name);
    }
    else if (val.IsHolding<VtVec2fArray>())
    {
        const auto &arr = val.UncheckedGet<VtVec2fArray>();
        attr_info.tupleSize *= 2;
        attr_info.storage = HAPI_STORAGETYPE_FLOAT;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloatData(session, node_id, part_id, safe_name,
                &attr_info, (const float *)arr.cdata(), 0, attr_info.count),
            false, "Failed to set vec2f data for '%s'", name);
    }
    else if (val.IsHolding<VtVec3fArray>())
    {
        const auto &arr = val.UncheckedGet<VtVec3fArray>();
        attr_info.tupleSize *= 3;
        attr_info.storage = HAPI_STORAGETYPE_FLOAT;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloatData(session, node_id, part_id, safe_name,
                &attr_info, (const float *)arr.cdata(), 0, attr_info.count),
            false, "Failed to set vec3f data for '%s'", name);
    }
    else if (val.IsHolding<VtVec4fArray>())
    {
        const auto &arr = val.UncheckedGet<VtVec4fArray>();
        attr_info.tupleSize *= 4;
        attr_info.storage = HAPI_STORAGETYPE_FLOAT;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloatData(session, node_id, part_id, safe_name,
                &attr_info, (const float *)arr.cdata(), 0, attr_info.count),
            false, "Failed to set vec4f data for '%s'", name);
    }
    else if (val.IsHolding<VtDoubleArray>())
    {
        const auto &arr = val.UncheckedGet<VtDoubleArray>();
        attr_info.storage = HAPI_STORAGETYPE_FLOAT64;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloat64Data(session, node_id, part_id, safe_name,
                &attr_info, arr.cdata(), 0, attr_info.count),
            false, "Failed to set double data for '%s'", name);
    }
    else if (val.IsHolding<VtVec2dArray>())
    {
        const auto &arr = val.UncheckedGet<VtVec2dArray>();
        attr_info.tupleSize *= 2;
        attr_info.storage = HAPI_STORAGETYPE_FLOAT64;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloat64Data(session, node_id, part_id, safe_name,
                &attr_info, (const double *)arr.cdata(), 0, attr_info.count),
            false, "Failed to set vec2d data for '%s'", name);
    }
    else if (val.IsHolding<VtVec3dArray>())
    {
        const auto &arr = val.UncheckedGet<VtVec3dArray>();
        attr_info.tupleSize *= 3;
        attr_info.storage = HAPI_STORAGETYPE_FLOAT64;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloat64Data(session, node_id, part_id, safe_name,
                &attr_info, (const double *)arr.cdata(), 0, attr_info.count),
            false, "Failed to set vec3d data for '%s'", name);
    }
    else if (val.IsHolding<VtVec4dArray>())
    {
        const auto &arr = val.UncheckedGet<VtVec4dArray>();
        attr_info.tupleSize *= 4;
        attr_info.storage = HAPI_STORAGETYPE_FLOAT64;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloat64Data(session, node_id, part_id, safe_name,
                &attr_info, (const double *)arr.cdata(), 0, attr_info.count),
            false, "Failed to set vec4d data for '%s'", name);
    }
    else if (val.IsHolding<VtIntArray>())
    {
        const auto &arr = val.UncheckedGet<VtIntArray>();
        attr_info.storage = HAPI_STORAGETYPE_INT;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeIntData(session, node_id, part_id, safe_name,
                &attr_info, arr.cdata(), 0, attr_info.count),
            false, "Failed to set int data for '%s'", name);
    }
    else if (val.IsHolding<VtVec2iArray>())
    {
        const auto &arr = val.UncheckedGet<VtVec2iArray>();
        attr_info.tupleSize *= 2;
        attr_info.storage = HAPI_STORAGETYPE_INT;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeIntData(session, node_id, part_id, safe_name,
                &attr_info, (const int *)arr.cdata(), 0, attr_info.count),
            false, "Failed to set vec2i data for '%s'", name);
    }
    else if (val.IsHolding<VtVec3iArray>())
    {
        const auto &arr = val.UncheckedGet<VtVec3iArray>();
        attr_info.tupleSize *= 3;
        attr_info.storage = HAPI_STORAGETYPE_INT;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeIntData(session, node_id, part_id, safe_name,
                &attr_info, (const int *)arr.cdata(), 0, attr_info.count),
            false, "Failed to set vec3i data for '%s'", name);
    }
    else if (val.IsHolding<VtVec4iArray>())
    {
        const auto &arr = val.UncheckedGet<VtVec4iArray>();
        attr_info.tupleSize *= 4;
        attr_info.storage = HAPI_STORAGETYPE_INT;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeIntData(session, node_id, part_id, safe_name,
                &attr_info, (const int *)arr.cdata(), 0, attr_info.count),
            false, "Failed to set vec4i data for '%s'", name);
    }
    else if (val.IsHolding<VtStringArray>())
    {
        const auto &arr = val.UncheckedGet<VtStringArray>();
        attr_info.storage = HAPI_STORAGETYPE_STRING;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, safe_name, &attr_info),
            false, "Failed to add attribute '%s'", name);
        std::vector<const char *> ptrs(arr.size());
        for (size_t i = 0; i < arr.size(); ++i)
            ptrs[i] = arr[i].c_str();
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeStringData(session, node_id, part_id, safe_name,
                &attr_info, ptrs.data(), 0, attr_info.count),
            false, "Failed to set string data for '%s'", name);
    }
    else
    {
        TF_WARN("Unsupported primvar type for '%s', skipping", name);
        return false;
    }
    return true;
}

bool
HD_HdGpHapiLoadAsset(
        const HAPI_Session *session,
        HAPI_NodeId parent_node,
        const std::string &graphpath,
        HAPI_NodeId &out_node)
{
    out_node = -1;

    // Parse "path/to/XXXXX:SDF_FORMAT_ARGS:opname=XXXXX"
    std::string file;
    SdfLayer::FileFormatArguments args;
    SdfLayer::SplitIdentifier(graphpath, &file, &args);

    auto iter = args.find("opname");
    if (iter == args.end())
    {
        TF_WARN("Missing 'opname' argument in '%s':"
                " expected path/to/XXXXX:SDF_FORMAT_ARGS:opname=XXXXX",
                graphpath.c_str());
        return false;
    }
    std::string opname = iter->second;

    // We'll first try to load the asset using the path as-is
    HAPI_AssetLibraryId hda_id;
    if (HAPI_LoadAssetLibraryFromFile(
        session, file.c_str(), false, &hda_id) != HAPI_RESULT_SUCCESS)
    {
        // That failed, so we'll try going through the asset resolver
        std::string layer_id = ArGetResolver().CreateIdentifier(file);
        ArResolvedPath resolved_path = ArGetResolver().Resolve(layer_id);
        if (resolved_path.IsEmpty() ||
            HAPI_LoadAssetLibraryFromFile(
                session, resolved_path.GetPathString().c_str(), false, &hda_id)
                != HAPI_RESULT_SUCCESS)
        {
            TF_WARN("Unable to find and load file '%s'", file.c_str());
            return false;
        }
    }

    if (HAPI_CreateNode(session, parent_node, opname.c_str(),
        nullptr, false, &out_node) != HAPI_RESULT_SUCCESS)
    {
        TF_WARN("Failed to create node of type '%s'", opname.c_str());
        out_node = -1;
        return false;
    }

    return true;
}

bool
HD_HdGpHapiSetParms(
        const HAPI_Session *session,
        HAPI_NodeId node_id,
        const UT_Options &parms)
{
    for (auto iter = parms.begin(), end = parms.end(); iter != end; ++iter)
    {
        auto entry = iter.entry();
        if (!entry)
            continue;

        const char *name = iter.name().c_str();
        switch (entry->getType())
        {
            case UT_OPTION_FPREAL:
            {
                float fval = static_cast<float>(entry->getOptionF());
                HAPI_SetParmFloatValue(session, node_id, name, 0, fval);
                break;
            }
            case UT_OPTION_INT:
            {
                int ival = static_cast<int>(entry->getOptionI());
                HAPI_SetParmIntValue(session, node_id, name, 0, ival);
                break;
            }
            case UT_OPTION_STRING:
            {
                HAPI_ParmId parm_id;
                if (HAPI_GetParmIdFromName(session, node_id,
                        name, &parm_id) == HAPI_RESULT_SUCCESS)
                {
                    const char *sval = entry->getOptionS().c_str();
                    HAPI_SetParmStringValue(session, node_id,
                        sval, parm_id, 0);
                }
                break;
            }
            default:
                TF_WARN("Unhandled parameter type %d for '%s'",
                    static_cast<int>(entry->getType()), name);
                break;
        }
    }
    return true;
}

bool
HD_HdGpHapiSetMeshFromHydra(
        const HAPI_Session *session,
        HAPI_NodeId node_id,
        HAPI_PartId part_id,
        const HdSceneIndexPrim &prim,
        HAPI_PartInfo *out_part_info)
{
    auto primvars = HdPrimvarsSchema::GetFromParent(prim.dataSource);
    auto points = primvars.GetPrimvar(HdPrimvarsSchemaTokens->points);
    HdSampledDataSourceHandle points_ds = points.GetPrimvarValue();
    VtValue points_vt = points_ds->GetValue(0.0f);
    VtVec3fArray points_data = points_vt.UncheckedGet<VtVec3fArray>();

    auto mesh = HdMeshSchema::GetFromParent(prim.dataSource);
    auto topology = HdMeshTopologySchema::GetFromParent(mesh.GetContainer());
    VtIntArray fvc_data = topology.GetFaceVertexCounts()->GetTypedValue(0.0f);
    VtIntArray fvi_data = topology.GetFaceVertexIndices()->GetTypedValue(0.0f);

    HAPI_PartInfo part_info = HAPI_PartInfo_Create();
    part_info.type = HAPI_PARTTYPE_MESH;
    part_info.faceCount = fvc_data.size();
    part_info.vertexCount = fvi_data.size();
    part_info.pointCount = points_data.size();

    HDGP_HAPI_CHECK_RETURN(
        HAPI_SetPartInfo(session, node_id, part_id, &part_info), false,
        "Failed to set part info");
    HDGP_HAPI_CHECK_RETURN(
        HAPI_SetFaceCounts(session, node_id, part_id,
            fvc_data.data(), 0, fvc_data.size()), false,
        "Failed to set face counts");
    HDGP_HAPI_CHECK_RETURN(
        HAPI_SetVertexList(session, node_id, part_id,
            fvi_data.data(), 0, fvi_data.size()), false,
        "Failed to set vertex list");

    TfTokenVector pv_names = primvars.GetPrimvarNames();
    for (const TfToken &pv_name : pv_names)
    {
        HdPrimvarSchema pv = primvars.GetPrimvar(pv_name);
        HD_HdGpHapiSetPrimvar(session, node_id, part_id,
            HD_HdGpHapiAttribName(pv_name), pv, part_info);
    }

    if (out_part_info)
        *out_part_info = part_info;
    return true;
}

bool
HD_HdGpHapiSetPointsFromHydra(
        const HAPI_Session *session,
        HAPI_NodeId node_id,
        HAPI_PartId part_id,
        const HdSceneIndexPrim &prim,
        HAPI_PartInfo *out_part_info)
{
    auto primvars = HdPrimvarsSchema::GetFromParent(prim.dataSource);

    auto translations_pv = primvars.GetPrimvar(
        HdInstancerTokens->instanceTranslations);
    HdSampledDataSourceHandle translations_ds = translations_pv.GetPrimvarValue();
    if (!translations_ds)
        return false;
    VtValue translations_vt = translations_ds->GetValue(0.0f);
    if (!translations_vt.IsHolding<VtVec3fArray>())
        return false;
    VtVec3fArray translations = translations_vt.UncheckedGet<VtVec3fArray>();
    if (translations.empty())
        return false;

    int point_count = static_cast<int>(translations.size());

    VtQuathArray rotations;
    if (auto pv = primvars.GetPrimvar(HdInstancerTokens->instanceRotations))
    {
        if (HdSampledDataSourceHandle ds = pv.GetPrimvarValue())
        {
            VtValue v = ds->GetValue(0.0f);
            if (v.IsHolding<VtQuathArray>())
                rotations = v.UncheckedGet<VtQuathArray>();
        }
    }

    VtVec3fArray scales;
    if (auto pv = primvars.GetPrimvar(HdInstancerTokens->instanceScales))
    {
        if (HdSampledDataSourceHandle ds = pv.GetPrimvarValue())
        {
            VtValue v = ds->GetValue(0.0f);
            if (v.IsHolding<VtVec3fArray>())
                scales = v.UncheckedGet<VtVec3fArray>();
        }
    }

    HAPI_PartInfo part_info = HAPI_PartInfo_Create();
    part_info.type = HAPI_PARTTYPE_MESH;
    part_info.faceCount = 0;
    part_info.vertexCount = 0;
    part_info.pointCount = point_count;

    HDGP_HAPI_CHECK_RETURN(
        HAPI_SetPartInfo(session, node_id, part_id, &part_info), false,
        "Failed to set part info");

    {
        HAPI_AttributeInfo info = HAPI_AttributeInfo_Create();
        info.exists = true;
        info.owner = HAPI_ATTROWNER_POINT;
        info.count = point_count;
        info.tupleSize = 3;
        info.storage = HAPI_STORAGETYPE_FLOAT;
        info.typeInfo = HAPI_ATTRIBUTE_TYPE_POINT;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id,
                HAPI_ATTRIB_POSITION, &info),
            false, "Failed to add P attribute");
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloatData(session, node_id, part_id,
                HAPI_ATTRIB_POSITION, &info,
                reinterpret_cast<const float*>(translations.cdata()),
                0, point_count),
            false, "Failed to set P data");
    }

    if (!scales.empty())
    {
        HAPI_AttributeInfo info = HAPI_AttributeInfo_Create();
        info.exists = true;
        info.owner = HAPI_ATTROWNER_POINT;
        info.count = point_count;
        info.tupleSize = 1;
        info.storage = HAPI_STORAGETYPE_FLOAT;
        info.typeInfo = HAPI_ATTRIBUTE_TYPE_NONE;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, "pscale", &info),
            false, "Failed to add pscale attribute");
        std::vector<float> pscale_data(point_count, 1.0f);
        size_t n = std::min(static_cast<size_t>(point_count), scales.size());
        for (size_t i = 0; i < n; ++i)
            pscale_data[i] = scales[i][0];
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloatData(session, node_id, part_id, "pscale",
                &info, pscale_data.data(), 0, point_count),
            false, "Failed to set pscale data");
    }

    if (!rotations.empty())
    {
        HAPI_AttributeInfo info = HAPI_AttributeInfo_Create();
        info.exists = true;
        info.owner = HAPI_ATTROWNER_POINT;
        info.count = point_count;
        info.tupleSize = 4;
        info.storage = HAPI_STORAGETYPE_FLOAT;
        info.typeInfo = HAPI_ATTRIBUTE_TYPE_QUATERNION;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_AddAttribute(session, node_id, part_id, "orient", &info),
            false, "Failed to add orient attribute");
        // Houdini's `orient` attribute packs as (i, j, k, w).
        // GfQuath: GetReal() = w, GetImaginary() = (i, j, k).
        std::vector<float> orient_data(point_count * 4);
        size_t n = std::min(static_cast<size_t>(point_count), rotations.size());
        for (size_t i = 0; i < n; ++i)
        {
            const GfQuath &q = rotations[i];
            const auto &imag = q.GetImaginary();
            orient_data[i * 4 + 0] = imag[0];
            orient_data[i * 4 + 1] = imag[1];
            orient_data[i * 4 + 2] = imag[2];
            orient_data[i * 4 + 3] = q.GetReal();
        }
        for (size_t i = n; i < static_cast<size_t>(point_count); ++i)
        {
            orient_data[i * 4 + 0] = 0.f;
            orient_data[i * 4 + 1] = 0.f;
            orient_data[i * 4 + 2] = 0.f;
            orient_data[i * 4 + 3] = 1.f;
        }
        HDGP_HAPI_CHECK_RETURN(
            HAPI_SetAttributeFloatData(session, node_id, part_id, "orient",
                &info, orient_data.data(), 0, point_count),
            false, "Failed to set orient data");
    }

    // Pass through any remaining primvars unchanged, skipping the three
    // already handled above (so we don't double-translate them).
    for (const TfToken &pv_name : primvars.GetPrimvarNames())
    {
        if (pv_name == HdInstancerTokens->instanceTranslations ||
            pv_name == HdInstancerTokens->instanceRotations ||
            pv_name == HdInstancerTokens->instanceScales)
            continue;
        HdPrimvarSchema pv = primvars.GetPrimvar(pv_name);
        HD_HdGpHapiSetPrimvar(session, node_id, part_id,
            HD_HdGpHapiAttribName(pv_name), pv, part_info);
    }

    if (out_part_info)
        *out_part_info = part_info;
    return true;
}

bool
HD_HdGpHapiSetCameraFromHydra(
        const HAPI_Session *session,
        HAPI_NodeId camera_node,
        const HdSceneIndexPrim &prim)
{
    auto cam_schema = HdCameraSchema::GetFromParent(prim.dataSource);
    if (!cam_schema.IsDefined())
    {
        TF_WARN("Unable to get camera data from Hydra prim");
        return false;
    }

    HAPI_CameraInfo camera = HAPI_CameraInfo_Create();

    // Ideally these would be scaled to mm-scale, but I don't think we have
    // access to metrics about scene units to use here.
    // NOTE: These are no longer in 1/10 scene units (as they were in USD),
    //       but have already been converted to actual scene units by Hydra.
    camera.focal = cam_schema.GetFocalLength()->GetTypedValue(0.f);
    camera.aperture = cam_schema.GetHorizontalAperture()->GetTypedValue(0.f);

    camera.focusDistance = cam_schema.GetFocusDistance()->GetTypedValue(0.f);
    camera.fStop = cam_schema.GetFStop()->GetTypedValue(0.f);

    auto clip = cam_schema.GetClippingRange()->GetTypedValue(0.f);
    camera.clipNear = clip[0];
    camera.clipFar = clip[1];

    camera.shutterOpen = cam_schema.GetShutterOpen()->GetTypedValue(0.f);
    camera.shutterClose = cam_schema.GetShutterClose()->GetTypedValue(0.f);

    auto projection = cam_schema.GetProjection()->GetTypedValue(0.f);
    if (projection == HdCameraSchemaTokens->perspective)
        camera.projection = HAPI_CAMERAPROJECTIONTYPE_PERSPECTIVE;
    else if (projection == HdCameraSchemaTokens->orthographic)
        camera.projection = HAPI_CAMERAPROJECTIONTYPE_ORTHO;
    else
    {
        TF_WARN("Unsupported projection type (%s)", projection.GetText());
        camera.projection = HAPI_CAMERAPROJECTIONTYPE_INVALID;
    }

    // TODO: Search through the renderSettings to fetch the resolution.
    //       For now, we'll emulate the shape by using a 100x100 image
    //       with a pixelAspect matching the aperture aspect.
    camera.resX = 100;
    camera.resY = 100;
    float vaperture = cam_schema.GetVerticalAperture()->GetTypedValue(0.f);
    camera.pixelAspect = camera.aperture / vaperture;

    // TODO: Do we have the data we need to set these?
    /*
    camera.cropX[0] = ...; camera.cropX[1] = ...;
    camera.cropY[0] = ...; camera.cropY[1] = ...;
    camera.winX[0] = ...; camera.winX[1] = ...;
    camera.winY[0] = ...; camera.winY[1] = ...;
    camera.orthoZoom = ...;
    camera.guideScale = ...;
    camera.imagingDistance = ...;
    */

    HDGP_HAPI_CHECK_RETURN(
        HAPI_SetInputCameraInfo(session, camera_node, &camera),
        false, "Failed to set camera info");

    HAPI_Transform xform = HAPI_Transform_Create();
    if (auto xform_schema = HdXformSchema::GetFromParent(prim.dataSource))
    {
        auto mtx = xform_schema.GetMatrix()->GetTypedValue(0.f);
        mtx = mtx.RemoveScaleShear();
        auto translation = mtx.ExtractTranslation();
        xform.position[0] = translation[0];
        xform.position[1] = translation[1];
        xform.position[2] = translation[2];
        auto quat = mtx.ExtractRotationQuat();
        xform.rotationQuaternion[0] = quat.GetImaginary()[0];
        xform.rotationQuaternion[1] = quat.GetImaginary()[1];
        xform.rotationQuaternion[2] = quat.GetImaginary()[2];
        xform.rotationQuaternion[3] = quat.GetReal();
    }

    HDGP_HAPI_CHECK_RETURN(
        HAPI_SetInputCameraTransform(session, camera_node,
            HAPI_RSTORDER_DEFAULT, HAPI_XYZORDER_DEFAULT, &xform),
        false, "Failed to set camera xform");

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
