//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniLightBarnDoorAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniLightBarnDoorAPI)
{
    TfType::Define<UsdHoudiniHoudiniLightBarnDoorAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniLightBarnDoorAPI::~UsdHoudiniHoudiniLightBarnDoorAPI()
{
}

/* static */
UsdHoudiniHoudiniLightBarnDoorAPI
UsdHoudiniHoudiniLightBarnDoorAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniLightBarnDoorAPI();
    }
    return UsdHoudiniHoudiniLightBarnDoorAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniLightBarnDoorAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniLightBarnDoorAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniLightBarnDoorAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniLightBarnDoorAPI>(whyNot);
}

/* static */
UsdHoudiniHoudiniLightBarnDoorAPI
UsdHoudiniHoudiniLightBarnDoorAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniLightBarnDoorAPI>()) {
        return UsdHoudiniHoudiniLightBarnDoorAPI(prim);
    }
    return UsdHoudiniHoudiniLightBarnDoorAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniLightBarnDoorAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniLightBarnDoorAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniLightBarnDoorAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniLightBarnDoorAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::GetBarndoorleftAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->barndoorleft);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::CreateBarndoorleftAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->barndoorleft,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::GetBarndoorleftedgeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->barndoorleftedge);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::CreateBarndoorleftedgeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->barndoorleftedge,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::GetBarndoorrightAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->barndoorright);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::CreateBarndoorrightAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->barndoorright,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::GetBarndoorrightedgeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->barndoorrightedge);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::CreateBarndoorrightedgeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->barndoorrightedge,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::GetBarndoortopAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->barndoortop);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::CreateBarndoortopAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->barndoortop,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::GetBarndoortopedgeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->barndoortopedge);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::CreateBarndoortopedgeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->barndoortopedge,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::GetBarndoorbottomAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->barndoorbottom);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::CreateBarndoorbottomAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->barndoorbottom,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::GetBarndoorbottomedgeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->barndoorbottomedge);
}

UsdAttribute
UsdHoudiniHoudiniLightBarnDoorAPI::CreateBarndoorbottomedgeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->barndoorbottomedge,
                       SdfValueTypeNames->Float,
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
UsdHoudiniHoudiniLightBarnDoorAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->barndoorleft,
        UsdHoudiniTokens->barndoorleftedge,
        UsdHoudiniTokens->barndoorright,
        UsdHoudiniTokens->barndoorrightedge,
        UsdHoudiniTokens->barndoortop,
        UsdHoudiniTokens->barndoortopedge,
        UsdHoudiniTokens->barndoorbottom,
        UsdHoudiniTokens->barndoorbottomedge,
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
