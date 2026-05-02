//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniApexCharacterAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniApexCharacterAPI)
{
    TfType::Define<UsdHoudiniHoudiniApexCharacterAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniApexCharacterAPI::~UsdHoudiniHoudiniApexCharacterAPI()
{
}

/* static */
UsdHoudiniHoudiniApexCharacterAPI
UsdHoudiniHoudiniApexCharacterAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniApexCharacterAPI();
    }
    return UsdHoudiniHoudiniApexCharacterAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniApexCharacterAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniApexCharacterAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniApexCharacterAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniApexCharacterAPI>(whyNot);
}

/* static */
UsdHoudiniHoudiniApexCharacterAPI
UsdHoudiniHoudiniApexCharacterAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniApexCharacterAPI>()) {
        return UsdHoudiniHoudiniApexCharacterAPI(prim);
    }
    return UsdHoudiniHoudiniApexCharacterAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniApexCharacterAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniApexCharacterAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniApexCharacterAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniApexCharacterAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniApexCharacterAPI::GetFilesAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniApexCharacterFiles);
}

UsdAttribute
UsdHoudiniHoudiniApexCharacterAPI::CreateFilesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniApexCharacterFiles,
                       SdfValueTypeNames->AssetArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniApexCharacterAPI::GetRigAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniApexCharacterRig);
}

UsdAttribute
UsdHoudiniHoudiniApexCharacterAPI::CreateRigAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniApexCharacterRig,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
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
UsdHoudiniHoudiniApexCharacterAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniApexCharacterFiles,
        UsdHoudiniTokens->houdiniApexCharacterRig,
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
