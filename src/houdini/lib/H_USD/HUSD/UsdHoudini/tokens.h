//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_TOKENS_H
#define USDHOUDINI_TOKENS_H

/// \file usdHoudini/tokens.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// 
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
// 
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


/// \class UsdHoudiniTokensType
///
/// \link UsdHoudiniTokens \endlink provides static, efficient
/// \link TfToken TfTokens\endlink for use in all public USD API.
///
/// These tokens are auto-generated from the module's schema, representing
/// property names, for when you need to fetch an attribute or relationship
/// directly by name, e.g. UsdPrim::GetAttribute(), in the most efficient
/// manner, and allow the compiler to verify that you spelled the name
/// correctly.
///
/// UsdHoudiniTokens also contains all of the \em allowedTokens values
/// declared for schema builtin attributes of 'token' scene description type.
/// Use UsdHoudiniTokens like so:
///
/// \code
///     gprim.GetMyTokenValuedAttr().Set(UsdHoudiniTokens->albedoscope);
/// \endcode
struct UsdHoudiniTokensType {
    USDHOUDINI_API UsdHoudiniTokensType();
    /// \brief "albedoscope"
    /// 
    /// UsdHoudiniHoudiniImageFilterList
    const TfToken albedoscope;
    /// \brief "all"
    /// 
    /// Possible value for UsdHoudiniHoudiniImageFilterList::GetAOVsAttr()
    const TfToken all;
    /// \brief "AOVlist"
    /// 
    /// UsdHoudiniHoudiniImageFilterList
    const TfToken aOVlist;
    /// \brief "AOVs"
    /// 
    /// UsdHoudiniHoudiniImageFilterList
    const TfToken aOVs;
    /// \brief "approximating"
    /// 
    /// Possible value for UsdHoudiniHoudiniHairDeformAPI::GetSmoothingMethodAttr()
    const TfToken approximating;
    /// \brief "as_solid"
    /// 
    /// Possible value for UsdHoudiniHoudiniHairDeformAPI::GetTetMeshTreatmentAttr()
    const TfToken as_solid;
    /// \brief "as_surface"
    /// 
    /// Possible value for UsdHoudiniHoudiniHairDeformAPI::GetTetMeshTreatmentAttr()
    const TfToken as_surface;
    /// \brief "barndoorbottom"
    /// 
    /// UsdHoudiniHoudiniLightBarnDoorAPI
    const TfToken barndoorbottom;
    /// \brief "barndoorbottomedge"
    /// 
    /// UsdHoudiniHoudiniLightBarnDoorAPI
    const TfToken barndoorbottomedge;
    /// \brief "barndoorleft"
    /// 
    /// UsdHoudiniHoudiniLightBarnDoorAPI
    const TfToken barndoorleft;
    /// \brief "barndoorleftedge"
    /// 
    /// UsdHoudiniHoudiniLightBarnDoorAPI
    const TfToken barndoorleftedge;
    /// \brief "barndoorright"
    /// 
    /// UsdHoudiniHoudiniLightBarnDoorAPI
    const TfToken barndoorright;
    /// \brief "barndoorrightedge"
    /// 
    /// UsdHoudiniHoudiniLightBarnDoorAPI
    const TfToken barndoorrightedge;
    /// \brief "barndoortop"
    /// 
    /// UsdHoudiniHoudiniLightBarnDoorAPI
    const TfToken barndoortop;
    /// \brief "barndoortopedge"
    /// 
    /// UsdHoudiniHoudiniLightBarnDoorAPI
    const TfToken barndoortopedge;
    /// \brief "bypass"
    /// 
    /// UsdHoudiniHoudiniImageFilter
    const TfToken bypass;
    /// \brief "character"
    /// 
    /// Property namespace prefix for the UsdHoudiniHoudiniApexCharacterBindingAPI schema.
    const TfToken character;
    /// \brief "character:__INSTANCE_NAME__:binding"
    /// 
    /// UsdHoudiniHoudiniApexCharacterBindingAPI
    const TfToken character_MultipleApplyTemplate_Binding;
    /// \brief "character:__INSTANCE_NAME__:rig"
    /// 
    /// UsdHoudiniHoudiniApexCharacterBindingAPI
    const TfToken character_MultipleApplyTemplate_Rig;
    /// \brief "color"
    /// 
    /// Fallback value for UsdHoudiniHoudiniImageFilterList::GetAOVsAttr()
    const TfToken color;
    /// \brief "depthscope"
    /// 
    /// UsdHoudiniHoudiniImageFilterList
    const TfToken depthscope;
    /// \brief "exponentialbump"
    /// 
    /// Possible value for UsdHoudiniHoudiniHairDeformAPI::GetKernelTypeAttr()
    const TfToken exponentialbump;
    /// \brief "houdini:apex:character:files"
    /// 
    /// UsdHoudiniHoudiniApexCharacterAPI
    const TfToken houdiniApexCharacterFiles;
    /// \brief "houdini:apex:character:rig"
    /// 
    /// UsdHoudiniHoudiniApexCharacterAPI
    const TfToken houdiniApexCharacterRig;
    /// \brief "houdini:apex:deform:joints"
    /// 
    /// UsdHoudiniHoudiniApexShapeDeformAPI
    const TfToken houdiniApexDeformJoints;
    /// \brief "houdini:apex:shape"
    /// 
    /// Property namespace prefix for the UsdHoudiniHoudiniApexShapeBindingAPI schema.
    const TfToken houdiniApexShape;
    /// \brief "houdini:apex:shape:__INSTANCE_NAME__:binding"
    /// 
    /// UsdHoudiniHoudiniApexShapeBindingAPI
    const TfToken houdiniApexShape_MultipleApplyTemplate_Binding;
    /// \brief "houdini:apex:shape:__INSTANCE_NAME__:input"
    /// 
    /// UsdHoudiniHoudiniApexShapeBindingAPI
    const TfToken houdiniApexShape_MultipleApplyTemplate_Input;
    /// \brief "houdini:apex:shape:__INSTANCE_NAME__:output"
    /// 
    /// UsdHoudiniHoudiniApexShapeBindingAPI
    const TfToken houdiniApexShape_MultipleApplyTemplate_Output;
    /// \brief "houdini:apex:xform"
    /// 
    /// Property namespace prefix for the UsdHoudiniHoudiniApexXformBindingAPI schema.
    const TfToken houdiniApexXform;
    /// \brief "houdini:apex:xform:__INSTANCE_NAME__:binding"
    /// 
    /// UsdHoudiniHoudiniApexXformBindingAPI
    const TfToken houdiniApexXform_MultipleApplyTemplate_Binding;
    /// \brief "houdini:apex:xform:__INSTANCE_NAME__:joint"
    /// 
    /// UsdHoudiniHoudiniApexXformBindingAPI
    const TfToken houdiniApexXform_MultipleApplyTemplate_Joint;
    /// \brief "houdini:apex:xform:__INSTANCE_NAME__:output"
    /// 
    /// UsdHoudiniHoudiniApexXformBindingAPI
    const TfToken houdiniApexXform_MultipleApplyTemplate_Output;
    /// \brief "houdini:backgroundimage"
    /// 
    /// UsdHoudiniHoudiniCameraPlateAPI
    const TfToken houdiniBackgroundimage;
    /// \brief "houdini:clippingRange"
    /// 
    /// UsdHoudiniHoudiniViewportLightAPI
    const TfToken houdiniClippingRange;
    /// \brief "houdini:containerInputDisplayColor"
    /// 
    /// UsdHoudiniHoudiniNodeGraphContainerAPI
    const TfToken houdiniContainerInputDisplayColor;
    /// \brief "houdini:containerInputPos"
    /// 
    /// UsdHoudiniHoudiniNodeGraphContainerAPI
    const TfToken houdiniContainerInputPos;
    /// \brief "houdini:containerOutputDisplayColor"
    /// 
    /// UsdHoudiniHoudiniNodeGraphContainerAPI
    const TfToken houdiniContainerOutputDisplayColor;
    /// \brief "houdini:containerOutputPos"
    /// 
    /// UsdHoudiniHoudiniNodeGraphContainerAPI
    const TfToken houdiniContainerOutputPos;
    /// \brief "houdini:containerWireStyle"
    /// 
    /// UsdHoudiniHoudiniNodeGraphContainerAPI
    const TfToken houdiniContainerWireStyle;
    /// \brief "houdini:editable"
    /// 
    /// UsdHoudiniHoudiniEditableAPI
    const TfToken houdiniEditable;
    /// \brief "houdini:foregroundimage"
    /// 
    /// UsdHoudiniHoudiniCameraPlateAPI
    const TfToken houdiniForegroundimage;
    /// \brief "houdini:guidescale"
    /// 
    /// UsdHoudiniHoudiniViewportGuideAPI
    const TfToken houdiniGuidescale;
    /// \brief "houdini:hairdeform:capturemaxpoints"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformCapturemaxpoints;
    /// \brief "houdini:hairdeform:captureminpoints"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformCaptureminpoints;
    /// \brief "houdini:hairdeform:captureradius"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformCaptureradius;
    /// \brief "houdini:hairdeform:deformerPrim"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformDeformerPrim;
    /// \brief "houdini:hairdeform:deformmethod"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformDeformmethod;
    /// \brief "houdini:hairdeform:gsilengthpenaltyscale"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformGsilengthpenaltyscale;
    /// \brief "houdini:hairdeform:gsimaxcandidates"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformGsimaxcandidates;
    /// \brief "houdini:hairdeform:gsimaxguides"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformGsimaxguides;
    /// \brief "houdini:hairdeform:gsiminguides"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformGsiminguides;
    /// \brief "houdini:hairdeform:gsinsamples"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformGsinsamples;
    /// \brief "houdini:hairdeform:gsisearchradius"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformGsisearchradius;
    /// \brief "houdini:hairdeform:gsisigmascale"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformGsisigmascale;
    /// \brief "houdini:hairdeform:gsiweightthreshold"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformGsiweightthreshold;
    /// \brief "houdini:hairdeform:guideInterpMeshPrim"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformGuideInterpMeshPrim;
    /// \brief "houdini:hairdeform:kerneltype"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformKerneltype;
    /// \brief "houdini:hairdeform:orientblend"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformOrientblend;
    /// \brief "houdini:hairdeform:preserveclumpsdamping"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveclumpsdamping;
    /// \brief "houdini:hairdeform:preserveclumpsenable"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveclumpsenable;
    /// \brief "houdini:hairdeform:preserveclumpsmaxconstraints"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveclumpsmaxconstraints;
    /// \brief "houdini:hairdeform:preserveclumpsmaxneighbors"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveclumpsmaxneighbors;
    /// \brief "houdini:hairdeform:preserveclumpsstiffness"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveclumpsstiffness;
    /// \brief "houdini:hairdeform:preserveshapeenable"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveshapeenable;
    /// \brief "houdini:hairdeform:preserveshapeiterations"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveshapeiterations;
    /// \brief "houdini:hairdeform:preserveshapekbend"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveshapekbend;
    /// \brief "houdini:hairdeform:preserveshapekstretch"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveshapekstretch;
    /// \brief "houdini:hairdeform:preserveshapelockroots"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveshapelockroots;
    /// \brief "houdini:hairdeform:preserveshaperefposstrength"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformPreserveshaperefposstrength;
    /// \brief "houdini:hairdeform:skinPrim"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformSkinPrim;
    /// \brief "houdini:hairdeform:smoothcapture"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformSmoothcapture;
    /// \brief "houdini:hairdeform:smoothcaptureradius"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformSmoothcaptureradius;
    /// \brief "houdini:hairdeform:smoothinglevel"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformSmoothinglevel;
    /// \brief "houdini:hairdeform:smoothingmethod"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformSmoothingmethod;
    /// \brief "houdini:hairdeform:tetmeshtreatment"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformTetmeshtreatment;
    /// \brief "houdini:hairdeform:useorientattrib"
    /// 
    /// UsdHoudiniHoudiniHairDeformAPI
    const TfToken houdiniHairdeformUseorientattrib;
    /// \brief "houdini:inviewermenu"
    /// 
    /// UsdHoudiniHoudiniViewportGuideAPI
    const TfToken houdiniInviewermenu;
    /// \brief "houdiniProcedural"
    /// 
    /// Property namespace prefix for the UsdHoudiniHoudiniProceduralAPI schema.
    const TfToken houdiniProcedural;
    /// \brief "houdiniProcedural:__INSTANCE_NAME__:houdini:active"
    /// 
    /// UsdHoudiniHoudiniProceduralAPI
    const TfToken houdiniProcedural_MultipleApplyTemplate_HoudiniActive;
    /// \brief "houdiniProcedural:__INSTANCE_NAME__:houdini:animated"
    /// 
    /// UsdHoudiniHoudiniProceduralAPI
    const TfToken houdiniProcedural_MultipleApplyTemplate_HoudiniAnimated;
    /// \brief "houdiniProcedural:__INSTANCE_NAME__:houdini:priority"
    /// 
    /// UsdHoudiniHoudiniProceduralAPI
    const TfToken houdiniProcedural_MultipleApplyTemplate_HoudiniPriority;
    /// \brief "houdiniProcedural:__INSTANCE_NAME__:houdini:procedural:args"
    /// 
    /// UsdHoudiniHoudiniProceduralAPI
    const TfToken houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralArgs;
    /// \brief "houdiniProcedural:__INSTANCE_NAME__:houdini:procedural:path"
    /// 
    /// UsdHoudiniHoudiniProceduralAPI
    const TfToken houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralPath;
    /// \brief "houdiniProcedural:__INSTANCE_NAME__:houdini:procedural:type"
    /// 
    /// UsdHoudiniHoudiniProceduralAPI
    const TfToken houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralType;
    /// \brief "houdini:selectable"
    /// 
    /// UsdHoudiniHoudiniSelectableAPI
    const TfToken houdiniSelectable;
    /// \brief "inheritAnimationLayers"
    /// 
    /// UsdHoudiniHoudiniApexScene
    const TfToken inheritAnimationLayers;
    /// \brief "inherited"
    /// 
    /// Fallback value for UsdHoudiniHoudiniNodeGraphContainerAPI::GetContainerWireStyleAttr()
    const TfToken inherited;
    /// \brief "interpolating"
    /// 
    /// Possible value for UsdHoudiniHoudiniHairDeformAPI::GetSmoothingMethodAttr()
    const TfToken interpolating;
    /// \brief "linear"
    /// 
    /// Possible value for UsdHoudiniHoudiniHairDeformAPI::GetKernelTypeAttr()
    const TfToken linear;
    /// \brief "list"
    /// 
    /// Possible value for UsdHoudiniHoudiniImageFilterList::GetAOVsAttr()
    const TfToken list;
    /// \brief "none"
    /// 
    /// Possible value for UsdHoudiniHoudiniHairDeformAPI::GetSmoothingMethodAttr(), Possible value for UsdHoudiniHoudiniHairDeformAPI::GetTetMeshTreatmentAttr(), Possible value for UsdHoudiniHoudiniImageFilterList::GetAOVsAttr()
    const TfToken none;
    /// \brief "normalscope"
    /// 
    /// UsdHoudiniHoudiniImageFilterList
    const TfToken normalscope;
    /// \brief "orderedFilters"
    /// 
    /// UsdHoudiniHoudiniImageFilterList
    const TfToken orderedFilters;
    /// \brief "pivot"
    /// 
    /// Op suffix for the standard scale-rotate pivot on a UsdHoudiniHoudiniXformCommonAPI-compatible prim. 
    const TfToken pivot;
    /// \brief "primvars:houdini:apex:deform:jointIndices"
    /// 
    /// UsdHoudiniHoudiniApexShapeDeformAPI
    const TfToken primvarsHoudiniApexDeformJointIndices;
    /// \brief "primvars:houdini:apex:deform:jointWeights"
    /// 
    /// UsdHoudiniHoudiniApexShapeDeformAPI
    const TfToken primvarsHoudiniApexDeformJointWeights;
    /// \brief "quadratic"
    /// 
    /// Possible value for UsdHoudiniHoudiniHairDeformAPI::GetKernelTypeAttr()
    const TfToken quadratic;
    /// \brief "rounded"
    /// 
    /// Possible value for UsdHoudiniHoudiniNodeGraphContainerAPI::GetContainerWireStyleAttr()
    const TfToken rounded;
    /// \brief "sceneFiles"
    /// 
    /// UsdHoudiniHoudiniApexScene
    const TfToken sceneFiles;
    /// \brief "shear"
    /// 
    /// Op suffix for the standard shear matrix on a UsdHoudiniHoudiniXformCommonAPI-compatible prim. 
    const TfToken shear;
    /// \brief "straight"
    /// 
    /// Possible value for UsdHoudiniHoudiniNodeGraphContainerAPI::GetContainerWireStyleAttr()
    const TfToken straight;
    /// \brief "truncatedgaussian"
    /// 
    /// Possible value for UsdHoudiniHoudiniHairDeformAPI::GetKernelTypeAttr()
    const TfToken truncatedgaussian;
    /// \brief "type"
    /// 
    /// UsdHoudiniHoudiniImageFilter
    const TfToken type;
    /// \brief "HoudiniApexCharacterAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniApexCharacterAPI
    const TfToken HoudiniApexCharacterAPI;
    /// \brief "HoudiniApexCharacterBindingAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniApexCharacterBindingAPI
    const TfToken HoudiniApexCharacterBindingAPI;
    /// \brief "HoudiniApexScene"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniApexScene
    const TfToken HoudiniApexScene;
    /// \brief "HoudiniApexShapeBindingAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniApexShapeBindingAPI
    const TfToken HoudiniApexShapeBindingAPI;
    /// \brief "HoudiniApexShapeDeformAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniApexShapeDeformAPI
    const TfToken HoudiniApexShapeDeformAPI;
    /// \brief "HoudiniApexXformBindingAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniApexXformBindingAPI
    const TfToken HoudiniApexXformBindingAPI;
    /// \brief "HoudiniCameraPlateAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniCameraPlateAPI
    const TfToken HoudiniCameraPlateAPI;
    /// \brief "HoudiniEditableAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniEditableAPI
    const TfToken HoudiniEditableAPI;
    /// \brief "HoudiniFieldAsset"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniFieldAsset
    const TfToken HoudiniFieldAsset;
    /// \brief "HoudiniHairDeformAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniHairDeformAPI
    const TfToken HoudiniHairDeformAPI;
    /// \brief "HoudiniImageFilter"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniImageFilter
    const TfToken HoudiniImageFilter;
    /// \brief "HoudiniImageFilterList"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniImageFilterList
    const TfToken HoudiniImageFilterList;
    /// \brief "HoudiniLayerInfo"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniLayerInfo
    const TfToken HoudiniLayerInfo;
    /// \brief "HoudiniLightBarnDoorAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniLightBarnDoorAPI
    const TfToken HoudiniLightBarnDoorAPI;
    /// \brief "HoudiniMetaCurves"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniMetaCurves
    const TfToken HoudiniMetaCurves;
    /// \brief "HoudiniNodeGraphContainerAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniNodeGraphContainerAPI
    const TfToken HoudiniNodeGraphContainerAPI;
    /// \brief "HoudiniProceduralAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniProceduralAPI
    const TfToken HoudiniProceduralAPI;
    /// \brief "HoudiniSelectableAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniSelectableAPI
    const TfToken HoudiniSelectableAPI;
    /// \brief "HoudiniViewportGuideAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniViewportGuideAPI
    const TfToken HoudiniViewportGuideAPI;
    /// \brief "HoudiniViewportLightAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniViewportLightAPI
    const TfToken HoudiniViewportLightAPI;
    /// \brief "HoudiniXformCommonAPI"
    /// 
    /// Schema identifer and family for UsdHoudiniHoudiniXformCommonAPI
    const TfToken HoudiniXformCommonAPI;
    /// A vector of all of the tokens listed above.
    const std::vector<TfToken> allTokens;
};

/// \var UsdHoudiniTokens
///
/// A global variable with static, efficient \link TfToken TfTokens\endlink
/// for use in all public USD API.  \sa UsdHoudiniTokensType
extern USDHOUDINI_API TfStaticData<UsdHoudiniTokensType> UsdHoudiniTokens;

PXR_NAMESPACE_CLOSE_SCOPE

#endif
