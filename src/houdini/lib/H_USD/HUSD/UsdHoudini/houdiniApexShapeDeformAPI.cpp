//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniApexShapeDeformAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniApexShapeDeformAPI)
{
    TfType::Define<UsdHoudiniHoudiniApexShapeDeformAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniApexShapeDeformAPI::~UsdHoudiniHoudiniApexShapeDeformAPI()
{
}

/* static */
UsdHoudiniHoudiniApexShapeDeformAPI
UsdHoudiniHoudiniApexShapeDeformAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniApexShapeDeformAPI();
    }
    return UsdHoudiniHoudiniApexShapeDeformAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniApexShapeDeformAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniApexShapeDeformAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniApexShapeDeformAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniApexShapeDeformAPI>(whyNot);
}

/* static */
UsdHoudiniHoudiniApexShapeDeformAPI
UsdHoudiniHoudiniApexShapeDeformAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniApexShapeDeformAPI>()) {
        return UsdHoudiniHoudiniApexShapeDeformAPI(prim);
    }
    return UsdHoudiniHoudiniApexShapeDeformAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniApexShapeDeformAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniApexShapeDeformAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniApexShapeDeformAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniApexShapeDeformAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniApexShapeDeformAPI::GetJointsAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniApexDeformJoints);
}

UsdAttribute
UsdHoudiniHoudiniApexShapeDeformAPI::CreateJointsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniApexDeformJoints,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniApexShapeDeformAPI::GetJointIndicesAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->primvarsHoudiniApexDeformJointIndices);
}

UsdAttribute
UsdHoudiniHoudiniApexShapeDeformAPI::CreateJointIndicesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->primvarsHoudiniApexDeformJointIndices,
                       SdfValueTypeNames->IntArray,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniApexShapeDeformAPI::GetJointWeightsAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->primvarsHoudiniApexDeformJointWeights);
}

UsdAttribute
UsdHoudiniHoudiniApexShapeDeformAPI::CreateJointWeightsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->primvarsHoudiniApexDeformJointWeights,
                       SdfValueTypeNames->FloatArray,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

namespace {
static inline TfTokenVector
_ConcatenateAttributeNames(const TfTokenVector& left,const TfTokenVector& right)
{
    TfTokenVector result;
    result.reserve(left.size() + right.size());
    result.insert(result.end(), left.begin(), left.end());
    result.insert(result.end(), right.begin(), right.end());
    return result;
}
}

/*static*/
const TfTokenVector&
UsdHoudiniHoudiniApexShapeDeformAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniApexDeformJoints,
        UsdHoudiniTokens->primvarsHoudiniApexDeformJointIndices,
        UsdHoudiniTokens->primvarsHoudiniApexDeformJointWeights,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdAPISchemaBase::GetSchemaAttributeNames(true),
            localNames);

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
