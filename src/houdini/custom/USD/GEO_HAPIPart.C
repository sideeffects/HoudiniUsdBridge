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
#include "GEO_FilePrimInstancerUtils.h"
#include <GT/GT_CountArray.h>
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DASubArray.h>
#include <HUSD/XUSD_Utils.h>
#include <UT/UT_Assert.h>
#include <UT/UT_VarEncode.h>
#include <gusd/GT_PackedUSD.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Gf.h>
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

using namespace UT::Literal;

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
    switch (myType)
    {
    case HAPI_PARTTYPE_MESH:
    {
        myData.reset(new MeshData);
        MeshData *mData = UTverify_cast<MeshData *>(myData.get());

        int numFaces = part.faceCount;
        int numVertices = part.vertexCount;

        if (numFaces > 0)
        {
            GT_DANumeric<int> *faceCounts = new GT_DANumeric<int>(numFaces, 1);
            mData->faceCounts = faceCounts;

            ENSURE_SUCCESS(HAPI_GetFaceCounts(&session, geo.nodeId, part.id,
                                              faceCounts->data(), 0, numFaces),
                           session);
        }

        if (numVertices > 0)
        {
            GT_DANumeric<int> *vertices = new GT_DANumeric<int>(numVertices, 1);
            mData->vertices = vertices;

            ENSURE_SUCCESS(HAPI_GetVertexList(&session, geo.nodeId, part.id,
                                              vertices->data(), 0, numVertices),
                           session);
        }
        break;
    }

    case HAPI_PARTTYPE_CURVE:
    {
        myData.reset(new CurveData);
        CurveData *cData = UTverify_cast<CurveData *>(myData.get());
        HAPI_CurveInfo cInfo;

        ENSURE_SUCCESS(
            HAPI_GetCurveInfo(&session, geo.nodeId, part.id, &cInfo), session);

        int numCurves = cInfo.curveCount;
        int numKnots = cInfo.hasKnots ? cInfo.knotCount : 0;
        cData->curveType = cInfo.curveType;
        cData->constantOrder = cInfo.order;
        cData->periodic = cInfo.isPeriodic;

        if (numCurves > 0)
        {
            GT_DANumeric<int> *curveCounts = new GT_DANumeric<int>(
                numCurves, 1);
            cData->curveCounts = curveCounts;

            ENSURE_SUCCESS(
                HAPI_GetCurveCounts(&session, geo.nodeId, part.id,
                                    curveCounts->data(), 0, numCurves),
                session);

            // If the order varies between curves
            if (!cData->constantOrder)
            {
                GT_DANumeric<int> *curveOrders = new GT_DANumeric<int>(
                    numCurves, 1);
                cData->curveOrders = curveOrders;

                ENSURE_SUCCESS(
                    HAPI_GetCurveOrders(&session, geo.nodeId, part.id,
                                        curveOrders->data(), 0, numCurves),
                    session);
            }
        }

        if (numKnots > 0)
        {
            GT_DANumeric<float> *curveKnots = new GT_DANumeric<float>(
                numKnots, 1);
            cData->curveKnots = curveKnots;

            ENSURE_SUCCESS(HAPI_GetCurveKnots(&session, geo.nodeId, part.id,
                                              curveKnots->data(), 0, numKnots),
                           session);
        }

        break;
    }

    case HAPI_PARTTYPE_VOLUME:
    {
        myData.reset(new VolumeData);
        VolumeData *vData = UTverify_cast<VolumeData *>(myData.get());
        HAPI_VolumeInfo vInfo;

        ENSURE_SUCCESS(
            HAPI_GetVolumeInfo(&session, geo.nodeId, part.id, &vInfo), session);

        CHECK_RETURN(GEOhapiExtractString(session, vInfo.nameSH, buf));
        vData->name = buf.buffer();

        // Get bounding box
        UT_BoundingBoxF &bbox = vData->bbox;
        ENSURE_SUCCESS(HAPI_GetVolumeBounds(&session, geo.nodeId, part.id,
                                            &bbox.vals[0][0], &bbox.vals[1][0],
                                            &bbox.vals[2][0], &bbox.vals[0][1],
                                            &bbox.vals[1][1], &bbox.vals[2][1],
                                            nullptr, nullptr, nullptr),
                       session);

        GEOhapiConvertXform(vInfo.transform, vData->xform);

        break;
    }

    case HAPI_PARTTYPE_INSTANCER:
    {
        myData.reset(new InstanceData);
        InstanceData *iData = UTverify_cast<InstanceData *>(myData.get());

        // Get data for all parts to instance
        int partCount = part.instancedPartCount;
        UT_UniquePtr<HAPI_PartId> instanceIds(new HAPI_PartId[partCount]);
        ENSURE_SUCCESS(
            HAPI_GetInstancedPartIds(
                &session, geo.nodeId, part.id, instanceIds.get(), 0, partCount),
            session);

        iData->instances.setSize(partCount);
        HAPI_PartInfo partInfo;

        for (int i = 0; i < partCount; i++)
        {
            ENSURE_SUCCESS(HAPI_GetPartInfo(&session, geo.nodeId,
                                            instanceIds.get()[i], &partInfo),
                           session);

            CHECK_RETURN(
                iData->instances[i].loadPartData(session, geo, partInfo, buf));
        }

        int instanceCount = part.instanceCount;
        UT_UniquePtr<HAPI_Transform> hapiXforms(
            new HAPI_Transform[instanceCount]);
        ENSURE_SUCCESS(HAPI_GetInstancerPartTransforms(
                           &session, geo.nodeId, part.id, HAPI_RSTORDER_DEFAULT,
                           hapiXforms.get(), 0, instanceCount),
                       session);

        iData->instanceTransforms.setSize(instanceCount);

        for (int i = 0; i < instanceCount; i++)
        {
            GEOhapiConvertXform(
                hapiXforms.get()[i], iData->instanceTransforms[i]);
        }

        break;
    }

    case HAPI_PARTTYPE_SPHERE:
    {
        myData.reset(new SphereData);
        SphereData *sData = UTverify_cast<SphereData *>(myData.get());
        HAPI_SphereInfo sInfo;

        ENSURE_SUCCESS(
            HAPI_GetSphereInfo(&session, geo.nodeId, part.id, &sInfo), session);

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
        return false;
    }

    if (!myData)
    {
        myData.reset(new PartData);
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
            ENSURE_SUCCESS(
                HAPI_GetAttributeNames(&session, geo.nodeId, part.id,
                                       (HAPI_AttributeOwner)i, handles,
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

                // Ignore an attribute if one with the same name is already
                // saved
                if (!myAttribs.contains(attribName))
                {
                    exint nameIndex = myAttribNames.append(attribName);
                    GEO_HAPIAttributeHandle attrib(new GEO_HAPIAttribute);

                    CHECK_RETURN(attrib->loadAttrib(session, geo, part,
                                                    (HAPI_AttributeOwner)i,
                                                    attrInfo, attribName, buf));

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

UT_BoundingBoxR
GEO_HAPIPart::getBounds() const
{
    UT_BoundingBoxR bbox;
    bbox.makeInvalid();

    switch (myType)
    {
    case HAPI_PARTTYPE_SPHERE:
    {
        //
        // The sphere's radius will be set to  1 and then transformed.
        // The bounds will also be transformed, so the bounds will
        // match a sphere at the origin with radius 1
        //

        bbox.setBounds(-1, -1, -1, // Min
                       1, 1, 1);   // Max

        break;
    }

    case HAPI_PARTTYPE_VOLUME:
    {
        VolumeData *vData = UTverify_cast<VolumeData *>(myData.get());
        bbox = vData->bbox;
        break;
    }

    default:
    {
        // Add all points to the Bounding Box

        if (myAttribs.contains(HAPI_ATTRIB_POSITION))
        {
            const GEO_HAPIAttributeHandle &points =
                myAttribs.at(HAPI_ATTRIB_POSITION);

            // Points attribute should be a float type
            if (points->myDataType != HAPI_STORAGETYPE_STRING)
            {
                // Make sure points are 3 dimensions
                points->convertTupleSize(3);

                GT_DataArray *xyz = points->myData.get();

                for (int i = 0; i < points->entries(); i++)
                {
                    bbox.enlargeBounds(xyz->getF32(i, 0), xyz->getF32(i, 1),
                                       xyz->getF32(i, 2));
                }
            }
        }
    }
    }

    return bbox;
}

UT_Matrix4D
GEO_HAPIPart::getXForm() const
{
    UT_Matrix4D xform;
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

    case HAPI_PARTTYPE_VOLUME:
    {
        VolumeData *vData = UTverify_cast<VolumeData *>(myData.get());
        xform = vData->xform;
        break;
    }

    default:
    {
        // return identitiy matrix
    }
    }

    return xform;
}

void
GEO_HAPIPart::extractCubicBasisCurves()
{
    CurveData *curve = UTverify_cast<CurveData *>(myData.get());
    UT_ASSERT(!curve->hasExtractedBasisCurves);

    GT_Int32Array *cubics = new GT_Int32Array(0, 1);
    GT_Int32Array *vertexRemap = new GT_Int32Array(0, 1);

    exint vertexIndex = 0;

    for (exint i = 0, n = curve->curveOrders->entries(); i < n; i++)
    {
        // Find all cubic curves
        if (curve->curveOrders->getI32(i) == 4)
        {
            cubics->append(i);

            for (int v = 0, n = curve->curveCounts->getI32(i); v < n; v++)
            {
                vertexRemap->append(vertexIndex + v);
            }
        }

        vertexIndex += curve->curveCounts->getI32(i);
    }

    // If we found cubic curves, update this part to display them
    if (cubics->entries() > 0)
    {
        curve->hasExtractedBasisCurves = true;
        curve->constantOrder = 4;

        curve->curveCounts = new GT_DAIndirect(cubics, curve->curveCounts);

        // Attributes need to be updated to ignore data for unsupported curves
        for (exint i = 0; i < myAttribNames.entries(); i++)
        {
            GEO_HAPIAttribute *attr = myAttribs[myAttribNames[i]].get();

            if (attr->myOwner == HAPI_ATTROWNER_PRIM)
            {
                attr->myData = new GT_DAIndirect(cubics, attr->myData);
            }
            else if (attr->myOwner == HAPI_ATTROWNER_VERTEX ||
                     attr->myOwner == HAPI_ATTROWNER_POINT)
            {
                attr->myData = new GT_DAIndirect(vertexRemap, attr->myData);
            }
        }
    }
}

bool
GEO_HAPIPart::isPinned()
{
    CurveData *curve = UTverify_cast<CurveData *>(myData.get());
    const int order = curve->constantOrder;

    // Only modify sets of cubic NURBS curves
    if (order != 4 || curve->curveType != HAPI_CURVETYPE_NURBS ||
        curve->periodic || !curve->curveKnots)
        return false;

    GT_Size numCurves = curve->curveCounts->entries();

    // Check knot values
    const GT_DataArrayHandle knots = curve->curveKnots;
    GT_Offset startOffset = 0;
    for (GT_Size curveIndex = 0; curveIndex < numCurves; curveIndex++)
    {
        const GT_Offset knotStart = startOffset;
        const GT_Size knotCount = curve->curveCounts->getI64(curveIndex) +
                                  order;
        // Update offset for next curve
        startOffset += knotCount;

        fpreal64 knotVal = knots->getF64(knotStart);
        for (GT_Size i = 1; i < order; i++)
        {
            if (!SYSisEqual(knots->getF64(knotStart + i), knotVal))
                return false;
        }

        knotVal = knots->getF64(startOffset - 1);
        for (GT_Size i = knotCount - order; i < knotCount - 1; i++)
        {
            if (!SYSisEqual(knots->getF64(knotStart + i), knotVal))
                return false;
        }
    }

    return true;
}

void
GEO_HAPIPart::revertToOriginalCurves()
{
    CurveData *curve = UTverify_cast<CurveData *>(myData.get());
    GT_DAIndirect *indirect;

    if (curve->hasExtractedBasisCurves)
    {
        curve->hasExtractedBasisCurves = false;
        curve->constantOrder = 0;

        // Indirects were used to manipulate the data, so use the data the
        // indirect was referencing
        indirect = UTverify_cast<GT_DAIndirect *>(curve->curveCounts.get());
        curve->curveCounts = indirect->referencedData();

        for (exint i = 0; i < myAttribNames.entries(); i++)
        {
            GEO_HAPIAttribute *attr = myAttribs[myAttribNames[i]].get();

            if (attr->myOwner == HAPI_ATTROWNER_PRIM ||
                attr->myOwner == HAPI_ATTROWNER_VERTEX ||
                attr->myOwner == HAPI_ATTROWNER_POINT)
            {
                indirect = UTverify_cast<GT_DAIndirect *>(attr->myData.get());
                attr->myData = indirect->referencedData();
            }
        }
    }
}

/////////////////////////////////////////////
// HAPI to USD functions
//

PXR_NAMESPACE_USING_DIRECTIVE

static constexpr UT_StringLit theBoundsName("bounds");
static constexpr UT_StringLit theVisibilityName("visibility");
// static constexpr UT_StringLit theVolumeSavePathName("usdvolumesavepath");

void
GEO_HAPIPointInstancerData::initRelationships(GEO_FilePrimMap &filePrimMap)
{
    if (madePointInstancer)
    {
        UT_ASSERT(!pointInstancerPath.IsEmpty());
        GEO_FilePrim &piPrim = filePrimMap[pointInstancerPath];

        piPrim.addRelationship(UsdGeomTokens->prototypes, protoPaths);
    }
}

void
GEO_HAPIPart::partToPrim(GEO_HAPIPart &part,
                         const GEO_ImportOptions &options,
                         const SdfPath &parentPath,
                         GEO_FilePrimMap &filePrimMap,
                         const std::string &pathName,
                         GEO_HAPIPrimCounts &counts,
                         GEO_HAPIPointInstancerData &piData)
{
    if (part.isInstancer())
    {
        // Instancers need to set up their instances
        part.setupInstances(
            parentPath, filePrimMap, pathName, options, counts, piData);
    }
    else
    {
        SdfPath path = GEOhapiGetPrimPath(part.getType(), parentPath, counts);

        GEO_FilePrim &filePrim(filePrimMap[path]);
        filePrim.setPath(path);

        // For index remapping
        GT_DataArrayHandle indirectVertices;

        // adjust type-specific properties
        bool define = part.setupPrimType(
            filePrim, filePrimMap, options, pathName, indirectVertices);

        // add attributes to the prim
        if (define)
        {
            part.setupPrimAttributes(filePrim, options, indirectVertices);
        }

        filePrim.setIsDefined(define);
        filePrim.setInitialized();
    }
}

static constexpr UT_StringLit thePrototypeName("Prototypes");
static constexpr UT_StringLit thePointInstancerName("instances");

void
GEO_HAPIPart::setupInstances(const SdfPath &parentPath,
                             GEO_FilePrimMap &filePrimMap,
                             const std::string &pathName,
                             const GEO_ImportOptions &options,
                             GEO_HAPIPrimCounts &counts,
                             GEO_HAPIPointInstancerData &piData)
{
    UT_ASSERT(isInstancer());
    InstanceData *iData = UTverify_cast<InstanceData *>(myData.get());
    static const std::string theInstanceSuffix = "obj_";

    // Apply the attributes on the instancer to the child transform
    auto processChildAttributes = [&](GEO_FilePrim &xformPrim,
                                      GEO_HAPIPart &childPart) {
        UT_ArrayStringSet processedAttribs(options.myProcessedAttribs);

        // We don't want the positions attributes
        processedAttribs.insert(HAPI_ATTRIB_POSITION);

        childPart.setupColorAttributes(
            xformPrim, options, GT_DataArrayHandle(), processedAttribs);
        childPart.setupExtraPrimAttributes(
            xformPrim, processedAttribs, options, GT_DataArrayHandle());
    };

    if (options.myPackedPrimHandling == GEO_PACKED_NATIVEINSTANCES)
    {
        SdfPath protoPath = parentPath.AppendChild(TfToken(thePrototypeName));
        GEO_FilePrim &protoPrim(filePrimMap[protoPath]);
        const exint protoCount = iData->instances.entries();

        // If there are no prototypes at this level yet,
        // set up a prototype scope
        if (counts.prototypes <= 0 && protoCount > 0)
        {
            // Create an invisible scope
            protoPrim.setPath(protoPath);
            protoPrim.setTypeName(GEO_FilePrimTypeTokens->Scope);
            protoPrim.setInitialized();

            GEO_FileProp *prop = protoPrim.addProperty(
                UsdGeomTokens->visibility, SdfValueTypeNames->Token,
                new GEO_FilePropConstantSource<TfToken>(
                    UsdGeomTokens->invisible));
            prop->setValueIsDefault(true);
            prop->setValueIsUniform(true);
        }

        UT_Array<SdfPath> objPaths;

        exint childProtoIndex = -1;
        GEO_HAPIPrimCounts childProtoCounts;

        for (exint i = 0; i < protoCount; i++)
        {
            // We want to keep all prototypes together to avoid
            // having multiple prototype scopes on the same level
            if (iData->instances[i].isInstancer())
            {
                if (childProtoIndex < 0)
                {
                    std::string suffix = theInstanceSuffix +
                                         std::to_string(counts.prototypes++);
                    childProtoIndex =
                        objPaths.append(protoPath.AppendChild(TfToken(suffix)));
                }

                partToPrim(iData->instances[i], options,
                           objPaths[childProtoIndex], filePrimMap, pathName,
                           childProtoCounts, piData);
            }
            else
            {
                // Create the part under a transform
                std::string suffix = theInstanceSuffix +
                                     std::to_string(counts.prototypes++);
                objPaths.append(protoPath.AppendChild(TfToken(suffix)));

                // Make a new primcounts struct to keep track of what's under
                // this transform
                GEO_HAPIPrimCounts childCounts;
                partToPrim(iData->instances[i], options, objPaths[i],
                           filePrimMap, pathName, childCounts, piData);
            }
        }

        // Create the references to the prototypes
        GEO_HAPIPart tempPart;
        for (exint transInd = 0; transInd < iData->instanceTransforms.entries();
             transInd++)
        {
            createInstancePart(tempPart, transInd);

            for (exint objInd = 0; objInd < objPaths.entries(); objInd++)
            {
                SdfPath refPath = GEOhapiGetPrimPath(
                    HAPI_PARTTYPE_INSTANCER, parentPath, counts);
                GEO_FilePrim &refPrim(filePrimMap[refPath]);
                refPrim.setPath(refPath);
                refPrim.setTypeName(GEO_FilePrimTypeTokens->Xform);
                refPrim.addMetadata(SdfFieldKeys->Instanceable, VtValue(true));

                // Make this a reference of the corresponding prototype
                GEOinitInternalReference(refPrim, objPaths[objInd]);

                // Apply the corresponding transform
                GEOhapiInitXformAttrib(
                    refPrim, iData->instanceTransforms[transInd], options);

                // Apply attributes
                processChildAttributes(refPrim, tempPart);
            }
        }
    }
    else if (options.myPackedPrimHandling == GEO_PACKED_POINTINSTANCER)
    {
        const exint protoCount = iData->instances.entries();

        // Generate point instancer if it hasn't been created yet
        if (piData.prototypeCounts.prototypes <= 0 && protoCount > 0)
        {
            setupPointInstancer(parentPath, filePrimMap, piData, options);
        }

        // Place all the instances under the prototype scope
        SdfPath protoPath =
            parentPath.AppendChild(TfToken(thePointInstancerName))
                .AppendChild(TfToken(thePrototypeName));

        SdfPath childInstancerPath = SdfPath::EmptyPath();
        GEO_HAPIPrimCounts childInstancerCounts;
        GEO_HAPIPointInstancerData childInstancerPiData(
            iData->instances, filePrimMap);

        for (exint i = 0; i < protoCount; i++)
        {
            // We want to keep all prototypes together to avoid
            // having multiple prototype scopes on the same level
            if (iData->instances[i].isInstancer())
            {
                if (childInstancerPath.IsEmpty())
                {
                    std::string suffix = theInstanceSuffix +
                                         std::to_string(counts.prototypes++);
                    childInstancerPath = protoPath.AppendChild(TfToken(suffix));
                    piData.protoPaths.push_back(childInstancerPath);
                }

                partToPrim(iData->instances[i], options, childInstancerPath,
                           filePrimMap, pathName, childInstancerCounts,
                           childInstancerPiData);
            }
            else
            {
                // Create the part under a transform
                std::string suffix = theInstanceSuffix +
                                     std::to_string(counts.prototypes++);
                SdfPath instancePath = protoPath.AppendChild(TfToken(suffix));

                // Create structs to keep track of new level in tree
                GEO_HAPIPrimCounts childCounts;
                GEO_HAPIPointInstancerData childPiData(
                    iData->instances, filePrimMap);
                partToPrim(iData->instances[i], options, instancePath,
                           filePrimMap, pathName, childCounts, childPiData);

                piData.protoPaths.push_back(instancePath);
            }
        }

        // Set up relationships of all child instancers
        childInstancerPiData.initRelationships(filePrimMap);
    }
    else // options.myPackedPrimHandling == GEO_PACKED_XFORMS
    {
        // Create transforms to hold copies of the packed parts
        GEO_HAPIPart tempPart;

        for (exint transInd = 0; transInd < iData->instanceTransforms.entries();
             transInd++)
        {
            GEO_HAPIPrimCounts childInstCounts;
            SdfPath childInstPath = SdfPath::EmptyPath();

            // Update tempPart to hold the attributes needed for the xforms
            // above the new instances
            createInstancePart(tempPart, transInd);

            for (exint objInd = 0; objInd < iData->instances.entries();
                 objInd++)
            {
                // Have all child instancers put their instances under the same
                // transform
                if (iData->instances[objInd].isInstancer())
                {
                    if (childInstPath.IsEmpty())
                    {
                        // Init the transform to hold the instancers
                        childInstPath = GEOhapiGetPrimPath(
                            HAPI_PARTTYPE_INSTANCER, parentPath, counts);

                        GEO_FilePrim &xformPrim(filePrimMap[childInstPath]);
                        xformPrim.setPath(childInstPath);
                        xformPrim.setTypeName(GEO_FilePrimTypeTokens->Xform);
                        GEOhapiInitXformAttrib(
                            xformPrim, iData->instanceTransforms[transInd],
                            options);

                        // Apply attributes
                        processChildAttributes(xformPrim, tempPart);
                    }

                    // Initialize this child instancer under the transform
                    // pointed to by childInstPath
                    partToPrim(iData->instances[objInd], options, childInstPath,
                               filePrimMap, pathName, childInstCounts, piData);
                }
                else
                {
                    SdfPath objPath = GEOhapiGetPrimPath(
                        HAPI_PARTTYPE_INSTANCER, parentPath, counts);

                    GEO_FilePrim &xformPrim(filePrimMap[objPath]);
                    xformPrim.setPath(objPath);
                    xformPrim.setTypeName(GEO_FilePrimTypeTokens->Xform);

                    // Create a new counts object to keep track of children
                    GEO_HAPIPrimCounts childCounts;

                    // Create the prim
                    partToPrim(iData->instances[objInd], options, objPath,
                               filePrimMap, pathName, childCounts, piData);

                    // Apply the corresponding transform
                    GEOhapiInitXformAttrib(xformPrim,
                                           iData->instanceTransforms[transInd],
                                           options);

                    // Apply attributes
                    processChildAttributes(xformPrim, tempPart);
                }
            }
        }
    }
}

bool
GEO_HAPIPart::isInvisible(const GEO_ImportOptions &options) const
{
    bool invisible = false;

    if (myAttribs.contains(theVisibilityName.asHolder()) &&
        theVisibilityName.asRef().multiMatch(options.myAttribs))
    {
        const GEO_HAPIAttributeHandle &vis =
            myAttribs.at(theVisibilityName.asHolder());

        // This is expected as a string
        if (vis->myDataType == HAPI_STORAGETYPE_STRING)
        {
            // Use the first string to define visibility
            TfToken visibility(vis->myData->getS(0, 0));

            if (!visibility.IsEmpty())
            {
                invisible = (visibility == UsdGeomTokens->invisible);
            }
        }
    }

    return invisible;
}

// Assumes the order of piData.siblingParts matches the order of partToPrim()
// calls with the same parts
void
GEO_HAPIPart::setupPointInstancer(const SdfPath &parentPath,
                                  GEO_FilePrimMap &filePrimMap,
                                  GEO_HAPIPointInstancerData &piData,
                                  const GEO_ImportOptions &options)
{
    static const UT_StringHolder &theIdsAttrib(GA_Names::id);

    // Create a point instancer under parentPath
    SdfPath piPath = parentPath.AppendChild(TfToken(thePointInstancerName));
    GEO_FilePrim &piPrim = filePrimMap[piPath];
    piPrim.setPath(piPath);
    piPrim.setTypeName(GEO_FilePrimTypeTokens->PointInstancer);
    piPrim.setInitialized();

    exint numSiblings = piData.siblingParts.entries();
    GEO_HAPIPartArray &siblings = piData.siblingParts;

    UT_Array<int> protoIndices;
    UT_Array<exint> invisibleInstances;
    UT_Array<UT_Matrix4D> xforms;
    exint protoIndex = 0;

    GEO_HAPIPart piPart;
    UT_StringMap<UT_Array<GEO_HAPIAttributeHandle>> attribsMap;

    for (exint s = 0; s < numSiblings; s++)
    {
        GEO_HAPIPart &part = siblings(s);
        if (part.isInstancer())
        {
            InstanceData *iData =
                UTverify_cast<InstanceData *>(part.myData.get());

            exint numTransforms = iData->instanceTransforms.entries();
            exint numInstances = iData->instances.entries();

            bool foundChildInstance = false;
            for (exint i = 0; i < numInstances; i++)
            {
                // Instances go under the same transform, so we only need 1
                // instance prototype
                if (foundChildInstance && iData->instances(i).isInstancer())
                    continue;

                for (exint t = 0; t < numTransforms; t++)
                {
                    protoIndices.append(protoIndex);
                    xforms.append(iData->instanceTransforms(t));
                }

                if (part.isInvisible(options))
                {
                    invisibleInstances.append(protoIndex);
                }

                if (iData->instances(i).isInstancer())
                    foundChildInstance = true;

                protoIndex++;
            }

            // Get the relevant attributes
            for (exint a = 0; a < part.myAttribNames.entries(); a++)
            {
                GEO_HAPIAttributeHandle &attr =
                    part.myAttribs[part.myAttribNames[a]];

                // We only need prim attributes
                if (attr->myOwner != HAPI_ATTROWNER_PRIM &&
                    attr->myName != theIdsAttrib)
                    continue;

                /*
                if (piPart.myAttribs.contains(part.myAttribNames[a]))
                {
                    piPart.myAttribs[part.myAttribNames[a]]->concatAttrib(attr);
                }
                else
                {
                    piPart.myAttribNames.append(part.myAttribNames[a]);
                    piPart.myAttribs[part.myAttribNames[a]].reset(new
                GEO_HAPIAttribute(*(attr.get())));
                }
                */

                if (!attribsMap.contains(part.myAttribNames[a]))
                {
                    piPart.myAttribNames.append(part.myAttribNames[a]);
                }

                attribsMap[part.myAttribNames[a]].emplace_back(
                    new GEO_HAPIAttribute(*attr));
            }
        }
    }

    // Fill the part with PointInstancer attributes
    for (exint i = 0, n = piPart.myAttribNames.entries(); i < n; i++)
    {
        UT_StringRef &name = piPart.myAttribNames[i];
        piPart.myAttribs[name].reset(
            GEO_HAPIAttribute::concatAttribs(attribsMap[name]));
    }

    // Apply attributes

    // Proto Indices
    GEO_FileProp *prop = piPrim.addProperty(
        UsdGeomTokens->protoIndices, SdfValueTypeNames->IntArray,
        new GEO_FilePropConstantArraySource<int>(protoIndices));
    prop->setValueIsDefault(options.myTopologyHandling ==
                            GEO_USD_TOPOLOGY_STATIC);

    // Transform attributes
    VtVec3fArray positions, scales;
    VtQuathArray orientations;
    GEOdecomposeTransforms(xforms, positions, orientations, scales);

    const bool xFormDefault = GEOhasStaticPackedXform(options);
    prop = piPrim.addProperty(
        UsdGeomTokens->positions, SdfValueTypeNames->Point3fArray,
        new GEO_FilePropConstantSource<VtVec3fArray>(positions));
    prop->setValueIsDefault(xFormDefault);

    prop = piPrim.addProperty(
        UsdGeomTokens->orientations, SdfValueTypeNames->QuathArray,
        new GEO_FilePropConstantSource<VtQuathArray>(orientations));
    prop->setValueIsDefault(xFormDefault);

    prop = piPrim.addProperty(
        UsdGeomTokens->scales, SdfValueTypeNames->Float3Array,
        new GEO_FilePropConstantSource<VtVec3fArray>(scales));
    prop->setValueIsDefault(xFormDefault);

    // Invisible Ids
    if (theVisibilityName.asRef().multiMatch(options.myAttribs))
    {
        // If we're authoring ids, then we need to use the id of each
        // instance instead of its index.
        UT_Array<exint> invisibleIds;
        if (theIdsAttrib.multiMatch(options.myAttribs) &&
            piPart.myAttribs.contains(theIdsAttrib))
        {
            GEO_HAPIAttributeHandle &idAttr = piPart.myAttribs[theIdsAttrib];

            if (idAttr->myOwner == HAPI_ATTROWNER_POINT)
            {
                invisibleIds.setCapacity(invisibleInstances.entries());
                for (exint i : invisibleInstances)
                    invisibleIds.append(idAttr->myData->getI64(i));
            }
        }

        prop = piPrim.addProperty(
            UsdGeomTokens->invisibleIds, SdfValueTypeNames->Int64Array,
            new GEO_FilePropConstantArraySource<exint>(
                !invisibleIds.isEmpty() ? invisibleIds : invisibleInstances));

        prop->setValueIsDefault(
            theVisibilityName.asRef().multiMatch(options.myStaticAttribs));
    }

    UT_ArrayStringSet processedAttribs(options.myProcessedAttribs);
    processedAttribs.insert(HAPI_ATTRIB_POSITION);

    // Point Ids
    if (piPart.checkAttrib(theIdsAttrib, options))
    {
        GEO_HAPIAttributeHandle &id = piPart.myAttribs[theIdsAttrib];

        if (id->myDataType != HAPI_STORAGETYPE_STRING)
        {
            id->convertTupleSize(1);

            piPart.applyAttrib<int64>(
                piPrim, id, UsdGeomTokens->ids, SdfValueTypeNames->Int64Array,
                processedAttribs, false, options, GT_DataArrayHandle());
        }
    }

    // Acceleration, Velocity, Angular Velocity
    piPart.setupKinematicAttributes(
        piPrim, options, GT_DataArrayHandle(), processedAttribs);
    piPart.setupAngVelAttribute(
        piPrim, options, GT_DataArrayHandle(), processedAttribs);

    // Extras
    piPart.setupExtraPrimAttributes(
        piPrim, processedAttribs, options, GT_DataArrayHandle());

    // Create an invisible scope to hold the parts' prototypes
    SdfPath protoPath = piPath.AppendChild(TfToken(thePrototypeName));
    GEO_FilePrim &protoPrim(filePrimMap[protoPath]);
    protoPrim.setPath(protoPath);
    protoPrim.setTypeName(GEO_FilePrimTypeTokens->Scope);
    protoPrim.setInitialized();

    prop = protoPrim.addProperty(
        UsdGeomTokens->visibility, SdfValueTypeNames->Token,
        new GEO_FilePropConstantSource<TfToken>(UsdGeomTokens->invisible));
    prop->setValueIsDefault(true);
    prop->setValueIsUniform(true);

    piData.madePointInstancer = true;
    piData.pointInstancerPath = piPath;
}

bool
GEO_HAPIPart::setupPrimType(GEO_FilePrim &filePrim,
                            GEO_FilePrimMap &filePrimMap,
                            const GEO_ImportOptions &options,
                            const std::string &pathName,
                            GT_DataArrayHandle &vertexIndirect)
{
    // Update transform
    UT_Matrix4D primXform = getXForm();
    GEOhapiInitXformAttrib(filePrim, primXform, options);

    GEO_HandleOtherPrims other_prim_handling = options.myOtherPrimHandling;

    if (other_prim_handling == GEO_OTHER_XFORM)
    {
        return false;
    }

    bool define = (other_prim_handling == GEO_OTHER_DEFINE);
    GEO_FileProp *prop;

    switch (myType)
    {
    case HAPI_PARTTYPE_MESH:
    {
        MeshData *meshData = UTverify_cast<MeshData *>(myData.get());
        GT_DataArrayHandle attribData;
        filePrim.setTypeName(GEO_FilePrimTypeTokens->Mesh);

        if (options.myTopologyHandling != GEO_USD_TOPOLOGY_NONE)
        {
            attribData = meshData->faceCounts;
            prop = filePrim.addProperty(
                UsdGeomTokens->faceVertexCounts, SdfValueTypeNames->IntArray,
                new GEO_FilePropAttribSource<int>(attribData));
            prop->setValueIsDefault(options.myTopologyHandling ==
                                    GEO_USD_TOPOLOGY_STATIC);

            attribData = meshData->vertices;
            if (options.myReversePolygons)
            {
                GEOhapiReversePolygons(
                    vertexIndirect, meshData->faceCounts, meshData->vertices);
                attribData = new GT_DAIndirect(vertexIndirect, attribData);
            }

            prop = filePrim.addProperty(
                UsdGeomTokens->faceVertexIndices, SdfValueTypeNames->IntArray,
                new GEO_FilePropAttribSource<int>(attribData));
            prop->addCustomData(
                HUSDgetDataIdToken(), VtValue(attribData->getDataId()));
            prop->setValueIsDefault(options.myTopologyHandling ==
                                    GEO_USD_TOPOLOGY_STATIC);

            prop = filePrim.addProperty(
                UsdGeomTokens->orientation, SdfValueTypeNames->Token,
                new GEO_FilePropConstantSource<TfToken>(
                    options.myReversePolygons ? UsdGeomTokens->rightHanded :
                                                UsdGeomTokens->leftHanded));
            prop->setValueIsDefault(true);
            prop->setValueIsUniform(true);

            // Subdivision meshes are not extracted from HAPI
            TfToken subd_scheme = UsdGeomTokens->none;

            prop = filePrim.addProperty(
                UsdGeomTokens->subdivisionScheme, SdfValueTypeNames->Token,
                new GEO_FilePropConstantSource<TfToken>(subd_scheme));
            prop->setValueIsDefault(true);
            prop->setValueIsUniform(true);
        }
        else if (options.myReversePolygons)
        {
            GEOhapiReversePolygons(
                vertexIndirect, meshData->faceCounts, meshData->vertices);
        }

        GEOsetKind(filePrim, options.myKindSchema, GEO_KINDGUIDE_LEAF);

        break;
    }

    case HAPI_PARTTYPE_CURVE:
    {
        CurveData *curve = UTverify_cast<CurveData *>(myData.get());
        GT_DataArrayHandle attribData;

        if (options.myTopologyHandling != GEO_USD_TOPOLOGY_NONE)
        {
            int order = curve->constantOrder;
            GT_DataArrayHandle curveCounts = curve->curveCounts;
            HAPI_CurveType type = curve->curveType;
            GEO_FileProp *prop = nullptr;

            bool useNurbs =
                (type == HAPI_CURVETYPE_NURBS) &&
                (options.myNurbsCurveHandling == GEO_NURBS_NURBSCURVES);

            if (useNurbs)
            {
                if (curve->hasExtractedBasisCurves)
                {
                    // Nurbs curves are supported for all orders
                    revertToOriginalCurves();
                    curveCounts = curve->curveCounts;
                }

                filePrim.setTypeName(GEO_FilePrimTypeTokens->NurbsCurves);

                exint curveCount = curve->curveCounts->entries();

                VtIntArray orders;
                orders.resize(curveCount);

                VtArray<GfVec2d> ranges;
                ranges.resize(curveCount);

                const GT_DataArrayHandle knots = curve->curveKnots;
                UT_ASSERT(knots);

                GT_Offset startOffset = 0;
                for (GT_Size i = 0; i < curveCount; ++i)
                {
                    orders[i] = curve->constantOrder ?
                                    curve->constantOrder :
                                    curve->curveOrders->getI32(i);

                    GT_Offset knotStart = startOffset;
                    GT_Offset knotEnd = knotStart +
                                        curve->curveCounts->getI32(i) +
                                        orders[i] - 1;
                    startOffset = knotEnd + 1;

                    ranges[i] = GfVec2d(
                        knots->getF64(knotStart), knots->getF64(knotEnd));
                }

                prop = filePrim.addProperty(
                    UsdGeomTokens->order, SdfValueTypeNames->IntArray,
                    new GEO_FilePropConstantSource<VtIntArray>(orders));
                prop->setValueIsDefault(true);
                prop->setValueIsUniform(true);

                prop = filePrim.addProperty(
                    UsdGeomTokens->ranges, SdfValueTypeNames->Double2Array,
                    new GEO_FilePropConstantSource<VtArray<GfVec2d>>(ranges));
                prop->setValueIsDefault(true);
                prop->setValueIsUniform(true);

                prop = filePrim.addProperty(
                    UsdGeomTokens->knots, SdfValueTypeNames->DoubleArray,
                    new GEO_FilePropAttribSource<double>(knots));
                prop->addCustomData(
                    HUSDgetDataIdToken(), VtValue(knots->getDataId()));
                prop->setValueIsDefault(true);
                prop->setValueIsUniform(true);
            }
            else
            {
                // All non-linear bezier curves can be in the same part
                // If this part has varying order, there may be some
                // cubic curves that can still be displayed
                if (order == 0 && curve->periodic == false)
                {
                    extractCubicBasisCurves();
                    order = curve->constantOrder;
                    curveCounts = curve->curveCounts;
                }

                if (order == 2 || order == 4)
                {
                    filePrim.setTypeName(GEO_FilePrimTypeTokens->BasisCurves);

                    prop = filePrim.addProperty(
                        UsdGeomTokens->type, SdfValueTypeNames->Token,
                        new GEO_FilePropConstantSource<TfToken>(
                            order == 2 ? UsdGeomTokens->linear :
                                         UsdGeomTokens->cubic));
                    prop->setValueIsDefault(true);
                    prop->setValueIsUniform(true);

                    prop = filePrim.addProperty(
                        UsdGeomTokens->basis, SdfValueTypeNames->Token,
                        new GEO_FilePropConstantSource<TfToken>(
                            GEOhapiCurveTypeToBasisToken(type)));
                    prop->setValueIsDefault(true);
                    prop->setValueIsUniform(true);

                    bool periodic = curve->periodic;
                    const TfToken &periodToken = periodic ?
                                                     UsdGeomTokens->periodic :
                                                     UsdGeomTokens->nonperiodic;
                    bool pinned = false;

                    // Houdini repeats the first point for closed beziers.
                    // USD does not expect this, so we need to remove the
                    // extra point.
                    if (order == 4 && periodic)
                    {
                        GT_DANumeric<float> *modcounts =
                            new GT_DANumeric<float>(curveCounts->entries(), 1);

                        for (GT_Size i = 0, n = curveCounts->entries(); i < n;
                             ++i)
                        {
                            modcounts->set(curveCounts->getF32(i) - 4, i);
                        }
                        curveCounts = modcounts;
                    }
                    else if (isPinned())
                    {
                        pinned = true;
                    }

                    prop = filePrim.addProperty(
                        UsdGeomTokens->wrap, SdfValueTypeNames->Token,
                        new GEO_FilePropConstantSource<TfToken>(
                            pinned ? UsdGeomTokens->pinned : periodToken));
                    prop->setValueIsDefault(true);
                    prop->setValueIsUniform(true);
                }
                else
                {
                    // Don't define unsupported curves (return false)
                    define = false;
                    break;
                }
            }

            prop = filePrim.addProperty(
                UsdGeomTokens->curveVertexCounts, SdfValueTypeNames->IntArray,
                new GEO_FilePropAttribSource<int>(curveCounts));
            prop->addCustomData(
                HUSDgetDataIdToken(), VtValue(curveCounts->getDataId()));
            prop->setValueIsDefault(options.myTopologyHandling ==
                                    GEO_USD_TOPOLOGY_STATIC);
        }

        GEOsetKind(filePrim, options.myKindSchema, GEO_KINDGUIDE_LEAF);

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

        GEOsetKind(filePrim, options.myKindSchema, GEO_KINDGUIDE_BRANCH);
        break;
    }

    case HAPI_PARTTYPE_VOLUME:
    {
        // TODO

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
                          const GT_DataArrayHandle &vertexIndirect,
                          const GT_DataArrayHandle &attribDataOverride)
{
    typedef GEO_FilePropAttribSource<DT, ComponentDT> FilePropAttribSource;
    typedef GEO_FilePropConstantArraySource<DT> FilePropConstantSource;

    GEO_FileProp *prop = nullptr;

    if (attrib->myData && !processedAttribs.contains(attrib->myName))
    {
        GT_DataArrayHandle srcAttrib = attribDataOverride ? attribDataOverride :
                                                            attrib->myData;
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
    bool applied = false; // set in the macro below

// start #define
#define APPLY_ATTRIB(usdTypeName, type, typeComp)                              \
    applyAttrib<type, typeComp>(filePrim, attrib, usdAttribName, usdTypeName,  \
                                processedAttribs, createIndicesAttrib,         \
                                options, vertexIndirect);                      \
    applied = true;
    // end #define

    // Factors that determine the property type
    HAPI_AttributeTypeInfo typeInfo = attrib->myTypeInfo;
    HAPI_StorageType storage = attrib->myDataType;
    int tupleSize = attrib->getTupleSize();

    // Specific type names
    switch (tupleSize)
    {
    case 16:
    {
        if (typeInfo == HAPI_ATTRIBUTE_TYPE_MATRIX)
        {
            APPLY_ATTRIB(
                SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
        }
        break;
    }

    case 9:
    {
        if (typeInfo == HAPI_ATTRIBUTE_TYPE_MATRIX3)
        {
            APPLY_ATTRIB(
                SdfValueTypeNames->Matrix3dArray, GfMatrix3d, fpreal64);
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
                APPLY_ATTRIB(
                    SdfValueTypeNames->TexCoord3fArray, GfVec3f, fpreal32);
            }
            else if (storage == HAPI_STORAGETYPE_FLOAT64)
            {
                APPLY_ATTRIB(
                    SdfValueTypeNames->TexCoord3dArray, GfVec3d, fpreal64);
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
                APPLY_ATTRIB(
                    SdfValueTypeNames->TexCoord2fArray, GfVec2f, fpreal32);
            }
            else if (storage == HAPI_STORAGETYPE_FLOAT64)
            {
                APPLY_ATTRIB(
                    SdfValueTypeNames->TexCoord2dArray, GfVec2d, fpreal64);
            }
        }
        break;
    }

    default:
        break;
    }

    if (!applied)
    {
        // General type names
        switch (storage)
        {
        case HAPI_STORAGETYPE_FLOAT:
        {
            if (tupleSize == 16)
            {
                APPLY_ATTRIB(
                    SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
            }
            else if (tupleSize == 9)
            {
                APPLY_ATTRIB(
                    SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
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
                APPLY_ATTRIB(
                    SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
            }
            else if (tupleSize == 9)
            {
                APPLY_ATTRIB(
                    SdfValueTypeNames->Matrix4dArray, GfMatrix4d, fpreal64);
            }
            else if (tupleSize == 4)
            {
                APPLY_ATTRIB(
                    SdfValueTypeNames->Double4Array, GfVec4d, fpreal64);
            }
            else if (tupleSize == 3)
            {
                APPLY_ATTRIB(
                    SdfValueTypeNames->Double3Array, GfVec3d, fpreal64);
            }
            else if (tupleSize == 2)
            {
                APPLY_ATTRIB(
                    SdfValueTypeNames->Double2Array, GfVec2d, fpreal64);
            }
            else if (tupleSize == 1)
            {
                APPLY_ATTRIB(
                    SdfValueTypeNames->DoubleArray, fpreal64, fpreal64);
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
                APPLY_ATTRIB(
                    SdfValueTypeNames->StringArray, std::string, std::string);
            }
            break;
        }

        default:
            break;
        }
    }

#undef APPLY_ATTRIB
}

void
GEO_HAPIPart::setupExtraPrimAttributes(GEO_FilePrim &filePrim,
                                       UT_ArrayStringSet &processedAttribs,
                                       const GEO_ImportOptions &options,
                                       const GT_DataArrayHandle &vertexIndirect)
{
    static const std::string thePrimvarPrefix("primvars:");

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
GEO_HAPIPart::checkAttrib(const UT_StringHolder &attribName,
                          const GEO_ImportOptions &options)
{
    return (myAttribs.contains(attribName) && options.multiMatch(attribName));
}

void
GEO_HAPIPart::setupBoundsAttribute(GEO_FilePrim &filePrim,
                                   const GEO_ImportOptions &options,
                                   const GT_DataArrayHandle &vertexIndirect,
                                   UT_ArrayStringSet &processedAttribs)
{
    const UT_StringHolder &boundsName = theBoundsName.asHolder();
    if (!processedAttribs.contains(boundsName) &&
        (boundsName.multiMatch(options.myAttribs)))
    {
        UT_BoundingBoxR bbox = getBounds();

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
}

void
GEO_HAPIPart::setupColorAttributes(GEO_FilePrim &filePrim,
                                   const GEO_ImportOptions &options,
                                   const GT_DataArrayHandle &vertexIndirect,
                                   UT_ArrayStringSet &processedAttribs)
{
    static const UT_StringHolder &theColorAttrib(GA_Names::Cd);
    static const UT_StringHolder &theAlphaAttrib(GA_Names::Alpha);

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
            if (col->getTupleSize() >= 4)
            {
                if (!myAttribs.contains(theAlphaAttrib))
                {
                    // Make alpha attrib
                    GT_DANumeric<float> *alphas = new GT_DANumeric<float>(
                        col->entries(), 1);

                    for (exint i = 0; i < col->entries(); i++)
                    {
                        fpreal32 aVal = col->myData->getF32(i, 3);
                        alphas->set(aVal, i);
                    }

                    GEO_HAPIAttributeHandle a(
                        new GEO_HAPIAttribute(theAlphaAttrib, col->myOwner,
                                              HAPI_STORAGETYPE_FLOAT, alphas));

                    // Add the alpha attribute
                    myAttribs[theAlphaAttrib].swap(a);

                    UT_ASSERT(!a.get());
                }
            }

            col->convertTupleSize(3, GEO_FillMethod::Hold);

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
}

void
GEO_HAPIPart::setupCommonAttributes(GEO_FilePrim &filePrim,
                                    const GEO_ImportOptions &options,
                                    const GT_DataArrayHandle &vertexIndirect,
                                    UT_ArrayStringSet &processedAttribs)
{
    static const UT_StringHolder &thePointsAttrib(GA_Names::P);

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

    static const UT_StringHolder &theNormalsAttrib(GA_Names::N);

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

    // Color and Alpha
    setupColorAttributes(filePrim, options, vertexIndirect, processedAttribs);

    static const UT_StringHolder &theTexCoordAttrib(GA_Names::uv);

    // Texture Coordinates (UV/ST)
    if (checkAttrib(theTexCoordAttrib, options) && options.myTranslateUVToST)
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
                        filePrim, tex, stToken,
                        SdfValueTypeNames->TexCoord2fArray, processedAttribs,
                        true, options, vertexIndirect);
                }
                else // tex->myDataType == HAPI_STORAGETYPE_FLOAT64
                {
                    applyAttrib<GfVec2d, double>(
                        filePrim, tex, stToken,
                        SdfValueTypeNames->TexCoord2dArray, processedAttribs,
                        true, options, vertexIndirect);
                }
            }
        }
    }
}

void
GEO_HAPIPart::setupAngVelAttribute(GEO_FilePrim &filePrim,
                                   const GEO_ImportOptions &options,
                                   const GT_DataArrayHandle &vertexIndirect,
                                   UT_ArrayStringSet &processedAttribs)
{
    static const UT_StringHolder &theAngularVelocityAttrib(GA_Names::w);

    // Angular Velocity
    if (checkAttrib(theAngularVelocityAttrib, options))
    {
        GEO_HAPIAttributeHandle &w = myAttribs[theAngularVelocityAttrib];

        if (w->myDataType != HAPI_STORAGETYPE_STRING)
        {
            w->convertTupleSize(3);

            // w is in radians/second, but a point instancer's angular velocity
            // is in degrees/second
            GT_DataArrayHandle wInDegrees = GEOconvertRadToDeg(w->myData);

            applyAttrib<GfVec3f, float>(
                filePrim, w, UsdGeomTokens->angularVelocities,
                SdfValueTypeNames->Vector3fArray, processedAttribs, false,
                options, vertexIndirect, wInDegrees);
        }
    }
}

// Velocity and Acceleration
void
GEO_HAPIPart::setupKinematicAttributes(GEO_FilePrim &filePrim,
                                       const GEO_ImportOptions &options,
                                       const GT_DataArrayHandle &vertexIndirect,
                                       UT_ArrayStringSet &processedAttribs)
{
    static const UT_StringHolder &theVelocityAttrib(GA_Names::v);

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

    static const UT_StringHolder &theAccelAttrib(GA_Names::accel);

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
}

void
GEO_HAPIPart::setupVisibilityAttribute(GEO_FilePrim &filePrim,
                                       const GEO_ImportOptions &options,
                                       UT_ArrayStringSet &processedAttribs)
{
    static constexpr UT_StringLit theVisibilityAttrib("usdvisibility");
    if (myAttribs.contains(theVisibilityAttrib.asHolder()) &&
        theVisibilityName.asRef().multiMatch(options.myAttribs))
    {
        GEO_HAPIAttributeHandle &vis =
            myAttribs[theVisibilityAttrib.asHolder()];

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
}

void
GEO_HAPIPart::setupPointSizeAttribute(GEO_FilePrim &filePrim,
                                      const GEO_ImportOptions &options,
                                      const GT_DataArrayHandle &vertexIndirect,
                                      UT_ArrayStringSet &processedAttribs)
{
    UT_StringHolder widthAttrib = "widths"_sh;
    fpreal widthScale = 1.0;
    if (!checkAttrib(widthAttrib, options))
    {
        widthAttrib = GA_Names::width;
    }
    if (!checkAttrib(widthAttrib, options))
    {
        // pscale represents radius, but widths represents diameter
        widthAttrib = GA_Names::pscale;
        widthScale = 2.0;
    }
    if (checkAttrib(widthAttrib, options))
    {
        GEO_HAPIAttributeHandle &w = myAttribs[widthAttrib];

        if (w->myDataType != HAPI_STORAGETYPE_STRING)
        {
            w->convertTupleSize(1);

            GT_DataArrayHandle adjustedWidths = GEOscaleWidthsAttrib(
                w->myData, widthScale);

            applyAttrib<float>(filePrim, w, UsdGeomTokens->widths,
                               SdfValueTypeNames->FloatArray, processedAttribs,
                               false, options, vertexIndirect, adjustedWidths);
        }
    }
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
    setupBoundsAttribute(filePrim, options, vertexIndirect, processedAttribs);

    // Points, Normals, Texture, Color
    setupCommonAttributes(filePrim, options, vertexIndirect, processedAttribs);

    // Visibility
    setupVisibilityAttribute(filePrim, options, processedAttribs);

    // Velocity, Acceleration
    setupKinematicAttributes(
        filePrim, options, vertexIndirect, processedAttribs);

    // Point Size
    setupPointSizeAttribute(
        filePrim, options, vertexIndirect, processedAttribs);

    // Extra Attributes
    setupExtraPrimAttributes(
        filePrim, processedAttribs, options, vertexIndirect);
}

void
GEO_HAPIPart::createInstancePart(GEO_HAPIPart &partOut, exint attribIndex)
{
    partOut.myAttribNames.clear();
    partOut.myAttribs.clear();

    for (exint i = 0; i < myAttribNames.size(); i++)
    {
        GEO_HAPIAttributeHandle &attr = myAttribs[myAttribNames[i]];

        if (attr->myOwner == HAPI_ATTROWNER_PRIM)
        {
            GEO_HAPIAttributeHandle newAttr;
            attr->createElementIndirect(attribIndex, newAttr);
            partOut.myAttribNames.append(myAttribNames[i]);
            partOut.myAttribs[myAttribNames[i]].swap(newAttr);

            UT_ASSERT(!newAttr.get());
        }
    }
}
