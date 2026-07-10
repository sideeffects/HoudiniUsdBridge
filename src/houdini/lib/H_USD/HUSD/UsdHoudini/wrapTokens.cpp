//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// GENERATED FILE.  DO NOT EDIT.
#include "pxr/external/boost/python/class.hpp"
#include "./tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

#define _ADD_TOKEN(cls, name) \
    cls.add_static_property(#name, +[]() { return UsdHoudiniTokens->name.GetString(); });

void wrapUsdHoudiniTokens()
{
    pxr_boost::python::class_<UsdHoudiniTokensType, pxr_boost::python::noncopyable>
        cls("Tokens", pxr_boost::python::no_init);
    _ADD_TOKEN(cls, albedoscope);
    _ADD_TOKEN(cls, all);
    _ADD_TOKEN(cls, aOVlist);
    _ADD_TOKEN(cls, aOVs);
    _ADD_TOKEN(cls, approximating);
    _ADD_TOKEN(cls, as_solid);
    _ADD_TOKEN(cls, as_surface);
    _ADD_TOKEN(cls, barndoorbottom);
    _ADD_TOKEN(cls, barndoorbottomedge);
    _ADD_TOKEN(cls, barndoorleft);
    _ADD_TOKEN(cls, barndoorleftedge);
    _ADD_TOKEN(cls, barndoorright);
    _ADD_TOKEN(cls, barndoorrightedge);
    _ADD_TOKEN(cls, barndoortop);
    _ADD_TOKEN(cls, barndoortopedge);
    _ADD_TOKEN(cls, bypass);
    _ADD_TOKEN(cls, character);
    _ADD_TOKEN(cls, character_MultipleApplyTemplate_Binding);
    _ADD_TOKEN(cls, character_MultipleApplyTemplate_Rig);
    _ADD_TOKEN(cls, color);
    _ADD_TOKEN(cls, depthscope);
    _ADD_TOKEN(cls, exponentialbump);
    _ADD_TOKEN(cls, houdiniApexCharacterFiles);
    _ADD_TOKEN(cls, houdiniApexCharacterRig);
    _ADD_TOKEN(cls, houdiniApexDeformJoints);
    _ADD_TOKEN(cls, houdiniApexShape);
    _ADD_TOKEN(cls, houdiniApexShape_MultipleApplyTemplate_Binding);
    _ADD_TOKEN(cls, houdiniApexShape_MultipleApplyTemplate_Input);
    _ADD_TOKEN(cls, houdiniApexShape_MultipleApplyTemplate_Output);
    _ADD_TOKEN(cls, houdiniApexXform);
    _ADD_TOKEN(cls, houdiniApexXform_MultipleApplyTemplate_Binding);
    _ADD_TOKEN(cls, houdiniApexXform_MultipleApplyTemplate_Joint);
    _ADD_TOKEN(cls, houdiniApexXform_MultipleApplyTemplate_Output);
    _ADD_TOKEN(cls, houdiniBackgroundimage);
    _ADD_TOKEN(cls, houdiniCanvasdistance);
    _ADD_TOKEN(cls, houdiniClippingRange);
    _ADD_TOKEN(cls, houdiniContainerInputDisplayColor);
    _ADD_TOKEN(cls, houdiniContainerInputPos);
    _ADD_TOKEN(cls, houdiniContainerOutputDisplayColor);
    _ADD_TOKEN(cls, houdiniContainerOutputPos);
    _ADD_TOKEN(cls, houdiniContainerWireStyle);
    _ADD_TOKEN(cls, houdiniEditable);
    _ADD_TOKEN(cls, houdiniForegroundimage);
    _ADD_TOKEN(cls, houdiniGuidescale);
    _ADD_TOKEN(cls, houdiniHairdeformCapturemaxpoints);
    _ADD_TOKEN(cls, houdiniHairdeformCaptureminpoints);
    _ADD_TOKEN(cls, houdiniHairdeformCaptureradius);
    _ADD_TOKEN(cls, houdiniHairdeformDeformerPrim);
    _ADD_TOKEN(cls, houdiniHairdeformDeformmethod);
    _ADD_TOKEN(cls, houdiniHairdeformGsilengthpenaltyscale);
    _ADD_TOKEN(cls, houdiniHairdeformGsimaxcandidates);
    _ADD_TOKEN(cls, houdiniHairdeformGsimaxguides);
    _ADD_TOKEN(cls, houdiniHairdeformGsiminguides);
    _ADD_TOKEN(cls, houdiniHairdeformGsinsamples);
    _ADD_TOKEN(cls, houdiniHairdeformGsisearchradius);
    _ADD_TOKEN(cls, houdiniHairdeformGsisigmascale);
    _ADD_TOKEN(cls, houdiniHairdeformGsiweightthreshold);
    _ADD_TOKEN(cls, houdiniHairdeformGuideInterpMeshPrim);
    _ADD_TOKEN(cls, houdiniHairdeformKerneltype);
    _ADD_TOKEN(cls, houdiniHairdeformOrientblend);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveclumpsdamping);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveclumpsenable);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveclumpsmaxconstraints);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveclumpsmaxneighbors);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveclumpsstiffness);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveshapeenable);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveshapeiterations);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveshapekbend);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveshapekstretch);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveshapelockroots);
    _ADD_TOKEN(cls, houdiniHairdeformPreserveshaperefposstrength);
    _ADD_TOKEN(cls, houdiniHairdeformSkinPrim);
    _ADD_TOKEN(cls, houdiniHairdeformSmoothcapture);
    _ADD_TOKEN(cls, houdiniHairdeformSmoothcaptureradius);
    _ADD_TOKEN(cls, houdiniHairdeformSmoothinglevel);
    _ADD_TOKEN(cls, houdiniHairdeformSmoothingmethod);
    _ADD_TOKEN(cls, houdiniHairdeformTetmeshtreatment);
    _ADD_TOKEN(cls, houdiniHairdeformUseorientattrib);
    _ADD_TOKEN(cls, houdiniInviewermenu);
    _ADD_TOKEN(cls, houdiniProcedural);
    _ADD_TOKEN(cls, houdiniProcedural_MultipleApplyTemplate_HoudiniActive);
    _ADD_TOKEN(cls, houdiniProcedural_MultipleApplyTemplate_HoudiniAnimated);
    _ADD_TOKEN(cls, houdiniProcedural_MultipleApplyTemplate_HoudiniPriority);
    _ADD_TOKEN(cls, houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralArgs);
    _ADD_TOKEN(cls, houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralPath);
    _ADD_TOKEN(cls, houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralType);
    _ADD_TOKEN(cls, houdiniSelectable);
    _ADD_TOKEN(cls, inheritAnimationLayers);
    _ADD_TOKEN(cls, inherited);
    _ADD_TOKEN(cls, interpolating);
    _ADD_TOKEN(cls, linear);
    _ADD_TOKEN(cls, list);
    _ADD_TOKEN(cls, none);
    _ADD_TOKEN(cls, normalscope);
    _ADD_TOKEN(cls, orderedFilters);
    _ADD_TOKEN(cls, pivot);
    _ADD_TOKEN(cls, primvarsHoudiniApexDeformJointIndices);
    _ADD_TOKEN(cls, primvarsHoudiniApexDeformJointWeights);
    _ADD_TOKEN(cls, quadratic);
    _ADD_TOKEN(cls, rounded);
    _ADD_TOKEN(cls, sceneFiles);
    _ADD_TOKEN(cls, shear);
    _ADD_TOKEN(cls, straight);
    _ADD_TOKEN(cls, truncatedgaussian);
    _ADD_TOKEN(cls, type);
    _ADD_TOKEN(cls, HoudiniApexCharacterAPI);
    _ADD_TOKEN(cls, HoudiniApexCharacterBindingAPI);
    _ADD_TOKEN(cls, HoudiniApexScene);
    _ADD_TOKEN(cls, HoudiniApexShapeBindingAPI);
    _ADD_TOKEN(cls, HoudiniApexShapeDeformAPI);
    _ADD_TOKEN(cls, HoudiniApexXformBindingAPI);
    _ADD_TOKEN(cls, HoudiniCameraPlateAPI);
    _ADD_TOKEN(cls, HoudiniCanvasCameraAPI);
    _ADD_TOKEN(cls, HoudiniEditableAPI);
    _ADD_TOKEN(cls, HoudiniFieldAsset);
    _ADD_TOKEN(cls, HoudiniHairDeformAPI);
    _ADD_TOKEN(cls, HoudiniImageFilter);
    _ADD_TOKEN(cls, HoudiniImageFilterList);
    _ADD_TOKEN(cls, HoudiniLayerInfo);
    _ADD_TOKEN(cls, HoudiniLightBarnDoorAPI);
    _ADD_TOKEN(cls, HoudiniMetaCurves);
    _ADD_TOKEN(cls, HoudiniNodeGraphContainerAPI);
    _ADD_TOKEN(cls, HoudiniProceduralAPI);
    _ADD_TOKEN(cls, HoudiniSelectableAPI);
    _ADD_TOKEN(cls, HoudiniViewportGuideAPI);
    _ADD_TOKEN(cls, HoudiniViewportLightAPI);
    _ADD_TOKEN(cls, HoudiniXformCommonAPI);
}
