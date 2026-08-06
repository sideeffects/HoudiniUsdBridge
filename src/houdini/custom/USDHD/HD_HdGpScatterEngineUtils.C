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

#include "HD_HdGpScatterEngine.h"

#include <GA/GA_Names.h>
#include <GU/GU_Detail.h>
#include <GU/GU_PrimPolySoup.h>

#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>

PXR_NAMESPACE_OPEN_SCOPE

GU_ConstDetailHandle
HD_HdGpMakeGuDetailFromMesh(const HdSceneIndexPrim &prim)
{
    auto primvars = HdPrimvarsSchema::GetFromParent(prim.dataSource);
    HdPrimvarSchema points = primvars.GetPrimvar(HdPrimvarsSchemaTokens->points);
    HdSampledDataSourceHandle pointsDs = points.GetPrimvarValue();
    VtValue pointsVt = pointsDs->GetValue(0.0f);
    VtVec3fArray pointsData = pointsVt.UncheckedGet<VtVec3fArray>();

    auto mesh = HdMeshSchema::GetFromParent(prim.dataSource);
    auto topology = HdMeshTopologySchema::GetFromParent(mesh.GetContainer());
    auto fvcData = topology.GetFaceVertexCounts()->GetTypedValue(0.0f);
    GEO_PolyCounts polycounts;
    for (auto fvc : fvcData)
        polycounts.append(fvc);
    auto fviData = topology.GetFaceVertexIndices()->GetTypedValue(0.0f);

    GU_DetailHandle result;
    GU_Detail *dst = new GU_Detail();
    GU_PrimPolySoup::build(dst,
        (const UT_Vector3*)&pointsData.cfront(),
        pointsData.size(),
        polycounts,
        &fviData.cfront());

    result.allocateAndSet(dst);
    return result;
}

GU_ConstDetailHandle
HD_HdGpMakeGuDetailFromOptions(const UT_Options &opts)
{
    GU_Detail *gdp = new GU_Detail();

    gdp->addDictTuple(GA_ATTRIB_DETAIL, "parms", 1);
    GA_RWHandleDict attrib_h;
    attrib_h.bind(gdp, GA_ATTRIB_DETAIL, "parms");
    attrib_h.set(GA_DETAIL_OFFSET, UT_OptionsHolder(&opts));

    GU_DetailHandle gdh;
    gdh.allocateAndSet(gdp);
    return gdh;
}

HD_HdGpScatterEngine::Result
HD_HdGpExtractResultFromGuDetail(GU_ConstDetailHandle gdh)
{
    const GU_Detail *gdp = gdh.gdp();
    GA_Range ptRange = gdp->getPointRange();
    
    UT_Vector3FArray pArray;
    gdp->getPos3AsArray(ptRange, pArray);
    size_t npts = pArray.size();
    VtVec3fArray pVtArray(npts);
    auto pSpan = TfMakeSpan(pVtArray);
    for (size_t i = 0; i < npts; ++i)
        pSpan[i] = *(GfVec3f*)&pArray[i];
    
    VtIntArray idVtArray;
    idVtArray.reserve(npts);
    GA_ROHandleI idHnd = gdp->findIntTuple(GA_ATTRIB_POINT, GA_Names::id);
    if (idHnd.isValid())
    {
        for (GA_Offset offset : ptRange)
            idVtArray.push_back(idHnd.get(offset));
    }
    else
    {
        idVtArray.resize(npts, 0);
    }
    
    VtVec3fArray scaleVtArray(npts, GfVec3f(1.f));
    VtQuathArray rotVtArray(npts, GfQuath::GetIdentity());

    return { pVtArray, scaleVtArray, rotVtArray, idVtArray, nullptr };
}

GU_ConstDetailHandle
HD_HdGpLoadGraph(const std::string &graph)
{
    GU_DetailHandle gdh;
    GU_Detail *dst = new GU_Detail();
    gdh.allocateAndSet(dst);
    if (!dst->load(graph.c_str()))
    {
        TF_WARN("Failed to load graph from '%s'", graph.c_str());
        return GU_ConstDetailHandle();
    }
    return gdh;
}

PXR_NAMESPACE_CLOSE_SCOPE
