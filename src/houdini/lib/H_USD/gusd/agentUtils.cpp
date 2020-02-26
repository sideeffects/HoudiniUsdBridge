//
// Copyright 2019 Pixar
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
#include "agentUtils.h"

#include "error.h"
#include "GU_PackedUSD.h"
#include "GU_USD.h"
#include "stageCache.h"
#include "UT_Gf.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/tf/span.h"

#include "pxr/usd/usdGeom/imageable.h"
#include "pxr/usd/usdSkel/binding.h"
#include "pxr/usd/usdSkel/skeleton.h"
#include "pxr/usd/usdSkel/skeletonQuery.h"
#include "pxr/usd/usdSkel/topology.h"

#include <GA/GA_AIFIndexPair.h>
#include <GA/GA_AIFTuple.h>
#include <GA/GA_Handle.h>
#include <GA/GA_SplittableRange.h>
#include <GEO/GEO_AttributeCaptureRegion.h>
#include <GEO/GEO_AttributeIndexPairs.h>
#include <GEO/GEO_Detail.h>
#include <GU/GU_DetailHandle.h>
#include <GU/GU_MergeUtils.h>
#include <GU/GU_PrimPacked.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_VarEncode.h>
#include <atomic>
#include <numeric>


PXR_NAMESPACE_OPEN_SCOPE


// TODO: Encoding of namespaced properties is subject to change in
// future releases.
static const UT_StringHolder GUSD_SKEL_JOINTINDICES_ATTR =
    UT_VarEncode::encodeAttrib("skel:jointIndices"_UTsh);
static const UT_StringHolder GUSD_SKEL_JOINTWEIGHTS_ATTR =
    UT_VarEncode::encodeAttrib("skel:jointWeights"_UTsh);


namespace {


void
Gusd_ConvertTokensToStrings(const VtTokenArray& tokens,
                            UT_StringArray& strings)
{
    strings.setSize(tokens.size());
    for (size_t i = 0; i < tokens.size(); ++i) {
        strings[i] = GusdUSD_Utils::TokenToStringHolder(tokens[i]);
    }
}


/// Get names for each joint in \p skel, for use in a GU_AgentRig.
bool
Gusd_GetJointNames(const UsdSkelSkeleton& skel,
                   const VtTokenArray& joints,
                   VtTokenArray& jointNames)
{
    // Skeleton may optionally specify explicit joint names.
    // If so, use those instead of paths.
    if (skel.GetJointNamesAttr().Get(&jointNames)) {
        if (jointNames.size() != joints.size()) {
            GUSD_WARN().Msg("%s -- size of jointNames [%zu] "
                            "!= size of joints [%zu]",
                            skel.GetPrim().GetPath().GetText(),
                            jointNames.size(), joints.size());
            return false;
        }
    } else {
        // No explicit joint names authored.
        // Use the joint paths instead.
        // Although the path tokens could be converted to SdfPath objects,
        // and the tail of those paths could be extracted, they may not
        // be unique: uniqueness is only required for full joint paths.
        jointNames = joints;
    }
    return true;
}


/// Compute an ordered array giving the number of children for each
/// joint in \p topology.
bool
Gusd_GetChildCounts(const UsdSkelTopology& topology,
                    UT_IntArray& childCounts)
{
    childCounts.setSize(topology.GetNumJoints());
    childCounts.constant(0);
    for (size_t i = 0; i < topology.GetNumJoints(); ++i) {
        const int parent = topology.GetParent(i);
        if (parent >= 0) {
            UT_ASSERT_P(static_cast<size_t>(parent) < topology.GetNumJoints());
            ++childCounts[parent];
        }
    }
    return true;
}


/// Compute an ordered array of the children of all joints in \p topology.
bool
Gusd_GetChildren(const UsdSkelTopology& topology,
                 const UT_IntArray& childCounts,
                 UT_IntArray& children)
{
    UT_ASSERT_P(childCounts.size() == topology.GetNumJoints());

    const size_t numJoints = topology.GetNumJoints();

    // Create an array of (nextChild,numAdded) per joint.
    // This will be filled with (startIndex,0) for every joint,
    // then used to to populate the 'children' array.
    UT_Array<std::pair<exint,int> > childIters(numJoints, numJoints);
    exint startIndex = 0;
    exint numChildren = 0;
    for (exint i = 0; i < childCounts.size(); ++i) {
        childIters[i].first = startIndex;
        childIters[i].second = 0;
        startIndex += childCounts[i];
        numChildren += childCounts[i];
    }

    // Now use the iterators above to insert all children, in order.
    children.setSize(numChildren);
    for (size_t i = 0; i < numJoints; ++i) {
        int parent = topology.GetParent(i);
        if (parent >= 0) {
            exint childIndex = childIters[parent].first;
            children[childIndex] = static_cast<int>(i);
            ++childIters[parent].first;
            ++childIters[parent].second;

            if (!TF_VERIFY(childIters[parent].second <= childCounts[parent])) {
                return false;
            }
        }
    }
    return true;
}


} // namespace

