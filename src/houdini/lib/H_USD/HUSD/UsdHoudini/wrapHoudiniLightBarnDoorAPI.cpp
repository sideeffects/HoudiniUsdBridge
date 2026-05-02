//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniLightBarnDoorAPI.h"
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
_CreateBarndoorleftAttr(UsdHoudiniHoudiniLightBarnDoorAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBarndoorleftAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateBarndoorleftedgeAttr(UsdHoudiniHoudiniLightBarnDoorAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBarndoorleftedgeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateBarndoorrightAttr(UsdHoudiniHoudiniLightBarnDoorAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBarndoorrightAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateBarndoorrightedgeAttr(UsdHoudiniHoudiniLightBarnDoorAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBarndoorrightedgeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateBarndoortopAttr(UsdHoudiniHoudiniLightBarnDoorAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBarndoortopAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateBarndoortopedgeAttr(UsdHoudiniHoudiniLightBarnDoorAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBarndoortopedgeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateBarndoorbottomAttr(UsdHoudiniHoudiniLightBarnDoorAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBarndoorbottomAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateBarndoorbottomedgeAttr(UsdHoudiniHoudiniLightBarnDoorAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBarndoorbottomedgeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}

static std::string
_Repr(const UsdHoudiniHoudiniLightBarnDoorAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdHoudini.HoudiniLightBarnDoorAPI(%s)",
        primRepr.c_str());
}

struct UsdHoudiniHoudiniLightBarnDoorAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdHoudiniHoudiniLightBarnDoorAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdHoudiniHoudiniLightBarnDoorAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim)
{
    std::string whyNot;
    bool result = UsdHoudiniHoudiniLightBarnDoorAPI::CanApply(prim, &whyNot);
    return UsdHoudiniHoudiniLightBarnDoorAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdHoudiniHoudiniLightBarnDoorAPI()
{
    typedef UsdHoudiniHoudiniLightBarnDoorAPI This;

    UsdHoudiniHoudiniLightBarnDoorAPI_CanApplyResult::Wrap<UsdHoudiniHoudiniLightBarnDoorAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("HoudiniLightBarnDoorAPI");

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

        
        .def("GetBarndoorleftAttr",
             &This::GetBarndoorleftAttr)
        .def("CreateBarndoorleftAttr",
             &_CreateBarndoorleftAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetBarndoorleftedgeAttr",
             &This::GetBarndoorleftedgeAttr)
        .def("CreateBarndoorleftedgeAttr",
             &_CreateBarndoorleftedgeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetBarndoorrightAttr",
             &This::GetBarndoorrightAttr)
        .def("CreateBarndoorrightAttr",
             &_CreateBarndoorrightAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetBarndoorrightedgeAttr",
             &This::GetBarndoorrightedgeAttr)
        .def("CreateBarndoorrightedgeAttr",
             &_CreateBarndoorrightedgeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetBarndoortopAttr",
             &This::GetBarndoortopAttr)
        .def("CreateBarndoortopAttr",
             &_CreateBarndoortopAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetBarndoortopedgeAttr",
             &This::GetBarndoortopedgeAttr)
        .def("CreateBarndoortopedgeAttr",
             &_CreateBarndoortopedgeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetBarndoorbottomAttr",
             &This::GetBarndoorbottomAttr)
        .def("CreateBarndoorbottomAttr",
             &_CreateBarndoorbottomAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetBarndoorbottomedgeAttr",
             &This::GetBarndoorbottomedgeAttr)
        .def("CreateBarndoorbottomedgeAttr",
             &_CreateBarndoorbottomedgeAttr,
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
