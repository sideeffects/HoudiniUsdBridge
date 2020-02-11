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
 */

#include "GEO_FilePrimUtils.h"
#include "GEO_FilePrim.h"
#include "GEO_FilePrimAgentUtils.h"
#include "GEO_FilePrimInstancerUtils.h"
#include "GEO_FilePrimVolumeUtils.h"
#include "GEO_FilePropSource.h"
#include <HUSD/XUSD_Utils.h>
#include <HUSD/XUSD_Format.h>
#include <gusd/GT_PackedUSD.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Gf.h>
#include <GA/GA_AttributeInstanceMatrix.h>
#include <GT/GT_DAIndexedString.h>
#include <GT/GT_DASubArray.h>
#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_PrimCurveMesh.h>
#include <GT/GT_PrimInstance.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_PrimSphere.h>
#include <GT/GT_PrimSubdivisionMesh.h>
#include <GT/GT_PrimTube.h>
#include <GT/GT_PrimVolume.h>
#include <GT/GT_PrimVDB.h>
#include <GT/GT_DAIndirect.h>
#include <GT/GT_Util.h>
#include <GU/GU_Agent.h>
#include <GU/GU_AgentBlendShapeDeformer.h>
#include <GU/GU_AgentBlendShapeUtils.h>
#include <GU/GU_AgentRig.h>
#include <GU/GU_PrimPacked.h>
#include <GU/GU_PackedDisk.h>
#include <UT/UT_ScopeExit.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMMPattern.h>
#include <UT/UT_String.h>
#include <UT/UT_VarEncode.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdSkel/tokens.h>
#include <pxr/usd/usdSkel/topology.h>
#include <pxr/usd/usdSkel/utils.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/usdUtils/pipeline.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

static constexpr UT_StringLit theBoundsName("bounds");
static constexpr UT_StringLit theVisibilityName("visibility");
static constexpr UT_StringLit theVolumeSavePathName("usdvolumesavepath");

static UT_StringHolder
GEOgetStringFromAttrib(const GT_Primitive &gtprim, const UT_StringRef &attrname)
{
    GT_Owner owner;
    GT_DataArrayHandle attrib = gtprim.findAttribute(attrname, owner, 0);

    if (attrib && attrib->getStorage() == GT_STORE_STRING)
        return attrib->getS(0);

    return UT_StringHolder();
}

static TfToken
GEOgetTokenFromAttrib(const GT_Primitive &gtprim, const UT_StringRef &attrname)
{
    UT_StringHolder value = GEOgetStringFromAttrib(gtprim, attrname);
    return value ? TfToken(value) : TfToken();
}

template <typename T>
static const T *
GEOgetAttribValue(const GT_Primitive &gtprim, const UT_StringHolder &attrname,
                  const GEO_ImportOptions &options,
                  UT_ArrayStringSet &processed_attribs, T &value)
{
    if (!options.multiMatch(attrname))
        return nullptr;

    GT_Owner owner;
    GT_DataArrayHandle attrib = gtprim.findAttribute(attrname, owner, 0);

    if (!attrib || attrib->getTupleSize() != UT_FixedVectorTraits<T>::TupleSize)
        return nullptr;

    attrib->import(0, value.data(), UT_FixedVectorTraits<T>::TupleSize);
    processed_attribs.insert(attrname);
    return &value;
}

static UT_Matrix4D
GEOcomputeStandardPointXform(const GT_Primitive &gtprim,
                             const GEO_ImportOptions &options,
                             UT_ArrayStringSet &processed_attribs)
{
    // If the number of attributes changes. this method probably needs
    // updating.
    SYS_STATIC_ASSERT(GA_AttributeInstanceMatrix::theNumAttribs == 10);

    UT_Vector3D P(0, 0, 0);
    GEOgetAttribValue(gtprim, GA_Names::P, options, processed_attribs, P);

    UT_Matrix4D xform(1.0);
    UT_Matrix3D xform3;
    bool has_xform_attrib = false;

    if (GEOgetAttribValue(gtprim, GA_Names::transform, options,
                          processed_attribs, xform))
    {
        has_xform_attrib = true;
    }
    else if (GEOgetAttribValue(gtprim, GA_Names::transform, options,
                               processed_attribs, xform3))
    {
        xform = xform3;
        has_xform_attrib = true;
    }

    // If the transform attrib is present, only P / trans / pivot are used.
    if (has_xform_attrib)
    {
        UT_Vector3D trans(0, 0, 0);
        GEOgetAttribValue(gtprim, GA_Names::trans, options, processed_attribs,
                          trans);

        UT_Vector3D p;
        xform.getTranslates(p);
        p += P + trans;
        xform.setTranslates(p);

        UT_Vector3D pivot;
        if (GEOgetAttribValue(gtprim, GA_Names::pivot, options,
                              processed_attribs, pivot))
        {
            xform.pretranslate(-pivot);
        }

        return xform;
    }

    UT_Vector3D N(0, 0, 0);
    if (!GEOgetAttribValue(gtprim, GA_Names::N, options, processed_attribs, N))
        GEOgetAttribValue(gtprim, GA_Names::v, options, processed_attribs, N);

    UT_FixedVector<double, 1> pscale(1.0);
    GEOgetAttribValue(gtprim, GA_Names::pscale, options, processed_attribs,
                      pscale);

    UT_Vector3D s3, up, trans, pivot;
    UT_QuaternionD rot, orient;

    xform.instance(
        P, N, pscale[0],
        GEOgetAttribValue(gtprim, GA_Names::scale, options, processed_attribs,
                          s3),
        GEOgetAttribValue(gtprim, GA_Names::up, options, processed_attribs, up),
        GEOgetAttribValue(gtprim, GA_Names::rot, options, processed_attribs,
                          rot),
        GEOgetAttribValue(gtprim, GA_Names::trans, options, processed_attribs,
                          trans),
        GEOgetAttribValue(gtprim, GA_Names::orient, options, processed_attribs,
                          orient),
        GEOgetAttribValue(gtprim, GA_Names::pivot, options, processed_attribs,
                          pivot));
    return xform;
}

static void
GEOfilterPackedPrimAttribs(UT_ArrayStringSet &processed_attribs)
{
    // Exclude P from the attributes to import, since it's baked into the
    // prim's transform. It can also cause confusion when inherited on meshes
    // underneath the packed prim's root.
    processed_attribs.insert(GA_Names::P);

    // For now, don't filter the additional point attributes used when
    // pointinstancetransform is enabled. Some, like 'v', are useful to import
    // separately.
}

static bool
GEOhasStaticPackedXform(const GEO_ImportOptions &options)
{
    // Matching GEOfilterPackedPrimAttribs(), only check against P and the
    // packed prim's transform.
    return GA_Names::P.multiMatch(options.myStaticAttribs) &&
           GA_Names::transform.multiMatch(options.myStaticAttribs);
}

static const TfToken &
GEOgetInterpTokenFromMeshOwner(GT_Owner attr_owner)
{
    static UT_Map<GT_Owner, TfToken> theOwnerToInterpMap = {
	{ GT_OWNER_POINT, UsdGeomTokens->vertex },
	{ GT_OWNER_VERTEX, UsdGeomTokens->faceVarying },
	{ GT_OWNER_UNIFORM, UsdGeomTokens->uniform },
	{ GT_OWNER_DETAIL, UsdGeomTokens->constant }
    };

    return theOwnerToInterpMap[attr_owner];
}

static const TfToken &
GEOgetInterpTokenFromCurveOwner(GT_Owner attr_owner)
{
    static UT_Map<GT_Owner, TfToken> theOwnerToInterpMap = {
	{ GT_OWNER_VERTEX, UsdGeomTokens->vertex },
	{ GT_OWNER_UNIFORM, UsdGeomTokens->uniform },
	{ GT_OWNER_DETAIL, UsdGeomTokens->constant }
    };

    return theOwnerToInterpMap[attr_owner];
}

static const TfToken &
GEOgetBasisToken(GT_Basis basis)
{
    static UT_Map<GT_Basis, TfToken> theBasisMap = {
	{ GT_BASIS_BEZIER, UsdGeomTokens->bezier },
	{ GT_BASIS_BSPLINE, UsdGeomTokens->bspline },
	{ GT_BASIS_CATMULLROM, UsdGeomTokens->catmullRom },
	{ GT_BASIS_CATMULL_ROM, UsdGeomTokens->catmullRom },
	{ GT_BASIS_HERMITE, UsdGeomTokens->hermite }
    };

    return theBasisMap[basis];
}

static void
GEOreverseWindingOrder(GT_Int32Array* indices,
	GT_DataArrayHandle faceCounts)
{
    GT_DataArrayHandle buffer;
    int* indicesData = indices->data();
    const int32 *faceCountsData = faceCounts->getI32Array( buffer );
    size_t base = 0;
    for( size_t f = 0; f < faceCounts->entries(); ++f ) {
	int32 numVerts = faceCountsData[f];
	for( size_t p = 1, e = (numVerts + 1) / 2; p < e; ++p ) {
	    std::swap( indicesData[base+p], indicesData[base+numVerts-p] );
	}
	base+= numVerts;
    }
}

static void
initSubsets(GEO_FilePrim &fileprim,
	GEO_FilePrimMap &fileprimmap,
	const GT_FaceSetMapPtr &faceset_map,
	const GEO_ImportOptions &options)
{
    if (!faceset_map)
	return;

    for (auto it = faceset_map->begin(); it != faceset_map->end(); ++it)
    {
	UT_String	 faceset_name(it.name());
	GT_FaceSetPtr	 faceset = it.faceSet();

	if (!faceset_name.multiMatch(options.mySubsetGroups) || !faceset)
	    continue;

	HUSDmakeValidUsdName(faceset_name, false);

	TfToken		 subname(faceset_name);
	SdfPath		 subpath = fileprim.getPath().AppendChild(subname);
	GEO_FilePrim	&subprim = fileprimmap[subpath];
	GEO_FileProp	*prop = nullptr;

	subprim.setPath(subpath);
	subprim.setTypeName(GEO_FilePrimTypeTokens->GeomSubset);
	subprim.setInitialized();
	prop = subprim.addProperty(UsdGeomTokens->indices,
	    SdfValueTypeNames->IntArray,
	    new GEO_FilePropAttribSource<int>(faceset->extractMembers()));
        // Use the topology handling value to decide if geometry subset
        // membership should be time varying or not. There is a Hydra bug
        // that requires geom subsets be time varying if the mesh topology
        // is time varying.
        prop->setValueIsDefault(
            options.myTopologyHandling != GEO_USD_TOPOLOGY_ANIMATED);
    }
}

/// See UsdGeomSubset::SetFamilyType(). Ideally _GetFamilyTypeAttrName() would
/// be accessible ...
static TfToken
GEOgetFamilyTypeAttrName(const TfToken &familyName)
{
    return TfToken(TfStringJoin(std::vector<std::string>{
                GEO_FilePrimTokens->subsetFamily.GetString(),
                familyName.GetString(),
                GEO_FilePrimTokens->familyType.GetString()}, ":"));
}

static void
initPartition(GEO_FilePrim &fileprim,
	GEO_FilePrimMap &fileprimmap,
	const GT_DataArrayHandle &hou_attr,
	const std::string &attr_name,
	const GEO_ImportOptions &options)
{
    struct Partition {
        UT_StringHolder mySubsetName;
        UT_StringHolder mySourceString;
        exint mySourceInt = 0;
        UT_Array<int> myIndices;
    };

    UT_Array<Partition> partitions;
    GEO_FileProp			*prop = nullptr;
    TfToken				 attr_name_token(attr_name);
    UT_String				 primname;

    if (hou_attr->getStorage() == GT_STORE_INT8 ||
	hou_attr->getStorage() == GT_STORE_UINT8 ||
	hou_attr->getStorage() == GT_STORE_INT32 ||
	hou_attr->getStorage() == GT_STORE_INT64)
    {
	UT_Map<exint, exint> value_to_partition;

	for (GT_Offset i = 0, n = hou_attr->entries(); i < n; i++)
	{
	    exint attr_value = hou_attr->getI64(i);

            auto it = value_to_partition.find(attr_value);
            if (it == value_to_partition.end())
            {
		primname.sprintf("%s_%" SYS_PRId64, attr_name.c_str(), attr_value);
		HUSDmakeValidUsdName(primname, false);

                const exint partition_idx = partitions.append();
                value_to_partition[attr_value] = partition_idx;
                Partition &partition = partitions[partition_idx];
                partition.mySubsetName = primname;
                partition.mySourceInt = attr_value;
                partition.myIndices.append(i);
            }
            else
                partitions[it->second].myIndices.append(i);
	}
    }
    else if (hou_attr->getStorage() == GT_STORE_STRING)
    {
	UT_StringMap<exint> value_to_partition;

	for (GT_Offset i = 0, n = hou_attr->entries(); i < n; i++)
	{
	    GT_String	 attr_value = hou_attr->getS(i);

            auto it = value_to_partition.find(attr_value);
            if (it == value_to_partition.end())
            {
		primname.sprintf("%s_%s", attr_name.c_str(),attr_value.c_str());
		HUSDmakeValidUsdName(primname, false);

                const exint partition_idx = partitions.append();
                value_to_partition[attr_value] = partition_idx;
                Partition &partition = partitions[partition_idx];
                partition.mySubsetName = primname;
                partition.mySourceString = attr_value;
                partition.myIndices.append(i);
            }
            else
                partitions[it->second].myIndices.append(i);
	}
    }

    if (partitions.isEmpty())
        return;

    // Author the family type as an attribute on the parent primitive. See
    // UsdGeomSubset::SetFamilyType().
    prop = fileprim.addProperty(
        GEOgetFamilyTypeAttrName(attr_name_token), SdfValueTypeNames->Token,
        new GEO_FilePropConstantSource<TfToken>(UsdGeomTokens->partition));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);

    for (const Partition &partition: partitions)
    {
	TfToken		 subname(partition.mySubsetName);
	SdfPath		 subpath = fileprim.getPath().AppendChild(subname);
	GEO_FilePrim	&subprim = fileprimmap[subpath];

	subprim.setPath(subpath);
	subprim.setTypeName(GEO_FilePrimTypeTokens->GeomSubset);
	subprim.setInitialized();
	prop = subprim.addProperty(UsdGeomTokens->indices,
	    SdfValueTypeNames->IntArray,
	    new GEO_FilePropConstantArraySource<int>(partition.myIndices));
        // Use the topology handling value to decide if geometry subset
        // membership should be time varying or not. There is a Hydra bug
        // that requires geom subsets be time varying if the mesh topology
        // is time varying.
        prop->setValueIsDefault(
            options.myTopologyHandling != GEO_USD_TOPOLOGY_ANIMATED);
	prop = subprim.addProperty(UsdGeomTokens->familyName,
	    SdfValueTypeNames->Token,
	    new GEO_FilePropConstantSource<TfToken>(attr_name_token));
	prop->setValueIsDefault(true);
	prop->setValueIsUniform(true);

        // Record the original value for the partition, without any invalid
        // characters replaced.
        if (partition.mySourceString)
        {
            subprim.addCustomData(
                GEO_FilePrimTokens->partitionValue,
                VtValue(partition.mySourceString.toStdString()));
        }
        else
        {
            subprim.addCustomData(
                GEO_FilePrimTokens->partitionValue,
                VtValue(partition.mySourceInt));
        }
        prop->setValueIsDefault(true);
    }
}