GU_AgentRigPtr
GusdCreateAgentRig(const UT_StringHolder &name,
                   const UsdSkelSkeletonQuery &skelQuery)
{
    TRACE_FUNCTION();

    if (!skelQuery.IsValid()) {
        GUSD_WARN().Msg("%s -- invalid skelDefinition.",
                        skelQuery.GetSkeleton().GetPrim().GetPath().GetText());
        return nullptr;
    }

    if(!skelQuery.HasBindPose()) {
        GUSD_WARN().Msg("%s -- `bindTransformsAttrs` is invalid.",
                        skelQuery.GetSkeleton().GetPrim().GetPath().GetText());                 
        return nullptr;
    }        
   
    const UsdSkelSkeleton& skel = skelQuery.GetSkeleton();

    if (!skel) {
        TF_CODING_ERROR("'skel' is invalid");
        return nullptr;
    }

    VtTokenArray jointNames;
    if (!GusdGetJointNames(skel, jointNames)) {
        return nullptr;
    }

    const UsdSkelTopology &topology = skelQuery.GetTopology();
    std::string reason;
    if (!topology.Validate(&reason)) {
        GUSD_WARN().Msg("%s -- invalid topology: %s",
                        skel.GetPrim().GetPath().GetText(),
                        reason.c_str());
        return nullptr;
    }

    return GusdCreateAgentRig(name, topology, jointNames);
}


GU_AgentRigPtr
GusdCreateAgentRig(const UT_StringHolder &name,
                   const UsdSkelTopology& topology,
                   const VtTokenArray& jointNames)
{
    TRACE_FUNCTION();

    if (jointNames.size() != topology.GetNumJoints()) {
        TF_CODING_ERROR("jointNames size [%zu] != num joints [%zu]",
                        jointNames.size(), topology.GetNumJoints());
        return nullptr;
    }

    UT_IntArray childCounts;
    if (!Gusd_GetChildCounts(topology, childCounts)) {
        return nullptr;
    }
    UT_IntArray children;
    if (!Gusd_GetChildren(topology, childCounts, children)) {
        return nullptr;
    }

    UT_ASSERT(childCounts.size() == jointNames.size());
    UT_ASSERT(std::accumulate(childCounts.begin(),
                              childCounts.end(), 0) == children.size());

    UT_StringArray names;
    Gusd_ConvertTokensToStrings(jointNames, names);

    // Add a __locomotion__ transform for root motion.
    static constexpr UT_StringLit theLocomotionName("__locomotion__");
    if (names.find(theLocomotionName.asHolder()) < 0)
    {
        names.append(theLocomotionName.asHolder());
        childCounts.append(0);
    }

    const auto rig = GU_AgentRig::addRig(name);
    UT_ASSERT_P(rig);

    if (rig->construct(names, childCounts, children)) {
        return rig;
    } else {
        // XXX: Would be nice if we got a reasonable warning/error...
        GUSD_WARN().Msg("internal error constructing agent rig '%s'",
                        name.c_str());
    }
    return nullptr;
}


