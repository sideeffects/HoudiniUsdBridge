//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniApexXformBindingAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniApexXformBindingAPI)
{
    TfType::Define<UsdHoudiniHoudiniApexXformBindingAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniApexXformBindingAPI::~UsdHoudiniHoudiniApexXformBindingAPI()
{
}

/* static */
UsdHoudiniHoudiniApexXformBindingAPI
UsdHoudiniHoudiniApexXformBindingAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniApexXformBindingAPI();
    }
    TfToken name;
    if (!IsHoudiniApexXformBindingAPIPath(path, &name)) {
        TF_CODING_ERROR("Invalid houdini:apex:xform path <%s>.", path.GetText());
        return UsdHoudiniHoudiniApexXformBindingAPI();
    }
    return UsdHoudiniHoudiniApexXformBindingAPI(stage->GetPrimAtPath(path.GetPrimPath()), name);
}

UsdHoudiniHoudiniApexXformBindingAPI
UsdHoudiniHoudiniApexXformBindingAPI::Get(const UsdPrim &prim, const TfToken &name)
{
    return UsdHoudiniHoudiniApexXformBindingAPI(prim, name);
}

/* static */
std::vector<UsdHoudiniHoudiniApexXformBindingAPI>
UsdHoudiniHoudiniApexXformBindingAPI::GetAll(const UsdPrim &prim)
{
    std::vector<UsdHoudiniHoudiniApexXformBindingAPI> schemas;
    
    for (const auto &schemaName :
         UsdAPISchemaBase::_GetMultipleApplyInstanceNames(prim, _GetStaticTfType())) {
        schemas.emplace_back(prim, schemaName);
    }

    return schemas;
}


/* static */
bool 
UsdHoudiniHoudiniApexXformBindingAPI::IsSchemaPropertyBaseName(const TfToken &baseName)
{
    static TfTokenVector attrsAndRels = {
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Output),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Joint),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Binding),
    };

    return find(attrsAndRels.begin(), attrsAndRels.end(), baseName)
            != attrsAndRels.end();
}

/* static */
bool
UsdHoudiniHoudiniApexXformBindingAPI::IsHoudiniApexXformBindingAPIPath(
    const SdfPath &path, TfToken *name)
{
    if (!path.IsPropertyPath()) {
        return false;
    }

    std::string propertyName = path.GetName();
    TfTokenVector tokens = SdfPath::TokenizeIdentifierAsTokens(propertyName);

    // The baseName of the  path can't be one of the 
    // schema properties. We should validate this in the creation (or apply)
    // API.
    TfToken baseName = *tokens.rbegin();
    if (IsSchemaPropertyBaseName(baseName)) {
        return false;
    }

    if (tokens.size() >= 2
        && tokens[0] == UsdHoudiniTokens->houdiniApexXform) {
        *name = TfToken(propertyName.substr(
           UsdHoudiniTokens->houdiniApexXform.GetString().size() + 1));
        return true;
    }

    return false;
}

/* virtual */
UsdSchemaKind UsdHoudiniHoudiniApexXformBindingAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniApexXformBindingAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniApexXformBindingAPI::CanApply(
    const UsdPrim &prim, const TfToken &name, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniApexXformBindingAPI>(name, whyNot);
}

/* static */
UsdHoudiniHoudiniApexXformBindingAPI
UsdHoudiniHoudiniApexXformBindingAPI::Apply(const UsdPrim &prim, const TfToken &name)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniApexXformBindingAPI>(name)) {
        return UsdHoudiniHoudiniApexXformBindingAPI(prim, name);
    }
    return UsdHoudiniHoudiniApexXformBindingAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniApexXformBindingAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniApexXformBindingAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniApexXformBindingAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniApexXformBindingAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

/// Returns the property name prefixed with the correct namespace prefix, which
/// is composed of the the API's propertyNamespacePrefix metadata and the
/// instance name of the API.
static inline
TfToken
_GetNamespacedPropertyName(const TfToken instanceName, const TfToken propName)
{
    return UsdSchemaRegistry::MakeMultipleApplyNameInstance(propName, instanceName);
}

UsdAttribute
UsdHoudiniHoudiniApexXformBindingAPI::GetOutputAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Output));
}

UsdAttribute
UsdHoudiniHoudiniApexXformBindingAPI::CreateOutputAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Output),
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniApexXformBindingAPI::GetJointAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Joint));
}

UsdAttribute
UsdHoudiniHoudiniApexXformBindingAPI::CreateJointAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Joint),
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdRelationship
UsdHoudiniHoudiniApexXformBindingAPI::GetBindingRel() const
{
    return GetPrim().GetRelationship(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Binding));
}

UsdRelationship
UsdHoudiniHoudiniApexXformBindingAPI::CreateBindingRel() const
{
    return GetPrim().CreateRelationship(
                       _GetNamespacedPropertyName(
                           GetName(),
                           UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Binding),
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
UsdHoudiniHoudiniApexXformBindingAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Output,
        UsdHoudiniTokens->houdiniApexXform_MultipleApplyTemplate_Joint,
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

/*static*/
TfTokenVector
UsdHoudiniHoudiniApexXformBindingAPI::GetSchemaAttributeNames(
    bool includeInherited, const TfToken &instanceName)
{
    const TfTokenVector &attrNames = GetSchemaAttributeNames(includeInherited);
    if (instanceName.IsEmpty()) {
        return attrNames;
    }
    TfTokenVector result;
    result.reserve(attrNames.size());
    for (const TfToken &attrName : attrNames) {
        result.push_back(
            UsdSchemaRegistry::MakeMultipleApplyNameInstance(attrName, instanceName));
    }
    return result;
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
