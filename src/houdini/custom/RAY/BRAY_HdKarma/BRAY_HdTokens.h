/*
 * Copyright 2019 Side Effects Software Inc.
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
 *
 * Produced by:
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#ifndef __BRAY_HdTokens__
#define __BRAY_HdTokens__

#include <pxr/pxr.h>
#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE

#define BRAY_HD
#define BRAY_HD_TOKENS \
    (LightFilter)		\
    (N)		                \
    (UsdPreviewSurface)         \
    (VEX)		        \
    (houdiniFieldAsset)         \
    (karmaSkyAtmosphere)        \
    (delegateRenderProducts)	\
    (extra_aov_resource)	\
    (fallback)		        \
    (ids)		        \
    (invalidConformPolicy)      \
    (karma)		        \
    (kma)		        \
    (karma_procedural)          \
    (karma_xpu)		        \
    (vex)		        \
    (leftHanded)		\
    (mtlx)		        \
    (openvdbAsset)              \
    (primvarUsage)		\
    (rasterRenderProducts)	\
    (renderCameraPath)		\
    (renderProgressAnnotation)	\
    (renderStatsAnnotation)	\
    (usdFileTimeStamp)		\
    (usdFilename)		\
    (varname)		        \
    (viewerMouseClick)		\
    (xpu_devices)               \
    (ND_normalmap)              \
    \
    (barndoorleft)              \
    (barndoorleftedge)          \
    (barndoorright)             \
    (barndoorrightedge)         \
    (barndoortop)               \
    (barndoortopedge)           \
    (barndoorbottom)            \
    (barndoorbottomedge)        \
    \
    /* duplicated render stat tokens from HUSD */  \
    (huskErrorStatus)           \
    (PxrDistantLight)           \
    (PxrDomeLight)              \
    \
    ((unknown_src_type, "unknown source type"))                         \
    \
    ((colorSpace_file, "colorSpace:file"))                              \
    ((houdini_fps, "houdini:fps"))                                      \
    ((houdini_frame, "houdini:frame"))                                  \
    ((houdini_cop_texture_changed, "houdini:cop_texture_changed"))      \
    ((houdini_interactive, "houdini:interactive"))                      \
    ((houdini_render_pause, "houdini:render_pause"))                    \
    ((houdini_viewport, "houdini:viewport"))                            \
    ((husk_productmetadata, "husk:productmetadata"))                    \
    ((husk_snapshot, "husk:snapshot"))                                  \
    \
    ((hydra_override_lighting, "karma:hydra:override_lighting"))        \
    ((hydra_override_lighting_menu, "karma:hydra:override_lighting_menu")) \
    ((hydra_disablelighting, "karma:hydra:disablelighting"))            \
    ((hydra_denoise, "karma:hydra:denoise"))                            \
    ((hydra_variance, "karma:hydra:variance"))                          \
    \
    ((karma_info_id, "karma:info:id"))                                  \
    ((karma_camera_use_lensshader, "karma:camera:use_lensshader"))      \
    ((karma_global_rendercamera, "karma:global:rendercamera"))          \
    ((karma_global_engine, "karma:global:engine"))                      \
    ((karma_object_rendervisibility, "karma:object:rendervisibility"))  \
    \
    ((aovDescriptor_aovSettings,         "aovDescriptor.aovSettings"))  \
    ((driver_parameters_aov_husk_name,        "driver:parameters:aov:husk:name")) \
    ((driver_parameters_aov_husk_format,      "driver:parameters:aov:husk:format")) \
    ((driver_parameters_aov_husk_multiSample, "driver:parameters:aov:husk:multiSample")) \
    ((driver_parameters_aov_name,        "driver:parameters:aov:name")) \
    ((driver_parameters_aov_format,      "driver:parameters:aov:format")) \
    ((driver_parameters_aov_multiSample, "driver:parameters:aov:multiSample")) \
 /* end macro */

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DECLARE_PUBLIC_TOKENS(BRAYHdTokens, BRAY_HD, BRAY_HD_TOKENS);
ARCH_PRAGMA_POP

#undef BRAY_HD

PXR_NAMESPACE_CLOSE_SCOPE

#endif
