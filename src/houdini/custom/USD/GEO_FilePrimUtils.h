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
#ifndef __GEO_FILE_PRIM_UTILS_H__
#define __GEO_FILE_PRIM_UTILS_H__

#include "GEO_FileUtils.h"
#include "GEO_FilePrim.h"
#include <GT/GT_Primitive.h>
#include <GA/GA_Types.h>
#include <UT/UT_ArrayStringSet.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_String.h>
#include <UT/UT_StringMMPattern.h>
#include "pxr/pxr.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/usd/sdf/path.h"
#include <set>

class GEO_Detail;
class GT_PrimCurveMesh;
class GT_PrimTube;
class UT_StringMMPattern;

PXR_NAMESPACE_OPEN_SCOPE

class TfToken;
class SdfPath;
struct GEO_AgentShapeInfo;
class GEO_FilePrim;

/// Match an attribute name against a pattern, along with special tokens for
/// matching by attribute type (e.g. 'type:string').
bool GEOmatchAttribPattern(
        const UT_StringMMPattern &pattern,
        const UT_StringRef &attr_name,
        const UT_StringRef &decoded_attr_name,
        const GT_Storage storage,
        const GT_Type type_info);

/// Overload which uses the storage and type from a GT attribute.
inline bool
GEOmatchAttribPattern(
        const UT_StringMMPattern &pattern,
        const UT_StringRef &attr_name,
        const UT_StringRef &decoded_attr_name,
        const GT_DataArray &gt_attrib)
{
    return GEOmatchAttribPattern(
            pattern, attr_name, decoded_attr_name, gt_attrib.getStorage(),
            gt_attrib.getTypeInfo());
}

class GEO_ImportOptions
{
public:
    /// Returns whether the attribute should be imported as a primvar / custom
    /// attribute. This can match against the decoded attrib name as well as
    /// special patterns like 'type:string'. Note that only the primary
    /// 'myAttribs' pattern uses the type to determine whether the attrib should
    /// be imported at all (otherwise, the default 'type:string' pattern for
    /// indexed attribs would unexpectedly always force all string attribs to
    /// import). The other patterns require using the attrib's name to cause an
    /// attrib to be imported.
    bool shouldImportAttrib(
            const UT_StringRef &attr_name,
            const UT_StringRef &decoded_attr_name,
            GT_Storage storage,
            GT_Type type_info) const
    {
        return GEOmatchAttribPattern(
                       myAttribs, attr_name, decoded_attr_name, storage,
                       type_info)
               || GEOmatchAttribPattern(
                       myIndexAttribs, attr_name, decoded_attr_name,
                       GT_STORE_INVALID, GT_TYPE_NONE)
               || GEOmatchAttribPattern(
                       myConstantAttribs, attr_name, decoded_attr_name,
                       GT_STORE_INVALID, GT_TYPE_NONE)
               || GEOmatchAttribPattern(
                       myScalarConstantAttribs, attr_name, decoded_attr_name,
                       GT_STORE_INVALID, GT_TYPE_NONE)
               || GEOmatchAttribPattern(
                       myCustomAttribs, attr_name, decoded_attr_name,
                       GT_STORE_INVALID, GT_TYPE_NONE);
    }

    /// Overload which uses the storage and type from a GT attribute.
    inline bool shouldImportAttrib(
            const UT_StringRef &attr_name,
            const UT_StringRef &decoded_attr_name,
            const GT_DataArray &gt_attr) const
    {
        return shouldImportAttrib(
                attr_name, decoded_attr_name, gt_attr.getStorage(),
                gt_attr.getTypeInfo());
    }

    /// Overload which uses the storage and type from a GT attribute and
    /// doesn't require a decoded attribute name.
    inline bool shouldImportAttrib(
            const UT_StringRef &attr_name,
            const GT_DataArray &gt_attr) const
    {
        return shouldImportAttrib(
                attr_name,
                /*decoded_attr_name=*/UT_StringHolder::theEmptyString, gt_attr);
    }

