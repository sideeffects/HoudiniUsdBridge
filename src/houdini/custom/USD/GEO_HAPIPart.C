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

#include "GEO_HAPIPart.h"
#include "GEO_HAPIUtils.h"
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DASubArray.h>
#include <gusd/GT_PackedUSD.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Gf.h>
#include <HUSD/XUSD_Utils.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdSkel/tokens.h>
#include <pxr/usd/usdSkel/topology.h>
#include <pxr/usd/usdSkel/utils.h>
#include <pxr/usd/usdUtils/pipeline.h>
#include <pxr/usd/usdVol/tokens.h>
#include <UT/UT_Assert.h>
#include <UT/UT_VarEncode.h>


GEO_HAPIPart::GEO_HAPIPart() : myType(HAPI_PARTTYPE_INVALID) {}

GEO_HAPIPart::~GEO_HAPIPart() {}

bool
GEO_HAPIPart::loadPartData(const HAPI_Session &session,
                           HAPI_GeoInfo &geo,
                           HAPI_PartInfo &part,
                           UT_WorkBuffer &buf)
{
    // Save general information
    myType = part.type;

    // Get and save extra information from each type
    // TODO: Save relavent info from every type
    switch (myType)
    {
	case HAPI_PARTTYPE_MESH:
	{
	    // No extra data needed
	    break;
	}

	case HAPI_PARTTYPE_CURVE:
	{

	    break;
	}

	case HAPI_PARTTYPE_VOLUME:
	{
	    break;
	}

	case HAPI_PARTTYPE_INSTANCER:
	{
	    break;
	}

	case HAPI_PARTTYPE_SPHERE:
	{
	    myData.reset(new SphereData);
	    SphereData *sData = UTverify_cast<SphereData *>(myData.get());
	    HAPI_SphereInfo sInfo;

	    ENSURE_SUCCESS(
		HAPI_GetSphereInfo(&session, geo.nodeId, part.id, &sInfo),
		session);

	    for (int i = 0; i < 3; i++)
	    {
		sData->center[i] = sInfo.center[i];
	    }

	    sData->radius = sInfo.radius;

	    break;
	}

	// Should not generate box primitives
	case HAPI_PARTTYPE_BOX:
	default:
	    CLEANUP(session);
    }

    if (!myData)
    {
        myData.reset(new PartData);
    }

    int numFaces = part.faceCount;
    int numVertices = part.vertexCount;

    if (numFaces > 0)
    {
        GT_DANumeric<int> *faceCounts = new GT_DANumeric<int>(numFaces, 1);
	myData->faceCounts = faceCounts;

        ENSURE_SUCCESS(HAPI_GetFaceCounts(
            &session, geo.nodeId, part.id, faceCounts->data(), 0, numFaces),
	    session);
    }

    if (numVertices > 0)
    {
        GT_DANumeric<int> *vertices = new GT_DANumeric<int>(numVertices, 1);

        ENSURE_SUCCESS(HAPI_GetVertexList(
            &session, geo.nodeId, part.id, vertices->data(), 0, numVertices),
	    session);

        myData->vertices = vertices;
    }

    // Find max array size so we only allocate once
    int greatestCount = *std::max_element(
        &part.attributeCounts[0], &part.attributeCounts[HAPI_ATTROWNER_MAX]);

    UT_UniquePtr<HAPI_StringHandle> sHandleUnique(
        new HAPI_StringHandle[greatestCount]);

    HAPI_StringHandle *handles = sHandleUnique.get();
    HAPI_AttributeInfo attrInfo;

    // Iterate through all owners to get all attributes
    for (int i = 0; i < HAPI_ATTROWNER_MAX; i++)
    {
        if (part.attributeCounts[i] > 0)
        {

            ENSURE_SUCCESS(HAPI_GetAttributeNames(
                &session, geo.nodeId, part.id, (HAPI_AttributeOwner)i, handles,
                part.attributeCounts[i]),
		session);

            for (int j = 0; j < part.attributeCounts[i]; j++)
            {
                CHECK_RETURN(GEOhapiExtractString(session, handles[j], buf));

                ENSURE_SUCCESS(HAPI_GetAttributeInfo(
                    &session, geo.nodeId, part.id, buf.buffer(),
                    (HAPI_AttributeOwner)i, &attrInfo),
		    session);

		UT_StringHolder attribName(buf.buffer());

		// Ignore an attribute if one with the same name is already saved
		if (!myAttribs.contains(attribName))
		{
		    exint nameIndex = myAttribNames.append(attribName);
		    GEO_HAPIAttributeHandle attrib(new GEO_HAPIAttribute);

		    CHECK_RETURN(attrib->loadAttrib(session, geo, part,
                                            (HAPI_AttributeOwner)i, attrInfo,
                                            attribName, buf));

		    // Add the loaded attribute to our string map
		    myAttribs[myAttribNames[nameIndex]].swap(attrib);

		    UT_ASSERT(!attrib.get());
		}
                
            }
        }
    }

    // Sort the names to keep the order consistent 
    // when there are small changes to the list
    myAttribNames.sort(true, false);

    return true;
}

