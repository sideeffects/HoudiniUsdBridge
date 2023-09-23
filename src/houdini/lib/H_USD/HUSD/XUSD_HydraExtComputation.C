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
 * NAME:	XUSD_HydraExtComputation.C (XUSD Library, C++)
 *
 * COMMENTS:
 */

#include "XUSD_HydraExtComputation.h"
#include "XUSD_HydraUtils.h"

#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/usdImaging/usdImaging/delegate.h>

#include <GT/GT_DAConstant.h>
#include <GT/GT_DANumeric.h>
#include <GT/GT_DAValues.h>
#include <GT/GT_DAVaryingArray.h>
#include <GT/GT_DeformUtils.h>
#include <gusd/UT_Gf.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _skinningTokens,
    // Scene Input Names
    (primWorldToLocal)
    (blendShapeWeights)
    (skinningXforms)
    (skelLocalToWorld)

    // Computation Input Names
    (restPoints)
    (geomBindXform)
    (influences)
    (numInfluencesPerComponent)
    (hasConstantInfluences)
    (blendShapeOffsets)
    (blendShapeOffsetRanges)
    (numBlendShapeOffsetRanges)

    // Computation Output Names
    (skinnedPoints)
);

XUSD_HydraExtComputation::XUSD_HydraExtComputation(SdfPath const &id)
    : HdExtComputation(id)
{
    mySkinnedBounds.initBounds();
}

XUSD_HydraExtComputation::~XUSD_HydraExtComputation() = default;

void
XUSD_HydraExtComputation::Sync(
        HdSceneDelegate *scene_delegate,
        HdRenderParam *render_param,
        HdDirtyBits *dirty_bits)
{
    HD_TRACE_FUNCTION();
    // We copy the dirty bits since the HdExtComputation::Sync will clear it
    myCachedDirtyBits |= *dirty_bits;
    HdExtComputation::Sync(scene_delegate, render_param, dirty_bits);
}

bool
XUSD_HydraExtComputation::hasSkinningComputation(
        HdSceneDelegate *scene_delegate)
{
    if (mySkinning || !(myCachedDirtyBits & (DirtyInputDesc | DirtyOutputDesc)))
        return mySkinning;

    const UT_Set<TfToken> complete_set{
            _skinningTokens->primWorldToLocal,
            _skinningTokens->blendShapeWeights,
            _skinningTokens->skinningXforms,
            _skinningTokens->skelLocalToWorld,
            _skinningTokens->restPoints,
            _skinningTokens->geomBindXform,
            _skinningTokens->influences,
            _skinningTokens->numInfluencesPerComponent,
            _skinningTokens->hasConstantInfluences,
            _skinningTokens->blendShapeOffsets,
            _skinningTokens->blendShapeOffsetRanges,
            _skinningTokens->numBlendShapeOffsetRanges,
            _skinningTokens->skinnedPoints};
    UT_Set<TfToken> cur_set;

    // Inputs Scene Names
    for (const TfToken &token : GetSceneInputNames())
    {
        cur_set.insert(token);
    }

    // Input Computation Names
    for (const HdExtComputationInputDescriptor &comp_input :
         GetComputationInputs())
    {
        cur_set.insert(comp_input.name);
    }

    // Outputs Computation Names
    for (const TfToken &token : GetOutputNames())
    {
        cur_set.insert(token);
    }

    myCachedDirtyBits &= ~(DirtyInputDesc | DirtyOutputDesc);
    if (complete_set == cur_set)
    {
        mySkinning = true;
        return mySkinning;
    }
    UT_ASSERT_MSG(
            cur_set.empty() || complete_set == cur_set,
            "There may have been a change to the USD skinning in/outputs");
    mySkinning = false;
    return mySkinning;
}

bool
XUSD_HydraExtComputation::isSkinning(HdSceneDelegate *scene_delegate)
{
    if (hasSkinningComputation(scene_delegate))
    {
        buildDataArrays(scene_delegate);
        return mySkinningCacheValid;
    }
    return false;
}

bool
XUSD_HydraExtComputation::isBlendShape(HdSceneDelegate *scene_delegate)
{
    if (hasSkinningComputation(scene_delegate))
    {
        buildDataArrays(scene_delegate);
        return myBlendShapeCacheValid;
    }
    return false;
}