    UT_StringArray		 myPathAttrNames;
    SdfPath			 myPrefixPath;
    bool			 myPrefixAbsolutePaths = false;
    UT_StringHolder		 myImportGroup;
    UT_StringHolder		 myImportGroupType;
    UT_StringHolder		 mySubdGroup;
    UT_StringMMPattern		 myAttribs;
    UT_StringMMPattern		 myIndexAttribs;
    UT_StringMMPattern		 myConstantAttribs;
    UT_StringMMPattern		 myScalarConstantAttribs;
    UT_StringMMPattern		 myBoolAttribs;
    UT_StringMMPattern		 myUIntAttribs;
    UT_StringMMPattern		 myUInt64Attribs;
    UT_StringMMPattern		 myAssetPathAttribs;
    UT_StringMMPattern		 myStaticAttribs;
    UT_StringMMPattern		 myPartitionAttribs;
    bool			 myPrefixPartitionSubsetNames = true;
    UT_StringMMPattern		 mySubsetGroups;
    UT_StringMMPattern		 myCustomAttribs;
    UT_ArrayStringSet		 myProcessedAttribs;
    GEO_TopologyHandling	 myTopologyHandling =
                                        GEO_USD_TOPOLOGY_ANIMATED;
    GEO_HandleUsdPackedPrims	 myUsdHandling =
                                        GEO_USD_PACKED_XFORM_ATTRIBS;
    GEO_HandlePackedPrims	 myPackedPrimHandling =
                                        GEO_PACKED_NATIVEINSTANCES;
    GEO_HandleAgents	         myAgentHandling = GEO_AGENT_INSTANCED_SKELROOTS;
    GEO_HandleNurbsCurves	 myNurbsCurveHandling =
                                        GEO_NURBS_BASISCURVES;
    GEO_HandleNurbsSurfs	 myNurbsSurfHandling = GEO_NURBSSURF_MESHES;
    GEO_KindSchema		 myKindSchema =
                                        GEO_KINDSCHEMA_COMPONENT;
    GEO_HandleOtherPrims	 myOtherPrimHandling =
                                        GEO_OTHER_DEFINE;
    bool			 myPolygonsAsSubd = false;
    bool			 myReversePolygons = false;
    bool                         myDefineOnlyLeafPrims = false;
    bool                         myTranslateUVToST = true;
    GEO_HandleCaptureWeights     myCaptureWeightsHandling = GEO_CAPTWEIGHTS_USDSKEL;
    bool                         mySetDefaultPrim = true;
    bool                         myHeightfieldConvert = false;
    bool                         myUseXformCommonAPI = false;
    float                        myDefaultWidth = -1.f;
};

void
GEOinitInternalReference(GEO_FilePrim &fileprim,
			 const SdfPath &reference_path,
                         bool instanceable = false);

void
GEOinitRootPrim(GEO_FilePrim &fileprim,
	const TfToken &default_prim_name,
        bool save_sample_range,
        const std::set<double> &time_samples);

void
GEOinitXformPrim(GEO_FilePrim &fileprim,
	GEO_HandleOtherPrims other_handling);

void
GEOinitXformOver(GEO_FilePrim &fileprim,
	const GT_PrimitiveHandle &gtprim,
	const UT_Matrix4D &prim_xform,
        const GEO_ImportOptions &options);

/// Sets the USD prim's xform.
/// 'author_identity' controls whether an xformOp is authored for identity
/// transforms.
void GEOinitXformAttrib(GEO_FilePrim &fileprim,
                        const UT_Matrix4D &prim_xform,
                        const GEO_ImportOptions &options,
                        bool author_identity = true,
                        const UT_Vector3D &pivot = UT_Vector3D(0, 0, 0));

/// Sets the USD prim's purpose.
void
GEOinitPurposeAttrib(GEO_FilePrim &fileprim, const TfToken &purpose_type);

/// Controls whether GEOinitProperty authors a primvar (appending the primvars:
/// prefix to the attribute name) or a custom attribute.
/// The 'auto' mode will author an attribute if it matches the primitive's
/// schema, and will create a primvar otherwise.
/// When disabled, the provided attribute name is used as-is.
enum class GEO_CreatePrimvar
{
    Disabled,
    Auto,
    Enabled
};

template <class GtT, class GtComponentT = GtT>
GEO_FileProp *GEOinitProperty(GEO_FilePrim &fileprim,
                              const GT_DataArrayHandle &hou_attr,
                              const UT_StringRef &attr_name,
                              const UT_StringRef &decoded_attr_name,
                              GT_Owner attr_owner,
                              bool prim_is_curve,
                              const GEO_ImportOptions &options,
                              TfToken usd_attr_name,
                              SdfValueTypeName usd_attr_type,
                              GEO_CreatePrimvar create_primvar,
                              bool create_indices_attr,
                              const int64 *override_data_id,
                              const GT_DataArrayHandle &vertex_indirect,
                              bool override_is_constant,
                              bool override_is_array = false);

