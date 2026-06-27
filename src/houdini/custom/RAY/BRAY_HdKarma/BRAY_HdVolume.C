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
#include "BRAY_HdFormat.h"

#include <pxr/imaging/hd/enums.h>
#include <pxr/base/gf/matrix4f.h>
#include <UT/UT_Lock.h>
#include <UT/UT_ErrorLog.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
//#define DISABLE_USD_THREADING_TO_DEBUG
#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    static UT_Lock	theLock;
#endif
}

/// Public methods
BRAY_HdVolume::BRAY_HdVolume(const SdfPath& id)
    : HdVolume(id)
{
}

void
BRAY_HdVolume::Finalize(HdRenderParam* renderParam)
{
    UT_ASSERT(myInstance || !GetInstancerId().IsEmpty());

    BRAY::ScenePtr &scene =
	UTverify_cast<BRAY_HdParam*>(renderParam)->getSceneForEdit();

    if (myInstance)
    {
	scene.updateObject(myInstance, BRAY_EVENT_DEL);
    }
    else
    {
        BRAY_HdParam     *rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
        BRAY_HdInstancer *instancer = UTverify_cast<BRAY_HdInstancer*>(
            rparm->getInstancer(GetInstancerId()));
        if (instancer)
            instancer->erasePrototype(GetId(), scene);
    }

    if (myVolume)
	scene.updateObject(myVolume, BRAY_EVENT_DEL);

    myVolume = BRAY::ObjectPtr();
    myInstance = BRAY::ObjectPtr();
}

