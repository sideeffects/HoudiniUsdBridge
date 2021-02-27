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

#include "BRAY_HdMesh.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdParam.h"
#include "BRAY_HdInstancer.h"
#include <pxr/imaging/pxOsd/tokens.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_ErrorLog.h>
#include <GT/GT_DANumeric.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_PrimSubdivisionMesh.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_HydraUtils.h>
#include <iostream>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
#if UT_ASSERT_LEVEL > 0
    // This should never be enabled in production builds
    //#define DISABLE_USD_THREADING_TO_DEBUG
#endif
#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    static UT_Lock	theLock;
#endif
    static constexpr UT_StringLit	theN("N");
    static constexpr UT_StringLit	theNormals("normals");
    static constexpr UT_StringLit	theLeftHanded("leftHanded");
    static const TfToken theNToken("N", TfToken::Immortal);
    static const TfToken theLeftHandedToken("leftHanded", TfToken::Immortal);

    static bool
    hasNormals(const GT_PrimPolygonMesh &pmesh)
    {
	for (auto &&a : { pmesh.getShared(), pmesh.getVertex() })
	{
	    if (a
		&& (a->get(theN.asHolder()) || a->get(theNormals.asHolder())))
	    {
		return true;
	    }
	}
	return false;
    }

    static bool
    renderOnlyHull(HdMeshGeomStyle style)
    {
	return style == HdMeshGeomStyleHull
	    || style == HdMeshGeomStyleHullEdgeOnly
	    || style == HdMeshGeomStyleHullEdgeOnSurf;
    }
}

BRAY_HdMesh::BRAY_HdMesh(SdfPath const &id, SdfPath const &instancerId)
    : HdMesh(id, instancerId)
    , myInstance()
    , myMesh()
    , myComputeN(false)
    , myLeftHanded(false)
    , myRefineLevel(-1)
{
}

BRAY_HdMesh::~BRAY_HdMesh()
{
}

