//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniCameraPlateAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniCameraPlateAPI)
{
    TfType::Define<UsdHoudiniHoudiniCameraPlateAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniCameraPlateAPI::~UsdHoudiniHoudiniCameraPlateAPI()
{
}

/* static */
UsdHoudiniHoudiniCameraPlateAPI
UsdHoudiniHoudiniCameraPlateAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniCameraPlateAPI();
    }
    return UsdHoudiniHoudiniCameraPlateAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniCameraPlateAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniCameraPlateAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniCameraPlateAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniCameraPlateAPI>(whyNot);
}

/* static */
UsdHoudiniHoudiniCameraPlateAPI
UsdHoudiniHoudiniCameraPlateAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniCameraPlateAPI>()) {
        return UsdHoudiniHoudiniCameraPlateAPI(prim);
    }
    return UsdHoudiniHoudiniCameraPlateAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniCameraPlateAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniCameraPlateAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniCameraPlateAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniCameraPlateAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniCameraPlateAPI::GetHoudiniBackgroundimageAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniBackgroundimage);
}

UsdAttribute
UsdHoudiniHoudiniCameraPlateAPI::CreateHoudiniBackgroundimageAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniBackgroundimage,
                       SdfValueTypeNames->Asset,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniCameraPlateAPI::GetHoudiniForegroundimageAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniForegroundimage);
}

UsdAttribute
UsdHoudiniHoudiniCameraPlateAPI::CreateHoudiniForegroundimageAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniForegroundimage,
                       SdfValueTypeNames->Asset,
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
UsdHoudiniHoudiniCameraPlateAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniBackgroundimage,
        UsdHoudiniTokens->houdiniForegroundimage,
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