HdDirtyBits
BRAY_HdVolume::GetInitialDirtyBitsMask() const
{
    // No need to set VolumeField bit (set by HdField)

    static const int	mask = HdChangeTracker::Clean
	| HdChangeTracker::InitRepr
	| HdChangeTracker::DirtyCullStyle
	| HdChangeTracker::DirtyDoubleSided
	| HdChangeTracker::DirtyInstanceIndex
	| HdChangeTracker::DirtyInstancer
	| HdChangeTracker::DirtyMaterialId
	| HdChangeTracker::DirtyNormals
	| HdChangeTracker::DirtyParams
	| HdChangeTracker::DirtyPoints
	| HdChangeTracker::DirtyPrimvar
	| HdChangeTracker::DirtySubdivTags
	| HdChangeTracker::DirtyTopology
	| HdChangeTracker::DirtyTransform
	| HdChangeTracker::DirtyVisibility
	| HdChangeTracker::DirtyCategories
	| HdChangeTracker::DirtyExtent
	;

    return (HdDirtyBits)mask;
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

    if (BRAY_HdUtil::noDirtyBits(*dirtyBits))
        return;

    BRAY_HdParam &rparm = *UTverify_cast<BRAY_HdParam*>(renderParam);
#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    UT_Lock::Scope  single_threaded(theLock);
#endif

    BRAY::ScenePtr&		scene = rparm.getSceneForEdit();
    BRAY::MaterialPtr		material;
    BRAY::ObjectPtr::FieldList	fields;
    const SdfPath&		id = GetId();
    BRAY_HdUtil::MaterialId	matId(*sceneDelegate, id);
    BRAY_EventType		event = BRAY_NO_EVENT;
    bool			xform_dirty = false;
    bool			update_required = false;
    BRAY::OptionSet		props = myVolume.objectProperties(scene);
    bool			props_changed = false;

    // Handle materials
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId)
	SetMaterialId(matId.resolvePath());

    // Update settings first
    static const TfToken &primType = HdPrimTypeTokens->volume;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar)
    {
	int prevvblur = *props.bval(BRAY_OBJ_MOTION_BLUR) ?
            *props.ival(BRAY_OBJ_GEO_VELBLUR) : 0;
	props_changed = BRAY_HdUtil::updateObjectPrimvarProperties(props,
		*sceneDelegate, dirtyBits, id, primType);

        // updateObjectPrimvarProperties might delete the light category links
        // when it shouldn't and if so we set the dirty bit to
        // recreate the links.
        if (props_changed)
            *dirtyBits = *dirtyBits | HdChangeTracker::DirtyCategories;

	event = props_changed ? (event | BRAY_EVENT_PROPERTIES) : event;

        // Handle instantaneous blur toggle (no need to worry about
        // update/dirty flag since it triggers render restart)
        if (rparm.disableMotionBlur())
            props.set(BRAY_OBJ_GEO_VELBLUR, int64(0));
	int currvblur = *props.bval(BRAY_OBJ_MOTION_BLUR) ?
            *props.ival(BRAY_OBJ_GEO_VELBLUR) : 0;
        update_required |= prevvblur != currvblur;
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
	xform_dirty = true;
	BRAY_HdUtil::xformBlur(sceneDelegate, rparm, id, myXform, props);
    }

    // Any update to the underlying field is marked as a topology update
    // on the volume containing that field. Hence we can safely collect
    // all the fields here. Since updates to the underlying fields are
    // are processed in the fields themselves, the fetching operation of
    // field data is relatively lightweight here.
    bool topoDirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);

    // Iterate through all fields this volume has
    bool vdbpoint = false;
    bool fieldchanged = ((*dirtyBits & HdChangeTracker::DirtyVolumeField) != 0);
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
            // Use fieldName from the volume field descriptors (which has the
            // field relationship name), not the fieldName from the field prim.
	    fields.emplace_back(std::make_pair(fdesc.fieldName,
					       field->getGTPrimitive()));

	    // register the rprim with the bprim as for updates
	    fieldchanged |= field->registerVolume(BRAY_HdUtil::toStr(id),
                renderParam);

	    vdbpoint |= BRAY_HdUtil::isVDBPoint(field->getGTPrimitive());
	}
    }
    if (!topoDirty && myVolume && !vdbpoint)
    {
	// Check to see if the primvars are the same

        // we need to detect changes on karma:width_scale prop
        // if this is a vdbpoint
        bool skip_namespace = !vdbpoint;

	if (!BRAY_HdUtil::matchAttributes(sceneDelegate, rparm, id, primType,
		    HdInterpolationConstant, myDetailAttribs,
                    BRAY_HdUtil::PrimvarSpan(), nullptr, skip_namespace))
	{
	    topoDirty = true;
	}
    }

    if (!myVolume || topoDirty || fieldchanged)
    {
	// Volumes have only constant attributes
	myDetailAttribs = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
		primType, 1, props, HdInterpolationConstant,
                BRAY_HdUtil::PrimvarSpan());
	update_required = true;

	event = (event  | BRAY_EVENT_TOPOLOGY
			| BRAY_EVENT_ATTRIB_P
			| BRAY_EVENT_ATTRIB);
    }

#if 0
    for (auto& x : myXform)
	UTdebugFormat("{} : {}", id, x);
