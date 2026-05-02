//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniMetaCurves.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniMetaCurves)
{
    TfType::Define<UsdHoudiniHoudiniMetaCurves,
        TfType::Bases< UsdGeomBasisCurves > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("HoudiniMetaCurves")
    // to find TfType<UsdHoudiniHoudiniMetaCurves>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, UsdHoudiniHoudiniMetaCurves>("HoudiniMetaCurves");
}

/* virtual */
UsdHoudiniHoudiniMetaCurves::~UsdHoudiniHoudiniMetaCurves()
{
}

/* static */
UsdHoudiniHoudiniMetaCurves
UsdHoudiniHoudiniMetaCurves::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniMetaCurves();
    }
    return UsdHoudiniHoudiniMetaCurves(stage->GetPrimAtPath(path));
}

/* static */
UsdHoudiniHoudiniMetaCurves
UsdHoudiniHoudiniMetaCurves::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("HoudiniMetaCurves");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniMetaCurves();
    }
    return UsdHoudiniHoudiniMetaCurves(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind UsdHoudiniHoudiniMetaCurves::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniMetaCurves::schemaKind;
}

/* static */
const TfType &
UsdHoudiniHoudiniMetaCurves::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniMetaCurves>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniMetaCurves::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniMetaCurves::_GetTfType() const
{
    return _GetStaticTfType();
}

/*static*/
const TfTokenVector&
UsdHoudiniHoudiniMetaCurves::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames;
    static TfTokenVector allNames =
        UsdGeomBasisCurves::GetSchemaAttributeNames(true);

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
