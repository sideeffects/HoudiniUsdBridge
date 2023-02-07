/*
 * Copyright 2019 Side Effects Software Inc.
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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraInstancer.C (HUSD Library, C++)
 *
 * COMMENTS:	Basic instancer for creating instance transforms.
 *
 */

#include "XUSD_HydraInstancer.h"
#include "XUSD_Format.h"
#include "XUSD_Tokens.h"
#include "HUSD_HydraGeoPrim.h"
#include "HUSD_Path.h"
#include "HUSD_Scene.h"

#include <UT/UT_Debug.h>
#include <UT/UT_StopWatch.h>

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/quaternion.h>
#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace 
{
    template <typename V3> static void
    instanceTranslate(VtMatrix4dArray &transforms,
            const VtIntArray &instanceIndices,
            const VtArray<V3> &primvar)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d      mat(1);
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
                {
                    GfVec3d         xd(primvar[instanceIndices[i]]);
                    mat.SetTranslate(xd);
                    transforms[i] = mat * transforms[i];
                }
            }
        );
    }

    template <typename V4> static void
    instanceRotate(VtMatrix4dArray &transforms,
            const VtIntArray &instanceIndices,
            const VtArray<V4> &primvar)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d      mat(1);
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
                {
                    const V4    &x = primvar[instanceIndices[i]];
                    GfQuatd     q = GfQuatd(x[3], GfVec3d(x[0], x[1], x[2]));
                    mat.SetRotate(q);
                    transforms[i] = mat * transforms[i];
                }
            }
        );
    }

    template <typename Q> static void
    instanceRotateQ(VtMatrix4dArray &transforms,
            const VtIntArray &instanceIndices,
            const VtArray<Q> &primvar)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d      mat(1);
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
                {
                    GfQuatd     q = GfQuatd(primvar[instanceIndices[i]]);
                    mat.SetRotate(q);
                    transforms[i] = mat * transforms[i];
                }
            }
        );
    }

    template <typename V3> static void
    instanceScale(VtMatrix4dArray &transforms,
            const VtIntArray &instanceIndices,
            const VtArray<V3> &primvar)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d      mat(1);
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
                {
                    GfVec3d         xd(primvar[instanceIndices[i]]);
                    mat.SetScale(xd);
                    transforms[i] = mat * transforms[i];
                }
            }
        );
    }

    template <typename M4> static void
    instanceTransform(VtMatrix4dArray &transforms,
            const VtIntArray &instanceIndices,
            const VtArray<M4> &primvar)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d      mat(1);
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
                {
                    GfMatrix4d  xd(primvar[instanceIndices[i]]);
                    transforms[i] = xd * transforms[i];
                }
            }
        );
    }

} // Namespace

XUSD_HydraInstancer::XUSD_HydraInstancer(HdSceneDelegate* delegate,
					 SdfPath const& id)
    : HdInstancer(delegate, id)
    , myID(HUSD_HydraPrim::newUniqueId())
{
}

XUSD_HydraInstancer::~XUSD_HydraInstancer()
{
}

void
XUSD_HydraInstancer::syncPrimvars(HdSceneDelegate* delegate,
        HdRenderParam* renderParam,
        HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();

    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id))
    {
        // If this instancer has dirty primvars, get the list of
        // primvar names and then cache each one.
        HdPrimvarDescriptorVector primvarDescriptors;
        primvarDescriptors = GetDelegate()->
            GetPrimvarDescriptors(id, HdInterpolationInstance);
        HdPrimvarDescriptorVector constantDescriptors;
        constantDescriptors = GetDelegate()->
            GetPrimvarDescriptors(id, HdInterpolationConstant);
        primvarDescriptors.insert(primvarDescriptors.end(),
            constantDescriptors.begin(), constantDescriptors.end());

        VtValue         uvalues;
        UT_Set<TfToken> all_primvars;

        for (auto &&descriptor : primvarDescriptors)
        {
            const auto	&name = descriptor.name;
            all_primvars.insert(name);

            if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, name))
            {
                uvalues = GetDelegate()->Get(id, name);
                if (!uvalues.IsEmpty())
                {
                    myPrimvarMap.erase(name);
                    myPrimvarMap.emplace(name, uvalues);
                }
            }
        }

        // Go through all the primvars that we have to see if they've been
        // erased from the primitive.
        UT_SmallArray<TfToken>  erase_me;
        for (const auto &item : myPrimvarMap)
        {
            // If the primvar wasn't found, we need to erase it.
            if (!all_primvars.contains(item.first))
                erase_me.append(item.first);
        }
        for (const auto &item : erase_me)
            myPrimvarMap.erase(item);
    }
}

