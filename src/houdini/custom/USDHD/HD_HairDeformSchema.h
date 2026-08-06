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

#pragma once

#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/schema.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

#define HAIRDEFORMSCHEMA_TOKENS \
    ((houdiniHairDeform, "houdini:hairdeform")) \
    (skinPrim) \
    (deformerPrim) \
    (guideInterpMeshPrim) \
    (preserveshapeenable) \
    (preserveshapeiterations) \
    (preserveshapelockroots) \
    (preserveshapekstretch) \
    (preserveshapekbend) \
    (preserveshaperefposstrength) \
    (preserveclumpsstiffness) \
    (preserveclumpsdamping) \
    (preserveclumpsenable) \
    (preserveclumpsmaxneighbors) \
    (preserveclumpsmaxconstraints) \
    (captureradius) \
    (capturemaxpoints) \
    (captureminpoints) \
    (smoothcapture) \
    (smoothcaptureradius) \
    (kerneltype) \
    (smoothingmethod) \
    (smoothinglevel) \
    (tetmeshtreatment) \
    (useorientattrib) \
    (orientblend) \
    (transform) \
    (moves) \
    (deformmethod) \
    (pointdeform) \
    (surfacedeform) \
    (guideinterpolationmesh) \
    (guideweights) \
    (guideshapeinterpolation) \
    (gsimaxcandidates) \
    (gsisearchradius) \
    (gsinsamples) \
    (gsiminguides) \
    (gsimaxguides) \
    (gsisigmascale) \
    (gsiweightthreshold) \
    (gsilengthpenaltyscale)

TF_DECLARE_PUBLIC_TOKENS(HairDeformSchemaTokens, HAIRDEFORMSCHEMA_TOKENS);

class HairDeformSchema : public HdSchema
{
public:
    HairDeformSchema(HdContainerDataSourceHandle container)
        : HdSchema(container)
    {
    }

    HdPathArrayDataSourceHandle GetSkinPrims();
    HdPathArrayDataSourceHandle GetDeformerPrims();
    HdPathArrayDataSourceHandle GetGuideInterpMeshPrims();
    HdRetainedTypedSampledDataSource<bool>::Handle GetPreserveShapeEnable();
    HdRetainedTypedSampledDataSource<int>::Handle GetPreserveShapeIterations();
    HdRetainedTypedSampledDataSource<bool>::Handle GetPreserveShapeLockRoots();
    HdRetainedTypedSampledDataSource<float>::Handle GetPreserveShapeKStretch();
    HdRetainedTypedSampledDataSource<float>::Handle GetPreserveShapeKBend();
    HdRetainedTypedSampledDataSource<float>::Handle GetPreserveShapeRefPosStrength();
    HdRetainedTypedSampledDataSource<float>::Handle GetPreserveClumpsStiffness();
    HdRetainedTypedSampledDataSource<float>::Handle GetPreserveClumpsDamping();
    HdRetainedTypedSampledDataSource<bool>::Handle GetPreserveClumpsEnable();
    HdRetainedTypedSampledDataSource<int>::Handle GetPreserveClumpsMaxNeighbors();
    HdRetainedTypedSampledDataSource<int>::Handle GetPreserveClumpsMaxConstraints();
    HdRetainedTypedSampledDataSource<float>::Handle GetCaptureRadius();
    HdRetainedTypedSampledDataSource<int>::Handle GetCaptureMaxPoints();
    HdRetainedTypedSampledDataSource<int>::Handle GetCaptureMinPoints();
    HdRetainedTypedSampledDataSource<bool>::Handle GetSmoothCapture();
    HdRetainedTypedSampledDataSource<float>::Handle GetSmoothCaptureRadius();
    HdRetainedTypedSampledDataSource<std::string>::Handle GetKernelType();
    HdRetainedTypedSampledDataSource<std::string>::Handle GetSmoothingMethod();
    HdRetainedTypedSampledDataSource<int>::Handle GetSmoothingLevel();
    HdRetainedTypedSampledDataSource<std::string>::Handle GetTetMeshTreatment();
    HdRetainedTypedSampledDataSource<bool>::Handle GetUseOrientAttrib();
    HdRetainedTypedSampledDataSource<float>::Handle GetOrientBlend();
    HdRetainedTypedSampledDataSource<std::string>::Handle GetDeformMethod();
    HdRetainedTypedSampledDataSource<int>::Handle GetGsiMaxCandidates();
    HdRetainedTypedSampledDataSource<float>::Handle GetGsiSearchRadius();
    HdRetainedTypedSampledDataSource<int>::Handle GetGsiNSamples();
    HdRetainedTypedSampledDataSource<int>::Handle GetGsiMinGuides();
    HdRetainedTypedSampledDataSource<int>::Handle GetGsiMaxGuides();
    HdRetainedTypedSampledDataSource<float>::Handle GetGsiSigmaScale();
    HdRetainedTypedSampledDataSource<float>::Handle GetGsiWeightThreshold();
    HdRetainedTypedSampledDataSource<float>::Handle GetGsiLengthPenaltyScale();

    static HairDeformSchema GetFromParent(
            const HdContainerDataSourceHandle &fromParentContainer);
};

PXR_NAMESPACE_CLOSE_SCOPE
