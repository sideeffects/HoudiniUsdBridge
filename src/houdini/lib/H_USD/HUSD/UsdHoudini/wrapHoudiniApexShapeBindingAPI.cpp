//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniApexShapeBindingAPI.h"
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
_CreateInputAttr(UsdHoudiniHoudiniApexShapeBindingAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateInputAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateOutputAttr(UsdHoudiniHoudiniApexShapeBindingAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateOutputAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}

static bool _WrapIsHoudiniApexShapeBindingAPIPath(const SdfPath &path) {
    TfToken collectionName;
    return UsdHoudiniHoudiniApexShapeBindingAPI::IsHoudiniApexShapeBindingAPIPath(
        path, &collectionName);
}

static std::string
_Repr(const UsdHoudiniHoudiniApexShapeBindingAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    std::string instanceName = TfPyRepr(self.GetName());
    return TfStringPrintf(
        "UsdHoudini.HoudiniApexShapeBindingAPI(%s, '%s')",
        primRepr.c_str(), instanceName.c_str());
}

struct UsdHoudiniHoudiniApexShapeBindingAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdHoudiniHoudiniApexShapeBindingAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdHoudiniHoudiniApexShapeBindingAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim, const TfToken& name)
{
    std::string whyNot;
    bool result = UsdHoudiniHoudiniApexShapeBindingAPI::CanApply(prim, name, &whyNot);
    return UsdHoudiniHoudiniApexShapeBindingAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdHoudiniHoudiniApexShapeBindingAPI()
{
    typedef UsdHoudiniHoudiniApexShapeBindingAPI This;

    UsdHoudiniHoudiniApexShapeBindingAPI_CanApplyResult::Wrap<UsdHoudiniHoudiniApexShapeBindingAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("HoudiniApexShapeBindingAPI");

    cls
        .def(init<UsdPrim, TfToken>((arg("prim"), arg("name"))))
        .def(init<UsdSchemaBase const&, TfToken>((arg("schemaObj"), arg("name"))))
        .def(TfTypePythonClass())

        .def("Get",
            (UsdHoudiniHoudiniApexShapeBindingAPI(*)(const UsdStagePtr &stage, 
                                       const SdfPath &path))
               &This::Get,
            (arg("stage"), arg("path")))
        .def("Get",
            (UsdHoudiniHoudiniApexShapeBindingAPI(*)(const UsdPrim &prim,
                                       const TfToken &name))
               &This::Get,
            (arg("prim"), arg("name")))
        .staticmethod("Get")

        .def("GetAll",
            (std::vector<UsdHoudiniHoudiniApexShapeBindingAPI>(*)(const UsdPrim &prim))
                &This::GetAll,
            arg("prim"),
            return_value_policy<TfPySequenceToList>())
        .staticmethod("GetAll")

        .def("CanApply", &_WrapCanApply, (arg("prim"), arg("name")))
        .staticmethod("CanApply")

        .def("Apply", &This::Apply, (arg("prim"), arg("name")))
        .staticmethod("Apply")

        .def("GetSchemaAttributeNames",
             (const TfTokenVector &(*)(bool))&This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .def("GetSchemaAttributeNames",
             (TfTokenVector(*)(bool, const TfToken &))
                &This::GetSchemaAttributeNames,
             arg("includeInherited"),
             arg("instanceName"),
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetInputAttr",
             &This::GetInputAttr)
        .def("CreateInputAttr",
             &_CreateInputAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetOutputAttr",
             &This::GetOutputAttr)
        .def("CreateOutputAttr",
             &_CreateOutputAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        
        .def("GetBindingRel",
             &This::GetBindingRel)
        .def("CreateBindingRel",
             &This::CreateBindingRel)
        .def("IsHoudiniApexShapeBindingAPIPath", _WrapIsHoudiniApexShapeBindingAPIPath)
            .staticmethod("IsHoudiniApexShapeBindingAPIPath")
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
