//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniApexScene.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniApexScene)
{
    TfType::Define<UsdHoudiniHoudiniApexScene,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("HoudiniApexScene")
    // to find TfType<UsdHoudiniHoudiniApexScene>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, UsdHoudiniHoudiniApexScene>("HoudiniApexScene");
}

/* virtual */
UsdHoudiniHoudiniApexScene::~UsdHoudiniHoudiniApexScene()
{
}

/* static */
UsdHoudiniHoudiniApexScene
UsdHoudiniHoudiniApexScene::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniApexScene();
    }
    return UsdHoudiniHoudiniApexScene(stage->GetPrimAtPath(path));
}

/* static */
UsdHoudiniHoudiniApexScene
UsdHoudiniHoudiniApexScene::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("HoudiniApexScene");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniApexScene();
    }
    return UsdHoudiniHoudiniApexScene(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind UsdHoudiniHoudiniApexScene::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniApexScene::schemaKind;
}

/* static */
const TfType &
UsdHoudiniHoudiniApexScene::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniApexScene>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniApexScene::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniApexScene::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniApexScene::GetSceneFilesAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->sceneFiles);
}

UsdAttribute
UsdHoudiniHoudiniApexScene::CreateSceneFilesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->sceneFiles,
                       SdfValueTypeNames->AssetArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniApexScene::GetInheritAnimationLayersAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->inheritAnimationLayers);
}

UsdAttribute
UsdHoudiniHoudiniApexScene::CreateInheritAnimationLayersAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->inheritAnimationLayers,
                       SdfValueTypeNames->BoolArray,
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
UsdHoudiniHoudiniApexScene::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->sceneFiles,
        UsdHoudiniTokens->inheritAnimationLayers,
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
