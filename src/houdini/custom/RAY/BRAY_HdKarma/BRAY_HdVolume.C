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

#include "BRAY_HdVolume.h"
#include "BRAY_HdField.h"
#include "BRAY_HdInstancer.h"
#include "BRAY_HdParam.h"
#include "BRAY_HdUtil.h"

#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_HydraUtils.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/base/gf/matrix4f.h>
#include <UT/UT_Lock.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
//#define DISABLE_USD_THREADING_TO_DEBUG
#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    static UT_Lock	theLock;
#endif

    static constexpr uint AllDirty = ~0;
}

/// Public methods
BRAY_HdVolume::BRAY_HdVolume(const SdfPath& id, const SdfPath& instancerId)
    : HdVolume(id, instancerId)
{
}

void
BRAY_HdVolume::Finalize(HdRenderParam* renderParam)
{
    UT_ASSERT(myInstance || !GetInstancerId().IsEmpty());

    BRAY::ScenePtr &scene =
	UTverify_cast<BRAY_HdParam*>(renderParam)->getSceneForEdit();

    if (myVolume)
	scene.updateObject(myVolume, BRAY_EVENT_DEL);

    if (myInstance)
	scene.updateObject(myInstance, BRAY_EVENT_DEL);
}

HdDirtyBits
BRAY_HdVolume::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

/// Protected methods
HdDirtyBits
BRAY_HdVolume::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
BRAY_HdVolume::_InitRepr(const TfToken& repr, HdDirtyBits* dirtyBits)
{
    TF_UNUSED(repr);
    TF_UNUSED(dirtyBits);
}

void
BRAY_HdVolume::Sync(HdSceneDelegate* sceneDelegate,
        HdRenderParam *renderParam,
        HdDirtyBits *dirtyBits,
        const TfToken &repr)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    BRAY_HdParam &rparm = *UTverify_cast<BRAY_HdParam*>(renderParam);
#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    UT_Lock::Scope  single_threaded(theLock);
#endif

    BRAY::ScenePtr&		scene = rparm.getSceneForEdit();
    BRAY::MaterialPtr		material;
    BRAY::ObjectPtr::FieldList	fields;
    const SdfPath&		id = GetId();
    BRAY_HdUtil::MaterialId	matId(*sceneDelegate, id);
    GT_AttributeListHandle	clist;
    BRAY_EventType		event = BRAY_NO_EVENT;
    bool			xform_dirty = false;
    bool			update_required = false;
    BRAY::OptionSet		props = myVolume.objectProperties(scene);
    bool			props_changed = false;

    // Handle materials
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId)
    {
	_SetMaterialId(sceneDelegate->GetRenderIndex().GetChangeTracker(),
	    matId.resolvePath());
    }

    // Update settings first
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar)
    {
	props_changed = BRAY_HdUtil::updateObjectPrimvarProperties(props,
		*sceneDelegate, dirtyBits, id);
	event = props_changed ? (event | BRAY_EVENT_PROPERTIES) : event;
    }

    if (*dirtyBits & HdChangeTracker::DirtyCategories)
    {
	BRAY_HdUtil::updatePropCategories(rparm, sceneDelegate, this, props);
	event = event | BRAY_EVENT_TRACESET;
	props_changed = true;
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id))
    {
	_UpdateVisibility(sceneDelegate, dirtyBits);

	BRAY_HdUtil::updateVisibility(sceneDelegate, id,
		props, IsVisible(), GetRenderTag(sceneDelegate));

	event = event | BRAY_EVENT_PROPERTIES;
	props_changed = true;

    }

    props_changed |= BRAY_HdUtil::updateRprimId(props, this);

    if (props_changed && matId.IsEmpty())
	matId.resolvePath();

    // Update transforms
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id))
    {
	UTdebugFormat("{} : transform dirty ", id);
	xform_dirty = true;
	BRAY_HdUtil::xformBlur(sceneDelegate, rparm, id, myXform, props);
    }

    // Any update to the underlying field is marked as a topology update
    // on the volume containing that field. Hence we can safely collect
    // all the fields here. Since updates to the underlying fields are
    // are processed in the fields themselves, the fetching operation of
    // field data is relatively lightweight here.
    bool topoDirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
    static const TfToken &primType = HdPrimTypeTokens->volume;

    // Iterate through all fields this volume has
    bool fieldchanged = false;
    for (auto&& fdesc : sceneDelegate->GetVolumeFieldDescriptors(id))
    {
	HdBprim* bprim = sceneDelegate->GetRenderIndex().
	    GetBprim(fdesc.fieldPrimType, fdesc.fieldId);

	if (bprim)
	{
	    // TODO: potential optimization
	    // NOTE: we are currently pulling out all the xforms and field 
	    // data from the underlying field no matter what.
	    auto&& field = UTverify_cast<BRAY_HdField*>(bprim);
	    fields.emplace_back(std::make_pair(field->getFieldName(),
					       field->getGTPrimitive()));

	    // register the rprim with the bprim as for updates
	    fieldchanged |= field->registerVolume(id.GetText());
	}
    }
    if (!topoDirty && myVolume)
    {
	// Check to see if the primvars are the same
	if (!BRAY_HdUtil::matchAttributes(sceneDelegate, id, primType,
		    HdInterpolationConstant, myVolume.volumeDetailAttributes()))
	{
	    topoDirty = true;
	}
    }

    if (!myVolume || topoDirty || fieldchanged)
    {
	// Volumes have only constant attributes
	clist = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
		primType, 1, props, HdInterpolationConstant);
	update_required = true;

	event = (event  | BRAY_EVENT_TOPOLOGY
			| BRAY_EVENT_ATTRIB_P
			| BRAY_EVENT_ATTRIB);
    }