#endif

    // Check for updates with regards to constant primvars
    if (myVolume && !(event & BRAY_EVENT_TOPOLOGY) && !vdbpoint)
    {
	auto&& dattribs = myVolume.volumeDetailAttributes();
        myDetailAttribs = nullptr;
	update_required |= BRAY_HdUtil::updateAttributes(sceneDelegate, rparm,
				dirtyBits, id, dattribs, myDetailAttribs,
				event, props, HdInterpolationConstant);
    }

    UT_BoundingBox extent;
    extent.initBounds();
    if (HdChangeTracker::IsExtentDirty(*dirtyBits, id))
    {
        VtValue vtval =
            BRAY_HdUtil::evalVt(sceneDelegate, id, HdTokens->extent);
        if (vtval.IsHolding<VtVec3fArray>())
        {
            VtVec3fArray varray = vtval.UncheckedGet<VtVec3fArray>();
            UT_Vector3 extentmin, extentmax;
            std::copy(varray[0].data(), varray[0].data() + 3, extentmin.data());
            std::copy(varray[1].data(), varray[1].data() + 3, extentmax.data());
            //myVolume.setExtent(extentmin, extentmax);
            extent = UT_BoundingBox(extentmin ,extentmax);

            update_required = true;
            event = (event  | BRAY_EVENT_TOPOLOGY
                            | BRAY_EVENT_ATTRIB_P
                            | BRAY_EVENT_ATTRIB);
        }
    }

    if (!myVolume || event)
    {
	// If no volume was present, create the actual geometry in BRAY_GTVolume
	// else update the existing geometry and attributes

	if (!myVolume)
	{
	    if (vdbpoint)
	    {
                UT_ErrorLog::format(8, "create point mesh");
                GT_PrimitiveHandle pointprim =
                    BRAY_HdUtil::createPointPrimFromVDB(fields, props, rparm, myDetailAttribs);
                myVolume = scene.createGeometry(pointprim);
                // for vdp point (converted to pointmesh) we don't need to
                // call the update if object is initialized first time. The last
                // scene.update() on the instance is enough.
                // See BRAY_HdPointPrim::Sync()
                update_required = false;
	    }
            else
            {
                myVolume = scene.createVolume(BRAY_HdUtil::toStr(id));
                update_required = true;
            }
	}

	if (update_required)
	{
            if (vdbpoint)
            {
                GT_PrimitiveHandle pointprim =
                    BRAY_HdUtil::createPointPrimFromVDB(fields, props, rparm, myDetailAttribs);
                myVolume.setGeometry(scene, pointprim);
            }
            else
		myVolume.setVolume(scene, myDetailAttribs, fields, extent);
            // Needed to update bounds in the accelerator
            scene.updateObject(myVolume, event);
	}
    }

    if (!matId.IsEmpty())
	material = scene.findMaterial(matId.path());

    // Populate the instancer object

    // Make sure our instancer and it's parent instancers are synced.
    _UpdateInstancer(sceneDelegate, dirtyBits);
    HdInstancer::_SyncInstancerAndParents(
        sceneDelegate->GetRenderIndex(), GetInstancerId());

    UT_SmallArray<BRAY::SpacePtr>	xforms;
    BRAY_EventType			iupdate = BRAY_NO_EVENT;
    if (GetInstancerId().IsEmpty())
    {
	if (!myInstance || xform_dirty)
	    xforms.append(BRAY_HdUtil::makeSpace(myXform.data(),
		myXform.size(), props));

	if (!myInstance)
	{
	    UT_ASSERT(xforms.size());
	    myInstance = scene.createInstance(myVolume,
                    BRAY_HdUtil::toStr(id));
	    myInstance.setInstanceTransforms(scene, xforms);
	    iupdate = BRAY_EVENT_NEW;
	}
	else if (xforms.size())
	{
	    myInstance.setInstanceTransforms(scene, xforms);
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

        minst->NestedInstances(rparm, scene, GetId(), myVolume, myXform, props);
    }

    // Set the material *after* we create the instance hierarchy so that
    // instance primvar variants are known.
    if (myVolume && (material || props_changed))
    {
        UT_ErrorLog::format(8, "Assign {} to {}", matId.path(), id);
	myVolume.setMaterial(scene, material, props);
        scene.updateObject(myVolume, BRAY_EVENT_MATERIAL);
    }

    myVolume.setCoordSysAliases(scene,
            BRAY_HdUtil::getCoordSysBindings(sceneDelegate, id));

    // Now the volume is all up to date, send the instance update
    if (iupdate != BRAY_NO_EVENT)
	scene.updateObject(myInstance, iupdate);

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void
BRAY_HdVolume::UpdateRenderTag(HdSceneDelegate *delegate,
    HdRenderParam *renderParam)
{
    const TfToken prevtag = GetRenderTag();
    HdVolume::UpdateRenderTag(delegate, renderParam);

    // If the mesh hadn't been previously synced, don't attempt to update it.
    if (!myVolume || GetRenderTag() == prevtag)
        return;

    BRAY_HdParam	&rparm = *UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr	&scene = rparm.getSceneForEdit();
    BRAY::OptionSet	 props = myVolume.objectProperties(scene);

    BRAY_HdUtil::updateVisibility(delegate, GetId(),
            props, IsVisible(), GetRenderTag(delegate));
    scene.updateObject(myVolume, BRAY_EVENT_PROPERTIES);
}

PXR_NAMESPACE_CLOSE_SCOPE
