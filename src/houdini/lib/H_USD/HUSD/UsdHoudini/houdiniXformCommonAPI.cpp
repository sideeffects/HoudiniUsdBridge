//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniXformCommonAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniXformCommonAPI)
{
    TfType::Define<UsdHoudiniHoudiniXformCommonAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniXformCommonAPI::~UsdHoudiniHoudiniXformCommonAPI()
{
}

/* static */
UsdHoudiniHoudiniXformCommonAPI
UsdHoudiniHoudiniXformCommonAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniXformCommonAPI();
    }
    return UsdHoudiniHoudiniXformCommonAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniXformCommonAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniXformCommonAPI::schemaKind;
}

/* static */
const TfType &
UsdHoudiniHoudiniXformCommonAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniXformCommonAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniXformCommonAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniXformCommonAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

/*static*/
const TfTokenVector&
UsdHoudiniHoudiniXformCommonAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames;
    static TfTokenVector allNames =
        UsdAPISchemaBase::GetSchemaAttributeNames(true);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

PXR_NAMESPACE_CLOSE_SCOPE

// ===================================================================== //
// Feel free to add custom code below this line. It will be preserved by
// the code generator.
//
// Just remember to wrap code in the appropriate delimiters:
// 'PXR_NAMESPACE_OPEN_SCOPE', 'PXR_NAMESPACE_CLOSE_SCOPE'.
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

#include "XUSD_Utils.h"
#include <gusd/UT_Gf.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_XformOrder.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/trace/trace.h>

#include <map>

using std::vector;

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfEnum)
{
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ, "XYZ");
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::RotationOrderXZY, "XZY");
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::RotationOrderYXZ, "YXZ");
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::RotationOrderYZX, "YZX");
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::RotationOrderZXY, "ZXY");
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::RotationOrderZYX, "ZYX");

    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::OpTranslate);
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::OpRotate);
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::OpShear);
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::OpScale);
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::OpPivot);
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::OpPivotRotate);
    TF_ADD_ENUM_NAME(UsdHoudiniHoudiniXformCommonAPI::OpOrient);
};

// ---------------------------------------------------------------------------
// Rotation class implementation
// ---------------------------------------------------------------------------

UsdHoudiniHoudiniXformCommonAPI::Rotation::Rotation()
    : _isOrient(false)
    , _eulerAngles(0.0f)
    , _rotOrder(RotationOrderXYZ)
    , _quaternion(GfQuatf::GetIdentity())
{
}

UsdHoudiniHoudiniXformCommonAPI::Rotation::Rotation(
    const GfVec3f &eulerAngles,
    RotationOrder order)
    : _isOrient(false)
    , _eulerAngles(eulerAngles)
    , _rotOrder(order)
    , _quaternion(GfQuatf::GetIdentity())
{
}

UsdHoudiniHoudiniXformCommonAPI::Rotation::Rotation(
    const GfQuatf &orient)
    : _isOrient(true)
    , _eulerAngles(0.0f)
    , _rotOrder(RotationOrderXYZ)
    , _quaternion(orient)
{
}

GfVec3f
UsdHoudiniHoudiniXformCommonAPI::Rotation::GetEulerAngles() const
{
    if (_isOrient)
    {
        UT_QuaternionF utq(_quaternion.GetReal(),
            GusdUT_Gf::Cast(_quaternion.GetImaginary()));
        UT_Vector3F utr = utq.computeRotations(UT_XformOrder());
        utr.radToDeg();
        return GusdUT_Gf::Cast(utr);
    }
    return _eulerAngles;
}

UsdHoudiniHoudiniXformCommonAPI::RotationOrder
UsdHoudiniHoudiniXformCommonAPI::Rotation::GetRotationOrder() const
{
    if (_isOrient)
        return RotationOrderXYZ;
    return _rotOrder;
}

GfQuatf
UsdHoudiniHoudiniXformCommonAPI::Rotation::GetQuaternion() const
{
    if (!_isOrient)
    {
        GfMatrix4d rotMat = GetRotationTransform(_eulerAngles, _rotOrder);
        GfQuatd qd = rotMat.ExtractRotationQuat();
        return GfQuatf(qd.GetReal(), GfVec3f(qd.GetImaginary()));
    }
    return _quaternion;
}

static
bool
_GetCommonXformOps(
    const UsdGeomXformable& xformable,
    UsdGeomXformOp* translateOp=nullptr,
    UsdGeomXformOp* pivotOp=nullptr,
    UsdGeomXformOp* rotateOp=nullptr,
    UsdGeomXformOp* pivotRotateOp=nullptr,
    UsdGeomXformOp* shearOp=nullptr,
    UsdGeomXformOp* scaleOp=nullptr,
    UsdGeomXformOp* pivotInvOp=nullptr,
    UsdGeomXformOp* pivotRotateInvOp=nullptr,
    bool* resetXformStack=nullptr);

static
UsdHoudiniHoudiniXformCommonAPI::Ops
_GetOrAddCommonXformOps(
    const UsdGeomXformable& xformable,
    const UsdHoudiniHoudiniXformCommonAPI::RotationOrder* rotOrder,
    bool createTranslate,
    bool createPivot,
    bool createPivotRotate,
    bool createRotate,
    bool createShear,
    bool createScale,
    bool createOrient=false);

/* virtual */
bool
UsdHoudiniHoudiniXformCommonAPI::_IsCompatible() const
{
    if (!UsdAPISchemaBase::_IsCompatible())
    {
        return false;
    }

    UsdGeomXformable xformable(GetPrim());
    if (!xformable)
    {
        return false;
    }

    return _GetCommonXformOps(xformable);
}

// Assumes rotationOrder is XYZ.
static void
_RotMatToRotXYZ(
    const GfMatrix4d &rotMat,
    GfVec3f *rotXYZ)
{
    GfRotation rot = rotMat.ExtractRotation();
    GfVec3d angles = rot.Decompose(GfVec3d::ZAxis(),
                                   GfVec3d::YAxis(),
                                   GfVec3d::XAxis());
    *rotXYZ = GfVec3f(angles[2], angles[1], angles[0]);
}

static void
_ConvertMatrixToComponents(const GfMatrix4d &matrix,
                           GfVec3d *translation,
                           GfVec3f *rotation,
                           GfVec3f *shear,
                           GfVec3f *scale)
{
    // The original function was using Factor() from GfMatrix4d,
    // that would not give us the desired outputs, since no shear is preserved.
    // Use explode() instead

    const UT_Matrix4D &utMatrix = GusdUT_Gf::Cast(matrix);
    UT_XformOrder order(UT_XformOrder::SRT, UT_XformOrder::XYZ);

    UT_Vector3D rotD(0.0), scaleD(1.0), transTmp(0.0), pivot(0.0), shearD(0.0);
    UT_Vector3D *transPtr =
        translation ? GusdUT_Gf::Cast(translation) : &transTmp;

    utMatrix.explode(order, rotD, scaleD, *transPtr, pivot, &shearD);

    if (rotation)
    {
        rotation->Set(SYSradToDeg(rotD.x()),
                      SYSradToDeg(rotD.y()),
                      SYSradToDeg(rotD.z()));
    }
    if (scale)
    {
        scale->Set(scaleD.x(), scaleD.y(), scaleD.z());
    }
    if (shear)
    {
        GusdUT_Gf::Convert(shearD, *shear);
    }
}

