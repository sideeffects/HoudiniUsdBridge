//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniFieldAsset.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniFieldAsset)
{
    TfType::Define<UsdHoudiniHoudiniFieldAsset,
        TfType::Bases< UsdVolFieldAsset > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("HoudiniFieldAsset")
    // to find TfType<UsdHoudiniHoudiniFieldAsset>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, UsdHoudiniHoudiniFieldAsset>("HoudiniFieldAsset");
}

/* virtual */
UsdHoudiniHoudiniFieldAsset::~UsdHoudiniHoudiniFieldAsset()
{
}

/* static */
UsdHoudiniHoudiniFieldAsset
UsdHoudiniHoudiniFieldAsset::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniFieldAsset();
    }
    return UsdHoudiniHoudiniFieldAsset(stage->GetPrimAtPath(path));
}

/* static */
UsdHoudiniHoudiniFieldAsset
UsdHoudiniHoudiniFieldAsset::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("HoudiniFieldAsset");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniFieldAsset();
    }
    return UsdHoudiniHoudiniFieldAsset(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind UsdHoudiniHoudiniFieldAsset::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniFieldAsset::schemaKind;
}

/* static */
const TfType &
UsdHoudiniHoudiniFieldAsset::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniFieldAsset>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniFieldAsset::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniFieldAsset::_GetTfType() const
{
    return _GetStaticTfType();
}

/*static*/
const TfTokenVector&
UsdHoudiniHoudiniFieldAsset::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames;
    static TfTokenVector allNames =
        UsdVolFieldAsset::GetSchemaAttributeNames(true);

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