void
GEO_HAPIPart::getBounds(UT_BoundingBoxR &bbox)
{
    bbox.makeInvalid();

    switch (myType)
    {
	case HAPI_PARTTYPE_SPHERE:
	{
	    //
	    // The sphere's radius will be set to  1 and then transformed
	    // the bounds will also be transformed, so the bounds will
	    // match a sphere at the origin with radius 1
	    //

	    bbox.setBounds(-1, -1, -1, // Min
			   1, 1, 1);   // Max

	    break;
	}

	default:
	{
	    // Add all points to the Bounding Box

	    if (myAttribs.contains(HAPI_ATTRIB_POSITION))
	    {
		GEO_HAPIAttributeHandle &points = myAttribs[HAPI_ATTRIB_POSITION];

		// Points attribute should be a float type
		if (points->myDataType != HAPI_STORAGETYPE_STRING)
		{
		    // Make sure points are 3 dimensions
		    points->convertTupleSize(3);

		    GT_DataArray *xyz = points->myData.get();

		    for (int i = 0; i < points->myCount; i++)
		    {
			bbox.enlargeBounds(xyz->getF32(i, 0), xyz->getF32(i, 1),
					   xyz->getF32(i, 2));
		    }
		}
	    }
	}
    }
}

void
GEO_HAPIPart::getXForm(UT_Matrix4D &xform)
{
    xform.identity();

    switch (myType)
    {
	case HAPI_PARTTYPE_SPHERE:
	{
	    SphereData *data = UTverify_cast<SphereData *>(myData.get());
	    float *center = data->center.data();

	    xform.scale(data->radius);
	    xform.translate(center[0], center[1], center[2]);

	    break;
	}

	default:
	{
	    // return identitiy matrix
	}
    }
}


/////////////////////////////////////////////
// HAPI to USD functions
//

PXR_NAMESPACE_USING_DIRECTIVE

static constexpr UT_StringLit theBoundsName("bounds");
static constexpr UT_StringLit theVisibilityName("visibility");
static constexpr UT_StringLit theVolumeSavePathName("usdvolumesavepath");