template<class GtT, class GtComponentT = GtT>
GEO_FileProp *
initProperty(GEO_FilePrim &fileprim,
	const GT_DataArrayHandle &hou_attr,
	const UT_StringRef &attr_name,
	GT_Owner attr_owner,
	bool prim_is_curve,
	const GEO_ImportOptions &options,
	const TfToken &usd_attr_name,
	const SdfValueTypeName &usd_attr_type,
	bool create_indices_attr,
	const int64 *override_data_id,
	const GT_DataArrayHandle &vertex_indirect,
        bool override_is_constant)
{
    typedef GEO_FilePropAttribSource<GtT, GtComponentT> FilePropAttribSource;
    typedef GEO_FilePropConstantArraySource<GtT> FilePropConstantSource;

    GEO_FileProp	*prop = nullptr;

    if (hou_attr)
    {
	GT_DataArrayHandle	 src_hou_attr = hou_attr;
	int64			 dataid = override_data_id
					? *override_data_id
					: hou_attr->getDataId();
	GEO_FilePropSource	*prop_source = nullptr;
	FilePropAttribSource	*attrib_source = nullptr;
	bool			 attr_is_constant;
	bool			 attr_is_default;

        attr_is_constant = attr_name.isstring() &&
                           (override_is_constant ||
                            attr_name.multiMatch(options.myConstantAttribs));
        attr_is_default = attr_name.isstring() &&
	    attr_name.multiMatch(options.myStaticAttribs);
	if (attr_is_constant && attr_owner != GT_OWNER_CONSTANT)
	{
	    // If the attribute is configured as "constant", just take the
	    // first value from the attribute and use that as if it were a
	    // detail attribute. Note we can ignore the vertex indirection in
	    // this situation, since all element attribute values are the same.
	    attr_owner = GT_OWNER_DETAIL;
	    src_hou_attr = new GT_DASubArray(hou_attr, GT_Offset(0), 1);
	}
	else if (attr_owner == GT_OWNER_VERTEX && vertex_indirect)
	{
	    // If this is a vertex attribute, and we are changing the
	    // handedness or the geometry, and so have a vertex indirection
	    // array, create the reversed attribute array here.
	    src_hou_attr = new GT_DAIndirect(vertex_indirect, src_hou_attr);
	}

	// Create a FilePropSource for the Houdini attribute. This may be added
	// directly to the FilePrim as a property, or be used as a way to get
	// at the raw elements in a type-safe way.
	attrib_source = new FilePropAttribSource(src_hou_attr);
	prop_source = attrib_source;

	// If this is a primvar being authored, we want to create an ":indices"
	// array for the attribute to make sure that if we are bringing in this
	// geometry as an overlay, and we are overlaying a primvar that had an
	// ":indices" array, that we don't accidentally keep that old
	// ":indices" array. We will either create a real indices attribute, or
	// author a blocking value. The special "SdfValueBlock" value tells USD
	// to return the schema default for the attribute.
	if (create_indices_attr)
	{
	    GEO_FileProp	*indices_prop = nullptr;
	    std::string		 indices_attr_name(usd_attr_name.GetString());

	    indices_attr_name += ":indices";
	    if (!attr_is_constant &&
		attr_name.isstring() &&
		attr_name.multiMatch(options.myIndexAttribs))
	    {
		const GtT	*data = attrib_source->data();
		UT_Array<int>	 indices;
		UT_Array<GtT>	 values;
		UT_Map<GtT, int> attr_map;
		int		 maxidx = 0;

		// We have been asked to author an indices attribute for this
		// primvar. Go through all the values for the primvar, and
		// build a list of unique values and a list of indices into
		// this array of unique values.
		indices.setSizeNoInit(attrib_source->size());
		for (exint i = 0, n = attrib_source->size(); i < n; i++)
		{
		    const GtT	*value = &data[i];
		    auto	 it = attr_map.find(*value);

		    if (it == attr_map.end())
		    {
			it = attr_map.emplace(*value, maxidx++).first;
			values.append(*value);
		    }
		    indices(i) = it->second;
		}

		// Create the indices attribute from the indexes into the array
		// of unique values.
		indices_prop = fileprim.addProperty(
		    TfToken(indices_attr_name),
		    SdfValueTypeNames->IntArray,
		    new GEO_FilePropConstantArraySource<int>(indices));
		if (attr_is_default)
		    indices_prop->setValueIsDefault(true);
		indices_prop->addCustomData(
		    HUSDgetDataIdToken(), VtValue(dataid));

		// Update the data source to just be the array of the unique
		// values.
		delete prop_source;
		prop_source = new FilePropConstantSource(values);
	    }
	    else
	    {
		// Block the indices attribute. Blocked attribute must be set
		// as the default value.
		indices_prop = fileprim.addProperty(TfToken(indices_attr_name),
		    SdfValueTypeNames->IntArray,
		    new GEO_FilePropConstantSource<SdfValueBlock>(
			SdfValueBlock()));
		indices_prop->setValueIsDefault(true);
	    }
	}

	prop = fileprim.addProperty(usd_attr_name, usd_attr_type, prop_source);

	if (attr_owner != GT_OWNER_INVALID)
	{
	    const TfToken &interp = prim_is_curve
		? GEOgetInterpTokenFromCurveOwner(attr_owner)
		: GEOgetInterpTokenFromMeshOwner(attr_owner);

	    if (!interp.IsEmpty())
		prop->addMetadata(UsdGeomTokens->interpolation,
		    VtValue(interp));
	}

	if (attr_is_default)
	    prop->setValueIsDefault(true);
	prop->addCustomData(HUSDgetDataIdToken(), VtValue(dataid));
    }

    return prop;
}

/// Add the UsdSkel joint influence attributes. The interpolation type must be
/// either constant (for rigid deformation) or vertex.
static void
initJointInfluenceAttribs(GEO_FilePrim &fileprim,
                          const VtIntArray &joint_indices,
                          const VtFloatArray &joint_weights,
                          int influences_per_pt, const TfToken &interp_type,
                          const UT_Matrix4D &geom_bind_xform)
{
    GEO_FileProp *prop = fileprim.addProperty(
        UsdSkelTokens->primvarsSkelJointIndices, SdfValueTypeNames->IntArray,
        new GEO_FilePropConstantSource<VtIntArray>(joint_indices));
    prop->addMetadata(UsdGeomTokens->interpolation, VtValue(interp_type));
    prop->addMetadata(UsdGeomTokens->elementSize, VtValue(influences_per_pt));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);

    prop = fileprim.addProperty(
        UsdSkelTokens->primvarsSkelJointWeights, SdfValueTypeNames->FloatArray,
        new GEO_FilePropConstantSource<VtFloatArray>(joint_weights));
    prop->addMetadata(UsdGeomTokens->interpolation, VtValue(interp_type));
    prop->addMetadata(UsdGeomTokens->elementSize, VtValue(influences_per_pt));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);

    prop = fileprim.addProperty(UsdSkelTokens->primvarsSkelGeomBindTransform,
                                SdfValueTypeNames->Matrix4d,
                                new GEO_FilePropConstantSource<GfMatrix4d>(
                                    GusdUT_Gf::Cast(geom_bind_xform)));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);
}

/// Translate the standard boneCapture index-pair point attribute into the
/// UsdSkel joint influence attributes.
static void
initCommonBoneCaptureAttrib(GEO_FilePrim &fileprim,
                            const GT_PrimitiveHandle &gtprim,
                            UT_ArrayStringSet &processed_attribs,
			    const GEO_ImportOptions &options)
{
    const UT_StringHolder &attr_name = GA_Names::boneCapture;

    if (processed_attribs.contains(attr_name) ||
	!options.multiMatch(attr_name))
        return;

    GT_Owner attr_owner = GT_OWNER_INVALID;
    GT_DataArrayHandle hou_attr =
        gtprim->findAttribute(attr_name, attr_owner, 0);
    if (!hou_attr)
        return;

    // Verify that this is a valid index-pair attribute.
    // The GT representation matches the GA_AIFTuple interface, which presents
    // the data as (index0, weight0, index1, weight1, ...), so the tuple size
    // must be a multiple of 2.
    const GT_Type attr_type = hou_attr->getTypeInfo();
    const int tuple_size = hou_attr->getTupleSize();
    if (attr_type != GT_TYPE_INDEXPAIR ||
	attr_owner != GT_OWNER_POINT ||
        (tuple_size % 2) != 0)
        return;

    processed_attribs.insert(attr_name);

    // A fixed number of joint indices and weights are stored per point.
    const int influences_per_pt = tuple_size / 2;
    const exint num_points = hou_attr->entries();

    VtIntArray indices;
    VtFloatArray weights;
    indices.reserve(influences_per_pt * num_points);
    weights.reserve(indices.capacity());

    GT_DataArrayHandle buffer;
    const fpreal32 *data = hou_attr->getF32Array(buffer);
    for (exint pt_idx = 0; pt_idx < num_points; ++pt_idx)
    {
        const exint data_start = pt_idx * tuple_size;
        const exint data_end = data_start + tuple_size;
        for (exint i = data_start; i < data_end; i += 2)
        {
            const int region_idx = static_cast<int>(data[i]);

            // If a point has less than the max number of influences,
            // unused array elements are expected to be filled with zeros.
            if (region_idx < 0)
            {
                indices.push_back(0);
                weights.push_back(0.0);
            }
            else
            {
                indices.push_back(region_idx);
                weights.push_back(data[i + 1]);
            }
        }
    }

    // Sort the joint influences by weight, which is suggested as a best
    // practice in the UsdSkel docs, and also ensure that the weights are
    // normalized.
    UsdSkelSortInfluences(&indices, &weights, influences_per_pt);
    UsdSkelNormalizeWeights(&weights, influences_per_pt);

    UT_Matrix4D geom_bind_xform(1.0);
    initJointInfluenceAttribs(fileprim, indices, weights, influences_per_pt,
                              UsdGeomTokens->vertex, geom_bind_xform);
}

template <class GtT, class GtComponentT>
static GEO_FileProp *
initCommonAttrib(GEO_FilePrim &fileprim,
	const GT_PrimitiveHandle &gtprim,
	const UT_StringRef &attr_name,
	const TfToken &usd_attr_name,
	const SdfValueTypeName &usd_attr_type,
	UT_ArrayStringSet &processed_attribs,
	const GEO_ImportOptions &options,
	bool prim_is_curve,
	bool create_indices_attr,
	const GT_DataArrayHandle &vertex_indirect,
        bool override_is_constant = false)
{
    GT_Owner			 attr_owner = GT_OWNER_INVALID;
    GT_DataArrayHandle		 hou_attr;
    GEO_FileProp		*prop = nullptr;

    if (!processed_attribs.contains(attr_name) &&
	options.multiMatch(attr_name))
    {
	hou_attr = gtprim->findAttribute(attr_name, attr_owner, 0);
	processed_attribs.insert(attr_name);
        prop = initProperty<GtT, GtComponentT>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, usd_attr_type, create_indices_attr, nullptr,
            vertex_indirect, override_is_constant);

        if (prop && usd_attr_name == UsdGeomTokens->normals)
	{
	    // Normals attribute is not quite the same as primvars in how the
	    // interpolation value is set.
	    if (attr_owner == GT_OWNER_VERTEX)
		prop->addMetadata(UsdGeomTokens->interpolation,
		    VtValue(UsdGeomTokens->faceVarying));
	    else
		prop->addMetadata(UsdGeomTokens->interpolation,
		    VtValue(UsdGeomTokens->varying));
	}
    }

    return prop;
}

/// Set up the standard attributes for subdivision meshes.
static void
initSubdAttribs(
    GEO_FilePrim &fileprim,
    const UT_IntrusivePtr<const GT_PrimSubdivisionMesh> &subdmesh,
    UT_ArrayStringSet &processed_attribs, const GEO_ImportOptions &options,
    const GT_DataArrayHandle &vertex_indirect = GT_DataArrayHandle())
{
    static constexpr UT_StringLit theCornerWeightAttrib("cornerweight");
    static constexpr UT_StringLit theHoleAttrib("subdivision_hole");
    static constexpr UT_StringLit theCreaseWeightAttrib("creaseweight");
    static constexpr UT_StringLit theVtxBoundaryInterpName(
        "osd_vtxboundaryinterpolation");
    static constexpr UT_StringLit theFvarInterpName(
        "osd_fvarlinearinterpolation");
    static constexpr UT_StringLit theTriangleSubdivName("osd_trianglesubdiv");

    GEO_FileProp *prop = nullptr;

    // Set up cornerIndices / cornerSharpnesses.
    const GT_PrimSubdivisionMesh::Tag *tag = subdmesh->findTag("corner");
    if (tag && !processed_attribs.contains(theCornerWeightAttrib.asRef()) &&
        options.multiMatch(theCornerWeightAttrib.asRef()))
    {
        processed_attribs.insert(theCornerWeightAttrib.asHolder());

        const bool is_static =
            theCornerWeightAttrib.asRef().multiMatch(options.myStaticAttribs);

        GT_DataArrayHandle indices = tag->intArray();
        GT_DataArrayHandle weights = tag->realArray();
        UT_ASSERT(indices && weights);

        prop = fileprim.addProperty(UsdGeomTokens->cornerIndices,
                             SdfValueTypeNames->IntArray,
                             new GEO_FilePropAttribSource<int>(indices));
        prop->setValueIsDefault(is_static);

        prop = fileprim.addProperty(UsdGeomTokens->cornerSharpnesses,
                             SdfValueTypeNames->FloatArray,
                             new GEO_FilePropAttribSource<float>(weights));
        prop->setValueIsDefault(is_static);
    }

    // Set up holeIndices.
    tag = subdmesh->findTag("hole");
    if (tag && !processed_attribs.contains(theHoleAttrib.asRef()) &&
        options.multiMatch(theHoleAttrib.asRef()))
    {
        processed_attribs.insert(theHoleAttrib.asHolder());

        GT_DataArrayHandle indices = tag->intArray();
        prop = fileprim.addProperty(UsdGeomTokens->holeIndices,
                                    SdfValueTypeNames->IntArray,
                                    new GEO_FilePropAttribSource<int>(indices));
        prop->setValueIsDefault(
            theHoleAttrib.asRef().multiMatch(options.myStaticAttribs));
    }

    // Set up creaseIndices etc.
    tag = subdmesh->findTag("crease");
    if (tag && !processed_attribs.contains(theCreaseWeightAttrib.asRef()) &&
        options.multiMatch(theCreaseWeightAttrib.asRef()))
    {
        processed_attribs.insert(theCreaseWeightAttrib.asHolder());

        GT_DataArrayHandle indices = tag->intArray();
        GT_DataArrayHandle weights = tag->realArray();
        UT_ASSERT(indices && weights);

        const bool is_static =
            theCreaseWeightAttrib.asRef().multiMatch(options.myStaticAttribs);

        prop = fileprim.addProperty(UsdGeomTokens->creaseIndices,
                                    SdfValueTypeNames->IntArray,
                                    new GEO_FilePropAttribSource<int>(indices));
        prop->setValueIsDefault(is_static);

        UT_Array<int> lengths;
        lengths.setSizeNoInit(weights->entries());
        lengths.constant(2);
        prop = fileprim.addProperty(
            UsdGeomTokens->creaseLengths, SdfValueTypeNames->IntArray,
            new GEO_FilePropConstantArraySource<int>(lengths));
        prop->setValueIsDefault(is_static);

        prop = fileprim.addProperty(
            UsdGeomTokens->creaseSharpnesses, SdfValueTypeNames->FloatArray,
            new GEO_FilePropAttribSource<float>(weights));
        prop->setValueIsDefault(is_static);
    }

    // Set up interpolateBoundary.
    tag = subdmesh->findTag(theVtxBoundaryInterpName.asRef());
    if (tag && !processed_attribs.contains(theVtxBoundaryInterpName.asRef()) &&
        options.multiMatch(theVtxBoundaryInterpName.asRef()))
    {
        processed_attribs.insert(theVtxBoundaryInterpName.asHolder());

        TfToken interp_type;
        switch (tag->intArray()->getI32(0))
        {
        case 0:
            interp_type = UsdGeomTokens->none;
            break;
        case 1:
            interp_type = UsdGeomTokens->edgeOnly;
            break;
        case 2:
        default:
            interp_type = UsdGeomTokens->edgeAndCorner;
            break;
        }

        prop = fileprim.addProperty(
            UsdGeomTokens->interpolateBoundary, SdfValueTypeNames->Token,
            new GEO_FilePropConstantSource<TfToken>(interp_type));
        prop->setValueIsDefault(theVtxBoundaryInterpName.asRef().multiMatch(
            options.myStaticAttribs));
    }

    // Set up faceVaryingLinearInterpolation.
    tag = subdmesh->findTag(theFvarInterpName.asRef());
    if (tag && !processed_attribs.contains(theFvarInterpName.asRef()) &&
        options.multiMatch(theFvarInterpName.asRef()))
    {
        processed_attribs.insert(theFvarInterpName.asHolder());

        TfToken interp_type;
        switch (tag->intArray()->getI32(0))
        {
        case 0:
            interp_type = UsdGeomTokens->none;
            break;
        case 1:
            interp_type = UsdGeomTokens->cornersOnly;
            break;
        case 2:
        default:
            interp_type = UsdGeomTokens->cornersPlus1;
            break;
        case 3:
            interp_type = UsdGeomTokens->cornersPlus2;
            break;
        case 4:
            interp_type = UsdGeomTokens->boundaries;
            break;
        case 5:
            interp_type = UsdGeomTokens->all;
            break;
        }

        prop = fileprim.addProperty(
            UsdGeomTokens->faceVaryingLinearInterpolation,
            SdfValueTypeNames->Token,
            new GEO_FilePropConstantSource<TfToken>(interp_type));
        prop->setValueIsDefault(theFvarInterpName.asRef().multiMatch(
            options.myStaticAttribs));
    }

    // Set up triangleSubdivisionRule.
    tag = subdmesh->findTag(theTriangleSubdivName.asRef());
    if (tag && !processed_attribs.contains(theTriangleSubdivName.asRef()) &&
        options.multiMatch(theTriangleSubdivName.asRef()))
    {
        processed_attribs.insert(theTriangleSubdivName.asHolder());

        TfToken rule;
        switch (tag->intArray()->getI32(0))
        {
        case 0:
        default:
            rule = UsdGeomTokens->catmullClark;
            break;
        case 1:
            rule = UsdGeomTokens->smooth;
            break;
        }

        prop = fileprim.addProperty(
            UsdGeomTokens->triangleSubdivisionRule, SdfValueTypeNames->Token,
            new GEO_FilePropConstantSource<TfToken>(rule));
        prop->setValueIsDefault(
            theTriangleSubdivName.asRef().multiMatch(options.myStaticAttribs));
    }
}

