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

#ifndef __HD_HdGpHapiEngine__
#define __HD_HdGpHapiEngine__

#include "HD_HdGpHapiUtils.h"
#include "HD_HdGpScatterEngine.h"

#include <HAPI/HAPI.h>
#include <UT/UT_Array.h>
#include <UT/UT_ArrayStringMap.h>

#include <pxr/base/gf/matrix4d.h>

PXR_NAMESPACE_OPEN_SCOPE

class HD_HdGpHapiScatterEngine : public HD_HdGpScatterEngine
{
public:
    HD_HdGpHapiScatterEngine() = default;
    ~HD_HdGpHapiScatterEngine() override = default;

protected:
    bool loadGraphImpl(const std::string &path) override;
    bool setParmsImpl(const UT_Options &parms) override;
    bool setScatterSurfaceImpl(
        const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms) override;
    bool setMaskSurfaceImpl(
        const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms) override;
    bool setMaskPointsImpl(
        const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms) override;
    bool setCameraImpl(
        const std::string &id, const HdSceneIndexPrim &prim) override;
    Result cookImpl() override;

    bool rewireGraph();

private:
    bool setMeshNodes(
        UT_ArrayStringMap<UT_Array<HAPI_NodeId>> &target_nodes,
        const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms);
    bool setPointsNodes(
        UT_ArrayStringMap<UT_Array<HAPI_NodeId>> &target_nodes,
        const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms);
    bool wireMeshNodes(
        HAPI_NodeId &merge_node,
        const UT_ArrayStringMap<UT_Array<HAPI_NodeId>> &mesh_nodes,
        int scatter_input_idx);

    HD_HdGpHapiSession mySession;
    UT_ArrayStringMap<UT_Array<HAPI_NodeId>> myScatterSurfaceNodes;
    HAPI_NodeId myScatterSurfaceMergeNode = -1;
    UT_ArrayStringMap<UT_Array<HAPI_NodeId>> myMaskSurfaceNodes;
    HAPI_NodeId myMaskSurfaceMergeNode = -1;
    UT_ArrayStringMap<UT_Array<HAPI_NodeId>> myMaskPointsNodes;
    HAPI_NodeId myMaskPointsMergeNode = -1;
    HAPI_NodeId myCameraNode = -1;
    HAPI_NodeId myScatterNode = -1;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
