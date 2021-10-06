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
#include <GT/GT_DAConstantValue.h>
#include <GT/GT_DAIndexedString.h>
#include <GT/GT_UtilOpenSubdiv.h>

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdGeom/tokens.h>
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
#include <pxr/imaging/hd/camera.h>
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
INST_EVAL_ATTRIB(HdCamera::Projection);

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
template <typename TYPE>
GT_DataArrayHandle createGTConst(const TYPE &usd,
				 GT_Type tinfo,
				 int64 data_id)
{
    auto da= new GT_DAConstantValue<TYPE>(1, usd, 1, tinfo);
    da->setDataId(data_id);
    return GT_DataArrayHandle(da);
}
template <typename TYPE>
GT_DataArrayHandle createGTConstVec(const TYPE &hvec,
                                    GT_Type tinfo,
                                    int64 data_id)
{
    auto da= new GT_DAConstantValue<typename TYPE::value_type>
                 (1, hvec.data(), hvec.theSize, tinfo);
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
    else if(value.IsHolding<GfVec3f>())
	attr = createGTConstVec(GusdUT_Gf::Cast(value.Get<GfVec3f>()), tinfo, data_id);
    else if(value.IsHolding<GfVec4f>())
	attr = createGTConstVec(GusdUT_Gf::Cast(value.Get<GfVec4f>()), tinfo, data_id);
    else if(value.IsHolding<GfVec2f>())
	attr = createGTConstVec(GusdUT_Gf::Cast(value.Get<GfVec2f>()), tinfo, data_id);
    else if(value.IsHolding<GfVec3d>())
	attr = createGTConstVec(GusdUT_Gf::Cast(value.Get<GfVec3d>()), tinfo, data_id);
    else if(value.IsHolding<GfVec4d>())
	attr = createGTConstVec(GusdUT_Gf::Cast(value.Get<GfVec4d>()), tinfo, data_id);
    else if(value.IsHolding<GfVec2d>())
	attr = createGTConstVec(GusdUT_Gf::Cast(value.Get<GfVec2d>()), tinfo, data_id);
    else if(value.IsHolding<float>())
	attr = createGTConst(value.Get<float >(), tinfo, data_id);
    else if(value.IsHolding<double>())
	attr = createGTConst(value.Get<double >(), tinfo, data_id);
    else if(value.IsHolding<int32>())
	attr = createGTConst(value.Get<int32 >(), tinfo, data_id);
    else if(value.IsHolding<int64>())
	attr = createGTConst(value.Get<int64 >(), tinfo, data_id);
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
            if (s->GetResolvedPath().empty())
                sa->setString(idx, 0, s->GetAssetPath());
            else
                sa->setString(idx, 0, s->GetResolvedPath());
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
    processSubdivTags(subdivTags, VtIntArray(), subd_tags);
}

void
XUSD_HydraUtils::processSubdivTags(
    const PxOsdSubdivTags &subdivTags,
    const VtIntArray &hole_indices,
    UT_Array<GT_PrimSubdivisionMesh::Tag> &subd_tags)
{
    processSubdivTags(subd_tags,
            subdivTags.GetCreaseIndices(),
            subdivTags.GetCreaseLengths(),
            subdivTags.GetCreaseWeights(),

            subdivTags.GetCornerIndices(),
            subdivTags.GetCornerWeights(),

            hole_indices,

            subdivTags.GetVertexInterpolationRule(),
            subdivTags.GetFaceVaryingInterpolationRule()
    );
}

void
XUSD_HydraUtils::processSubdivTags(
    UT_Array<GT_PrimSubdivisionMesh::Tag> &subd_tags,
    // TODO: triangle mode, crease method
    const VtIntArray &crease_indices,
    const VtIntArray &crease_lengths,
    const VtFloatArray &crease_weights,
    const VtIntArray &corner_indices,
    const VtFloatArray &corner_weights,
    const VtIntArray &hole_indices,
    const TfToken &vi_token,
    const TfToken &fvar_token
)
{

    // Creases:
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

    using osd = GT_UtilOpenSubdiv::SdcOptions;

    // Boundary interpolation:
    int value = -1;
    if (vi_token == UsdGeomTokens->none)
        value = osd::VTX_BOUNDARY_NONE;
    else if (vi_token == UsdGeomTokens->edgeOnly)
        value = osd::VTX_BOUNDARY_EDGE_ONLY;
    else if (vi_token == UsdGeomTokens->edgeAndCorner)
        value = osd::VTX_BOUNDARY_EDGE_AND_CORNER;
    if (value != -1)
    {
	GT_PrimSubdivisionMesh::Tag tag("osd_vtxboundaryinterpolation");
	tag.appendInt(GT_DataArrayHandle(new GT_IntConstant(1, value)));
	subd_tags.append(tag);
    }

    // Face-varying interpolation:
    value = -1;
    if (fvar_token == UsdGeomTokens->none)
        value = osd::FVAR_LINEAR_NONE;
    else if (fvar_token == UsdGeomTokens->cornersOnly)
        value = osd::FVAR_LINEAR_CORNERS_ONLY;
    else if (fvar_token == UsdGeomTokens->cornersPlus1)
        value = osd::FVAR_LINEAR_CORNERS_PLUS1;
    else if (fvar_token == UsdGeomTokens->cornersPlus2)
        value = osd::FVAR_LINEAR_CORNERS_PLUS2;
    else if (fvar_token == UsdGeomTokens->boundaries)
        value = osd::FVAR_LINEAR_BOUNDARIES;
    else if (fvar_token == UsdGeomTokens->all)
        value = osd::FVAR_LINEAR_ALL;
    if (value != -1)
    {
	GT_PrimSubdivisionMesh::Tag tag("osd_fvarlinearinterpolation");
	tag.appendInt(GT_DataArrayHandle(new GT_IntConstant(1, value)));
	subd_tags.append(tag);
    }

    // Holes:
    if (hole_indices.size())
    {
	GT_Int32Array *holes = new GT_Int32Array(hole_indices.size(), 1);

	memcpy(holes->data(), hole_indices.data(),
	       sizeof(int) * hole_indices.size());

	GT_PrimSubdivisionMesh::Tag tag("hole");
	tag.appendInt(GT_DataArrayHandle(holes));
	subd_tags.append(tag);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
