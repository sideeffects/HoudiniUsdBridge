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

#include "HD_HairDeformSchema.h"

#include <pxr/base/trace/trace.h>
#include <pxr/imaging/hd/retainedDataSource.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HairDeformSchemaTokens, HAIRDEFORMSCHEMA_TOKENS);

HdPathArrayDataSourceHandle
HairDeformSchema::GetSkinPrims()
{
    return _GetTypedDataSource<HdPathArrayDataSource>(
            HairDeformSchemaTokens->skinPrim);
}

HdPathArrayDataSourceHandle
HairDeformSchema::GetDeformerPrims()
{
    return _GetTypedDataSource<HdPathArrayDataSource>(
            HairDeformSchemaTokens->deformerPrim);
}

HdPathArrayDataSourceHandle
HairDeformSchema::GetGuideInterpMeshPrims()
{
    return _GetTypedDataSource<HdPathArrayDataSource>(
            HairDeformSchemaTokens->guideInterpMeshPrim);
}

HdRetainedTypedSampledDataSource<bool>::Handle
HairDeformSchema::GetPreserveShapeEnable()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<bool>>(
            HairDeformSchemaTokens->preserveshapeenable);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetPreserveShapeIterations()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->preserveshapeiterations);
}

HdRetainedTypedSampledDataSource<bool>::Handle
HairDeformSchema::GetPreserveShapeLockRoots()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<bool>>(
            HairDeformSchemaTokens->preserveshapelockroots);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetPreserveShapeKStretch()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->preserveshapekstretch);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetPreserveShapeKBend()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->preserveshapekbend);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetPreserveShapeRefPosStrength()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->preserveshaperefposstrength);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetPreserveClumpsStiffness()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->preserveclumpsstiffness);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetPreserveClumpsDamping()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->preserveclumpsdamping);
}

HdRetainedTypedSampledDataSource<bool>::Handle
HairDeformSchema::GetPreserveClumpsEnable()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<bool>>(
            HairDeformSchemaTokens->preserveclumpsenable);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetPreserveClumpsMaxNeighbors()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->preserveclumpsmaxneighbors);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetPreserveClumpsMaxConstraints()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->preserveclumpsmaxconstraints);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetCaptureRadius()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->captureradius);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetCaptureMaxPoints()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->capturemaxpoints);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetCaptureMinPoints()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->captureminpoints);
}

HdRetainedTypedSampledDataSource<bool>::Handle
HairDeformSchema::GetSmoothCapture()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<bool>>(
            HairDeformSchemaTokens->smoothcapture);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetSmoothCaptureRadius()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->smoothcaptureradius);
}

HdRetainedTypedSampledDataSource<std::string>::Handle
HairDeformSchema::GetKernelType()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<std::string>>(
            HairDeformSchemaTokens->kerneltype);
}

HdRetainedTypedSampledDataSource<std::string>::Handle
HairDeformSchema::GetSmoothingMethod()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<std::string>>(
            HairDeformSchemaTokens->smoothingmethod);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetSmoothingLevel()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->smoothinglevel);
}

HdRetainedTypedSampledDataSource<std::string>::Handle
HairDeformSchema::GetTetMeshTreatment()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<std::string>>(
            HairDeformSchemaTokens->tetmeshtreatment);
}

HdRetainedTypedSampledDataSource<bool>::Handle
HairDeformSchema::GetUseOrientAttrib()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<bool>>(
            HairDeformSchemaTokens->useorientattrib);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetOrientBlend()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->orientblend);
}

HdRetainedTypedSampledDataSource<std::string>::Handle
HairDeformSchema::GetDeformMethod()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<std::string>>(
            HairDeformSchemaTokens->deformmethod);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetGsiMaxCandidates()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->gsimaxcandidates);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetGsiSearchRadius()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->gsisearchradius);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetGsiNSamples()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->gsinsamples);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetGsiMinGuides()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->gsiminguides);
}

HdRetainedTypedSampledDataSource<int>::Handle
HairDeformSchema::GetGsiMaxGuides()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<int>>(
            HairDeformSchemaTokens->gsimaxguides);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetGsiSigmaScale()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->gsisigmascale);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetGsiWeightThreshold()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->gsiweightthreshold);
}

HdRetainedTypedSampledDataSource<float>::Handle
HairDeformSchema::GetGsiLengthPenaltyScale()
{
    return _GetTypedDataSource<HdRetainedTypedSampledDataSource<float>>(
            HairDeformSchemaTokens->gsilengthpenaltyscale);
}

/*static*/
HairDeformSchema
HairDeformSchema::GetFromParent(
        const HdContainerDataSourceHandle &fromParentContainer)
{
    return HairDeformSchema(
            fromParentContainer
                    ? HdContainerDataSource::Cast(fromParentContainer->Get(
                            HairDeformSchemaTokens->houdiniHairDeform))
                    : nullptr);
}

PXR_NAMESPACE_CLOSE_SCOPE
