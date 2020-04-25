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
#include "GEO_HAPIPart.h"
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DANumeric.h>
#include <HUSD/HUSD_Utils.h>
#include <UT/UT_Map.h>
#include <UT/UT_Quaternion.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Gf.h>
#include <pxr/base/tf/diagnostic.h>
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

void
GEOhapiSendCookError(const HAPI_Session &session)
{
    PXR_NAMESPACE_USING_DIRECTIVE
    UT_WorkBuffer buf;

    int len;
    HAPI_GetStatusStringBufLength(
        &session, HAPI_STATUS_COOK_RESULT, HAPI_STATUSVERBOSITY_ERRORS, &len);

    char *str = buf.lock(0, len);
    HAPI_GetStatusString(&session, HAPI_STATUS_COOK_RESULT, str, len);
    buf.release();

    TF_WARN("%s", buf.buffer());
}

void
GEOhapiSendError(const HAPI_Session &session)
{
    PXR_NAMESPACE_USING_DIRECTIVE
    UT_WorkBuffer buf;

    int len;
    HAPI_GetStatusStringBufLength(
        &session, HAPI_STATUS_CALL_RESULT, HAPI_STATUSVERBOSITY_ERRORS, &len);

    char *str = buf.lock(0, len);
    HAPI_GetStatusString(&session, HAPI_STATUS_CALL_RESULT, str, len);
    buf.release();

    TF_WARN("%s", buf.buffer());
}

GT_Type
GEOhapiAttribType(HAPI_AttributeTypeInfo typeinfo)
{
    // Unfortunately, HAPI_AttributeTypeInfo doesn't quite match GT_Type so we
    // can't directly cast between them.
    switch (typeinfo)
    {
    case HAPI_ATTRIBUTE_TYPE_POINT:
        return GT_TYPE_POINT;
    case HAPI_ATTRIBUTE_TYPE_HPOINT:
        return GT_TYPE_HPOINT;
    case HAPI_ATTRIBUTE_TYPE_VECTOR:
        return GT_TYPE_VECTOR;
    case HAPI_ATTRIBUTE_TYPE_NORMAL:
        return GT_TYPE_NORMAL;
    case HAPI_ATTRIBUTE_TYPE_COLOR:
        return GT_TYPE_COLOR;
    case HAPI_ATTRIBUTE_TYPE_QUATERNION:
        return GT_TYPE_QUATERNION;
    case HAPI_ATTRIBUTE_TYPE_MATRIX3:
        return GT_TYPE_MATRIX3;
    case HAPI_ATTRIBUTE_TYPE_MATRIX:
        return GT_TYPE_MATRIX;
    case HAPI_ATTRIBUTE_TYPE_ST:
        return GT_TYPE_ST;
    case HAPI_ATTRIBUTE_TYPE_HIDDEN:
        return GT_TYPE_HIDDEN;
    case HAPI_ATTRIBUTE_TYPE_BOX2:
        return GT_TYPE_BOX2;
    case HAPI_ATTRIBUTE_TYPE_BOX:
        return GT_TYPE_BOX;
    case HAPI_ATTRIBUTE_TYPE_TEXTURE:
        return GT_TYPE_TEXTURE;
    default:
        return GT_TYPE_NONE;
    }
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
GEOhapiNameToNewPath(const UT_StringHolder &name, const SdfPath &parentPath)
{
    UT_String out = name.c_str();
    HUSDmakeValidUsdPath(out, false);

    if (name[0] == '/')
    {
        // An absolute path was specified
        return SdfPath(out.toStdString());
    }
    else
    {
        // A relative path was specified, so prepend the parent path
        return parentPath.AppendPath(SdfPath(out.toStdString()));
    }
}

SdfPath
GEOhapiGetPrimPath(const GEO_HAPIPart &part,
                   const SdfPath &parentPath,
                   GEO_HAPIPrimCounts &counts,
                   const GEO_ImportOptions &options)
{
    const UT_StringMap<GEO_HAPIAttributeHandle> &attrs = part.getAttribMap();

    // First check if the path was specified by a path name attribute
    for (exint i = 0, n = options.myPathAttrNames.entries(); i < n; i++)
    {
        const UT_StringHolder &nameAttr = options.myPathAttrNames[i];
        if (attrs.contains(nameAttr))
        {
            const UT_StringHolder &name = attrs.at(nameAttr)->myData->getS(0);
            if (!name.isEmpty())
            {
                return GEOhapiNameToNewPath(name, parentPath);
            }
        }
    }

    std::string suffix;
    exint suffixNum;

    switch (part.getType())
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