/// Convert a texture coordinate attribute from tuple size 3 to 2.
template <typename T>
static GT_DataArrayHandle
GEOconvertToTexCoord2(const GT_DataArrayHandle &uv3_data)
{
    UT_IntrusivePtr<GT_DANumeric<T>> uv2_data =
        new GT_DANumeric<T>(uv3_data->entries(), 2, GT_TYPE_TEXTURE);

    UT_ASSERT(uv3_data->getTupleSize() == 3);
    uv3_data->fillArray(uv2_data->data(), 0, uv3_data->entries(), /* tsize */ 2,
                        /* stride */ 2);

    return uv2_data;
}

/// Translate 'uv' point / vertex attribute to the standard 'st' primvar.
static void
initTextureCoordAttrib(
    GEO_FilePrim &fileprim, const GT_PrimitiveHandle &gtprim,
    UT_ArrayStringSet &processed_attribs, const GEO_ImportOptions &options,
    bool prim_is_curve,
    const GT_DataArrayHandle &vertex_indirect = GT_DataArrayHandle(),
    bool override_is_constant = false)
{
    if (!options.myTranslateUVToST ||
        processed_attribs.contains(GA_Names::uv) ||
        !options.multiMatch(GA_Names::uv))
    {
        return;
    }

    // Only handle point / vertex uv.
    GT_Owner attr_owner = GT_OWNER_INVALID;
    GT_DataArrayHandle uv_attrib =
        gtprim->findAttribute(GA_Names::uv, attr_owner, 0);
    if (!uv_attrib ||
        (attr_owner != GT_OWNER_POINT && attr_owner != GT_OWNER_VERTEX))
    {
        return;
    }

    // Skip the renaming if an 'st' attribute already exists.
    UT_StringHolder st_name =
        GusdUSD_Utils::TokenToStringHolder(UsdUtilsGetPrimaryUVSetName());
    GT_Owner st_owner;
    if (gtprim->findAttribute(st_name, st_owner, 0))
        return;

    // Rename 'uv' to 'st'.
    UT_WorkBuffer buf;
    buf.format("primvars:{0}", st_name);
    TfToken primvars_st(buf.buffer());

    const GT_Storage storage = uv_attrib->getStorage();
    const int tuple_size = uv_attrib->getTupleSize();
    if (tuple_size != 2 && tuple_size != 3 && !GTisFloat(storage))
        return;

    processed_attribs.insert(GA_Names::uv);

    // Cast uv[3] to the expected tuple size of 2 for 'st'.
    if (tuple_size == 3)
    {
        if (storage == GT_STORE_FPREAL16)
            uv_attrib = GEOconvertToTexCoord2<fpreal16>(uv_attrib);
        else if (storage == GT_STORE_FPREAL32)
            uv_attrib = GEOconvertToTexCoord2<fpreal32>(uv_attrib);
        else if (storage == GT_STORE_FPREAL64)
            uv_attrib = GEOconvertToTexCoord2<fpreal64>(uv_attrib);
    }

#define INIT_UV_ATTRIB(GtT, GtComponentT, UsdAttribType)                       \
    initProperty<GtT, GtComponentT>(fileprim, uv_attrib, GA_Names::uv,         \
                                    attr_owner, prim_is_curve, options,        \
                                    primvars_st, UsdAttribType, true, nullptr, \
                                    vertex_indirect, override_is_constant);

    // Import as a primvar with the texCoord* type, regardless of whether the
    // uv attribute has GT_TYPE_TEXTURE.
    if (storage == GT_STORE_FPREAL32)
        INIT_UV_ATTRIB(GfVec2f, fpreal32, SdfValueTypeNames->TexCoord2fArray)
    else if (storage == GT_STORE_FPREAL64)
        INIT_UV_ATTRIB(GfVec2d, fpreal64, SdfValueTypeNames->TexCoord2dArray)
    else if (storage == GT_STORE_FPREAL16)
        INIT_UV_ATTRIB(GfVec2h, fpreal16, SdfValueTypeNames->TexCoord2hArray)

#undef INIT_UV_ATTRIB
}

static void
initVelocityAttrib(
    GEO_FilePrim &fileprim, const GT_PrimitiveHandle &gtprim,
    UT_ArrayStringSet &processed_attribs, const GEO_ImportOptions &options,
    bool prim_is_curve,
    const GT_DataArrayHandle &vertex_indirect = GT_DataArrayHandle(),
    bool override_is_constant = false)
{
    initCommonAttrib<GfVec3f, float>(
        fileprim, gtprim, GA_Names::v, UsdGeomTokens->velocities,
        SdfValueTypeNames->Vector3fArray, processed_attribs, options,
        prim_is_curve, false, vertex_indirect, override_is_constant);
}

static void
initAccelerationAttrib(
    GEO_FilePrim &fileprim, const GT_PrimitiveHandle &gtprim,
    UT_ArrayStringSet &processed_attribs, const GEO_ImportOptions &options,
    bool prim_is_curve,
    const GT_DataArrayHandle &vertex_indirect = GT_DataArrayHandle(),
    bool override_is_constant = false)
{
    initCommonAttrib<GfVec3f, float>(
        fileprim, gtprim, GA_Names::accel, UsdGeomTokens->accelerations,
        SdfValueTypeNames->Vector3fArray, processed_attribs, options,
        prim_is_curve, false, vertex_indirect, override_is_constant);
}

static void
initAngularVelocityAttrib(
    GEO_FilePrim &fileprim, const GT_PrimitiveHandle &gtprim,
    UT_ArrayStringSet &processed_attribs, const GEO_ImportOptions &options,
    bool prim_is_curve,
    const GT_DataArrayHandle &vertex_indirect = GT_DataArrayHandle(),
    bool override_is_constant = false)
{
    initCommonAttrib<GfVec3f, float>(
        fileprim, gtprim, GA_Names::w, UsdGeomTokens->angularVelocities,
        SdfValueTypeNames->Vector3fArray, processed_attribs, options,
        prim_is_curve, false, vertex_indirect, override_is_constant);
}

static void
initColorAttribs(
    GEO_FilePrim &fileprim, const GT_PrimitiveHandle &gtprim,
    UT_ArrayStringSet &processed_attribs, const GEO_ImportOptions &options,
    bool prim_is_curve,
    const GT_DataArrayHandle &vertex_indirect = GT_DataArrayHandle(),
    bool override_is_constant = false)
{
    initCommonAttrib<GfVec3f, float>(fileprim, gtprim, GA_Names::Cd,
	UsdGeomTokens->primvarsDisplayColor, SdfValueTypeNames->Color3fArray,
	processed_attribs, options, prim_is_curve, true, vertex_indirect);

    initCommonAttrib<float, float>(fileprim, gtprim, GA_Names::Alpha,
	UsdGeomTokens->primvarsDisplayOpacity, SdfValueTypeNames->FloatArray,
	processed_attribs, options, prim_is_curve, true, vertex_indirect);
}

static void
initCommonAttribs(GEO_FilePrim &fileprim,
	const GT_PrimitiveHandle &gtprim,
	UT_ArrayStringSet &processed_attribs,
	const GEO_ImportOptions &options,
	bool prim_is_curve,
	const GT_DataArrayHandle &vertex_indirect = GT_DataArrayHandle())
{
    initCommonAttrib<GfVec3f, float>(fileprim, gtprim, GA_Names::P,
	UsdGeomTokens->points, SdfValueTypeNames->Point3fArray,
	processed_attribs, options, prim_is_curve, false, vertex_indirect);

    initCommonAttrib<GfVec3f, float>(fileprim, gtprim, GA_Names::N,
	UsdGeomTokens->normals, SdfValueTypeNames->Normal3fArray,
	processed_attribs, options, prim_is_curve, false, vertex_indirect);

    initColorAttribs(fileprim, gtprim, processed_attribs, options,
                     prim_is_curve, vertex_indirect);
    initVelocityAttrib(fileprim, gtprim, processed_attribs, options,
                       prim_is_curve, vertex_indirect);
    initAccelerationAttrib(fileprim, gtprim, processed_attribs, options,
                           prim_is_curve, vertex_indirect);
    initTextureCoordAttrib(fileprim, gtprim, processed_attribs, options,
                           prim_is_curve, vertex_indirect);
    initCommonBoneCaptureAttrib(fileprim, gtprim, processed_attribs, options);
}

GT_DataArrayHandle
GEOscaleWidthsAttrib(const GT_DataArrayHandle &width_attr, const fpreal scale)
{
    if (SYSisEqual(scale, 1.0) || width_attr->getTupleSize() != 1)
        return width_attr;

    UT_IntrusivePtr<GT_DANumeric<float>> scaled_widths =
        new GT_DANumeric<float>(width_attr->entries(), 1);

    GT_DataArrayHandle buffer;
    const float *src_data = width_attr->getF32Array(buffer);
    float *data = scaled_widths->data();

    for (exint i = 0, n = width_attr->entries(); i < n; ++i)
        data[i] = src_data[i] * scale;

    return scaled_widths;
}

static void
initPointSizeAttribs(GEO_FilePrim &fileprim,
	const GT_PrimitiveHandle &gtprim,
	UT_ArrayStringSet &processed_attribs,
	const GEO_ImportOptions &options,
	bool prim_is_curve)
{
    GT_Owner attr_owner = GT_OWNER_INVALID;

    UT_StringHolder width_name = "widths"_sh;
    fpreal scale = 1.0;
    if (!options.multiMatch(width_name) ||
        !gtprim->findAttribute(width_name, attr_owner, 0))
    {
        width_name = GA_Names::width;
    }
    if (!options.multiMatch(width_name) ||
        !gtprim->findAttribute(width_name, attr_owner, 0))
    {
        // pscale represents radius, but widths in USD is a diameter.
        width_name = GA_Names::pscale;
        scale = 2;
    }

    if (processed_attribs.contains(width_name) ||
        !options.multiMatch(width_name))
    {
        return;
    }

    GT_DataArrayHandle width_attr = gtprim->findAttribute(
        width_name, attr_owner, 0);
    processed_attribs.insert(width_name);

    if (!width_attr)
        return;

    width_attr = GEOscaleWidthsAttrib(width_attr, scale);
    initProperty<float, float>(fileprim, width_attr, width_name, attr_owner,
                               prim_is_curve, options, UsdGeomTokens->widths,
                               SdfValueTypeNames->FloatArray, false, nullptr,
                               nullptr, false);
}

static void
initPointIdsAttrib(GEO_FilePrim &fileprim,
	const GT_PrimitiveHandle &gtprim,
	UT_ArrayStringSet &processed_attribs,
	const GEO_ImportOptions &options,
	bool prim_is_curve)
{
    initCommonAttrib<int64, int64>(fileprim, gtprim, GA_Names::id,
	UsdGeomTokens->ids, SdfValueTypeNames->Int64Array,
	processed_attribs, options, prim_is_curve, false, GT_DataArrayHandle());
}

/// Import an array attribute as two primvars:
///  - an array of constant interpolation with the concatenated values
///  - a list of array lengths, with the normal interpolation
template<typename GtT, class GtComponentT = GtT>
static GEO_FileProp *
initExtraArrayAttrib(GEO_FilePrim &fileprim, GT_DataArrayHandle hou_attr,
                     const UT_StringRef &attr_name, GT_Owner attr_owner,
                     bool prim_is_curve, const GEO_ImportOptions &options,
                     const TfToken &usd_attr_name,
                     const SdfValueTypeName &usd_attr_type,
                     const GT_DataArrayHandle &vertex_indirect,
                     bool override_is_constant)
{
    UT_IntrusivePtr<GT_DANumeric<GtComponentT>> all_values =
        new GT_DANumeric<GtComponentT>(0, 1);
    UT_IntrusivePtr<GT_DANumeric<exint>> lengths =
        new GT_DANumeric<exint>(0, 1);

    const bool is_constant = attr_name.multiMatch(options.myConstantAttribs);
    const exint n = is_constant ? 1 : hou_attr->entries();
    const GT_Size tuple_size = hou_attr->getTupleSize();

    if (attr_owner == GT_OWNER_VERTEX && vertex_indirect)
        hou_attr = new GT_DAIndirect(vertex_indirect, hou_attr);

    UT_ValArray<GtComponentT> values;
    for (exint i = 0; i < n; ++i)
    {
        values.clear();
        hou_attr->import(i, values);

        exint length = values.size();
        if (tuple_size > 1)
            length /= tuple_size;

        lengths->append(length);
        for (auto &&value : values)
            all_values->append(value);
    }

    std::string lengths_attr_name(usd_attr_name.GetString());
    lengths_attr_name += ":lengths";

    GEO_FileProp *prop = nullptr;
    prop = initProperty<int32>(
        fileprim, lengths, attr_name, attr_owner, prim_is_curve, options,
        TfToken(lengths_attr_name), SdfValueTypeNames->IntArray, false, nullptr,
        nullptr, override_is_constant);

    prop = initProperty<GtT, GtComponentT>(
        fileprim, all_values, attr_name, GT_OWNER_CONSTANT, prim_is_curve,
        options, usd_attr_name, usd_attr_type, true, nullptr, nullptr,
        override_is_constant);
    prop->addMetadata(UsdGeomTokens->elementSize,
                      VtValue(static_cast<int>(tuple_size)));

    return prop;
}

