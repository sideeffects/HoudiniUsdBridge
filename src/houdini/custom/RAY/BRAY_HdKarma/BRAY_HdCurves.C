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

#include "BRAY_HdCurves.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdParam.h"
#include "BRAY_HdInstancer.h"
#include "BRAY_HdIO.h"
#include <UT/UT_ErrorLog.h>
#include <GT/GT_DANumeric.h>
#include <GT/GT_PrimCurveMesh.h>
#include <UT/UT_FSA.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_HydraUtils.h>
#include <gusd/GT_VtArray.h>
#include <gusd/UT_Gf.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
#define DISABLE_USD_THREADING_TO_DEBUG
#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    static UT_Lock	theLock;
#endif

    static const TfToken	thePinnedToken("pinned", TfToken::Immortal);

    /// convert usd curve type to GT curve type
    static GT_Basis
    usdCurveTypeToGt(const HdBasisCurvesTopology& top)
    {
	const auto&& type = top.GetCurveType();
	if (type == HdTokens->linear)
	{
	    return GT_BASIS_LINEAR;
	}
	else if (type == HdTokens->cubic)
	{
	    const auto&& basis = top.GetCurveBasis();
	    if (basis == HdTokens->bezier)
	    {
		return GT_BASIS_BEZIER;
	    }
	    else if (basis == HdTokens->bSpline)
	    {
		return GT_BASIS_BSPLINE;
	    }
	    else if (basis == HdTokens->catmullRom)
	    {
		return GT_BASIS_CATMULLROM;
	    }
	    else
	    {
		BRAYerror("Unsupported curve basis {}. Using linear curves.",
			basis);
		UT_ASSERT(0);
		return GT_BASIS_LINEAR;
	    }
	}
	else
	{
	    BRAYerror("Unsupported curve type {}.  Using linear curves.", type);
	    UT_ASSERT(0);
	    return GT_BASIS_LINEAR;
	}
    }
}

BRAY_HdCurves::BRAY_HdCurves(SdfPath const &id, SdfPath const &instancerId)
    : HdBasisCurves(id, instancerId)
    , myInstance()
    , myMesh()
{
}

BRAY_HdCurves::~BRAY_HdCurves()
{
}

void
BRAY_HdCurves::Finalize(HdRenderParam *renderParam)
{
    UT_ASSERT(myInstance || !GetInstancerId().IsEmpty());

    BRAY::ScenePtr &scene =
	UTverify_cast<BRAY_HdParam *>(renderParam)->getSceneForEdit();

    // First, notify the scene the instances are going away
    if (myInstance)
	scene.updateObject(myInstance, BRAY_EVENT_DEL);
    else
    {
	UTdebugFormat("Can't delete instances right now");
    }
    if (myMesh)
	scene.updateObject(myMesh, BRAY_EVENT_DEL);
}

void
BRAY_HdCurves::Sync(HdSceneDelegate *sceneDelegate,
				HdRenderParam *renderParam,
				HdDirtyBits *dirtyBits,
				TfToken const &repr)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

#if 0
    _BasisCurvesReprConfig::DescArray descs = _GetReprDesc(repr);
    const HdBasisCurvesReprDesc &desc = descs[0];
#endif

    BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);

    updateGTCurves(*rparm, sceneDelegate, dirtyBits, _GetReprDesc(repr)[0]);
}

