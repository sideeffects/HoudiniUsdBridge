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

#include "BRAY_HdPointPrim.h"
#include "BRAY_HdParam.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdInstancer.h"
#include "BRAY_HdIO.h"
#include <pxr/imaging/pxOsd/tokens.h>
#include <BRAY/BRAY_ProceduralFactory.h>
#include <BRAY/BRAY_Procedural.h>
#include <BRAY/BRAY_AttribList.h>
#include <GT/GT_PrimPointMesh.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_HydraUtils.h>
#include <gusd/GT_VtArray.h>
#include <gusd/UT_Gf.h>
#include <UT/UT_Date.h>
#include <UT/UT_HashFunctor.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_WorkBuffer.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

#define DO_PARALLEL_INSTANCE_XFM_COMPUTATIONS 1
#define PERF_ANALYSIS_DO_TIMING 1

namespace
{
    constexpr uint AllDirty = ~0;

    static constexpr UT_StringLit theKarmaProcedural("karma_procedural");

    /// We will use a precomputed array of indices for
    /// querying the attribute everytime instead of checking
    /// for a handle everytime.
    enum AttributeOffset
    {
	OFFSET_POSITION = 0,
	OFFSET_ORIENT,
	OFFSET_PSCALE,
	OFFSET_SCALE,
	OFFSET_N,
	OFFSET_UP,
	OFFSET_V,
	OFFSET_ROT,
	OFFSET_TRANS,
	OFFSET_PIVOT,
	OFFSET_TRANSFORM,

	// TODO:
	// Handle materialpath and materialoverride?

	OFFSET_MAXIMUM
    };

    const int MAX_ATTRIBUTES_SUPPORTED = OFFSET_MAXIMUM;

    BRAY_Procedural *
    getProcedural(BRAY::ObjectPtr &obj)
    {
	return obj.procedural();
    }

    template<typename T>
    static void
    getAttributeValue(const BRAY_HdPointPrim::AttribHandleIdx& data,
	GT_Offset idx, int segment, T* dest, int size)
    {
	auto handle = data.myAttrib;
	if (handle)
	{
	    // if constant attrib - use element at zero index
	    int seg = SYSclamp(segment, 0, handle->getSegments() - 1);
	    handle->get(data.myAttribIndex, seg)->
		import(data.myConstAttrib ? 0 : idx, dest, size);
	}
    }

