/*
 * Copyright 2025 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "HD_HairDeformAPIAdapter.h"
#include "HD_HairDeformSchema.h"

#include <HUSD/UsdHoudini/houdiniHairDeformAPI.h>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/imaging/hd/dataSourceTypeDefs.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usdImaging/usdImaging/dataSourceRelationship.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HD_HairDeformAPIAdapter)
{
    using Adapter = HD_HairDeformAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

namespace
{
auto& _tokens = HairDeformSchemaTokens;

static const HdDataSourceLocator theSkinPrimLoc(
        _tokens->houdiniHairDeform,
        _tokens->skinPrim);
static const HdDataSourceLocator thePreserveShapeEnableLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveshapeenable);
static const HdDataSourceLocator thePreserveShapeIterationsLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveshapeiterations);
static const HdDataSourceLocator thePreserveShapeLockRootsLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveshapelockroots);
static const HdDataSourceLocator thePreserveShapeKStretchLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveshapekstretch);
static const HdDataSourceLocator thePreserveShapeKBendLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveshapekbend);
static const HdDataSourceLocator thePreserveShapeRefPosStrengthLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveshaperefposstrength);
static const HdDataSourceLocator thePreserveClumpsStiffnessLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveclumpsstiffness);
static const HdDataSourceLocator thePreserveClumpsDampingLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveclumpsdamping);
static const HdDataSourceLocator thePreserveClumpsEnableLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveclumpsenable);
static const HdDataSourceLocator thePreserveClumpsMaxNeighborsLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveclumpsmaxneighbors);
static const HdDataSourceLocator thePreserveClumpsMaxConstraintsLoc(
        _tokens->houdiniHairDeform,
        _tokens->preserveclumpsmaxconstraints);
static const HdDataSourceLocator theCaptureRadiusLoc(
        _tokens->houdiniHairDeform,
        _tokens->captureradius);
static const HdDataSourceLocator theCaptureMaxPointsLoc(
        _tokens->houdiniHairDeform,
        _tokens->capturemaxpoints);
static const HdDataSourceLocator theCaptureMinPointsLoc(
        _tokens->houdiniHairDeform,
        _tokens->captureminpoints);
static const HdDataSourceLocator theDeformMethodLoc(
        _tokens->houdiniHairDeform,
        _tokens->deformmethod);
static const HdDataSourceLocator theSmoothCaptureLoc(
        _tokens->houdiniHairDeform,
        _tokens->smoothcapture);
static const HdDataSourceLocator theSmoothCaptureRadiusLoc(
        _tokens->houdiniHairDeform,
        _tokens->smoothcaptureradius);
static const HdDataSourceLocator theKernelTypeLoc(
        _tokens->houdiniHairDeform,
        _tokens->kerneltype);
static const HdDataSourceLocator theSmoothingMethodLoc(
        _tokens->houdiniHairDeform,
        _tokens->smoothingmethod);
static const HdDataSourceLocator theSmoothingLevelLoc(
        _tokens->houdiniHairDeform,
        _tokens->smoothinglevel);
static const HdDataSourceLocator theTetMeshTreatmentLoc(
        _tokens->houdiniHairDeform,
        _tokens->tetmeshtreatment);
static const HdDataSourceLocator theUseOrientAttribLoc(
        _tokens->houdiniHairDeform,
        _tokens->useorientattrib);
static const HdDataSourceLocator theOrientBlendLoc(
        _tokens->houdiniHairDeform,
        _tokens->orientblend);
static const HdDataSourceLocator theDeformerPrimLoc(
        _tokens->houdiniHairDeform,
        _tokens->deformerPrim);
static const HdDataSourceLocator theGuideInterpMeshPrimLoc(
        _tokens->houdiniHairDeform,
        _tokens->guideInterpMeshPrim);
static const HdDataSourceLocator theGsiMaxCandidatesLoc(
        _tokens->houdiniHairDeform,
        _tokens->gsimaxcandidates);
static const HdDataSourceLocator theGsiSearchRadiusLoc(
        _tokens->houdiniHairDeform,
        _tokens->gsisearchradius);
static const HdDataSourceLocator theGsiNSamplesLoc(
        _tokens->houdiniHairDeform,
        _tokens->gsinsamples);
static const HdDataSourceLocator theGsiMinGuidesLoc(
        _tokens->houdiniHairDeform,
        _tokens->gsiminguides);
static const HdDataSourceLocator theGsiMaxGuidesLoc(
        _tokens->houdiniHairDeform,
        _tokens->gsimaxguides);
static const HdDataSourceLocator theGsiSigmaScaleLoc(
        _tokens->houdiniHairDeform,
        _tokens->gsisigmascale);
static const HdDataSourceLocator theGsiWeightThresholdLoc(
        _tokens->houdiniHairDeform,
        _tokens->gsiweightthreshold);
static const HdDataSourceLocator theGsiLengthPenaltyScaleLoc(
        _tokens->houdiniHairDeform,
        _tokens->gsilengthpenaltyscale);

static const TfToken theSkinPrimToken(theSkinPrimLoc.GetString(":"));
static const TfToken thePreserveShapeEnableToken(
        thePreserveShapeEnableLoc.GetString(":"));
static const TfToken thePreserveShapeIterationsToken(
        thePreserveShapeIterationsLoc.GetString(":"));

} // namespace

class HairDeformDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(HairDeformDataSource);

    HairDeformDataSource(
            const UsdPrim& prim,
            const UsdImagingDataSourceStageGlobals& stageGlobals)
        : _api(prim), _stageGlobals(stageGlobals)
    {
    }

    TfTokenVector GetNames() override
    {
        TfTokenVector result;

        if (_api.GetSkinPrimRel())
            result.push_back(_tokens->skinPrim);

        if (_api.GetDeformerPrimRel())
            result.push_back(_tokens->deformerPrim);

        if (_api.GetGuideInterpMeshPrimRel())
            result.push_back(_tokens->guideInterpMeshPrim);

        if (_api.GetPreserveShapeEnableAttr())
            result.push_back(_tokens->preserveshapeenable);

        if (_api.GetPreserveShapeIterationsAttr())
            result.push_back(_tokens->preserveshapeiterations);

        if (_api.GetPreserveShapeLockRootsAttr())
            result.push_back(_tokens->preserveshapelockroots);

        if (_api.GetPreserveShapeKStretchAttr())
            result.push_back(_tokens->preserveshapekstretch);

        if (_api.GetPreserveShapeKBendAttr())
            result.push_back(_tokens->preserveshapekbend);

        if (_api.GetPreserveShapeRefPosStrengthAttr())
            result.push_back(_tokens->preserveshaperefposstrength);

        if (_api.GetPreserveClumpsStiffnessAttr())
            result.push_back(_tokens->preserveclumpsstiffness);

        if (_api.GetPreserveClumpsDampingAttr())
            result.push_back(_tokens->preserveclumpsdamping);

        if (_api.GetPreserveClumpsEnableAttr())
            result.push_back(_tokens->preserveclumpsenable);

        if (_api.GetPreserveClumpsMaxNeighborsAttr())
            result.push_back(_tokens->preserveclumpsmaxneighbors);

        if (_api.GetPreserveClumpsMaxConstraintsAttr())
            result.push_back(_tokens->preserveclumpsmaxconstraints);

        if (_api.GetCaptureRadiusAttr())
            result.push_back(_tokens->captureradius);

        if (_api.GetCaptureMaxPointsAttr())
            result.push_back(_tokens->capturemaxpoints);

        if (_api.GetCaptureMinPointsAttr())
            result.push_back(_tokens->captureminpoints);

        if (_api.GetDeformMethodAttr())
            result.push_back(_tokens->deformmethod);

        if (_api.GetSmoothCaptureAttr())
            result.push_back(_tokens->smoothcapture);

        if (_api.GetSmoothCaptureRadiusAttr())
            result.push_back(_tokens->smoothcaptureradius);

        if (_api.GetKernelTypeAttr())
            result.push_back(_tokens->kerneltype);

        if (_api.GetSmoothingMethodAttr())
            result.push_back(_tokens->smoothingmethod);

        if (_api.GetSmoothingLevelAttr())
            result.push_back(_tokens->smoothinglevel);

        if (_api.GetTetMeshTreatmentAttr())
            result.push_back(_tokens->tetmeshtreatment);

        if (_api.GetUseOrientAttribAttr())
            result.push_back(_tokens->useorientattrib);

        if (_api.GetOrientBlendAttr())
            result.push_back(_tokens->orientblend);

        if (_api.GetGsiMaxCandidatesAttr())
            result.push_back(_tokens->gsimaxcandidates);

        if (_api.GetGsiSearchRadiusAttr())
            result.push_back(_tokens->gsisearchradius);

        if (_api.GetGsiNSamplesAttr())
            result.push_back(_tokens->gsinsamples);

        if (_api.GetGsiMinGuidesAttr())
            result.push_back(_tokens->gsiminguides);

        if (_api.GetGsiMaxGuidesAttr())
            result.push_back(_tokens->gsimaxguides);

        if (_api.GetGsiSigmaScaleAttr())
            result.push_back(_tokens->gsisigmascale);

        if (_api.GetGsiWeightThresholdAttr())
            result.push_back(_tokens->gsiweightthreshold);

        if (_api.GetGsiLengthPenaltyScaleAttr())
            result.push_back(_tokens->gsilengthpenaltyscale);

        return result;
    }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == _tokens->skinPrim)
        {
            if (UsdRelationship rel = _api.GetSkinPrimRel())
                return UsdImagingDataSourceRelationship::New(
                        rel, _stageGlobals);
        }
        else if (name == _tokens->deformerPrim)
        {
            if (UsdRelationship rel = _api.GetDeformerPrimRel())
            {
                return UsdImagingDataSourceRelationship::New(
                        rel, _stageGlobals);
            }
        }
        else if (name == _tokens->guideInterpMeshPrim)
        {
            if (UsdRelationship rel = _api.GetGuideInterpMeshPrimRel())
            {
                return UsdImagingDataSourceRelationship::New(
                        rel, _stageGlobals);
            }
        }
        else if (name == _tokens->preserveshapeenable)
        {
            if (UsdAttribute attr = _api.GetPreserveShapeEnableAttr())
            {
                bool value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<bool>::New(value);
            }
        }
        else if (name == _tokens->preserveshapeiterations)
        {
            if (UsdAttribute attr = _api.GetPreserveShapeIterationsAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->preserveshapelockroots)
        {
            if (UsdAttribute attr = _api.GetPreserveShapeLockRootsAttr())
            {
                bool value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<bool>::New(value);
            }
        }
        else if (name == _tokens->preserveshapekstretch)
        {
            if (UsdAttribute attr = _api.GetPreserveShapeKStretchAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->preserveshapekbend)
        {
            if (UsdAttribute attr = _api.GetPreserveShapeKBendAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->preserveshaperefposstrength)
        {
            if (UsdAttribute attr = _api.GetPreserveShapeRefPosStrengthAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->preserveclumpsstiffness)
        {
            if (UsdAttribute attr = _api.GetPreserveClumpsStiffnessAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->preserveclumpsdamping)
        {
            if (UsdAttribute attr = _api.GetPreserveClumpsDampingAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->preserveclumpsenable)
        {
            if (UsdAttribute attr = _api.GetPreserveClumpsEnableAttr())
            {
                bool value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<bool>::New(value);
            }
        }
        else if (name == _tokens->preserveclumpsmaxneighbors)
        {
            if (UsdAttribute attr = _api.GetPreserveClumpsMaxNeighborsAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->preserveclumpsmaxconstraints)
        {
            if (UsdAttribute attr = _api.GetPreserveClumpsMaxConstraintsAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->captureradius)
        {
            if (UsdAttribute attr = _api.GetCaptureRadiusAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->capturemaxpoints)
        {
            if (UsdAttribute attr = _api.GetCaptureMaxPointsAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->captureminpoints)
        {
            if (UsdAttribute attr = _api.GetCaptureMinPointsAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->deformmethod)
        {
            if (UsdAttribute attr = _api.GetDeformMethodAttr())
            {
                std::string value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<std::string>::New(value);
            }
        }
        else if (name == _tokens->smoothcapture)
        {
            if (UsdAttribute attr = _api.GetSmoothCaptureAttr())
            {
                bool value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<bool>::New(value);
            }
        }
        else if (name == _tokens->smoothcaptureradius)
        {
            if (UsdAttribute attr = _api.GetSmoothCaptureRadiusAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->kerneltype)
        {
            if (UsdAttribute attr = _api.GetKernelTypeAttr())
            {
                std::string value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<std::string>::New(value);
            }
        }
        else if (name == _tokens->smoothingmethod)
        {
            if (UsdAttribute attr = _api.GetSmoothingMethodAttr())
            {
                std::string value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<std::string>::New(value);
            }
        }
        else if (name == _tokens->smoothinglevel)
        {
            if (UsdAttribute attr = _api.GetSmoothingLevelAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->tetmeshtreatment)
        {
            if (UsdAttribute attr = _api.GetTetMeshTreatmentAttr())
            {
                std::string value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<std::string>::New(value);
            }
        }
        else if (name == _tokens->useorientattrib)
        {
            if (UsdAttribute attr = _api.GetUseOrientAttribAttr())
            {
                bool value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<bool>::New(value);
            }
        }
        else if (name == _tokens->orientblend)
        {
            if (UsdAttribute attr = _api.GetOrientBlendAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->gsimaxcandidates)
        {
            if (UsdAttribute attr = _api.GetGsiMaxCandidatesAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->gsisearchradius)
        {
            if (UsdAttribute attr = _api.GetGsiSearchRadiusAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->gsinsamples)
        {
            if (UsdAttribute attr = _api.GetGsiNSamplesAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->gsiminguides)
        {
            if (UsdAttribute attr = _api.GetGsiMinGuidesAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->gsimaxguides)
        {
            if (UsdAttribute attr = _api.GetGsiMaxGuidesAttr())
            {
                int value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<int>::New(value);
            }
        }
        else if (name == _tokens->gsisigmascale)
        {
            if (UsdAttribute attr = _api.GetGsiSigmaScaleAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->gsiweightthreshold)
        {
            if (UsdAttribute attr = _api.GetGsiWeightThresholdAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        else if (name == _tokens->gsilengthpenaltyscale)
        {
            if (UsdAttribute attr = _api.GetGsiLengthPenaltyScaleAttr())
            {
                float value;
                attr.Get(&value);
                return HdRetainedTypedSampledDataSource<float>::New(value);
            }
        }
        return HdDataSourceBaseHandle();
    }

private:
    UsdHoudiniHoudiniHairDeformAPI _api;
    const UsdImagingDataSourceStageGlobals& _stageGlobals;
};

HdContainerDataSourceHandle
HD_HairDeformAPIAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& appliedInstanceName,
        const UsdImagingDataSourceStageGlobals& stageGlobals)
{
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty())
        return nullptr;

    UsdHoudiniHoudiniHairDeformAPI api(prim);

    if (!api)
        return HdContainerDataSourceHandle();

    return HdRetainedContainerDataSource::New(
            _tokens->houdiniHairDeform,
            HairDeformDataSource::New(prim, stageGlobals));
}

HdDataSourceLocatorSet
HD_HairDeformAPIAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& appliedInstanceName,
        TfTokenVector const& properties,
        const UsdImagingPropertyInvalidationType invalidationType)
{
    HdDataSourceLocatorSet locators;
    for (const TfToken& prop : properties)
    {
        if (prop == UsdHoudiniTokens->houdiniHairdeformSkinPrim)
            locators.append(theSkinPrimLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformDeformerPrim)
            locators.append(theDeformerPrimLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformGuideInterpMeshPrim)
            locators.append(theGuideInterpMeshPrimLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveshapeenable)
            locators.append(thePreserveShapeEnableLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveshapeiterations)
            locators.append(thePreserveShapeIterationsLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveshapelockroots)
            locators.append(thePreserveShapeLockRootsLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveshapekstretch)
            locators.append(thePreserveShapeKStretchLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveshapekbend)
            locators.append(thePreserveShapeKBendLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveshaperefposstrength)
            locators.append(thePreserveShapeRefPosStrengthLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveclumpsstiffness)
            locators.append(thePreserveClumpsStiffnessLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveclumpsdamping)
            locators.append(thePreserveClumpsDampingLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveclumpsenable)
            locators.append(thePreserveClumpsEnableLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveclumpsmaxneighbors)
            locators.append(thePreserveClumpsMaxNeighborsLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformPreserveclumpsmaxconstraints)
            locators.append(thePreserveClumpsMaxConstraintsLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformCaptureradius)
            locators.append(theCaptureRadiusLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformCapturemaxpoints)
            locators.append(theCaptureMaxPointsLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformCaptureminpoints)
            locators.append(theCaptureMinPointsLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformDeformmethod)
            locators.append(theDeformMethodLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformSmoothcapture)
            locators.append(theSmoothCaptureLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformSmoothcaptureradius)
            locators.append(theSmoothCaptureRadiusLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformKerneltype)
            locators.append(theKernelTypeLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformSmoothingmethod)
            locators.append(theSmoothingMethodLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformSmoothinglevel)
            locators.append(theSmoothingLevelLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformTetmeshtreatment)
            locators.append(theTetMeshTreatmentLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformUseorientattrib)
            locators.append(theUseOrientAttribLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformOrientblend)
            locators.append(theOrientBlendLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformGsimaxcandidates)
            locators.append(theGsiMaxCandidatesLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformGsisearchradius)
            locators.append(theGsiSearchRadiusLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformGsinsamples)
            locators.append(theGsiNSamplesLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformGsiminguides)
            locators.append(theGsiMinGuidesLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformGsimaxguides)
            locators.append(theGsiMaxGuidesLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformGsisigmascale)
            locators.append(theGsiSigmaScaleLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformGsiweightthreshold)
            locators.append(theGsiWeightThresholdLoc);
        else if (prop == UsdHoudiniTokens->houdiniHairdeformGsilengthpenaltyscale)
            locators.append(theGsiLengthPenaltyScaleLoc);
    }

    return locators;
}

PXR_NAMESPACE_CLOSE_SCOPE