bool
UsdHoudiniHoudiniXformCommonAPI::SetXformVectors(
    const GfVec3d &translation,
    const Rotation &rotation,
    const GfVec3f &scale,
    const GfVec3f &shear,
    const GfVec3f &pivot,
    const GfVec3f &pivotRotate,
    const UsdTimeCode time) const
{
    bool isValidShear = !SYSequalZero(GusdUT_Gf::Cast(shear));
    bool isValidPivotRotate = !SYSequalZero(GusdUT_Gf::Cast(pivotRotate));

    // Build op flags based on rotation type
    int rotFlag = rotation.IsOrient() ? OpOrient : OpRotate;
    OpFlags flags = (OpFlags)(OpTranslate | rotFlag | OpScale | OpPivot);
    if (isValidShear)
        flags = (OpFlags)(flags | OpShear);
    if (isValidPivotRotate)
        flags = (OpFlags)(flags | OpPivotRotate);

    const Ops ops = rotation.IsEuler()
        ? CreateXformOps(rotation.GetRotationOrder(), flags)
        : CreateXformOps(flags);

    if (!ops.translateOp || !ops.rotateOp || !ops.scaleOp || !ops.pivotOp ||
         (isValidShear && !ops.shearOp) ||
         (isValidPivotRotate
              && (!ops.pivotRotateOp || !ops.inversePivotRotateOp)))
    {
        return false;
    }

    // Set the rotation value according to its type
    bool isSuccess = ops.translateOp.Set(translation, time);
    if (rotation.IsOrient())
    {
        isSuccess = isSuccess
            && ops.rotateOp.Set(rotation.GetQuaternion(), time);
    }
    else
    {
        isSuccess = isSuccess
            && ops.rotateOp.Set(rotation.GetEulerAngles(), time);
    }
    isSuccess = isSuccess
        && ops.scaleOp.Set(scale, time)
        && ops.pivotOp.Set(pivot, time);

    if (isValidPivotRotate)
    {
        isSuccess = isSuccess && ops.pivotRotateOp.Set(pivotRotate, time);
    }

    if (isValidShear)
    {
        UT_Matrix4D shearMatrix;
        shearMatrix.identity();
        shearMatrix.shear(shear[0], shear[1], shear[2]);
        isSuccess = isSuccess
            && ops.shearOp.Set(GusdUT_Gf::Cast(shearMatrix), time);
    }

    return isSuccess;
}

static
bool
_IsMatrixIdentity(const GfMatrix4d& matrix)
{
    const GfMatrix4d IDENTITY(1.0);
    const double TOLERANCE = 1e-6;

    if (GfIsClose(matrix.GetRow(0), IDENTITY.GetRow(0), TOLERANCE)      &&
            GfIsClose(matrix.GetRow(1), IDENTITY.GetRow(1), TOLERANCE)  &&
            GfIsClose(matrix.GetRow(2), IDENTITY.GetRow(2), TOLERANCE)  &&
            GfIsClose(matrix.GetRow(3), IDENTITY.GetRow(3), TOLERANCE))
    {
        return true;
    }

    return false;
}

static
bool
_MatricesAreInverses(const GfMatrix4d& matrix1, const GfMatrix4d& matrix2)
{
    GfMatrix4d mult = matrix1 * matrix2;
    return _IsMatrixIdentity(mult);
}

static constexpr
bool
_IsThreeAxisRotateOpType(UsdGeomXformOp::Type opType)
{
    static_assert(
        UsdGeomXformOp::TypeRotateZYX - UsdGeomXformOp::TypeRotateXYZ == 5,
        "Exactly six three-axis rotate op types");
    return opType >= UsdGeomXformOp::TypeRotateXYZ &&
           opType <= UsdGeomXformOp::TypeRotateZYX;
}

static constexpr
bool
_IsRotateOpType(UsdGeomXformOp::Type opType)
{
    static_assert(
        UsdGeomXformOp::TypeRotateZYX - UsdGeomXformOp::TypeRotateX == 8,
        "Exactly nine rotate op types");
    return opType >= UsdGeomXformOp::TypeRotateX &&
           opType <= UsdGeomXformOp::TypeRotateZYX;
}

static_assert(_IsRotateOpType(UsdGeomXformOp::TypeRotateX) &&
             !_IsThreeAxisRotateOpType(UsdGeomXformOp::TypeRotateX), "");
static_assert(_IsRotateOpType(UsdGeomXformOp::TypeRotateY) &&
             !_IsThreeAxisRotateOpType(UsdGeomXformOp::TypeRotateY), "");
static_assert(_IsRotateOpType(UsdGeomXformOp::TypeRotateZ) &&
             !_IsThreeAxisRotateOpType(UsdGeomXformOp::TypeRotateZ), "");
static_assert(_IsRotateOpType(UsdGeomXformOp::TypeRotateXYZ) &&
              _IsThreeAxisRotateOpType(UsdGeomXformOp::TypeRotateXYZ), "");
static_assert(_IsRotateOpType(UsdGeomXformOp::TypeRotateXZY) &&
              _IsThreeAxisRotateOpType(UsdGeomXformOp::TypeRotateXZY), "");
static_assert(_IsRotateOpType(UsdGeomXformOp::TypeRotateYXZ) &&
              _IsThreeAxisRotateOpType(UsdGeomXformOp::TypeRotateYXZ), "");
static_assert(_IsRotateOpType(UsdGeomXformOp::TypeRotateYZX) &&
              _IsThreeAxisRotateOpType(UsdGeomXformOp::TypeRotateYZX), "");
static_assert(_IsRotateOpType(UsdGeomXformOp::TypeRotateZXY) &&
              _IsThreeAxisRotateOpType(UsdGeomXformOp::TypeRotateZXY), "");
static_assert(_IsRotateOpType(UsdGeomXformOp::TypeRotateZYX) &&
              _IsThreeAxisRotateOpType(UsdGeomXformOp::TypeRotateZYX), "");

static_assert(!_IsRotateOpType(UsdGeomXformOp::TypeTranslate) &&
              !_IsThreeAxisRotateOpType(UsdGeomXformOp::TypeTranslate), "");
static_assert(!_IsRotateOpType(UsdGeomXformOp::TypeScale) &&
              !_IsThreeAxisRotateOpType(UsdGeomXformOp::TypeScale), "");

// Pivot rotate is ALWAYS TypeRotateXYZ with ":pivot" suffix
bool _IsPivotRotateXformOp(const UsdGeomXformOp &op)
{
    if (op.GetOpType() != UsdGeomXformOp::TypeRotateXYZ)
        return false;

    // Pivot rotate ops have the "pivot" suffix
    // e.g. "xformOp:rotateXYZ:pivot" or "!invert!xformOp:rotateXYZ:pivot"
    return TfStringEndsWith(op.GetOpName().GetString(),
        UsdHoudiniTokens->pivot.GetString());
}