    // Convenient macro to check for attributes of
    // required name, type, storage and tuplesize
#define CHECK_ATTRIB(TYPE, FUNC)				    \
    static void							    \
    checkAttribProps##TYPE(const GT_AttributeListHandle& attrib,    \
	const UT_StringRef& key, int tsize, int& index)		    \
    {								    \
	if (attrib)						    \
	{							    \
	    index = attrib->getIndex(key);			    \
	    if (index >= 0)					    \
	    {							    \
		const auto& h = attrib->get(index);		    \
		if (!(h->getTupleSize() == tsize &&		    \
		    FUNC(h->getStorage())))			    \
		    index = -1;					    \
	    }							    \
	}							    \
	else							    \
	    index = -1;						    \
    }								    \

    CHECK_ATTRIB(F, GTisFloat);
    CHECK_ATTRIB(S, GTisString);

    static bool
    isProcedural(const GT_AttributeListHandle& pointAttribs,
	const GT_AttributeListHandle& detailAttribs)
    {
	if (!pointAttribs || !pointAttribs->get("P"))
	    return false;

	int index1, index2;
	checkAttribPropsS(pointAttribs, theKarmaProcedural.asRef(), 1, index1);
	checkAttribPropsS(detailAttribs, theKarmaProcedural.asRef(), 1, index2);
	if (index1 >= 0 || index2 >= 0)
	    return true;
	return false;
    }

    static void
    precomputeAttributeOffsets(const GT_AttributeListHandle& pointAttribs,
	const GT_AttributeListHandle& detailAttribs,
	UT_Array<BRAY_HdPointPrim::AttribHandleIdx>& handles,
	int& xfmTupleSize)
    {
	auto checkAttribExists = [&](const UT_StringRef& key, int tupleSize,
	    int idx)
	{
	    // first check for attribute in detail attributes
	    // however they are overriden by same attribute in point attributes
	    int cindex = -1, pindex = -1;

	    // check if point attribs exists
	    checkAttribPropsF(pointAttribs, key, tupleSize, pindex);

	    // only check if we dont have point attribs
	    if(pindex == -1 && detailAttribs)
		checkAttribPropsF(detailAttribs, key, tupleSize, cindex);

	    // just make sure we never get this condition
	    UT_ASSERT(!(pindex >= 0 && cindex >= 0));

	    // Point attribute exists
	    if (pindex >= 0)
		handles[idx] = { pointAttribs , pindex, false };
	    // detail attribute exists
	    else if (cindex >= 0)
		handles[idx] = { detailAttribs, cindex, true };
	    else
		handles[idx] = { nullptr, -1, false };
	};

	// compute the offsets for all the attributes we are interested in
	checkAttribExists("P"_sh, 3, OFFSET_POSITION);
	checkAttribExists("orient"_sh, 4, OFFSET_ORIENT);
	checkAttribExists("pscale"_sh, 1, OFFSET_PSCALE);
	checkAttribExists("scale"_sh, 3, OFFSET_SCALE);
	checkAttribExists("N"_sh, 3, OFFSET_N);
	checkAttribExists("up"_sh, 3, OFFSET_UP);
	checkAttribExists("vel"_sh, 3, OFFSET_V);
	checkAttribExists("rot"_sh, 4, OFFSET_ROT);
	checkAttribExists("trans"_sh, 3, OFFSET_TRANS);
	checkAttribExists("pivot"_sh, 3, OFFSET_PIVOT);
	checkAttribExists("transform"_sh, 9, OFFSET_TRANSFORM);
	xfmTupleSize = 9;
	// check if we are 4x4 matrix
	if (!handles[OFFSET_TRANSFORM].myAttrib)
	{
	    xfmTupleSize = 16;
	    checkAttribExists("transform"_sh, 16, OFFSET_TRANSFORM);
	}
    }

    /// The ProceduralsParameter structure just stores some info
    /// about parameters that a procedural supports and is exposed by
    /// the underlying points.
    struct ProceduralsParameter
    {
	ProceduralsParameter(const GT_DataArrayHandle& handle,
	    const int tupleSize,
	    const exint offset,
	    const GA_Storage storage,
	    const UT_StringHolder& name)
	    : myHandle(handle)
	    , myTupleSize(tupleSize)
	    , myOffset(offset)
	    , myStorage(storage)
	    , myParamName(name)
	{
	    // Note we don't employ the offset within
	    // the hash because we are interested in checking
	    // if the value1(offset) == value2(offset)
	    // not the offsets themselves
	    UT_ASSERT(myHandle);
	    myHash = SYSwang_inthash(name.hash());
	    SYShashCombine(myHash, storage);
	    SYShashCombine(myHash, tupleSize);
	    exint sloc = myOffset * myTupleSize;

#define NUMERIC_VAL_HASH_COMBINE(STORAGE_TYPE, RAW_TYPE, BITS)\
	    if(myStorage == STORAGE_TYPE)\
	    {\
		RAW_TYPE src = myHandle->get##BITS##Array(myHandle);\
		for(auto i = 0; i < myTupleSize; i++)\
		{\
		    SYShashCombine(myHash, src[sloc + i]);\
		}\
	    }

	    NUMERIC_VAL_HASH_COMBINE(GA_STORE_INT32, const int32*, I32);
	    NUMERIC_VAL_HASH_COMBINE(GA_STORE_INT64, const int64*, I64);
	    NUMERIC_VAL_HASH_COMBINE(GA_STORE_REAL32, const fpreal32*, F32);
	    NUMERIC_VAL_HASH_COMBINE(GA_STORE_REAL64, const fpreal64*, F64);

#undef NUMERIC_VAL_HASH_COMBINE

	    // combine string data into hash
	    if (myStorage == GA_STORE_STRING)
	    {
		for (int i = 0; i < myTupleSize; i++)
		{
		    SYShashCombine(myHash,
			SYSstring_hash(myHandle->getS(sloc + i)));
		}
	    }
	}

	bool operator == (const ProceduralsParameter& P) const
	{
	    if (myHash != P.myHash ||
		myParamName != P.myParamName ||
		myStorage != P.myStorage ||
		myTupleSize != P.myTupleSize)
	    {
		return false;
	    }

	    // check the actual values
	    exint sloc = myOffset * myTupleSize;
	    exint dloc = P.myOffset * P.myTupleSize;
	    GT_DataArrayHandle temp;

#define CHECK_NUMERIC_VALS(STORAGE_TYPE, RAW_TYPE, BITS)\
	    if (myStorage == STORAGE_TYPE)\
	    {\
		RAW_TYPE src1 = myHandle->get##BITS##Array(temp);\
		RAW_TYPE src2 = P.myHandle->get##BITS##Array(temp);\
		for(auto i = 0; i < myTupleSize; i++)\
		    if(src1[sloc + i] != src2[dloc + i])\
			return false;\
		return true;\
	    }

	    CHECK_NUMERIC_VALS(GA_STORE_INT32, const int32*, I32);
	    CHECK_NUMERIC_VALS(GA_STORE_INT64, const int64*, I64);
	    CHECK_NUMERIC_VALS(GA_STORE_REAL32, const fpreal32*, F32);
	    CHECK_NUMERIC_VALS(GA_STORE_REAL64, const fpreal64*, F64);
#undef CHECK_NUMERIC_VALS

	    // check for string arrays
	    if (myStorage == GA_STORE_STRING)
	    {
		for (int i = 0; i < myTupleSize; i++)
		{
		    UT_StringRef src1(myHandle->getS(sloc + i));
		    UT_StringRef src2(myHandle->getS(dloc + i));
		    if (src1 != src2)
			return false;
		}
		return true;
	    }
	    return false;
	}

	SYS_HashType hash() const { return myHash; };

	GT_DataArrayHandle myHandle;
	int		   myTupleSize;
	exint		   myOffset;	// offset within the data
	GA_Storage	   myStorage;
	SYS_HashType	   myHash;
	UT_StringHolder	   myParamName;
    };

    // A procedurals key is nothing but a list of procedural parameters
    // that provid a hash and == operators. The procedural's key's hash
    // is combined hash of all the parameter-values that it contains.
    struct ProceduralsKey
    {
	ProceduralsKey(const UT_StringHolder& proceduralType)
	    : myProceduralType(proceduralType)
	{
	    myHash = myProceduralType.hash();
	}

	void addParameter(const ProceduralsParameter& P)
	{
	    myParams.emplace_back(P);
	    SYShashCombine(myHash, P.hash());
	}

	bool operator== (const ProceduralsKey& key) const
	{
	    // check if hashes match or sizes of params match
	    if (myHash != key.myHash ||
		myParams.size() != key.myParams.size() ||
		myProceduralType != key.myProceduralType)
	    {
		return false;
	    }

	    // check individual params
	    // Note: One might, wonder,
	    // what if the ordering of parameters are not
	    // the same within the list, but this would
	    // impossible because, procedurals of same 'type'
	    // have the same ordering of params since
	    // the parameter list is constructed in the same way.
	    //
	    // NOTE:!!
	    // however this would totally not work if we were to consider
	    // a different set of points and compare against a static
	    // list of procedurals!.
	    for (exint p = 0, np = myParams.size(); p < np; p++)
	    {
		if (!(myParams[p] == key.myParams[p]))
		    return false;
	    }

	    return true;
	}

	SYS_HashType hash() const { return myHash; }

	SYS_HashType			myHash;
	UT_Array<ProceduralsParameter>	myParams;
	UT_StringHolder			myProceduralType;
    };
}