#if 0
    for (auto& x : myXform)
	UTdebugFormat("{} : {}", id, x);
#endif

    // return immediately in case we were not able to find prims
    if (!fields.size() && !myVolume)
    {
	UT_ASSERT(0 && "No prim found");
	return;
    }

    // Check for updates with regards to constant primvars
    if (myVolume && !(event & BRAY_EVENT_TOPOLOGY))
    {
	auto&& dattribs = myVolume.volumeDetailAttributes();
	update_required |= BRAY_HdUtil::updateAttributes(sceneDelegate, rparm,
				dirtyBits, id, dattribs, clist,
				event, props, HdInterpolationConstant);
    }

    if (!myVolume || event)
    {
	// If no volume was present, create the actual geometry in BRAY_GTVolume
	// else update the existing geometry and attributes
	if (!myVolume)
	{
	    myVolume = BRAY::ObjectPtr::createVolume(id.GetText());
	    update_required = true;
	}

	if (update_required)
	{
	    myVolume.setVolume(scene, clist, fields);
	    if (myInstance && event)
	    {
		// Needed to update bounds in the accelerator
		scene.updateObject(myVolume, event);
	    }
	}
    }

    if (!matId.IsEmpty())
	material = scene.findMaterial(matId.path());

    // Populate the instancer object
    UT_SmallArray<BRAY::SpacePtr>	xforms;
    BRAY_EventType			iupdate = BRAY_NO_EVENT;
    if (GetInstancerId().IsEmpty())
    {
	if (!myInstance || xform_dirty)
	    xforms.append(BRAY_HdUtil::makeSpace(myXform.data(), 
		myXform.size()));

	if (!myInstance)
	{
	    UT_ASSERT(xforms.size());
	    myInstance = BRAY::ObjectPtr::createInstance(myVolume,
                    BRAY_HdUtil::toStr(id));
	    myInstance.setInstanceTransforms(xforms);
	    iupdate = BRAY_EVENT_NEW;
	}
	else if (xforms.size())
	{
	    myInstance.setInstanceTransforms(xforms);
	    iupdate = BRAY_EVENT_XFORM;
	}
    }
    else
    {
	// TODO: there's a bug with regards to the point instancer from USD
	// Rendering with instances might look weird/totally absent.
	UT_ASSERT(!myInstance);
	HdRenderIndex	&renderIndex = sceneDelegate->GetRenderIndex();
	HdInstancer	*instancer = renderIndex.GetInstancer(GetInstancerId());
	auto		minst = UTverify_cast<BRAY_HdInstancer*>(instancer);
	if (scene.nestedInstancing())
	    minst->NestedInstances(rparm, scene, GetId(), myVolume, myXform,
				BRAY_HdUtil::xformSamples(rparm, props));
	else
	    minst->FlatInstances(rparm, scene, GetId(), myVolume, myXform,
				BRAY_HdUtil::xformSamples(rparm, props));
    }

    // Set the material *after* we create the instance hierarchy so that
    // instance primvar variants are known.
    if (myVolume && (material || props_changed))
	myVolume.setMaterial(scene, material, props);

    // Now the volume is all up to date, send the instance update
    if (iupdate != BRAY_NO_EVENT)
	scene.updateObject(myInstance, iupdate);

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