void
XUSD_HydraExtComputation::buildDataArrays(HdSceneDelegate *scene_delegate)
{
    if (myCachedDirtyBits & DirtyCompInput)
        fetchComputationInputs(scene_delegate);

    if (myCachedDirtyBits & DirtySceneInput)
        fetchSceneInputs(scene_delegate);

    myCachedDirtyBits &= ~(DirtySceneInput | DirtyCompInput);
}

void
XUSD_HydraExtComputation::fetchSceneInputs(HdSceneDelegate *scene_delegate)
{
    const SdfPath &sprim_id = GetId();

    myBlendShapeWeightAttr.reset();
    VtValue val_blend_shape_weights = scene_delegate->GetExtComputationInput(
            sprim_id, _skinningTokens->blendShapeWeights);
    if (val_blend_shape_weights.IsEmpty())
        myBlendShapeCacheValid = false;
    else
    {
        UT_ASSERT(val_blend_shape_weights.IsHolding<VtArray<fpreal32>>());
        parseBlendShapeWeights(
                val_blend_shape_weights.UncheckedGet<VtArray<fpreal32>>());
    }

    mySkinningXformAttr.reset();
    VtValue prim_world_to_local = scene_delegate->GetExtComputationInput(
            sprim_id, _skinningTokens->primWorldToLocal);
    VtValue skel_local_to_world = scene_delegate->GetExtComputationInput(
            sprim_id, _skinningTokens->skelLocalToWorld);
    if (prim_world_to_local.IsEmpty() || skel_local_to_world.IsEmpty())
    {
        mySkinningCacheValid = false;
        return;
    }

    UT_ASSERT(prim_world_to_local.IsHolding<GfMatrix4d>());
    UT_ASSERT(skel_local_to_world.IsHolding<GfMatrix4d>());

    GfMatrix4f skel_to_prim_local = GfMatrix4f(
            skel_local_to_world.UncheckedGet<GfMatrix4d>()
            * prim_world_to_local.UncheckedGet<GfMatrix4d>());

    VtValue val_skinning_xforms = scene_delegate->GetExtComputationInput(
            sprim_id, _skinningTokens->skinningXforms);
    if (val_skinning_xforms.IsEmpty())
    {
        mySkinningCacheValid = false;
        return;
    }
    else
    {
        UT_ASSERT(val_skinning_xforms.IsHolding<VtArray<GfMatrix4f>>());
        parseSkinningXForms(
                val_skinning_xforms.UncheckedGet<VtArray<GfMatrix4f>>(),
                skel_to_prim_local);
    }
}

void
XUSD_HydraExtComputation::setupRestPoints(
        const int num_pnts,
        const GfMatrix4f &geo_bind_xform,
        VtVec3fArray &rest_points)
{
    auto data_arr = UTmakeIntrusive<GT_DANumeric<fpreal32>>(
            rest_points.data()->data(), num_pnts, 3, GT_TYPE_POINT);
    data_arr->setDataId((XUSD_HydraUtils::newDataId()));

    fpreal32 *dst = data_arr->data();

    UT_BlockedRange<int> range(0, num_pnts);
    UTparallelForLightItems(
            range,
            [&](const UT_BlockedRange<int> &range)
            {
                for (int i = range.begin(); i != range.end(); ++i)
                {
                    GfVec3f transformed_pt
                            = geo_bind_xform.TransformAffine(rest_points[i]);
                    dst[i * 3] = transformed_pt[0];
                    dst[i * 3 + 1] = transformed_pt[1];
                    dst[i * 3 + 2] = transformed_pt[2];
                }
            });
    myRestPointsAttr = data_arr;
}

