//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniViewportLightAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniViewportLightAPI)
{
    TfType::Define<UsdHoudiniHoudiniViewportLightAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniViewportLightAPI::~UsdHoudiniHoudiniViewportLightAPI()
{
}

/* static */
UsdHoudiniHoudiniViewportLightAPI
UsdHoudiniHoudiniViewportLightAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniViewportLightAPI();
    }
    return UsdHoudiniHoudiniViewportLightAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniViewportLightAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniViewportLightAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniViewportLightAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniViewportLightAPI>(whyNot);
}

/* static */
UsdHoudiniHoudiniViewportLightAPI
UsdHoudiniHoudiniViewportLightAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniViewportLightAPI>()) {
        return UsdHoudiniHoudiniViewportLightAPI(prim);
    }
    return UsdHoudiniHoudiniViewportLightAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniViewportLightAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniViewportLightAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniViewportLightAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniViewportLightAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniViewportLightAPI::GetHoudiniClippingRangeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniClippingRange);
}

UsdAttribute
UsdHoudiniHoudiniViewportLightAPI::CreateHoudiniClippingRangeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniClippingRange,
                       SdfValueTypeNames->Float2,
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
UsdHoudiniHoudiniViewportLightAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniClippingRange,
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