BRAY_HdPointPrim::BRAY_HdPointPrim(SdfPath const &id,
	SdfPath const &instancerId)
    : HdPoints(id, instancerId)
    , myIsProcedural(false)
{
}

void
BRAY_HdPointPrim::Finalize(HdRenderParam *renderParam)
{
    BRAY::ScenePtr	&scene =
	UTverify_cast<BRAY_HdParam *>(renderParam)->getSceneForEdit();

    // First, notify the scene the instances are going away
    for (auto&& i : myInstances)
	scene.updateObject(i, BRAY_EVENT_DEL);
    for (auto&& p : myPrims)
	scene.updateObject(p, BRAY_EVENT_DEL);
}

void
BRAY_HdPointPrim::Sync(HdSceneDelegate *sceneDelegate,
	HdRenderParam *renderParam,
	HdDirtyBits *dirtyBits,
	TfToken const &repr)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
    updatePrims(rparm, sceneDelegate, dirtyBits);
}

void
BRAY_HdPointPrim::updatePrims(BRAY_HdParam* rparm, HdSceneDelegate* sd,
    HdDirtyBits* dirtyBits)
{
    const SdfPath		&id	 = GetId();
    BRAY::ScenePtr		&scene	 = rparm->getSceneForEdit();
    BRAY::OptionSet		props	 = myPrims.isEmpty() ? 
	scene.objectProperties() : myPrims[0].objectProperties(scene);
    auto&&			rindex	 = sd->GetRenderIndex();
    auto&&			ctracker = rindex.GetChangeTracker();
    BRAY_EventType		event	 = BRAY_NO_EVENT;
    BRAY::MaterialPtr		material;
    BRAY_HdUtil::MaterialId	matId(*sd, id);
    GT_AttributeListHandle	alist[2];
    UT_Array<UT_Array<exint>>	rIdx;
    BRAY::SpacePtr		xformp;
    bool			xform_dirty = false;
    bool			flush	    = false;
    bool			props_changed = false;

    // Handle dirty material
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId)
    {
	_SetMaterialId(ctracker, matId.resolvePath());
    }

    // Handle dirty params
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar)
    {
	props_changed = BRAY_HdUtil::updateObjectPrimvarProperties(props,
            *sd, dirtyBits, id);
    }

    if (*dirtyBits & HdChangeTracker::DirtyCategories)
    {
	BRAY_HdUtil::updatePropCategories(*rparm, sd, this, props);
	event = event | BRAY_EVENT_TRACESET;
	props_changed = true;
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id))
    {
	_UpdateVisibility(sd, dirtyBits);
	if (!IsVisible())
	{
	    props.set(BRAY_OBJ_VISIBILITY_MASK, (int64)BRAY_RAY_NONE);
	}
	else
	{
	    int64 val = (int64)BRAY_RAY_ALL;
	    XUSD_HydraUtils::evalAttrib(val, sd,
		    id, TfToken(BRAYobjectProperty(BRAY_OBJ_VISIBILITY_MASK)));
	    // TODO: unset/erase visibility if evalAttrib fails, instead of
	    // setting to "ALL"
	    props.set(BRAY_OBJ_VISIBILITY_MASK, val & (int64)BRAY_RAY_ALL);
	}
	event = event | BRAY_EVENT_PROPERTIES;
	props_changed = true;
    }

    props_changed |= BRAY_HdUtil::updateRprimId(props, this);

    if (props_changed && matId.IsEmpty())
	matId.resolvePath();

    // Handle dirty topology
    bool topo_dirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
    if (!myPrims.size() || topo_dirty)
    {
	event = (event | BRAY_EVENT_TOPOLOGY
		       | BRAY_EVENT_ATTRIB
		       | BRAY_EVENT_ATTRIB_P);

	alist[0] = BRAY_HdUtil::makeAttributes(sd, *rparm, id,
		HdPrimTypeTokens->points, -1, props, HdInterpolationVertex);
	alist[1] = BRAY_HdUtil::makeAttributes(sd, *rparm, id,
		HdPrimTypeTokens->points, 1, props, HdInterpolationConstant);

	// perform velocity blur only if options is set
	if (*props.bval(BRAY_OBJ_MOTION_BLUR))
	{
	    alist[0] = BRAY_HdUtil::velocityBlur(alist[0],
			*props.ival(BRAY_OBJ_GEO_VELBLUR),
			*props.ival(BRAY_OBJ_GEO_SAMPLES),
			*rparm);
	}

	myIsProcedural = isProcedural(alist[0], alist[1]);
	flush = true;
    }

    // Get new material in case of dirty topo or dirty material
    if (!matId.IsEmpty() || topo_dirty)
    {
	event = (event | BRAY_EVENT_MATERIAL);
	material = scene.findMaterial(matId.path());
    }

    // Handle dirty transforms
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id))
    {
	xform_dirty = true;
	BRAY_HdUtil::xformBlur(sd, *rparm, id, myXform, props);
	xformp = BRAY_HdUtil::makeSpace(myXform.data(), myXform.size());
    }

    // Handle updates to primvars
    if (!(event & BRAY_EVENT_TOPOLOGY))
    {
	bool updated = false;
	auto isPrimvarDirty = [&](const GT_AttributeListHandle& pattribs,
	    const GT_AttributeListHandle& cattribs)
	{
	    updated |= BRAY_HdUtil::updateAttributes(sd, *rparm, dirtyBits, id,
		pattribs, alist[0], event, props, HdInterpolationVertex);
	    updated |= BRAY_HdUtil::updateAttributes(sd, *rparm, dirtyBits, id,
		cattribs, alist[1], event, props, HdInterpolationConstant);

	    if (updated)
	    {
		if (!alist[0])
		    alist[0] = pattribs;
		if (!alist[1])
		    alist[1] = cattribs;
	    }
	};

	if (!myIsProcedural && myPrims.size() && myPrims[0])
	{
	    auto&& prim = myPrims[0].geometry();
	    auto&& pmesh = UTverify_cast<const GT_PrimPointMesh*>(prim.get());
	    isPrimvarDirty(pmesh->getPointAttributes(),
			   pmesh->getDetailAttributes());
	}
	else
	{
	    isPrimvarDirty(myAlist[0], myAlist[1]);
	    if (updated)
		flush = true;
	}
    }

    // Create underlying new geometry
    if (!myPrims.size() || event)
    {
	if (myIsProcedural && flush)
	{
	    getUniqueProcedurals(alist[0], alist[1], rIdx);
	    // reset for future updates
	    myAlist[0] = alist[0];
	    myAlist[1] = alist[1];
	}
	else
	{
	    GT_PrimitiveHandle prim;
	    if (myPrims.size() && myPrims[0])
		prim = myPrims[0].geometry();

	    if (!(event & (BRAY_EVENT_ATTRIB | BRAY_EVENT_ATTRIB_P)))
	    {
		UT_ASSERT(prim && !alist[0] && !alist[1]);
		alist[0] = prim->getPointAttributes();
		alist[1] = prim->getDetailAttributes();
	    }

	    UT_ASSERT(alist[0]);
	    if (!alist[0] || !alist[0]->get("P"))
	    {
		prim.reset(new GT_PrimPointMesh(
			GT_AttributeList::createAttributeList(
				"P", new GT_Real32Array(0, 3)
			),
			GT_AttributeListHandle()));
	    }
	    else
	    {
		prim.reset(new GT_PrimPointMesh(alist[0], alist[1]));
	    }

	    if (myPrims.size() && myPrims[0])
	    {
		myPrims[0].setGeometry(prim);
		scene.updateObject(myPrims[0], event);
	    }
	    else
	    {
		UT_ASSERT(xform_dirty);
		myPrims.emplace_back(BRAY::ObjectPtr::createGeometry(prim));
	    }
	}
    }

    // Now, populate the instance objects
    SpaceList		xforms;
    BRAY_EventType	iupdate = BRAY_NO_EVENT;
    if (GetInstancerId().IsEmpty())
    {
	if (myIsProcedural && (xform_dirty || flush))
	    computeInstXfms(alist[0], alist[1], xformp, rIdx, flush, xforms);
	else if (!myInstances.size() || xform_dirty)
	    xforms.append(UT_Array<BRAY::SpacePtr>({ xformp }));

	if (!myInstances.size())
	{
	    iupdate = BRAY_EVENT_NEW;
	    for (auto &&p : myPrims)
	    {
		// get unique name
		UT_WorkBuffer name;
		if (myPrims.size() == 1)
		    name.append(id.GetString());
		else
		{
		    // make a unique name by appending index on the end
		    name.format(
			"{}__{}", id.GetString(), myInstances.size());
		}

		int idx = myInstances.emplace_back(
		    BRAY::ObjectPtr::createInstance(p, name.buffer()));
		myInstances[idx].setInstanceTransforms(xforms[idx]);
	    }
	}
	else if (xforms.size())
	{
	    int idx = 0;
	    iupdate = BRAY_EVENT_XFORM;
	    for (auto&& i : myInstances)
	    {
		i.setInstanceTransforms(xforms[idx++]);
	    }
	}
    }
    else
    {
	UT_ASSERT(!myInstances.size());
	HdInstancer	*instancer = rindex.GetInstancer(GetInstancerId());
	auto		 minst = UTverify_cast<BRAY_HdInstancer *>(instancer);

	for (auto&& p : myPrims)
	{
	    if (scene.nestedInstancing())
		minst->NestedInstances(*rparm, scene, id, p, myXform,
				BRAY_HdUtil::xformSamples(props));
	    else
		minst->FlatInstances(*rparm, scene, id, p, myXform,
				BRAY_HdUtil::xformSamples(props));
	}
    }

    // Assign material to prims/procedurals, but set the material *after* we
    // create the instance hierarchy so that instance primvar variants are
    // known.
    if (myPrims.size() && (material || props_changed))
    {
	for (auto&& p : myPrims)
	    p.setMaterial(scene, material, props);
    }

    // Now the mesh is all up to date, send the instance update
    if (iupdate != BRAY_NO_EVENT)
    {
	for (auto &&i : myInstances)
	    scene.updateObject(i, iupdate);
    }

    *dirtyBits &= ~AllDirty;
}