void
XUSD_HydraExtComputation::setupBlendShapes(
        HdSceneDelegate *scene_delegate,
        const int num_pnts,
        TfSpan<const GfVec4f> blend_offsets,
        TfSpan<const GfVec2i> blend_ranges)
{
    auto weights_arr = scene_delegate->GetExtComputationInput(
            GetId(), _skinningTokens->blendShapeWeights);

    UT_ASSERT(
            !weights_arr.IsEmpty()
            && weights_arr.IsHolding<VtArray<fpreal32>>());

    int num_target_shapes
            = weights_arr.UncheckedGet<VtArray<fpreal32>>().size();

    auto blend_shape_arr = UTmakeIntrusive<GT_DANumeric<fpreal32>>(
            num_pnts, 3 * num_target_shapes);
    blend_shape_arr->setDataId(XUSD_HydraUtils::newDataId());

    memset(blend_shape_arr->data(), 0,
           sizeof(fpreal32) * num_pnts * 3 * num_target_shapes);

    int end = std::min((int)blend_ranges.size(), num_pnts);
    UT_ASSERT(end <= num_pnts);
    UT_BlockedRange<int> range(0, end);
    UTparallelForLightItems(
            range,
            [&](const UT_BlockedRange<int> &range)
            {
                for (int point = range.begin(); point != range.end(); ++point)
                {
                    const GfVec2f range = blend_ranges[point];
                    for (int j = range[0]; j < range[1]; ++j)
                    {
                        UT_ASSERT(j < blend_offsets.size());
                        const GfVec4f &offset = blend_offsets[j];
                        const int shape_idx = static_cast<int>(offset[3]);
                        fpreal32 *dst = blend_shape_arr->data()
                                        + point * 3 * num_target_shapes
                                        + 3 * shape_idx;
                        dst[0] = offset[0];
                        dst[1] = offset[1];
                        dst[2] = offset[2];
                    }
                }
            });
    myBlendShapeOffsetsAttr = blend_shape_arr;
}

