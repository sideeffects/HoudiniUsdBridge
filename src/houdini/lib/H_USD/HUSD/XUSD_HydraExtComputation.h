/*
 * Copyright 2022 Side Effects Software Inc.
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
 *
 * Produced by:
 *	Haydn Keung
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraExtComputation.h (XUSD Library, C++)
 *
 * COMMENTS:
 */

#ifndef XUSD_HydraExtComputation_h
#define XUSD_HydraExtComputation_h

#include <pxr/imaging/hd/extComputation.h>
#include <pxr/base/tf/span.h>

#include <GT/GT_DataArray.h>
#include <UT/UT_BoundingBox.h>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_HydraExtComputation : public HdExtComputation
{
public:
    XUSD_HydraExtComputation(SdfPath const &id);

    ~XUSD_HydraExtComputation() override;

    void Sync(
            HdSceneDelegate *sceneDelegate,
            HdRenderParam *renderParam,
            HdDirtyBits *dirtyBits) override;

    // This function should be invoked
    // before calling getSkinningXformAttr(), getBoneIdxAttr(),
    // getBoneWeightAttr() or getRestPointsAttr()
    // since this method checks the dirty bits
    // and updates those attributes in addition
    // to returning whether we are performing
    // a skinning computation
    bool isSkinning(HdSceneDelegate *scene_delegate);

    // Similar to isSkinning, this method should
    // be invoked before getBlendShapeAttr()
    bool isBlendShape(HdSceneDelegate *scene_delegate);

    // Approximate bounds for the skinned result, if isSkinning() is true.
    // Otherwise, use the authored extents if there are only blendshapes.
    const UT_BoundingBox &getSkinnedBounds() const { return mySkinnedBounds; }

    const GT_DataArrayHandle &getSkinningXformAttr() const
            { return mySkinningXformAttr; }

    const GT_DataArrayHandle &getBlendShapeWeightAttr() const
            { return myBlendShapeWeightAttr; }

    const GT_DataArrayHandle &getBoneIdxAttr() const { return myBoneIdxAttr; }

    const GT_DataArrayHandle &getBoneWeightAttr() const
            { return myBoneWeightAttr; }

    const GT_DataArrayHandle &getRestPointsAttr() const
            { return myRestPointsAttr; }

    const GT_DataArrayHandle &getBlendShapeOffsetsAttr() const
            { return myBlendShapeOffsetsAttr; }

private:
    // Begining of methods and data for constructing
    // skinning data arrays.
    bool mySkinning = false;

    bool mySkinningCacheValid = false;
    bool myBlendShapeCacheValid = false;

    HdDirtyBits myCachedDirtyBits = Clean;

    // Scene Inputs
    GT_DataArrayHandle mySkinningXformAttr;
    GT_DataArrayHandle myBlendShapeWeightAttr;
    UT_BoundingBox mySkinnedBounds;

    // Computation Inputs
    GT_DataArrayHandle myBoneIdxAttr;
    GT_DataArrayHandle myBoneWeightAttr;
    GT_DataArrayHandle myRestPointsAttr;
    GT_DataArrayHandle myBlendShapeOffsetsAttr;

    // Determines if this computation node is a skinning operation
    bool hasSkinningComputation(HdSceneDelegate *scene_delegate);

    // Converts the skinning inputs from the format used by
    // HdExtComputationUtils::GetComputedPrimvarValues to a format that
    // is understood by the GLSL shaders
    void parseInfluences(
            const bool is_rigid_skinning,
            const int num_influences_per_comp,
            const int num_pnts_on_mesh,
            const VtArray<GfVec2f> &influence_arr);

    void parseSkinningXForms(
            const VtArray<GfMatrix4f> &x_forms,
            const GfMatrix4f &skel_to_prim_local);

    void parseBlendShapeWeights(const VtArray<fpreal32> &weights);

    // Checks if there are any dirty flags, if so invokes
    // fetchSceneInputs/fetchComputationInputs
    void buildDataArrays(HdSceneDelegate *scene_delegate);

    // Gets the scene inputs and store them in the source_comp
    void fetchSceneInputs(HdSceneDelegate *scene_delegate);

    // Gets the computation inputs and store them in the source_comp
    void fetchComputationInputs(HdSceneDelegate *scene_delegate);

    // apply the geo_xform onto the rest_points and assigns it to
    // myRestPointsAttr
    void setupRestPoints(
            const int num_pnts,
            const GfMatrix4f &geo_bind_xform,
            VtVec3fArray &rest_points);

    void setupBlendShapes(
            HdSceneDelegate *scene_delegate,
            const int num_pnts,
            TfSpan<const GfVec4f> blend_offsets,
            TfSpan<const GfVec2i> blend_ranges);

    // End of methods and data for constructing
    // skinning data arrays.
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HUSD_HydraComputation_h
