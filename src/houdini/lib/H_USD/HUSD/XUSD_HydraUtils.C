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
 * NAME:	XUSD_HydraUtils.h (HUSD Library, C++)
 *
 * COMMENTS:	Utility functions for Hydra delegates, as Hydra classes derive
 *		from Pixar Hd* classes so adding common methods between
 *		different hydra prim types is tricky.
 */

#include "XUSD_HydraUtils.h"
#include "XUSD_HydraInstancer.h"
#include <gusd/UT_Gf.h>
#include <gusd/GT_VtArray.h>
#include <GT/GT_DAIndexedString.h>

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/range1d.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/extComputationUtils.h>

//#define DUMP_ATTRIBS
#ifdef DUMP_ATTRIBS
#include <UT/UT_Debug.h>
#define DUMP(a,b) UTdebugPrint(a,b)
#else
#define DUMP(a,b)
#endif

PXR_NAMESPACE_OPEN_SCOPE

void populateList(HdSceneDelegate* sd,
		  SdfPath const &path,
		  HdInterpolation interp,
		  UT_StringMap< UT_Tuple<GT_Owner,int,bool, void*> > &map,
		  GT_Owner owner,
		  const UT_Map<GT_Owner, GT_Owner> *remap)
{

    if(remap)
    {
	auto entry = remap->find(owner);
	if(entry != remap->end())
	    owner = entry->second;
    }
    const HdPrimvarDescriptorVector &list =
	sd->GetPrimvarDescriptors(path, interp);
    for (auto &it : list)
    {
	DUMP(GTowner(owner), it.name.GetText());
	map[ it.name.GetText() ] = UTmakeTuple(owner, interp, false, nullptr);
    }
 
    const HdExtComputationPrimvarDescriptorVector &clist = 
	sd->GetExtComputationPrimvarDescriptors(path,interp);
    for (auto &it : clist)
    {
        auto *primd = new
            HdExtComputationPrimvarDescriptor(it.name,
                                              it.interpolation,
                                              it.role,
                                              it.sourceComputationId,
                                              it.sourceComputationOutputName,
                                              it.valueType);
	DUMP(GTowner(owner), it.name.GetText());
	map[ it.name.GetText() ] = UTmakeTuple(owner, interp, true, primd);
    }
}

void
XUSD_HydraUtils::buildAttribMap(
    HdSceneDelegate *sd,
    SdfPath const   &path,
    UT_StringMap<UT_Tuple<GT_Owner,int,bool,void*> > &map,
    const UT_Map<GT_Owner, GT_Owner> *remap)
{
    // build the maps for the parms which describes their owner.
    map.clear();

    populateList(sd, path, HdInterpolationFaceVarying,
		 map, GT_OWNER_VERTEX, remap);

    populateList(sd, path, HdInterpolationVertex,
		 map, GT_OWNER_POINT, remap);

    populateList(sd, path, HdInterpolationVarying,
		 map, GT_OWNER_POINT, remap);

    populateList(sd, path, HdInterpolationUniform,
		 map, GT_OWNER_PRIMITIVE, remap);

    populateList(sd, path, HdInterpolationConstant,
		 map, GT_OWNER_CONSTANT, remap);

#ifdef DUMP_ATTRIBS
   auto ilist = sd->GetPrimvarDescriptors(path, HdInterpolationInstance);
    for (auto &it : ilist)
    {
	DUMP("inst", it.name.GetText());
    }
#endif
}

UT_Matrix4D
XUSD_HydraUtils::fullTransform(HdSceneDelegate *scene_del,
			       const SdfPath   &prim_path)
{
    GfMatrix4d mat;
    float ft = 0.0;

    scene_del->SampleTransform(prim_path, 1, &ft, &mat);

    return GusdUT_Gf::Cast(mat);
}