/// Specialization of initExtraArrayAttrib() for strings.
template <>
GEO_FileProp *
initExtraArrayAttrib<std::string>(
    GEO_FilePrim &fileprim, GT_DataArrayHandle hou_attr,
    const UT_StringRef &attr_name, GT_Owner attr_owner, bool prim_is_curve,
    const GEO_ImportOptions &options, const TfToken &usd_attr_name,
    const SdfValueTypeName &usd_attr_type,
    const GT_DataArrayHandle &vertex_indirect, bool override_is_constant)
{
    UT_IntrusivePtr<GT_DAIndexedString> all_values = new GT_DAIndexedString(0);
    UT_IntrusivePtr<GT_DANumeric<exint>> lengths =
        new GT_DANumeric<exint>(0, 1);

    const bool is_constant = attr_name.multiMatch(options.myConstantAttribs);
    const exint n = is_constant ? 1 : hou_attr->entries();
    const GT_Size tuple_size = hou_attr->getTupleSize();

    if (attr_owner == GT_OWNER_VERTEX && vertex_indirect)
        hou_attr = new GT_DAIndirect(vertex_indirect, hou_attr);

    UT_StringArray values;

    // Make a first pass to compute the total number of strings.
    exint entries = 0;
    for (exint i = 0; i < n; ++i)
    {
        values.clear();
        hou_attr->getSA(values, i);
        entries += values.size();
    }

    // Fill in the lists of strings and lengths.
    all_values->resize(entries);
    entries = 0;
    for (exint i = 0; i < n; ++i)
    {
        values.clear();
        hou_attr->getSA(values, i);

        exint length = values.size();
        if (tuple_size > 1)
            length /= tuple_size;

        lengths->append(length);

        for (exint j = 0; j < values.size(); ++j)
        {
            all_values->setString(entries, 0, values[j]);
            ++entries;
        }
    }

    std::string lengths_attr_name(usd_attr_name.GetString());
    lengths_attr_name += ":lengths";

    GEO_FileProp *prop = nullptr;
    prop = initProperty<int32>(
        fileprim, lengths, attr_name, attr_owner, prim_is_curve, options,
        TfToken(lengths_attr_name), SdfValueTypeNames->IntArray, false, nullptr,
        nullptr, override_is_constant);
    prop = initProperty<std::string>(
        fileprim, all_values, attr_name, GT_OWNER_CONSTANT, prim_is_curve,
        options, usd_attr_name, usd_attr_type, true, nullptr, nullptr,
        override_is_constant);
    prop->addMetadata(UsdGeomTokens->elementSize,
                      VtValue(static_cast<int>(tuple_size)));
    return prop;
}

static GEO_FileProp *
initExtraAttrib(GEO_FilePrim &fileprim,
	const GT_DataArrayHandle &hou_attr,
	const UT_StringRef &attr_name,
	GT_Owner attr_owner,
	bool prim_is_curve,
	const GEO_ImportOptions &options,
	const GT_DataArrayHandle &vertex_indirect,
        bool override_is_constant)
{
    static std::string	 thePrimvarPrefix("primvars:");
    GT_Storage		 storage = hou_attr->getStorage();
    int			 tuple_size = hou_attr->getTupleSize();
    GT_Type		 attr_type = hou_attr->getTypeInfo();
    UT_StringHolder	 decoded_attr_name =
			    UT_VarEncode::decodeAttrib(attr_name);

    TfToken		 usd_attr_name;
    bool                 create_indices_attr = true;
    // For custom attributes, don't add the "primvars:" prefix or create
    // indexed primvars.
    if (attr_name.multiMatch(options.myCustomAttribs))
    {
        usd_attr_name = TfToken(decoded_attr_name.toStdString());
        create_indices_attr = false;
    }
    else
    {
        usd_attr_name =
            TfToken(thePrimvarPrefix + decoded_attr_name.toStdString());
    }

    GEO_FileProp	*prop = nullptr;
    if (hou_attr->hasArrayEntries())
    {
#define INIT_ARRAY_ATTRIB(GtT, GtComponentT, UsdAttribType)                    \
    initExtraArrayAttrib<GtT, GtComponentT>(                                   \
        fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,     \
        usd_attr_name, UsdAttribType, vertex_indirect, override_is_constant)

        if (storage == GT_STORE_INT32)
            prop = INIT_ARRAY_ATTRIB(int32, int32, SdfValueTypeNames->IntArray);
        else if (storage == GT_STORE_INT64)
        {
            prop = INIT_ARRAY_ATTRIB(int64, int64,
                                     SdfValueTypeNames->Int64Array);
        }
        else if (storage == GT_STORE_FPREAL16)
        {
            prop = INIT_ARRAY_ATTRIB(GfHalf, fpreal16,
                                     SdfValueTypeNames->HalfArray);
        }
        else if (storage == GT_STORE_FPREAL32)
        {
            prop = INIT_ARRAY_ATTRIB(fpreal32, fpreal32,
                                     SdfValueTypeNames->FloatArray);
        }
        else if (storage == GT_STORE_FPREAL64)
        {
            prop = INIT_ARRAY_ATTRIB(fpreal64, fpreal64,
                                     SdfValueTypeNames->DoubleArray);
        }
        else if (storage == GT_STORE_STRING)
        {
            prop = INIT_ARRAY_ATTRIB(std::string, std::string,
                                     SdfValueTypeNames->StringArray);
        }
        else
            UT_ASSERT_MSG(false, "Unsupported array attribute type.");

#undef INIT_ARRAY_ATTRIB

        return prop;
    }

    if (tuple_size == 16 && attr_type == GT_TYPE_MATRIX)
    {
        prop = initProperty<GfMatrix4d, fpreal64>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->Matrix4dArray,
            create_indices_attr, nullptr, vertex_indirect,
            override_is_constant);
    }
    else if (tuple_size == 9 && attr_type == GT_TYPE_MATRIX3)
    {
        prop = initProperty<GfMatrix3d, fpreal64>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->Matrix3dArray,
            create_indices_attr, nullptr, vertex_indirect,
            override_is_constant);
    }
    else if (tuple_size == 3 && attr_type == GT_TYPE_POINT)
    {
        prop = initProperty<GfVec3f, fpreal32>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->Point3fArray, create_indices_attr,
            nullptr, vertex_indirect, override_is_constant);
    }
    else if (tuple_size == 3 && attr_type == GT_TYPE_VECTOR)
    {
        prop = initProperty<GfVec3f, fpreal32>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->Vector3fArray,
            create_indices_attr, nullptr, vertex_indirect,
            override_is_constant);
    }
    else if (tuple_size == 3 && attr_type == GT_TYPE_NORMAL)
    {
        prop = initProperty<GfVec3f, fpreal32>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->Normal3fArray,
            create_indices_attr, nullptr, vertex_indirect,
            override_is_constant);
    }
    else if (tuple_size == 3 && attr_type == GT_TYPE_COLOR)
    {
        prop = initProperty<GfVec3f, fpreal32>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->Color3fArray, create_indices_attr,
            nullptr, vertex_indirect, override_is_constant);
    }
    else if (tuple_size == 4 && attr_type == GT_TYPE_COLOR)
    {
        prop = initProperty<GfVec4f, fpreal32>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->Color4fArray, create_indices_attr,
            nullptr, vertex_indirect, override_is_constant);
    }
    else if (tuple_size == 4 && attr_type == GT_TYPE_QUATERNION)
    {
        prop = initProperty<GfQuatf, fpreal32>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->QuatfArray, create_indices_attr,
            nullptr, vertex_indirect, override_is_constant);
    }
    else if (storage == GT_STORE_REAL32)
    {
        if (tuple_size == 4)
        {
            prop = initProperty<GfVec4f, fpreal32>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Float4Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 3)
        {
            prop = initProperty<GfVec3f, fpreal32>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name,
                attr_type == GT_TYPE_TEXTURE ?
                    SdfValueTypeNames->TexCoord3fArray :
                    SdfValueTypeNames->Float3Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 2)
        {
            prop = initProperty<GfVec2f, fpreal32>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name,
                attr_type == GT_TYPE_TEXTURE ?
                    SdfValueTypeNames->TexCoord2fArray :
                    SdfValueTypeNames->Float2Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 1)
        {
            prop = initProperty<fpreal32>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->FloatArray,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 16)
        {
            prop = initProperty<GfMatrix4d, fpreal64>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Matrix4dArray,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 9)
        {
            prop = initProperty<GfMatrix3d, fpreal64>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Matrix3dArray,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
    }
    else if (storage == GT_STORE_REAL64)
    {
        if (tuple_size == 4)
        {
            prop = initProperty<GfVec4d, fpreal64>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Double4Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 3)
        {
            prop = initProperty<GfVec3d, fpreal64>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name,
                attr_type == GT_TYPE_TEXTURE ?
                    SdfValueTypeNames->TexCoord3dArray :
                    SdfValueTypeNames->Double3Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 2)
        {
            prop = initProperty<GfVec2d, fpreal64>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name,
                attr_type == GT_TYPE_TEXTURE ?
                    SdfValueTypeNames->TexCoord2dArray :
                    SdfValueTypeNames->Double2Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 1)
        {
            prop = initProperty<fpreal64>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->DoubleArray,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 16)
        {
            prop = initProperty<GfMatrix4d, fpreal64>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Matrix4dArray,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 9)
        {
            prop = initProperty<GfMatrix3d, fpreal64>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Matrix3dArray,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
    }
    else if (storage == GT_STORE_REAL16)
    {
        if (tuple_size == 4)
        {
            prop = initProperty<GfVec4h, fpreal16>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Half4Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 3)
        {
            prop = initProperty<GfVec3h, fpreal16>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name,
                attr_type == GT_TYPE_TEXTURE ?
                    SdfValueTypeNames->TexCoord3hArray :
                    SdfValueTypeNames->Half3Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 2)
        {
            prop = initProperty<GfVec2h, fpreal16>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name,
                attr_type == GT_TYPE_TEXTURE ?
                    SdfValueTypeNames->TexCoord2hArray :
                    SdfValueTypeNames->Half2Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 1)
        {
            prop = initProperty<GfHalf, fpreal16>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->HalfArray,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
    }
    else if (storage == GT_STORE_INT32)
    {
        if (tuple_size == 4)
        {
            prop = initProperty<GfVec4i, int>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Int4Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 3)
        {
            prop = initProperty<GfVec3i, int>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Int3Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 2)
        {
            prop = initProperty<GfVec2i, int>(
                fileprim, hou_attr, attr_name, attr_owner, prim_is_curve,
                options, usd_attr_name, SdfValueTypeNames->Int2Array,
                create_indices_attr, nullptr, vertex_indirect,
                override_is_constant);
        }
        else if (tuple_size == 1)
        {
            prop = initProperty<int>(fileprim, hou_attr, attr_name, attr_owner,
                                     prim_is_curve, options, usd_attr_name,
                                     SdfValueTypeNames->IntArray,
                                     create_indices_attr, nullptr,
                                     vertex_indirect, override_is_constant);
        }
    }
    else if (storage == GT_STORE_INT64)
    {
        UT_ASSERT(tuple_size == 1);
        prop = initProperty<int64>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->Int64Array, create_indices_attr,
            nullptr, vertex_indirect, override_is_constant);
    }
    else if (storage == GT_STORE_STRING)
    {
        prop = initProperty<std::string>(
            fileprim, hou_attr, attr_name, attr_owner, prim_is_curve, options,
            usd_attr_name, SdfValueTypeNames->StringArray, create_indices_attr,
            nullptr, vertex_indirect, override_is_constant);
    }

    return prop;
}

static void
initExtraAttribs(GEO_FilePrim &fileprim,
	GEO_FilePrimMap &fileprimmap,
	const GT_PrimitiveHandle &gtprim,
	const GT_Owner *owners,
	const UT_ArrayStringSet &processed_attribs,
	const GEO_ImportOptions &options,
	bool prim_is_curve,
	const GT_DataArrayHandle &vertex_indirect = GT_DataArrayHandle(),
        bool override_is_constant = false)
{
    for (int i = 0; owners[i] != GT_OWNER_INVALID; i++)
    {
	GT_Owner	 attr_owner = owners[i];
	auto		 attr_list = gtprim->getAttributeList(attr_owner);

	if (!attr_list)
	    continue;

	for (exint i = 0, n = attr_list->entries(); i < n; ++i)
	{
	    const UT_StringHolder	&attr_name(attr_list->getName(i));

	    if (!processed_attribs.contains(attr_name))
	    {
		if (attr_owner == GT_OWNER_UNIFORM &&
		    attr_name.multiMatch(options.myPartitionAttribs))
		{
		    GT_DataArrayHandle	 hou_attr = attr_list->get(i);

		    if (!hou_attr->hasArrayEntries())
			initPartition(fileprim, fileprimmap,
			    hou_attr, attr_name.toStdString(), options);
		}
		else if (options.multiMatch(attr_name))
		{
		    GT_DataArrayHandle	 hou_attr = attr_list->get(i);

                    initExtraAttrib(fileprim, hou_attr,
                        attr_name, attr_owner, prim_is_curve,
                        options, vertex_indirect, override_is_constant);
		}

		// We don't need to bother adding this new attribute to the
		// set of processed attribs, because this function is always
		// the last scan through the geometry attributes. So don't
		// waste the time modifying the set.
	    }
	}
    }
}

static void
initXformAttrib(GEO_FilePrim &fileprim, const UT_Matrix4D &prim_xform,
                const GEO_ImportOptions &options)
{
    bool prim_xform_identity = prim_xform.isIdentity();

    if (!prim_xform_identity &&
        GA_Names::transform.multiMatch(options.myAttribs))
    {
        GEO_FileProp *prop = nullptr;
        VtArray<TfToken> xform_op_order;

        prop = fileprim.addProperty(GEO_FilePrimTokens->XformOpBase,
                                    SdfValueTypeNames->Matrix4d,
                                    new GEO_FilePropConstantSource<GfMatrix4d>(
                                        GusdUT_Gf::Cast(prim_xform)));
        prop->setValueIsDefault(
            GA_Names::transform.multiMatch(options.myStaticAttribs));

        xform_op_order.push_back(GEO_FilePrimTokens->XformOpBase);
        prop = fileprim.addProperty(
            UsdGeomTokens->xformOpOrder, SdfValueTypeNames->TokenArray,
            new GEO_FilePropConstantSource<VtArray<TfToken>>(xform_op_order));
        prop->setValueIsDefault(true);
        prop->setValueIsUniform(true);
    }
}

static void
initPurposeAttrib(GEO_FilePrim &fileprim, const TfToken &purpose_type)
{
    GEO_FileProp *prop = fileprim.addProperty(
        UsdGeomTokens->purpose, SdfValueTypeNames->Token,
        new GEO_FilePropConstantSource<TfToken>(purpose_type));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);
}

/// Author visibility with a specific value.
static void
initVisibilityAttrib(GEO_FilePrim &fileprim, bool visible,
                     const GEO_ImportOptions &options, bool force = false,
                     bool force_static = false)
{
    if (!force && !theVisibilityName.asRef().multiMatch(options.myAttribs))
        return;

    GEO_FileProp *prop = fileprim.addProperty(
        UsdGeomTokens->visibility, SdfValueTypeNames->Token,
        new GEO_FilePropConstantSource<TfToken>(
            visible ? UsdGeomTokens->inherited : UsdGeomTokens->invisible));

    prop->setValueIsDefault(
        force_static ||
        theVisibilityName.asRef().multiMatch(options.myStaticAttribs));
    prop->setValueIsUniform(force_static);
}

/// Author visibility from the usdvisibility attribute, if it exists.
static void
initVisibilityAttrib(GEO_FilePrim &fileprim, const GT_Primitive &gtprim,
                     const GEO_ImportOptions &options)
{
    static constexpr UT_StringLit theVisibilityAttrib("usdvisibility");

    TfToken visibility =
        GEOgetTokenFromAttrib(gtprim, theVisibilityAttrib.asRef());
    if (visibility.IsEmpty())
        return;

    initVisibilityAttrib(fileprim, visibility != UsdGeomTokens->invisible,
                         options);
}

static void
initExtentAttrib(GEO_FilePrim &fileprim,
	const GT_PrimitiveHandle &gtprim,
	UT_ArrayStringSet &processed_attribs,
	const GEO_ImportOptions &options,
        bool force = false)
{
    const UT_StringHolder &bounds_name = theBoundsName.asHolder();

    if (!processed_attribs.contains(bounds_name) &&
	(force || bounds_name.multiMatch(options.myAttribs)))
    {
	UT_BoundingBox	 bboxes[1];
	VtVec3fArray	 extent(2);
	GEO_FileProp	*prop = nullptr;

	bboxes[0].makeInvalid();
	gtprim->enlargeBounds(bboxes, 1);
	extent[0] = GfVec3f(bboxes[0].xmin(),
			    bboxes[0].ymin(),
			    bboxes[0].zmin());
	extent[1] = GfVec3f(bboxes[0].xmax(),
			    bboxes[0].ymax(),
			    bboxes[0].zmax());
	prop = fileprim.addProperty(UsdGeomTokens->extent,
	    SdfValueTypeNames->Float3Array,
	    new GEO_FilePropConstantSource<VtVec3fArray>(
		extent));
	if (prop && bounds_name.multiMatch(options.myStaticAttribs))
	    prop->setValueIsDefault(true);
	processed_attribs.insert(bounds_name);
    }
}

