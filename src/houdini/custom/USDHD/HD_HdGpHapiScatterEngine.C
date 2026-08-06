/*
 * Copyright 2025 Side Effects Software Inc.
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

#include "HD_HdGpHapiScatterEngine.h"
#include "HD_HdGpHapiPrimvarsDataSource.h"

#include <OP/OP_Node.h>
#include <UT/UT_TmpDir.h>
#include <tools/henv.h>

#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/quath.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (orient)
    (pscale)
    (protoindex)
);

bool
HD_HdGpHapiScatterEngine::loadGraphImpl(const std::string &graphpath)
{
    if (!mySession.isValid())
    {
        TF_WARN("HAPI session is not properly initialized, "
                "unable to load the graph");
        return false;
    }

    if (myScatterNode != -1)
    {
        HDGP_HAPI_CHECK(HAPI_DeleteNode(mySession.session(), myScatterNode),
            "Failed to delete previous scatter node");
        myScatterNode = -1;
    }

    if (!HD_HdGpHapiLoadAsset(mySession.session(), mySession.parentNode(),
            graphpath, myScatterNode))
        return false;

    return rewireGraph();
}

bool
HD_HdGpHapiScatterEngine::setParmsImpl(const UT_Options &parms)
{
    if (!mySession.isValid() || myScatterNode == -1)
    {
        TF_WARN("HAPI and/or the scatter node are not properly initialized, "
                "unable to set the graph parameters");
        return false;
    }

    return HD_HdGpHapiSetParms(mySession.session(), myScatterNode, parms);
}

bool
HD_HdGpHapiScatterEngine::setScatterSurfaceImpl(
        const std::string &id,
        const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms)
{
    return setMeshNodes(myScatterSurfaceNodes, id, prim, xforms);
}

bool
HD_HdGpHapiScatterEngine::setMaskSurfaceImpl(
        const std::string &id,
        const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms)
{
    return setMeshNodes(myMaskSurfaceNodes, id, prim, xforms);
}

bool
HD_HdGpHapiScatterEngine::setMaskPointsImpl(
        const std::string &id,
        const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms)
{
    return setPointsNodes(myMaskPointsNodes, id, prim, xforms);
}

bool
HD_HdGpHapiScatterEngine::setMeshNodes(
        UT_ArrayStringMap<UT_Array<HAPI_NodeId>> &target_nodes,
        const std::string &id,
        const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms)
{
    if (!mySession.isValid())
    {
        TF_WARN("HAPI session is not properly initialized, "
                "unable to create/modify input node");
        return false;
    }

    size_t instance_count = xforms.size();

    // Being passed an empty id and prim is not an error, but a valid "reset"
    // signal, so we'll just return (after having destroyed any previous meshes).
    if (id.empty() && !prim)
    {
        for (auto &&pair : target_nodes)
            for (auto node_id : pair.second)
                HDGP_HAPI_CHECK(
                    HAPI_DeleteNode(mySession.session(), node_id),
                    "Unable to delete previous mesh for %s", pair.first.c_str());
        target_nodes.clear();
        return true;
    }

    if (auto iter = target_nodes.find(id); iter != target_nodes.end())
    {
        for (auto node_id : iter->second)
            HDGP_HAPI_CHECK(
                HAPI_DeleteNode(mySession.session(), node_id),
                "Unable to delete previous mesh for %s", id.c_str());
        target_nodes.erase(iter);
    }

    // If we have an empty prim, we're done
    if (!prim)
        return true;

    UT_String label(id);
    OP_Node::forceValidOpName(label);
    UT_Array<HAPI_NodeId> &mesh_nodes = target_nodes[id];
    mesh_nodes.setCapacity(instance_count);
    for (size_t i = 0; i < instance_count; ++i)
    {
        HAPI_NodeId mesh_node;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_CreateInputNode(mySession.session(), mySession.parentNode(),
                &mesh_node, label.c_str()),
            false, "Failed to create HAPI input node");
        mesh_nodes.emplace_back(mesh_node);
    }

    GfMatrix4d local_mtx(1.0);
    if (auto xform = HdXformSchema::GetFromParent(prim.dataSource);
        xform.IsDefined())
        local_mtx = xform.GetMatrix()->GetTypedValue(0.0f);

    const HAPI_PartId part_id = 0;
    const char *hydraxform_name = "__hydraxform";
    for (size_t i = 0; i < instance_count; ++i)
    {
        HAPI_NodeId mesh_node = mesh_nodes[i];

        HAPI_PartInfo part_info;
        if (!HD_HdGpHapiSetMeshFromHydra(mySession.session(), mesh_node,
                part_id, prim, &part_info))
            return false;

        HAPI_AttributeInfo mtx_attr_info = HAPI_AttributeInfo_Create();
        mtx_attr_info.exists = true;
        mtx_attr_info.owner = HAPI_ATTROWNER_POINT;
        mtx_attr_info.count = part_info.pointCount;
        mtx_attr_info.tupleSize = 16;
        mtx_attr_info.storage = HAPI_STORAGETYPE_FLOAT;
        mtx_attr_info.typeInfo = HAPI_ATTRIBUTE_TYPE_MATRIX;
        HDGP_HAPI_CHECK(
            HAPI_AddAttribute(mySession.session(), mesh_node,
                part_id, hydraxform_name, &mtx_attr_info),
            "Failed to add attribute '%s'", hydraxform_name);
        std::vector mtx_array(part_info.pointCount,
            GfMatrix4f(local_mtx * xforms[i]));
        HDGP_HAPI_CHECK(
            HAPI_SetAttributeFloatData(mySession.session(), mesh_node,
                part_id, hydraxform_name, &mtx_attr_info,
                reinterpret_cast<const float*>(mtx_array.data()),
                0, mtx_attr_info.count),
            "Failed to set matrix data for '%s'", hydraxform_name);

        HDGP_HAPI_CHECK_RETURN(
            HAPI_CommitGeo(mySession.session(), mesh_node), false,
            "Failed to commit input geometry");
    }

    return rewireGraph();
}

bool
HD_HdGpHapiScatterEngine::setPointsNodes(
        UT_ArrayStringMap<UT_Array<HAPI_NodeId>> &target_nodes,
        const std::string &id,
        const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms)
{
    if (!mySession.isValid())
    {
        TF_WARN("HAPI session is not properly initialized, "
                "unable to create/modify input node");
        return false;
    }

    size_t instance_count = xforms.size();

    // Reset signal
    if (id.empty() && !prim)
    {
        for (auto &&pair : target_nodes)
            for (auto node_id : pair.second)
                HDGP_HAPI_CHECK(
                    HAPI_DeleteNode(mySession.session(), node_id),
                    "Unable to delete previous points for %s",
                    pair.first.c_str());
        target_nodes.clear();
        return true;
    }

    if (auto iter = target_nodes.find(id); iter != target_nodes.end())
    {
        for (auto node_id : iter->second)
            HDGP_HAPI_CHECK(
                HAPI_DeleteNode(mySession.session(), node_id),
                "Unable to delete previous points for %s", id.c_str());
        target_nodes.erase(iter);
    }

    if (!prim)
        return true;

    // Skip when the instancer has no instances. The HAPI helper would
    // also return false in this case, but checking up front lets us avoid
    // creating (and then having to clean up) input nodes we won't fill.
    bool has_instances = false;
    if (auto primvars = HdPrimvarsSchema::GetFromParent(prim.dataSource))
    {
        if (auto pv = primvars.GetPrimvar(HdInstancerTokens->instanceTranslations))
        {
            if (HdSampledDataSourceHandle ds = pv.GetPrimvarValue())
            {
                VtValue v = ds->GetValue(0.0f);
                if (v.IsHolding<VtVec3fArray>() && v.GetArraySize() > 0)
                    has_instances = true;
            }
        }
    }
    if (!has_instances)
        return true;

    UT_String label(id);
    OP_Node::forceValidOpName(label);
    UT_Array<HAPI_NodeId> &points_nodes = target_nodes[id];
    points_nodes.setCapacity(instance_count);
    for (size_t i = 0; i < instance_count; ++i)
    {
        HAPI_NodeId points_node;
        HDGP_HAPI_CHECK_RETURN(
            HAPI_CreateInputNode(mySession.session(), mySession.parentNode(),
                &points_node, label.c_str()),
            false, "Failed to create HAPI input node");
        points_nodes.emplace_back(points_node);
    }

    GfMatrix4d local_mtx(1.0);
    if (auto xform = HdXformSchema::GetFromParent(prim.dataSource);
        xform.IsDefined())
        local_mtx = xform.GetMatrix()->GetTypedValue(0.0f);

    const HAPI_PartId part_id = 0;
    const char *hydraxform_name = "__hydraxform";
    for (size_t i = 0; i < instance_count; ++i)
    {
        HAPI_NodeId points_node = points_nodes[i];

        HAPI_PartInfo part_info;
        if (!HD_HdGpHapiSetPointsFromHydra(mySession.session(), points_node,
                part_id, prim, &part_info))
            return false;

        HAPI_AttributeInfo mtx_attr_info = HAPI_AttributeInfo_Create();
        mtx_attr_info.exists = true;
        mtx_attr_info.owner = HAPI_ATTROWNER_POINT;
        mtx_attr_info.count = part_info.pointCount;
        mtx_attr_info.tupleSize = 16;
        mtx_attr_info.storage = HAPI_STORAGETYPE_FLOAT;
        mtx_attr_info.typeInfo = HAPI_ATTRIBUTE_TYPE_MATRIX;
        HDGP_HAPI_CHECK(
            HAPI_AddAttribute(mySession.session(), points_node,
                part_id, hydraxform_name, &mtx_attr_info),
            "Failed to add attribute '%s'", hydraxform_name);
        std::vector mtx_array(part_info.pointCount,
            GfMatrix4f(local_mtx * xforms[i]));
        HDGP_HAPI_CHECK(
            HAPI_SetAttributeFloatData(mySession.session(), points_node,
                part_id, hydraxform_name, &mtx_attr_info,
                reinterpret_cast<const float*>(mtx_array.data()),
                0, mtx_attr_info.count),
            "Failed to set matrix data for '%s'", hydraxform_name);

        HDGP_HAPI_CHECK_RETURN(
            HAPI_CommitGeo(mySession.session(), points_node), false,
            "Failed to commit input geometry");
    }

    return rewireGraph();
}

bool
HD_HdGpHapiScatterEngine::setCameraImpl(
        const std::string &id,
        const HdSceneIndexPrim &prim)
{
    if (!mySession.isValid())
    {
        TF_WARN("HAPI session is not properly initialized, "
                "unable to create camera");
        return false;
    }

    if (myCameraNode != -1)
    {
        HDGP_HAPI_CHECK(HAPI_DeleteNode(mySession.session(), myCameraNode),
            "Failed to delete previous camera node");
        myCameraNode = -1;
    }

    // Being passed an empty id and prim is not an error, but a valid "reset"
    // signal, so we'll just return (after having destroyed any previous camera
    // as per above.
    if (id.empty() && !prim)
        return true;

    UT_String label(id);
    OP_Node::forceValidOpName(label);

    HDGP_HAPI_CHECK_RETURN(
        HAPI_CreateInputCameraNode(mySession.session(), mySession.parentNode(),
            &myCameraNode, id.c_str(), label.c_str()),
        false, "Failed to create camera node");

    if (!HD_HdGpHapiSetCameraFromHydra(mySession.session(), myCameraNode, prim))
        return false;

    HDGP_HAPI_CHECK_RETURN(
        HAPI_CommitGeo(mySession.session(), myCameraNode),
        false, "Failed to commit camera geometry");

    return rewireGraph();
}

HD_HdGpScatterEngine::Result
HD_HdGpHapiScatterEngine::cookImpl()
{
    if (!mySession.isValid() || myScatterNode == -1)
    {
        TF_WARN("HAPI and/or the scatter node are not properly initialized, "
                "unable to cook the graph");
        return {};
    }

    // TODO: is this best triggered by an env-var? a primvar? something else?
    if (HoudiniGetenv("HOUDINI_HDGP_DEBUG"))
    {
        UT_String tmp_path;
        UTgetTmpName(tmp_path, "hapiScatter");
        tmp_path += ".hip";
        HDGP_HAPI_CHECK(
            HAPI_SaveHIPFile(mySession.session(), tmp_path.c_str(), false),
            "Failed to save HIP file to '%s'", tmp_path.c_str());
    }

    HDGP_HAPI_CHECK_RETURN(
        HAPI_CookNode(mySession.session(), myScatterNode,
            &mySession.cookOptions()),
        {}, "Failed to cook scatter node");

    Result result;
    const HAPI_PartId part_id = 0;

    // Get point count from part info
    HAPI_PartInfo part_info = HAPI_PartInfo_Create();
    HDGP_HAPI_CHECK_RETURN(
        HAPI_GetPartInfo(mySession.session(), myScatterNode, part_id, &part_info), {},
        "Failed to get part info from scatter node");
    int point_count = part_info.pointCount;

    // Extract instance transforms
    std::vector<HAPI_Transform> transforms(point_count);
    HDGP_HAPI_CHECK_RETURN(
        HAPI_GetInstanceTransformsOnPart(mySession.session(), myScatterNode,
            part_id, HAPI_SRT, transforms.data(), 0, point_count), {},
            "Failed to get instance transforms");

    result.translations.resize(point_count);
    result.scales.resize(point_count);
    result.rotations.resize(point_count);
    for (int i = 0; i < point_count; ++i)
    {
        const HAPI_Transform &xf = transforms[i];
        const auto &t = xf.position;
        const auto &q = xf.rotationQuaternion;
        const auto &s = xf.scale;
        result.translations[i] = GfVec3f(t[0], t[1], t[2]);
        result.rotations[i] = GfQuath(q[3], { q[0], q[1], q[2] });
        result.scales[i] = GfVec3f(s[0], s[1], s[2]);
    }

    // Extract protoindex attribute (default to 0 if not present)
    const char *protoindex_name = "protoindex";
    HAPI_AttributeInfo id_info = HAPI_AttributeInfo_Create();
    HAPI_GetAttributeInfo(mySession.session(), myScatterNode, part_id,
        protoindex_name, HAPI_ATTROWNER_POINT, &id_info);
    if (id_info.exists)
    {
        result.protoindexes.resize(id_info.count);
        HDGP_HAPI_CHECK_RETURN(
            HAPI_GetAttributeIntData(mySession.session(), myScatterNode, part_id,
                protoindex_name, &id_info, -1,
                result.protoindexes.data(), 0, id_info.count), {},
            "Failed to get %s attribute data", protoindex_name);
    }
    else
    {
        result.protoindexes.resize(point_count, 0);
    }

    // Expose any remaining HAPI attributes on the cooked part as
    // per-instance Hydra primvars via the lazy proxy. The four names
    // already surfaced via the typed-array path above are skipped so
    // they don't show up twice. POINT-owner attribs are tagged with
    // `instance` interpolation since this is feeding an instancer prim.
    HD_HdGpHapiPrimvarsDataSource::TfTokenSet excluded;
    excluded.insert(HdPrimvarsSchemaTokens->points);
    excluded.insert(_tokens->orient);
    excluded.insert(_tokens->pscale);
    excluded.insert(_tokens->protoindex);
    result.extraPrimvars = HD_HdGpHapiPrimvarsDataSource::New(
            mySession.session(), myScatterNode, part_id,
            HdPrimvarSchemaTokens->instance, excluded);

    return result;
}

bool
HD_HdGpHapiScatterEngine::rewireGraph()
{
    if (!mySession.isValid())
    {
        TF_WARN("HAPI session is not properly initialized, "
                "unable to wire up the graph");
        return false;
    }

    // Wire up what we can. The absence of any nodes is not necessarily
    // indicative of a problem.

    if (myScatterNode == -1)
        return true;

    if (!wireMeshNodes(myScatterSurfaceMergeNode, myScatterSurfaceNodes, 0))
        return false;
    if (!wireMeshNodes(myMaskSurfaceMergeNode, myMaskSurfaceNodes, 4))
        return false;
    if (!wireMeshNodes(myMaskPointsMergeNode, myMaskPointsNodes, 5))
        return false;

    if (myCameraNode != -1)
    {
        HDGP_HAPI_CHECK_RETURN(
            HAPI_ConnectNodeInput(mySession.session(), myScatterNode, 6,
                myCameraNode, 0),
            false, "Failed to connect camera to scatter node");
    }

    return true;
}

bool
HD_HdGpHapiScatterEngine::wireMeshNodes(
        HAPI_NodeId &merge_node,
        const UT_ArrayStringMap<UT_Array<HAPI_NodeId>> &mesh_nodes,
        int scatter_input_idx)
{
    if (merge_node != -1)
    {
        HDGP_HAPI_CHECK(
            HAPI_DeleteNode(mySession.session(), merge_node),
            "Failed to delete previous merge node");
        merge_node = -1;
    }

    if (mesh_nodes.empty())
        return true;

    HDGP_HAPI_CHECK_RETURN(
        HAPI_CreateNode(mySession.session(),
            mySession.parentNode(), "merge", nullptr, false, &merge_node),
        false, "Failed to create merge node");
    int idx = 0;
    for (auto input : mesh_nodes)
    {
        for (auto node_id : input.second)
            HDGP_HAPI_CHECK_RETURN(
                HAPI_ConnectNodeInput(mySession.session(),
                    merge_node, idx++, node_id, 0),
                false, "Failed to connect mesh %s to merge",
                input.first.c_str());
    }
    HDGP_HAPI_CHECK_RETURN(
        HAPI_ConnectNodeInput(mySession.session(),
            myScatterNode, scatter_input_idx, merge_node, 0),
        false, "Failed to connect merge to scatter node input %d",
        scatter_input_idx);

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