namespace {

GA_RWAttributeRef
Gusd_AddCaptureAttribute(GEO_Detail &gd,
                         const int tupleSize,
                         const VtMatrix4dArray &inverseBindTransforms,
                         const VtTokenArray &jointNames)
{
    const int numJoints = static_cast<int>(jointNames.size());

    int regionsPropId = -1;

    GA_RWAttributeRef captureAttr =
        gd.addPointCaptureAttribute(GEO_Detail::geo_NPairs(tupleSize));
    GA_AIFIndexPairObjects *joints =
        GEO_AttributeCaptureRegion::getBoneCaptureRegionObjects(
            captureAttr, regionsPropId);
    joints->setObjectCount(numJoints);

    // Set the names of each joint.
    {
        GEO_RWAttributeCapturePath jointPaths(&gd);
        for (int i = 0; i < numJoints; ++i)
        {
            // TODO: Elide the string copy.
            jointPaths.setPath(i, jointNames[i].GetText());
        }
    }

    // Store the inverse bind transforms of each joint.
    {
        const GfMatrix4d *xforms = inverseBindTransforms.cdata();
        for (int i = 0; i < numJoints; ++i)
        {
            GEO_CaptureBoneStorage r;
            r.myXform = GusdUT_Gf::Cast(xforms[i]);

            joints->setObjectValues(i, regionsPropId, r.floatPtr(),
                                    GEO_CaptureBoneStorage::tuple_size);
        }
    }

    return captureAttr;
}

bool
Gusd_CreateRigidCaptureAttribute(
    GEO_Detail& gd,
    const UsdSkelSkinningQuery &skinningQuery,
    const VtMatrix4dArray& inverseBindTransforms,
    const VtTokenArray& jointNames)
{
    TRACE_FUNCTION();

    UT_ASSERT(skinningQuery.IsRigidlyDeformed());

    UsdGeomPrimvar indices_pv = skinningQuery.GetJointIndicesPrimvar();
    UsdGeomPrimvar weights_pv = skinningQuery.GetJointWeightsPrimvar();
    UT_ASSERT(indices_pv && weights_pv);

    const int tupleSize = skinningQuery.GetNumInfluencesPerComponent();
    GA_RWAttributeRef captureAttr = Gusd_AddCaptureAttribute(
        gd, tupleSize, inverseBindTransforms, jointNames);

    const GA_AIFIndexPair* indexPair = captureAttr->getAIFIndexPair();
    indexPair->setEntries(captureAttr, tupleSize);

    UTparallelFor(
        GA_SplittableRange(gd.getPointRange()),
        [&](const GA_SplittableRange& r)
        {
            VtFloatArray weights;
            VtIntArray indices;
            UT_VERIFY(indices_pv.Get(&indices));
            UT_VERIFY(weights_pv.Get(&weights));

            auto* boss = UTgetInterrupt();
            char bcnt = 0;

            GA_Offset o,end;
            for (GA_Iterator it(r); it.blockAdvance(o,end); ) {
                if (ARCH_UNLIKELY(!++bcnt && boss->opInterrupt())) {
                    return;
                }

                for ( ; o < end; ++o) {
                    for (int c = 0; c < tupleSize; ++c) {
                        // Unused influences have both an index and weight
                        // of 0. Convert this back to an invalid index for
                        // the capture attribute.
                        indexPair->setIndex(
                            captureAttr, o, c,
                            (weights[c] == 0.0) ? -1 : indices[c]);
                        indexPair->setData(captureAttr, o, c, weights[c]);
                    }
                }
            }
        });

    return true;
}

/// Create capture attrs on \p gd, in the form expected for LBS skinning.
/// This expects \p gd to have already imported 'primvars:skel:jointIndices'
/// and 'primvars:skel:jointWeights' -- as defined by the UsdSkelBindingAPI.
/// If \p deleteInfluencePrimvars=true, the original primvars imported for
/// UsdSkel are deleted after conversion.
bool
Gusd_CreateVaryingCaptureAttribute(
    GEO_Detail& gd,
    const VtMatrix4dArray& inverseBindTransforms,
    const VtTokenArray& jointNames,
    bool deleteInfluencePrimvars=true)
{
    TRACE_FUNCTION();

    // Expect to find the jointIndices/jointWeights properties already
    // imported onto the detail. We could query them from USD ourselves,
    // but then we would need to worry about things like winding order, etc.

    const GA_Offset constant_offset = gd.primitiveOffset(0);
    GA_ROHandleI jointIndicesHnd(&gd, GA_ATTRIB_POINT,
                                 GUSD_SKEL_JOINTINDICES_ATTR);
    
    bool perPointJointIndices = true;

    if (jointIndicesHnd.isInvalid()) {

        // If the influences were stored with 'constant' interpolation,
        // they may be defined as a primitive attrib instead
        // (GusdPrimWrapper::convertPrimvarData() promotes constant primvars to
        // primitive attributes to ensure that the results are consistent when
        // merging)
        jointIndicesHnd.bind(&gd, GA_ATTRIB_PRIMITIVE,
                             GUSD_SKEL_JOINTINDICES_ATTR);
        if (jointIndicesHnd.isValid()) {
            perPointJointIndices = false;
        } else {
            GUSD_WARN().Msg("Could not find int skel_jointIndices attribute.");
            return false;
        }
    }
    GA_ROHandleF jointWeightsHnd(&gd, GA_ATTRIB_POINT,
                                 GUSD_SKEL_JOINTWEIGHTS_ATTR);
    bool perPointJointWeights = true;

    if (jointWeightsHnd.isInvalid()) {
        
        // If the influences were stored with 'constant' interpolation,
        // they may be defined as a detail attrib instead.
        jointWeightsHnd.bind(&gd, GA_ATTRIB_PRIMITIVE,
                             GUSD_SKEL_JOINTWEIGHTS_ATTR);
        if (jointWeightsHnd.isValid()) {
            perPointJointWeights = false;
        } else {
            GUSD_WARN().Msg("Could not find float skel_jointWeights "
                            "attribute.");
            return false;
        }
    }
    if (jointIndicesHnd.getTupleSize() != jointWeightsHnd.getTupleSize()) {
        GUSD_WARN().Msg("Tuple size of skel_jointIndices [%d] != "
                        "tuple size of skel_JointWeights [%d]",
                        jointIndicesHnd.getTupleSize(),
                        jointWeightsHnd.getTupleSize());
        return false;
    }

    const int tupleSize = jointIndicesHnd.getTupleSize();
    const int numJoints = static_cast<int>(jointNames.size());

    int regionsPropId = -1;

    GA_RWAttributeRef captureAttr =
        gd.addPointCaptureAttribute(GEO_Detail::geo_NPairs(tupleSize));
    GA_AIFIndexPairObjects* joints =
        GEO_AttributeCaptureRegion::getBoneCaptureRegionObjects(
            captureAttr, regionsPropId);
    joints->setObjectCount(numJoints);


    // Set the names of each joint.
    {
        GEO_RWAttributeCapturePath jointPaths(&gd);
        for (int i = 0; i < numJoints; ++i) {
            // TODO: Elide the string copy.
            jointPaths.setPath(i, jointNames[i].GetText());
        }
    }

    // Store the inverse bind transforms of each joint.
    {
        const GfMatrix4d* xforms = inverseBindTransforms.cdata();
        for (int i = 0; i < numJoints; ++i) {

            GEO_CaptureBoneStorage r;
            r.myXform = GusdUT_Gf::Cast(xforms[i]);

            joints->setObjectValues(i, regionsPropId, r.floatPtr(),
                                    GEO_CaptureBoneStorage::tuple_size);
        }
    }

    // Copy weights and indices.
    const GA_AIFTuple* jointIndicesTuple = jointIndicesHnd->getAIFTuple();
    const GA_AIFTuple* jointWeightsTuple = jointWeightsHnd->getAIFTuple();

    const GA_AIFIndexPair* indexPair = captureAttr->getAIFIndexPair();
    indexPair->setEntries(captureAttr, tupleSize);

    UTparallelFor(
        GA_SplittableRange(gd.getPointRange()),
        [&](const GA_SplittableRange& r)
        {
            UT_FloatArray weights(tupleSize, tupleSize);
            UT_IntArray indices(tupleSize, tupleSize);

            auto* boss = UTgetInterrupt();
            char bcnt = 0;

            GA_Offset o,end;
            for (GA_Iterator it(r); it.blockAdvance(o,end); ) {
                if (ARCH_UNLIKELY(!++bcnt && boss->opInterrupt())) {
                    return;
                }

                for ( ; o < end; ++o) {
                    if (jointIndicesTuple->get(jointIndicesHnd.getAttribute(),
                                               perPointJointIndices ? o : constant_offset,
                                               indices.data(), tupleSize) &&
                        jointWeightsTuple->get(jointWeightsHnd.getAttribute(),
                                               perPointJointWeights ? o : constant_offset,
                                               weights.data(), tupleSize)) {

                        // Joint influences are required to be stored
                        // pre-normalized in USD, but subsequent import
                        // processing may have altered that.
                        // Normalize in-place to be safe.
                        //
                        // TODO: If the shape was rigid, then we are needlessly
                        // re-normalizing over each run. It would be more
                        // efficient to pre-normalize instead.
                        float sum = 0;
                        for (int c = 0; c < tupleSize; ++c)
                            sum += weights[c];
                        if (sum > 1e-6) {
                            for (int c = 0; c < tupleSize; ++c) {
                                weights[c] /= sum;
                            }
                        }

                        for (int c = 0; c < tupleSize; ++c) {
                            // Unused influences have both an index and weight
                            // of 0. Convert this back to an invalid index for
                            // the capture attribute.
                            indexPair->setIndex(
                                captureAttr, o, c,
                                (weights[c] == 0.0) ? -1 : indices[c]);
                            indexPair->setData(captureAttr, o, c, weights[c]);
                        }
                    }
                }
            }
        });

    if (deleteInfluencePrimvars) {
        gd.destroyAttribute(jointIndicesHnd->getOwner(),
                            jointIndicesHnd->getName());
        gd.destroyAttribute(jointWeightsHnd->getOwner(),
                            jointWeightsHnd->getName());
    }
    return true;
}


bool
Gusd_ReadSkinnablePrims(const UsdSkelBinding& binding,
                        const VtTokenArray& jointNames,
                        const VtMatrix4dArray& invBindTransforms,
                        UsdTimeCode time,
                        const char* lod,
                        GusdPurposeSet purpose,
                        UT_ErrorSeverity sev,
                        const GT_RefineParms* refineParms,
                        UT_Array<GU_DetailHandle>& details)
{
    TRACE_FUNCTION();

    UT_AutoInterrupt task("Read USD shapes for shapelib");
    
    const exint numTargets = binding.GetSkinningTargets().size();

    details.clear();
    details.setSize(numTargets);

    GusdErrorTransport errTransport;

    // Read in details for all skinning targets in parallel.
    UTparallelForEachNumber(
        numTargets,
        [&](const UT_BlockedRange<exint>& r)
        {
            const GusdAutoErrorTransport autoErrTransport(errTransport);

            for (exint i = r.begin(); i < r.end(); ++i) {

                if (task.wasInterrupted())
                    return;

                const UsdGeomImageable ip(
                    binding.GetSkinningTargets()[i].GetPrim());
                if (!ip) {
                    continue;
                }
                if (ip.ComputeVisibility(time) == UsdGeomTokens->invisible) {
                    continue;
                }
                if (!GusdPurposeInSet(ip.ComputePurpose(), purpose)) {
                    continue;
                }

                GU_DetailHandle gdh;
                gdh.allocateAndSet(new GU_Detail);

                const GU_DetailHandleAutoWriteLock gdl(gdh);
                
                if (GusdReadSkinnablePrim(
                        *gdl.getGdp(), binding.GetSkinningTargets()[i],
                        jointNames, invBindTransforms,
                        time, lod, purpose, sev, refineParms)) {
                    details[i] = gdh;
                } else if (sev >= UT_ERROR_ABORT) {
                    return;
                }
            }
        });

    return !task.wasInterrupted();
}


void
Gusd_InvertTransforms(TfSpan<GfMatrix4d> xforms)
{
    UTparallelForLightItems(
        UT_BlockedRange<size_t>(0, xforms.size()),
        [&](const UT_BlockedRange<size_t>& r)
        {
            for (size_t i = r.begin(); i < r.end(); ++i) {
                xforms[i] = xforms[i].GetInverse();
            }
        });
}


bool
Gusd_ReadSkinnablePrims(const UsdSkelBinding& binding,  
                        UsdTimeCode time,
                        const char* lod,
                        GusdPurposeSet purpose,
                        UT_ErrorSeverity sev,
                        const GT_RefineParms* refineParms,
                        UT_Array<GU_DetailHandle>& details)
{
    const UsdSkelSkeleton& skel = binding.GetSkeleton();

    VtTokenArray joints;
    if (!skel.GetJointsAttr().Get(&joints)) {
        GUSD_WARN().Msg("%s -- 'joints' attr is invalid",
                        skel.GetPrim().GetPath().GetText());
        return false;
    }
    VtTokenArray jointNames;
    if (!Gusd_GetJointNames(skel, joints, jointNames)) {
        return false;
    }

    VtMatrix4dArray invBindTransforms;
    if (!skel.GetBindTransformsAttr().Get(&invBindTransforms)) {
        GUSD_WARN().Msg("%s -- no authored bindTransforms",
                        skel.GetPrim().GetPath().GetText());
        return false;
    }
    if (invBindTransforms.size() != joints.size()) {
        GUSD_WARN().Msg("%s -- size of 'bindTransforms' [%zu] != "
                        "size of 'joints' [%zu].",
                        skel.GetPrim().GetPath().GetText(),
                        invBindTransforms.size(), joints.size());
        return false;
    }
    // XXX: Want *inverse* bind transforms when writing out capture data.
    Gusd_InvertTransforms(invBindTransforms);
    
    return Gusd_ReadSkinnablePrims(binding, jointNames, invBindTransforms, time,
                                   lod, purpose, sev, refineParms, details);
}


} // namespace


