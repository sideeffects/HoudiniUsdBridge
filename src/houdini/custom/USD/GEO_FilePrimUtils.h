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
#include "pxr/usd/sdf/path.h"

class GT_PrimCurveMesh;
class GT_PrimTube;
class UT_StringMMPattern;

PXR_NAMESPACE_OPEN_SCOPE

class TfToken;
class SdfPath;
struct GEO_AgentShapeInfo;
class GEO_FilePrim;

class GEO_ImportOptions
{
public:
    bool			 multiMatch(const UT_String &str) const
				 {
				     return str.multiMatch(myAttribs) ||
					str.multiMatch(myIndexAttribs) ||
					str.multiMatch(myConstantAttribs) ||
                                        str.multiMatch(myCustomAttribs);
				 }
    bool			 multiMatch(const UT_StringRef &str) const
				 {
				     UT_String strwrap(str.c_str());
				     return multiMatch(strwrap);
				 }

    UT_StringArray		 myPathAttrNames;
    SdfPath			 myPrefixPath;
    UT_StringHolder		 myImportGroup;
    UT_StringHolder		 mySubdGroup;
    UT_StringMMPattern		 myAttribs;
    UT_StringMMPattern		 myIndexAttribs;
    UT_StringMMPattern		 myConstantAttribs;
    UT_StringMMPattern		 myStaticAttribs;
    UT_StringMMPattern		 myPartitionAttribs;
    UT_StringMMPattern		 mySubsetGroups;
    UT_StringMMPattern		 myCustomAttribs;
    UT_ArrayStringSet		 myProcessedAttribs;
    GEO_TopologyHandling	 myTopologyHandling = GEO_USD_TOPOLOGY_ANIMATED;
    GEO_HandleUsdPackedPrims	 myUsdHandling = GEO_USD_PACKED_XFORM;
    GEO_HandlePackedPrims	 myPackedPrimHandling = GEO_PACKED_NATIVEINSTANCES;
    GEO_HandleNurbsCurves	 myNurbsCurveHandling = GEO_NURBS_BASISCURVES;
    GEO_KindSchema		 myKindSchema = GEO_KINDSCHEMA_COMPONENT;
    GEO_HandleOtherPrims	 myOtherPrimHandling = GEO_OTHER_DEFINE;
    bool			 myPolygonsAsSubd = false;
    bool			 myReversePolygons = false;
    bool                         myDefineOnlyLeafPrims = false;
    bool                         myTranslateUVToST = true;
};

void 
GEOinitInternalReference(GEO_FilePrim &fileprim,
			 const SdfPath &reference_path);

void
GEOsetKind(GEO_FilePrim &prim,
	GEO_KindSchema kindschema,
	GEO_KindGuide kindguide);

void
GEOinitRootPrim(GEO_FilePrim &fileprim,
	const TfToken &default_prim_name,
        bool save_sample_frame,
        fpreal sample_frame);

void
GEOinitXformPrim(GEO_FilePrim &fileprim,
	GEO_HandleOtherPrims other_handling,
	GEO_KindSchema kindschema);

void
GEOinitXformOver(GEO_FilePrim &fileprim,
	const GT_PrimitiveHandle &gtprim,
	const UT_Matrix4D &prim_xform,
        const GEO_ImportOptions &options);

void
GEOinitXformAttrib(GEO_FilePrim &fileprim,
                   const UT_Matrix4D &prim_xform,
                   const GEO_ImportOptions &options);

bool 
GEOhasStaticPackedXform(const GEO_ImportOptions &options);

void
GEOinitGTPrim(GEO_FilePrim &fileprim,
	GEO_FilePrimMap &fileprimmap,
	const GT_PrimitiveHandle &gtprim,
	const UT_Matrix4D &prim_xform,
        const GA_DataId &topology_id,
	const std::string &file_path,
        const GEO_AgentShapeInfo &agent_shape_info,
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
