//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./houdiniHairDeformAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
// SIDEFX NOTE: IF THIS LINE IS DIFFERENT FROM THE CODE IN GITHUB, MAKE SURE WE
// USE TF_REGISTRY_FUNCTION_WITH_TAG, NOT JUST TF_REGISTRY_FUNCTION.
// THIS IS TO FIX A VS2019 BUILD ISSUE (SEE r352600, r366226).
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, schemaClass_UsdHoudiniHoudiniHairDeformAPI)
{
    TfType::Define<UsdHoudiniHoudiniHairDeformAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdHoudiniHoudiniHairDeformAPI::~UsdHoudiniHoudiniHairDeformAPI()
{
}

/* static */
UsdHoudiniHoudiniHairDeformAPI
UsdHoudiniHoudiniHairDeformAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdHoudiniHoudiniHairDeformAPI();
    }
    return UsdHoudiniHoudiniHairDeformAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdHoudiniHoudiniHairDeformAPI::_GetSchemaKind() const
{
    return UsdHoudiniHoudiniHairDeformAPI::schemaKind;
}

/* static */
bool
UsdHoudiniHoudiniHairDeformAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdHoudiniHoudiniHairDeformAPI>(whyNot);
}

/* static */
UsdHoudiniHoudiniHairDeformAPI
UsdHoudiniHoudiniHairDeformAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdHoudiniHoudiniHairDeformAPI>()) {
        return UsdHoudiniHoudiniHairDeformAPI(prim);
    }
    return UsdHoudiniHoudiniHairDeformAPI();
}

/* static */
const TfType &
UsdHoudiniHoudiniHairDeformAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdHoudiniHoudiniHairDeformAPI>();
    return tfType;
}

/* static */
bool 
UsdHoudiniHoudiniHairDeformAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdHoudiniHoudiniHairDeformAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveShapeEnableAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveshapeenable);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveShapeEnableAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveshapeenable,
                       SdfValueTypeNames->Bool,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveShapeIterationsAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveshapeiterations);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveShapeIterationsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveshapeiterations,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveShapeLockRootsAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveshapelockroots);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveShapeLockRootsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveshapelockroots,
                       SdfValueTypeNames->Bool,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveShapeKStretchAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveshapekstretch);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveShapeKStretchAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveshapekstretch,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveShapeKBendAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveshapekbend);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveShapeKBendAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveshapekbend,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveShapeRefPosStrengthAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveshaperefposstrength);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveShapeRefPosStrengthAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveshaperefposstrength,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveClumpsStiffnessAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsstiffness);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveClumpsStiffnessAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsstiffness,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveClumpsDampingAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsdamping);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveClumpsDampingAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsdamping,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveClumpsEnableAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsenable);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveClumpsEnableAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsenable,
                       SdfValueTypeNames->Bool,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveClumpsMaxNeighborsAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsmaxneighbors);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveClumpsMaxNeighborsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsmaxneighbors,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetPreserveClumpsMaxConstraintsAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsmaxconstraints);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreatePreserveClumpsMaxConstraintsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformPreserveclumpsmaxconstraints,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetCaptureRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformCaptureradius);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateCaptureRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformCaptureradius,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetCaptureMaxPointsAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformCapturemaxpoints);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateCaptureMaxPointsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformCapturemaxpoints,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetCaptureMinPointsAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformCaptureminpoints);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateCaptureMinPointsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformCaptureminpoints,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetDeformMethodAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformDeformmethod);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateDeformMethodAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformDeformmethod,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetSmoothCaptureAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformSmoothcapture);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateSmoothCaptureAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformSmoothcapture,
                       SdfValueTypeNames->Bool,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetSmoothCaptureRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformSmoothcaptureradius);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateSmoothCaptureRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformSmoothcaptureradius,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetKernelTypeAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformKerneltype);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateKernelTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformKerneltype,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetSmoothingMethodAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformSmoothingmethod);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateSmoothingMethodAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformSmoothingmethod,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetSmoothingLevelAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformSmoothinglevel);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateSmoothingLevelAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformSmoothinglevel,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetTetMeshTreatmentAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformTetmeshtreatment);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateTetMeshTreatmentAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformTetmeshtreatment,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetUseOrientAttribAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformUseorientattrib);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateUseOrientAttribAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformUseorientattrib,
                       SdfValueTypeNames->Bool,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetOrientBlendAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformOrientblend);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateOrientBlendAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformOrientblend,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetGsiMaxCandidatesAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformGsimaxcandidates);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateGsiMaxCandidatesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformGsimaxcandidates,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetGsiSearchRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformGsisearchradius);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateGsiSearchRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformGsisearchradius,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetGsiNSamplesAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformGsinsamples);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateGsiNSamplesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformGsinsamples,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetGsiMinGuidesAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformGsiminguides);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateGsiMinGuidesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformGsiminguides,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetGsiMaxGuidesAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformGsimaxguides);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateGsiMaxGuidesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformGsimaxguides,
                       SdfValueTypeNames->Int,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetGsiSigmaScaleAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformGsisigmascale);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateGsiSigmaScaleAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformGsisigmascale,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetGsiWeightThresholdAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformGsiweightthreshold);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateGsiWeightThresholdAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformGsiweightthreshold,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::GetGsiLengthPenaltyScaleAttr() const
{
    return GetPrim().GetAttribute(UsdHoudiniTokens->houdiniHairdeformGsilengthpenaltyscale);
}