static void
initInternalReference(GEO_FilePrim &fileprim, const SdfPath &reference_path)
{
    SdfReferenceListOp references;
    references.SetPrependedItems({SdfReference(std::string(), reference_path)});
    fileprim.addMetadata(SdfFieldKeys->References, VtValue(references));
}

static void
initPayload(GEO_FilePrim &fileprim, const std::string &asset_path)
{
    SdfPayloadListOp payload;
    payload.SetAppendedItems({SdfPayload(asset_path)});
    fileprim.addMetadata(SdfFieldKeys->Payload, VtValue(payload));
}

static void
initKind(GEO_FilePrim &fileprim,
	GEO_KindSchema kindschema,
	GEO_KindGuide kindguide)
{
    // Set "Kind" metadata on a primitive. Note that we use replaceMetadata
    // instead of addMetadata so that we can modify an existing value.
    switch (kindschema)
    {
	case GEO_KINDSCHEMA_NONE:
	    break;

	case GEO_KINDSCHEMA_COMPONENT:
	    if (kindguide == GEO_KINDGUIDE_TOP)
		fileprim.replaceMetadata(SdfFieldKeys->Kind,
		    VtValue(KindTokens->component));
	    break;

	case GEO_KINDSCHEMA_NESTED_GROUP:
	    if (kindguide == GEO_KINDGUIDE_LEAF)
		fileprim.replaceMetadata(SdfFieldKeys->Kind,
		    VtValue(KindTokens->component));
	    else
		fileprim.replaceMetadata(SdfFieldKeys->Kind,
		    VtValue(KindTokens->group));
	    break;

	case GEO_KINDSCHEMA_NESTED_ASSEMBLY:
	    if (kindguide == GEO_KINDGUIDE_LEAF)
		fileprim.replaceMetadata(SdfFieldKeys->Kind,
		    VtValue(KindTokens->component));
	    else if (kindguide == GEO_KINDGUIDE_BRANCH)
		fileprim.replaceMetadata(SdfFieldKeys->Kind,
		    VtValue(KindTokens->group));
	    else if (kindguide == GEO_KINDGUIDE_TOP)
		fileprim.replaceMetadata(SdfFieldKeys->Kind,
		    VtValue(KindTokens->assembly));
	    break;
    };
}

void
GEOsetKind(GEO_FilePrim &fileprim,
	GEO_KindSchema kindschema,
	GEO_KindGuide kindguide)
{
    initKind(fileprim, kindschema, kindguide);
}

void
GEOinitRootPrim(GEO_FilePrim &fileprim,
	const TfToken &default_prim_name,
        bool save_sample_frame,
        fpreal sample_frame)
{
    if (!default_prim_name.IsEmpty())
	fileprim.addMetadata(SdfFieldKeys->DefaultPrim,
	    VtValue(default_prim_name));

    if (save_sample_frame)
    {
	fileprim.addMetadata(SdfFieldKeys->StartTimeCode,
	    VtValue(sample_frame));
	fileprim.addMetadata(SdfFieldKeys->EndTimeCode,
	    VtValue(sample_frame));
    }

    fileprim.setInitialized();
}

void
GEOinitXformPrim(GEO_FilePrim &fileprim,
	GEO_HandleOtherPrims other_handling,
	GEO_KindSchema kindschema)
{
    if (other_handling == GEO_OTHER_DEFINE)
    {
	fileprim.setTypeName(GEO_FilePrimTypeTokens->Xform);
	initKind(fileprim, kindschema, GEO_KINDGUIDE_BRANCH);
    }
    fileprim.setIsDefined(other_handling == GEO_OTHER_DEFINE);
    fileprim.setInitialized();
}

void
GEOinitXformOver(GEO_FilePrim &fileprim, const GT_PrimitiveHandle &gtprim,
                 const UT_Matrix4D &prim_xform,
                 const GEO_ImportOptions &options)
{
    initXformAttrib(fileprim, prim_xform, options);
    fileprim.setIsDefined(false);
    fileprim.setInitialized();
}

/// Define a Skeleton primitive for the given GEO_AgentSkeleton.
static void
initSkeletonPrim(const GEO_FilePrim &defn_root, GEO_FilePrimMap &fileprimmap,
                 const GEO_ImportOptions &options, const GU_AgentRig &rig,
                 const GEO_AgentSkeleton &skeleton,
                 const VtTokenArray &joint_paths,
                 const UT_Array<exint> &joint_order)
{
    SdfPath skel_path = defn_root.getPath().AppendChild(skeleton.myName);
    GEO_FilePrim &skel_prim = fileprimmap[skel_path];
    skel_prim.setTypeName(GEO_FilePrimTypeTokens->Skeleton);
    skel_prim.setPath(skel_path);
    initPurposeAttrib(skel_prim, UsdGeomTokens->guide);
    skel_prim.setIsDefined(true);
    skel_prim.setInitialized();

    // Record the joint list.
    GEO_FileProp *prop = skel_prim.addProperty(
        UsdSkelTokens->joints, SdfValueTypeNames->TokenArray,
        new GEO_FilePropConstantSource<VtTokenArray>(joint_paths));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);

    // Also record the original unique joint names from GU_AgentRig.
    // These can be used instead of the full paths when importing into another
    // format (e.g. back to SOPs).
    VtTokenArray joint_names;
    joint_names.resize(joint_paths.size());
    for (exint i = 0, n = rig.transformCount(); i < n; ++i)
    {
        joint_names[joint_order[i]] =
            TfToken(rig.transformName(i).toStdString());
    }

    prop = skel_prim.addProperty(
        UsdSkelTokens->jointNames, SdfValueTypeNames->TokenArray,
        new GEO_FilePropConstantSource<VtTokenArray>(joint_names));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);

    // Set up the bind pose, which must also be re-ordered to match the order
    // of the USD joint list.
    VtMatrix4dArray bind_xforms =
        GEOconvertXformArray(rig, skeleton.myBindPose, joint_order);

    prop = skel_prim.addProperty(
        UsdSkelTokens->bindTransforms, SdfValueTypeNames->Matrix4dArray,
        new GEO_FilePropConstantSource<VtMatrix4dArray>(bind_xforms));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);

    // The rest transforms aren't strictly necessary since for each agent we
    // provide animation for all of the joints, but this ensures that the
    // source skeleton (which doesn't have an animation source) looks
    // reasonable if it's viewed.
    UsdSkelTopology topology(joint_paths);
    VtMatrix4dArray rest_xforms;
    UsdSkelComputeJointLocalTransforms(topology, bind_xforms, &rest_xforms);

    prop = skel_prim.addProperty(
        UsdSkelTokens->restTransforms, SdfValueTypeNames->Matrix4dArray,
        new GEO_FilePropConstantSource<VtMatrix4dArray>(rest_xforms));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);
}

/// Define a SkelAnimation prim from the given agent's pose.
static void
initSkelAnimationPrim(GEO_FilePrim &anim_prim, const GU_Agent &agent,
                      const GU_AgentRig &rig)
{
    // Add the joint list property.
    UT_Array<exint> joint_order;
    VtTokenArray joint_paths;
    GEObuildJointList(rig, joint_paths, joint_order);

    GEO_FileProp *prop = anim_prim.addProperty(
        UsdSkelTokens->joints, SdfValueTypeNames->TokenArray,
        new GEO_FilePropConstantSource<VtTokenArray>(joint_paths));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);

    // Build transform arrays.
    GU_Agent::Matrix4ArrayConstPtr local_xforms;
    if (agent.computeLocalTransforms(local_xforms))
    {
        VtMatrix4dArray xforms =
            GEOconvertXformArray(rig, *local_xforms, joint_order);

        VtVec3fArray translates;
        VtQuatfArray rotates;
        VtVec3hArray scales;
        UT_VERIFY(
            UsdSkelDecomposeTransforms(xforms, &translates, &rotates, &scales));

        anim_prim.addProperty(
            UsdSkelTokens->translations, SdfValueTypeNames->Float3Array,
            new GEO_FilePropConstantSource<VtVec3fArray>(translates));
        anim_prim.addProperty(
            UsdSkelTokens->rotations, SdfValueTypeNames->QuatfArray,
            new GEO_FilePropConstantSource<VtQuatfArray>(rotates));
        anim_prim.addProperty(
            UsdSkelTokens->scales, SdfValueTypeNames->Half3Array,
            new GEO_FilePropConstantSource<VtVec3hArray>(scales));
    }

    // Translate the agent's channel values into blendShapes /
    // blendShapeWeights.
    GU_Agent::FloatArrayConstPtr channel_values;
    if (agent.computeChannelValues(channel_values))
    {
        VtTokenArray channel_names;
        channel_names.reserve(rig.channelCount());
        for (exint i = 0, n = rig.channelCount(); i < n; ++i)
            channel_names.push_back(TfToken(rig.channelName(i)));

        GEO_FileProp *prop = anim_prim.addProperty(
            UsdSkelTokens->blendShapes, SdfValueTypeNames->TokenArray,
            new GEO_FilePropConstantSource<VtTokenArray>(channel_names));
        prop->setValueIsDefault(true);
        prop->setValueIsUniform(true);

        VtFloatArray weights(channel_values->begin(), channel_values->end());
        anim_prim.addProperty(
            UsdSkelTokens->blendShapeWeights, SdfValueTypeNames->FloatArray,
            new GEO_FilePropConstantSource<VtFloatArray>(weights));
    }
}

static void
initInbetweenShapes(
    GEO_FilePrim &primary_prim, const GU_Detail &base_shape_gdp,
    const UT_ArrayMap<GA_Index, exint> &primary_shape_pts,
    const GU_AgentShapeLib &shapelib, const UT_StringArray &inbetween_names,
    const GU_AgentBlendShapeUtils::FloatArray &inbetween_weights)
{
    if (inbetween_names.isEmpty())
        return;

    VtVec3fArray offsets;
    UT_WorkBuffer inbetween_prop_name;
    for (exint i = 0, n = inbetween_names.size(); i < n; ++i)
    {
        const UT_StringHolder &shape_name = inbetween_names[i];

        const GU_AgentShapeLib::ShapePtr shape = shapelib.findShape(shape_name);
        // Building the input cache should have failed if the shape name is
        // invalid.
        UT_ASSERT(shape);

        const GU_Detail &shape_gdp = *shape->shapeGeometry(shapelib).gdp();
        GA_ROHandleID id_attrib =
            shape_gdp.findIntTuple(GA_ATTRIB_POINT, GA_Names::id, 1);

        // USD requires the in-between shape to have the same number of points
        // (and order) as the primary shape. GU_Agent blendshapes are more
        // flexible, so we just fill in the position offsets for the matching
        // points.
        offsets.assign(primary_shape_pts.size(), GfVec3f(0, 0, 0));

        for (GA_Offset ptoff : shape_gdp.getPointRange())
        {
            const GA_Index src_idx = id_attrib.isValid() ?
                                         GA_Index(id_attrib.get(ptoff)) :
                                         shape_gdp.pointIndex(ptoff);

            auto it = primary_shape_pts.find(src_idx);
            if (it == primary_shape_pts.end())
                continue;

            const exint primary_pt_idx = it->second;
            UT_ASSERT(primary_pt_idx >= 0 && primary_pt_idx < offsets.size());

            // USD stores precomputed position offsets from the base shape.
            UT_Vector3 pos_offset(0, 0, 0);
            if (src_idx >= 0 && src_idx < base_shape_gdp.getNumPoints())
            {
                const GA_Offset src_ptoff = base_shape_gdp.pointOffset(src_idx);
                pos_offset = shape_gdp.getPos3(ptoff) -
                             base_shape_gdp.getPos3(src_ptoff);
            }
            else
            {
                UT_ASSERT_MSG(false, "Invalid id value");
            }

            offsets[primary_pt_idx] = GusdUT_Gf::Cast(pos_offset);
        }

        // Add the property for the inbetween shape's offsets.
        UT_String usd_shape_name(shape_name);
        HUSDmakeValidUsdName(usd_shape_name, false);
        inbetween_prop_name.format("inbetweens:{0}", usd_shape_name);

        GEO_FileProp *prop = primary_prim.addProperty(
            TfToken(inbetween_prop_name.buffer()),
            SdfValueTypeNames->Vector3fArray,
            new GEO_FilePropConstantSource<VtVec3fArray>(offsets));
        prop->setValueIsDefault(true);
        prop->setValueIsUniform(true);
        prop->addMetadata(UsdSkelTokens->weight, VtValue(inbetween_weights[i]));
    }
}

/// Translate blendshapes from the agent shape library.
static void
initBlendShapes(GEO_FilePrimMap &fileprimmap, GEO_FilePrim &fileprim,
                const GT_Primitive &base_prim,
                const GEO_AgentShapeInfo &shape_info)
{
    if (!shape_info)
        return;

    const GU_AgentShapeLib &shapelib = *shape_info.myDefinition->shapeLibrary();
    const GU_AgentRig &rig = *shape_info.myDefinition->rig();
    const GU_AgentShapeLib::Shape &shape =
        *shapelib.findShape(shape_info.myShapeName);

    GU_DetailHandleAutoReadLock shape_gdl(shape.shapeGeometry(shapelib));
    const GU_Detail &base_shape_gdp = *shape_gdl.getGdp();

    // Check if this shape has any blendshapes.
    GU_AgentBlendShapeUtils::InputCache input_cache;
    if (!input_cache.reset(base_shape_gdp, rig, shapelib))
        return;

    // The base shape may have been split into multiple primitives during
    // refinement, so we need to know which points from the blendshape inputs
    // are needed (and their new indices, for sparse blendshapes).
    UT_ArrayMap<GA_Index, exint> base_shape_pts;
    {
        GT_Owner owner;
        GT_DataArrayHandle P = base_prim.findAttribute(GA_Names::P, owner, 0);
        UT_ASSERT(P);
        if (!P)
            return;

        GT_DataArrayHandle indices_h =
            GT_Util::getPointIndex(base_prim, base_shape_gdp, P->entries());
        UT_ASSERT(indices_h);
        if (!indices_h)
            return;

        GT_DataArrayHandle buffer;
        const int64 *indices = indices_h->getI64Array(buffer);
        base_shape_pts.reserve(indices_h->entries());
        for (exint i = 0, n = indices_h->entries(); i < n; ++i)
        {
            GA_Index index = static_cast<GA_Index>(indices[i]);
            if (index >= 0 && index < base_shape_gdp.getNumPoints())
                base_shape_pts[index] = i;
        }
    }

    VtTokenArray channel_names;
    SdfPathVector target_paths;
    channel_names.reserve(input_cache.numInputs());
    target_paths.reserve(input_cache.numInputs());

    VtVec3fArray offsets;
    VtIntArray indices;
    UT_ArrayMap<GA_Index, exint> primary_shape_pts;
    UT_StringArray inbetween_names;
    GU_AgentBlendShapeUtils::FloatArray inbetween_weights;
    for (exint i = 0, n = input_cache.numInputs(); i <n;++i)
    {
        // Record the channel name and blendshape prim path.
        channel_names.push_back(
            TfToken(rig.channelName(input_cache.inputChannelIndex(i))));

        UT_String usd_shape_name(input_cache.primaryShapeName(i));
        HUSDmakeValidUsdName(usd_shape_name, false);
        SdfPath target_path =
            fileprim.getPath().AppendChild(TfToken(usd_shape_name));
        target_paths.push_back(target_path);

        // Set up the BlendShape prim for the primary target shape.
        GEO_FilePrim &target_prim = fileprimmap[target_path];
        target_prim.setPath(target_path);
        target_prim.setTypeName(GEO_FilePrimTypeTokens->BlendShape);
        target_prim.setIsDefined(true);
        target_prim.setInitialized();

        const GU_AgentShapeLib::ShapePtr primary_shape =
            shapelib.findShape(input_cache.primaryShapeName(i));
        // Building the input cache should have failed if the shape name is
        // invalid.
        UT_ASSERT(primary_shape);

        const GU_Detail &primary_shape_gdp =
            *primary_shape->shapeGeometry(shapelib).gdp();
        GA_ROHandleID id_attrib =
            primary_shape_gdp.findIntTuple(GA_ATTRIB_POINT, GA_Names::id, 1);

        offsets.clear();
        offsets.reserve(primary_shape_gdp.getNumPoints());

        indices.clear();
        indices.reserve(primary_shape_gdp.getNumPoints());

        primary_shape_pts.clear();
        for (GA_Offset ptoff : primary_shape_gdp.getPointRange())
        {
            const GA_Index src_idx = id_attrib.isValid() ?
                                         GA_Index(id_attrib.get(ptoff)) :
                                         primary_shape_gdp.pointIndex(ptoff);

            // Check if this point is in the base shape's USD prim (the shape
            // may have been split into multiple prims during refinement), and
            // record its new index for the pointIndices array.
            auto it = base_shape_pts.find(src_idx);
            if (it == base_shape_pts.end())
                continue;

            // For in-between shapes, record the points used by the primary
            // shape, and their ordering.
            primary_shape_pts[src_idx] = indices.size();

            indices.push_back(it->second);

            // USD stores precomputed position offsets from the base shape.
            UT_Vector3 pos_offset(0, 0, 0);
            if (src_idx >= 0 && src_idx < base_shape_gdp.getNumPoints())
            {
                const GA_Offset src_ptoff = base_shape_gdp.pointOffset(src_idx);
                pos_offset = primary_shape_gdp.getPos3(ptoff) -
                             base_shape_gdp.getPos3(src_ptoff);
            }
            else
            {
                UT_ASSERT_MSG(false, "Invalid id value");
            }

            offsets.push_back(GusdUT_Gf::Cast(pos_offset));
        }

        GEO_FileProp *prop = target_prim.addProperty(
            UsdSkelTokens->offsets, SdfValueTypeNames->Vector3fArray,
            new GEO_FilePropConstantSource<VtVec3fArray>(offsets));
        prop->setValueIsDefault(true);
        prop->setValueIsUniform(true);

        if (id_attrib.isValid())
        {
            prop = target_prim.addProperty(
                UsdSkelTokens->pointIndices, SdfValueTypeNames->IntArray,
                new GEO_FilePropConstantSource<VtIntArray>(indices));
            prop->setValueIsDefault(true);
            prop->setValueIsUniform(true);
        }

        // Author the properties describing the in-between shapes.
        input_cache.getInBetweenShapes(i, inbetween_names, inbetween_weights);
        initInbetweenShapes(target_prim, base_shape_gdp, primary_shape_pts,
                            shapelib, inbetween_names, inbetween_weights);
    }

    // Set up the skel:blendShapeTargets and skel:blendShapes attributes on the
    // base mesh.
    fileprim.addRelationship(UsdSkelTokens->skelBlendShapeTargets,
                             target_paths);

    GEO_FileProp *prop = fileprim.addProperty(
        UsdSkelTokens->skelBlendShapes, SdfValueTypeNames->TokenArray,
        new GEO_FilePropConstantSource<VtTokenArray>(channel_names));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);
}