static
UsdGeomXformOp::Type
_GetRotateOpType(const vector<UsdGeomXformOp>& ops)
{
    for (const UsdGeomXformOp& op : ops)
    {
        if (_IsPivotRotateXformOp(op))
        {
            continue;
        }
        if (_IsRotateOpType(op.GetOpType()) ||
            op.GetOpType() == UsdGeomXformOp::TypeOrient)
        {
            return op.GetOpType();
        }
    }
    return UsdGeomXformOp::TypeRotateXYZ;
}

/* static */
UsdGeomXformOp::Type
UsdHoudiniHoudiniXformCommonAPI::ConvertRotationOrderToOpType(
    UsdHoudiniHoudiniXformCommonAPI::RotationOrder rotOrder)
{
    switch (rotOrder)
    {
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ:
            return UsdGeomXformOp::TypeRotateXYZ;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderXZY:
            return UsdGeomXformOp::TypeRotateXZY;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderYXZ:
            return UsdGeomXformOp::TypeRotateYXZ;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderYZX:
            return UsdGeomXformOp::TypeRotateYZX;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderZXY:
            return UsdGeomXformOp::TypeRotateZXY;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderZYX:
            return UsdGeomXformOp::TypeRotateZYX;
        default:
            // Should never hit this.
            TF_CODING_ERROR("Invalid rotation order <%s>.",
                TfEnum::GetName(rotOrder).c_str());
            // Default rotation order is XYZ.
            return UsdGeomXformOp::TypeRotateXYZ;
    }
}

/* static */
UsdHoudiniHoudiniXformCommonAPI::RotationOrder
UsdHoudiniHoudiniXformCommonAPI::ConvertOpTypeToRotationOrder(
    UsdGeomXformOp::Type opType)
{
    switch (opType)
    {
        case UsdGeomXformOp::TypeRotateXYZ:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ;
        case UsdGeomXformOp::TypeRotateXZY:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderXZY;
        case UsdGeomXformOp::TypeRotateYXZ:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderYXZ;
        case UsdGeomXformOp::TypeRotateYZX:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderYZX;
        case UsdGeomXformOp::TypeRotateZXY:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderZXY;
        case UsdGeomXformOp::TypeRotateZYX:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderZYX;
        default:
            TF_CODING_ERROR("'%s' is not a three-axis rotate op type",
                TfEnum::GetName(opType).c_str());
            // Default rotation order is XYZ.
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ;
    }
}

/* static */
bool
UsdHoudiniHoudiniXformCommonAPI::CanConvertOpTypeToRotationOrder(
    UsdGeomXformOp::Type opType)
{
    // _IsThreeAxisRotateOpType must be a separate function because it is
    // constexpr (so that we can static_assert) but we want to keep the
    // definition out of the header (constexpr implies inline, so it needs to be
    // defined where it's declared).
    return _IsThreeAxisRotateOpType(opType);
}

static GfVec3f
_DecomposeByOrder(const GfMatrix4d &m, UsdHoudiniHoudiniXformCommonAPI::RotationOrder order)
{
    const GfRotation r = m.ExtractRotation();
    const GfVec3d x = GfVec3d::XAxis(), y = GfVec3d::YAxis(), z = GfVec3d::ZAxis();
    GfVec3d a;

    switch (order) {
    case UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ:
        a = r.Decompose(z, y, x);
        return GfVec3f(a[2], a[1], a[0]);
    case UsdHoudiniHoudiniXformCommonAPI::RotationOrderXZY:
        a = r.Decompose(y, z, x);
        return GfVec3f(a[2], a[0], a[1]);
    case UsdHoudiniHoudiniXformCommonAPI::RotationOrderYXZ:
        a = r.Decompose(z, x, y);
        return GfVec3f(a[1], a[2], a[0]);
    case UsdHoudiniHoudiniXformCommonAPI::RotationOrderYZX:
        a = r.Decompose(x, z, y);
        return GfVec3f(a[0], a[2], a[1]);
    case UsdHoudiniHoudiniXformCommonAPI::RotationOrderZXY:
        a = r.Decompose(y, x, z);
        return GfVec3f(a[1], a[0], a[2]);
    case UsdHoudiniHoudiniXformCommonAPI::RotationOrderZYX:
        a = r.Decompose(x, y, z);
        return GfVec3f(a[0], a[1], a[2]);
    }
    return GfVec3f(0.0f);
}

const TfToken &
_GetShearXformOpName()
{
    static const TfToken theShearName("xformOp:transform:shear");
    return theShearName;
}

bool _IsShearXformOp(const UsdGeomXformOp &op)
{
    return (op.GetOpType() == UsdGeomXformOp::TypeTransform &&
        op.GetOpName() == _GetShearXformOpName());
}

const TfToken &
_GetPivotRotateXformOpName()
{
    // Token remains ":pivot" to keep existing op naming stable.
    static const TfToken thePivotRotateName("xformOp:rotateXYZ:pivot");
    return thePivotRotateName;
}

// This helper method looks through the given xformOps and returns a vector of
// common op types that the xformOps could possibly be reduced to by
// accumulation.
static
vector<UsdGeomXformOp::Type>
_GetCommonOpTypesForOpOrder(const vector<UsdGeomXformOp>& xformOps,
                            int* translateIndex,
                            int* translatePivotIndex,
                            int* pivotRotateIndex,
                            int* rotateIndex,
                            int* shearIndex,
                            int* translateIdentityIndex,
                            int* scaleIndex,
                            int* translatePivotInvertIndex,
                            int* pivotRotateInvertIndex)
{
    UsdGeomXformOp::Type rotateOpType = UsdGeomXformOp::TypeRotateXYZ;
    bool hasRotateOp = false;
    bool hasPivotRotateOp = false;
    bool hasScaleOp = false;
    bool hasShearOp = false;
    size_t numInverseTranslateOps = 0;

    TF_FOR_ALL(it, xformOps)
    {
        if (_IsPivotRotateXformOp(*it))
        {
            hasPivotRotateOp = true;
        }
        else if (_IsRotateOpType(it->GetOpType()) ||
                 it->GetOpType() == UsdGeomXformOp::TypeOrient)
        {
            hasRotateOp = true;
            rotateOpType = it->GetOpType();
        }
        else if (it->GetOpType() == UsdGeomXformOp::TypeScale)
        {
            hasScaleOp = true;
        }
        else if (it->GetOpType() == UsdGeomXformOp::TypeTranslate &&
                   it->IsInverseOp())
        {
            ++numInverseTranslateOps;
        }
        else if (_IsShearXformOp(*it))
        {
            hasShearOp = true;
        }
    }

    vector<UsdGeomXformOp::Type> commonOpTypes;
    size_t currentIndex = 0;

    // Standard + Shear:     [Translate, Pivot, ...]
    // Pivot Rotate: [Pivot, PivotRotate, Translate, ...]

    if (hasPivotRotateOp)
    {
        // Order: Tp -> Pr -> T
        commonOpTypes.push_back(UsdGeomXformOp::TypeTranslate);
        *translatePivotIndex = currentIndex++;

        commonOpTypes.push_back(UsdGeomXformOp::TypeRotateXYZ);
        *pivotRotateIndex = currentIndex++;

        commonOpTypes.push_back(UsdGeomXformOp::TypeTranslate);
        *translateIndex = currentIndex++;
    }
    else
    {
        // Order: T -> Tp
        commonOpTypes.push_back(UsdGeomXformOp::TypeTranslate);
        *translateIndex = currentIndex++;

        commonOpTypes.push_back(UsdGeomXformOp::TypeTranslate);
        *translatePivotIndex = currentIndex++;

        *pivotRotateIndex = -1;
    }
    *rotateIndex = -1;
    *translateIdentityIndex = -1;
    *scaleIndex = -1;
    *shearIndex = -1;
    *pivotRotateInvertIndex = -1;

    if (hasRotateOp)
    {
        commonOpTypes.push_back(rotateOpType);
        *rotateIndex = currentIndex++;
    }

    if (hasShearOp)
    {
        commonOpTypes.push_back(UsdGeomXformOp::TypeTransform);
        *shearIndex = currentIndex++;
    }

    if (numInverseTranslateOps > 1)
    {
        // If more than one inverse translate is present, assume that means
        // that both a pivot rotate and a scale pivot are specified. For it to
        // be reducible, they must be at the same location in space, in which
        // case they'll accumulate to identity in the translateIdentityIndex
        // position.
        commonOpTypes.push_back(UsdGeomXformOp::TypeTranslate);
        *translateIdentityIndex = currentIndex++;
    }

    if (hasScaleOp)
    {
        commonOpTypes.push_back(UsdGeomXformOp::TypeScale);
        *scaleIndex = currentIndex++;
    }

    if (hasPivotRotateOp)
    {
        commonOpTypes.push_back(UsdGeomXformOp::TypeRotateXYZ);
        *pivotRotateInvertIndex = currentIndex++;
    }

    commonOpTypes.push_back(UsdGeomXformOp::TypeTranslate);
    *translatePivotInvertIndex = currentIndex++;

    return commonOpTypes;
}