bool
GEO_HAPIPart::setupPrimType(GEO_FilePrim &filePrim,
                            const GEO_ImportOptions &options,
                            GT_DataArrayHandle &vertexIndirect)
{
    // Update transform
    UT_Matrix4D primXform;
    getXForm(primXform);
    GEOhapiInitXformAttrib(filePrim, primXform, options);

    GEO_HandleOtherPrims other_prim_handling = options.myOtherPrimHandling;

    if (other_prim_handling == GEO_OTHER_XFORM)
    {
        GEOhapiInitXformAttrib(filePrim, primXform, options);
        return false;
    }

    bool define = (other_prim_handling == GEO_OTHER_DEFINE);
    GEO_FileProp *prop;

    switch (myType)
    {
	case HAPI_PARTTYPE_MESH:
	{
	    PartData *meshData = myData.get();
	    GT_DataArrayHandle attribData;
	    filePrim.setTypeName(GEO_FilePrimTypeTokens->Mesh);

	    if (options.myTopologyHandling != GEO_USD_TOPOLOGY_NONE)
	    {
		attribData = meshData->faceCounts;
		prop = filePrim.addProperty(
				UsdGeomTokens->faceVertexCounts, 
				SdfValueTypeNames->IntArray, 
				new GEO_FilePropAttribSource<int>
				    (attribData));
		prop->setValueIsDefault(
		    options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);

		attribData = meshData->vertices;
		if (options.myReversePolygons)
		{
		    GEOhapiReversePolygons(vertexIndirect, 
				    meshData->faceCounts, 
				    meshData->vertices);
		    attribData = new GT_DAIndirect(vertexIndirect, attribData);
		}

		prop = filePrim.addProperty(UsdGeomTokens->faceVertexIndices, 
					    SdfValueTypeNames->IntArray, 
					    new GEO_FilePropAttribSource<int>
						(attribData));
		prop->setValueIsDefault(
		    options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);

		prop = filePrim.addProperty(UsdGeomTokens->orientation,
		    SdfValueTypeNames->Token,
		    new GEO_FilePropConstantSource<TfToken>(
			options.myReversePolygons
			    ? UsdGeomTokens->rightHanded
			    : UsdGeomTokens->leftHanded));
		prop->setValueIsDefault(true);
		prop->setValueIsUniform(true);

		TfToken subd_scheme = UsdGeomTokens->none;
                // Subdivision meshes are not extracted from HAPI

		/*
                // Used during refinement when deciding whether to create the
                // GT_PrimSubdivisionMesh.
                processedAttribs.insert("osd_scheme"_sh);
		*/

		prop = filePrim.addProperty(UsdGeomTokens->subdivisionScheme,
		    SdfValueTypeNames->Token,
		    new GEO_FilePropConstantSource<TfToken>(
			subd_scheme));
		prop->setValueIsDefault(true);
		prop->setValueIsUniform(true);
	    }
	    else if (options.myReversePolygons)
	    {
		GEOhapiReversePolygons(vertexIndirect, 
				meshData->faceCounts,
				meshData->vertices);
	    }

	    GEOhapiInitKind(filePrim, options.myKindSchema, GEO_KINDGUIDE_LEAF);

	    break;
	}

	case HAPI_PARTTYPE_SPHERE:
	{
	    filePrim.setTypeName(GEO_FilePrimTypeTokens->Sphere);

	    // Houdini's spheres have a radius of 1, and then are scaled
	    // by the prim transform.
	    prop = filePrim.addProperty(
		UsdGeomTokens->radius, SdfValueTypeNames->Double,
		new GEO_FilePropConstantSource<double>(1.0));
	    prop->setValueIsDefault(true);

	    GEOhapiInitKind(filePrim, options.myKindSchema, GEO_KINDGUIDE_BRANCH);
	    break;
	}

	default:
	    break;
    }

    return define;
}