/// Set up any additional properties for an agent shape, such as skel:joints
/// for deforming shapes.
static void
initAgentShapePrim(GEO_FilePrimMap &fileprimmap,
                   const GU_AgentShapeLib &shapelib,
                   const GU_AgentShapeLib::Shape &shape,
                   const SdfPath &shapelib_path, const GU_AgentRig &rig,
                   const UT_Array<exint> &joint_order,
                   const VtTokenArray &joint_paths,
                   const UT_Map<exint, TfToken> &usd_shape_names)
{
    UT_ASSERT(usd_shape_names.contains(shape.uniqueId()));
    const TfToken usd_shape_name =
        usd_shape_names.find(shape.uniqueId())->second;
    SdfPath shape_path = shapelib_path.AppendChild(usd_shape_name);
    GEO_FilePrim &shape_prim = fileprimmap[shape_path];

    // Check if this shape has capture weights.
    GU_ConstDetailHandle gdh = shape.shapeGeometry(shapelib);
    GU_DetailHandleAutoReadLock gdl(gdh);
    const GU_Detail &gdp = *gdl.getGdp();

    GA_ROAttributeRef pcapt;
    GEO_AttributeCapturePath attr_capt_path;
    UT_Array<UT_Matrix4F> xforms;
    int max_pt_regions = 0;
    if (!GU_LinearSkinDeformerSourceWeights::getCaptureParms(
            gdp, pcapt, attr_capt_path, xforms, max_pt_regions))
    {
        return;
    }

    // While the indices and weights from the boneCapture attribute can be
    // easily translated into the jointIndices / jointWeights properties during
    // the normal process of converting Houdini attributes, we need knowledge
    // of the hierarchy / skeleton to set up the skel:joints property (which is
    // needed since the capture weights may use a different ordering and/or a
    // subset of the skeleton's joints). We can set skel:joints on the root
    // prim of the shape, since it's the same for the entire shape's geometry.
    const int num_regions = attr_capt_path.getNumPaths();
    VtTokenArray referenced_joints;
    for (int i = 0; i < num_regions; ++i)
    {
        // We need to build a list of the USD joint names that the indices from
        // the capture weights correspond to.
        // This requires first translating to the index in the agent's rig, and
        // then to the USD joint order.
        const exint xform_idx = rig.findTransform(attr_capt_path.getPath(i));

        if (xform_idx >= 0)
        {
            const exint usd_joint_idx = joint_order[xform_idx];
            referenced_joints.push_back(joint_paths[usd_joint_idx]);
        }
        else
            referenced_joints.push_back(TfToken());
    }

    GEO_FileProp *prop = shape_prim.addProperty(
        UsdSkelTokens->skelJoints, SdfValueTypeNames->TokenArray,
        new GEO_FilePropConstantSource<VtTokenArray>(referenced_joints));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);
}

static bool
requiresRigidSkinning(const GU_AgentLayer::ShapeBinding &binding)
{
    if (!binding.isAttachedToTransform())
        return false;

    const GU_AgentShapeDeformerConstPtr &deformer = binding.deformer();
    if (!deformer)
    {
        // Static shape attached to a joint.
        return true;
    }

    // Just check for a blendshape-only deformer that is attached to a joint
    // (no extra work is needed when skinning is present).
    // Other custom deformers won't be supported by USD anyways.
    auto blendshape_deformer =
        dynamic_cast<const GU_AgentBlendShapeDeformer *>(deformer.get());
    return blendshape_deformer && !blendshape_deformer->postBlendDeformer();
}

/// A layer is translated into a SkelRoot enclosing one or more skeleton
/// instances, and the instances of the shapes from the layer's shape bindings.
static void
createLayerPrims(const GEO_FilePrim &defn_root, GEO_FilePrimMap &fileprimmap,
                 const GEO_ImportOptions &options, const GU_AgentLayer &layer,
                 const SdfPath &layer_root_path,
                 const UT_Array<exint> &joint_order,
                 const UT_Array<GEO_AgentSkeleton> &skeletons,
                 const UT_Map<exint, exint> &shape_to_skeleton,
                 const UT_Map<exint, TfToken> &usd_shape_names)
{
    UT_String usd_layer_name(layer.name());
    HUSDmakeValidUsdName(usd_layer_name, false);
    const SdfPath layer_path =
        layer_root_path.AppendChild(TfToken(usd_layer_name));

    GEO_FilePrim &layer_prim = fileprimmap[layer_path];
    layer_prim.setTypeName(GEO_FilePrimTypeTokens->SkelRoot);
    layer_prim.setInitialized();

    UT_ArraySet<exint> known_skeletons;
    for (const GU_AgentLayer::ShapeBinding &binding : layer)
    {
        // FIXME - a layer can reference the same shape multiple times, so we
        // need to ensure the prim names are unique. There could also be name
        // conflicts with the skeleton prim(s).

        // Ensure that there is an instance of the shape's skeleton under the
        // SkelRoot.
        UT_ASSERT(shape_to_skeleton.contains(binding.shapeId()));
        const exint skeleton_id =
            shape_to_skeleton.find(binding.shapeId())->second;

        if (!known_skeletons.contains(skeleton_id))
        {
            known_skeletons.insert(skeleton_id);

            const GEO_AgentSkeleton &skel = skeletons[skeleton_id];
            const SdfPath skel_path = layer_path.AppendChild(skel.myName);

            GEO_FilePrim &skel_instance = fileprimmap[skel_path];
            skel_instance.setPath(skel_path);
            skel_instance.setIsDefined(false);
            skel_instance.setInitialized();

            // Explicitly set the skeleton instance as invisible, so that only
            // the layer's geometry is visible when an agent creates an
            // instance of the layer.
            initVisibilityAttrib(skel_instance, false, options,
                                 /* force */ true, /* force_static */ true);

            SdfPath skel_ref_path =
                defn_root.getPath().AppendChild(skel.myName);
            initInternalReference(skel_instance, skel_ref_path);
        }

        // Add an instance of the shape.
        UT_ASSERT(usd_shape_names.contains(binding.shapeId()));
        const TfToken usd_shape_name =
            usd_shape_names.find(binding.shapeId())->second;

        const SdfPath shape_instance_path =
            layer_path.AppendChild(usd_shape_name);
        GEO_FilePrim &shape_instance = fileprimmap[shape_instance_path];
        shape_instance.setPath(shape_instance_path);
        shape_instance.setIsDefined(false);
        shape_instance.setInitialized();

        SdfPath shape_ref_path =
            defn_root.getPath()
                .AppendChild(GEO_AgentPrimTokens->shapelibrary)
                .AppendChild(usd_shape_name);
        initInternalReference(shape_instance, shape_ref_path);

        // Reference the skeleton that this shape needs.
        const GEO_AgentSkeleton &skel = skeletons[skeleton_id];
        const SdfPath skel_path = layer_path.AppendChild(skel.myName);
        shape_instance.addRelationship(UsdSkelTokens->skelSkeleton,
                                       SdfPathVector({skel_path}));

        // Set up a shape binding that is attached to a joint - for GU_Agent,
        // this just applies the joint transform to the entire shape. For USD,
        // this is done with constant joint influences (see see the Rigid
        // Deformations section in the UsdSkel docs) and an identity bind pose.
        //
        // If a shape with the linear skinning deformer is attached to a joint,
        // we don't need to do anything extra when translating to USD.
        //
        // This needs to be done when defining the layers, since it's possible
        // (although not very useful) to have a static shape binding where the
        // geometry already has capture weights.
        if (requiresRigidSkinning(binding))
        {
            VtIntArray joint_indices;
            joint_indices.push_back(joint_order[binding.transformId()]);

            VtFloatArray joint_weights = {1.0};

            // We really want an identity bind transform, but to avoid an extra
            // Skeleton prim (per-mesh bind poses arne't supported) just set
            // the geomBindTransform property to cancel out the skeleton's bind
            // pose for the joint this shape is attached to. The skinning
            // applies the inverse transform.
            UT_Matrix4D geom_bind_xform;
            geom_bind_xform = skel.myBindPose[binding.transformId()];

            initJointInfluenceAttribs(shape_instance, joint_indices,
                                      joint_weights, 1, UsdGeomTokens->constant,
                                      geom_bind_xform);
        }
    }
}