static bool
_GetFromVec3dOrVec3f(
        const UsdGeomXformOp& attr,
        GfVec3f* value,
        const UsdTimeCode& time)
{
    // First, try GfVec3d which we downcast to GfVec3f.
    GfVec3d valueD;
    if (attr.Get(&valueD, time))
    {
        if (value)
        {
            *value = GfVec3f(valueD);
        }
        return true;
    }

    // Fallback to GfVec3f.
    if (attr.Get(value, time))
    {
        return true;
    }

    return false;
}

bool
UsdHoudiniHoudiniXformCommonAPI::GetXformVectors(
    GfVec3d *translation,
    Rotation *rotation,
    GfVec3f *scale,
    GfVec3f *shear,
    GfVec3f *pivot,
    GfVec3f *pivotRotate,
    const UsdTimeCode time) const
{
    if (!TF_VERIFY(translation && rotation && scale && pivot))
    {
        return false;
    }

    UsdGeomXformable xformable(GetPrim());

    // Handle incompatible xform case first.
    // It's ok for an xform to be incompatible when extracting xform vectors.
    UsdGeomXformOp t, p, pr, r, sh, s;
    GfMatrix4d shear4D;
    if (!_GetCommonXformOps(xformable, &t, &p, &r, &pr, &sh, &s))
    {
        GfMatrix4d localXform(1.);
        bool resetsXformStack = false;
        xformable.GetLocalTransformation(&localXform, &resetsXformStack, time);

        // We don't process (or return) resetsXformStack here. It is up to the
        // clients to call GetResetXformStack() and process it suitably.
        GfVec3f eulerAngles;
        _ConvertMatrixToComponents(localXform, translation, &eulerAngles,
                                   shear, scale);
        *rotation = Rotation(eulerAngles, RotationOrderXYZ);
        *pivot = GfVec3f(0.);
        *pivotRotate = GfVec3f(0.);

        return true;
    }

    // If any of the ops don't exist or if no value is authored, then returning
    // identity values.

    if (!t || !t.Get(translation, time))
    {
        *translation = GfVec3d(0.);
    }

    if (!r)
    {
        *rotation = Rotation();
    }
    else if (r.GetOpType() == UsdGeomXformOp::TypeOrient)
    {
        GfQuatf quat;
        if (r.Get(&quat, time))
            *rotation = Rotation(quat);
        else
            *rotation = Rotation(GfQuatf::GetIdentity());
    }
    else
    {
        GfVec3f eulerAngles;
        if (r.Get(&eulerAngles, time))
            *rotation = Rotation(eulerAngles,
                                 ConvertOpTypeToRotationOrder(r.GetOpType()));
        else
            *rotation = Rotation();
    }

    if (!sh || !sh.Get(&shear4D, time))
    {
        *shear = GfVec3f(0.0, 0.0, 0.0);
    }
    else
    {
        *shear = GfVec3f(shear4D[1][0], shear4D[2][0], shear4D[2][1]);
    }

    if (!s || !s.Get(scale, time))
    {
        *scale = GfVec3f(1.);
    }

    if (!p || !_GetFromVec3dOrVec3f(p, pivot, time))
    {
        *pivot = GfVec3f(0.);
    }

    if (!pr || !_GetFromVec3dOrVec3f(pr, pivotRotate, time))
    {
        *pivotRotate = GfVec3f(0.);
    }

    return true;
}


