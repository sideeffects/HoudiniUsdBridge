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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#include "BRAY_HdInstancer.h"

#include <pxr/imaging/hd/sceneDelegate.h>

#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_Tokens.h>
#include <HUSD/XUSD_HydraUtils.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Set.h>
#include <UT/UT_SmallArray.h>
#include "BRAY_HdUtil.h"
#include "BRAY_HdParam.h"

PXR_NAMESPACE_OPEN_SCOPE

#if 0
// Define local tokens for the names of the primvars the instancer
// consumes.
// XXX: These should be hydra tokens...
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (instanceTransform)
    (rotate)
    (scale)
    (translate)
);
#endif

namespace
{
    static UT_Set<TfToken> &
    transformTokens()
    {
	static UT_Set<TfToken>	theTokens({
		HusdHdPrimvarTokens()->translate,
		HusdHdPrimvarTokens()->rotate,
		HusdHdPrimvarTokens()->scale,
		HusdHdPrimvarTokens()->instanceTransform
	});
	return theTokens;
    }
}

#if 0
static void
dumpDesc(HdSceneDelegate *sd, HdInterpolation style, const SdfPath &id)
{
    const auto	&descs = sd->GetPrimvarDescriptors(id, style);
    if (!descs.size())
	return;
    UTdebugFormat("-- {} {} --", id, TfEnum::GetName(style));
    for (auto &d : descs)
	UTdebugFormat("  {}", d.name);
}

static void
dumpAllDesc(HdSceneDelegate *sd, const SdfPath &id)
{
    UTdebugFormat("-- {} --", id);
    for (auto &&style : {
	        HdInterpolationConstant,
		HdInterpolationUniform,
		HdInterpolationVarying,
		HdInterpolationVertex,
		HdInterpolationFaceVarying,
		HdInterpolationInstance,
	    })
	dumpDesc(sd, style, id);
}
#endif

BRAY_HdInstancer::BRAY_HdInstancer(HdSceneDelegate* delegate,
                                     SdfPath const& id,
                                     SdfPath const &parentId)
    : XUSD_HydraInstancer(delegate, id, parentId)
    , myNewObject(false)
    , myNestLevel(0)
{
}

BRAY_HdInstancer::~BRAY_HdInstancer()
{
}

void
BRAY_HdInstancer::applyNesting(BRAY_HdParam &rparm,
	BRAY::ScenePtr &scene)
{
    if (!myInstanceMap.size())
	return;

    // Make sure to build the scene graph if required
    BRAY::ObjectPtr	proto;

    if (myInstanceMap.size() > 1)
    {
	// In this case, we have multiple objects being instanced.  For this we
	// want to aggregate the edits into a scene graph.
	if (!mySceneGraph)
	{
	    myNewObject = true;
	    mySceneGraph = BRAY::ObjectPtr::createScene();
	    for (auto &&inst : myInstanceMap)
		mySceneGraph.addInstanceToScene(inst.second);
	}
	else
	{
	    myNewObject = false;
	    scene.updateObject(mySceneGraph, BRAY_EVENT_CONTENTS);
	}
	proto = mySceneGraph;	// This is the object we want to process
    }
    else
    {
	for (auto &&inst : myInstanceMap)
	{
	    UT_ASSERT(!proto);
	    proto = inst.second;
	    break;
	}
    }

    if (GetParentId().IsEmpty())
    {
	if (myNewObject)
	{
	    myNewObject = false;
	    scene.updateObject(proto, BRAY_EVENT_NEW);
	}
    }
    else
    {
	HdInstancer	*parentInstancer =
	    GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
	UT_ASSERT(parentInstancer);
	UT_SmallArray<GfMatrix4d>	px;
	px.emplace_back(1.0);

	UTverify_cast<BRAY_HdInstancer *>(parentInstancer)->
	    NestedInstances(rparm, scene, GetId(), proto, px, 1);

    }
}