VtMatrix4dArray
XUSD_HydraInstancer::privComputeTransforms(const SdfPath &prototypeId,
        bool recurse,
        int level,
        UT_IntArray *ids,
        HUSD_Scene *scene,
        int hou_proto_id,
        bool dirty_indices,
        XUSD_HydraInstancer *child_instancer)
{
    // The transforms for this level of instancer are computed by:
    // foreach(index : indices) {
    //     instancerTransform * translate(index) * rotate(index) *
    //     scale(index) * instanceTransform(index)
    // }
    // If any transform isn't provided, it's assumed to be the identity.
    HUSD_Path proto_path(prototypeId);
    HUSD_Path inst_path(GetId());

    VtIntArray instanceIndices =
        GetDelegate()->GetInstanceIndices(GetId(), prototypeId);
    int num_inst = instanceIndices.size();

    HdInstancer *parent_instancer = nullptr;
    VtMatrix4dArray parent_transforms;

    if (recurse && !GetParentId().IsEmpty())
        parent_instancer =
            GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());

    if (parent_instancer)
    {
        parent_transforms =
            UTverify_cast<XUSD_HydraInstancer *>(parent_instancer)->
                privComputeTransforms(GetId(), true, level-1,
                                      nullptr, scene, id(),
                                      dirty_indices, this);
        // If we have a parent, but that parent has no transforms (i.e. all
        // its instances are hidden) then this instancer is also hidden, so
        // we should immediately return with no transforms.
        //
        // This fixes a crash caused by calling GetPathForInstanceIndex on
        // our (indirectly invisible) instances.
        if (parent_transforms.size() == 0)
            return parent_transforms;
    }

    // Get motion blur interpolants
    VtMatrix4dArray	transforms(num_inst);
    std::fill(transforms.begin(), transforms.end(),
                GetDelegate()->GetInstancerTransform(GetId()));

    // Note that we do not need to lock myLock here to access myPrimvarMap.
    // The syncPrimvars method should be called before this method to build
    // myPrimvarMap, but it guarantees that only one thread (the first one to
    // make it through that method) will change myPrimvarMap. So by the time
    // any thread reaches this point, it is guaranteed that no other threads
    // will be modifying myPrimvarMap.

#define IS_ARRAY(VAL, TYPE) \
    val.IsHolding<VtArray<TYPE>>()