bool
UsdHoudiniHoudiniXformCommonAPI::GetXformVectorsByAccumulation(
    GfVec3d *translation,
    Rotation *rotation,
    GfVec3f *scale,
    GfVec3f *shear,
    GfVec3f *pivot,
    GfVec3f *pivotRotate,
    const UsdTimeCode time) const
{
    // If the xformOps are compatible as authored, then just use the usual
    // component extraction method.
    if (_IsCompatible())
    {
        return GetXformVectors(translation, rotation, scale, shear, pivot,
                                pivotRotate, time);
    }

    UsdGeomXformable xformable(GetPrim());
    bool unusedResetXformStack;
    const std::vector<UsdGeomXformOp> xformOps =
        xformable.GetOrderedXformOps(&unusedResetXformStack);

    // Note that we don't currently accumulate rotate ops, so we'll be looking
    // for one xformOp of a particular rotation type. Any xformOp order with
    // multiple rotates will be considered not to conform.
    const UsdGeomXformOp::Type rotateOpType = _GetRotateOpType(xformOps);
    const bool isOrientRotation =
        (rotateOpType == UsdGeomXformOp::TypeOrient);
    const UsdHoudiniHoudiniXformCommonAPI::RotationOrder resolvedRotOrder =
            CanConvertOpTypeToRotationOrder(rotateOpType)
            ? ConvertOpTypeToRotationOrder(rotateOpType)
            : UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ;

    // The xformOp order expected by the common API is:
    // {Translate, Translate (pivot), Rotate, Scale, Translate (invert pivot)}
    // Depending on what we find in the xformOps (presence/absence of rotate,
    // scale(s), and number of inverse translates), we come up with an order
    // of common op types that we might be able to reduce the xformOps to.
    // We also maintain some named indices into that order which may be -1
    // (invalid) if that op is not present.
    int translateIndex, translatePivotIndex, pivotRotateIndex, rotateIndex, shearIndex,
        translateIdentityIndex, scaleIndex, translatePivotInvertIndex, pivotRotateInvertIndex;
    vector<UsdGeomXformOp::Type> commonOpTypes =
        _GetCommonOpTypesForOpOrder(xformOps,
            &translateIndex, &translatePivotIndex, &pivotRotateIndex, &rotateIndex, &shearIndex,
            &translateIdentityIndex, &scaleIndex, &translatePivotInvertIndex, &pivotRotateInvertIndex);

    // Keep a set of matrices that we'll accumulate the xformOp transforms into.
    vector<GfMatrix4d> commonOpMatrices(commonOpTypes.size(), GfMatrix4d(1.0));

    // Scan backwards through the xformOps and list of commonOpTypes
    // accumulating transforms as we go. We scan backwards so that we
    // accumulate the inverse pivot first and can then use that to determine
    // where to split the translates at the front between pivot and non-pivot.
    int xformOpIndex = xformOps.size() - 1;
    int commonOpTypeIndex = commonOpTypes.size() - 1;

    #ifdef DEBUG
        for(auto ops : xformOps)
        {
            UTdebugPrint(ops.GetName().GetString());
        }
    #endif

    #ifdef DEBUG
        int index = 0;
        for(auto opType : commonOpTypes)
        {
            UTdebugPrint("commonOpTypes[", index++,"]:",
                TfEnum::GetName(opType).c_str());
        }
    #endif

    // When pivot rotate is not presented, translate index is at the front of the array
    // When pivot rotate is presented, translate pivot is at the front of the array
    while (xformOpIndex >= 0 &&
            ((commonOpTypeIndex >= translateIndex && pivotRotateIndex == -1) ||
             (commonOpTypeIndex >= translatePivotIndex && pivotRotateIndex > -1)))
    {
        const UsdGeomXformOp xformOp = xformOps[xformOpIndex];
        UsdGeomXformOp::Type commonOpType = commonOpTypes[commonOpTypeIndex];

        if (xformOp.GetOpType() != commonOpType)
        {
            --commonOpTypeIndex;
            continue;
        }

        // The current op has the type we expect. Multiply its transform
        // into the results.
        commonOpMatrices[commonOpTypeIndex] *= xformOp.GetOpTransform(time);
        --xformOpIndex;

        if (commonOpTypeIndex == rotateIndex)
        {
            // We currently do not allow rotate ops to accumulate, so as
            // soon as we match one, advance to the next commonOpType.
            --commonOpTypeIndex;
        }else if(commonOpTypeIndex == shearIndex)
        {
            // We don't accumulate shears
            --commonOpTypeIndex;
        }
        else if (commonOpTypeIndex == pivotRotateIndex &&
                    _MatricesAreInverses(commonOpMatrices[pivotRotateIndex],
                        commonOpMatrices[pivotRotateInvertIndex]))
        {
            // Found valid pair, move on, don't accumulate
            --commonOpTypeIndex;
        }
        else if (commonOpTypeIndex == pivotRotateInvertIndex && xformOp.IsInverseOp())
        {
            --commonOpTypeIndex;
        }
        else if (commonOpType == UsdGeomXformOp::TypeTranslate)
        {
            if (xformOp.IsInverseOp())
            {
                // We use the inverse-ness of translate ops to know when we
                // should move on to the next common op type. When we see an
                // inverse translate, we can assume that a valid order will
                // have its pair farther towards the front.
                --commonOpTypeIndex;
            }
            else if (commonOpTypeIndex == translatePivotIndex &&
                       _MatricesAreInverses(
                           commonOpMatrices[translatePivotIndex],
                           commonOpMatrices[translatePivotInvertIndex]))
            {
                // We've found a pair of pivot transforms, so we'll accumulate
                // the rest of the translates into regular translation.
                --commonOpTypeIndex;
            }

        }
        else if (commonOpType == UsdGeomXformOp::TypeTransform && commonOpTypeIndex == shearIndex)
        {
            // If shear is detected, proceed to next op type,
            // since we do not accumulate multiple shears
            --commonOpTypeIndex;
        }
    }

    #ifdef DEBUG
        int matrixIndex = 0;
            for(auto matrix : commonOpMatrices)
            {
                UTdebugPrint("commonOpMatrices[", matrixIndex++, "]:",
                    TfStringify(matrix));
            }
    #endif

    bool reducible = true;
    const int minOpIndex = (pivotRotateIndex > -1) ?
                            translatePivotIndex : translateIndex;
    if (xformOpIndex >= minOpIndex)
    {
        // We didn't make it all the way through the xformOps, so there must
        // have been something in there that does not conform.
        // UTdebugPrint("Not Reducible due to pivot index not to the end");
        reducible = false;
    }

    // Make sure that any translates between the rotate and scale ops
    // accumulated to identity.
    if (translateIdentityIndex >= 0 &&
            !_IsMatrixIdentity(commonOpMatrices[translateIdentityIndex]))
    {
        // UTdebugPrint("Not Reducible since identity translate is not identity");
        reducible = false;
    }

    // If all we saw while scanning were translates, then swap the accumulated
    // translation matrix from the "Translate (invert pivot)" position into the
    // "Translate" position.
    if (commonOpTypeIndex == translatePivotInvertIndex)
    {
        commonOpMatrices[translateIndex] = commonOpMatrices[commonOpTypeIndex];
        commonOpMatrices[commonOpTypeIndex] = GfMatrix4d(1.0);

    }

    // Verify that the translate pivot and inverse translate pivot are inverses
    // of each other. If there is no pivot, these should both still be identity.
    if (!_MatricesAreInverses(commonOpMatrices[translatePivotIndex],
                                 commonOpMatrices[translatePivotInvertIndex]))
    {
        // UTdebugPrint("Not Reducible since pivots don't cancel out");
        reducible = false;
    }

    if (!reducible)
    {
        if (shear)
            *shear = GfVec3f(0.);
        return GetXformVectors(translation, rotation, scale, shear, pivot,
                               pivotRotate, time);
    }

    if (translation)
    {
        *translation = commonOpMatrices[translateIndex].ExtractTranslation();
    }

    if (pivot)
    {
        GfVec3d result =
            commonOpMatrices[translatePivotIndex].ExtractTranslation();
        *pivot = GfVec3f(result[0], result[1], result[2]);
    }

    if (pivotRotate)
    {
        if (pivotRotateIndex >= 0)
        {
            *pivotRotate = _DecomposeByOrder(
                commonOpMatrices[pivotRotateIndex],UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ);
        }
       else
        {
            *pivotRotate = GfVec3f(0.0, 0.0, 0.0);
        }
    }

    if (shear)
    {
        if(shearIndex >= 0)
        {
            //if-else block is not used here since shear is optional
            const UT_XformOrder order = HUSDcastRotOrder(resolvedRotOrder);
            UT_Vector3D rotTmp(0.0), scaleTmp(1.0), transTmp(0.0), pivotTmp(0.0), shearVals(0.0);
            GusdUT_Gf::Cast(commonOpMatrices[shearIndex]).explode(
                order, rotTmp, scaleTmp, transTmp, pivotTmp, &shearVals);
            if(!SYSequalZero(rotTmp) || !SYSequalZero(scaleTmp - UT_Vector3D(1.0)) ||
                !SYSequalZero(transTmp) || !SYSequalZero(pivotTmp))
            {
                UTdebugPrint("Shear reduction runtime check failed.");
                return false;
            }
            // UTdebugPrint("Shear component matrix is:", TfStringify(commonOpMatrices[shearIndex]));
            GusdUT_Gf::Convert(shearVals, *shear);
        }
        else
        {
            *shear = GfVec3f(0.0, 0.0, 0.0);
        }
    }

    if (rotation)
    {
        if (rotateIndex >= 0)
        {
            if (isOrientRotation)
            {
                GfRotation rot =
                    commonOpMatrices[rotateIndex].ExtractRotation();
                GfQuaternion qd = rot.GetQuaternion();
                *rotation = Rotation(
                    GfQuatf(qd.GetReal(), GfVec3f(qd.GetImaginary())));
            }
            else
            {
                *rotation = Rotation(
                    _DecomposeByOrder(commonOpMatrices[rotateIndex],
                                      resolvedRotOrder),
                    resolvedRotOrder);
            }
        }
        else
        {
            *rotation = Rotation();
        }
    }

    if (scale)
    {
        if (scaleIndex >= 0)
        {
            (*scale)[0] = commonOpMatrices[scaleIndex][0][0];
            (*scale)[1] = commonOpMatrices[scaleIndex][1][1];
            (*scale)[2] = commonOpMatrices[scaleIndex][2][2];
        }
        else
        {
            *scale = GfVec3f(1.0, 1.0, 1.0);
        }
    }

    return true;
}

