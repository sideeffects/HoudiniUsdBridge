//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniProceduralAPI.h"
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
_CreateHoudiniProceduralTypeAttr(UsdHoudiniHoudiniProceduralAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateHoudiniProceduralTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateHoudiniProceduralPathAttr(UsdHoudiniHoudiniProceduralAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateHoudiniProceduralPathAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Asset), writeSparsely);
}
        
static UsdAttribute
_CreateHoudiniProceduralArgsAttr(UsdHoudiniHoudiniProceduralAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateHoudiniProceduralArgsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateHoudiniActiveAttr(UsdHoudiniHoudiniProceduralAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateHoudiniActiveAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Bool), writeSparsely);
}
        
static UsdAttribute
_CreateHoudiniPriorityAttr(UsdHoudiniHoudiniProceduralAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateHoudiniPriorityAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int), writeSparsely);
}
        
static UsdAttribute
_CreateHoudiniAnimatedAttr(UsdHoudiniHoudiniProceduralAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateHoudiniAnimatedAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Bool), writeSparsely);
}

static bool _WrapIsHoudiniProceduralAPIPath(const SdfPath &path) {
    TfToken collectionName;
    return UsdHoudiniHoudiniProceduralAPI::IsHoudiniProceduralAPIPath(
        path, &collectionName);
}

static std::string
_Repr(const UsdHoudiniHoudiniProceduralAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    std::string instanceName = TfPyRepr(self.GetName());
    return TfStringPrintf(
        "UsdHoudini.HoudiniProceduralAPI(%s, '%s')",
        primRepr.c_str(), instanceName.c_str());
}

struct UsdHoudiniHoudiniProceduralAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdHoudiniHoudiniProceduralAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdHoudiniHoudiniProceduralAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim, const TfToken& name)
{
    std::string whyNot;
    bool result = UsdHoudiniHoudiniProceduralAPI::CanApply(prim, name, &whyNot);
    return UsdHoudiniHoudiniProceduralAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdHoudiniHoudiniProceduralAPI()
{
    typedef UsdHoudiniHoudiniProceduralAPI This;

    UsdHoudiniHoudiniProceduralAPI_CanApplyResult::Wrap<UsdHoudiniHoudiniProceduralAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("HoudiniProceduralAPI");

    cls
        .def(init<UsdPrim, TfToken>((arg("prim"), arg("name"))))
        .def(init<UsdSchemaBase const&, TfToken>((arg("schemaObj"), arg("name"))))
        .def(TfTypePythonClass())

        .def("Get",
            (UsdHoudiniHoudiniProceduralAPI(*)(const UsdStagePtr &stage, 
                                       const SdfPath &path))
               &This::Get,
            (arg("stage"), arg("path")))
        .def("Get",
            (UsdHoudiniHoudiniProceduralAPI(*)(const UsdPrim &prim,
                                       const TfToken &name))
               &This::Get,
            (arg("prim"), arg("name")))
        .staticmethod("Get")

        .def("GetAll",
            (std::vector<UsdHoudiniHoudiniProceduralAPI>(*)(const UsdPrim &prim))
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

        
        .def("GetHoudiniProceduralTypeAttr",
             &This::GetHoudiniProceduralTypeAttr)
        .def("CreateHoudiniProceduralTypeAttr",
             &_CreateHoudiniProceduralTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetHoudiniProceduralPathAttr",
             &This::GetHoudiniProceduralPathAttr)
        .def("CreateHoudiniProceduralPathAttr",
             &_CreateHoudiniProceduralPathAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetHoudiniProceduralArgsAttr",
             &This::GetHoudiniProceduralArgsAttr)
        .def("CreateHoudiniProceduralArgsAttr",
             &_CreateHoudiniProceduralArgsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetHoudiniActiveAttr",
             &This::GetHoudiniActiveAttr)
        .def("CreateHoudiniActiveAttr",
             &_CreateHoudiniActiveAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetHoudiniPriorityAttr",
             &This::GetHoudiniPriorityAttr)
        .def("CreateHoudiniPriorityAttr",
             &_CreateHoudiniPriorityAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetHoudiniAnimatedAttr",
             &This::GetHoudiniAnimatedAttr)
        .def("CreateHoudiniAnimatedAttr",
             &_CreateHoudiniAnimatedAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("IsHoudiniProceduralAPIPath", _WrapIsHoudiniProceduralAPIPath)
            .staticmethod("IsHoudiniProceduralAPIPath")
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