void
BRAY_HdCurves::updateGTCurves(BRAY_HdParam &rparm,
	HdSceneDelegate *sceneDelegate,
	HdDirtyBits *dirtyBits,
	HdBasisCurvesReprDesc const &desc)
{
#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    UT_Lock::Scope	single_thread(theLock);
#endif
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    BRAY::ScenePtr		&scene = rparm.getSceneForEdit();
    const SdfPath		&id = GetId();
    BRAY_HdUtil::MaterialId	 matId(*sceneDelegate, id);
    GT_DataArrayHandle		 counts;
    GT_AttributeListHandle	 alist[4];
    bool			 xform_dirty = false;
    BRAY_EventType		 event = BRAY_NO_EVENT;
    bool			 wrap = false;
    GT_Basis			 curveBasis = GT_BASIS_INVALID;
    BRAY::MaterialPtr		 material;
    BRAY::OptionSet		 props = myMesh.objectProperties(scene);
    bool			 props_changed = false;
    bool			 basis_changed = false;

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId)
    {
	_SetMaterialId(sceneDelegate->GetRenderIndex().GetChangeTracker(),
		       matId.resolvePath());
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar)
    {
	int prev_basis = *props.ival(BRAY_OBJ_CURVE_BASIS);
	props_changed = BRAY_HdUtil::updateObjectPrimvarProperties(props,
		*sceneDelegate, dirtyBits, id);
	if (*props.ival(BRAY_OBJ_CURVE_BASIS) != prev_basis)
	{
	    basis_changed = true;
	}
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id))
    {
	_UpdateVisibility(sceneDelegate, dirtyBits);
	if (!IsVisible())
	{
	    props.set(BRAY_OBJ_VISIBILITY_MASK, (int64)BRAY_RAY_NONE);
	}
	else
	{
	    int64 val = (int64)BRAY_RAY_ALL;
	    XUSD_HydraUtils::evalAttrib(val, sceneDelegate,
		    id, TfToken(BRAYobjectProperty(BRAY_OBJ_VISIBILITY_MASK)));
	    // TODO: unset/erase visibility if evalAttrib fails, instead of
	    // setting to "ALL"
	    props.set(BRAY_OBJ_VISIBILITY_MASK, val & (int64)BRAY_RAY_ALL);
	}
	event = event | BRAY_EVENT_PROPERTIES;
	props_changed = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyCategories)
    {
	BRAY_HdUtil::updatePropCategories(rparm, sceneDelegate, this, props);
	event = event | BRAY_EVENT_TRACESET;
	props_changed = true;
    }

    props_changed |= BRAY_HdUtil::updateRprimId(props, this);

    if (props_changed && matId.IsEmpty())
	matId.resolvePath();

    bool	top_dirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
    bool	pinned = false;

    static constexpr HdInterpolation	thePtInterp[] = {
	HdInterpolationVarying,
	HdInterpolationVertex,
	HdInterpolationFaceVarying
    };

    // Pull scene data
    if (!myMesh || top_dirty || basis_changed || !matId.IsEmpty())
    {
	// Update topology
	auto &&top = HdBasisCurvesTopology(GetBasisCurvesTopology(sceneDelegate));
	if (top_dirty || basis_changed)
	{
	    UT_ASSERT(!top.HasIndices());

	    event = (event | BRAY_EVENT_TOPOLOGY
			    | BRAY_EVENT_ATTRIB_P
			    | BRAY_EVENT_ATTRIB);

	    curveBasis = usdCurveTypeToGt(top);
	    counts = BRAY_HdUtil::gtArray(top.GetCurveVertexCounts());
	    TfToken	wrapToken = top.GetCurveWrap();
	    if (wrapToken == thePinnedToken)
	    {
		wrap = false;
		pinned = true;
	    }
	    else
	    {
		wrap = (wrapToken == HdTokens->periodic);
	    }

	    // TODO: GetPrimvarInstanceNames()
	    alist[3] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
		HdPrimTypeTokens->basisCurves, 1,
		props, HdInterpolationConstant);
	    alist[2] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
		HdPrimTypeTokens->basisCurves, counts->entries(),
		props, HdInterpolationUniform);
	    alist[1] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
		HdPrimTypeTokens->basisCurves, BRAY_HdUtil::sumCounts(counts),
		props, thePtInterp, SYScountof(thePtInterp));

	    // Handle velocity/accel blur
	    if (*props.bval(BRAY_OBJ_MOTION_BLUR))
	    {
		alist[1] = BRAY_HdUtil::velocityBlur(alist[1],
			*props.ival(BRAY_OBJ_GEO_VELBLUR),
			*props.ival(BRAY_OBJ_GEO_SAMPLES),
			rparm);
	    }
	}

	if (top_dirty || !matId.IsEmpty())
	{
	    event = (event | BRAY_EVENT_MATERIAL);

	    material = scene.findMaterial(matId.path());

#if 0
	    // TODO: Update when curve meshes support geometry subsets
	    const auto &subsets = top.GetGeomSubsets();
	    if (subsets.size() > 0)
	    {
	    }
#endif
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
	auto pmesh = UTverify_cast<const GT_PrimCurveMesh *>(prim.get());
	// Check to see if any variables are dirty
	bool updated = false;
	updated |= BRAY_HdUtil::updateAttributes(sceneDelegate, rparm,
	    dirtyBits, id, pmesh->getDetail(), alist[3], event, props,
	    HdInterpolationConstant);
	updated |= BRAY_HdUtil::updateAttributes(sceneDelegate, rparm,
	    dirtyBits, id, pmesh->getUniform(), alist[2], event, props,
	    HdInterpolationUniform);
	updated |= BRAY_HdUtil::updateAttributes(sceneDelegate, rparm,
	    dirtyBits, id, pmesh->getVertex(), alist[1], event, props,
	    thePtInterp, SYScountof(thePtInterp));

	if (updated)
	{
	    // if there was an update on any primvar
	    // we need to make sure that any 'other' primvar
	    // that was not updated ends up being in alists[]
	    // so that we can construct the new prim with all the
	    // updated and non-updated primvars
	    if (!alist[1])
	    {
		alist[1] = pmesh->getVertex();
	    }
	    if (!alist[2])
	    {
		alist[2] = pmesh->getUniform();
	    }
	    if (!alist[3])
	    {
		alist[3] = pmesh->getDetail();
	    }
	}
    }

    if (!myMesh || event)
    {
	GT_PrimitiveHandle	prim;
	if (myMesh)
	    prim = myMesh.geometry();

	if (pinned)
	{
	    UT_ErrorLog::warningOnce(
		    "Currently Karma does not supported pinned curves");
	}

	GT_PrimCurveMesh	*pmesh = nullptr;
	if (!counts)
	{
	    UT_ASSERT(prim);
	    pmesh = UTverify_cast<GT_PrimCurveMesh *>(prim.get());
	    counts = pmesh->getCurveCounts();
	    curveBasis = pmesh->getBasis();
	}
	if (!(event & (BRAY_EVENT_ATTRIB|BRAY_EVENT_ATTRIB_P)))
	{
	    // There should be no updates to any of the attributes
	    UT_ASSERT(prim && !alist[0] && !alist[2] && !alist[3]);
	    alist[1] = pmesh->getVertex();
	    alist[2] = pmesh->getUniform();
	    alist[3] = pmesh->getDetail();
	}
	UT_ASSERT(alist[1]);
	UT_ASSERT(!alist[0]);
	UT_ASSERT(curveBasis != GT_BASIS_INVALID);
	if (!alist[1] || !alist[1]->get("P"))
	{
	    // Empty mesh
	    pmesh = new GT_PrimCurveMesh(curveBasis,
		    new GT_Int32Array(0, 1),
		    GT_AttributeList::createAttributeList(
			    "P", new GT_Real32Array(0, 3)
		    ),
		    GT_AttributeListHandle(),
		    GT_AttributeListHandle(),
		    false);
	}
	else
	{
	    pmesh = new GT_PrimCurveMesh(curveBasis,
		    counts,
		    alist[1],	// Vertex
		    alist[2],	// Uniform
		    alist[3],	// Detail
		    wrap);	// Wrapping
	}

	// make linear curves for now
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

    // 4. Populate karma instance objects.
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
	    xforms.append(BRAY_HdUtil::makeSpace(myXform.data(), 
		myXform.size()));

	if (!myInstance)
	{
	    UT_ASSERT(xforms.size());
	    // TODO:  Update new object
	    myInstance = BRAY::ObjectPtr::createInstance(myMesh, id.GetString());
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
	    minst->NestedInstances(rparm, scene, GetId(), myMesh, myXform,
				BRAY_HdUtil::xformSamples(rparm, props));
	else
	    minst->FlatInstances(rparm, scene, GetId(), myMesh, myXform,
				BRAY_HdUtil::xformSamples(rparm, props));
    }

    // Set the material *after* we create the instance hierarchy so that
    // instance primvar variants are known.
    if (myMesh && (material || props_changed))
	myMesh.setMaterial(scene, material, props);

    // Now the mesh is all up to date, send the instance update
    if (iupdate != BRAY_NO_EVENT)
	scene.updateObject(myInstance, iupdate);

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

HdDirtyBits
BRAY_HdCurves::GetInitialDirtyBitsMask() const
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
	| HdChangeTracker::DirtyWidths
	;

    return (HdDirtyBits)mask;
}

HdDirtyBits
BRAY_HdCurves::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
BRAY_HdCurves::_InitRepr(TfToken const &repr,
	HdDirtyBits *dirtyBits)
{
    TF_UNUSED(repr);
    TF_UNUSED(dirtyBits);
}

PXR_NAMESPACE_CLOSE_SCOPE
