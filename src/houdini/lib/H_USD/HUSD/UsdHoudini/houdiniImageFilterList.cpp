//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniImageFilterList.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniImageFilterList)
{
    TfType::Define<UsdHoudiniHoudiniImageFilterList,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("HoudiniImageFilterList")
    // to find TfType<UsdHoudiniHoudiniImageFilterList>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, UsdHoudiniHoudiniImageFilterList>("HoudiniImageFilterList");
}

/* virtual */
UsdHoudiniHoudiniImageFilterList::~UsdHoudiniHoudiniImageFilterList()
{
}

/* static */
UsdHoudiniHoudiniImageFilterList
UsdHoudiniHoudiniImageFilterList::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniImageFilterList();
    }
    return UsdHoudiniHoudiniImageFilterList(stage->GetPrimAtPath(path));
}

/* static */
UsdHoudiniHoudiniImageFilterList
UsdHoudiniHoudiniImageFilterList::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("HoudiniImageFilterList");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniImageFilterList();
    }
    return UsdHoudiniHoudiniImageFilterList(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind UsdHoudiniHoudiniImageFilterList::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniImageFilterList::schemaKind;
}

/* static */
const TfType &
UsdHoudiniHoudiniImageFilterList::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniImageFilterList>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniImageFilterList::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniImageFilterList::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::GetAOVsAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->aOVs);
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::CreateAOVsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->aOVs,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::GetAOVlistAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->aOVlist);
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::CreateAOVlistAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->aOVlist,
                       SdfValueTypeNames->StringArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::GetNormalscopeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->normalscope);
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::CreateNormalscopeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->normalscope,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::GetDepthscopeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->depthscope);
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::CreateDepthscopeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->depthscope,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::GetAlbedoscopeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->albedoscope);
}

UsdAttribute
UsdHoudiniHoudiniImageFilterList::CreateAlbedoscopeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->albedoscope,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdRelationship
UsdHoudiniHoudiniImageFilterList::GetOrderedFiltersRel() const
{
    return GetPrim().GetRelationship(UsdHoudiniTokens->orderedFilters);
}

UsdRelationship
UsdHoudiniHoudiniImageFilterList::CreateOrderedFiltersRel() const
{
    return GetPrim().CreateRelationship(UsdHoudiniTokens->orderedFilters,
                       /* custom = */ false);
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
UsdHoudiniHoudiniImageFilterList::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->aOVs,
        UsdHoudiniTokens->aOVlist,
        UsdHoudiniTokens->normalscope,
        UsdHoudiniTokens->depthscope,
        UsdHoudiniTokens->albedoscope,
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