void
XUSD_HydraExtComputation::fetchComputationInputs(
        HdSceneDelegate *scene_delegate)
{
    myBoneIdxAttr.reset();
    myBoneWeightAttr.reset();
    myRestPointsAttr.reset();
    myBlendShapeOffsetsAttr.reset();
    mySkinnedBounds.initBounds();

    mySkinningCacheValid = false;
    myBlendShapeCacheValid = false;

    const UT_Set<TfToken> needed_computations{
            _skinningTokens->restPoints,
            _skinningTokens->geomBindXform,
            _skinningTokens->influences,
            _skinningTokens->numInfluencesPerComponent,
            _skinningTokens->hasConstantInfluences,
            _skinningTokens->blendShapeOffsets,
            _skinningTokens->blendShapeOffsetRanges,
            _skinningTokens->numBlendShapeOffsetRanges};

    HdRenderIndex &render_index = scene_delegate->GetRenderIndex();
    for (const HdExtComputationInputDescriptor &aggregate_descrip :
         GetComputationInputs())
    {
        HdExtComputation const *aggregate_comp
                = static_cast<HdExtComputation const *>(render_index.GetSprim(
                        HdPrimTypeTokens->extComputation,
                        aggregate_descrip.sourceComputationId));

        if (aggregate_comp->GetSceneInputNames().size()
            < needed_computations.size())
            continue;

        bool is_rigid_skinning = false;
        int num_influences_per_comp = 0;
        VtArray<GfVec2f> influence_arr;
        int num_pnts = 0;
        GfMatrix4f geo_bind_xform(1);
        VtVec3fArray rest_points;

        VtArray<GfVec4f> blend_offsets;
        VtArray<GfVec2i> blend_ranges;
        SYS_MAYBE_UNUSED int num_ranges = 0;

        UT_Set<TfToken> cur_computations;

        for (const TfToken &aggregate_input :
             aggregate_comp->GetSceneInputNames())
        {
            VtValue val = scene_delegate->GetExtComputationInput(
                    aggregate_comp->GetId(), aggregate_input);

            if (aggregate_input == _skinningTokens->restPoints)
            {
                UT_ASSERT(val.IsHolding<VtVec3fArray>());
                rest_points = val.UncheckedGet<VtVec3fArray>();
                num_pnts = rest_points.size();
            }
            else if (aggregate_input == _skinningTokens->influences)
            {
                UT_ASSERT(val.IsHolding<VtVec2fArray>());
                influence_arr = val.UncheckedGet<VtVec2fArray>();
            }
            else if (aggregate_input == _skinningTokens->hasConstantInfluences)
            {
                UT_ASSERT(val.IsHolding<bool>());
                is_rigid_skinning = val.UncheckedGet<bool>();
            }
            else if (
                    aggregate_input
                    == _skinningTokens->numInfluencesPerComponent)
            {
                UT_ASSERT(val.IsHolding<int>());
                num_influences_per_comp = val.UncheckedGet<int>();
            }
            else if (aggregate_input == _skinningTokens->geomBindXform)
            {
                UT_ASSERT(val.IsHolding<GfMatrix4f>());
                geo_bind_xform = val.UncheckedGet<GfMatrix4f>();
            }
            else if (aggregate_input == _skinningTokens->blendShapeOffsets)
            {
                UT_ASSERT(val.IsHolding<VtVec4fArray>());
                blend_offsets = val.UncheckedGet<VtVec4fArray>();
            }
            else if (aggregate_input == _skinningTokens->blendShapeOffsetRanges)
            {
                UT_ASSERT(val.IsHolding<VtVec2iArray>());
                blend_ranges = val.UncheckedGet<VtVec2iArray>();
            }
            else if (
                    aggregate_input
                    == _skinningTokens->numBlendShapeOffsetRanges)
            {
                UT_ASSERT(val.IsHolding<int>());
                num_ranges = val.UncheckedGet<int>();
            }
            else
                continue;
            cur_computations.insert(aggregate_input);
        }
        UT_ASSERT(num_pnts > 0);
        if (num_pnts == 0)
            return;
        if (blend_offsets.size() > 0 && blend_ranges.size() > 0)
            myBlendShapeCacheValid = true;
        if (!influence_arr.empty() && num_influences_per_comp > 0)
            mySkinningCacheValid = true;

        // Only need to pre-apply the geometry bind transform if skinning is
        // enabled.
        if (!mySkinningCacheValid)
            geo_bind_xform.SetIdentity();

        setupRestPoints(num_pnts, geo_bind_xform, rest_points);

        if (myBlendShapeCacheValid)
        {
            UT_ASSERT(num_ranges == blend_ranges.size());
            // We only apply the geometry binding transform to the blendshape
            // offsets if we are doing skinning
            if (mySkinningCacheValid)
            {
                // Note VtArray::operator[] is not thread safe, so a span is
                // safer to use here.
                TfSpan<GfVec4f> blend_offsets_data = blend_offsets;

                UT_BlockedRange<int> range(0, blend_offsets.size());
                UTparallelForLightItems(
                        range,
                        [&blend_offsets_data,
                         &geo_bind_xform](const UT_BlockedRange<int> &range)
                        {
                            for (int i = range.begin(); i != range.end(); ++i)
                            {
                                GfVec3f offset(blend_offsets_data[i].data());
                                offset = geo_bind_xform.TransformDir(offset);
                                blend_offsets_data[i][0] = offset[0];
                                blend_offsets_data[i][1] = offset[1];
                                blend_offsets_data[i][2] = offset[2];
                            }
                        });
            }
            setupBlendShapes(
                    scene_delegate, num_pnts, blend_offsets, blend_ranges);
        }
        if (mySkinningCacheValid)
        {
            parseInfluences(
                    is_rigid_skinning, num_influences_per_comp, num_pnts,
                    influence_arr);
        }

        if (cur_computations == needed_computations)
            return;
    }
}

void
XUSD_HydraExtComputation::parseBlendShapeWeights(
        const VtArray<fpreal32> &weights)
{
    if (weights.size() == 0)
    {
        myBlendShapeCacheValid = false;
        return;
    }

    int64 id = XUSD_HydraUtils::newDataId();
    GT_DataArrayHandle blend_shape_weights_attr
            = XUSD_HydraUtils::createGTArray(weights, GT_TYPE_NONE, id);

    GT_CountArray count_array;
    count_array.init(1, blend_shape_weights_attr->entries());

    GT_DataArrayHandle varying_arr = UTmakeIntrusive<GT_DAVaryingArray>(
            blend_shape_weights_attr, count_array);
    varying_arr->setDataId(id);

    myBlendShapeWeightAttr = varying_arr;
}