template <class DT, class ComponentDT>
GEO_FileProp *
GEO_HAPIPart::applyAttrib(GEO_FilePrim &filePrim,
                          const GEO_HAPIAttributeHandle &attrib,
                          const TfToken &usdAttribName,
                          const SdfValueTypeName &usdTypeName,
                          UT_ArrayStringSet &processedAttribs,
                          bool createIndicesAttrib,
                          const GEO_ImportOptions &options,
                          const GT_DataArrayHandle &vertexIndirect)
{
    typedef GEO_FilePropAttribSource<DT, ComponentDT> FilePropAttribSource;
    typedef GEO_FilePropConstantArraySource<DT> FilePropConstantSource;

    GEO_FileProp *prop = nullptr;

    if (attrib->myData && !processedAttribs.contains(attrib->myName))
    {
        GT_DataArrayHandle srcAttrib = attrib->myData;
        int64 dataId = srcAttrib->getDataId();
        GEO_FilePropSource *propSource = nullptr;
        FilePropAttribSource *attribSource = nullptr;
        HAPI_AttributeOwner owner = attrib->myOwner;

        bool constantAttrib =
            attrib->myName.multiMatch(options.myConstantAttribs);
        bool defaultAttrib = attrib->myName.multiMatch(options.myStaticAttribs);

        if (constantAttrib && owner != HAPI_ATTROWNER_DETAIL)
        {
            // If the attribute is configured as "constant", just take the
            // first value from the attribute and use that as if it were a
            // detail attribute. Note we can ignore the vertex indirection in
            // this situation, since all element attribute values are the same.
            owner = HAPI_ATTROWNER_DETAIL;
            srcAttrib = new GT_DASubArray(attrib->myData, GT_Offset(0), 1);
        }
        else if (owner == HAPI_ATTROWNER_VERTEX && vertexIndirect)
        {
            // If this is a vertex attribute, and we are changing the
            // handedness or the geometry, and so have a vertex indirection
            // array, create the reversed attribute array here.
            srcAttrib = new GT_DAIndirect(vertexIndirect, srcAttrib);
        }

        // Create a FilePropSource for the Houdini attribute. This may be added
        // directly to the FilePrim as a property, or be used as a way to get
        // at the raw elements in a type-safe way.
        attribSource = new FilePropAttribSource(srcAttrib);
        propSource = attribSource;

        // If this is a primvar being authored, we want to create an ":indices"
        // array for the attribute to make sure that if we are bringing in this
        // geometry as an overlay, and we are overlaying a primvar that had an
        // ":indices" array, that we don't accidentally keep that old
        // ":indices" array. We will either create a real indices attribute, or
        // author a blocking value. The special "SdfValueBlock" value tells USD
        // to return the schema default for the attribute.
        if (createIndicesAttrib)
        {
            GEO_FileProp *indicesProp = nullptr;
            std::string indicesAttribName(usdAttribName.GetString());

            indicesAttribName += ":indices";
            if (!constantAttrib &&
                attrib->myName.multiMatch(options.myIndexAttribs))
            {
                const DT *data = attribSource->data();
                UT_Array<int> indices;
                UT_Array<DT> values;
                UT_Map<DT, int> attribMap;
                int maxidx = 0;

                // We have been asked to author an indices attribute for this
                // primvar. Go through all the values for the primvar, and
                // build a list of unique values and a list of indices into
                // this array of unique values.
                indices.setSizeNoInit(attribSource->size());
                for (exint i = 0, n = attribSource->size(); i < n; i++)
                {
                    const DT *value = &data[i];
                    auto it = attribMap.find(*value);

                    if (it == attribMap.end())
                    {
                        it = attribMap.emplace(*value, maxidx++).first;
                        values.append(*value);
                    }
                    indices(i) = it->second;
                }

                // Create the indices attribute from the indexes into the array
                // of unique values.
                indicesProp = filePrim.addProperty(
                    TfToken(indicesAttribName), SdfValueTypeNames->IntArray,
                    new GEO_FilePropConstantArraySource<int>(indices));
                if (defaultAttrib)
                    indicesProp->setValueIsDefault(true);
                indicesProp->addCustomData(
                    HUSDgetDataIdToken(), VtValue(dataId));

                // Update the data source to just be the array of the unique
                // values.
                delete propSource;
                propSource = new FilePropConstantSource(values);
            }
            else
            {
                // Block the indices attribute. Blocked attribute must be set
                // as the default value.
                indicesProp = filePrim.addProperty(
                    TfToken(indicesAttribName), SdfValueTypeNames->IntArray,
                    new GEO_FilePropConstantSource<SdfValueBlock>(
                        SdfValueBlock()));
                indicesProp->setValueIsDefault(true);
            }
        }

        prop = filePrim.addProperty(usdAttribName, usdTypeName, propSource);

        if (owner != HAPI_ATTROWNER_INVALID)
        {
            const TfToken &interp = (myType == HAPI_PARTTYPE_CURVE) ?
                                        GEOhapiCurveOwnerToInterpToken(owner) :
                                        GEOhapiMeshOwnerToInterpToken(owner);

            if (!interp.IsEmpty())
                prop->addMetadata(
                    UsdGeomTokens->interpolation, VtValue(interp));
        }

        if (defaultAttrib)
            prop->setValueIsDefault(true);
        prop->addCustomData(HUSDgetDataIdToken(), VtValue(dataId));

        processedAttribs.insert(attrib->myName);
    }

    return prop;
}

