/*
 * Copyright 2020 Side Effects Software Inc.
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

#include "GEO_HAPIUtils.h"
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DANumeric.h>
#include <UT/UT_Map.h>
#include <UT/UT_Quaternion.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Gf.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usdSkel/tokens.h>
#include <pxr/usd/usdSkel/topology.h>
#include <pxr/usd/usdSkel/utils.h>
#include <pxr/usd/usdUtils/pipeline.h>
#include <pxr/usd/usdVol/tokens.h>

bool
GEOhapiExtractString(const HAPI_Session &session,
                     HAPI_StringHandle &handle,
                     UT_WorkBuffer &buf)
{
    int retSize;
    ENSURE_SUCCESS(
        HAPI_GetStringBufLength(&session, handle, &retSize), session);

    char *str = buf.lock(0, retSize);

    ENSURE_SUCCESS(HAPI_GetString(&session, handle, str, retSize), session);
    buf.release();

    return true;
}

// USD functions
PXR_NAMESPACE_OPEN_SCOPE

const TfToken &
GEOhapiCurveOwnerToInterpToken(HAPI_AttributeOwner owner)
{
    static UT_Map<HAPI_AttributeOwner, TfToken> theOwnerToInterpMap = {
        {HAPI_ATTROWNER_VERTEX, UsdGeomTokens->vertex},
        {HAPI_ATTROWNER_PRIM, UsdGeomTokens->uniform},
        {HAPI_ATTROWNER_DETAIL, UsdGeomTokens->constant}};

    return theOwnerToInterpMap[owner];
}

const TfToken &
GEOhapiMeshOwnerToInterpToken(HAPI_AttributeOwner owner)
{
    static UT_Map<HAPI_AttributeOwner, TfToken> theOwnerToInterpMap = {
        {HAPI_ATTROWNER_POINT, UsdGeomTokens->vertex},
        {HAPI_ATTROWNER_VERTEX, UsdGeomTokens->faceVarying},
        {HAPI_ATTROWNER_PRIM, UsdGeomTokens->uniform},
        {HAPI_ATTROWNER_DETAIL, UsdGeomTokens->constant}};

    return theOwnerToInterpMap[owner];
}

const TfToken &
GEOhapiCurveTypeToBasisToken(HAPI_CurveType type)
{
    // Linear curves return a blank token
    static UT_Map<HAPI_CurveType, TfToken> theBasisMap = {
        {HAPI_CURVETYPE_BEZIER, UsdGeomTokens->bezier},
        {HAPI_CURVETYPE_NURBS, UsdGeomTokens->bspline}};

    return theBasisMap[type];
}

void
GEOhapiInitXformAttrib(GEO_FilePrim &fileprim,
                       const UT_Matrix4D &prim_xform,
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

void
GEOhapiReversePolygons(GT_DataArrayHandle &vertArrOut,
                       const GT_DataArrayHandle &faceCounts,
                       const GT_DataArrayHandle &vertices)
{
    GT_Int32Array *indirectVertices = new GT_Int32Array(vertices->entries(), 1);
    vertArrOut.reset(indirectVertices);
    for (int i = 0; i < vertices->entries(); i++)
    {
        indirectVertices->set(i, i);
    }

    int32 *data = indirectVertices->data();

    exint base = 0;
    for (exint f = 0; f < faceCounts->entries(); f++)
    {
        exint numVerts = faceCounts->getI32(f);
        for (exint p = 1; p < (numVerts + 1) / 2; p++)
        {
            std::swap(data[base + p], data[base + numVerts - p]);
        }
        base += numVerts;
    }
}

SdfPath
GEOhapiGetPrimPath(HAPI_PartType type, const SdfPath &parentPath, GEO_HAPIPrimCounts &counts)
{
    std::string suffix;
    exint suffixNum;

    switch (type)
    {
	case HAPI_PARTTYPE_BOX:
	    suffix = "box_";
	    suffixNum = counts.boxes++;
	    break;

	case HAPI_PARTTYPE_CURVE:
	    suffix = "curve_";
	    suffixNum = counts.curves++;
	    break;

	case HAPI_PARTTYPE_INSTANCER:
	    suffix = "obj_";
	    suffixNum = counts.instances++;
	    break;

	case HAPI_PARTTYPE_MESH:
	    suffix = "mesh_";
	    suffixNum = counts.meshes++;
	    break;

	case HAPI_PARTTYPE_SPHERE:
	    suffix = "sphere_";
	    suffixNum = counts.spheres++;
	    break;

	case HAPI_PARTTYPE_VOLUME:
	    suffix = "volume_";
	    suffixNum = counts.volumes++;
	    break;

	default:
	    suffix = "geo_";
	    suffixNum = counts.others++;
    }

    suffix += std::to_string(suffixNum);

    return parentPath.AppendChild(TfToken(suffix));
}

PXR_NAMESPACE_CLOSE_SCOPE