bool
GusdReadSkinnablePrims(const UsdSkelBinding& binding,
                       UT_Array<GU_DetailHandle>& details,
                       UsdTimeCode time,
                       const char* lod,
                       GusdPurposeSet purpose,
                       UT_ErrorSeverity sev,
                       const GT_RefineParms* refineParms)
{
    const UsdSkelSkeleton& skel = binding.GetSkeleton();

    VtTokenArray joints;
    if (!skel.GetJointsAttr().Get(&joints)) {
        GUSD_WARN().Msg("%s -- 'joints' attr is invalid",
                        skel.GetPrim().GetPath().GetText());
        return false;
    }
    VtTokenArray jointNames;
    if (!Gusd_GetJointNames(skel, joints, jointNames)) {
        return false;
    }

    VtMatrix4dArray invBindTransforms;
    if (!skel.GetBindTransformsAttr().Get(&invBindTransforms)) {
        GUSD_WARN().Msg("%s -- no authored bindTransforms",
                        skel.GetPrim().GetPath().GetText());
        return false;
    }
    if (invBindTransforms.size() != joints.size()) {
        GUSD_WARN().Msg("%s -- size of 'bindTransforms' [%zu] != "
                        "size of 'joints' [%zu].",
                        skel.GetPrim().GetPath().GetText(),
                        invBindTransforms.size(), joints.size());
        return false;
    }
    // XXX: Want *inverse* bind transforms when writing out capture data.
    Gusd_InvertTransforms(invBindTransforms);
    
    return Gusd_ReadSkinnablePrims(binding, jointNames, invBindTransforms, time,
                                   lod, purpose, sev, refineParms, details);
}

