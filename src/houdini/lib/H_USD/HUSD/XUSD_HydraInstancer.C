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
    , myIsResolved(false)
    , myIsPointInstancer(false)
    , myNumNativeInstanceIndices(0)
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
        UT_StringArray *instances,
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
    HUSD_Path ppath(prototypeId);
    UT_StringHolder proto_path = ppath.pathStr();
    HUSD_Path ipath(GetId());
    UT_StringHolder inst_path = ipath.pathStr();

    /// BEGIN LOCKED SECTION
    {
        UT_Lock::Scope  lock(myLock);
        if(dirty_indices)
        {
            myResolvedInstances.clear();
            myIsResolved = false;
        }

        myPrototypeIds[hou_proto_id] = proto_path;
        myPrototypePaths[proto_path] = hou_proto_id;
    }
    /// END LOCKED SECTION

    VtIntArray instanceIndices =
        GetDelegate()->GetInstanceIndices(GetId(), prototypeId);
    int num_inst = instanceIndices.size();

    HdInstancer *parent_instancer = nullptr;
    VtMatrix4dArray parent_transforms;
    UT_StringArray parent_names;
    UT_StringArray inames;

    if (recurse && !GetParentId().IsEmpty())
        parent_instancer =
            GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());

    if (parent_instancer)
    {
        parent_transforms =
            UTverify_cast<XUSD_HydraInstancer *>(parent_instancer)->
                privComputeTransforms(GetId(), true, level-1,
                                      &parent_names, nullptr,
                                      scene, id(),
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

    {
        // Lock while accessing myPrototypes
        UT_Lock::Scope  lock(myLock);
        auto &proto_indices = myPrototypes[proto_path];
        if (!myIsPointInstancer)
            myNumNativeInstanceIndices = num_inst;
        if (num_inst > 0)
        {
            UT_WorkBuffer buf;
            for(int i=0; i<num_inst; i++)
            {
                const int idx = instanceIndices[i];
                proto_indices[idx] = 1;

                buf.sprintf("%d", myIsPointInstancer ? idx : i);
                inames.append(buf.buffer());
                if(instances && !ids && !parent_instancer)
                    instances->append(inames.last());
            }
        }
        else
            proto_indices.clear();
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
        if(scene && ids && ids->entries() != transforms.size())
        {
            UT_StringHolder prefix;
            UT_StringHolder path;
            const int nids = transforms.size();

            ids->entries(nids);
            prefix.sprintf("?%d %d ", id(), hou_proto_id);
            for (size_t i = 0; i < nids; ++i)
            {
                path.sprintf("%s%s", prefix.c_str(), inames(i).c_str());
                (*ids)[i] = scene->getOrCreateInstanceID(
                    path, inst_path, proto_path);
                if(instances)
                    instances->append(path);
            }
        }
        else if (scene &&
                 child_instancer &&
                 child_instancer->myIsPointInstancer &&
                 !myIsPointInstancer)
        {
            // We are a native instancer that is a parent of a point instancer.
            // We need to create instance entries in the husd_SceneTree so that
            // we have a prototype registered against this instancer, so that
            // we will register all the instance paths with the tree
            // (HUSD_Scene::registerInstances) so that we can select native
            // instances of the point instancer primitives.
            UT_StringHolder prefix;
            UT_StringHolder path;
            const int nids = transforms.size();

            prefix.sprintf("?%d %d ", id(), hou_proto_id);
            for (size_t i = 0; i < nids; ++i)
            {
                path.sprintf("%s%s", prefix.c_str(), inames(i).c_str());
                scene->getOrCreateInstanceID(
                    path, inst_path, proto_path);
            }
            scene->setParentInstancer(child_instancer->id(), id());
        }

        // Top level transforms
        return transforms;
    }

    VtMatrix4dArray final(parent_transforms.size() * transforms.size());
    const int stride = transforms.size();
    if(ids && dirty_indices)
    {
        UT_StringHolder prefix;
        UT_StringHolder path;
        prefix.sprintf("?%d %d", id(), hou_proto_id);

        ids->entries(parent_transforms.size() * stride);
        for (size_t i = 0; i < parent_transforms.size(); ++i)
            for (size_t j = 0; j < stride; ++j)
            {
                final[i * stride + j] = transforms[j] * parent_transforms[i];

                path.sprintf("%s %s %s", prefix.c_str(),
                             parent_names[i].c_str(),
                             inames[j].c_str());

                (*ids)[i*stride + j] =
                    scene->getOrCreateInstanceID(path, inst_path, proto_path);
                if(instances)
                    instances->append(path);
            }
    }
    else if(instances)
    {
        UT_StringHolder path;

        for (size_t i = 0; i < parent_transforms.size(); ++i)
            for (size_t j = 0; j < stride; ++j)
            {
                final[i * stride + j] =  transforms[j] * parent_transforms[i];

                path.sprintf("%s %s",
                             parent_names[i].c_str(),
                             inames[j].c_str());
                instances->append(path);
            }
    }
    else
    {
        for (size_t i = 0; i < parent_transforms.size(); ++i)
        {
            for (size_t j = 0; j < stride; ++j)
            {
                final[i * stride + j] =  transforms[j] * parent_transforms[i];
            }
        }
    }

    return final;
}

VtMatrix4dArray
XUSD_HydraInstancer::computeTransforms(const SdfPath    &protoId,
                                       bool              recurse,
                                       int               hou_proto_id)
{
    return privComputeTransforms(protoId, recurse,
                                 0, nullptr, nullptr, nullptr,
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
    return privComputeTransforms(protoId, recurse, level, nullptr,
                                 &ids, scene, hou_proto_id,
                                 dirty_indices, nullptr);
}

const UT_StringRef &
XUSD_HydraInstancer::getCachedResolvedInstance(const UT_StringRef &id_key)
{
    static UT_StringRef theEmptyRef;

    auto entry = myResolvedInstances.find(id_key);
    if(entry != myResolvedInstances.end())
        return entry->second;

    return theEmptyRef;
}

void
XUSD_HydraInstancer::cacheResolvedInstance(const UT_StringRef &id_key,
        const UT_StringRef &resolved)
{
    myResolvedInstances[id_key] = resolved;
}

UT_StringArray
XUSD_HydraInstancer::resolveInstance(int proto_id,
        const std::vector<int> &indices,
        int index_level)
{
    UT_StringArray instances;

    if(myIsPointInstancer)
    {
        HdInstancer *pinst;

        pinst = GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
        if(pinst)
        {
            index_level++;
            if(index_level < indices.size())
            {
                instances = UTverify_cast<XUSD_HydraInstancer *>(pinst)->
                    resolveInstance(id(), indices, index_level);
            }
            else
                instances.append(UTverify_cast<XUSD_HydraInstancer *>(pinst)->
                                 findParentInstancer());
        }
        else
            instances.append(GetId().GetAsString());

        UT_WorkBuffer inst;
        inst.sprintf("[%d]", indices[index_level]);
        for(auto &i : instances)
            i += inst.buffer();
    }
    else
    {
        auto p = myPrototypeIds.find(proto_id);
        if(p != myPrototypeIds.end())
        {
            SdfPath prototype_id(p->second.toStdString());
            SdfPath primpath = GetDelegate()->GetRenderIndex().
                GetSceneDelegateForRprim(prototype_id)->
                GetScenePrimPath(prototype_id, indices[index_level]);
            instances.append(primpath.GetAsString());
        }
    }
    
    return instances;
}

UT_StringArray
XUSD_HydraInstancer::resolveInstances(int proto_id,
        const std::vector<int> &parent_indices,
        const std::vector<int> &instance_indices)
{
    UT_StringArray instances;

    if(myIsPointInstancer)
    {
        UT_StringHolder prefix;
        HdInstancer *pinst;

        pinst = GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
        if(pinst)
        {
            UT_StringArray parents =
                UTverify_cast<XUSD_HydraInstancer *>(pinst)->
                resolveInstance(id(), parent_indices);
            if (!parents.isEmpty())
                prefix = parents.last();
        }
        else
            prefix = GetId().GetAsString();

        for (int i = 0; i < instance_indices.size(); i++)
        {
            UT_WorkBuffer inst(prefix);
            inst.appendSprintf("[%d]", instance_indices[i]);
            instances.append(inst);
        }
    }
    else
    {
        auto p = myPrototypeIds.find(proto_id);
        if(p != myPrototypeIds.end())
        {
            SdfPath prototype_id(p->second.toStdString());
            SdfPathVector primpaths = GetDelegate()->GetRenderIndex().
                GetSceneDelegateForRprim(prototype_id)->
                GetScenePrimPaths(prototype_id, instance_indices);
            for (auto &&path : primpaths)
                instances.append(path.GetAsString());
        }
    }

    return instances;
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


UT_StringArray
XUSD_HydraInstancer::resolveInstanceID(HUSD_Scene &scene,
                                       const UT_StringRef &houdini_inst_path,
                                       int instance_idx,
                                       UT_StringHolder &child_indices,
                                       UT_StringArray *proto_id) const
{
    UT_StringArray result;
    int index = -1;
    int end_instance = houdini_inst_path.findCharIndex(']', instance_idx);
    if(end_instance != -1 && instance_idx != -1)
    {
        UT_StringHolder digit(houdini_inst_path.c_str() + instance_idx+1,
                              end_instance-instance_idx-1);
        index = SYSatoi(digit.c_str());
    }

    for (auto &prototype : myPrototypes)
    {
        // UTdebugPrint(index, "Proto", prototype.first);
        UT_StringArray proto;
        UT_StringHolder indices;

        auto child_instr = scene.getInstancer(prototype.first);
        if (child_instr)
        {
            // UTdebugPrint("Resolve child instancer");
            const int next_instance =
                houdini_inst_path.findCharIndex('[', end_instance);

            child_instr->resolveInstanceID(
                scene, houdini_inst_path, next_instance, indices, &proto);
        }
        else
        {
            UT_WorkBuffer buf;
            int pid = -1;

            auto entry = myPrototypePaths.find(prototype.first);
            if (entry != myPrototypePaths.end())
                pid = entry->second;
            buf.sprintf("?%d %d", id(), pid);
            proto.append(buf.buffer());
        }

        UT_WorkBuffer key;
        if (proto_id)
        {
            if (index != -1)
            {
                key.sprintf(" %d%s", index, indices.c_str());
                child_indices = key.buffer();
            }
            for (auto &p : proto)
                proto_id->append(p);
        }
        else
        {
            UT_ASSERT(index != -1);
            for (auto &p : proto)
            {
                key.sprintf("%s %d%s", p.c_str(), index, indices.c_str());
                result.append(key.buffer());
            }
        }
    }

    return result;
}


void
XUSD_HydraInstancer::removePrototype(const UT_StringRef &proto_path,
                                     int id)
{
    UT_StringHolder path(proto_path);
    UT_AutoLock locker(myLock);

    myPrototypes.erase(path);
    myPrototypeIds.erase(id);
    myPrototypePaths.erase(path);
}

void
XUSD_HydraInstancer::addInstanceRef(int id)
{
    myInstanceRefs[id] = 1;
}

bool
XUSD_HydraInstancer::invalidateInstanceRefs()
{
    for(auto &itr : myInstanceRefs)
        itr.second = 0;

    return myInstanceRefs.size() > 0;
}

const UT_Map<int,int> &
XUSD_HydraInstancer::instanceRefs() const
{
    return myInstanceRefs;
}

void
XUSD_HydraInstancer::removeInstanceRef(int id)
{
    myInstanceRefs.erase(id);
}

void
XUSD_HydraInstancer::clearInstanceRefs()
{
    myInstanceRefs.clear();
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