namespace XUSD_HydraUtils
{

template<typename T> bool eval(VtValue &vval, T &ret_val)
{
    if(vval.IsEmpty() || !vval.IsHolding<T>())
	return false;

    ret_val = vval.UncheckedGet<T>();
    return true;
}

template<typename T> bool evalAttrib(T		    &val,
				     HdSceneDelegate *scene_del,
				     const SdfPath   &prim_path,
				     const TfToken   &attrib_name)
{
    VtValue vtval = scene_del->Get(prim_path, attrib_name);
    if(vtval.IsEmpty())
    {
	//UTdebugPrint("Empty vtvalue");
	return false;
    }
    if(!vtval.IsHolding<T>())
    {
	UTdebugPrint(attrib_name.GetText(), "type mismatch, expected",
		     vtval.GetTypeName());
	return false;
    }
    return eval<T>(vtval, val);
}

template<typename T> bool evalCameraAttrib(T	    &val,
				     HdSceneDelegate *scene_del,
				     const SdfPath   &prim_path,
				     const TfToken   &attrib_name)
{
    VtValue vtval = scene_del->GetCameraParamValue(prim_path, attrib_name);
    if(vtval.IsEmpty())
    {
	//UTdebugPrint("Empty vtvalue");
	return false;
    }
    if(!vtval.IsHolding<T>())
    {
	UTdebugPrint(attrib_name.GetText(), "type mismatch, expected",
		     vtval.GetTypeName());
	return false;
    }
    return eval<T>(vtval, val);
}
template<typename T> bool evalLightAttrib(T		    &val,
					  HdSceneDelegate *scene_del,
					  const SdfPath   &prim_path,
					  const TfToken   &attrib_name)
{
    VtValue vtval = scene_del->GetLightParamValue(prim_path, attrib_name);
    if(vtval.IsEmpty())
	return false;
    if(!vtval.IsHolding<T>())
    {
	UTdebugPrint(attrib_name.GetText(), "type mismatch, expected",
		     vtval.GetTypeName());
	return false;
    }
    return eval<T>(vtval, val);
}

#define INST_EVAL_ATTRIB(TYPE)	\
    template HUSD_API bool eval<TYPE>(VtValue &, TYPE &); \
    template HUSD_API bool evalAttrib<TYPE>(TYPE &, HdSceneDelegate *, \
					    const SdfPath &,const TfToken &); \
    template HUSD_API bool evalCameraAttrib<TYPE>(TYPE &, HdSceneDelegate *, \
					    const SdfPath &,const TfToken &); \
    template HUSD_API bool evalLightAttrib<TYPE>(TYPE &, HdSceneDelegate *, \
						 const SdfPath &, \
						 const TfToken &)

INST_EVAL_ATTRIB(bool);
INST_EVAL_ATTRIB(int32);
INST_EVAL_ATTRIB(int64);
INST_EVAL_ATTRIB(fpreal32);
INST_EVAL_ATTRIB(fpreal64);
INST_EVAL_ATTRIB(GfVec2i);
INST_EVAL_ATTRIB(GfVec3i);
INST_EVAL_ATTRIB(GfVec4i);
INST_EVAL_ATTRIB(GfVec2f);
INST_EVAL_ATTRIB(GfVec3f);
INST_EVAL_ATTRIB(GfVec4f);
INST_EVAL_ATTRIB(GfVec2d);
INST_EVAL_ATTRIB(GfVec3d);
INST_EVAL_ATTRIB(GfVec4d);
INST_EVAL_ATTRIB(GfMatrix2f);
INST_EVAL_ATTRIB(GfMatrix3f);
INST_EVAL_ATTRIB(GfMatrix4f);
INST_EVAL_ATTRIB(GfMatrix2d);
INST_EVAL_ATTRIB(GfMatrix3d);
INST_EVAL_ATTRIB(GfMatrix4d);
INST_EVAL_ATTRIB(GfRange1f);
INST_EVAL_ATTRIB(GfRange1d);
INST_EVAL_ATTRIB(TfToken);
INST_EVAL_ATTRIB(SdfAssetPath);
INST_EVAL_ATTRIB(std::string);

GT_TransformArrayHandle createTransformArray(const VtMatrix4dArray &insts)
{
    auto array = new XUSD_HydraTransforms();
    const int n = insts.size();
    array->setEntries(n);
    for(exint i=0; i<n; i++)
    {
	UT_Matrix4D tr;
	memcpy(tr.data(), insts[i].GetArray(), sizeof(UT_Matrix4D));
	GT_TransformHandle trh = new GT_Transform(&tr, 1);
	array->set(i, trh);
    }

    return array;
}

template <typename A_TYPE>
GT_DataArrayHandle createGTArray(const A_TYPE &usd,
				 GT_Type tinfo,
				 int64 data_id)
{
    auto da= new GusdGT_VtArray<typename A_TYPE::value_type>(usd, tinfo);
    da->setDataId(data_id);
    return GT_DataArrayHandle(da);
}

GT_DataArrayHandle attribGT(const VtValue &value, GT_Type tinfo, int64 data_id)
{
    GT_DataArrayHandle attr;
    
    if(value.IsHolding<VtVec3fArray>())
	attr = createGTArray(value.Get<VtVec3fArray>(), tinfo, data_id);
    else if(value.IsHolding<VtVec4fArray>())
	attr = createGTArray(value.Get<VtVec4fArray>(), tinfo, data_id);
    else if(value.IsHolding<VtVec2fArray>())
	attr = createGTArray(value.Get<VtVec2fArray>(), tinfo, data_id);
    else if(value.IsHolding<VtVec3dArray>())
	attr = createGTArray(value.Get<VtVec3dArray>(), tinfo, data_id);
    else if(value.IsHolding<VtVec4dArray>())
	attr = createGTArray(value.Get<VtVec4dArray>(), tinfo, data_id);
    else if(value.IsHolding<VtVec2dArray>())
	attr = createGTArray(value.Get<VtVec2dArray>(), tinfo, data_id);
    else if(value.IsHolding<VtArray<float> >())
	attr = createGTArray(value.Get<VtArray<float> >(), tinfo, data_id);
    else if(value.IsHolding<VtArray<double> >())
	attr = createGTArray(value.Get<VtArray<double> >(), tinfo, data_id);
    else if(value.IsHolding<VtArray<int> >())
	attr = createGTArray(value.Get<VtArray<int> >(), tinfo, data_id);
    else if(value.IsHolding<VtArray<int64> >())
	attr = createGTArray(value.Get<VtArray<int64> >(), tinfo, data_id);
    else if(value.IsHolding<VtArray<std::string> >())
    {
        VtArray<std::string> v = value.Get<VtArray<std::string> >();
        GT_DAIndexedString *sa = new GT_DAIndexedString(v.size());
        GT_Size idx = 0;
        for(auto s = v.cbegin();  s != v.cend(); ++s)
        {
            sa->setString(idx, 0, *s);
            idx++;
        }
        sa->setDataId(data_id);
        attr = sa;
    }
    else if(value.IsHolding<VtArray<SdfAssetPath> >())
    {
        VtArray<SdfAssetPath> v = value.Get<VtArray<SdfAssetPath> >();
        GT_DAIndexedString *sa = new GT_DAIndexedString(v.size());
        GT_Size idx = 0;
        for(auto s = v.cbegin();  s != v.cend(); ++s)
        {
            sa->setString(idx, 0, s->GetAssetPath());
            idx++;
        }
        sa->setDataId(data_id);
        attr = sa;
    }
    else if(value.IsHolding<VtArray<TfToken> >())
    {
        VtArray<TfToken> v = value.Get<VtArray<TfToken> >();
        GT_DAIndexedString *sa = new GT_DAIndexedString(v.size());
        GT_Size idx = 0;
        for(auto s = v.cbegin();  s != v.cend(); ++s)
        {
            sa->setString(idx, 0, s->GetText());
            idx++;
        }
        sa->setDataId(data_id);
        attr = sa;
    }
    else
	attr.reset();

    return attr;
}
    
#define INST_GT_ARRAY(TYPE)		\
template HUSD_API GT_DataArrayHandle	\
createGTArray<TYPE>(const TYPE &, GT_Type, int64)

INST_GT_ARRAY(VtVec2fArray);
INST_GT_ARRAY(VtVec3fArray);
INST_GT_ARRAY(VtVec4fArray);
INST_GT_ARRAY(VtVec2dArray);
INST_GT_ARRAY(VtVec3dArray);
INST_GT_ARRAY(VtVec4dArray);
INST_GT_ARRAY(VtArray<float>);
INST_GT_ARRAY(VtArray<double>);
INST_GT_ARRAY(VtArray<int>);
INST_GT_ARRAY(VtArray<int64>);

}	// End namespace XUSD_HydraUtils