void
GEO_HAPIPart::convertExtraAttrib(GEO_FilePrim &filePrim,
                                 GEO_HAPIAttributeHandle &attrib,
                                 const TfToken &usdAttribName,
                                 UT_ArrayStringSet &processedAttribs,
                                 bool createIndicesAttrib,
                                 const GEO_ImportOptions &options,
                                 const GT_DataArrayHandle &vertexIndirect)
{
    bool applied = false;   // set in the macro below

// start #define
#define APPLY_ATTRIB(usdTypeName, type, typeComp)				\
    applyAttrib<type, typeComp>(filePrim, attrib, usdAttribName, usdTypeName,	\
                                processedAttribs, createIndicesAttrib,		\
                                options, vertexIndirect);			\
    applied = true;
// end #define


    // Factors that determine the property type
    HAPI_AttributeTypeInfo typeInfo = attrib->myTypeInfo;
    HAPI_StorageType storage = attrib->myDataType;
    int tupleSize = attrib->myTupleSize;

    // Specific type names
    switch (tupleSize)
    {
	case 16:
	{
	    if (typeInfo == HAPI_ATTRIBUTE_TYPE_MATRIX)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
	    }
	    break;
	}

	case 9:
	{
	    if (typeInfo == HAPI_ATTRIBUTE_TYPE_MATRIX3)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Matrix3dArray, GfMatrix3d, fpreal64);
	    }
	    break;
	}

	case 4:
	{
	    if (typeInfo == HAPI_ATTRIBUTE_TYPE_COLOR)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Color4fArray, GfVec4f, fpreal);
	    }
	    else if (typeInfo == HAPI_ATTRIBUTE_TYPE_QUATERNION)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->QuatfArray, GfVec4f, fpreal);
	    }
	    break;
	}

	case 3:
	{
	    if (typeInfo == HAPI_ATTRIBUTE_TYPE_POINT)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Point3fArray, GfVec3f, fpreal32);
	    }
	    else if (typeInfo == HAPI_ATTRIBUTE_TYPE_HPOINT)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Point3fArray, GfVec3f, fpreal32);
	    }
	    else if (typeInfo == HAPI_ATTRIBUTE_TYPE_VECTOR)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Vector3fArray, GfVec3f, fpreal32);
	    }
	    else if (typeInfo == HAPI_ATTRIBUTE_TYPE_NORMAL)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Normal3fArray, GfVec3f, fpreal32);
	    }
	    else if (typeInfo == HAPI_ATTRIBUTE_TYPE_COLOR)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Color3fArray, GfVec3f, fpreal32);
	    }
	    else if (typeInfo == HAPI_ATTRIBUTE_TYPE_TEXTURE)
	    {
		if (storage == HAPI_STORAGETYPE_FLOAT)
		{
		    APPLY_ATTRIB(SdfValueTypeNames->TexCoord3fArray, GfVec3f, fpreal32);
		}
		else if (storage == HAPI_STORAGETYPE_FLOAT64)
		{
		    APPLY_ATTRIB(SdfValueTypeNames->TexCoord3dArray, GfVec3d, fpreal64);
		}
	    }
	    break;
	}

	case 2:
	{
	    if (typeInfo == HAPI_ATTRIBUTE_TYPE_TEXTURE)
	    {
		if (storage == HAPI_STORAGETYPE_FLOAT)
		{
		    APPLY_ATTRIB(SdfValueTypeNames->TexCoord2fArray, GfVec2f, fpreal32);
		}
		else if (storage == HAPI_STORAGETYPE_FLOAT64)
		{
		    APPLY_ATTRIB(SdfValueTypeNames->TexCoord2dArray, GfVec2d, fpreal64);
		}
	    }
	    break;
	}

	default:
	    break;
    }

    if (applied)
        return;

    // General type names
    switch (storage)
    {
	case HAPI_STORAGETYPE_FLOAT:
	{
	    if (tupleSize == 16)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
	    }
	    else if (tupleSize == 9)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
	    }
	    else if (tupleSize == 4)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Float4Array, GfVec4f, fpreal32);
	    }
	    else if (tupleSize == 3)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Float3Array, GfVec3f, fpreal32);
	    }
	    else if (tupleSize == 2)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Float2Array, GfVec2f, fpreal32);
	    }
	    else if (tupleSize == 1)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->FloatArray, fpreal32, fpreal32);
	    }
	    break;
	}

	case HAPI_STORAGETYPE_FLOAT64:
	{
	    if (tupleSize == 16)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
	    }
	    else if (tupleSize == 9)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
	    }
	    else if (tupleSize == 4)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Double4Array, GfVec4d, fpreal64);
	    }
	    else if (tupleSize == 3)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Double3Array, GfVec3d, fpreal64);
	    }
	    else if (tupleSize == 2)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Double2Array, GfVec2d, fpreal64);
	    }
	    else if (tupleSize == 1)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->DoubleArray, fpreal64, fpreal64);
	    }
	    break;
	}

	case HAPI_STORAGETYPE_INT:
	{
	    if (tupleSize == 4)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Int4Array, GfVec4i, int);
	    }
	    else if (tupleSize == 3)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Int3Array, GfVec3i, int);
	    }
	    else if (tupleSize == 2)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Int2Array, GfVec2i, int);
	    }
	    else if (tupleSize == 1)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->IntArray, int, int);
	    }
	    break;
	}

	case HAPI_STORAGETYPE_INT64:
	{
	    if (tupleSize == 1)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->Int64Array, int64, int64);
	    }
	    break;
	}

	case HAPI_STORAGETYPE_STRING:
	{
	    if (tupleSize == 1)
	    {
		APPLY_ATTRIB(SdfValueTypeNames->StringArray, std::string, std::string);
	    }
	    break;
	}

	default:
	    break;
    }