void
XUSD_HydraExtComputation::parseSkinningXForms(
        const VtArray<GfMatrix4f> &xforms,
        const GfMatrix4f &gf_skel_to_prim_local)
{
    mySkinnedBounds.initBounds();

    VtArray<fpreal32> skinning_xforms_as_vec(16 * xforms.size());
    int idx = 0;
    const UT_Matrix4F &skel_to_prim_local
            = GusdUT_Gf::Cast(gf_skel_to_prim_local);

    for (const GfMatrix4f &gf_xform : xforms)
    {
        UT_Matrix4F xform = GusdUT_Gf::Cast(gf_xform) * skel_to_prim_local;
        std::copy(
                xform.data(), xform.data() + 16,
                skinning_xforms_as_vec.begin() + 16 * idx);
        idx++;

        // Compute the bounding box of the joint positions (in the primitive's
        // space), similar to GU_AgentLinearSkinDeformer::computeBounds()
        UT_Vector3F joint_pos;
        xform.getTranslates(joint_pos);
        mySkinnedBounds.enlargeBounds(joint_pos);
    }

    int64 id = XUSD_HydraUtils::newDataId();
    GT_DataArrayHandle skinning_xform_attr = XUSD_HydraUtils::createGTArray(
            skinning_xforms_as_vec, GT_TYPE_NONE, id);

    GT_CountArray count_array;
    count_array.init(1, skinning_xform_attr->entries());

    GT_DataArrayHandle varying_arr = UTmakeIntrusive<GT_DAVaryingArray>(
            skinning_xform_attr, count_array);
    varying_arr->setDataId(id);

    mySkinningXformAttr = varying_arr;
}

void
XUSD_HydraExtComputation::parseInfluences(
        const bool is_rigid_skinning,
        const int num_influences_per_comp,
        const int num_pnts_on_mesh,
        const VtArray<GfVec2f> &influence_arr)
{
    UT_ASSERT(influence_arr.size() % num_influences_per_comp == 0);

    UT_Vector4iArray bone_idx_data;
    UT_Vector4FArray bone_weight_data;

    bone_idx_data.entries(is_rigid_skinning ? 1 : num_pnts_on_mesh);
    bone_weight_data.entries(is_rigid_skinning ? 1 : num_pnts_on_mesh);
    // We assume the memory layout of the influenceArr is tightly padded
    // such that influenceArr contains [Point_1, Point_2, Point_3...]
    // where Point_i = index_1,weight_1,index_2,weight_2...
    // index_numInfluencesPerComp,weight_numInfluencesPerComp

    if (is_rigid_skinning)
    {
        GT_DAValues const_influence_arr(
                influence_arr.begin()->data(), 1, 2 * num_influences_per_comp);

        GT_DeformUtils::buildBoneIndexAndWeights(
                const_influence_arr, &bone_idx_data, &bone_weight_data, 4);

        GT_DataArrayHandle const_bone_idx_arr
                = UTmakeIntrusive<GT_DANumeric<int32>>(
                        bone_idx_data.array()->data(), 1, 4);
        GT_DataArrayHandle const_bone_weight_arr
                = UTmakeIntrusive<GT_DANumeric<fpreal32>>(
                        bone_weight_data.array()->data(), 1, 4);

        myBoneIdxAttr = UTmakeIntrusive<GT_DAConstant>(
                const_bone_idx_arr, 0, num_pnts_on_mesh);
        myBoneWeightAttr = UTmakeIntrusive<GT_DAConstant>(
                const_bone_weight_arr, 0, num_pnts_on_mesh);
    }
    else
    {
        GT_DAValues gt_influence_arr(
                influence_arr.begin()->data(), num_pnts_on_mesh,
                2 * num_influences_per_comp);

        GT_DeformUtils::buildBoneIndexAndWeights(
                gt_influence_arr, &bone_idx_data, &bone_weight_data, 4);
        myBoneIdxAttr = UTmakeIntrusive<GT_DANumeric<int32>>(
                bone_idx_data.array()->data(), num_pnts_on_mesh, 4);
        myBoneWeightAttr = UTmakeIntrusive<GT_DANumeric<fpreal32>>(
                bone_weight_data.array()->data(), num_pnts_on_mesh, 4);
    }
    myBoneIdxAttr->setDataId(XUSD_HydraUtils::newDataId());
    myBoneWeightAttr->setDataId(XUSD_HydraUtils::newDataId());
}

PXR_NAMESPACE_CLOSE_SCOPE