/// Translate an attribute with array-valued entries.
template <typename GtT, class GtComponentT = GtT>
GEO_FileProp *GEOinitArrayAttrib(
        GEO_FilePrim &fileprim,
        GT_DataArrayHandle hou_attr,
        const UT_StringRef &attr_name,
        const UT_StringRef &decoded_attr_name,
        GT_Owner attr_owner,
        bool prim_is_curve,
        const GEO_ImportOptions &options,
        const TfToken &usd_attr_name,
        const SdfValueTypeName &usd_attr_type,
        GEO_CreatePrimvar create_primvar,
        const GT_DataArrayHandle &vertex_indirect,
        bool override_is_constant);

bool
GEOhasStaticPackedXform(const GEO_ImportOptions &options);

using GEO_VolumeFileMap = UT_Map<const GEO_Detail *, SdfAssetPath>;
void
GEOinitGTPrim(GEO_FilePrim &fileprim,
        UT_Array<GEO_FilePrim> &extra_prims,
	const GT_PrimitiveHandle &gtprim,
	const UT_Matrix4D &prim_xform,
        const TfToken &purpose,
        const GA_DataId &topology_id,
	const GEO_VolumeFileMap &volume_path_map,
        const SdfFileFormat::FileFormatArguments &file_format_args,
        const UT_IntrusivePtr<GEO_AgentShapeInfo> &agent_shape_info,
	const GEO_ImportOptions &options);

bool
GEOisGTPrimSupported(const GT_PrimitiveHandle &gtprim);

/// Returns true if the tube can be converted into a USD cylinder primitive.
bool
GEOisCylinder(const GT_PrimTube &tube);

/// Returns true if the tube can be converted into a USD cone primitive.
bool
GEOisCone(const GT_PrimTube &tube);

/// Returns true if, when the primitive is refined, any resulting mesh
/// primitives should be marked as subdivision surfaces.
bool
GEOshouldRefineToSubdMesh(int gttype);

/// Applies a scale to the width values. This can be used for e.g. converting
/// pscale from a radius to diameter.
GT_DataArrayHandle
GEOscaleWidthsAttrib(const GT_DataArrayHandle &width_attr, const fpreal scale);

/// Converts a float attribute from radians to degrees. This can be used for
/// creating the angularVelocities attribute for point instancers.
GT_DataArrayHandle
GEOconvertRadToDeg(const GT_DataArrayHandle &attr);

/// When converting NURBS to B-Splines, repeat the first and last control
/// vertices of each curve so that the curve ends at those positions.
/// https://rmanwiki.pixar.com/display/REN23/Curves has some useful
/// visualizations, since USD BasisCurves prims closely match Renderman.
UT_IntrusivePtr<GT_PrimCurveMesh>
GEOfixEndInterpolation(const UT_IntrusivePtr<GT_PrimCurveMesh> &src_curves);

/// Reverses the winding order for a mesh, returning a list of indirect
/// indices that can be used with GT_DAIndirect.
GT_DataArrayHandle
GEOreverseWindingOrder(const GT_DataArrayHandle &faceCounts,
                       const GT_DataArrayHandle &vertices);

/// Simple utility method to retrieve a TfToken from a constant GT attribute (or
/// the first value if not constant).
TfToken
GEOgetTokenFromAttrib(const GT_Primitive &gtprim, const UT_StringRef &attrname);

PXR_NAMESPACE_CLOSE_SCOPE

/// Specifies how to fill in the additional entries when extending the tuple
/// size for GEOconvertTupleSize(). They can be initialized to zero or can be
/// copies of the end value.
enum class GEO_FillMethod
{
    Zero,
    Hold
};

/// Increase or decrease the tuple size, useful when converting to a standard
/// USD attribute such as 'velocities'.
GT_DataArrayHandle GEOconvertTupleSize(
    const GT_DataArrayHandle &src,
    int newSize,
    GEO_FillMethod method = GEO_FillMethod::Zero);

#endif // __GEO_FILE_PRIM_UTILS_H__
