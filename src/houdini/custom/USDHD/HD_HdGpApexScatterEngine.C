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

#include "HD_HdGpApexScatterEngine.h"
#include "HD_HdGpScatterEngineUtils.h"

#include <GU/GU_Detail.h>

using namespace apex;

PXR_NAMESPACE_USING_DIRECTIVE

void
HD_HdGpApexScatterEngine::loadGraphImpl(const std::string &path)
{
    GU_Detail gdp;
    gdp.load(path.c_str());
    myGraph.loadFromGeometry(&gdp);
    myGraph.compileProgram();
}

void
HD_HdGpApexScatterEngine::setParmsImpl(const UT_Options &parms)
{
    Dict &graph_parms = myGraph.getParameters();
    graph_parms->clear();
    graph_parms->mergeUtOptions(UT_OptionsHolder(&parms));
}

HD_HdGpScatterEngine::Result
HD_HdGpApexScatterEngine::cookImpl(const HdSceneIndexPrim &prim)
{
    GU_ConstDetailHandle gdh_mesh = HD_HdGpMakeGuDetailFromMesh(prim);
    Geometry apex_mesh(gdh_mesh.castAwayConst());

    Dict &graph_parms = myGraph.getParameters();
    graph_parms->set("geo", apex_mesh);

    APEX_Argument *output = myGraph.evaluateOutput("output:geo");
    if (!output)
    {
        TF_WARN("No geometry generated");
        return {};
    }
    
    Geometry *pts_geo = castArg<Geometry>(output);
    if (!pts_geo)
    {
        TF_WARN("Output from APEX graph is not a geometry");
        return {};
    }
    return HD_HdGpExtractResultFromGuDetail(pts_geo->asConstHandle());
}