GT_AttributeListHandle
BRAY_HdInstancer::attributesForPrototype(const SdfPath &protoId)
{
    // If there are no attributes, just return an empty array
    if (!myAttributes || !myAttributes->entries())
	return GT_AttributeListHandle();

    VtIntArray	indices = GetDelegate()->GetInstanceIndices(GetId(), protoId);
    auto alist_size = myAttributes->get(0)->entries();
    if (indices.size() == alist_size)
	return myAttributes;
    GT_DataArrayHandle	gt_indices = XUSD_HydraUtils::createGTArray(indices);
    return myAttributes->createIndirect(gt_indices);
}

UT_Array<exint>
BRAY_HdInstancer::instanceIdsForPrototype(const SdfPath &protoId)
{
    VtIntArray	indices = GetDelegate()->GetInstanceIndices(GetId(), protoId);
    UT_Array<exint> result(indices.size());
    bool contiguous = true;
    for (exint i = 0, n = indices.size(); i < n; ++i)
    {
	result.append(indices[i]);
	if (indices[i] != i)
	    contiguous = false;
    }

    if (contiguous)
	return UT_Array<exint>();

    return result;
}

void
BRAY_HdInstancer::updateAttributes(BRAY_HdParam &rparm,
	BRAY::ScenePtr &scene,
	const BRAY::ObjectPtr &protoObj)
{
    HdChangeTracker	&tracker =
			    GetDelegate()->GetRenderIndex().GetChangeTracker();
    int dirtyBits = tracker.GetInstancerDirtyBits(GetId());
    if (HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, GetId()))
    {
	// TODO: Switch over to UT_DoubleLock when it can handle UT_IntrusivePtr
	if (!myAttributes)
	{
	    UT_Lock::Scope	lock(myAttributeLock);
	    if (!myAttributes)
	    {
		SYSstoreFence();
		// Make an attribute list, but exclude all the tokens for
		// transforms
		myAttributes = BRAY_HdUtil::makeAttributes(GetDelegate(),
			rparm,
			GetId(),
			HdInstancerTokens->instancer,
			-1,
			protoObj.objectProperties(scene),
			HdInterpolationInstance,
			&transformTokens());
	    }
	}
	// Don't clear the dirty bits since we need to discover this when
	// computing transforms.
    }
}

void
BRAY_HdInstancer::NestedInstances(BRAY_HdParam &rparm,
	BRAY::ScenePtr &scene,
	SdfPath const &prototypeId,
	const BRAY::ObjectPtr &protoObj,
	const UT_Array<GfMatrix4d> &protoXform,
	int nsegs)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // figure out nesting level
    myNestLevel = 0;
    if (!GetParentId().IsEmpty())
    {
	HdInstancer *instancer = this;
	while (!instancer->GetParentId().IsEmpty())
	{
	    myNestLevel++;
	    instancer = GetDelegate()->GetRenderIndex().GetInstancer(
		instancer->GetParentId());
	}
    }

    UT_Array<BRAY::SpacePtr>	 xforms;

    // Make an attribute list, but exclude all the tokens for transforms
    // We need to capture attributes before syncPrimvars() clears the dirty
    // bits when it caches the transform data.
    //
    // NOTE: There's a possible indeterminant order here.  The prototypes can
    // be processed in arbitrary order, but the prototype's motion blur
    // settings are used to determine the motion segments for attributes on the
    // instance attribs.  So, if prototypes have different motion blur
    // settings, the behaviour of the instance evaluation might be different.
    updateAttributes(rparm, scene, protoObj);

    // TODO: When we pull out syncPrimvars from the instance, we can find out
    // how many segments exist on the instance.  So if there's a single
    // protoXform, we can still get motion segments from the instancer.
    UT_StackBuffer<VtMatrix4dArray>	xformList(nsegs);
    UT_StackBuffer<float>		shutter_times(nsegs);
    syncPrimvars(false, nsegs);
    rparm.fillShutterTimes(shutter_times, nsegs);
    for (int i = 0; i < nsegs; ++i)
    {
	int	pidx = SYSmin(int(protoXform.size()-1), nsegs);
	xformList[i] = computeTransforms(prototypeId, false,
				&protoXform[pidx], shutter_times[i]);
    }
    BRAY_HdUtil::makeSpaceList(xforms, xformList.array(), nsegs);

    bool		 new_instance = false;
    BRAY::ObjectPtr	&inst = findOrCreate(prototypeId);
    if (!inst)
    {
	new_instance = true;
	myNewObject = true;	// There's a new object in me
	inst = BRAY::ObjectPtr::createInstance(protoObj, GetId().GetString());
    }

    // Update information
    inst.setInstanceTransforms(xforms);
    inst.setInstanceAttributes(scene, attributesForPrototype(prototypeId));
    inst.setInstanceIds(instanceIdsForPrototype(prototypeId));
    inst.validateInstance();

    if (!new_instance)
	scene.updateObject(inst, BRAY_EVENT_XFORM);

    // Make sure to process myself after all my children have been processed.
    rparm.queueInstancer(GetDelegate(), this);
}