bool
GusdCreateCaptureAttribute(GU_Detail &detail,
                           const UsdSkelSkinningQuery &skinningQuery,
                           const VtTokenArray &jointNames,
                           const VtMatrix4dArray &invBindTransforms)
{
    // Convert joint names and bind transforms in Skeleton order to the order
    // specified on this skinnable prim (if any).
    VtTokenArray localJointNames = jointNames;
    VtMatrix4dArray localInvBindTransforms = invBindTransforms;
    if (skinningQuery.GetMapper()) {
        if (!skinningQuery.GetMapper()->Remap(
                jointNames, &localJointNames)) {
            return false;
        }
        if (!skinningQuery.GetMapper()->Remap(
                invBindTransforms, &localInvBindTransforms)) {
            return false;
        }
    }

    if (skinningQuery.IsRigidlyDeformed())
    {
        return Gusd_CreateRigidCaptureAttribute(
            detail, skinningQuery, localInvBindTransforms, localJointNames);
    }
    else
    {
        return Gusd_CreateVaryingCaptureAttribute(
            detail, localInvBindTransforms, localJointNames, true);
    }
}

bool
GusdReadSkinnablePrim(GU_Detail& gd,
                      const UsdSkelSkinningQuery& skinningQuery,
                      const VtTokenArray& jointNames,
                      const VtMatrix4dArray& invBindTransforms,
                      UsdTimeCode time,
                      const char* lod,
                      GusdPurposeSet purpose,
                      UT_ErrorSeverity sev,
                      const GT_RefineParms* refineParms)
{
    TRACE_FUNCTION();

    const GfMatrix4d geomBindTransform = skinningQuery.GetGeomBindTransform();
    const UsdPrim &skinnedPrim = skinningQuery.GetPrim();
    const char *primvarPattern = "Cd skel:jointIndices skel:jointWeights";
    const UT_StringHolder &attributePattern = UT_StringHolder::theEmptyString;
    // Not needed since st isn't in the primvar pattern.
    const bool translateSTtoUV = false;
    const UT_StringHolder &nonTransformingPrimvarPattern =
        UT_StringHolder::theEmptyString;

    return (GusdGU_USD::ImportPrimUnpacked(
                gd, skinnedPrim, time, lod, purpose, primvarPattern,
                attributePattern, translateSTtoUV,
                nonTransformingPrimvarPattern,
                &GusdUT_Gf::Cast(geomBindTransform), refineParms) &&
            GusdCreateCaptureAttribute(
                gd, skinningQuery, jointNames, invBindTransforms));
}

