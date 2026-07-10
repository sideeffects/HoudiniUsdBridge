//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniCanvasCameraAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniCanvasCameraAPI)
{
    TfType::Define<UsdHoudiniHoudiniCanvasCameraAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniCanvasCameraAPI::~UsdHoudiniHoudiniCanvasCameraAPI()
{
}

/* static */
UsdHoudiniHoudiniCanvasCameraAPI
UsdHoudiniHoudiniCanvasCameraAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniCanvasCameraAPI();
    }
    return UsdHoudiniHoudiniCanvasCameraAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniCanvasCameraAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniCanvasCameraAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniCanvasCameraAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniCanvasCameraAPI>(whyNot);
}

/* static */
UsdHoudiniHoudiniCanvasCameraAPI
UsdHoudiniHoudiniCanvasCameraAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniCanvasCameraAPI>()) {
        return UsdHoudiniHoudiniCanvasCameraAPI(prim);
    }
    return UsdHoudiniHoudiniCanvasCameraAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniCanvasCameraAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniCanvasCameraAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniCanvasCameraAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniCanvasCameraAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniCanvasCameraAPI::GetHoudiniCanvasdistanceAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniCanvasdistance);
}

UsdAttribute
UsdHoudiniHoudiniCanvasCameraAPI::CreateHoudiniCanvasdistanceAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniCanvasdistance,
                       SdfValueTypeNames->Double,
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
UsdHoudiniHoudiniCanvasCameraAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniCanvasdistance,
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
