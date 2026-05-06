//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniXformCommonAPI.h"
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


static std::string
_Repr(const UsdHoudiniHoudiniXformCommonAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdHoudini.HoudiniXformCommonAPI(%s)",
        primRepr.c_str());
}

} // anonymous namespace

void wrapUsdHoudiniHoudiniXformCommonAPI()
{
    typedef UsdHoudiniHoudiniXformCommonAPI This;

    class_<This, bases<UsdAPISchemaBase> >
        cls("HoudiniXformCommonAPI");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)


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

#include "pxr/base/tf/pyEnum.h"

namespace {

tuple
_GetXformVectors(
    UsdHoudiniHoudiniXformCommonAPI self,
    const UsdTimeCode &time)
{
    GfVec3d translation;
    UsdHoudiniHoudiniXformCommonAPI::Rotation rotation;
    GfVec3f scale, pivot, shear, pivot_rotate;

    bool result = self.GetXformVectors(&translation, &rotation,
        &scale, &shear, &pivot, &pivot_rotate, time);

    return result ? make_tuple(translation, rotation, scale, shear,
                               pivot, pivot_rotate)
                  : tuple();
}

tuple
_GetXformVectorsByAccumulation(
    UsdHoudiniHoudiniXformCommonAPI self,
    const UsdTimeCode &time)
{
    GfVec3d translation;
    UsdHoudiniHoudiniXformCommonAPI::Rotation rotation;
    GfVec3f scale, pivot, shear, pivot_rotate;

    bool result = self.GetXformVectorsByAccumulation(&translation, &rotation,
        &scale, &shear, &pivot, &pivot_rotate, time);

    return result ? make_tuple(translation, rotation, scale, shear,
                               pivot, pivot_rotate)
                  : tuple();
}

tuple
_CreateXformOps1(
    UsdHoudiniHoudiniXformCommonAPI self,
    UsdHoudiniHoudiniXformCommonAPI::RotationOrder rotOrder,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op1,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op2,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op3,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op4,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op5,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op6)
{
    UsdHoudiniHoudiniXformCommonAPI::Ops ops = self.CreateXformOps(
        rotOrder, op1, op2, op3, op4, op5, op6);
    return make_tuple(
        ops.pivotOp,
        ops.pivotRotateOp,
        ops.translateOp,
        ops.rotateOp,
        ops.shearOp,
        ops.scaleOp,
        ops.inversePivotRotateOp,
        ops.inversePivotOp);
}

static tuple
_CreateXformOps2(
    UsdHoudiniHoudiniXformCommonAPI self,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op1,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op2,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op3,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op4,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op5,
    UsdHoudiniHoudiniXformCommonAPI::OpFlags op6)
{
    UsdHoudiniHoudiniXformCommonAPI::Ops ops =
        self.CreateXformOps(op1, op2, op3, op4, op5, op6);
    return make_tuple(
        ops.pivotOp,
        ops.pivotRotateOp,
        ops.translateOp,
        ops.rotateOp,
        ops.shearOp,
        ops.scaleOp,
        ops.inversePivotRotateOp,
        ops.inversePivotOp);
}

WRAP_CUSTOM {
    using This = UsdHoudiniHoudiniXformCommonAPI;

    {
        scope xformCommonAPIScope = _class;
        TfPyWrapEnum<This::RotationOrder>();
        TfPyWrapEnum<This::OpFlags>();

        class_<This::Rotation>("Rotation",
            "Represents a rotation as either Euler angles with a rotation "
            "order, or a quaternion orientation.")
            .def(init<>())
            .def(init<GfVec3f, This::RotationOrder>(
                (arg("eulerAngles"), arg("order"))))
            .def(init<GfQuatf>((arg("orient"))))
            .def("IsEuler", &This::Rotation::IsEuler)
            .def("IsOrient", &This::Rotation::IsOrient)
            .def("GetEulerAngles", &This::Rotation::GetEulerAngles)
            .def("GetEulerAnglesWithOrder", &This::Rotation::GetEulerAnglesWithOrder,
                (arg("order")))
            .def("GetRotationOrder", &This::Rotation::GetRotationOrder)
            .def("GetQuaternion", &This::Rotation::GetQuaternion)
            ;
    }

    _class
        .def("SetXformVectors", &This::SetXformVectors,
            (arg("translation"),
             arg("rotation"),
             arg("scale"),
             arg("shear"),
             arg("pivot"),
             arg("pivotRotate"),
             arg("time")))

        .def("GetXformVectors", &_GetXformVectors,
            arg("time"))

        .def("GetXformVectorsByAccumulation", &_GetXformVectorsByAccumulation,
            arg("time"))

        .def("SetTranslate", &This::SetTranslate,
            (arg("translation"),
             arg("time")=UsdTimeCode::Default()))

        .def("SetPivot", &This::SetPivot,
            (arg("pivot"),
             arg("time")=UsdTimeCode::Default()))

        .def("SetPivotRotate", &This::SetPivotRotate,
            (arg("pivotRotate"),
             arg("time")=UsdTimeCode::Default()))

        .def("SetRotate", &This::SetRotate,
            (arg("rotation"),
             arg("time")=UsdTimeCode::Default()))

        .def("SetShear", &This::SetShear,
            (arg("shear"),
             arg("time")=UsdTimeCode::Default()))

        .def("SetScale", &This::SetScale,
            (arg("scale"),
             arg("time")=UsdTimeCode::Default()))

        .def("GetResetXformStack", &This::GetResetXformStack)

        .def("SetResetXformStack", &This::SetResetXformStack,
            arg("resetXformStack"))

        .def("CreateXformOps", _CreateXformOps1,
            (arg("rotationOrder"),
             arg("op1")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op2")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op3")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op4")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op5")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op6")=UsdHoudiniHoudiniXformCommonAPI::OpNone))

        .def("CreateXformOps", _CreateXformOps2,
            (arg("op1")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op2")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op3")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op4")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op5")=UsdHoudiniHoudiniXformCommonAPI::OpNone,
             arg("op6")=UsdHoudiniHoudiniXformCommonAPI::OpNone))

        .def("GetRotationTransform", &This::GetRotationTransform,
            (arg("rotation"), arg("rotationOrder")))
        .staticmethod("GetRotationTransform")

        .def("ConvertRotationOrderToOpType",
            &This::ConvertRotationOrderToOpType,
            arg("rotationOrder"))
        .staticmethod("ConvertRotationOrderToOpType")

        .def("ConvertOpTypeToRotationOrder",
            &This::ConvertOpTypeToRotationOrder,
            arg("opType"))
        .staticmethod("ConvertOpTypeToRotationOrder")

        .def("CanConvertOpTypeToRotationOrder",
            &This::CanConvertOpTypeToRotationOrder,
            arg("opType"))
        .staticmethod("CanConvertOpTypeToRotationOrder")
        ;
}

}