HdDirtyBits
BRAY_HdPointPrim::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

HdDirtyBits
BRAY_HdPointPrim::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
BRAY_HdPointPrim::_InitRepr(TfToken const &repr,
	HdDirtyBits *dirtyBits)
{
    TF_UNUSED(repr);
    TF_UNUSED(dirtyBits);
}

bool
BRAY_HdPointPrim::updateProceduralPrims(const GT_AttributeListHandle& pointAttribs,
    const GT_AttributeListHandle& detailAttribs,
    UT_UniquePtr<BRAY_Procedural> &proc,
    exint offset)
{
    // check if we have an underlying procedural defined
    proc->beginUpdate();

    // Optimization step:
    // instead of going through all the attributes
    // fetch the attributes which a procedural has support for
    // and then search through the list if we have it
    // if so, directly send it to the procedural
    // if the underlying procedural does not provide any values
    // for supported attributes, then iterate and give it a chance
    // to fill in values that it might need
    const BRAY_AttribList *supportedParams = proc->paramList();
    if (supportedParams)
    {
	for (int i = 0; i < supportedParams->size(); i++)
	{
	    const BRAY_AttribList::Attrib& param = (*supportedParams)[i];

	    // we will always have point attribs
	    const auto &pData = pointAttribs->get(param.name());
	    if (pData)
	    {
		passParameterData(param.name(), proc, offset, pData);
	    }
	    else if (detailAttribs)
	    {
		const auto& cData = detailAttribs->get(param.name());
		if (cData)
		{
		    // in case of a constant attribute, we just one 1 single
		    // value hence offset = 0
		    passParameterData(param.name(), proc, 0, cData);
		}
	    }
	}
    }

    // Signal the procedural that we have finished updating.
    // It can do its own stuff.
    proc->endUpdate();
    return proc->isValid();
}