void
GEOinitGTPrim(GEO_FilePrim &fileprim,
	GEO_FilePrimMap &fileprimmap,
	const GT_PrimitiveHandle &gtprim,
	const UT_Matrix4D &prim_xform,
        const GA_DataId &topology_id,
	const std::string &file_path,
        const GEO_AgentShapeInfo &agent_shape_info,
	const GEO_ImportOptions &options)
{
    GEO_HandleOtherPrims other_prim_handling = options.myOtherPrimHandling;

    // Allow overriding the define vs over choice with an attribute (assumed to
    // be constant over the piece)
    {
        static constexpr UT_StringLit override_handling_attrib(
            "usdconfigotherprims");
        TfToken override_handling =
            GEOgetTokenFromAttrib(*gtprim, override_handling_attrib.asRef());

        if (!override_handling.IsEmpty())
            GEOconvertTokenToEnum(override_handling, other_prim_handling);
    }

    if (other_prim_handling == GEO_OTHER_XFORM)
    {
        GEOinitXformOver(fileprim, gtprim, prim_xform, options);
        return;
    }

    bool defined = (other_prim_handling == GEO_OTHER_DEFINE);

    // Copy the processed attribute list because we modify it as we
    // import attributes from the geometry.
    UT_ArrayStringSet processed_attribs(options.myProcessedAttribs);

    // Don't author extents for prims produced from agent shapes. If there is
    // skinning, the rest shape's bounding box can be very wrong.
    if (agent_shape_info)
        processed_attribs.insert(theBoundsName.asHolder());

    if (gtprim->getPrimitiveType() == GT_PRIM_POLYGON_MESH ||
	gtprim->getPrimitiveType() == GT_PRIM_SUBDIVISION_MESH)
    {
	const GT_PrimPolygonMesh	*gtmesh = nullptr;

	gtmesh = UTverify_cast<const GT_PrimPolygonMesh *>(gtprim.get());
	if (gtmesh)
	{
	    GT_DataArrayHandle	 hou_attr;
	    GT_DataArrayHandle	 vertex_indirect;
	    GEO_FileProp	*prop = nullptr;

	    fileprim.setTypeName(GEO_FilePrimTypeTokens->Mesh);

	    if (options.myTopologyHandling != GEO_USD_TOPOLOGY_NONE)
	    {
		hou_attr = gtmesh->getFaceCounts();
		prop = initProperty<int>(fileprim,
		    hou_attr, UT_String::getEmptyString(), GT_OWNER_INVALID,
		    false, options,
		    UsdGeomTokens->faceVertexCounts,
		    SdfValueTypeNames->IntArray,
		    false, &topology_id,
		    GT_DataArrayHandle(), false);
		prop->setValueIsDefault(
		    options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);

		hou_attr = gtmesh->getVertexList();
		if (options.myReversePolygons)
		{
		    GT_Size entries = hou_attr->entries();
		    GT_Int32Array *indirect = new GT_Int32Array(entries, 1);
		    for (GT_Size i = 0; i < entries; i++)
			indirect->set(i, i);
		    GEOreverseWindingOrder(indirect, gtmesh->getFaceCounts());
		    vertex_indirect = indirect;
		    hou_attr = new GT_DAIndirect(vertex_indirect, hou_attr);
		}
		prop = initProperty<int>(fileprim,
		    hou_attr, UT_String::getEmptyString(), GT_OWNER_INVALID,
		    false, options,
		    UsdGeomTokens->faceVertexIndices,
		    SdfValueTypeNames->IntArray,
		    false, &topology_id,
		    vertex_indirect, false);
		prop->setValueIsDefault(
		    options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);

		prop = fileprim.addProperty(UsdGeomTokens->orientation,
		    SdfValueTypeNames->Token,
		    new GEO_FilePropConstantSource<TfToken>(
			options.myReversePolygons
			    ? UsdGeomTokens->rightHanded
			    : UsdGeomTokens->leftHanded));
		prop->setValueIsDefault(true);
		prop->setValueIsUniform(true);

                TfToken subd_scheme = UsdGeomTokens->none;
                if (gtprim->getPrimitiveType() == GT_PRIM_SUBDIVISION_MESH)
		{
		    const GT_PrimSubdivisionMesh	*gtsubdmesh = nullptr;

		    gtsubdmesh = UTverify_cast<const GT_PrimSubdivisionMesh *>(
			gtprim.get());
		    if (gtsubdmesh->scheme() == GT_CATMULL_CLARK)
			subd_scheme = UsdGeomTokens->catmullClark;
		    else if (gtsubdmesh->scheme() == GT_LOOP)
			subd_scheme = UsdGeomTokens->loop;
		    else if (gtsubdmesh->scheme() == GT_BILINEAR)
			subd_scheme = UsdGeomTokens->bilinear;

                    initSubdAttribs(fileprim, gtsubdmesh, processed_attribs,
                                    options, vertex_indirect);
                }
                // Used during refinement when deciding whether to create the
                // GT_PrimSubdivisionMesh.
                processed_attribs.insert("osd_scheme"_sh);

		prop = fileprim.addProperty(UsdGeomTokens->subdivisionScheme,
		    SdfValueTypeNames->Token,
		    new GEO_FilePropConstantSource<TfToken>(
			subd_scheme));
		prop->setValueIsDefault(true);
		prop->setValueIsUniform(true);
	    }
	    else if (options.myReversePolygons)
	    {
		// If we have been asked not to create topology information,
		// but we have been asked to reverse polygons, we need to
		// create the vertex index remapping attribute.
		hou_attr = gtmesh->getVertexList();
		GT_Size entries = hou_attr->entries();
		GT_Int32Array *indirect = new GT_Int32Array(entries, 1);
		for (GT_Size i = 0; i < entries; i++)
		    indirect->set(i, i);
		GEOreverseWindingOrder(indirect, gtmesh->getFaceCounts());
		vertex_indirect = indirect;
	    }

	    static GT_Owner owners[] = {
		GT_OWNER_VERTEX, GT_OWNER_POINT, GT_OWNER_UNIFORM,
		GT_OWNER_DETAIL, GT_OWNER_INVALID
	    };
	    initCommonAttribs(fileprim, gtprim,
		processed_attribs, options,
		false, vertex_indirect);
	    initExtentAttrib(fileprim, gtprim, processed_attribs, options);
	    initVisibilityAttrib(fileprim, *gtprim, options);
	    initExtraAttribs(fileprim, fileprimmap,
		gtprim, owners,
		processed_attribs, options,
		false, vertex_indirect);
	    initSubsets(fileprim, fileprimmap,
		gtmesh->faceSetMap(), options);
	    initXformAttrib(fileprim, prim_xform, options);
	    initKind(fileprim, options.myKindSchema, GEO_KINDGUIDE_LEAF);

            initBlendShapes(fileprimmap, fileprim, *gtprim, agent_shape_info);
	}
    }
    else if (gtprim->getPrimitiveType() == GT_PRIM_POINT_MESH ||
	     gtprim->getPrimitiveType() == GT_PRIM_PARTICLE)
    {
        fileprim.setTypeName(GEO_FilePrimTypeTokens->Points);

        // Allow authoring a different prim type based on an attribute. The
        // attribute value is assumed to be constant for this point mesh, since
        // a path attribute should be used to split up points into multiple USD
        // prims.
        static constexpr UT_StringLit thePrimTypeAttrib("usdprimtype");
        const TfToken primtype =
            GEOgetTokenFromAttrib(*gtprim, thePrimTypeAttrib.asRef());
        if (!primtype.IsEmpty())
            fileprim.setTypeName(primtype);

        // Similarly, allow authoring kind using a point attribute.
        static constexpr UT_StringLit theKindAttrib("usdkind");
        TfToken kind = GEOgetTokenFromAttrib(*gtprim, theKindAttrib.asRef());
        if (!kind.IsEmpty() && KindRegistry::GetInstance().HasKind(kind))
            fileprim.replaceMetadata(SdfFieldKeys->Kind, VtValue(kind));

        // Only author the common attributes like points, velocities, etc for
        // prim types that support them.
        const bool is_point_based = UsdSchemaRegistry::GetAttributeDefinition(
            fileprim.getTypeName(), UsdGeomTokens->points);
        if (is_point_based)
        {
            initCommonAttribs(fileprim, gtprim, processed_attribs, options,
                              false);
        }

        // Unless we're authoring a point-based primitive, use constant
        // interpolation for the primvars (the default behaviour would be
        // vertex since the source is a point attribute).
        const bool force_constant_interpolation = !is_point_based;
        initColorAttribs(fileprim, gtprim, processed_attribs, options, false,
                         nullptr, force_constant_interpolation);

        // Set up properties if a points prim is being created.
        if (fileprim.getTypeName() == GEO_FilePrimTypeTokens->Points)
        {
            initPointSizeAttribs(fileprim, gtprim, processed_attribs, options,
                                 false);
            initPointIdsAttrib(fileprim, gtprim, processed_attribs, options,
                               false);
            initExtentAttrib(fileprim, gtprim, processed_attribs, options);
            initXformAttrib(fileprim, prim_xform, options);

            if (kind.IsEmpty())
                initKind(fileprim, options.myKindSchema, GEO_KINDGUIDE_LEAF);
        }
        else if (UsdSchemaRegistry::GetAttributeDefinition(
                     fileprim.getTypeName(), UsdGeomTokens->xformOpOrder))
        {
            // Author a transform from the standard point instancing
            // attributes.
            initXformAttrib(fileprim,
                            GEOcomputeStandardPointXform(*gtprim, options,
                                                         processed_attribs),
                            options);
        }

        static GT_Owner owners[] = {GT_OWNER_VERTEX, GT_OWNER_POINT,
                                    GT_OWNER_UNIFORM, GT_OWNER_DETAIL,
                                    GT_OWNER_INVALID};
        initExtraAttribs(fileprim, fileprimmap, gtprim, owners,
                         processed_attribs, options, false, nullptr,
                         force_constant_interpolation);
        initVisibilityAttrib(fileprim, *gtprim, options);
    }
    else if (gtprim->getPrimitiveType() == GT_PRIM_CURVE_MESH ||
	     gtprim->getPrimitiveType() == GT_PRIM_SUBDIVISION_CURVES)
    {
	const GT_PrimCurveMesh		*gtcurves = nullptr;

	gtcurves = UTverify_cast<const GT_PrimCurveMesh *>(gtprim.get());
	if (gtcurves)
	{
            const int order = gtcurves->uniformOrder();
            const GT_Basis basis = gtcurves->getBasis();

            // The BasisCurves prim only supports linear and cubic curves.
            // The NurbsCurves prim is more general, but doesn't currently have
            // imaging support.
#ifdef ENABLE_NURBS_CURVES
	    if (basis == GT_BASIS_BSPLINE || (order == 2 || order == 4))
#else
	    if (order == 2 || order == 4)
#endif
	    {
		if (options.myTopologyHandling != GEO_USD_TOPOLOGY_NONE)
		{
                    GT_DataArrayHandle curve_counts = gtcurves->getCurveCounts();
                    GEO_FileProp *prop = nullptr;

#ifdef ENABLE_NURBS_CURVES
                    if (basis == GT_BASIS_BSPLINE)
                    {
                        fileprim.setTypeName(
                            GEO_FilePrimTypeTokens->NurbsCurves);

                        VtIntArray orders;
                        orders.resize(gtcurves->getCurveCount());

                        VtArray<GfVec2d> ranges;
                        ranges.resize(gtcurves->getCurveCount());

                        const GT_DataArrayHandle knots = gtcurves->knots();
                        UT_ASSERT(knots);

                        for (GT_Size i = 0, n = gtcurves->getCurveCount();
                             i < n; ++i)
                        {
                            orders[i] = gtcurves->getOrder(i);

                            GT_Offset knot_start = gtcurves->knotOffset(i);
                            GT_Offset knot_end = knot_start +
                                                 gtcurves->getVertexCount(i) +
                                                 gtcurves->getOrder(i) - 1;
                            ranges[i] = GfVec2d(knots->getF64(knot_start),
                                                knots->getF64(knot_end));
                        }

                        prop = fileprim.addProperty(
                            UsdGeomTokens->order, SdfValueTypeNames->IntArray,
                            new GEO_FilePropConstantSource<VtIntArray>(orders));
                        prop->setValueIsDefault(true);
                        prop->setValueIsUniform(true);

                        prop = fileprim.addProperty(
                            UsdGeomTokens->ranges,
                            SdfValueTypeNames->Double2Array,
                            new GEO_FilePropConstantSource<VtArray<GfVec2d>>(
                                ranges));
                        prop->setValueIsDefault(true);
                        prop->setValueIsUniform(true);

                        prop = initProperty<double>(
                            fileprim, knots, UT_StringHolder::theEmptyString,
                            GT_OWNER_INVALID, false, options,
                            UsdGeomTokens->knots,
                            SdfValueTypeNames->DoubleArray, false,
                            &options.myTopologyId, GT_DataArrayHandle());
                        prop->setValueIsDefault(true);
                        prop->setValueIsUniform(true);
                    }
                    else
#endif
                    {
                        fileprim.setTypeName(
                            GEO_FilePrimTypeTokens->BasisCurves);

                        prop = fileprim.addProperty(UsdGeomTokens->type,
                            SdfValueTypeNames->Token,
                            new GEO_FilePropConstantSource<TfToken>(
                                order == 2 ? UsdGeomTokens->linear
                                    : UsdGeomTokens->cubic));
                        prop->setValueIsDefault(true);
                        prop->setValueIsUniform(true);
                        prop = fileprim.addProperty(UsdGeomTokens->basis,
                            SdfValueTypeNames->Token,
                            new GEO_FilePropConstantSource<TfToken>(
                                GEOgetBasisToken(basis)));
                        prop->setValueIsDefault(true);
                        prop->setValueIsUniform(true);

                        const bool wrap = gtcurves->getWrap();
                        prop = fileprim.addProperty(UsdGeomTokens->wrap,
                            SdfValueTypeNames->Token,
                            new GEO_FilePropConstantSource<TfToken>(
                                wrap ? UsdGeomTokens->periodic
                                    : UsdGeomTokens->nonperiodic));
                        prop->setValueIsDefault(true);
                        prop->setValueIsUniform(true);

                        // Houdini repeats the first point for closed beziers.
                        // USD does not expect this, so we need to remove the
                        // extra point.
                        if (order == 4 && wrap)
                        {
                            auto modcounts =
                                new GT_Real32Array(curve_counts->entries(), 1);

                            for (GT_Size i = 0, n = curve_counts->entries();
                                 i < n; ++i)
                            {
                                modcounts->set(
                                    curve_counts->getValue<fpreal32>(i) - 4, i);
                            }
                            curve_counts = modcounts;
                        }
                    }

		    prop = initProperty<int>(fileprim,
			curve_counts, UT_String::getEmptyString(),
                        GT_OWNER_INVALID, false, options,
			UsdGeomTokens->curveVertexCounts,
			SdfValueTypeNames->IntArray,
			false, &topology_id,
			GT_DataArrayHandle(), false);
		    prop->setValueIsDefault(
			options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);
		}

		initCommonAttribs(fileprim, gtprim,
		    processed_attribs, options, true);
		initPointSizeAttribs(fileprim, gtprim,
		    processed_attribs, options, true);
		static GT_Owner owners[] = {
		    GT_OWNER_VERTEX, GT_OWNER_UNIFORM,
		    GT_OWNER_DETAIL, GT_OWNER_INVALID
		};
		initExtentAttrib(fileprim, gtprim, processed_attribs, options);
                initVisibilityAttrib(fileprim, *gtprim, options);
		initExtraAttribs(fileprim, fileprimmap,
		    gtprim, owners,
		    processed_attribs, options, true);
		initSubsets(fileprim, fileprimmap,
		    gtcurves->faceSetMap(), options);
		initXformAttrib(fileprim, prim_xform, options);
		initKind(fileprim, options.myKindSchema, GEO_KINDGUIDE_LEAF);
	    }
	}
    }
    else if (gtprim->getPrimitiveType() ==
             GT_PrimPackedInstance::getStaticPrimitiveType())
    {
        auto inst = UTverify_cast<const GT_PrimPackedInstance *>(gtprim.get());

        fileprim.setTypeName(GEO_FilePrimTypeTokens->Xform);

        if (inst->isPrototype())
        {
            // The parent prim for the prototypes should be invisible.
            GEO_FilePrim &prototype_group =
                fileprimmap[fileprim.getPath().GetParentPath()];
            prototype_group.setTypeName(GEO_FilePrimTypeTokens->Scope);
            prototype_group.setInitialized();
            initVisibilityAttrib(prototype_group, false, options,
                                 /* force */ true, /* force_static */ true);
        }
        else
        {
            // Author the instance's visibility.
            initVisibilityAttrib(fileprim, inst->isVisible(), options);
        }

        if (!inst->getPrototypePath().IsEmpty())
        {
            // Set up an instance of the prototype prim.
            initInternalReference(fileprim, inst->getPrototypePath());
            fileprim.addMetadata(SdfFieldKeys->Instanceable, VtValue(true));
        }
        else
        {
            // Set up a payload for the file path.
            auto diskimpl =
                dynamic_cast<const GU_PackedDisk *>(inst->getPackedImpl());
            if (diskimpl)
            {
                initPayload(fileprim, diskimpl->filename().toStdString());
                fileprim.addMetadata(SdfFieldKeys->Instanceable, VtValue(true));
                initExtentAttrib(fileprim, gtprim, processed_attribs, options);
            }
        }

        initXformAttrib(fileprim, prim_xform, options);
        initKind(fileprim, options.myKindSchema, GEO_KINDGUIDE_BRANCH);

        static constexpr GT_Owner owners[] = {GT_OWNER_DETAIL,
                                              GT_OWNER_INVALID};
        GEOfilterPackedPrimAttribs(processed_attribs);
        initColorAttribs(fileprim, gtprim, processed_attribs, options, false);
        initExtraAttribs(fileprim, fileprimmap, gtprim, owners,
                         processed_attribs, options, false);
    }
    else if (gtprim->getPrimitiveType() == GT_PRIM_SPHERE ||
             gtprim->getPrimitiveType() == GT_PRIM_TUBE)
    {
        if (gtprim->getPrimitiveType() == GT_PRIM_SPHERE)
        {
            fileprim.setTypeName(GEO_FilePrimTypeTokens->Sphere);
            initXformAttrib(fileprim, prim_xform, options);
        }
        else
        {
            auto tube = UTverify_cast<const GT_PrimTube *>(gtprim.get());
            if (GEOisCone(*tube))
                fileprim.setTypeName(GEO_FilePrimTypeTokens->Cone);
            else
            {
                UT_ASSERT(GEOisCylinder(*tube));
                fileprim.setTypeName(GEO_FilePrimTypeTokens->Cylinder);
            }

            // GT tubes are flipped, and the direction must be correct for
            // cone prims.
            UT_Matrix4D tube_xform = prim_xform;
            tube_xform.prerotateHalf<UT_Axis3::XAXIS>();
            initXformAttrib(fileprim, tube_xform, options);

            // The default cylinder / cone height is 2, but Houdini's tubes
            // have a height of 1.
            GEO_FileProp *prop = fileprim.addProperty(
                UsdGeomTokens->height, SdfValueTypeNames->Double,
                new GEO_FilePropConstantSource<double>(1.0));
            prop->setValueIsDefault(true);

            // GT tubes are always aligned along Z.
            prop = fileprim.addProperty(
                UsdGeomTokens->axis, SdfValueTypeNames->Token,
                new GEO_FilePropConstantSource<TfToken>(UsdGeomTokens->z));
            prop->setValueIsDefault(true);
            prop->setValueIsUniform(true);
        }

        // Houdini's spheres / tubes have a radius of 1, and then are scaled by
        // the prim transform.
        GEO_FileProp *prop = fileprim.addProperty(
            UsdGeomTokens->radius, SdfValueTypeNames->Double,
            new GEO_FilePropConstantSource<double>(1.0));
        prop->setValueIsDefault(true);

        initExtentAttrib(fileprim, gtprim, processed_attribs, options);
        initVisibilityAttrib(fileprim, *gtprim, options);
        initKind(fileprim, options.myKindSchema, GEO_KINDGUIDE_BRANCH);

        static constexpr GT_Owner owners[] = {
            GT_OWNER_DETAIL, GT_OWNER_INVALID
        };
        initCommonAttribs(fileprim, gtprim, processed_attribs, options, false);
        initExtraAttribs(fileprim, fileprimmap, gtprim, owners,
                         processed_attribs, options, false);
    }
    else if (gtprim->getPrimitiveType() == GT_PRIM_VOXEL_VOLUME ||
	     gtprim->getPrimitiveType() == GT_PRIM_VDB_VOLUME)
    {
	const GEO_Primitive	*geoprim = nullptr;
	GT_DataArrayHandle	 namehandle;
	GT_Owner		 nameowner;

	if (gtprim->getPrimitiveType() == GT_PRIM_VOXEL_VOLUME)
	{
	    GT_PrimVolume *gtvolume=static_cast<GT_PrimVolume *>(gtprim.get());
	    geoprim = gtvolume->getGeoPrimitive();
	    fileprim.setTypeName(GEO_FilePrimTypeTokens->HoudiniFieldAsset);
	}
	else
	{
	    GT_PrimVDB *gtvolume=static_cast<GT_PrimVDB *>(gtprim.get());
	    geoprim = gtvolume->getGeoPrimitive();
	    fileprim.setTypeName(GEO_FilePrimTypeTokens->OpenVDBAsset);
	}

	initXformAttrib(fileprim, prim_xform, options);
	fileprim.addProperty(UsdVolTokens->filePath,
	    SdfValueTypeNames->Asset,
	    new GEO_FilePropConstantSource<SdfAssetPath>(
		SdfAssetPath(file_path)));
	// Find the name attribute, and set it as the field name.
	namehandle = gtprim->findAttribute(GA_Names::name, nameowner, 0);
	if (namehandle && namehandle->getStorage() == GT_STORE_STRING)
	    fileprim.addProperty(UsdVolTokens->fieldName,
		SdfValueTypeNames->Token,
		new GEO_FilePropConstantSource<TfToken>(
		    TfToken(namehandle->getS(0))));
	// Houdini Native Volumes have a field index to fall back to if the
	// name attribute isn't set.
	if (gtprim->getPrimitiveType() == GT_PRIM_VOXEL_VOLUME)
	    fileprim.addProperty(UsdVolTokens->fieldIndex,
		SdfValueTypeNames->Int,
		new GEO_FilePropConstantSource<int>(
		(int)geoprim->getMapIndex()));
        // Always set extents for volume prims.
        initExtentAttrib(fileprim, gtprim, processed_attribs, options,
                         /*force*/ true);
	initKind(fileprim, options.myKindSchema, GEO_KINDGUIDE_BRANCH);
        initVisibilityAttrib(fileprim, *gtprim, options);

        static constexpr GT_Owner owners[] = {
            GT_OWNER_UNIFORM, GT_OWNER_INVALID
        };
        initExtraAttribs(fileprim, fileprimmap, gtprim, owners,
                         processed_attribs, options, false);

        // If the volume save path was specified, record as custom data.
        UT_StringHolder save_path =
            GEOgetStringFromAttrib(*gtprim, theVolumeSavePathName.asRef());
        if (save_path)
        {
            // We record it as a String attribute rather than an Asset Path
            // because we don't want USD resolving the path for us. Relative
            // paths should remain relative.
            fileprim.addProperty(HUSDgetSavePathToken(),
                SdfValueTypeNames->String,
                new GEO_FilePropConstantSource<std::string>(
                    save_path.toStdString()));
        }
    }
    else if (gtprim->getPrimitiveType() ==
             GT_PrimVolumeCollection::getStaticPrimitiveType())
    {
        auto collection =
            UTverify_cast<const GT_PrimVolumeCollection *>(gtprim.get());
        fileprim.setTypeName(GEO_FilePrimTypeTokens->Volume);

        // For a volume prim, just set up the relationships with the field
        // prims.
        UT_WorkBuffer field_prop;
        for (const SdfPath &field : collection->getFields())
        {
            field_prop = UsdVolTokens->field.GetString();
            field_prop.append(':');
            field_prop.append(field.GetName());
            fileprim.addRelationship(TfToken(field_prop.buffer()),
                                     SdfPathVector({field}));
        }
    }
    else if (gtprim->getPrimitiveType() ==
	     GusdGT_PackedUSD::getStaticPrimitiveType())
    {
	defined = false;
	initXformAttrib(fileprim, prim_xform, options);
    }
    else if (gtprim->getPrimitiveType() ==
             GT_PrimAgentDefinition::getStaticPrimitiveType())
    {
        auto defn_prim =
            UTverify_cast<const GT_PrimAgentDefinition *>(gtprim.get());
        const GU_AgentDefinition &defn = defn_prim->getDefinition();
        UT_ASSERT(defn.rig());
        UT_ASSERT(defn.shapeLibrary());
        const GU_AgentRig &rig = *defn.rig();

        GEO_FilePrim &definitions_group =
            fileprimmap[fileprim.getPath().GetParentPath()];
        definitions_group.setTypeName(GEO_FilePrimTypeTokens->Scope);
        definitions_group.setInitialized();
        initVisibilityAttrib(definitions_group, false, options,
                             /* force */ true, /* force_static */ true);

        fileprim.setTypeName(GEO_FilePrimTypeTokens->Scope);
        // Build the skeleton's joint list, which expresses the hierarchy
        // through the joint names and must be ordered so that parents appear
        // before children (unlike GU_AgentRig).
        UT_Array<exint> joint_order;
        VtTokenArray joint_paths;
        GEObuildJointList(rig, joint_paths, joint_order);

        UT_Map<exint, TfToken> usd_shape_names;
        GEObuildUsdShapeNames(*defn.shapeLibrary(), usd_shape_names);

        // Figure out how many Skeleton prims we need to create.
        UT_Array<GEO_AgentSkeleton> skeletons;
        UT_Map<exint, exint> shape_to_skeleton;
        GEObuildUsdSkeletons(defn, *defn_prim->getFallbackBindPose(), skeletons,
                             shape_to_skeleton);

        for (const GEO_AgentSkeleton &skeleton : skeletons)
        {
            initSkeletonPrim(fileprim, fileprimmap, options, rig, skeleton,
                             joint_paths, joint_order);
        }

        // During refinement the shape library geometry was also refined
        // through GT, so here we just need to set up any additional
        // agent-specific properties on the shape prims.
        SdfPath shapelib_path =
            fileprim.getPath().AppendChild(GEO_AgentPrimTokens->shapelibrary);
        GEO_FilePrim &shapelib_prim = fileprimmap[shapelib_path];
        shapelib_prim.setTypeName(GEO_FilePrimTypeTokens->Scope);
        shapelib_prim.setInitialized();

        const GU_AgentShapeLib &shapelib = *defn.shapeLibrary();
        {
            // The GU_AgentShapeLib iterator is unordered, so sort by shape
            // name to produce nicer diffs when new shapes are added.
            UT_StringArray shape_names;
            shape_names.setCapacity(shapelib.entries());
            for (auto &&entry : shapelib)
                shape_names.append(entry.first);

            shape_names.sort();
            for (const UT_StringHolder &shape_name : shape_names)
            {
                initAgentShapePrim(fileprimmap, shapelib,
                                   *shapelib.findShape(shape_name),
                                   shapelib_path, rig, joint_order, joint_paths,
                                   usd_shape_names);
            }
        }

        // For each layer, create a SkelRoot prim enclosing the shape instances
        // and instances of the skeletons required by those shapes. Each agent
        // can then bind their unique animation to an instance of the
        // appropriate SkelRoot.
        const SdfPath layer_root_path =
            fileprim.getPath().AppendChild(GEO_AgentPrimTokens->layers);
        GEO_FilePrim &layer_root_prim = fileprimmap[layer_root_path];
        layer_root_prim.setTypeName(GEO_FilePrimTypeTokens->Scope);
        layer_root_prim.setInitialized();

        for (const GU_AgentLayerConstPtr &layer : defn.layers())
        {
            createLayerPrims(fileprim, fileprimmap, options, *layer,
                             layer_root_path, joint_order, skeletons,
                             shape_to_skeleton, usd_shape_names);
        }
    }
    else if (gtprim->getPrimitiveType() ==
             GT_PrimAgentInstance::getStaticPrimitiveType())
    {
        auto agent_instance =
            UTverify_cast<const GT_PrimAgentInstance *>(gtprim.get());

        const GU_Agent &agent = agent_instance->getAgent();
        UT_ASSERT(agent.getRig());
        const GU_AgentRig &rig = *agent.getRig();

        // Create a prim for the agent, to enclose the animation and the
        // instanced bind state.
        fileprim.setTypeName(GEO_FilePrimTypeTokens->Xform);
        initXformAttrib(fileprim, prim_xform, options);
        initKind(fileprim, options.myKindSchema, GEO_KINDGUIDE_LEAF);

        static GT_Owner owners[] = {GT_OWNER_DETAIL, GT_OWNER_INVALID};
        GEOfilterPackedPrimAttribs(processed_attribs);
        initColorAttribs(fileprim, gtprim, processed_attribs, options, false);
        initExtraAttribs(fileprim, fileprimmap, gtprim, owners,
	    processed_attribs, options, false);

        // Instance the agent's bind state - the agent definition prim
        // hierarchy contains a SkelRoot prim for each layer.
        //
        // TODO - if an agent doesn't have a current layer, we should create an
        // instance of its skeleton.
        const GU_AgentLayer *layer = agent.getCurrentLayer();
        if (layer)
        {
            const SdfPath layer_instance_path =
                fileprim.getPath().AppendChild(GEO_AgentPrimTokens->geometry);

            GEO_FilePrim &layer_instance = fileprimmap[layer_instance_path];
            layer_instance.setPath(layer_instance_path);
            layer_instance.setIsDefined(false);
            layer_instance.setInitialized();

            UT_String usd_layer_name(layer->name());
            HUSDmakeValidUsdName(usd_layer_name, false);

            SdfPath layer_ref_path =
                agent_instance->getDefinitionPath()
                    .AppendChild(GEO_AgentPrimTokens->layers)
                    .AppendChild(TfToken(usd_layer_name));
            initInternalReference(layer_instance, layer_ref_path);

            // Author the agent's bounding box on the SkelRoot prim.
            initExtentAttrib(layer_instance, gtprim, processed_attribs,
                             options);
        }

        // Add a SkelAnimation primitive for the agent's pose.
        SdfPath anim_path =
            fileprim.getPath().AppendChild(GEO_AgentPrimTokens->animation);
        fileprim.addRelationship(UsdSkelTokens->skelAnimationSource,
                                 SdfPathVector({anim_path}));

        GEO_FilePrim &anim_prim = fileprimmap[anim_path];
        anim_prim.setTypeName(GEO_FilePrimTypeTokens->SkelAnimation);
        anim_prim.setPath(anim_path);
        anim_prim.setIsDefined(true);
        anim_prim.setInitialized();
        initSkelAnimationPrim(anim_prim, agent, rig);
    }
    else if (gtprim->getPrimitiveType() ==
             GT_PrimPointInstancer::getStaticPrimitiveType())
    {
        auto instancer =
            UTverify_cast<const GT_PrimPointInstancer *>(gtprim.get());

        fileprim.setTypeName(GEO_FilePrimTypeTokens->PointInstancer);

        GEO_FileProp *protoIndices = fileprim.addProperty(
            UsdGeomTokens->protoIndices,
            SdfValueTypeNames->IntArray,
            new GEO_FilePropConstantArraySource<int>(
                instancer->getProtoIndices()));
        protoIndices->setValueIsDefault(
            options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);

        fileprim.addRelationship(UsdGeomTokens->prototypes,
                                 instancer->getPrototypePaths());

        // Set up the instance transforms.
        VtVec3fArray positions, scales;
        VtQuathArray orientations;
        GEOdecomposeTransforms(instancer->getInstanceXforms(), positions,
                               orientations, scales);

        const bool xform_is_default = GEOhasStaticPackedXform(options);
        GEO_FileProp *prop = fileprim.addProperty(
            UsdGeomTokens->positions, SdfValueTypeNames->Point3fArray,
            new GEO_FilePropConstantSource<VtVec3fArray>(positions));
        prop->setValueIsDefault(xform_is_default);

        prop = fileprim.addProperty(
            UsdGeomTokens->orientations, SdfValueTypeNames->QuathArray,
            new GEO_FilePropConstantSource<VtQuathArray>(orientations));
        prop->setValueIsDefault(xform_is_default);

        prop = fileprim.addProperty(
            UsdGeomTokens->scales, SdfValueTypeNames->Float3Array,
            new GEO_FilePropConstantSource<VtVec3fArray>(scales));
        prop->setValueIsDefault(xform_is_default);

        // Author the invisibleIds attribute.
        if (theVisibilityName.asRef().multiMatch(options.myAttribs))
        {
            const UT_Array<exint> &invisible_instances =
                instancer->getInvisibleInstances();

            // If we're authoring ids, then we need to use the id of each
            // instance instead of its index.
            UT_Array<exint> invisible_ids;
            if (GA_Names::id.multiMatch(options.myAttribs))
            {
                GT_Owner owner;
                GT_DataArrayHandle id_attrib =
                    gtprim->findAttribute(GA_Names::id, owner, 0);
                if (id_attrib && owner == GT_OWNER_POINT)
                {
                    invisible_ids.setCapacity(invisible_instances.entries());
                    for (exint i : invisible_instances)
                        invisible_ids.append(id_attrib->getI64(i));
                }
            }

            prop = fileprim.addProperty(
                UsdGeomTokens->invisibleIds, SdfValueTypeNames->Int64Array,
                new GEO_FilePropConstantArraySource<exint>(
                    !invisible_ids.isEmpty() ? invisible_ids :
                                               invisible_instances));

            prop->setValueIsDefault(
                theVisibilityName.asRef().multiMatch(options.myStaticAttribs));
        }

        // Set up the standard ids, velocities, and angularVelocities
        // properties.
        initPointIdsAttrib(fileprim, gtprim, processed_attribs, options, false);
        initVelocityAttrib(fileprim, gtprim, processed_attribs, options, false);
        initAccelerationAttrib(fileprim, gtprim, processed_attribs, options,
                               false);
        initAngularVelocityAttrib(fileprim, gtprim, processed_attribs, options,
                                  false);

        static constexpr GT_Owner owners[] = {
            GT_OWNER_POINT, GT_OWNER_DETAIL, GT_OWNER_INVALID
        };
        GEOfilterPackedPrimAttribs(processed_attribs);
        initExtraAttribs(fileprim, fileprimmap, gtprim, owners,
                         processed_attribs, options, false);
        initXformAttrib(fileprim, prim_xform, options);
    }

    fileprim.setIsDefined(defined);
    fileprim.setInitialized();
}

