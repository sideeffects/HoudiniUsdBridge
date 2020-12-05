//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "XUSD_Tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

HusdHdPrimTypeTokensType::HusdHdPrimTypeTokensType()
    : sprimGeometryLight("sprimGeometryLight", TfToken::Immortal),
      bprimHoudiniFieldAsset("bprimHoudiniFieldAsset", TfToken::Immortal),
      openvdbAsset("openvdbAsset", TfToken::Immortal)
{
}

TfStaticData<HusdHdPrimTypeTokensType> &HusdHdPrimTypeTokens()
{
    static TfStaticData<HusdHdPrimTypeTokensType> theTokens;

    return theTokens;
}

HusdHdLightTokensType::HusdHdLightTokensType()
    : attenStart("mantra:atten:start", TfToken::Immortal)
    , attenType("mantra:atten:type", TfToken::Immortal)
    , attenDist("mantra:atten:dist", TfToken::Immortal)
    , coneAngle("shaping:cone:angle", TfToken::Immortal)
    , coneSoftness("shaping:cone:softness", TfToken::Immortal)
    , coneDelta("mantra:shaping:cone:delta", TfToken::Immortal)
    , coneRolloff("mantra:shaping:cone:rolloff", TfToken::Immortal)
    , diffuse("diffuse", TfToken::Immortal)
    , specular("specular", TfToken::Immortal)
    , barndoorleft("barndoorleft", TfToken::Immortal)
    , barndoorleftedge("barndoorleftedge", TfToken::Immortal)
    , barndoorright("barndoorright", TfToken::Immortal)
    , barndoorrightedge("barndoorrightedge", TfToken::Immortal)
    , barndoortop("barndoortop", TfToken::Immortal)
    , barndoortopedge("barndoortopedge", TfToken::Immortal)
    , barndoorbottom("barndoorbottom", TfToken::Immortal)
    , barndoorbottomedge("barndoorbottomedge", TfToken::Immortal)
    , shadowIntensity("mantra:shadow:intensity", TfToken::Immortal)
    , none("none", TfToken::Immortal)
    , physical("physical", TfToken::Immortal)
    , halfDistance("half", TfToken::Immortal)
    , activeRadiusEnable("mantra:light:activeRadiusEnable", TfToken::Immortal)
    , activeRadius("mantra:light:activeRadius", TfToken::Immortal)
    , shadowType("mantra:shadow:type", TfToken::Immortal)
    , projectMap("texture:file", TfToken::Immortal)
    , projectAngle("mantra:projection:angle", TfToken::Immortal)
    , clipNear("mantra:clipping:near", TfToken::Immortal)
    , clipFar("mantra:clipping:far", TfToken::Immortal)
    , fogIntensity("gl:fogintensity", TfToken::Immortal)
    , fogScatterPara("gl:fogscatterpara", TfToken::Immortal)
    , fogScatterPerp("gl:fogscatterperp", TfToken::Immortal)
    , singleSided("karma:light:singlesided", TfToken::Immortal)
{
}

TfStaticData<HusdHdLightTokensType> &HusdHdLightTokens()
{
    static TfStaticData<HusdHdLightTokensType> theTokens;
    return theTokens;
}

HusdHdCameraTokensType::HusdHdCameraTokensType()
    : inViewerMenu("houdini:inviewermenu", TfToken::Immortal)
{
}

TfStaticData<HusdHdCameraTokensType> &HusdHdCameraTokens()
{
    static TfStaticData<HusdHdCameraTokensType> theTokens;
    return theTokens;
}


HusdHdPrimvarTokensType::HusdHdPrimvarTokensType()
    : translate("translate", TfToken::Immortal),
      rotate("rotate", TfToken::Immortal),
      scale("scale", TfToken::Immortal),
      instanceTransform("instanceTransform", TfToken::Immortal),
      viewLOD("model:drawMode", TfToken::Immortal),
      uv("uv", TfToken::Immortal)
{}

TfStaticData<HusdHdPrimvarTokensType> &HusdHdPrimvarTokens()
{
    static TfStaticData<HusdHdPrimvarTokensType> theTokens;
    return theTokens;
}


