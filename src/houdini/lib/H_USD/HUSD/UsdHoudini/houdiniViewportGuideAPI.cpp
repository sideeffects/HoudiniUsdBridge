//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniViewportGuideAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniViewportGuideAPI)
{
    TfType::Define<UsdHoudiniHoudiniViewportGuideAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniViewportGuideAPI::~UsdHoudiniHoudiniViewportGuideAPI()
{
}

/* static */
UsdHoudiniHoudiniViewportGuideAPI
UsdHoudiniHoudiniViewportGuideAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniViewportGuideAPI();
    }
    return UsdHoudiniHoudiniViewportGuideAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniViewportGuideAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniViewportGuideAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniViewportGuideAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniViewportGuideAPI>(whyNot);
}

/* static */
UsdHoudiniHoudiniViewportGuideAPI
UsdHoudiniHoudiniViewportGuideAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniViewportGuideAPI>()) {
        return UsdHoudiniHoudiniViewportGuideAPI(prim);
    }
    return UsdHoudiniHoudiniViewportGuideAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniViewportGuideAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniViewportGuideAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniViewportGuideAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniViewportGuideAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniViewportGuideAPI::GetHoudiniGuidescaleAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniGuidescale);
}

UsdAttribute
UsdHoudiniHoudiniViewportGuideAPI::CreateHoudiniGuidescaleAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniGuidescale,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniViewportGuideAPI::GetHoudiniInviewermenuAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniInviewermenu);
}

UsdAttribute
UsdHoudiniHoudiniViewportGuideAPI::CreateHoudiniInviewermenuAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniInviewermenu,
                       SdfValueTypeNames->Bool,
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
UsdHoudiniHoudiniViewportGuideAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniGuidescale,
        UsdHoudiniTokens->houdiniInviewermenu,
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