void
BRAY_HdMesh::Finalize(HdRenderParam *renderParam)
{
    UT_ASSERT(myInstance || !GetInstancerId().IsEmpty());

    //const SdfPath	&id = GetId();
    BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr	&scene = rparm->getSceneForEdit();

    if (myMesh)
	scene.updateObject(myMesh, BRAY_EVENT_DEL);

    // First, notify the scene the instances are going away
    if (myInstance)
	scene.updateObject(myInstance, BRAY_EVENT_DEL);
    else
    {
	//UTdebugFormat("Can't delete instances right now");
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

void
BRAY_HdMesh::Sync(HdSceneDelegate *sceneDelegate,
        HdRenderParam *renderParam,
        HdDirtyBits *dirtyBits,
        TfToken const &repr)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    BRAY_HdParam	&rparm = *UTverify_cast<BRAY_HdParam *>(renderParam);
    _MeshReprConfig::DescArray descs = _GetReprDesc(repr);
    const HdMeshReprDesc &desc = descs[0];

#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    UT_Lock::Scope	single_thread(theLock);
#endif
    BRAY::ScenePtr	&scene = rparm.getSceneForEdit();

    // Get existing object properties
    BRAY::OptionSet	 props = myMesh.objectProperties(scene);

    bool			 props_changed = false;
    const SdfPath		&id = GetId();
    GT_DataArrayHandle		 counts, vlist;
    GT_AttributeListHandle	 alist[4];
    bool			 xform_dirty = false;
    BRAY_EventType		 event = BRAY_NO_EVENT;
    TfToken			 scheme;
    UT_Array<GT_PrimSubdivisionMesh::Tag>	subd_tags;

    BRAY_HdUtil::MaterialId			matId(*sceneDelegate, id);
    BRAY::MaterialPtr				material;
    UT_SmallArray<BRAY::FacesetMaterial>	fmats;
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId)
    {
	_SetMaterialId(sceneDelegate->GetRenderIndex().GetChangeTracker(),
		       matId.resolvePath());
    }

    int prevvblur = *props.bval(BRAY_OBJ_MOTION_BLUR) ?
        *props.ival(BRAY_OBJ_GEO_VELBLUR) : 0;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar)
    {
	props_changed = BRAY_HdUtil::updateObjectPrimvarProperties(props,
            *sceneDelegate, dirtyBits, id);
	event = props_changed ? (event | BRAY_EVENT_PROPERTIES) : event;
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id))
    {
        _UpdateVisibility(sceneDelegate, dirtyBits);

	BRAY_HdUtil::updateVisibility(sceneDelegate, id,
		props, IsVisible(), GetRenderTag(sceneDelegate));

	event = event | BRAY_EVENT_PROPERTIES;
	props_changed = true;
    }

    // For some reason we get material bit set for category updates.
    if (*dirtyBits & HdChangeTracker::DirtyCategories ||
	*dirtyBits & HdChangeTracker::DirtyMaterialId)
    {
	BRAY_HdUtil::updatePropCategories(rparm, sceneDelegate, this, props);
	event = event | BRAY_EVENT_TRACESET;
	props_changed = true;
    }

    props_changed |= BRAY_HdUtil::updateRprimId(props, this);

    if (props_changed && matId.IsEmpty())
	matId.resolvePath();

    //UTdebugFormat("MESH Material: {}", GetMaterialId());
    // Pull scene data
    bool	top_dirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
    auto	refineLvl = sceneDelegate->GetDisplayStyle(id).refineLevel;
    static constexpr HdInterpolation	thePtInterp[] = {
	HdInterpolationVarying,
	HdInterpolationVertex
    };
    static const TfToken &primType = HdPrimTypeTokens->mesh;
    if (!top_dirty && myMesh)
    {
	static UT_Set<TfToken>	theSkipN({
				    theNToken,
				});
	static UT_Set<TfToken>	theSkipLeft({
				    theLeftHandedToken,
				});
	const UT_Set<TfToken> *skipN = myComputeN ? &theSkipN : nullptr;
	// Check to see if the primvars are the same
	auto &&prim = myMesh.geometry();
	auto pmesh = UTverify_cast<const GT_PrimPolygonMesh *>(prim.get());
	if (!BRAY_HdUtil::matchAttributes(sceneDelegate, id, primType,
		    HdInterpolationConstant, pmesh->getDetail(), &theSkipLeft)
	    || !BRAY_HdUtil::matchAttributes(sceneDelegate, id, primType,
		    HdInterpolationUniform, pmesh->getUniform())
	    || !BRAY_HdUtil::matchAttributes(sceneDelegate, id, primType,
		    thePtInterp, SYSarraySize(thePtInterp), pmesh->getShared(),
                    skipN)
	    || !BRAY_HdUtil::matchAttributes(sceneDelegate, id, primType,
		    HdInterpolationFaceVarying, pmesh->getVertex(), skipN))
	{
	    top_dirty = true;
            props_changed = true;
	}
    }
    if (!myMesh || top_dirty || !matId.IsEmpty() || props_changed)
    {
#if 0
	UTdebugFormat("Topology: {} {}", myMesh.objectPtr(),
		HdChangeTracker::IsTopologyDirty(*dirtyBits, id));
#endif
	// Update topology
	auto &&top = HdMeshTopology(GetMeshTopology(sceneDelegate), refineLvl);
	myRefineLevel = SYSclamp(top.GetRefineLevel(), 0, SYS_INT8_MAX);

        // Force topo dirty if velocity blur toggles changed to make new blur P
        // attributes (can't really rely on updateAttributes() because it won't
        // do anything if P is not dirty)
        int currvblur = *props.bval(BRAY_OBJ_MOTION_BLUR) ?
            *props.ival(BRAY_OBJ_GEO_VELBLUR) : 0;
        top_dirty |= prevvblur != currvblur;

	if (top_dirty)
	{
	    event = (event | BRAY_EVENT_TOPOLOGY
			    | BRAY_EVENT_ATTRIB_P
			    | BRAY_EVENT_ATTRIB);

	    counts = BRAY_HdUtil::gtArray(top.GetFaceVertexCounts());
	    vlist = BRAY_HdUtil::gtArray(top.GetFaceVertexIndices());
	    UT_ASSERT(counts->getTupleSize() == 1 && vlist->getTupleSize() ==1);

	    GT_Size	nface = counts->entries();
	    GT_Size	nvtx = vlist->entries();
	    GT_Size	npts = -1;
	    if (vlist->getTupleSize() == 1)
	    {
		fpreal64	vmin, vmax;
		vlist->getMinMax(&vmin, &vmax);
		npts = vmax + 1;
	    }
            UT_ErrorLog::format(8,
                    "{} topology change: {} faces, {} vertices, {} points",
                    id, nface, nvtx, npts);

	    // TODO: GetPrimvarInstanceNames()
	    alist[3] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
			primType, 1, props, HdInterpolationConstant);
	    alist[2] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
			primType, nface, props, HdInterpolationUniform);
	    alist[1] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
			primType, npts, props, thePtInterp,
                        SYSarraySize(thePtInterp));
	    alist[0] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
			primType, nvtx, props, HdInterpolationFaceVarying);
	    myComputeN = false;

	    // Handle velocity/accel blur
	    if (*props.bval(BRAY_OBJ_MOTION_BLUR))
	    {
		alist[1] = BRAY_HdUtil::velocityBlur(alist[1],
				*props.ival(BRAY_OBJ_GEO_VELBLUR),
				*props.ival(BRAY_OBJ_GEO_SAMPLES),
				rparm);
	    }
            if (UT_ErrorLog::isMantraVerbose(8))
                BRAY_HdUtil::dump(id, alist);

	    scheme = top.GetScheme();

	    myLeftHanded = (top.GetOrientation() != HdTokens->rightHanded);
	}

	if (top_dirty || !matId.IsEmpty() || props_changed)
	{
	    event = (event | BRAY_EVENT_MATERIAL);

	    const auto &subsets = top.GetGeomSubsets();
	    if (subsets.size() > 0)
	    {
		for (const auto &set : subsets)
		{
                    if (!set.materialId.IsEmpty())
                    {
                        fmats.emplace_back(
                                BRAY_HdUtil::gtArray(set.indices),
                                scene.findMaterial(BRAY_HdUtil::toStr(set.materialId)));
                    }
		}
	    }
	    if (matId.IsEmpty() && fmats.isEmpty())
		matId.resolvePath();

	    material = scene.findMaterial(matId.path());
            if (!matId.IsEmpty() && !material.isValid())
            {
                UT_ErrorLog::error("Invalid material binding: {} -> {}",
                        GetId(), matId.path());
                UTdebugFormat("Invalid material binding: {} -> {}", GetId(), matId.path());
            }
	}
    }
    if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id) && myRefineLevel > 0)
    {
	UT_ASSERT(top_dirty && "The scheme might not be set?");
	if (scheme == PxOsdOpenSubdivTokens->catmullClark)
	{
	    PxOsdSubdivTags subdivTags = sceneDelegate->GetSubdivTags(id);
	    XUSD_HydraUtils::processSubdivTags(subdivTags, subd_tags);
	}
    }
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id))
    {
	xform_dirty = true;
	BRAY_HdUtil::xformBlur(sceneDelegate, rparm, id, myXform, props);
    }

    if (myMesh && !(event & BRAY_EVENT_TOPOLOGY))
    {
	auto &&prim = myMesh.geometry();
	auto pmesh = UTverify_cast<GT_PrimPolygonMesh *>(prim.get());
	// Check to see if any variables are dirty
	bool updated = false;
	updated |= BRAY_HdUtil::updateAttributes(sceneDelegate, rparm,
	    dirtyBits, id, pmesh->getDetail(), alist[3], event, props,
	    HdInterpolationConstant);
	updated |= BRAY_HdUtil::updateAttributes(sceneDelegate, rparm,
	    dirtyBits, id, pmesh->getUniform(), alist[2], event, props,
	    HdInterpolationUniform);
	updated |= BRAY_HdUtil::updateAttributes(sceneDelegate, rparm,
	    dirtyBits, id, pmesh->getShared(), alist[1], event, props,
	    thePtInterp, SYSarraySize(thePtInterp));
	updated |= BRAY_HdUtil::updateAttributes(sceneDelegate, rparm,
	    dirtyBits, id, pmesh->getVertex(), alist[0], event, props,
	    HdInterpolationFaceVarying);

	if (updated)
	{
	    // if there was an update on any primvar
	    // we need to make sure that any 'other' primvar
	    // that was not updated ends up being in alists[]
	    // so that we can construct the new prim with all the
	    // updated and non-updated primvars
	    if (!alist[0])
		alist[0] = pmesh->getVertex();
	    if (!alist[1])
		alist[1] = pmesh->getShared();
	    if (!alist[2])
		alist[2] = pmesh->getUniform();
	    if (!alist[3])
		alist[3] = pmesh->getDetail();

            if (UT_ErrorLog::isMantraVerbose(8))
                BRAY_HdUtil::dump(id, alist);
	}
    }

    if (!myMesh || event)
    {
	GT_PrimPolygonMesh	*pmesh = nullptr;
	GT_PrimitiveHandle	 prim;
	bool			 valid = true;

	if (myMesh)
	    prim = myMesh.geometry();

	if (!counts || !vlist)
	{
	    UT_ASSERT(prim);
	    pmesh = UTverify_cast<GT_PrimPolygonMesh *>(prim.get());
	}
	if (!(event & (BRAY_EVENT_ATTRIB|BRAY_EVENT_ATTRIB_P)))
	{
	    // There should be no updates to any of the attributes
	    UT_ASSERT(prim && !alist[0] && !alist[2] && !alist[3]);
	    alist[0] = pmesh->getVertex();
	    alist[1] = pmesh->getShared();
	    alist[2] = pmesh->getUniform();
	    alist[3] = pmesh->getDetail();
	}
	if (!counts)
	    counts = pmesh->getFaceCounts();
	if (!vlist)
	    vlist = pmesh->getVertexList();

	if (!alist[1] || !alist[1]->get("P"))
        {
            UT_ErrorLog::error("Mesh {} missing position primvar", id);
	    valid = false;
        }

	if (valid && scheme.IsEmpty())
	{
	    // Unknown scheme (some event other than topology update).
	    scheme = PxOsdOpenSubdivTokens->bilinear;
	    const GT_PrimSubdivisionMesh *primsubd = nullptr;
	    if (myMesh)
		primsubd = dynamic_cast<const GT_PrimSubdivisionMesh *>(
		    myMesh.geometry().get());

	    if (primsubd)
	    {
		scheme = PxOsdOpenSubdivTokens->catmullClark;
		// copy subd tags
		for (auto it = primsubd->beginTags(); !it.atEnd(); ++it)
		    subd_tags.append(*it);
	    }
	}

	if (!myLeftHanded)
	{
	    // Make orientation detail attribute for BRAY
	    // (Assumed to be lefthanded if it doesn't exist)
	    GT_Int32Array *attr = new GT_Int32Array(0, 1);
	    attr->append(0);

	    if (!alist[3])
	    {
		alist[3] = GT_AttributeList::createAttributeList(
		    theLeftHanded.asRef(), attr);
	    }
	    else
	    {
		alist[3] = alist[3]->addAttribute(
		    theLeftHanded.asRef(), attr, true);
	    }
	}

	//if (0 && valid && !renderOnlyHull(desc.geomStyle) &&
	UT_ASSERT(myRefineLevel >= 0);
	// Husk sets the refine level to 2 for medium or less
	if (valid
		&& myRefineLevel > 2
		&& !renderOnlyHull(desc.geomStyle)
		&& scheme == PxOsdOpenSubdivTokens->catmullClark)
	{
            UT_ErrorLog::format(8, "{} create subdivision surface", id);
	    auto subd = new GT_PrimSubdivisionMesh(counts, vlist,
		    alist[1],	// Shared
		    alist[0],	// Vertex
		    alist[2],	// Uniform
		    alist[3]);	// detail

	    for (int i = 0; i < subd_tags.size(); ++i)
		subd->appendTag(subd_tags[i]);

	    pmesh = subd;
	}
	else
	{
	    if (!valid)
	    {
		// Empty mesh
                UT_ErrorLog::warning("{} invalid mesh", id);
		pmesh = new GT_PrimPolygonMesh(
			new GT_Int32Array(0, 1),
			new GT_Int32Array(0, 1),
			GT_AttributeList::createAttributeList(
				"P", new GT_Real32Array(0, 3)
			),
			GT_AttributeListHandle(),
			GT_AttributeListHandle(),
			GT_AttributeListHandle());
	    }
	    else
	    {
		if (myComputeN)
		{
		    alist[0] = alist[0]->removeAttribute(theN.asRef());
		    myComputeN = false;
		}
                UT_ErrorLog::format(8, "{} create polygon mesh", id);
		pmesh = new GT_PrimPolygonMesh(counts, vlist,
			alist[1],	// Shared
			alist[0],	// Vertex
			alist[2],	// Uniform
			alist[3]);	// detail
		if (!hasNormals(*pmesh))
		{
		    UT_UniquePtr<GT_PrimPolygonMesh> newmesh;
		    newmesh.reset(pmesh->createVertexNormalsIfMissing());
		    if (newmesh && newmesh.get() != pmesh)
		    {
			if (!myLeftHanded)
			{
			    // Vertex normals are computed with the assumption
			    // that pmesh is left-handed, so must be flipped
			    const GT_AttributeListHandle &attrlist =
				newmesh->getVertex();
			    const GT_DataArrayHandle oldnmls =
				attrlist->get(GA_Names::N);

			    UT_ASSERT(oldnmls && oldnmls->getTupleSize() == 3);
			    auto nmls = new GT_Real32Array(oldnmls->entries(),
				3, GT_TYPE_NORMAL);

			    // flip
			    for (exint i = 0; i < oldnmls->entries(); ++i)
			    {
				UT_Vector3 n;
				oldnmls->import(i, n.data());
				n *= -1.0f;
				nmls->setTuple(n.data(), i);
			    }

			    GT_AttributeListHandle newattrlist =
				attrlist->addAttribute(GA_Names::N,
					GT_DataArrayHandle(nmls), true);
			    newmesh.reset(new GT_PrimPolygonMesh(counts, vlist,
						    alist[1],	// Shared
						    newattrlist,// Vertex
						    alist[2],	// Uniform
						    alist[3]));	// detail
			}

			delete pmesh;
			pmesh = newmesh.release();
			myComputeN = true;
		    }
		}
	    }
	}

	prim.reset(pmesh);
	//prim->dumpPrimitive();
	if (myMesh)
	{
	    myMesh.setGeometry(prim);
	    scene.updateObject(myMesh, event);
	}
	else
	{
	    UT_ASSERT(xform_dirty);
	    xform_dirty = false;
	    myMesh = BRAY::ObjectPtr::createGeometry(prim);
	}
    }

    // 4. Populate instance objects.
    // If the mesh is instanced, create one new instance per transform.
    // TODO: The current instancer invalidation tracking makes it hard for
    // HdKarma to tell whether transforms will be dirty, so this code pulls
    // them every frame.
    UT_SmallArray<BRAY::SpacePtr>	xforms;
    BRAY_EventType			iupdate = BRAY_NO_EVENT;
    if (GetInstancerId().IsEmpty())
    {
	// Otherwise, create our single instance (if necessary) and update
	// the transform (if necessary).
	if (!myInstance || xform_dirty)
        {
	    xforms.append(BRAY_HdUtil::makeSpace(myXform.data(),
		myXform.size()));
        }
        if (UT_ErrorLog::isMantraVerbose(8) && xforms.size())
            BRAY_HdUtil::dump(id, xforms);

	if (!myInstance)
	{
	    UT_ASSERT(xforms.size());
	    // TODO:  Update new object
	    myInstance = BRAY::ObjectPtr::createInstance(myMesh,
                    BRAY_HdUtil::toStr(id));
	    myInstance.setInstanceTransforms(xforms);
	    iupdate = BRAY_EVENT_NEW;
	}
	else if (xforms.size())
	{
	    // TODO: Update transform dirty
	    myInstance.setInstanceTransforms(xforms);
	    iupdate = BRAY_EVENT_XFORM;
	}
    }
    else
    {
	// Here, we are part of an instance object, so it's the instance object
	// that interfaces with the batch scene.
	UT_ASSERT(!myInstance);

	// Retrieve instance transforms from the instancer.
	HdRenderIndex	&renderIndex = sceneDelegate->GetRenderIndex();
	HdInstancer	*instancer = renderIndex.GetInstancer(GetInstancerId());
	auto		 minst = UTverify_cast<BRAY_HdInstancer *>(instancer);
	if (scene.nestedInstancing())
	    minst->NestedInstances(rparm, scene, GetId(), myMesh, myXform, props);
	else
	    minst->FlatInstances(rparm, scene, GetId(), myMesh, myXform, props);
    }

    // Set the material *after* we create the instance hierarchy so that
    // instance primvar variants are known.
    if (myMesh && (material || fmats.size() || props_changed))
	myMesh.setMaterial(scene, material, props, fmats.size(), fmats.array());

    // Now the mesh is all up to date, send the instance update
    if (iupdate != BRAY_NO_EVENT)
	scene.updateObject(myInstance, iupdate);

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

HdDirtyBits
BRAY_HdMesh::GetInitialDirtyBitsMask() const
{
    static const int	mask = HdChangeTracker::Clean
	| HdChangeTracker::InitRepr
	| HdChangeTracker::DirtyCullStyle
	| HdChangeTracker::DirtyDoubleSided
	| HdChangeTracker::DirtyInstanceIndex
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
	;

    return (HdDirtyBits)mask;
}

HdDirtyBits
BRAY_HdMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
BRAY_HdMesh::_InitRepr(TfToken const &repr,
	HdDirtyBits *dirtyBits)
{
    TF_UNUSED(repr);
    TF_UNUSED(dirtyBits);
}

PXR_NAMESPACE_CLOSE_SCOPE
