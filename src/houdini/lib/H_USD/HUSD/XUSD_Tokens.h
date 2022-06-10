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
#ifndef HUSD_TOKENS_H
#define HUSD_TOKENS_H

#include "HUSD_API.h"
#include <pxr/pxr.h>
#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE

#define HUSD_PRIMTYPE_TOKENS \
    (bprimHoudiniFieldAsset)    \
    (openvdbAsset)              \
    (boundingBox)               \
    (metaCurves)                \
    /* end macro */

TF_DECLARE_PUBLIC_TOKENS(HusdHdPrimTypeTokens, HUSD_API, HUSD_PRIMTYPE_TOKENS);

/// Tokens for light parameters
#define HUSD_LIGHT_TOKENS \
    (barndoorleft)      \
    (barndoorleftedge)  \
    (barndoorright)     \
    (barndoorrightedge) \
    (barndoortop)       \
    (barndoortopedge)   \
    (barndoorbottom)    \
    (barndoorbottomedge)\
    ((fogIntensity,      "gl:fogintensity"))            \
    ((fogScatterPara,    "gl:fogscatterpara"))          \
    ((fogScatterPerp,    "gl:fogscatterperp"))          \
    ((singleSided,       "karma:light:singlesided"))    \
    ((guideScale,        "houdini:guidescale"))         \
    \
    ((attentype,         "karma:light:attentype"))      \
    ((atten,             "karma:light:atten"))          \
    ((attenstart,        "karma:light:attenstart"))     \
    \
    (none)              \
    (physical)          \
    (half)              \
    /* end macro */

TF_DECLARE_PUBLIC_TOKENS(HusdHdLightTokens, HUSD_API, HUSD_LIGHT_TOKENS);

#define HUSD_PRIMVAR_TOKENS \
    ((viewLOD, "model:drawMode"))       \
    ((glWire, "houdini:gl_wireframe"))  \
    (uv)        \
    (widths)    \
    /* end macro */

TF_DECLARE_PUBLIC_TOKENS(HusdHdPrimvarTokens, HUSD_API, HUSD_PRIMVAR_TOKENS);

#define HUSD_MATERIAL_TOKENS \
    (UsdPreviewSurface) \
    (UsdPrimvarReader)  \
    (UsdUVTexture)      \
    (UsdTransform2d)    \
    \
    (bias)              \
    (diffuseColor)      \
    (emissiveColor)     \
    (specularColor)     \
    (clearcoat)         \
    (clearcoatRoughness)\
    (displacement)      \
    (fallback)          \
    (file)              \
    (ior)               \
    (metallic)          \
    (normal)            \
    (occlusion)         \
    (opacity)           \
    (opacityThreshold)  \
    (roughness)         \
    (rotation)          \
    (scale)             \
    (translation)       \
    (useSpecularWorkflow)      \
    (varname)           \
    (wrapS)             \
    (wrapT)             \
    /* end macro */

TF_DECLARE_PUBLIC_TOKENS(HusdHdMaterialTokens, HUSD_API, HUSD_MATERIAL_TOKENS);

#define HUSD_PRIMVALUE_TOKENS \
    (bounds)    \
    (cards)     \
    (origin)    \
    ((full, "default")) \
    (render)    \
    /* end macro */

TF_DECLARE_PUBLIC_TOKENS(HusdHdPrimValueTokens, HUSD_API, HUSD_PRIMVALUE_TOKENS);

#define HUSD_RENDERSTATS_TOKENS \
    (rendererName)      \
    (rendererVersion)   \
    (rendererSettings)  \
    (worldToCamera)     \
    (worldToScreen)     \
    (worldToNDC)        \
    (worldToRaster)     \
    (clipNear)          \
    (clipFar)           \
    (percentDone)       \
    (fractionDone)      \
    (cameraRays)        \
    (indirectRays)      \
    (occlusionRays)     \
    (lightGeoRays)      \
    (probeRays)         \
    (polyCounts)        \
    (curveCounts)       \
    (pointCounts)       \
    (pointMeshCounts)   \
    (volumeCounts)      \
    (proceduralCounts)  \
    (lightCounts)       \
    (lightTreeCounts)   \
    (cameraCounts)      \
    (coordSysCounts)    \
    (octreeBuildTime)   \
    (loadClockTime)     \
    (loadUTime)         \
    (loadSTime)         \
    (loadMemory)        \
    (totalClockTime)    \
    (totalUTime)        \
    (totalSTime)        \
    (totalMemory)       \
    (peakMemory)        \
    (viewerMouseClick)  \
    /* end macro */

TF_DECLARE_PUBLIC_TOKENS(HusdHdRenderStatsTokens, HUSD_API, HUSD_RENDERSTATS_TOKENS);

#define HUSD_HUSK_TOKENS \
    (aovBindings)               \
    (color4f)                   \
    (dataType)                  \
    (delegateRenderProducts)    \
    (extra_aov_resource)        \
    (husk)                      \
    (includeAovs)               \
    (includedPurposes)          \
    (invalidConformPolicy)      \
    (ip)                        \
    (karma)                     \
    (karmaTask)                 \
    (orderedVars)               \
    (randomseed)                \
    (renderBufferDescriptor)    \
    (renderCameraPath)          \
    (renderPassState)           \
    (sourceName)                \
    (sourcePrim)                \
    (sourceType)                \
    (stageMetersPerUnit)        \
    (viewerMouseClick)          \
    \
    ((aovDescriptor_aovSettings,        "aovDescriptor.aovSettings"))   \
    ((aovDescriptor_clearValue,         "aovDescriptor.clearValue"))    \
    ((aovDescriptor_format,             "aovDescriptor.format"))        \
    ((aovDescriptor_multiSampled,       "aovDescriptor.multiSampled"))  \
    ((driver_parameters_aov_clearValue,  "driver:parameters:aov:clearValue"))  \
    ((driver_parameters_aov_format,      "driver:parameters:aov:format"))      \
    ((driver_parameters_aov_multiSampled,"driver:parameters:aov:multiSampled"))\
    ((driver_parameters_aov_name,        "driver:parameters:aov:name"))        \
    ((houdini_fps,      "houdini:fps"))         \
    ((houdini_frame,    "houdini:frame"))       \
    ((houdini_renderer, "houdini:renderer"))    \
    ((husk_snapshot,    "husk:snapshot"))       \
    /* end macro */
TF_DECLARE_PUBLIC_TOKENS(HusdHuskTokens, HUSD_API, HUSD_HUSK_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

#endif //HUSD_TOKENS_H