bool
GEOisGTPrimSupported(const GT_PrimitiveHandle &gtprim)
{
    auto	 gttype = gtprim->getPrimitiveType();

    if (gttype == GT_PRIM_TUBE)
    {
        auto tube = UTverify_cast<const GT_PrimTube *>(gtprim.get());
        return GEOisCylinder(*tube) || GEOisCone(*tube);
    }
    else
    {
	if (gttype == GT_PRIM_POLYGON_MESH ||
	    gttype == GT_PRIM_SUBDIVISION_MESH ||
	    gttype == GT_PRIM_CURVE_MESH ||
	    gttype == GT_PRIM_SUBDIVISION_CURVES ||
	    gttype == GT_PRIM_POINT_MESH ||
	    gttype == GT_PRIM_PARTICLE ||
	    gttype == GT_PRIM_SPHERE ||
	    gttype == GT_PRIM_VOXEL_VOLUME ||
	    gttype == GT_PRIM_VDB_VOLUME ||
	    gttype == GusdGT_PackedUSD::getStaticPrimitiveType() ||
            gttype == GT_PrimAgentDefinition::getStaticPrimitiveType() ||
            gttype == GT_PrimAgentInstance::getStaticPrimitiveType() ||
            gttype == GT_PrimVolumeCollection::getStaticPrimitiveType() ||
            gttype == GT_PrimPointInstancer::getStaticPrimitiveType() ||
            gttype == GT_PrimPackedInstance::getStaticPrimitiveType())
	    return true;
    }

    return false;
}

bool
GEOisCylinder(const GT_PrimTube &tube)
{
    // USD cylinders have end caps and no tapering.
    return tube.getCaps() && SYSisEqual(tube.getTaper(), 1.0);
}

bool
GEOisCone(const GT_PrimTube &tube)
{
    // Cones are equivalent to being fully tapered in the positive direction.
    return tube.getCaps() && SYSequalZero(tube.getTaper());
}

bool
GEOshouldRefineToSubdMesh(const int gttype)
{
    // When refining metaballs or tubes (some tubes can't be converted to a USD
    // cylinder or cone), mark the resulting meshes as subdivision surfaces.
    return gttype == GT_PRIM_TUBE || gttype == GT_PRIM_METAEXPR;
}

PXR_NAMESPACE_CLOSE_SCOPE
