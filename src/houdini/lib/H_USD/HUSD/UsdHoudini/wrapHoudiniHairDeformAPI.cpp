//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniHairDeformAPI.h"
#include "pxr/usd/usd/schemaBase.h"

#include "pxr/usd/sdf/primSpec.h"

#include "pxr/usd/usd/pyConversions.h"
#include "pxr/base/tf/pyAnnotatedBoolResult.h"
#include "pxr/base/tf/pyContainerConversions.h"
#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/base/tf/pyUtils.h"
#include "pxr/base/tf/wrapTypeHelpers.h"

#include "pxr/external/boost/python.hpp"

#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

namespace {

#define WRAP_CUSTOM                                                     \
    template <class Cls> static void _CustomWrapCode(Cls &_class)

// fwd decl.
WRAP_CUSTOM;

        
static UsdAttribute
_CreatePreserveShapeEnableAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveShapeEnableAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Bool), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveShapeIterationsAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveShapeIterationsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveShapeLockRootsAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveShapeLockRootsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Bool), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveShapeKStretchAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveShapeKStretchAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveShapeKBendAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveShapeKBendAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveShapeRefPosStrengthAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveShapeRefPosStrengthAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveClumpsStiffnessAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveClumpsStiffnessAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveClumpsDampingAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveClumpsDampingAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveClumpsEnableAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveClumpsEnableAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Bool), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveClumpsMaxNeighborsAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveClumpsMaxNeighborsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreatePreserveClumpsMaxConstraintsAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePreserveClumpsMaxConstraintsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreateCaptureRadiusAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCaptureRadiusAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateCaptureMaxPointsAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCaptureMaxPointsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreateCaptureMinPointsAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCaptureMinPointsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreateDeformMethodAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateDeformMethodAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateSmoothCaptureAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSmoothCaptureAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Bool), writeSparsely);
}
        
static UsdAttribute
_CreateSmoothCaptureRadiusAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSmoothCaptureRadiusAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateKernelTypeAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateKernelTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateSmoothingMethodAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSmoothingMethodAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateSmoothingLevelAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSmoothingLevelAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreateTetMeshTreatmentAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateTetMeshTreatmentAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateUseOrientAttribAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateUseOrientAttribAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Bool), writeSparsely);
}
        
static UsdAttribute
_CreateOrientBlendAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateOrientBlendAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateGsiMaxCandidatesAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateGsiMaxCandidatesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreateGsiSearchRadiusAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateGsiSearchRadiusAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateGsiNSamplesAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateGsiNSamplesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreateGsiMinGuidesAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateGsiMinGuidesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreateGsiMaxGuidesAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateGsiMaxGuidesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreateGsiSigmaScaleAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateGsiSigmaScaleAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateGsiWeightThresholdAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateGsiWeightThresholdAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateGsiLengthPenaltyScaleAttr(UsdHoudiniHoudiniHairDeformAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateGsiLengthPenaltyScaleAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}

static std::string
_Repr(const UsdHoudiniHoudiniHairDeformAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdHoudini.HoudiniHairDeformAPI(%s)",
        primRepr.c_str());
}

struct UsdHoudiniHoudiniHairDeformAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdHoudiniHoudiniHairDeformAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdHoudiniHoudiniHairDeformAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim)
{
    std::string whyNot;
    bool result = UsdHoudiniHoudiniHairDeformAPI::CanApply(prim, &whyNot);
    return UsdHoudiniHoudiniHairDeformAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdHoudiniHoudiniHairDeformAPI()
{
    typedef UsdHoudiniHoudiniHairDeformAPI This;

    UsdHoudiniHoudiniHairDeformAPI_CanApplyResult::Wrap<UsdHoudiniHoudiniHairDeformAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("HoudiniHairDeformAPI");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("CanApply", &_WrapCanApply, (arg("prim")))
        .staticmethod("CanApply")

        .def("Apply", &This::Apply, (arg("prim")))
        .staticmethod("Apply")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetPreserveShapeEnableAttr",
             &This::GetPreserveShapeEnableAttr)
        .def("CreatePreserveShapeEnableAttr",
             &_CreatePreserveShapeEnableAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveShapeIterationsAttr",
             &This::GetPreserveShapeIterationsAttr)
        .def("CreatePreserveShapeIterationsAttr",
             &_CreatePreserveShapeIterationsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveShapeLockRootsAttr",
             &This::GetPreserveShapeLockRootsAttr)
        .def("CreatePreserveShapeLockRootsAttr",
             &_CreatePreserveShapeLockRootsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveShapeKStretchAttr",
             &This::GetPreserveShapeKStretchAttr)
        .def("CreatePreserveShapeKStretchAttr",
             &_CreatePreserveShapeKStretchAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveShapeKBendAttr",
             &This::GetPreserveShapeKBendAttr)
        .def("CreatePreserveShapeKBendAttr",
             &_CreatePreserveShapeKBendAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveShapeRefPosStrengthAttr",
             &This::GetPreserveShapeRefPosStrengthAttr)
        .def("CreatePreserveShapeRefPosStrengthAttr",
             &_CreatePreserveShapeRefPosStrengthAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveClumpsStiffnessAttr",
             &This::GetPreserveClumpsStiffnessAttr)
        .def("CreatePreserveClumpsStiffnessAttr",
             &_CreatePreserveClumpsStiffnessAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveClumpsDampingAttr",
             &This::GetPreserveClumpsDampingAttr)
        .def("CreatePreserveClumpsDampingAttr",
             &_CreatePreserveClumpsDampingAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveClumpsEnableAttr",
             &This::GetPreserveClumpsEnableAttr)
        .def("CreatePreserveClumpsEnableAttr",
             &_CreatePreserveClumpsEnableAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveClumpsMaxNeighborsAttr",
             &This::GetPreserveClumpsMaxNeighborsAttr)
        .def("CreatePreserveClumpsMaxNeighborsAttr",
             &_CreatePreserveClumpsMaxNeighborsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetPreserveClumpsMaxConstraintsAttr",
             &This::GetPreserveClumpsMaxConstraintsAttr)
        .def("CreatePreserveClumpsMaxConstraintsAttr",
             &_CreatePreserveClumpsMaxConstraintsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCaptureRadiusAttr",
             &This::GetCaptureRadiusAttr)
        .def("CreateCaptureRadiusAttr",
             &_CreateCaptureRadiusAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCaptureMaxPointsAttr",
             &This::GetCaptureMaxPointsAttr)
        .def("CreateCaptureMaxPointsAttr",
             &_CreateCaptureMaxPointsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCaptureMinPointsAttr",
             &This::GetCaptureMinPointsAttr)
        .def("CreateCaptureMinPointsAttr",
             &_CreateCaptureMinPointsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetDeformMethodAttr",
             &This::GetDeformMethodAttr)
        .def("CreateDeformMethodAttr",
             &_CreateDeformMethodAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSmoothCaptureAttr",
             &This::GetSmoothCaptureAttr)
        .def("CreateSmoothCaptureAttr",
             &_CreateSmoothCaptureAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSmoothCaptureRadiusAttr",
             &This::GetSmoothCaptureRadiusAttr)
        .def("CreateSmoothCaptureRadiusAttr",
             &_CreateSmoothCaptureRadiusAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetKernelTypeAttr",
             &This::GetKernelTypeAttr)
        .def("CreateKernelTypeAttr",
             &_CreateKernelTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSmoothingMethodAttr",
             &This::GetSmoothingMethodAttr)
        .def("CreateSmoothingMethodAttr",
             &_CreateSmoothingMethodAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSmoothingLevelAttr",
             &This::GetSmoothingLevelAttr)
        .def("CreateSmoothingLevelAttr",
             &_CreateSmoothingLevelAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetTetMeshTreatmentAttr",
             &This::GetTetMeshTreatmentAttr)
        .def("CreateTetMeshTreatmentAttr",
             &_CreateTetMeshTreatmentAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetUseOrientAttribAttr",
             &This::GetUseOrientAttribAttr)
        .def("CreateUseOrientAttribAttr",
             &_CreateUseOrientAttribAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetOrientBlendAttr",
             &This::GetOrientBlendAttr)
        .def("CreateOrientBlendAttr",
             &_CreateOrientBlendAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetGsiMaxCandidatesAttr",
             &This::GetGsiMaxCandidatesAttr)
        .def("CreateGsiMaxCandidatesAttr",
             &_CreateGsiMaxCandidatesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetGsiSearchRadiusAttr",
             &This::GetGsiSearchRadiusAttr)
        .def("CreateGsiSearchRadiusAttr",
             &_CreateGsiSearchRadiusAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetGsiNSamplesAttr",
             &This::GetGsiNSamplesAttr)
        .def("CreateGsiNSamplesAttr",
             &_CreateGsiNSamplesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetGsiMinGuidesAttr",
             &This::GetGsiMinGuidesAttr)
        .def("CreateGsiMinGuidesAttr",
             &_CreateGsiMinGuidesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetGsiMaxGuidesAttr",
             &This::GetGsiMaxGuidesAttr)
        .def("CreateGsiMaxGuidesAttr",
             &_CreateGsiMaxGuidesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetGsiSigmaScaleAttr",
             &This::GetGsiSigmaScaleAttr)
        .def("CreateGsiSigmaScaleAttr",
             &_CreateGsiSigmaScaleAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetGsiWeightThresholdAttr",
             &This::GetGsiWeightThresholdAttr)
        .def("CreateGsiWeightThresholdAttr",
             &_CreateGsiWeightThresholdAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetGsiLengthPenaltyScaleAttr",
             &This::GetGsiLengthPenaltyScaleAttr)
        .def("CreateGsiLengthPenaltyScaleAttr",
             &_CreateGsiLengthPenaltyScaleAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        
        .def("GetSkinPrimRel",
             &This::GetSkinPrimRel)
        .def("CreateSkinPrimRel",
             &This::CreateSkinPrimRel)
        
        .def("GetDeformerPrimRel",
             &This::GetDeformerPrimRel)
        .def("CreateDeformerPrimRel",
             &This::CreateDeformerPrimRel)
        
        .def("GetGuideInterpMeshPrimRel",
             &This::GetGuideInterpMeshPrimRel)
        .def("CreateGuideInterpMeshPrimRel",
             &This::CreateGuideInterpMeshPrimRel)
        .def("__repr__", ::_Repr)
    ;

    _CustomWrapCode(cls);
}

// ===================================================================== //
// Feel free to add custom code below this line, it will be preserved by 
// the code generator.  The entry point for your custom code should look
// minimally like the following:
//
// WRAP_CUSTOM {
//     _class
//         .def("MyCustomMethod", ...)
//     ;
// }
//
// Of course any other ancillary or support code may be provided.
// 
// Just remember to wrap code in the appropriate delimiters:
// 'namespace {', '}'.
//
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

namespace {

WRAP_CUSTOM {
}

}