#undef APPLY_ATTRIB
}

void
GEO_HAPIPart::setupExtraPrimAttributes(GEO_FilePrim &filePrim,
                                       UT_ArrayStringSet &processedAttribs,
                                       const GEO_ImportOptions &options,
                                       const GT_DataArrayHandle &vertexIndirect)
{
    static std::string thePrimvarPrefix("primvars:");

    for (exint i = 0; i < myAttribNames.entries(); i++)
    {
        // Don't process attributes that have already been processed
        if (!processedAttribs.contains(myAttribNames[i]))
        {
            if (options.multiMatch(myAttribNames[i]))
            {
                GEO_HAPIAttributeHandle &attrib = myAttribs[myAttribNames[i]];

                TfToken usdAttribName;
                bool createIndicesAttrib = true;
                UT_StringHolder decodedName =
                    UT_VarEncode::decodeAttrib(attrib->myName);

                if (attrib->myName.multiMatch(options.myCustomAttribs))
                {
                    usdAttribName = TfToken(decodedName.toStdString());
                    createIndicesAttrib = false;
                }
                else
                {
                    usdAttribName =
                        TfToken(thePrimvarPrefix + decodedName.toStdString());
                }

                convertExtraAttrib(filePrim, attrib, usdAttribName,
                                   processedAttribs, createIndicesAttrib,
                                   options, vertexIndirect);
            }
        }
    }
}

SYS_FORCE_INLINE
bool
GEO_HAPIPart::checkAttrib(UT_StringHolder &attribName,
                          const GEO_ImportOptions &options)
{
    return (myAttribs.contains(attribName) &&
            options.multiMatch(attribName));
}