void
BRAY_HdPointPrim::getUniqueProcedurals(const GT_AttributeListHandle& pointAttribs,
    const GT_AttributeListHandle& detailAttribs,
    UT_Array<UT_Array<exint>>& indices)
{
    UT_ASSERT(pointAttribs->get("P"_sh));
    bool checkProceduralExists = isProcedural(pointAttribs, detailAttribs);
    if (checkProceduralExists)
    {
	// get the required data
	const auto& gData = pointAttribs->get(theKarmaProcedural.asRef());
	const auto& cData = detailAttribs ?
			    detailAttribs->get(theKarmaProcedural.asRef())
			    : nullptr;

	const exint numPts = pointAttribs->get("P"_sh)->entries();

	// KeyMap stores different unique instances of a
	// particular procedural type. the value is the uniqueIndex
	// among all procedurals
	UT_Map<ProceduralsKey, int, UT_HashFunctor<ProceduralsKey>> proceduralsMap;

	// Get the map of parameters by supported procedurals
	auto&& procedurals = BRAY_ProceduralFactory::procedurals();

	int uniqueIdx = 0;
	UT_StringRef proceduralType;
	if (cData)
	    proceduralType = cData->getS(0);

	for (exint pt = 0; pt < numPts; pt++)
	{
	    if(gData)
		proceduralType = gData->getS(pt);
	    auto&& g = procedurals.find(proceduralType);
	    if (g != procedurals.end())
	    {
		const BRAY_AttribList* params =
		    g->second->paramList(pointAttribs, detailAttribs);

		// Step 1: compose the key for the procedural defined
		//         on this point based on parameters
		ProceduralsKey gKey(g->first);
		for (int pidx = 0, np = params->size(); pidx < np; pidx++)
		{
		    const GT_DataArrayHandle& data =
			pointAttribs->get(params->name(pidx));
		    if (data)
		    {
			ProceduralsParameter gp(data,
			    params->tupleSize(pidx),
			    pt,
			    params->storage(pidx),
			    params->name(pidx));

			gKey.addParameter(gp);

			// we cannot have the same parameter
			// defined on both the point attributes
			// and detail attributes
			continue;
		    }

		    if (detailAttribs)
		    {
			const GT_DataArrayHandle& cdata =
			    detailAttribs->get(params->name(pidx));
			if (cdata)
			{
			    ProceduralsParameter gp(cdata,
				params->tupleSize(pidx),
				0, // for detail primvars, there's always only 1
				params->storage(pidx),
				params->name(pidx));
			    gKey.addParameter(gp);
			}
		    }
		}

		// Step 2: check if we've seen this key
		auto instance = proceduralsMap.find(gKey);
		if (instance != proceduralsMap.end())
		{
		    // We have already seen this procedural
		    auto uidx = instance->second;
		    indices[uidx].emplace_back(pt);
		}
		else
		{
		    // create a new instance of this procedural
		    // TODO: incase of parallelizing this, this
		    //       is the place to add an atomic insert
		    proceduralsMap[gKey] = uniqueIdx;
		    uniqueIdx++;

		    // create the procedural and and store in our list
		    UT_UniquePtr<BRAY_Procedural>	proc(g->second->create());

		    // Update the procedural with attribute values
		    if (updateProceduralPrims(pointAttribs, detailAttribs, proc, pt))
		    {
			UT_ASSERT(myPrims.size() == indices.size());
			exint gidx = myPrims.size();
			indices.append(UT_Array<exint>());
			myPrims.append(BRAY::ObjectPtr::createProcedural(std::move(proc)));
			indices[gidx].append(pt);	// Now, track the point
		    }
		}
	    }
	    else
	    {
		// We encountered a procedural that we dont
		// support yet!? silently ignore
		BRAYerrorOnce("Unsupported procedural: {}", proceduralType);
		UT_ASSERT(0);
	    }
	}

	UTdebugPrint("Number of unique instances : ", uniqueIdx);
    }
}

