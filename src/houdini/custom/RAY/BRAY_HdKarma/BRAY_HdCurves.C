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
#include <UT/UT_ErrorLog.h>
#include <GT/GT_DANumeric.h>
#include <GT/GT_PrimCurveMesh.h>
#include <UT/UT_FSA.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_HydraUtils.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
//#define DISABLE_USD_THREADING_TO_DEBUG
#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    static UT_Lock	theLock;
#endif

    static constexpr UT_StringLit   theBoth("Both");

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
                UT_ErrorLog::error(
                        "Unsupported curve basis {}. Using linear curves.",
			basis);
		UT_ASSERT(0);
		return GT_BASIS_LINEAR;
	    }
	}
	else
	{
            UT_ErrorLog::error(
                    "Unsupported curve type {}.  Using linear curves.", type);
	    UT_ASSERT(0);
	    return GT_BASIS_LINEAR;
	}
    }
}

BRAY_HdCurves::BRAY_HdCurves(SdfPath const &id)
    : HdBasisCurves(id)
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

    BRAY_HdParam        &rparm = *UTverify_cast<BRAY_HdParam *>(renderParam);
#if 0
    const HdBasisCurvesReprDesc &desc = _GetReprDesc(repr)[0];
#endif

#if defined(DISABLE_USD_THREADING_TO_DEBUG)
    UT_Lock::Scope	single_thread(theLock);