void
BRAY_HdInstancer::FlatInstances(BRAY_HdParam &rparm,
	BRAY::ScenePtr &scene,
	SdfPath const &prototypeId,
	const BRAY::ObjectPtr &protoObj,
	const UT_Array<GfMatrix4d> &protoXform,
	int nsegs)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // Compute *all* the transforms, including parents, etc.
    UT_SmallArray<BRAY::SpacePtr>	 xforms;
    bool				 new_instance = false;
    BRAY::ObjectPtr			&inst = findOrCreate(prototypeId);

    if (!inst)
    {
	new_instance = true;
	inst = BRAY::ObjectPtr::createInstance(protoObj, GetId().GetString());
    }

    // If new instance, must be passed in valid xform.
    UT_ASSERT(!new_instance || protoXform.size());
    // Make an attribute list, but exclude all the tokens for transforms
    GT_AttributeListHandle alist = BRAY_HdUtil::makeAttributes(GetDelegate(),
		rparm,
		GetId(),
		HdInstancerTokens->instancer,
		-1,
		protoObj.objectProperties(scene),
		HdInterpolationInstance,
		&transformTokens());

    UT_StackBuffer<VtMatrix4dArray>	xformList(nsegs);
    UT_StackBuffer<float>		shutter_times(nsegs);
    syncPrimvars(false, nsegs);
    rparm.fillShutterTimes(shutter_times, nsegs);
    for (int i = 0; i < nsegs; ++i)
    {
	int	pidx = SYSmin(int(protoXform.size()-1), nsegs);
	xformList[i] = computeTransforms(prototypeId, true,
				&protoXform[pidx], shutter_times[i]);
    }

    // TODO: We should be able to get blur transforms in computeTransforms()
    BRAY_HdUtil::makeSpaceList(xforms, xformList.array(), nsegs);

    if (xforms.size() == 0)
	return;

    inst.setInstanceTransforms(xforms);
    inst.setInstanceAttributes(scene, alist);
    inst.setInstanceIds(instanceIdsForPrototype(prototypeId));
    inst.validateInstance();

    if (new_instance)
	scene.updateObject(inst, BRAY_EVENT_NEW);
    else
	scene.updateObject(inst, BRAY_EVENT_XFORM);
}

void
BRAY_HdInstancer::eraseFromScenegraph(BRAY::ScenePtr &scene)
{
    // post delete for all instances
    for (auto &&inst : myInstanceMap)
    {
	UT_ASSERT(inst.second);
	scene.updateObject(inst.second, BRAY_EVENT_DEL);
    }

    // also post delete for the scenegraph (if we have one)
    if (mySceneGraph)
	scene.updateObject(mySceneGraph, BRAY_EVENT_DEL);
}

BRAY::ObjectPtr &
BRAY_HdInstancer::findOrCreate(const SdfPath &prototypeId)
{
    UT_Lock::Scope	lock(myLock);
    // If this is a new entry in the map, it will be initialized by the caller
    return myInstanceMap[prototypeId];
}

PXR_NAMESPACE_CLOSE_SCOPE