void
BRAY_HdPointPrim::passParameterData(const UT_StringRef& key,
    UT_UniquePtr<BRAY_Procedural> &proc,
    exint off,
    const GT_DataArrayHandle& data) const
{
    GT_Storage type = data->getStorage();
    int numel = data->getTupleSize();
    GT_DataArrayHandle storage;
    switch (type)
    {
	case GT_STORE_INT32:
	{
	    proc->setParameter(key, &data->getI32Array(storage)[off], numel);
	    break;
	}
	case GT_STORE_INT64:
	{
	    proc->setParameter(key, &data->getI64Array(storage)[off], numel);
	    break;
	}
	case GT_STORE_FPREAL32:
	{
	    proc->setParameter(key, &data->getF32Array(storage)[off], numel);
	    break;
	}
	case GT_STORE_FPREAL64:
	{
	    proc->setParameter(key, &data->getF64Array(storage)[off], numel);
	    break;
	}
	case GT_STORE_STRING:
	{
	    UT_StringArray sdata(numel);
	    for (int s = 0; s < numel; s++)
	    {
		sdata.insert(data->getS(off + s), s);
	    }
	    UT_ASSERT(sdata.size() == numel);
	    proc->setParameter(key, sdata.getArray(), sdata.size());
	    break;
	}
	default:
	{
	    // Unhandled attribute type obtained?
	    // What do we do with this?
	    break;
	}
    }
}

