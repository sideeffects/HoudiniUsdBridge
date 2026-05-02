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
    (houdiniFieldAsset)         \
    (openvdbAsset)              \
    (boundingBox)               \
    (metaCurves)                \
    (karmaSkyAtmosphere)        \
    /* end macro */

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(HusdHdPrimTypeTokens, HUSD_API, HUSD_PRIMTYPE_TOKENS);
ARCH_PRAGMA_POP

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
    ((clippingRange,     "houdini:clippingRange"))      \
    ((fogIntensity,      "gl:fogintensity"))            \
    ((fogScatterPara,    "gl:fogscatterpara"))          \
    ((fogScatterPerp,    "gl:fogscatterperp"))          \
    ((singleSided,       "karma:light:singlesided"))    \
    ((guideScale,        "houdini:guidescale"))         \
    ((lightColor,        "light:color"))                \
    \
    ((attentype,         "karma:light:attentype"))      \
    ((atten,             "karma:light:atten"))          \
    ((attenstart,        "karma:light:attenstart"))     \
    \
    (none)              \
    (physical)          \
    (half)              \
    /* end macro */

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(HusdHdLightTokens, HUSD_API, HUSD_LIGHT_TOKENS);
ARCH_PRAGMA_POP

/// Tokens for camera parameters
#define HUSD_CAMERA_TOKENS \
    ((imagingDistance,     "houdini:imagingdistance"))      \
    /* end macro */

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(HusdCameraTokens, HUSD_API, HUSD_CAMERA_TOKENS);
ARCH_PRAGMA_POP

#define HUSD_PRIMVAR_TOKENS \
    ((viewLOD, "model:drawMode"))       \
    ((glWire, "houdini:gl_wireframe"))  \
    ((glCurveStyle, "houdini:gl_curve_style"))  \
    (uv)        \
    (widths)    \
    /* end macro */

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(HusdHdPrimvarTokens, HUSD_API, HUSD_PRIMVAR_TOKENS);
ARCH_PRAGMA_POP

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

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(HusdHdMaterialTokens, HUSD_API, HUSD_MATERIAL_TOKENS);
ARCH_PRAGMA_POP

#define HUSD_PRIMVALUE_TOKENS \
    (bounds)    \
    (cards)     \
    (origin)    \
    ((full, "default")) \
    (render)    \
    /* end macro */

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(HusdHdPrimValueTokens, HUSD_API, HUSD_PRIMVALUE_TOKENS);
ARCH_PRAGMA_POP

#define HUSD_RENDERSTATS_TOKENS \
    (rendererName)      \
    (rendererStage)     \
    (renderProgressAnnotation)  \
    (renderStatsAnnotation)     \
    (activeBuckets)     \
    (huskErrorStatus)   \
    (percentDone)       \
    (fractionDone)      \
    (totalClockTime)    \
    (totalUTime)        \
    (totalSTime)        \
    (totalMemory)       \
    (peakMemory)        \
    (viewerMouseClick)  \
    /* end macro */

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(HusdHdRenderStatsTokens, HUSD_API, HUSD_RENDERSTATS_TOKENS);
ARCH_PRAGMA_POP

#define HUSD_HUSK_TOKENS \
    (aovBindings)               \
    (color4f)                   \
    (dataType)                  \
    (delegateRenderProducts)    \
    (extra_aov_resource)        \
    (distant)                   \
    (dome)                      \
    (none)                      \
    (husk)                      \
    (includeAovs)               \
    (includedPurposes)          \
    (invalidConformPolicy)      \
    (ip)                        \
    (karmaTask)                 \
    (orderedVars)               \
    (orderedFilters)            \
    (randomseed)                \
    (rasterRenderProducts)      \
    (renderBufferDescriptor)    \
    (renderCameraPath)          \
    (renderPassState)           \
    (sourceName)                \
    (sourcePrim)                \
    (sourceType)                \
    (stageMetersPerUnit)        \
    (vex)                       \
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
    ((driver_parameters_aov_husk_clearValue,  "driver:parameters:aov:husk:clearValue"))  \
    ((driver_parameters_aov_husk_format,      "driver:parameters:aov:husk:format"))      \
    ((driver_parameters_aov_husk_multiSampled,"driver:parameters:aov:husk:multiSampled"))\
    ((driver_parameters_aov_husk_name,        "driver:parameters:aov:husk:name"))        \
    ((husk_orderedImageFilters,         "husk:orderedImageFilters"))        \
    ((houdini_fps,                      "houdini:fps"))                     \
    ((houdini_frame,                    "houdini:frame"))                   \
    ((houdini_cop_texture_changed,      "houdini:cop_texture_changed"))     \
    ((houdini_renderer,                 "houdini:renderer"))                \
    ((husk_snapshot,                    "husk:snapshot"))                   \
    ((houdini_interactive,              "houdini:interactive"))             \
    ((husk_mplay,                       "husk:mplay"))                      \
    ((huskNullRaster,                   "husk:null_raster"))                \
    ((husk_productmetadata, "husk:productmetadata")) \
    ((karma_global_randomseed,         "karma:global:randomseed"))      \
    /* end macro */

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(HusdHuskTokens, HUSD_API, HUSD_HUSK_TOKENS);
ARCH_PRAGMA_POP

/// Hydra tokens for APEX schemas
#define HUSD_APEX_TOKENS \
    (houdiniApexCharacter) \
    (houdiniApexCharacterBindings) \
    (houdiniApexShapeBindings) \
    (houdiniApexXformBindings) \
    (houdiniApexScene) \
    (houdiniApexShapeDeform) \
    (binding) \
    (files) \
    (input) \
    (joint) \
    (joints) \
    (output) \
    (rig) \
    ((houdiniApexDeformJointIndices, "houdini:apex:deform:jointIndices")) \
    ((houdiniApexDeformJointWeights, "houdini:apex:deform:jointWeights")) \
    /* end macro */

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(HusdHdApexTokens, HUSD_API, HUSD_APEX_TOKENS);
ARCH_PRAGMA_POP

PXR_NAMESPACE_CLOSE_SCOPE

#endif //HUSD_TOKENS_H