bool
UsdHoudiniHoudiniXformCommonAPI::GetResetXformStack() const
{
    return UsdGeomXformable(GetPrim()).GetResetXformStack();
}

bool
UsdHoudiniHoudiniXformCommonAPI::SetResetXformStack(bool resetXformStack) const
{
    return UsdGeomXformable(GetPrim()).SetResetXformStack(resetXformStack);
}

// Retrieves the XformCommonAPI-compatible component ops for the given xformable
// prim. Returns true if the ops are in a compatible order or false if they're
// in an incompatible order. Populates the non-null out-parameters with the
// requested ops. (If an op does not exist, the corresponding out-parameter is
// populated with an invalid UsdGeomXformOp.) If resetXformStack is non-null,
// populates it with the value of UsdGeomXformable::GetResetXformStack().
static
bool
_GetCommonXformOps(
    const UsdGeomXformable& xformable,
    UsdGeomXformOp* translateOp,
    UsdGeomXformOp* pivotOp,
    UsdGeomXformOp* rotateOp,
    UsdGeomXformOp* pivotRotateOp,
    UsdGeomXformOp* shearOp,
    UsdGeomXformOp* scaleOp,
    UsdGeomXformOp* pivotInvOp,
    UsdGeomXformOp* pivotRotateInvOp,
    bool* resetXformStack)
{
    TRACE_FUNCTION();

    bool tempResetXformStack;
    std::vector<UsdGeomXformOp> xformOps =
        xformable.GetOrderedXformOps(&tempResetXformStack);
    if (xformOps.size() > 8)
        return false;

    // The vanilla order is:
    // ["xformOp:translate", "xformOp:translate:pivot", "xformOp:rotateABC",
    //  "xformOp:scale", "!invert!xformOp:translate:pivot"]
    // The XUSD Xform Common API order is:
    // ["xformOp:translate", "xformOp:translate:pivot", "xformOp:rotateABC",
    //  "xformOp:transform:shear", "xformOp:scale", "!invert!xformOp:translate:pivot"]
    // Whenever there is a non-zero pivot rotate:
    // ["xformOp:translate:pivot", "xformOp:rotateXYZ:pivot", "xformOp:translate", "xformOp:rotateABC",
    //  "xformOp:transform:shear", "xformOp:scale", "!invert!xformOp:rotateXYZ:pivot", "!invert!xformOp:translate:pivot"]
    auto it = xformOps.begin();

    // This holds the computed attribute name tokens so that we can avoid
    // hard-coding them.
    // The name for the rotate op is not computed here because it can vary.
    static const struct {
        TfToken translate = UsdGeomXformOp::GetOpName(
            UsdGeomXformOp::TypeTranslate);
        TfToken pivot = UsdGeomXformOp::GetOpName(
            UsdGeomXformOp::TypeTranslate, UsdHoudiniTokens->pivot);
        TfToken scale = UsdGeomXformOp::GetOpName(
            UsdGeomXformOp::TypeScale);
        TfToken shear  = _GetShearXformOpName();
        TfToken pivotrotate = _GetPivotRotateXformOpName();
    } attrNames;

    // Search one-by-one for the ops in the correct order.
    // We can skip ops in the "expected" order (that is, all the common ops are
    // optional) but we can't skip ops in the "actual" order (that is, extra ops
    // aren't allowed).
    //
    // Note, in checks below, avoid using UsdGeomXformOp::GetOpName() because
    // it will construct strings in the case of an inverted op.
    UsdGeomXformOp t;
    if (it != xformOps.end() &&
            it->GetName() == attrNames.translate &&
            !it->IsInverseOp())
    {
        // UTdebugPrint("t moved: ", it->GetName().GetString());
        t = std::move(*it);
        ++it;
    }

    UsdGeomXformOp p;
    if (it != xformOps.end() &&
            it->GetName() == attrNames.pivot &&
            !it->IsInverseOp())
    {
        // UTdebugPrint("p moved: ", it->GetName().GetString());
        p = std::move(*it);
        ++it;
    }

    // if translate is initialized, then it won't be compatible with pivot rotate
    UsdGeomXformOp pr;
    if ((bool) !t && it != xformOps.end() &&
            it->GetName() == attrNames.pivotrotate &&
            !it->IsInverseOp())
    {
        // UTdebugPrint("pr moved: ", it->GetName().GetString());
        pr = std::move(*it);
        ++it;
    }

    // Check for translate again here,
    // The position of t and tp will be swapped when pivot rotate is presented
    if ((bool) !t && (bool) pr && it != xformOps.end() &&
            it->GetName() == attrNames.translate &&
            !it->IsInverseOp())
    {
        // UTdebugPrint("t moved: ", it->GetName().GetString());
        t = std::move(*it);
        ++it;
    }

    UsdGeomXformOp r;
    if (it != xformOps.end() &&
            (UsdHoudiniHoudiniXformCommonAPI::CanConvertOpTypeToRotationOrder(
                it->GetOpType()) ||
             it->GetOpType() == UsdGeomXformOp::TypeOrient) &&
            !it->IsInverseOp()  &&
            it->GetName() != attrNames.pivotrotate)
    {
        // UTdebugPrint("r moved: ", it->GetName().GetString());
        r = std::move(*it);
        ++it;
    }

    UsdGeomXformOp sh;
    if (it != xformOps.end() &&
        _IsShearXformOp(*it) &&
        !it->IsInverseOp())
    {
        // UTdebugPrint("sh moved: ", it->GetName().GetString());
        sh = std::move(*it);
        ++it;
    }

    UsdGeomXformOp s;
    if (it != xformOps.end() &&
            it->GetName() == attrNames.scale &&
            !it->IsInverseOp())
    {
        // UTdebugPrint("s moved: ", it->GetName().GetString());
        s = std::move(*it);
        ++it;
    }

    UsdGeomXformOp prInv;
    if (it != xformOps.end() &&
            it->GetName() == attrNames.pivotrotate &&
            it->IsInverseOp())
    {
        // UTdebugPrint("prInv moved: ", it->GetName().GetString());
        prInv = std::move(*it);
        ++it;
    }

    UsdGeomXformOp pInv;
    if (it != xformOps.end() &&
            it->GetName() == attrNames.pivot &&
            it->IsInverseOp())
    {
        // UTdebugPrint("pInv moved: ", it->GetName().GetString());
        pInv = std::move(*it);
        ++it;
    }

    // If we did not reach the end of the xformOps vector, then there were
    // extra ops that did not match any of the expected ops.
    // This means that the xformOps vector isn't XformCommonAPI-compatible.
    if (it != xformOps.end())
    {
        // UTdebugPrint("Returned false at it != xformOps.end()");
        return false;
    }

    // Verify that translate pivot and inverse translate pivot are either both
    // present or both absent.
    if ((bool) p != (bool) pInv)
    {
        // UTdebugPrint("Returned false at (bool) p != (bool) pInv");
        return false;
    }

    // Verify that pivot rotate and inverse pivot rotate are either both
    // present or both absent.
    if ((bool) pr != (bool) prInv)
    {
        // UTdebugPrint("Returned false at (bool) pr != (bool) prInv");
        return false;
    }

    if (translateOp)
    {
        *translateOp = std::move(t);
    }

    if (pivotOp)
    {
        *pivotOp = std::move(p);
    }

    if (rotateOp)
    {
        *rotateOp = std::move(r);
    }

    if (pivotRotateOp)
    {
        *pivotRotateOp = std::move(pr);
    }

    if (shearOp)
    {
        *shearOp = std::move(sh);
    }

    if (scaleOp)
    {
        *scaleOp = std::move(s);
    }

    if (pivotInvOp)
    {
        *pivotInvOp = std::move(pInv);
    }

    if (pivotRotateInvOp)
    {
        *pivotRotateInvOp = std::move(prInv);
    }

    if (resetXformStack)
    {
        *resetXformStack = tempResetXformStack;
    }

    return true;
}

