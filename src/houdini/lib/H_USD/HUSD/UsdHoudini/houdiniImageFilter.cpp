//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniImageFilter.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniImageFilter)
{
    TfType::Define<UsdHoudiniHoudiniImageFilter,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("HoudiniImageFilter")
    // to find TfType<UsdHoudiniHoudiniImageFilter>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, UsdHoudiniHoudiniImageFilter>("HoudiniImageFilter");
}

/* virtual */
UsdHoudiniHoudiniImageFilter::~UsdHoudiniHoudiniImageFilter()
{
}

/* static */
UsdHoudiniHoudiniImageFilter
UsdHoudiniHoudiniImageFilter::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniImageFilter();
    }
    return UsdHoudiniHoudiniImageFilter(stage->GetPrimAtPath(path));
}

/* static */
UsdHoudiniHoudiniImageFilter
UsdHoudiniHoudiniImageFilter::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("HoudiniImageFilter");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniImageFilter();
    }
    return UsdHoudiniHoudiniImageFilter(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind UsdHoudiniHoudiniImageFilter::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniImageFilter::schemaKind;
}

/* static */
const TfType &
UsdHoudiniHoudiniImageFilter::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniImageFilter>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniImageFilter::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniImageFilter::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniImageFilter::GetTypeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->type);
}

UsdAttribute
UsdHoudiniHoudiniImageFilter::CreateTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->type,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniImageFilter::GetBypassAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->bypass);
}

UsdAttribute
UsdHoudiniHoudiniImageFilter::CreateBypassAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->bypass,
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
UsdHoudiniHoudiniImageFilter::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->type,
        UsdHoudiniTokens->bypass,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdTyped::GetSchemaAttributeNames(true),
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