#endif
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

    bool	top_dirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId)
	SetMaterialId(matId.resolvePath());

    static const TfToken &primType = HdPrimTypeTokens->basisCurves;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar)
    {
        // Disable direct refraction subset to allow our hair shader (which has
        // refract component) to function properly without users having to
        // disable it manually.
        props.set(BRAY_OBJ_LIGHT_SUBSET, theBoth.asHolder());

	int prev_basis = *props.ival(BRAY_OBJ_CURVE_BASIS);
	int prev_style = *props.ival(BRAY_OBJ_CURVE_STYLE);
        int prevvblur = *props.bval(BRAY_OBJ_MOTION_BLUR) ?
            *props.ival(BRAY_OBJ_GEO_VELBLUR) : 0;
	props_changed = BRAY_HdUtil::updateObjectPrimvarProperties(props,
		*sceneDelegate, dirtyBits, id, primType);
	if (*props.ival(BRAY_OBJ_CURVE_BASIS) != prev_basis
                || *props.ival(BRAY_OBJ_CURVE_STYLE) != prev_style)
	{
	    basis_changed = true;
	}
	event = props_changed ? (event | BRAY_EVENT_PROPERTIES) : event;

        // Force topo dirty if velocity blur toggles changed to make new blur P
        // attributes (can't really rely on updateAttributes() because it won't
        // do anything if P is not dirty)
        int currvblur = *props.bval(BRAY_OBJ_MOTION_BLUR) ?
            *props.ival(BRAY_OBJ_GEO_VELBLUR) : 0;
        top_dirty |= prevvblur != currvblur;
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id))
    {
	_UpdateVisibility(sceneDelegate, dirtyBits);

	BRAY_HdUtil::updateVisibility(sceneDelegate, id,
		props, IsVisible(), GetRenderTag(sceneDelegate));

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

    bool	pinned = false;
    bool        widths_dirty = *dirtyBits & HdChangeTracker::DirtyWidths;

    static constexpr HdInterpolation	thePtInterp[] = {
	HdInterpolationVarying,
	HdInterpolationVertex,
	HdInterpolationFaceVarying
    };
    if (!top_dirty && myMesh)
    {
	// Check to see if the primvars are the same
	auto &&prim = myMesh.geometry();
	auto pmesh = UTverify_cast<const GT_PrimCurveMesh *>(prim.get());
	if (!BRAY_HdUtil::matchAttributes(sceneDelegate, id, primType,
		    HdInterpolationConstant, pmesh->getDetail())
	    || !BRAY_HdUtil::matchAttributes(sceneDelegate, id, primType,
		    HdInterpolationUniform, pmesh->getUniform())
	    || !BRAY_HdUtil::matchAttributes(sceneDelegate, id, primType,
		    thePtInterp, SYSarraySize(thePtInterp), pmesh->getVertex()))
	{
	    top_dirty = true;
            props_changed = true;
	}
    }

    if (props_changed && matId.IsEmpty())
	matId.resolvePath();

    // Pull scene data
    if (!myMesh || top_dirty || basis_changed || widths_dirty ||
        !matId.IsEmpty())
    {
	// Update topology
	auto &&top = HdBasisCurvesTopology(GetBasisCurvesTopology(sceneDelegate));
	if (top_dirty || basis_changed || widths_dirty)
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
            UT_ErrorLog::format(8,
                    "{} topology change {} curves {} vertices wrap:{} pin:{}",
                    id, counts->entries(), BRAY_HdUtil::sumCounts(counts),
                    wrap, pinned);

	    // TODO: GetPrimvarInstanceNames()
	    alist[3] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
		primType, 1, props, HdInterpolationConstant);
	    alist[2] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
		primType, counts->entries(), props, HdInterpolationUniform);
	    alist[1] = BRAY_HdUtil::makeAttributes(sceneDelegate, rparm, id,
		primType, BRAY_HdUtil::sumCounts(counts),
		props, thePtInterp, SYSarraySize(thePtInterp));

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
    GT_PrimitiveHandle  prim;
    bool                unpinned = false;
    if (myMesh && !(event & BRAY_EVENT_TOPOLOGY))
    {
        auto &&top = HdBasisCurvesTopology(GetBasisCurvesTopology(sceneDelegate));
        prim = myMesh.geometry();
	auto pmesh = UTverify_cast<const GT_PrimCurveMesh *>(prim.get());

        // Unpin the curves before updating.
        if (top.GetCurveWrap() == thePinnedToken)
        {
            prim = pmesh->unpinCurves();
            pmesh = UTverify_cast<const GT_PrimCurveMesh *>(prim.get());
            unpinned = true;
            pinned = true;              // We need to re-pin the curves
        }
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
	    thePtInterp, SYSarraySize(thePtInterp));

	if (updated)
	{
	    // if there was an update on any primvar
	    // we need to make sure that any 'other' primvar
	    // that was not updated ends up being in alists[]
	    // so that we can construct the new prim with all the
	    // updated and non-updated primvars
	    if (!alist[1])
		alist[1] = pmesh->getVertex();
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
	if (myMesh && !unpinned)
	    prim = myMesh.geometry();

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

            // Since we're not updating attributes, don't repin the curve mesh
            pinned = false;
	}
	UT_ASSERT(alist[1]);
	UT_ASSERT(!alist[0]);
	UT_ASSERT(curveBasis != GT_BASIS_INVALID);
	if (!alist[1] || !alist[1]->get("P"))
	{
	    // Empty mesh
            UT_ErrorLog::warning("{} invalid curve mesh", id);
	    pmesh = new GT_PrimCurveMesh(curveBasis,
		    new GT_Int32Array(0, 1),
		    GT_AttributeList::createAttributeList(
			    "P", new GT_Real32Array(0, 3)
		    ),
		    GT_AttributeListHandle(),
		    GT_AttributeListHandle(),
		    false);
            pinned = false;
	}
	else
	{
            UT_ErrorLog::format(8, "{} create curve mesh", id);
	    pmesh = new GT_PrimCurveMesh(curveBasis,
		    counts,
		    alist[1],	// Vertex
		    alist[2],	// Uniform
		    alist[3],	// Detail
		    wrap);	// Wrapping
	}

	// make linear curves for now
        if (pinned)
        {
            prim = pmesh->pinCurves();
            if (!prim)
            {
                UT_ErrorLog::error("Unable to pin curves for {}", id);
                prim.reset(pmesh);
            }
        }
        else
        {
            prim.reset(pmesh);
        }
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

    // Make sure our instancer and it's parent instancers are synced.
    _UpdateInstancer(sceneDelegate, dirtyBits);
    HdInstancer::_SyncInstancerAndParents(
        sceneDelegate->GetRenderIndex(), GetInstancerId());

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

        minst->NestedInstances(rparm, scene, GetId(), myMesh, myXform, props);
    }

    // Set the material *after* we create the instance hierarchy so that
    // instance primvar variants are known.
    if (myMesh && (material || props_changed))
    {
        UT_ErrorLog::format(8, "Assign {} to {}", matId.path(), id);
	myMesh.setMaterial(scene, material, props);
    }

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
