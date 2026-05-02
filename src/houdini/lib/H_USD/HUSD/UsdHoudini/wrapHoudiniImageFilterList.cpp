//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniImageFilterList.h"
#include "pxr/usd/usd/schemaBase.h"

#include "pxr/usd/sdf/primSpec.h"

#include "pxr/usd/usd/pyConversions.h"
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
_CreateAOVsAttr(UsdHoudiniHoudiniImageFilterList &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateAOVsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Token), writeSparsely);
}
        
static UsdAttribute
_CreateAOVlistAttr(UsdHoudiniHoudiniImageFilterList &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateAOVlistAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->StringArray), writeSparsely);
}
        
static UsdAttribute
_CreateNormalscopeAttr(UsdHoudiniHoudiniImageFilterList &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateNormalscopeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateDepthscopeAttr(UsdHoudiniHoudiniImageFilterList &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateDepthscopeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateAlbedoscopeAttr(UsdHoudiniHoudiniImageFilterList &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateAlbedoscopeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}

static std::string
_Repr(const UsdHoudiniHoudiniImageFilterList &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdHoudini.HoudiniImageFilterList(%s)",
        primRepr.c_str());
}

} // anonymous namespace

void wrapUsdHoudiniHoudiniImageFilterList()
{
    typedef UsdHoudiniHoudiniImageFilterList This;

    class_<This, bases<UsdTyped> >
        cls("HoudiniImageFilterList");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("Define", &This::Define, (arg("stage"), arg("path")))
        .staticmethod("Define")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetAOVsAttr",
             &This::GetAOVsAttr)
        .def("CreateAOVsAttr",
             &_CreateAOVsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetAOVlistAttr",
             &This::GetAOVlistAttr)
        .def("CreateAOVlistAttr",
             &_CreateAOVlistAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetNormalscopeAttr",
             &This::GetNormalscopeAttr)
        .def("CreateNormalscopeAttr",
             &_CreateNormalscopeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetDepthscopeAttr",
             &This::GetDepthscopeAttr)
        .def("CreateDepthscopeAttr",
             &_CreateDepthscopeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetAlbedoscopeAttr",
             &This::GetAlbedoscopeAttr)
        .def("CreateAlbedoscopeAttr",
             &_CreateAlbedoscopeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        
        .def("GetOrderedFiltersRel",
             &This::GetOrderedFiltersRel)
        .def("CreateOrderedFiltersRel",
             &This::CreateOrderedFiltersRel)
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