int64
XUSD_HydraUtils::newDataId()
{
    static int64 theDataID = 1;

    return theDataID++;
}

void
XUSD_HydraUtils::processSubdivTags(
    const PxOsdSubdivTags &subdivTags,
    UT_Array<GT_PrimSubdivisionMesh::Tag> &subd_tags)
{
    // TODO: triangle mode, crease method

    // Creases:
    const VtIntArray &crease_indices = subdivTags.GetCreaseIndices();
    const VtIntArray &crease_lengths = subdivTags.GetCreaseLengths();
    const VtFloatArray &crease_weights = subdivTags.GetCreaseWeights();
    int numedges = 0;
    for (int i = 0; i < crease_lengths.size(); ++i)
	numedges += crease_lengths[i]-1;
    if (numedges)
    {
	GT_Int32Array *creases = new GT_Int32Array(numedges * 2, 1);
	GT_Real32Array *weights = new GT_Real32Array(numedges, 1);
	bool per_crease_weights = 
	    crease_lengths.size() == crease_weights.size();
	int didx = 0;
	int cidx = 0;
	for (int i = 0; i < crease_lengths.size(); ++i)
	{
	    for (int j = 0; j < crease_lengths[i]-1;++j)
	    {
		if (per_crease_weights)
		    weights->data()[didx/2] = crease_weights[i];
		else
		    weights->data()[didx/2] = crease_weights[cidx-i];

		creases->data()[didx++] = crease_indices[cidx++];
		creases->data()[didx++] = crease_indices[cidx];
	    }
	    cidx++;
	}
	GT_PrimSubdivisionMesh::Tag tag("crease");
	tag.appendInt(GT_DataArrayHandle(creases));
	tag.appendReal(GT_DataArrayHandle(weights));
	subd_tags.append(tag);
    }

    // Corners:
    const VtIntArray &corner_indices = subdivTags.GetCornerIndices();
    const VtFloatArray &corner_weights = subdivTags.GetCornerWeights();
    if (corner_indices.size())
    {
	GT_Int32Array *corners = 
	    new GT_Int32Array(corner_indices.size(), 1);
	GT_Real32Array *weights = 
	    new GT_Real32Array(corner_weights.size(), 1);

	memcpy(corners->data(), corner_indices.data(), 
	       sizeof(int) * corner_indices.size());
	memcpy(weights->data(), corner_weights.data(), 
	       sizeof(float) * corner_weights.size());

	GT_PrimSubdivisionMesh::Tag tag("corner");
	tag.appendInt(GT_DataArrayHandle(corners));
	tag.appendReal(GT_DataArrayHandle(weights));
	subd_tags.append(tag);
    }

    // XXX: Apparently the version of USD we're using doesn't support
    //      hole tags.
#if 0  
    // Holes:
    const VtIntArray &hole_indices = subdivTags.GetHoleIndices();
    if (hole_indices.size())
    {
	GT_Int32Array *holes = 
	    new GT_Int32Array(hole_indices.size(), 1);

	memcpy(holes->data(), hole_indices.data(),
	       sizeof(int) * hole_indices.size());

	GT_PrimSubdivisionMesh::Tag tag("hole");
	tag.appendInt(GT_DataArrayHandle(holes));
	subd_tags.append(tag);
    }
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE
