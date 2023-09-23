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
#include "./tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

UsdHoudiniTokensType::UsdHoudiniTokensType() :
    houdiniBackgroundimage("houdini:backgroundimage", TfToken::Immortal),
    houdiniClippingRange("houdini:clippingRange", TfToken::Immortal),
    houdiniEditable("houdini:editable", TfToken::Immortal),
    houdiniForegroundimage("houdini:foregroundimage", TfToken::Immortal),
    houdiniGuidescale("houdini:guidescale", TfToken::Immortal),
    houdiniInviewermenu("houdini:inviewermenu", TfToken::Immortal),
    houdiniProcedural("houdiniProcedural", TfToken::Immortal),
    houdiniProcedural_MultipleApplyTemplate_HoudiniActive("houdiniProcedural:__INSTANCE_NAME__:houdini:active", TfToken::Immortal),
    houdiniProcedural_MultipleApplyTemplate_HoudiniAnimated("houdiniProcedural:__INSTANCE_NAME__:houdini:animated", TfToken::Immortal),
    houdiniProcedural_MultipleApplyTemplate_HoudiniPriority("houdiniProcedural:__INSTANCE_NAME__:houdini:priority", TfToken::Immortal),
    houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralArgs("houdiniProcedural:__INSTANCE_NAME__:houdini:procedural:args", TfToken::Immortal),
    houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralPath("houdiniProcedural:__INSTANCE_NAME__:houdini:procedural:path", TfToken::Immortal),
    houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralType("houdiniProcedural:__INSTANCE_NAME__:houdini:procedural:type", TfToken::Immortal),
    houdiniSelectable("houdini:selectable", TfToken::Immortal),
    HoudiniCameraPlateAPI("HoudiniCameraPlateAPI", TfToken::Immortal),
    HoudiniEditableAPI("HoudiniEditableAPI", TfToken::Immortal),
    HoudiniFieldAsset("HoudiniFieldAsset", TfToken::Immortal),
    HoudiniLayerInfo("HoudiniLayerInfo", TfToken::Immortal),
    HoudiniMetaCurves("HoudiniMetaCurves", TfToken::Immortal),
    HoudiniProceduralAPI("HoudiniProceduralAPI", TfToken::Immortal),
    HoudiniSelectableAPI("HoudiniSelectableAPI", TfToken::Immortal),
    HoudiniViewportGuideAPI("HoudiniViewportGuideAPI", TfToken::Immortal),
    HoudiniViewportLightAPI("HoudiniViewportLightAPI", TfToken::Immortal),
    allTokens({
        houdiniBackgroundimage,
        houdiniClippingRange,
        houdiniEditable,
        houdiniForegroundimage,
        houdiniGuidescale,
        houdiniInviewermenu,
        houdiniProcedural,
        houdiniProcedural_MultipleApplyTemplate_HoudiniActive,
        houdiniProcedural_MultipleApplyTemplate_HoudiniAnimated,
        houdiniProcedural_MultipleApplyTemplate_HoudiniPriority,
        houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralArgs,
        houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralPath,
        houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralType,
        houdiniSelectable,
        HoudiniCameraPlateAPI,
        HoudiniEditableAPI,
        HoudiniFieldAsset,
        HoudiniLayerInfo,
        HoudiniMetaCurves,
        HoudiniProceduralAPI,
        HoudiniSelectableAPI,
        HoudiniViewportGuideAPI,
        HoudiniViewportLightAPI
    })
{
}

TfStaticData<UsdHoudiniTokensType> UsdHoudiniTokens;

PXR_NAMESPACE_CLOSE_SCOPE
