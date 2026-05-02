//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniNodeGraphContainerAPI.h"
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
_CreateContainerInputPosAttr(UsdHoudiniHoudiniNodeGraphContainerAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateContainerInputPosAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float2), writeSparsely);
}
        
static UsdAttribute
_CreateContainerOutputPosAttr(UsdHoudiniHoudiniNodeGraphContainerAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateContainerOutputPosAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float2), writeSparsely);
}
        
static UsdAttribute
_CreateContainerInputDisplayColorAttr(UsdHoudiniHoudiniNodeGraphContainerAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateContainerInputDisplayColorAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Color3f), writeSparsely);
}
        
static UsdAttribute
_CreateContainerOutputDisplayColorAttr(UsdHoudiniHoudiniNodeGraphContainerAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateContainerOutputDisplayColorAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Color3f), writeSparsely);
}
        
static UsdAttribute
_CreateContainerWireStyleAttr(UsdHoudiniHoudiniNodeGraphContainerAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateContainerWireStyleAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Token), writeSparsely);
}

static std::string
_Repr(const UsdHoudiniHoudiniNodeGraphContainerAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdHoudini.HoudiniNodeGraphContainerAPI(%s)",
        primRepr.c_str());
}

struct UsdHoudiniHoudiniNodeGraphContainerAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdHoudiniHoudiniNodeGraphContainerAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdHoudiniHoudiniNodeGraphContainerAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim)
{
    std::string whyNot;
    bool result = UsdHoudiniHoudiniNodeGraphContainerAPI::CanApply(prim, &whyNot);
    return UsdHoudiniHoudiniNodeGraphContainerAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdHoudiniHoudiniNodeGraphContainerAPI()
{
    typedef UsdHoudiniHoudiniNodeGraphContainerAPI This;

    UsdHoudiniHoudiniNodeGraphContainerAPI_CanApplyResult::Wrap<UsdHoudiniHoudiniNodeGraphContainerAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("HoudiniNodeGraphContainerAPI");

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

        
        .def("GetContainerInputPosAttr",
             &This::GetContainerInputPosAttr)
        .def("CreateContainerInputPosAttr",
             &_CreateContainerInputPosAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetContainerOutputPosAttr",
             &This::GetContainerOutputPosAttr)
        .def("CreateContainerOutputPosAttr",
             &_CreateContainerOutputPosAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetContainerInputDisplayColorAttr",
             &This::GetContainerInputDisplayColorAttr)
        .def("CreateContainerInputDisplayColorAttr",
             &_CreateContainerInputDisplayColorAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetContainerOutputDisplayColorAttr",
             &This::GetContainerOutputDisplayColorAttr)
        .def("CreateContainerOutputDisplayColorAttr",
             &_CreateContainerOutputDisplayColorAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetContainerWireStyleAttr",
             &This::GetContainerWireStyleAttr)
        .def("CreateContainerWireStyleAttr",
             &_CreateContainerWireStyleAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

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
