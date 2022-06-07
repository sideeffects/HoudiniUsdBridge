//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "./houdiniCameraPlateAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"
#include "pxr/usd/usd/tokens.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE USE
// TF_REGISTRY_FUNCTION_WITH_TAG, JUST NOT TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniCameraPlateAPI)
{
    TfType::Define<UsdHoudiniHoudiniCameraPlateAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

// PLEASE DO NOT UNDO THIS CHANGE (moving of the line below)
// This fixes clang unused variable warnings
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