void
GEO_HAPIPart::setupPrimAttributes(GEO_FilePrim &filePrim,
                                  const GEO_ImportOptions &options,
                                  const GT_DataArrayHandle &vertexIndirect)
{
    // Copy the processed attribute list because we modify it as we
    // import attributes from the geometry.
    UT_ArrayStringSet processedAttribs(options.myProcessedAttribs);

    // If a common attribute is a string instead of a numeric value
    // or vice-versa, it will be treated as an "extra" attribute

    // Bounds
    const UT_StringHolder &boundsName = theBoundsName.asHolder();
    if (!processedAttribs.contains(boundsName) &&
        (boundsName.multiMatch(options.myAttribs)))
    {
        UT_BoundingBoxR bbox;
        getBounds(bbox);

        if (!bbox.isInvalidFast())
        {
            VtVec3fArray bounds(2);
            bounds[0] = GfVec3f(bbox.xmin(), bbox.ymin(), bbox.zmin());
            bounds[1] = GfVec3f(bbox.xmax(), bbox.ymax(), bbox.zmax());

            GEO_FileProp *prop = filePrim.addProperty(
                UsdGeomTokens->extent, SdfValueTypeNames->Float3Array,
                new GEO_FilePropConstantSource<VtVec3fArray>(bounds));

            if (prop && boundsName.multiMatch(options.myStaticAttribs))
                prop->setValueIsDefault(true);
            processedAttribs.insert(boundsName);
        }
    }

    static UT_StringHolder thePointsAttrib(GA_Names::P);

    // Points
    if (checkAttrib(thePointsAttrib, options))
    {
        GEO_HAPIAttributeHandle &attrib = myAttribs[thePointsAttrib];

        if (attrib->myDataType != HAPI_STORAGETYPE_STRING)
        {
            // point values must be in a vector3 array
            attrib->convertTupleSize(3);

            applyAttrib<GfVec3f, float>(filePrim, attrib, UsdGeomTokens->points,
                                        SdfValueTypeNames->Point3fArray,
                                        processedAttribs, false, options,
                                        vertexIndirect);
        }
    }

    static UT_StringHolder theNormalsAttrib(GA_Names::N);

    // Normals
    if (checkAttrib(theNormalsAttrib, options))
    {
        GEO_HAPIAttributeHandle &attrib = myAttribs[theNormalsAttrib];

        if (attrib->myDataType != HAPI_STORAGETYPE_STRING)
        {
            // normal values must be in a vector3 array
            attrib->convertTupleSize(3);

            GEO_FileProp *prop = applyAttrib<GfVec3f, float>(
                filePrim, attrib, UsdGeomTokens->normals,
                SdfValueTypeNames->Normal3fArray, processedAttribs, false,
                options, vertexIndirect);

            // Normals attribute is not quite the same as primvars in how the
            // interpolation value is set.
            if (prop)
            {
                if (attrib->myOwner == HAPI_ATTROWNER_VERTEX)
                    prop->addMetadata(UsdGeomTokens->interpolation,
                                      VtValue(UsdGeomTokens->faceVarying));
                else
                    prop->addMetadata(UsdGeomTokens->interpolation,
                                      VtValue(UsdGeomTokens->varying));
            }
        }
    }

    static UT_StringHolder theColorAttrib(GA_Names::Cd);
    static UT_StringHolder theAlphaAttrib(GA_Names::Alpha);

    // Color (RGB)
    if (checkAttrib(theColorAttrib, options))
    {
        GEO_HAPIAttributeHandle &col = myAttribs[theColorAttrib];

        if (col->myDataType != HAPI_STORAGETYPE_STRING)
        {
            // HAPI gives us RGBA tuples by default
            // USD expects RGB and Alpha seperately,
            // so make another alpha attribute if
            // it doesn't already exist
            if (col->myTupleSize >= 4)
            {
                if (!myAttribs.contains(theAlphaAttrib))
                {
                    // Make alpha attrib
                    GT_DANumeric<float> *alphas = new GT_DANumeric<float>(
                        col->myCount, 1);

                    for (exint i = 0; i < col->myCount; i++)
                    {
                        fpreal32 aVal = col->myData->getF32(i, 3);
                        alphas->set(aVal, i);
                    }

                    GEO_HAPIAttributeHandle a(new GEO_HAPIAttribute(
                        theAlphaAttrib, col->myOwner, col->myCount, 1,
                        HAPI_STORAGETYPE_FLOAT, alphas));

		    // Add the alpha attribute
                    myAttribs[theAlphaAttrib].swap(a);

		    UT_ASSERT(!a.get());
                }
            }

            col->convertTupleSize(3);

            applyAttrib<GfVec3f, float>(
                filePrim, col, UsdGeomTokens->primvarsDisplayColor,
                SdfValueTypeNames->Color3fArray, processedAttribs, true,
                options, vertexIndirect);
        }
    }

    // Alpha
    if (checkAttrib(theAlphaAttrib, options))
    {
        GEO_HAPIAttributeHandle &a = myAttribs[theAlphaAttrib];

        if (a->myDataType != HAPI_STORAGETYPE_STRING)
        {
            a->convertTupleSize(1);

            applyAttrib<float>(filePrim, a,
                               UsdGeomTokens->primvarsDisplayOpacity,
                               SdfValueTypeNames->FloatArray, processedAttribs,
                               true, options, vertexIndirect);
        }
    }

    // Visibility
    static constexpr UT_StringLit theVisibilityAttrib("usdvisibility");
    if (myAttribs.contains(theVisibilityAttrib.asHolder()) &&
        theVisibilityName.asRef().multiMatch(options.myAttribs))
    {
        GEO_HAPIAttributeHandle &vis = myAttribs[theVisibilityAttrib.asHolder()];

        // This is expected as a string
        if (vis->myDataType == HAPI_STORAGETYPE_STRING)
        {
            // Use the first string to define visibility
            TfToken visibility(vis->myData->getS(0, 0));

            if (!visibility.IsEmpty())
            {
                bool makeVisible = (visibility != UsdGeomTokens->invisible);

                GEO_FileProp *prop = filePrim.addProperty(
                    UsdGeomTokens->visibility, SdfValueTypeNames->Token,
                    new GEO_FilePropConstantSource<TfToken>(
                        makeVisible ? UsdGeomTokens->inherited :
                                      UsdGeomTokens->invisible));

                prop->setValueIsDefault(theVisibilityName.asRef().multiMatch(
                    options.myStaticAttribs));
                prop->setValueIsUniform(false);

                processedAttribs.insert(theVisibilityAttrib);
            }
        }
    }

    static UT_StringHolder theVelocityAttrib(GA_Names::v);

    // Velocity
    if (checkAttrib(theVelocityAttrib, options))
    {
        GEO_HAPIAttributeHandle &v = myAttribs[theVelocityAttrib];

        if (v->myDataType != HAPI_STORAGETYPE_STRING)
        {
            v->convertTupleSize(3);

            applyAttrib<GfVec3f, float>(filePrim, v, UsdGeomTokens->velocities,
                                        SdfValueTypeNames->Vector3fArray,
                                        processedAttribs, false, options,
                                        vertexIndirect);
        }
    }

    static UT_StringHolder theAccelAttrib(GA_Names::accel);

    // Acceleration
    if (checkAttrib(theAccelAttrib, options))
    {
        GEO_HAPIAttributeHandle &a = myAttribs[theAccelAttrib];

        if (a->myDataType != HAPI_STORAGETYPE_STRING)
        {
            a->convertTupleSize(3);

            applyAttrib<GfVec3f, float>(
                filePrim, a, UsdGeomTokens->accelerations,
                SdfValueTypeNames->Vector3fArray, processedAttribs, false,
                options, vertexIndirect);
        }
    }

    static UT_StringHolder theTexCoordAttrib(GA_Names::uv);

    // Texture Coordinates (UV/ST)
    if (checkAttrib(theTexCoordAttrib, options) &&
        options.myTranslateUVToST)
    {
        GEO_HAPIAttributeHandle &tex = myAttribs[theTexCoordAttrib];

        // Skip renaming if st attrib exists
        UT_StringHolder stName =
            GusdUSD_Utils::TokenToStringHolder(UsdUtilsGetPrimaryUVSetName());
        if (!myAttribs.contains(stName))
        {
            UT_WorkBuffer buf;
            buf.format("primvars:{0}", stName);
            TfToken stToken(buf.buffer());

            if (tex->myDataType == HAPI_STORAGETYPE_FLOAT ||
                tex->myDataType == HAPI_STORAGETYPE_FLOAT64)
            {
                tex->convertTupleSize(2);

		if (tex->myDataType == HAPI_STORAGETYPE_FLOAT)
		{
		    applyAttrib<GfVec2f, float>(
			filePrim, tex, stToken, SdfValueTypeNames->TexCoord2fArray,
			processedAttribs, true, options, vertexIndirect);
		}
		else // tex->myDataType == HAPI_STORAGETYPE_FLOAT64
		{
		    applyAttrib<GfVec2d, double>(
			filePrim, tex, stToken, SdfValueTypeNames->TexCoord2dArray,
			processedAttribs, true, options, vertexIndirect);
		}
            }
        }
    }

    // Extra Attributes
    setupExtraPrimAttributes(
        filePrim, processedAttribs, options, vertexIndirect);
}