void
BRAY_HdPointPrim::computeInstXfms(const GT_AttributeListHandle& pointAttribs,
    const GT_AttributeListHandle& detailAttribs, const BRAY::SpacePtr& xform,
    const UT_Array<UT_Array<exint>>& indices, bool flush, SpaceList& xfms)
{

#if PERF_ANALYSIS_DO_TIMING
    UT_StopWatch timer;
    timer.start();
#endif

    UT_Array<AttribHandleIdx>	handles;
    const exint			numProcedurals  = myPrims.size();
    int				xfmTupleSize = 0;
    exint			ninstances   = 0;

    // allocate enough memory for 'n' procedurals
    xfms.setSize(numProcedurals);
    if (flush)
    {
	myOriginalSpace.setSize(numProcedurals);
	handles.setSize(MAX_ATTRIBUTES_SUPPORTED);
	precomputeAttributeOffsets(pointAttribs, detailAttribs, handles,
				   xfmTupleSize);
    }

    // allocate enough memory for 'n' instances of each procedural
    for (exint g = 0; g < numProcedurals; g++)
    {
	if (flush)
	{
	    ninstances = indices[g].size();
	    myOriginalSpace[g].setSize(ninstances);
	}
	else
	    ninstances = myOriginalSpace[g].size();
	xfms[g].setSize(ninstances);
    }

    // TODO: test different strategies for these two par-for loops
#if DO_PARALLEL_INSTANCE_XFM_COMPUTATIONS
    UTparallelFor(UT_BlockedRange<exint>(0, numProcedurals),
#else
    UTserialFor(UT_BlockedRange<exint>(0, numProcedurals),
#endif
    [&, this](const UT_BlockedRange<exint>& range)
    {
	for (exint i = range.begin(), n = range.end(); i < n; i++)
	{
	    exint numInstances = myOriginalSpace[i].size();
#if DO_PARALLEL_INSTANCE_XFM_COMPUTATIONS
	    UTparallelForLightItems(UT_BlockedRange<exint>(0, numInstances),
#else
	    UTserialFor(UT_BlockedRange<exint>(0, numInstances),
#endif
	    [&, this](const UT_BlockedRange<exint>& iRange)
	    {
		UT_Matrix4D temp;
		for (exint id = iRange.begin(); id < iRange.end(); id++)
		{
		    if (flush)
		    {
			const int nseg = pointAttribs->getSegments();
			UT_StackBuffer<UT_Matrix4D> oxforms(nseg);
			exint pointIdx = indices[i][id];
			for (int seg = 0; seg < nseg; seg++)
			{
			    composeXfm(handles, pointIdx, seg, xfmTupleSize, temp);
			    oxforms[seg] = temp;
			}
			myOriginalSpace[i][id] = BRAY::SpacePtr(oxforms, nseg);
		    }

		    // Multiply the spaces
		    xfms[i][id] = myOriginalSpace[i][id].mulSpace(xform);
		}
	    });
	}
    });

#if PERF_ANALYSIS_DO_TIMING
    UT_WorkBuffer buf;
    UT_Date::printSeconds(buf, timer.lap(), false, true, true);
    UTdebugFormat("Instance xfms computed in : {} sec", buf.buffer());
#endif
}

void
BRAY_HdPointPrim::composeXfm(UT_Array<AttribHandleIdx>& data,
    exint index, int seg, int xfmTupleSize,
    UT_Matrix4D& xfm) const
{
    // Only initialize these three as others required quanities can be left
    // uninitialized
    UT_Vector3	P(0.f, 0.f, 0.f), pivot(0.f, 0.f, 0.f), trans(0.f, 0.f, 0.f);

    // We need these 3 before we can make a decision with regards
    // to which attributes to actually evaluate
    getAttributeValue(data[OFFSET_POSITION], index, seg, P.data(), 3);
    getAttributeValue(data[OFFSET_PIVOT], index, seg, pivot.data(), 3);
    getAttributeValue(data[OFFSET_TRANS], index, seg, trans.data(), 3);

    // If transformAttrib exists, it overrides everything else
    // hence we do a delayed computation
    if (data[OFFSET_TRANSFORM].myAttrib)
    {
	UT_Matrix4D m;
	xfm.identity();	// initialize the matrix
	xfm.translate(trans + P);
	if (xfmTupleSize == 9)
	    getAttributeValue(data[OFFSET_TRANSFORM], index, seg, m.data(), 9);
	else
	    getAttributeValue(data[OFFSET_TRANSFORM], index, seg, m.data(), 16);
	xfm.preMultiply(m);
	xfm.pretranslate(-pivot);
    }
    else
    {
	UT_Vector3 scale, N, up;
	UT_Quaternion orient, rot;
	float pscale;
	getAttributeValue(data[OFFSET_ORIENT], index, seg, orient.data(), 4);
	if (data[OFFSET_PSCALE].myAttrib)
	    getAttributeValue(data[OFFSET_PSCALE], index, seg, &pscale, 1);
	else
	    pscale = 1;
	getAttributeValue(data[OFFSET_SCALE], index, seg, scale.data(), 3);
	if (data[OFFSET_N].myAttrib)
	    getAttributeValue(data[OFFSET_N], index, seg, N.data(), 3);
	else if (data[OFFSET_V].myAttrib)
	    getAttributeValue(data[OFFSET_V], index, seg, N.data(), 3);
	else
	    N.assign(0, 0, 1);
	getAttributeValue(data[OFFSET_UP], index, seg, up.data(), 3);
	getAttributeValue(data[OFFSET_ROT], index, seg, rot.data(), 4);

	// Use the existing function to do this for us.
	xfm.instanceT(P, N, pscale,
	    (data[OFFSET_SCALE].myAttrib)   ? &scale  : nullptr,
	    (data[OFFSET_UP].myAttrib)	    ? &up     : nullptr,
	    (data[OFFSET_ROT].myAttrib)	    ? &rot    : nullptr,
	    (data[OFFSET_TRANS].myAttrib)   ? &trans  : nullptr,
	    (data[OFFSET_ORIENT].myAttrib)  ? &orient : nullptr,
	    (data[OFFSET_PIVOT].myAttrib)   ? &pivot  : nullptr);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