// Similar to _GetCommonXformOps, except also adds ops for any non-null out
// parameter whose op does not yet exist. If this returns true, then it
// guarantees that every op returned in an out-parameter is valid.
//
// When creating a rotate op and rotOrder is specified, then it will be used
// to choose the rotate op type (or to validate the existing rotate op type).
// If rotOrder is not specified, then a rotateXYZ op will be created (or
// any existing three-axis rotate returned).
static
UsdHoudiniHoudiniXformCommonAPI::Ops
_GetOrAddCommonXformOps(
    const UsdGeomXformable& xformable,
    const UsdHoudiniHoudiniXformCommonAPI::RotationOrder* rotOrder,
    bool createTranslate,
    bool createPivot,
    bool createPivotRotate,
    bool createRotate,
    bool createShear,
    bool createScale,
    bool createOrient)
{
    TRACE_FUNCTION();

    // Validate mutual exclusivity of Euler rotate and orient.
    if (createRotate && createOrient)
    {
        TF_CODING_ERROR("OpRotate and OpOrient are mutually exclusive");
        return UsdHoudiniHoudiniXformCommonAPI::Ops();
    }

    UsdGeomXformOp t, p, pr, r, s, sh, pInv, prInv;
    bool resetXformStack = false;
    if(!_GetCommonXformOps(xformable, &t, &p, &r, &pr, &sh, &s, &pInv, &prInv, &resetXformStack))
    {
        TF_WARN("Could not determine xform ops for incompatible xformable <%s>",
                xformable.GetPath().GetText());
        return UsdHoudiniHoudiniXformCommonAPI::Ops();
    }

    // If creating the rotate op and the rotate op already exists, we must check
    // that the existing rotation order matches the requested rotation order.
    // We do this first so that we can early-exit without modifying the xform
    // op order if we encounter an error.
    if (createRotate && r)
    {
        if (r.GetOpType() == UsdGeomXformOp::TypeOrient)
        {
            TF_CODING_ERROR("Cannot create Euler rotate op on prim <%s>: "
                "orient op already exists",
                xformable.GetPath().GetText());
            return UsdHoudiniHoudiniXformCommonAPI::Ops();
        }
        if (rotOrder)
        {
            const UsdHoudiniHoudiniXformCommonAPI::RotationOrder
                existingRotOrder =
                    UsdHoudiniHoudiniXformCommonAPI::
                        ConvertOpTypeToRotationOrder(r.GetOpType());
            if (existingRotOrder != *rotOrder)
            {
                TF_CODING_ERROR(
                    "Rotation order mismatch on prim <%s> (%s != %s)",
                    xformable.GetPath().GetText(),
                    TfEnum::GetName(*rotOrder).c_str(),
                    TfEnum::GetName(existingRotOrder).c_str());
                return UsdHoudiniHoudiniXformCommonAPI::Ops();
            }
        }
    }
    if (createOrient && r &&
        r.GetOpType() != UsdGeomXformOp::TypeOrient)
    {
        TF_CODING_ERROR("Cannot create orient op on prim <%s>: "
            "Euler rotate op already exists",
            xformable.GetPath().GetText());
        return UsdHoudiniHoudiniXformCommonAPI::Ops();
    }

    // Add ops if they were requested but the ops do not yet exist.
    bool addedOps = false;
    if (createTranslate && !t)
    {
        addedOps = true;
        t = xformable.AddTranslateOp();
        if (!TF_VERIFY(t))
        {
            return UsdHoudiniHoudiniXformCommonAPI::Ops();
        }
    }
    if (createPivot && !p)
    {
        addedOps = true;
        p = xformable.AddTranslateOp(
            UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot);
        pInv = xformable.AddTranslateOp(
            UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot,
            /* isInverseOp */ true);
        if (!TF_VERIFY(p && pInv))
        {
            return UsdHoudiniHoudiniXformCommonAPI::Ops();
        }
    }
    if (createPivotRotate && !pr)
    {
        addedOps = true;
        pr = xformable.AddXformOp(
             UsdGeomXformOp::TypeRotateXYZ,
             UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot);
        prInv = xformable.AddXformOp(
             UsdGeomXformOp::TypeRotateXYZ,
             UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot,
             /* isInverseOp */ true);
        if (!TF_VERIFY(pr && prInv))
        {
            return UsdHoudiniHoudiniXformCommonAPI::Ops();
        }
    }
    if (createRotate && !r)
    {
        addedOps = true;
        const UsdGeomXformOp::Type rotateOpType = rotOrder
            ? UsdHoudiniHoudiniXformCommonAPI::ConvertRotationOrderToOpType(*rotOrder)
            : UsdGeomXformOp::TypeRotateXYZ;
        r = xformable.AddXformOp(
            rotateOpType,
            UsdGeomXformOp::PrecisionFloat);
        if (!TF_VERIFY(r))
        {
            return UsdHoudiniHoudiniXformCommonAPI::Ops();
        }
    }
    if (createOrient && !r)
    {
        addedOps = true;
        r = xformable.AddOrientOp(UsdGeomXformOp::PrecisionFloat);
        if (!TF_VERIFY(r))
        {
            return UsdHoudiniHoudiniXformCommonAPI::Ops();
        }
    }
    if (createScale && !s)
    {
        addedOps = true;
        s = xformable.AddScaleOp();
        if (!TF_VERIFY(s))
        {
            return UsdHoudiniHoudiniXformCommonAPI::Ops();
        }
    }
    if (createShear && !sh)
    {
        addedOps = true;
        sh = xformable.AddTransformOp(
            UsdGeomXformOp::PrecisionDouble,
             TfToken("shear"));
        // UTdebugPrint("Shear added in _GetOrAddCommonXformOps");
        if (!TF_VERIFY(sh))
            return UsdHoudiniHoudiniXformCommonAPI::Ops();

    }

    bool hadPivotRotate = (pr || prInv);
    addedOps = addedOps || (hadPivotRotate && !createPivotRotate);
    // Only update the xform op order if we had to add new ops.
    if (addedOps)
    {
        std::vector<UsdGeomXformOp> newXformOps;
        if (createPivotRotate && pr)
        {
            if (p)
                newXformOps.push_back(p);

            newXformOps.push_back(pr);
            if (t)
                newXformOps.push_back(t);
        }
        else
        {
            if (t)
                newXformOps.push_back(t);
            if (p)
                newXformOps.push_back(p);
        }
        if (r)
            newXformOps.push_back(r);
        if (createShear && sh)
            newXformOps.push_back(sh);
        if (s)
            newXformOps.push_back(s);
        if (createPivotRotate && prInv)
            newXformOps.push_back(prInv);
        if (pInv)
            newXformOps.push_back(pInv);
        xformable.SetXformOpOrder(newXformOps, resetXformStack);
    }

    return UsdHoudiniHoudiniXformCommonAPI::Ops
    {
        std::move(t),
        std::move(p),
        std::move(pr),
        std::move(r),
        std::move(sh),
        std::move(s),
        std::move(pInv),
        std::move(prInv)
    };
}

