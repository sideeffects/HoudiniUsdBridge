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

#include "HD_HdGpScatterEngineUtils.h"
#include "HD_HdGpSopScatterEngine.h"

#include <GU/GU_Detail.h>
#include <SOP/SOP_NodeVerb.h>

PXR_NAMESPACE_OPEN_SCOPE

void
HD_HdGpSopScatterEngine::loadGraph(const std::string &graphpath)
{
    GU_DetailHandle gdh;
    GU_Detail *gdp = new GU_Detail();
    gdh.allocateAndSet(gdp);
    gdp->load(graphpath.c_str());
    myGraph = gdh;
}

VtVec3fArray
HD_HdGpSopScatterEngine::cook(
    const HdSceneIndexPrim &prim,
    const UT_Options &overrides)
{
    GU_ConstDetailHandle gdh_mesh = HD_HdGpMakeGuDetailFromMesh(prim);
    GU_ConstDetailHandle gdh_overrides = HD_HdGpMakeGuDetailFromOptions(overrides);
    UT_Array inputs({myGraph, gdh_mesh, gdh_overrides});

    GU_DetailHandle gdh_out;
    GU_Detail *dst = new GU_Detail();
    gdh_out.allocateAndSet(dst);

    OP_Context ctx;
    UT_ErrorManager err_mgr;
    DEP_MicroNode depnode;

    const SOP_NodeVerb *invoke = SOP_NodeVerb::lookupVerb("invokegraph");
    SOP_NodeCache *cache = invoke->allocCache();
    SOP_NodeParms *parms = invoke->allocParms();
    UT_Options invoke_overrides;
    invoke_overrides.setOptionI("method", 1);
    invoke_overrides.setOptionS("inputgroup", "inputs");
    parms->applyOptionsOverride(&invoke_overrides);

    SOP_NodeVerb::CookParms cook_parms(
        /*destgdh=*/ gdh_out,
        /*inputs=*/ inputs,
        /*cookengine=*/ OP_COOK_COMPILED,
        /*node=*/ nullptr,
        /*contex=*/ ctx,
        /*parms=*/ parms,
        /*cache=*/ cache,
        /*error=*/ &err_mgr,
        /*depnode=*/ &depnode);
    invoke->cook(cook_parms);

    delete parms;
    delete cache;

    return HD_HdGpExtractPointsFromGuDetail(gdh_out);
}

PXR_NAMESPACE_CLOSE_SCOPE