HusdHdMaterialTokensType::HusdHdMaterialTokensType()
       // node types
    :  usdPreviewMaterial("UsdPreviewSurface", TfToken::Immortal),
       usdPrimvarReader("UsdPrimvarReader", TfToken::Immortal),
       usdUVTexture("UsdUVTexture", TfToken::Immortal),
       // parms
       bias("bias", TfToken::Immortal),
       diffuseColor("diffuseColor", TfToken::Immortal),
       emissiveColor("emissiveColor", TfToken::Immortal),
       specularColor("specularColor", TfToken::Immortal),
       clearcoat("clearcoat", TfToken::Immortal),
       clearcoatRoughness("clearcoatRoughness", TfToken::Immortal),
       displacement("displacement", TfToken::Immortal),
       file("file", TfToken::Immortal),
       ior("ior", TfToken::Immortal),
       metallic("metallic", TfToken::Immortal),
       normal("normal", TfToken::Immortal),
       occlusion("occlusion", TfToken::Immortal),
       opacity("opacity", TfToken::Immortal),
       roughness("roughness", TfToken::Immortal),
       scale("scale", TfToken::Immortal),
       useSpecWorkflow("useSpecularWorkflow", TfToken::Immortal),
       varname("varname", TfToken::Immortal),
       wrapS("wrapS", TfToken::Immortal),
       wrapT("wrapT", TfToken::Immortal)
{
}

TfStaticData<HusdHdMaterialTokensType> &HusdHdMaterialTokens()
{
    static TfStaticData<HusdHdMaterialTokensType> theTokens;
    return theTokens;
}


HusdHdPrimValueTokenType::HusdHdPrimValueTokenType()
    : bounds("bounds", TfToken::Immortal),
      cards("cards", TfToken::Immortal),
      origin("origin", TfToken::Immortal),
      full("default", TfToken::Immortal),
      render("render", TfToken::Immortal)
{}

TfStaticData<HusdHdPrimValueTokenType> &HusdHdPrimValueTokens()
{
    static TfStaticData<HusdHdPrimValueTokenType> theTokens;
    return theTokens;
}

HusdHdRenderStatsTokensType::HusdHdRenderStatsTokensType()
    : rendererName("rendererName", TfToken::Immortal)
    , rendererVersion("rendererVersion", TfToken::Immortal)
    , worldToCamera("worldToCamera", TfToken::Immortal)
    , worldToScreen("worldToScreen", TfToken::Immortal)
    , percentDone("percentDone", TfToken::Immortal)
    , fractionDone("fractionDone", TfToken::Immortal)
    , cameraRays("cameraRays", TfToken::Immortal)
    , indirectRays("indirectRays", TfToken::Immortal)
    , occlusionRays("occlusionRays", TfToken::Immortal)
    , lightGeoRays("lightGeoRays", TfToken::Immortal)
    , probeRays("probeRays", TfToken::Immortal)
    , polyCounts("polyCounts", TfToken::Immortal)
    , curveCounts("curveCounts", TfToken::Immortal)
    , pointCounts("pointCounts", TfToken::Immortal)
    , pointMeshCounts("pointMeshCounts", TfToken::Immortal)
    , volumeCounts("volumeCounts", TfToken::Immortal)
    , proceduralCounts("proceduralCounts", TfToken::Immortal)
    , lightCounts("lightCounts", TfToken::Immortal)
    , lightTreeCounts("lightTreeCounts", TfToken::Immortal)
    , cameraCounts("cameraCounts", TfToken::Immortal)
    , octreeBuildTime("octreeBuildTime", TfToken::Immortal)
    , loadClockTime("loadClockTime", TfToken::Immortal)
    , loadUTime("loadUTime", TfToken::Immortal)
    , loadSTime("loadSTime", TfToken::Immortal)
    , loadMemory("loadMemory", TfToken::Immortal)
    , totalClockTime("totalClockTime", TfToken::Immortal)
    , totalUTime("totalUTime", TfToken::Immortal)
    , totalSTime("totalSTime", TfToken::Immortal)
    , totalMemory("totalMemory", TfToken::Immortal)
    , peakMemory("peakMemory", TfToken::Immortal)
{
}

TfStaticData<HusdHdRenderStatsTokensType> &
HusdHdRenderStatsTokens()
{
    static TfStaticData<HusdHdRenderStatsTokensType> theTokens;
    return theTokens;
}


PXR_NAMESPACE_CLOSE_SCOPE