UsdAttribute
UsdHoudiniHoudiniHairDeformAPI::CreateGsiLengthPenaltyScaleAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdHoudiniTokens->houdiniHairdeformGsilengthpenaltyscale,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdRelationship
UsdHoudiniHoudiniHairDeformAPI::GetSkinPrimRel() const
{
    return GetPrim().GetRelationship(UsdHoudiniTokens->houdiniHairdeformSkinPrim);
}

UsdRelationship
UsdHoudiniHoudiniHairDeformAPI::CreateSkinPrimRel() const
{
    return GetPrim().CreateRelationship(UsdHoudiniTokens->houdiniHairdeformSkinPrim,
                       /* custom = */ false);
}

UsdRelationship
UsdHoudiniHoudiniHairDeformAPI::GetDeformerPrimRel() const
{
    return GetPrim().GetRelationship(UsdHoudiniTokens->houdiniHairdeformDeformerPrim);
}

UsdRelationship
UsdHoudiniHoudiniHairDeformAPI::CreateDeformerPrimRel() const
{
    return GetPrim().CreateRelationship(UsdHoudiniTokens->houdiniHairdeformDeformerPrim,
                       /* custom = */ false);
}

UsdRelationship
UsdHoudiniHoudiniHairDeformAPI::GetGuideInterpMeshPrimRel() const
{
    return GetPrim().GetRelationship(UsdHoudiniTokens->houdiniHairdeformGuideInterpMeshPrim);
}

UsdRelationship
UsdHoudiniHoudiniHairDeformAPI::CreateGuideInterpMeshPrimRel() const
{
    return GetPrim().CreateRelationship(UsdHoudiniTokens->houdiniHairdeformGuideInterpMeshPrim,
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
UsdHoudiniHoudiniHairDeformAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdHoudiniTokens->houdiniHairdeformPreserveshapeenable,
        UsdHoudiniTokens->houdiniHairdeformPreserveshapeiterations,
        UsdHoudiniTokens->houdiniHairdeformPreserveshapelockroots,
        UsdHoudiniTokens->houdiniHairdeformPreserveshapekstretch,
        UsdHoudiniTokens->houdiniHairdeformPreserveshapekbend,
        UsdHoudiniTokens->houdiniHairdeformPreserveshaperefposstrength,
        UsdHoudiniTokens->houdiniHairdeformPreserveclumpsstiffness,
        UsdHoudiniTokens->houdiniHairdeformPreserveclumpsdamping,
        UsdHoudiniTokens->houdiniHairdeformPreserveclumpsenable,
        UsdHoudiniTokens->houdiniHairdeformPreserveclumpsmaxneighbors,
        UsdHoudiniTokens->houdiniHairdeformPreserveclumpsmaxconstraints,
        UsdHoudiniTokens->houdiniHairdeformCaptureradius,
        UsdHoudiniTokens->houdiniHairdeformCapturemaxpoints,
        UsdHoudiniTokens->houdiniHairdeformCaptureminpoints,
        UsdHoudiniTokens->houdiniHairdeformDeformmethod,
        UsdHoudiniTokens->houdiniHairdeformSmoothcapture,
        UsdHoudiniTokens->houdiniHairdeformSmoothcaptureradius,
        UsdHoudiniTokens->houdiniHairdeformKerneltype,
        UsdHoudiniTokens->houdiniHairdeformSmoothingmethod,
        UsdHoudiniTokens->houdiniHairdeformSmoothinglevel,
        UsdHoudiniTokens->houdiniHairdeformTetmeshtreatment,
        UsdHoudiniTokens->houdiniHairdeformUseorientattrib,
        UsdHoudiniTokens->houdiniHairdeformOrientblend,
        UsdHoudiniTokens->houdiniHairdeformGsimaxcandidates,
        UsdHoudiniTokens->houdiniHairdeformGsisearchradius,
        UsdHoudiniTokens->houdiniHairdeformGsinsamples,
        UsdHoudiniTokens->houdiniHairdeformGsiminguides,
        UsdHoudiniTokens->houdiniHairdeformGsimaxguides,
        UsdHoudiniTokens->houdiniHairdeformGsisigmascale,
        UsdHoudiniTokens->houdiniHairdeformGsiweightthreshold,
        UsdHoudiniTokens->houdiniHairdeformGsilengthpenaltyscale,
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