bool
UsdHoudiniHoudiniXformCommonAPI::SetTranslate(
    const GfVec3d &translation,
    const UsdTimeCode time/*=UsdTimeCode::Default()*/) const
{
    // Can't set translate on an xformable with incompatible schema.
    Ops ops = CreateXformOps(OpTranslate);
    if (!ops.translateOp)
    {
        return false;
    }

    return ops.translateOp.Set(translation, time);
}

bool
UsdHoudiniHoudiniXformCommonAPI::SetPivot(
    const GfVec3f &pivot,
    const UsdTimeCode time/*=UsdTimeCode::Default()*/) const
{
    // Can't set pivot on an xformable with incompatible schema.
    Ops ops = CreateXformOps(OpPivot);
    if (!ops.pivotOp)
    {
        return false;
    }

    return ops.pivotOp.Set(pivot, time);
}

bool
UsdHoudiniHoudiniXformCommonAPI::SetPivotRotate(const GfVec3f &pivotRotate,
    const UsdTimeCode time/*=UsdTimeCode::Default()*/) const
{
    // Can't set pivot on an xformable with incompatible schema.
    Ops ops = CreateXformOps(OpPivotRotate);
    if (!ops.pivotRotateOp)
    {
        return false;
    }

    return ops.pivotRotateOp.Set(pivotRotate, time);
}

bool
UsdHoudiniHoudiniXformCommonAPI::SetRotate(
    const Rotation &rotation,
    const UsdTimeCode time/*=UsdTimeCode::Default()*/) const
{
    // Can't set rotate on an xformable with incompatible schema.
    if (rotation.IsOrient())
    {
        Ops ops = CreateXformOps(OpOrient);
        if (!ops.rotateOp)
        {
            return false;
        }
        return ops.rotateOp.Set(rotation.GetQuaternion(), time);
    }
    else
    {
        Ops ops = CreateXformOps(rotation.GetRotationOrder(), OpRotate);
        if (!ops.rotateOp)
        {
            return false;
        }
        return ops.rotateOp.Set(rotation.GetEulerAngles(), time);
    }
}

bool
UsdHoudiniHoudiniXformCommonAPI::SetShear(
    const GfVec3f &shear,
    const UsdTimeCode time/*=UsdTimeCode::Default()*/) const
{
    Ops ops = CreateXformOps(OpShear);
    if (!ops.shearOp)
    {
        return false;
    }

    UT_Matrix4D shearMatrix;
    shearMatrix.identity();
    shearMatrix.shear(shear[0], shear[1], shear[2]);

    return ops.shearOp.Set(GusdUT_Gf::Cast(shearMatrix), time);
}

bool
UsdHoudiniHoudiniXformCommonAPI::SetScale(
    const GfVec3f &scale,
    const UsdTimeCode time/*=UsdTimeCode::Default()*/) const
{
    // Can't set scale on an xformable with incompatible schema.
    Ops ops = CreateXformOps(OpScale);
    if (!ops.scaleOp)
    {
        return false;
    }

    return ops.scaleOp.Set(scale, time);
}

UsdHoudiniHoudiniXformCommonAPI::Ops
UsdHoudiniHoudiniXformCommonAPI::CreateXformOps(
    UsdHoudiniHoudiniXformCommonAPI::RotationOrder rotOrder,
    OpFlags op1,
    OpFlags op2,
    OpFlags op3,
    OpFlags op4,
    OpFlags op5,
    OpFlags op6
) const
{
    UsdGeomXformable xformable(GetPrim());
    if (!xformable)
    {
        return UsdHoudiniHoudiniXformCommonAPI::Ops();
    }

    const auto flags = op1 | op2 | op3 | op4 | op5 | op6;
    return _GetOrAddCommonXformOps(
        xformable,
        &rotOrder,
        flags & OpTranslate,
        flags & OpPivot,
        flags & OpPivotRotate,
        flags & OpRotate,
        flags & OpShear,
        flags & OpScale,
        flags & OpOrient);
}

UsdHoudiniHoudiniXformCommonAPI::Ops
UsdHoudiniHoudiniXformCommonAPI::CreateXformOps(
    OpFlags op1,
    OpFlags op2,
    OpFlags op3,
    OpFlags op4,
    OpFlags op5,
    OpFlags op6) const
{
    UsdGeomXformable xformable(GetPrim());
    if (!xformable)
    {
        return UsdHoudiniHoudiniXformCommonAPI::Ops();
    }

    const auto flags = op1 | op2 | op3 | op4 | op5 | op6;
    return _GetOrAddCommonXformOps(
        xformable,
        nullptr,
        flags & OpTranslate,
        flags & OpPivot,
        flags & OpPivotRotate,
        flags & OpRotate,
        flags & OpShear,
        flags & OpScale,
        flags & OpOrient);
}

/* static */
GfMatrix4d
UsdHoudiniHoudiniXformCommonAPI::GetRotationTransform(
    const GfVec3f &rotation,
    const UsdHoudiniHoudiniXformCommonAPI::RotationOrder rotationOrder)
{
    const UsdGeomXformOp::Type rotateOpType =
        UsdHoudiniHoudiniXformCommonAPI::ConvertRotationOrderToOpType(rotationOrder);
    return UsdGeomXformOp::GetOpTransform(rotateOpType, VtValue(rotation));
}

PXR_NAMESPACE_CLOSE_SCOPE