GU_AgentShapeLibPtr
GusdCreateAgentShapeLib(const UsdSkelBinding& binding,  
                        UsdTimeCode time,
                        const char* lod,
                        GusdPurposeSet purpose,
                        UT_ErrorSeverity sev,
                        const GT_RefineParms* refineParms)
{
    const UsdSkelSkeleton& skel = binding.GetSkeleton();

    // Read geom for each skinning target into its own detail.

    UT_Array<GU_DetailHandle> details;
    if (!Gusd_ReadSkinnablePrims(binding, time, lod, purpose,
                                 sev, refineParms, details)) {
        return nullptr;
    }

    const size_t numTargets = binding.GetSkinningTargets().size();
    UT_ASSERT_P(details.size() == numTargets);

    auto shapeLib =
        GU_AgentShapeLib::addLibrary(skel.GetPrim().GetPath().GetText());

    // Add the resulting details to the shape lib.
    for (size_t i = 0; i < numTargets; ++i) {
        if (const auto& gdh = details[i]) {
            const UsdPrim& prim = binding.GetSkinningTargets()[i].GetPrim();

            const UT_StringHolder name(prim.GetPath().GetString());
            shapeLib->addShape(name, gdh);
        }
    }
    return shapeLib;
}


namespace {

// TODO: This is the bottle neck in import.
bool
_CoalesceShapes(GU_Detail& coalescedGd,
                UT_Array<GU_DetailHandle>& details)
{
    UT_AutoInterrupt task("Coalesce shapes");

    UT_Array<GU_Detail *> gdps;
    for (GU_DetailHandle &gdh : details)
    {
        if (gdh.isValid())
            gdps.append(gdh.gdpNC());
    }

    GUmatchAttributesAndMerge(coalescedGd, gdps);

    return !task.wasInterrupted();
}

} // namespace


