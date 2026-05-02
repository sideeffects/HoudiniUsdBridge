//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniApexShapeBindingAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniApexShapeBindingAPI)
{
    TfType::Define<UsdHoudiniHoudiniApexShapeBindingAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniApexShapeBindingAPI::~UsdHoudiniHoudiniApexShapeBindingAPI()
{
}

/* static */
UsdHoudiniHoudiniApexShapeBindingAPI
UsdHoudiniHoudiniApexShapeBindingAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniApexShapeBindingAPI();
    }
    TfToken name;
    if (!IsHoudiniApexShapeBindingAPIPath(path, &name)) {
        TF_CODING_ERROR("Invalid houdini:apex:shape path <%s>.", path.GetText());
        return UsdHoudiniHoudiniApexShapeBindingAPI();
    }
    return UsdHoudiniHoudiniApexShapeBindingAPI(stage->GetPrimAtPath(path.GetPrimPath()), name);
}

UsdHoudiniHoudiniApexShapeBindingAPI
UsdHoudiniHoudiniApexShapeBindingAPI::Get(const UsdPrim &prim, const TfToken &name)
{
    return UsdHoudiniHoudiniApexShapeBindingAPI(prim, name);
}

/* static */
std::vector<UsdHoudiniHoudiniApexShapeBindingAPI>
UsdHoudiniHoudiniApexShapeBindingAPI::GetAll(const UsdPrim &prim)
{
    std::vector<UsdHoudiniHoudiniApexShapeBindingAPI> schemas;
    
    for (const auto &schemaName :
         UsdAPISchemaBase::_GetMultipleApplyInstanceNames(prim, _GetStaticTfType())) {
        schemas.emplace_back(prim, schemaName);
    }

    return schemas;
}


/* static */
bool 
UsdHoudiniHoudiniApexShapeBindingAPI::IsSchemaPropertyBaseName(const TfToken &baseName)
{
    static TfTokenVector attrsAndRels = {
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Input),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Output),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Binding),
    };

    return find(attrsAndRels.begin(), attrsAndRels.end(), baseName)
            != attrsAndRels.end();
}

/* static */
bool
UsdHoudiniHoudiniApexShapeBindingAPI::IsHoudiniApexShapeBindingAPIPath(
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
        && tokens[0] == UsdHoudiniTokens->houdiniApexShape) {
        *name = TfToken(propertyName.substr(
           UsdHoudiniTokens->houdiniApexShape.GetString().size() + 1));
        return true;
    }

    return false;
}

/* virtual */
UsdSchemaKind UsdHoudiniHoudiniApexShapeBindingAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniApexShapeBindingAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniApexShapeBindingAPI::CanApply(
    const UsdPrim &prim, const TfToken &name, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniApexShapeBindingAPI>(name, whyNot);
}

/* static */
UsdHoudiniHoudiniApexShapeBindingAPI
UsdHoudiniHoudiniApexShapeBindingAPI::Apply(const UsdPrim &prim, const TfToken &name)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniApexShapeBindingAPI>(name)) {
        return UsdHoudiniHoudiniApexShapeBindingAPI(prim, name);
    }
    return UsdHoudiniHoudiniApexShapeBindingAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniApexShapeBindingAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniApexShapeBindingAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniApexShapeBindingAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniApexShapeBindingAPI::_GetTfType() const
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
UsdHoudiniHoudiniApexShapeBindingAPI::GetInputAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Input));
}

UsdAttribute
UsdHoudiniHoudiniApexShapeBindingAPI::CreateInputAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Input),
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniApexShapeBindingAPI::GetOutputAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Output));
}

UsdAttribute
UsdHoudiniHoudiniApexShapeBindingAPI::CreateOutputAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Output),
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdRelationship
UsdHoudiniHoudiniApexShapeBindingAPI::GetBindingRel() const
{
    return GetPrim().GetRelationship(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Binding));
}

UsdRelationship
UsdHoudiniHoudiniApexShapeBindingAPI::CreateBindingRel() const
{
    return GetPrim().CreateRelationship(
                       _GetNamespacedPropertyName(
                           GetName(),
                           UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Binding),
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
UsdHoudiniHoudiniApexShapeBindingAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Input,
        UsdHoudiniTokens->houdiniApexShape_MultipleApplyTemplate_Output,
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
UsdHoudiniHoudiniApexShapeBindingAPI::GetSchemaAttributeNames(
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
