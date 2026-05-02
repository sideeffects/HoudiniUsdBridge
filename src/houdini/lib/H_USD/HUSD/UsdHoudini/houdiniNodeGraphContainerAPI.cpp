//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniNodeGraphContainerAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniNodeGraphContainerAPI)
{
    TfType::Define<UsdHoudiniHoudiniNodeGraphContainerAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniNodeGraphContainerAPI::~UsdHoudiniHoudiniNodeGraphContainerAPI()
{
}

/* static */
UsdHoudiniHoudiniNodeGraphContainerAPI
UsdHoudiniHoudiniNodeGraphContainerAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniNodeGraphContainerAPI();
    }
    return UsdHoudiniHoudiniNodeGraphContainerAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniNodeGraphContainerAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniNodeGraphContainerAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniNodeGraphContainerAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniNodeGraphContainerAPI>(whyNot);
}

/* static */
UsdHoudiniHoudiniNodeGraphContainerAPI
UsdHoudiniHoudiniNodeGraphContainerAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniNodeGraphContainerAPI>()) {
        return UsdHoudiniHoudiniNodeGraphContainerAPI(prim);
    }
    return UsdHoudiniHoudiniNodeGraphContainerAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniNodeGraphContainerAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniNodeGraphContainerAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniNodeGraphContainerAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniNodeGraphContainerAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::GetContainerInputPosAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniContainerInputPos);
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::CreateContainerInputPosAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniContainerInputPos,
                       SdfValueTypeNames->Float2,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::GetContainerOutputPosAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniContainerOutputPos);
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::CreateContainerOutputPosAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniContainerOutputPos,
                       SdfValueTypeNames->Float2,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::GetContainerInputDisplayColorAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniContainerInputDisplayColor);
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::CreateContainerInputDisplayColorAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniContainerInputDisplayColor,
                       SdfValueTypeNames->Color3f,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::GetContainerOutputDisplayColorAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniContainerOutputDisplayColor);
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::CreateContainerOutputDisplayColorAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniContainerOutputDisplayColor,
                       SdfValueTypeNames->Color3f,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::GetContainerWireStyleAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniContainerWireStyle);
}

UsdAttribute
UsdHoudiniHoudiniNodeGraphContainerAPI::CreateContainerWireStyleAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniContainerWireStyle,
                       SdfValueTypeNames->Token,
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
UsdHoudiniHoudiniNodeGraphContainerAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniContainerInputPos,
        UsdHoudiniTokens->houdiniContainerOutputPos,
        UsdHoudiniTokens->houdiniContainerInputDisplayColor,
        UsdHoudiniTokens->houdiniContainerOutputDisplayColor,
        UsdHoudiniTokens->houdiniContainerWireStyle,
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