bool
GusdCoalesceAgentShapes(GU_Detail& gd,
                        const UsdSkelBinding& binding,
                        UsdTimeCode time,
                        const char* lod,
                        GusdPurposeSet purpose,
                        UT_ErrorSeverity sev,
                        const GT_RefineParms* refineParms)
{
    UT_Array<GU_DetailHandle> details;
    if (GusdReadSkinnablePrims(binding, details, time, lod,
                               purpose, sev, refineParms)) {
        return _CoalesceShapes(gd, details);
    }
    return false;
}

bool
GusdForEachSkinnedPrim(const UsdSkelBinding &binding,
                       const GusdSkinImportParms &parms,
                       const GusdSkinnedPrimCallback &callback)
{
    const UsdSkelSkeleton &skel = binding.GetSkeleton();

    VtTokenArray joints;
    skel.GetJointsAttr().Get(&joints);

    VtTokenArray jointNames;
    if (!Gusd_GetJointNames(skel, joints, jointNames))
        return false;

    VtMatrix4dArray invBindTransforms;
    if (!joints.empty() &&
        !skel.GetBindTransformsAttr().Get(&invBindTransforms))
    {
        GUSD_WARN().Msg("%s -- no authored bindTransforms",
                        skel.GetPrim().GetPath().GetText());
        return false;
    }

    if (invBindTransforms.size() != joints.size())
    {
        GUSD_WARN().Msg("%s -- size of 'bindTransforms' [%zu] != "
                        "size of 'joints' [%zu].",
                        skel.GetPrim().GetPath().GetText(),
                        invBindTransforms.size(), joints.size());
        return false;
    }
    Gusd_InvertTransforms(invBindTransforms);

    // TODO - convert Gusd_ReadSkinnablePrims to reuse this method.
    const exint num_targets = binding.GetSkinningTargets().size();
    GusdErrorTransport err_transport;
    std::atomic_bool worker_success(true);
    UTparallelForEachNumber(
        num_targets, [&](const UT_BlockedRange<exint> &r) {
            const GusdAutoErrorTransport auto_err_transport(err_transport);

            for (exint i = r.begin(); i < r.end(); ++i)
            {
                const UsdGeomImageable ip(
                    binding.GetSkinningTargets()[i].GetPrim());
                if (!ip)
                    continue;

                if (ip.ComputeVisibility(parms.myTime) ==
                    UsdGeomTokens->invisible)
                {
                    continue;
                }

                if (!GusdPurposeInSet(ip.ComputePurpose(), parms.myPurpose))
                    continue;

                if (!callback(i, parms, jointNames, invBindTransforms))
                {
                    worker_success = false;
                    return;
                }
            }
        });

    return worker_success;
}

bool
GusdGetJointNames(const UsdSkelSkeleton &skel, VtTokenArray &jointNames)
{
    VtTokenArray joints;
    // Note - it's acceptable for there to be no joints authored if e.g. there
    // are only blendshapes.
    skel.GetJointsAttr().Get(&joints);

    jointNames.clear();
    return Gusd_GetJointNames(skel, joints, jointNames);
}

PXR_NAMESPACE_CLOSE_SCOPE
