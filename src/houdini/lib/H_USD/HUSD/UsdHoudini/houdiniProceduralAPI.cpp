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
#include "./houdiniProceduralAPI.h"
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
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniProceduralAPI)
{
    TfType::Define<UsdHoudiniHoudiniProceduralAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

// PLEASE DO NOT UNDO THIS CHANGE (moving of the line below)
// This fixes clang unused variable warnings
TF_DEFINE_PRIVATE_TOKENS(
    _schemaTokens,
    (HoudiniProceduralAPI)
    (houdiniProcedural)
);

// PLEASE DO NOT UNDO THIS CHANGE (moving of the line below)
/* virtual */
UsdHoudiniHoudiniProceduralAPI::~UsdHoudiniHoudiniProceduralAPI()
{
}

/* static */
UsdHoudiniHoudiniProceduralAPI
UsdHoudiniHoudiniProceduralAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniProceduralAPI();
    }
    TfToken name;
    if (!IsHoudiniProceduralAPIPath(path, &name)) {
        TF_CODING_ERROR("Invalid houdiniProcedural path <%s>.", path.GetText());
        return UsdHoudiniHoudiniProceduralAPI();
    }
    return UsdHoudiniHoudiniProceduralAPI(stage->GetPrimAtPath(path.GetPrimPath()), name);
}

UsdHoudiniHoudiniProceduralAPI
UsdHoudiniHoudiniProceduralAPI::Get(const UsdPrim &prim, const TfToken &name)
{
    return UsdHoudiniHoudiniProceduralAPI(prim, name);
}

//-----------------------------------------------------------------------------
// NOTE: SIDEFX: The following is a custom change and should not be removed
//               when merging new versions of this file from the USD codebase
//               until https://github.com/PixarAnimationStudios/USD/pull/1773
//               is resolved.
/* static */
std::vector<UsdHoudiniHoudiniProceduralAPI>
UsdHoudiniHoudiniProceduralAPI::GetAll(const UsdPrim &prim)
{
    std::vector<UsdHoudiniHoudiniProceduralAPI> schemas;

    auto appliedSchemas = prim.GetAppliedSchemas();
    if (appliedSchemas.empty()) {
        return schemas;
    }

    for (const auto &appliedSchema : appliedSchemas) {
        const std::string schemaPrefix = std::string("HoudiniProceduralAPI") +
                                         UsdObject::GetNamespaceDelimiter();
        if (TfStringStartsWith(appliedSchema, schemaPrefix)) {
            const std::string schemaName =
                    appliedSchema.GetString().substr(schemaPrefix.size());
            schemas.emplace_back(prim, TfToken(schemaName));
        }
    }

    return schemas;
}
//-----------------------------------------------------------------------------


/* static */
bool 
UsdHoudiniHoudiniProceduralAPI::IsSchemaPropertyBaseName(const TfToken &baseName)
{
    static TfTokenVector attrsAndRels = {
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralPath),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralArgs),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniActive),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniPriority),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniAnimated),
    };

    return find(attrsAndRels.begin(), attrsAndRels.end(), baseName)
            != attrsAndRels.end();
}

/* static */
bool
UsdHoudiniHoudiniProceduralAPI::IsHoudiniProceduralAPIPath(
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
        && tokens[0] == _schemaTokens->houdiniProcedural) {
        *name = TfToken(propertyName.substr(
            _schemaTokens->houdiniProcedural.GetString().size() + 1));
        return true;
    }

    return false;
}

/* virtual */
UsdSchemaKind UsdHoudiniHoudiniProceduralAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniProceduralAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniProceduralAPI::CanApply(
    const UsdPrim &prim, const TfToken &name, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniProceduralAPI>(name, whyNot);
}

/* static */
UsdHoudiniHoudiniProceduralAPI
UsdHoudiniHoudiniProceduralAPI::Apply(const UsdPrim &prim, const TfToken &name)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniProceduralAPI>(name)) {
        return UsdHoudiniHoudiniProceduralAPI(prim, name);
    }
    return UsdHoudiniHoudiniProceduralAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniProceduralAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniProceduralAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniProceduralAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniProceduralAPI::_GetTfType() const
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
UsdHoudiniHoudiniProceduralAPI::GetHoudiniProceduralPathAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralPath));
}

UsdAttribute
UsdHoudiniHoudiniProceduralAPI::CreateHoudiniProceduralPathAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralPath),
                       SdfValueTypeNames->Asset,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniProceduralAPI::GetHoudiniProceduralArgsAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralArgs));
}

UsdAttribute
UsdHoudiniHoudiniProceduralAPI::CreateHoudiniProceduralArgsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralArgs),
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniProceduralAPI::GetHoudiniActiveAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniActive));
}

UsdAttribute
UsdHoudiniHoudiniProceduralAPI::CreateHoudiniActiveAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniActive),
                       SdfValueTypeNames->Bool,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniProceduralAPI::GetHoudiniPriorityAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniPriority));
}

UsdAttribute
UsdHoudiniHoudiniProceduralAPI::CreateHoudiniPriorityAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniPriority),
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniProceduralAPI::GetHoudiniAnimatedAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniAnimated));
}

UsdAttribute
UsdHoudiniHoudiniProceduralAPI::CreateHoudiniAnimatedAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniAnimated),
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
UsdHoudiniHoudiniProceduralAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralPath,
        UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniProceduralArgs,
        UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniActive,
        UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniPriority,
        UsdHoudiniTokens->houdiniProcedural_MultipleApplyTemplate_HoudiniAnimated,
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
UsdHoudiniHoudiniProceduralAPI::GetSchemaAttributeNames(
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