#define CHECK_FUNC(TYPE, FUNC) \
    if (val.IsHolding<VtArray<TYPE>>()) { \
        FUNC<TYPE>(transforms, instanceIndices, val.UncheckedGet<VtArray<TYPE>>()); \
    } \
    /* end macro */

    UTisolate([&]()
    {
        // "translate" holds a translation vector for each index.
        auto &&vitt = myPrimvarMap.find(HdInstancerTokens->translate);
        if (vitt != myPrimvarMap.end())
        {
            const auto &val = vitt->second;

                 CHECK_FUNC(GfVec3f, instanceTranslate)
            else CHECK_FUNC(GfVec3d, instanceTranslate)
            else CHECK_FUNC(GfVec3h, instanceTranslate)
            else
            {
                UTdebugFormat("Type: {}", val.GetType().GetTypeName());
                UT_ASSERT(0 && "Unknown translate buffer type");
            }
        }
        // "rotate" holds a quaternion in <real, i, j, k> format for each index.
        auto &&vitr = myPrimvarMap.find(HdInstancerTokens->rotate);
        if (vitr != myPrimvarMap.end())
        {
            const auto &val = vitr->second;

                 CHECK_FUNC(GfQuath, instanceRotateQ)
            else CHECK_FUNC(GfQuatf, instanceRotateQ)
            else CHECK_FUNC(GfQuatd, instanceRotateQ)
            else CHECK_FUNC(GfVec4f, instanceRotate)
            else CHECK_FUNC(GfVec4d, instanceRotate)
            else CHECK_FUNC(GfVec4h, instanceRotate)
            else
            {
                UTdebugFormat("Type: {}", val.GetType().GetTypeName());
                UT_ASSERT(0 && "Unknown translate buffer type");
            }
        }

        // "scale" holds an axis-aligned scale vector for each index.
        auto &&vits = myPrimvarMap.find(HdInstancerTokens->scale);
        if (vits != myPrimvarMap.end())
        {
            const auto &val = vits->second;

                 CHECK_FUNC(GfVec3f, instanceScale)
            else CHECK_FUNC(GfVec3d, instanceScale)
            else CHECK_FUNC(GfVec3h, instanceScale)
            else
            {
                UTdebugFormat("Type: {}", val.GetType().GetTypeName());
                UT_ASSERT(0 && "Unknown translate buffer type");
            }
        }

        // "instanceTransform" holds a 4x4 transform matrix for each index.
        auto &&viti = myPrimvarMap.find(HdInstancerTokens->instanceTransform);
        if (viti != myPrimvarMap.end())
        {
            const auto &val = viti->second;

                 CHECK_FUNC(GfMatrix4f, instanceTransform)
            else CHECK_FUNC(GfMatrix4d, instanceTransform)
            else
            {
                UTdebugFormat("Type: {}", val.GetType().GetTypeName());
                UT_ASSERT(0 && "Unknown translate buffer type");
            }
        }
    });

    if (!parent_instancer)
    {
        const HdRprim *prprim =
            GetDelegate()->GetRenderIndex().GetRprim(prototypeId);
        if(scene && prprim && ids && ids->entries() != transforms.size())
            (*ids) = scene->getOrCreateInstanceIds(prprim->GetPrimId(),
                transforms.size());

        // Top level transforms
        return transforms;
    }

    VtMatrix4dArray final(parent_transforms.size() * transforms.size());
    const int stride = transforms.size();
    for (size_t i = 0; i < parent_transforms.size(); ++i)
        for (size_t j = 0; j < stride; ++j)
            final[i * stride + j] =  transforms[j] * parent_transforms[i];

    if(dirty_indices)
    {
        const HdRprim *prprim =
            GetDelegate()->GetRenderIndex().GetRprim(prototypeId);
        if (scene && prprim && ids)
            (*ids) = scene->getOrCreateInstanceIds(prprim->GetPrimId(),
                parent_transforms.size() * stride);
    }

    return final;
}

VtMatrix4dArray
XUSD_HydraInstancer::computeTransforms(const SdfPath    &protoId,
                                       bool              recurse,
                                       int               hou_proto_id)
{
    return privComputeTransforms(protoId, recurse,
                                 0, nullptr, nullptr,
                                 hou_proto_id, false, nullptr);
}

VtMatrix4dArray
XUSD_HydraInstancer::computeTransformsAndIDs(const SdfPath    &protoId,
                                             bool              recurse,
                                             int               level,
                                             UT_IntArray      &ids,
                                             HUSD_Scene       *scene,
                                             int               hou_proto_id,
                                             bool              dirty_indices)
{
    return privComputeTransforms(protoId, recurse, level,
                                 &ids, scene, hou_proto_id,
                                 dirty_indices, nullptr);
}

UT_StringHolder
XUSD_HydraInstancer::findParentInstancer() const
{
    if(GetParentId().IsEmpty())
    {
        HUSD_Path hpath(GetId());
        return hpath.pathStr();
    }
    
    auto *pinst=GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
    return UTverify_cast<XUSD_HydraInstancer *>(pinst)->findParentInstancer();
}

const VtValue &
XUSD_HydraInstancer::primvarValue(const TfToken &name) const
{
    auto it = myPrimvarMap.find(name);

    if (it == myPrimvarMap.end())
    {
        static VtValue theEmptyValue;
        return theEmptyValue;
    }

    return it->second;
}

void
XUSD_HydraInstancer::Sync(HdSceneDelegate* delegate,
        HdRenderParam* renderParam,
        HdDirtyBits* dirtyBits)
{
    _UpdateInstancer(delegate, dirtyBits);

    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, GetId()) ||
        HdChangeTracker::IsTransformDirty(*dirtyBits, GetId()))
    {
        syncPrimvars(delegate, renderParam, dirtyBits);
    }

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
