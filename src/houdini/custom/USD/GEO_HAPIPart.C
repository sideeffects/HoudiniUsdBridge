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

#include "GEO_FilePrimInstancerUtils.h"
#include "GEO_HAPIPart.h"
#include "GEO_SharedUtils.h"
#include <GT/GT_CountArray.h>
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DASubArray.h>
#include <GU/GU_PrimVDB.h>
#include <GU/GU_PrimVolume.h>
#include <HUSD/HUSD_HydraField.h>
#include <HUSD/XUSD_Utils.h>
#include <UT/UT_Algorithm.h>
#include <UT/UT_Assert.h>
#include <UT/UT_VarEncode.h>
#include <gusd/GT_PackedUSD.h>
#include <gusd/GU_USD.h>
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

static inline GEO_VolumeVis
hapiToGeoVolumeVis(HAPI_VolumeVisualType type)
{
    switch (type)
    {
    case HAPI_VOLUMEVISTYPE_RAINBOW:
        return GEO_VOLUMEVIS_RAINBOW;

    case HAPI_VOLUMEVISTYPE_ISO:
        return GEO_VOLUMEVIS_ISO;

    case HAPI_VOLUMEVISTYPE_INVISIBLE:
        return GEO_VOLUMEVIS_INVISIBLE;

    case HAPI_VOLUMEVISTYPE_HEIGHTFIELD:
        return GEO_VOLUMEVIS_HEIGHTFIELD;

    case HAPI_VOLUMEVISTYPE_SMOKE:
    default:
        return GEO_VOLUMEVIS_SMOKE;
    }
}

static UT_StringHolder
hapiGetStringFromAttrib(
        const UT_StringMap<GEO_HAPIAttributeHandle>
                attribs[HAPI_ATTROWNER_MAX],
        const UT_StringRef &attrib_name)
{
    for (int owner = 0; owner < HAPI_ATTROWNER_MAX; ++owner)
    {
        auto it = attribs[owner].find(attrib_name);
        if (it == attribs[owner].end())
            continue;

        const GEO_HAPIAttributeHandle &attrib = it->second;
        if (attrib->myDataType != HAPI_STORAGETYPE_STRING)
            return UT_StringHolder::theEmptyString;

        return attrib->myData->getS(0);
    }

    return UT_StringHolder::theEmptyString;
}

static TfToken
hapiGetTokenFromAttrib(
        const UT_StringMap<GEO_HAPIAttributeHandle> attribs[HAPI_ATTROWNER_MAX],
        const UT_StringRef &attrib_name)
{
    UT_StringHolder value = hapiGetStringFromAttrib(attribs, attrib_name);
    return value ? TfToken(value) : TfToken();
}

static SYS_FORCE_INLINE bool
hapiIsFloatAttrib(HAPI_StorageType storage)
{
    return storage == HAPI_STORAGETYPE_FLOAT
           || storage == HAPI_STORAGETYPE_FLOAT64;
}

static SYS_FORCE_INLINE bool
hapiIsIntAttrib(HAPI_StorageType storage)
{
    return storage == HAPI_STORAGETYPE_INT8
           || storage == HAPI_STORAGETYPE_INT16
           || storage == HAPI_STORAGETYPE_INT
           || storage == HAPI_STORAGETYPE_INT64;
}

//
// GEO_HAPIPart
//

GEO_HAPIPart::GEO_HAPIPart() : myType(HAPI_PARTTYPE_INVALID) {}

GEO_HAPIPart::~GEO_HAPIPart() {}

bool
GEO_HAPIPart::loadPartData(
        const HAPI_Session &session,
        HAPI_GeoInfo &geo,
        HAPI_PartInfo &part,
        UT_WorkBuffer &buf,
        GU_DetailHandle &gdh)
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
        mData->numPoints = part.pointCount;

        int numFaces = part.faceCount;
        int numVertices = part.vertexCount;

        if (numFaces > 0)
        {
            GT_DANumeric<int> *faceCounts = new GT_DANumeric<int>(numFaces, 1);
            mData->faceCounts = faceCounts;

            ENSURE_SUCCESS(
                    HAPI_GetFaceCounts(
                            &session, geo.nodeId, part.id, faceCounts->data(),
                            0, numFaces),
                    session);
        }
        else
        {
            mData->faceCounts.reset();
        }

        if (numVertices > 0)
        {
            GT_DANumeric<int> *vertices = new GT_DANumeric<int>(numVertices, 1);
            mData->vertices = vertices;

            ENSURE_SUCCESS(
                    HAPI_GetVertexList(
                            &session, geo.nodeId, part.id, vertices->data(), 0,
                            numVertices),
                    session);
        }
        else
        {
            mData->vertices.reset();
        }

        // Set the allowed owners of extra attribs
        mData->extraOwners.clear();
        mData->extraOwners.append(HAPI_ATTROWNER_VERTEX);
        mData->extraOwners.append(HAPI_ATTROWNER_POINT);
        mData->extraOwners.append(HAPI_ATTROWNER_PRIM);
        mData->extraOwners.append(HAPI_ATTROWNER_DETAIL);

        break;
    }

    case HAPI_PARTTYPE_CURVE:
    {
        myData.reset(new CurveData);
        CurveData *cData = UTverify_cast<CurveData *>(myData.get());
        HAPI_CurveInfo cInfo;

        ENSURE_SUCCESS(
                HAPI_GetCurveInfo(&session, geo.nodeId, part.id, &cInfo),
                session);

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
                    HAPI_GetCurveCounts(
                            &session, geo.nodeId, part.id, curveCounts->data(),
                            0, numCurves),
                    session);

            // If the order varies between curves
            if (!cData->constantOrder)
            {
                GT_DANumeric<int> *curveOrders = new GT_DANumeric<int>(
                        numCurves, 1);
                cData->curveOrders = curveOrders;

                ENSURE_SUCCESS(
                        HAPI_GetCurveOrders(
                                &session, geo.nodeId, part.id,
                                curveOrders->data(), 0, numCurves),
                        session);
            }
        }
        else
        {
            cData->curveCounts.reset();
        }

        if (numKnots > 0)
        {
            GT_DANumeric<float> *curveKnots = new GT_DANumeric<float>(
                    numKnots, 1);
            cData->curveKnots = curveKnots;

            ENSURE_SUCCESS(
                    HAPI_GetCurveKnots(
                            &session, geo.nodeId, part.id, curveKnots->data(),
                            0, numKnots),
                    session);
        }

        // Set the allowed owners of extra attribs
        // This differs from SOP Import: in GT, curves only have vertex
        // attributes, but HAPI hides this and returns them as point
        // attributes.
        cData->extraOwners.clear();
        cData->extraOwners.append(HAPI_ATTROWNER_POINT);
        cData->extraOwners.append(HAPI_ATTROWNER_PRIM);
        cData->extraOwners.append(HAPI_ATTROWNER_DETAIL);

        break;
    }

    case HAPI_PARTTYPE_VOLUME:
    {
        myData.reset(new VolumeData);
        VolumeData *vData = UTverify_cast<VolumeData *>(myData.get());
        HAPI_VolumeInfo vInfo;

        ENSURE_SUCCESS(
                HAPI_GetVolumeInfo(&session, geo.nodeId, part.id, &vInfo),
                session);

        CHECK_RETURN(GEOhapiExtractString(session, vInfo.nameSH, buf));
        vData->name = buf.buffer();

        // Get bounding box
        UT_BoundingBoxF &bbox = vData->bbox;
        ENSURE_SUCCESS(
                HAPI_GetVolumeBounds(
                        &session, geo.nodeId, part.id, &bbox.vals[0][0],
                        &bbox.vals[1][0], &bbox.vals[2][0], &bbox.vals[0][1],
                        &bbox.vals[1][1], &bbox.vals[2][1], nullptr, nullptr,
                        nullptr),
                session);

        // Shears are ignored
        vInfo.transform.shear[0] = 0.f;
        vInfo.transform.shear[1] = 0.f;
        vInfo.transform.shear[2] = 0.f;

        vData->volumeType = vInfo.type;

        if (!gdh)
        {
            GU_Detail *gdp = new GU_Detail();
            gdh.allocateAndSet(gdp);
        }

        // Add a volume/vdb primitive to the detail. This detail will be used
        // when rendering the volume
        vData->gdh = gdh;
        GU_DetailHandleAutoWriteLock lock(gdh);
        CHECK_RETURN(lock.isValid());
        GU_Detail *gdp = lock.getGdp();

        if (vInfo.type == HAPI_VOLUMETYPE_HOUDINI)
        {
            GEO_PrimVolume *prim = GU_PrimVolume::build(gdp);

            // Update taper
            if (vInfo.hasTaper)
            {
                prim->setTaperX(vInfo.xTaper);
                prim->setTaperY(vInfo.yTaper);
            }

            // Set the volume transform and position
            UT_Matrix3F xform;
            xform.identity();
            xform.scale(bbox.sizeX() / 2, bbox.sizeY() / 2, bbox.sizeZ() / 2);
            prim->setTransform(xform);

            // Get the visualization info to properly display this volume
            HAPI_VolumeVisualInfo vis;
            ENSURE_SUCCESS(
                    HAPI_GetVolumeVisualInfo(
                            &session, geo.nodeId, part.id, &vis),
                    session);

            prim->setVisualization(
                    hapiToGeoVolumeVis(vis.type), vis.iso, vis.density);

            gdp->setPos3(prim->getMapOffset(), bbox.center());

            // Set voxel values
            CHECK_RETURN(GEOhapiExtractVoxelValues(
                    prim, session, geo.nodeId, part.id, vInfo));

            vData->fieldIndex = prim->getMapIndex();
        }
        else // vInfo.type == HAPI_VOLUMETYPE_VDB
        {
            // Author a grid containing the voxel values from Houdini Engine
            openvdb::GridBase::Ptr grid;

            CHECK_RETURN(GEOhapiInitVDBGrid(
                    grid, session, geo.nodeId, part.id, vInfo));

            grid->setTransform(
                    openvdb::math::Transform::createLinearTransform());

            GU_PrimVDB *prim = GU_PrimVDB::buildFromGrid(
                    *gdp, grid, nullptr, vData->name.c_str());

            // prim->setVisOptions(GEO_VolumeOptions(GEO_VOLUMEVIS_ISO));

            // Configure transform
            GEO_PrimVolumeXform xform;
            xform.init();
            xform.myHasTaper = vInfo.hasTaper;
            xform.myTaperX = vInfo.xTaper;
            xform.myTaperY = vInfo.yTaper;

            UT_QuaternionD q(vInfo.transform.rotationQuaternion);
            UT_Matrix3 matrix;
            q.getRotationMatrix(matrix);
            UT_Matrix3 scale(1);
            scale.scale(
                    vInfo.transform.scale[0], vInfo.transform.scale[1],
                    vInfo.transform.scale[2]);
            matrix = matrix * scale;
            xform.myXform = matrix;
            matrix.invert();
            xform.myInverseXform = matrix;
            xform.myCenter.x() = vInfo.transform.position[0];
            xform.myCenter.y() = vInfo.transform.position[1];
            xform.myCenter.z() = vInfo.transform.position[2];

            prim->setSpaceTransform(xform, UT_Vector3R(1, 1, 1));

            // Get the visualization info to properly display this volume
            HAPI_VolumeVisualInfo vis;
            ENSURE_SUCCESS(
                    HAPI_GetVolumeVisualInfo(
                            &session, geo.nodeId, part.id, &vis),
                    session);

            prim->setVisualization(
                    hapiToGeoVolumeVis(vis.type), vis.iso, vis.density);

            vData->fieldIndex = prim->getMapIndex();
        }

        if (!gdh)
        {
            TF_WARN("Unable to load geometry");
            return false;
        }

        // Set the allowed owners of extra attribs
        vData->extraOwners.clear();
        vData->extraOwners.append(HAPI_ATTROWNER_PRIM);

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
                        &session, geo.nodeId, part.id, instanceIds.get(), 0,
                        partCount),
                session);

        iData->instances.setSize(partCount);
        HAPI_PartInfo partInfo;

        for (int i = 0; i < partCount; i++)
        {
            ENSURE_SUCCESS(
                    HAPI_GetPartInfo(
                            &session, geo.nodeId, instanceIds.get()[i],
                            &partInfo),
                    session);

            CHECK_RETURN(iData->instances[i].loadPartData(
                    session, geo, partInfo, buf, gdh));
        }

        int instanceCount = part.instanceCount;
        UT_UniquePtr<HAPI_Transform> hapiXforms(
                new HAPI_Transform[instanceCount]);
        ENSURE_SUCCESS(
                HAPI_GetInstancerPartTransforms(
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
                HAPI_GetSphereInfo(&session, geo.nodeId, part.id, &sInfo),
                session);

        for (int i = 0; i < 3; i++)
        {
            sData->center[i] = sInfo.center[i];
        }

        sData->radius = sInfo.radius;

        // Set the allowed owners of extra attribs
        sData->extraOwners.clear();
        sData->extraOwners.append(HAPI_ATTROWNER_DETAIL);

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
            &part.attributeCounts[0],
            &part.attributeCounts[HAPI_ATTROWNER_MAX]);

    UT_UniquePtr<HAPI_StringHandle> sHandleUnique(
            new HAPI_StringHandle[greatestCount]);

    HAPI_StringHandle *handles = sHandleUnique.get();
    HAPI_AttributeInfo attrInfo;

    // Iterate through all owners to get all attributes
    for (int i = 0; i < HAPI_ATTROWNER_MAX; i++)
    {
        auto owner = static_cast<HAPI_AttributeOwner>(i);
        if (part.attributeCounts[i] > 0)
        {
            ENSURE_SUCCESS(
                    HAPI_GetAttributeNames(
                            &session, geo.nodeId, part.id,
                            owner, handles,
                            part.attributeCounts[i]),
                    session);

            auto &&attrib_map = myAttribs[owner];
            UT_StringArray &attrib_names = myAttribNames[owner];

            for (int j = 0; j < part.attributeCounts[i]; j++)
            {
                CHECK_RETURN(GEOhapiExtractString(session, handles[j], buf));

                ENSURE_SUCCESS(
                        HAPI_GetAttributeInfo(
                                &session, geo.nodeId, part.id, buf.buffer(),
                                owner, &attrInfo),
                        session);

                UT_StringHolder attribName(buf.buffer());

                if (!attrInfo.exists)
                    continue;

                UT_ASSERT(!attrib_map.contains(attribName));
                exint nameIndex = attrib_names.append(attribName);
                GEO_HAPIAttributeHandle attrib(new GEO_HAPIAttribute);

                CHECK_RETURN(attrib->loadAttrib(
                        session, geo, part, owner, attrInfo, attribName, buf));

                // Add the loaded attribute to our string map
                attrib_map[attrib_names[nameIndex]].swap(attrib);

                UT_ASSERT(!attrib.get());
            }

            // Sort the names to keep the order consistent
            // when there are small changes to the list
            attrib_names.sort(true, false);
        }
    }

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

        bbox.setBounds(
                -1, -1, -1, // Min
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

        if (myAttribs[HAPI_ATTROWNER_POINT].contains(HAPI_ATTRIB_POSITION))
        {
            const GEO_HAPIAttributeHandle &points
                    = myAttribs[HAPI_ATTROWNER_POINT].at(HAPI_ATTRIB_POSITION);

            // Points attribute should be a float type
            if (hapiIsFloatAttrib(points->myDataType))
            {
                // Make sure points are 3 dimensions
                points->convertTupleSize(3);

                GT_DataArray *xyz = points->myData.get();

                for (int i = 0; i < points->entries(); i++)
                {
                    bbox.enlargeBounds(
                            xyz->getF32(i, 0), xyz->getF32(i, 1),
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
        for (auto &&[_, attrib] : myAttribs[HAPI_ATTROWNER_PRIM])
        {
            attrib->myData = UTmakeIntrusive<GT_DAIndirect>(
                    cubics, attrib->myData);
        }

        for (auto &&[_, attrib] : myAttribs[HAPI_ATTROWNER_POINT])
        {
            attrib->myData = UTmakeIntrusive<GT_DAIndirect>(
                    vertexRemap, attrib->myData);
        }
    }
}

void
GEO_HAPIPart::fixCurveEndInterpolation()
{
    CurveData *curve = UTverify_cast<CurveData *>(myData.get());
    if (curve->hasFixedEndInterpolation)
        return;

    const int order = curve->constantOrder;

    // Only modify sets of cubic NURBS curves
    if (order != 4 || curve->curveType != HAPI_CURVETYPE_NURBS
        || curve->periodic || !curve->curveKnots)
    {
        return;
    }

    GT_Size numCurves = curve->curveCounts->entries();

    // Check knot values
    const GT_DataArrayHandle knots = curve->curveKnots;
    GT_Offset startOffset = 0;
    for (GT_Size curveIndex = 0; curveIndex < numCurves; curveIndex++)
    {
        const GT_Offset knotStart = startOffset;
        const GT_Size knotCount = curve->curveCounts->getI64(curveIndex)
                                  + order;
        // Update offset for next curve
        startOffset += knotCount;

        fpreal64 knotVal = knots->getF64(knotStart);
        for (GT_Size i = 1; i < order; i++)
        {
            if (!SYSisEqual(knots->getF64(knotStart + i), knotVal))
                return;
        }

        knotVal = knots->getF64(startOffset - 1);
        for (GT_Size i = knotCount - order; i < knotCount - 1; i++)
        {
            if (!SYSisEqual(knots->getF64(knotStart + i), knotVal))
                return;
        }
    }

    // TODO - this could all be replaced by setting 'wrap' to 'pinned' once
    // Hydra supports that.
    const GT_DataArrayHandle &src_counts = curve->curveCounts;
    UT_IntrusivePtr<GT_Int32Array> new_counts = new GT_Int32Array(numCurves, 1);

    // Add copies of the end vertices.
    static constexpr exint num_copies = 2;
    exint total_verts = 0;
    for (GT_Size i = 0; i < numCurves; ++i)
    {
        exint num_verts = src_counts->getI32(i) + num_copies * 2;
        new_counts->set(num_verts, i);
        total_verts += num_verts;
    }

    UT_IntrusivePtr<GT_Int64Array> indirect = new GT_Int64Array(total_verts, 1);

    // Generate an indirect array of point indices to duplicate the attribute
    // values for the new vertices.
    exint src_idx = 0;
    exint dst_idx = 0;
    for (GT_Size i = 0; i < numCurves; ++i)
    {
        // Add the start point and its copies.
        for (exint j = 0; j <= num_copies; ++j)
            indirect->set(src_idx, dst_idx++);

        ++src_idx;

        for (exint j = 1; j < src_counts->getI32(i) - 1; ++j)
            indirect->set(src_idx++, dst_idx++);

        // Add the end point and its copies.
        for (exint j = 0; j <= num_copies; ++j)
            indirect->set(src_idx, dst_idx++);

        ++src_idx;
    }

    UT_ASSERT(dst_idx == total_verts);

    // Update attribs to use the new indirect array.
    for (auto &&[_, attrib] : myAttribs[HAPI_ATTROWNER_POINT])
    {
        attrib->myData = UTmakeIntrusive<GT_DAIndirect>(
                indirect, attrib->myData);
    }

    curve->curveCounts = new_counts;
    curve->hasFixedEndInterpolation = true;
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

        for (auto &&[_, attrib] : myAttribs[HAPI_ATTROWNER_POINT])
        {
            indirect = UTverify_cast<GT_DAIndirect *>(attrib->myData.get());
            attrib->myData = indirect->referencedData();
        }
        for (auto &&[_, attrib] : myAttribs[HAPI_ATTROWNER_PRIM])
        {
            indirect = UTverify_cast<GT_DAIndirect *>(attrib->myData.get());
            attrib->myData = indirect->referencedData();
        }
    }
}

/////////////////////////////////////////////
// HAPI to USD functions
//

PXR_NAMESPACE_USING_DIRECTIVE

static constexpr UT_StringLit theBoundsName("bounds");
static constexpr UT_StringLit theVisibilityName("visibility");
static constexpr UT_StringLit theVolumePathAttribName("usdvolumepath");
static constexpr UT_StringLit theVolumeSavePathName("usdvolumesavepath");
static constexpr UT_StringLit theInstancerPathAttrib("usdinstancerpath");

void
GEO_HAPISharedData::initRelationships(GEO_FilePrimMap &filePrimMap)
{
    if (madePointInstancer)
    {
        UT_ASSERT(!pointInstancerPath.IsEmpty());
        GEO_FilePrim &piPrim = filePrimMap[pointInstancerPath];

        piPrim.addRelationship(UsdGeomTokens->prototypes, protoPaths);
    }
}

void
GEO_HAPIPart::partToPrim(
        GEO_HAPIPart &part,
        const GEO_ImportOptions &options,
        const SdfPath &parentPath,
        GEO_FilePrimMap &filePrimMap,
        const std::string &pathName,
        GEO_HAPIPrimCounts &counts,
        GEO_HAPISharedData &sharedData,
        const UT_Matrix4D *parentXform)
{
    if (part.isInstancer())
    {
        // Instancers need to set up their instances
        part.setupInstances(
                parentPath, filePrimMap, pathName, options, counts, sharedData);
    }
    else
    {
        auto primSetup = [&](GEO_HAPIPart &partToSetup) {
            SdfPath path;

            if (part.getType() == HAPI_PARTTYPE_VOLUME)
            {
                path = getVolumeCollectionPath(
                        partToSetup, parentPath, options, counts, sharedData);
            }
            else
            {
                // Use point partition attributes for a points-only prim, and a
                // prim attribute otherwise.
                HAPI_AttributeOwner owner = HAPI_ATTROWNER_PRIM;
                if (part.getType() == HAPI_PARTTYPE_MESH)
                {
                    auto mesh = UTverify_cast<const MeshData *>(
                            part.myData.get());
                    if (mesh->isOnlyPoints())
                        owner = HAPI_ATTROWNER_POINT;
                }

                path = GEOhapiGetPrimPath(
                        partToSetup, owner, parentPath, counts, options);
            }

            GEO_FilePrim &filePrim(filePrimMap[path]);
            filePrim.setPath(path);

            // For index remapping
            GT_DataArrayHandle indirectVertices;

            // adjust type-specific properties
            bool define = partToSetup.setupPrimType(
                    filePrim, filePrimMap, options, pathName, indirectVertices,
                    sharedData, parentXform);

            filePrim.setIsDefined(define);
            filePrim.setInitialized();
        };

        // Check if this part can be split up into many parts by name
        GEO_HAPIPartArray partArray;
        if (part.splitPartsByName(partArray, options))
        {
            for (int i = 0, n = partArray.entries(); i < n; i++)
            {
                primSetup(partArray(i));
            }
        }
        else
        {
            primSetup(part);
        }
    }
}

static constexpr UT_StringLit thePrototypeName("Prototypes");
static constexpr UT_StringLit thePointInstancerName("instances");

void
GEO_HAPIPart::setupInstances(
        const SdfPath &parentPath,
        GEO_FilePrimMap &filePrimMap,
        const std::string &pathName,
        const GEO_ImportOptions &options,
        GEO_HAPIPrimCounts &counts,
        GEO_HAPISharedData &piData)
{
    UT_ASSERT(isInstancer());
    InstanceData *iData = UTverify_cast<InstanceData *>(myData.get());
    static const std::string theInstanceSuffix = "obj_";

    // Apply the attributes on the instancer to the child transform
    auto processChildAttributes = [&](GEO_FilePrim &xformPrim,
                                      GEO_HAPIPart &childPart) {
        UT_ArrayStringSet processedAttribs(options.myProcessedAttribs);

        // We don't want the positions attributes
        processedAttribs.insert(GA_Names::P);

        childPart.setupColorAttributes(
                xformPrim, options, GT_DataArrayHandle(), processedAttribs);
        childPart.setupExtraPrimAttributes(
                xformPrim, options, GT_DataArrayHandle(), processedAttribs);
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
                    std::string suffix = theInstanceSuffix
                                         + std::to_string(counts.prototypes++);
                    childProtoIndex = objPaths.append(
                            protoPath.AppendChild(TfToken(suffix)));
                }

                partToPrim(
                        iData->instances[i], options, objPaths[childProtoIndex],
                        filePrimMap, pathName, childProtoCounts, piData);
            }
            else
            {
                // Create the part under a transform
                std::string suffix = theInstanceSuffix
                                     + std::to_string(counts.prototypes++);
                objPaths.append(protoPath.AppendChild(TfToken(suffix)));

                // Make a new primcounts struct to keep track of what's under
                // this transform
                GEO_HAPIPrimCounts childCounts;
                partToPrim(
                        iData->instances[i], options, objPaths[i], filePrimMap,
                        pathName, childCounts, piData);
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
                        tempPart, HAPI_ATTROWNER_PRIM, parentPath, counts,
                        options);
                GEO_FilePrim &refPrim(filePrimMap[refPath]);
                refPrim.setPath(refPath);
                refPrim.setTypeName(GEO_FilePrimTypeTokens->Xform);
                refPrim.addMetadata(SdfFieldKeys->Instanceable, VtValue(true));

                // Make this a reference of the corresponding prototype
                GEOinitInternalReference(refPrim, objPaths[objInd]);

                // Apply the corresponding transform
                GEOinitXformAttrib(
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
        SdfPath protoPath = piData.pointInstancerPath.AppendChild(
                TfToken(thePrototypeName));

        SdfPath childInstancerPath = SdfPath::EmptyPath();
        GEO_HAPIPrimCounts childInstancerCounts;
        GEO_HAPISharedData childInstancerData(iData->instances);

        for (exint i = 0; i < protoCount; i++)
        {
            // We want to keep all prototypes together to avoid
            // having multiple prototype scopes on the same level
            if (iData->instances[i].isInstancer())
            {
                if (childInstancerPath.IsEmpty())
                {
                    std::string suffix = theInstanceSuffix
                                         + std::to_string(counts.prototypes++);
                    childInstancerPath = protoPath.AppendChild(TfToken(suffix));
                    piData.protoPaths.push_back(childInstancerPath);
                }

                partToPrim(
                        iData->instances[i], options, childInstancerPath,
                        filePrimMap, pathName, childInstancerCounts,
                        childInstancerData);
            }
            else
            {
                // Create the part under a transform
                std::string suffix = theInstanceSuffix
                                     + std::to_string(counts.prototypes++);
                SdfPath instancePath = protoPath.AppendChild(TfToken(suffix));

                // Create structs to keep track of new level in tree
                GEO_HAPIPrimCounts childCounts;
                GEO_HAPISharedData childSharedData(iData->instances);
                partToPrim(
                        iData->instances[i], options, instancePath, filePrimMap,
                        pathName, childCounts, childSharedData);

                piData.protoPaths.push_back(instancePath);
            }
        }

        // Set up relationships of all child instancers
        childInstancerData.initRelationships(filePrimMap);
    }
    else if (options.myPackedPrimHandling == GEO_PACKED_XFORMS)
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
                                tempPart, HAPI_ATTROWNER_PRIM, parentPath, counts, options);

                        GEO_FilePrim &xformPrim(filePrimMap[childInstPath]);
                        xformPrim.setPath(childInstPath);
                        xformPrim.setTypeName(GEO_FilePrimTypeTokens->Xform);
                        GEOinitXformAttrib(
                                xformPrim, iData->instanceTransforms[transInd],
                                options);

                        // Apply attributes
                        processChildAttributes(xformPrim, tempPart);
                    }

                    // Initialize this child instancer under the transform
                    // pointed to by childInstPath
                    partToPrim(
                            iData->instances[objInd], options, childInstPath,
                            filePrimMap, pathName, childInstCounts, piData);
                }
                else
                {
                    SdfPath objPath = GEOhapiGetPrimPath(
                            tempPart, HAPI_ATTROWNER_PRIM, parentPath, counts, options);

                    GEO_FilePrim &xformPrim(filePrimMap[objPath]);
                    xformPrim.setPath(objPath);
                    xformPrim.setTypeName(GEO_FilePrimTypeTokens->Xform);

                    // Create a new counts object to keep track of children
                    GEO_HAPIPrimCounts childCounts;

                    // Create the prim
                    partToPrim(
                            iData->instances[objInd], options, objPath,
                            filePrimMap, pathName, childCounts, piData);

                    // Apply the corresponding transform
                    GEOinitXformAttrib(
                            xformPrim, iData->instanceTransforms[transInd],
                            options);

                    // Apply attributes
                    processChildAttributes(xformPrim, tempPart);
                }
            }
        }
    }
    else // GEO_PACKED_UNPACK
    {
        for (exint transInd = 0; transInd < iData->instanceTransforms.entries();
             transInd++)
        {
            for (exint objInd = 0; objInd < iData->instances.entries();
                 objInd++)
            {
                // Import without any additional Xform prims, but apply the
                // instance transform.
                partToPrim(
                        iData->instances[objInd], options, parentPath,
                        filePrimMap, pathName, counts, piData,
                        &iData->instanceTransforms[transInd]);
            }
        }
    }
}

SdfPath
GEO_HAPIPart::getVolumeCollectionPath(
        const GEO_HAPIPart &part,
        const SdfPath &parentPath,
        const GEO_ImportOptions &options,
        GEO_HAPIPrimCounts &counts,
        GEO_HAPISharedData &sharedData)
{
    UT_ASSERT(part.getType() == HAPI_PARTTYPE_VOLUME);
    SdfPath collectionPath;

    // Check if the volume path was specified
    UT_StringHolder pathFromAttrib = hapiGetStringFromAttrib(
            part.myAttribs, theVolumePathAttribName.asRef());

    if (pathFromAttrib)
    {
        collectionPath = GEOhapiNameToNewPath(pathFromAttrib, parentPath);
    }
    else
    {
        VolumeData *vData = UTverify_cast<VolumeData *>(part.myData.get());
        UT_StringHolder &fieldName(vData->name);

        // Create a new default collection path if there is a name conflict
        SdfPath &defaultPath = sharedData.defaultCollectionPath;
        UT_ArrayStringSet &names = sharedData.namesInDefaultCollection;

        if (defaultPath.IsEmpty() || names.contains(fieldName))
        {
            defaultPath = GEOhapiAppendDefaultPathName(
                    HAPI_PARTTYPE_VOLUME, parentPath, counts);
            names.clear();
            sharedData.defaultFieldNameSuffix = 0;
        }

        names.insert(fieldName);
        collectionPath = defaultPath;
    }

    return collectionPath;
}

bool
GEO_HAPIPart::isInvisible(const GEO_ImportOptions &options) const
{
    if (!theVisibilityName.asRef().multiMatch(options.myAttribs))
        return false;

    const TfToken visibility = hapiGetTokenFromAttrib(
            myAttribs, theVisibilityName.asRef());
    return visibility == UsdGeomTokens->invisible;
}

// Assumes the order of piData.siblingParts matches the order of partToPrim()
// calls with the same parts
void
GEO_HAPIPart::setupPointInstancer(
        const SdfPath &parentPath,
        GEO_FilePrimMap &filePrimMap,
        GEO_HAPISharedData &piData,
        const GEO_ImportOptions &options)
{
    static const UT_StringHolder &theIdsAttrib(GA_Names::id);

    // Determine the path of the point instancer
    SdfPath piPath;
    if (UT_StringHolder instancer_path = hapiGetStringFromAttrib(
                myAttribs, theInstancerPathAttrib.asHolder()))
    {
        piPath = GEOhapiNameToNewPath(instancer_path, parentPath);
    }

    if (piPath.IsEmpty())
        piPath = parentPath.AppendChild(TfToken(thePointInstancerName));

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
            InstanceData *iData
                    = UTverify_cast<InstanceData *>(part.myData.get());

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

            // Get the relevant prim attributes or the ids attribute
            const UT_StringArray &attrib_names = part.myAttribNames[HAPI_ATTROWNER_PRIM];
            auto &&attrib_map = part.myAttribs[HAPI_ATTROWNER_PRIM];
            for (exint a = 0; a < attrib_names.entries(); a++)
            {
                GEO_HAPIAttributeHandle &attr = attrib_map[attrib_names[a]];

                if (!attribsMap.contains(attrib_names[a]))
                {
                    piPart.myAttribNames[HAPI_ATTROWNER_PRIM].append(attrib_names[a]);
                }

                attribsMap[attrib_names[a]].emplace_back(
                        new GEO_HAPIAttribute(*attr));
            }

            if (part.myAttribs[HAPI_ATTROWNER_POINT].contains(theIdsAttrib))
            {
                if (!attribsMap.contains(theIdsAttrib))
                {
                    piPart.myAttribNames[HAPI_ATTROWNER_POINT].append(
                            theIdsAttrib);
                }

                attribsMap[theIdsAttrib].emplace_back(new GEO_HAPIAttribute(
                        *part.myAttribs[HAPI_ATTROWNER_POINT][theIdsAttrib]));
            }
        }
    }

    // Fill the part with PointInstancer attributes
    for (exint owner = 0; owner < HAPI_ATTROWNER_MAX; ++owner)
    {
        for (exint i = 0, n = piPart.myAttribNames[owner].entries(); i < n; i++)
        {
            UT_StringHolder &name = piPart.myAttribNames[owner][i];
            piPart.myAttribs[owner][name]
                    = GEO_HAPIAttribute::concatAttribs(attribsMap[name]);
        }
    }

    // Apply attributes

    // Proto Indices
    GEO_FileProp *prop = piPrim.addProperty(
            UsdGeomTokens->protoIndices, SdfValueTypeNames->IntArray,
            new GEO_FilePropConstantArraySource<int>(protoIndices));
    prop->setValueIsDefault(
            options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);

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
        if (theIdsAttrib.multiMatch(options.myAttribs)
            && piPart.myAttribs[HAPI_ATTROWNER_POINT].contains(theIdsAttrib))
        {
            GEO_HAPIAttributeHandle &idAttr = piPart.myAttribs[HAPI_ATTROWNER_POINT][theIdsAttrib];
            invisibleIds.setCapacity(invisibleInstances.entries());
            for (exint i : invisibleInstances)
                invisibleIds.append(idAttr->myData->getI64(i));
        }

        prop = piPrim.addProperty(
                UsdGeomTokens->invisibleIds, SdfValueTypeNames->Int64Array,
                new GEO_FilePropConstantArraySource<exint>(
                        !invisibleIds.isEmpty() ? invisibleIds :
                                                  invisibleInstances));

        prop->setValueIsDefault(
                theVisibilityName.asRef().multiMatch(options.myStaticAttribs));
    }

    UT_ArrayStringSet processedAttribs(options.myProcessedAttribs);
    processedAttribs.insert(GA_Names::P);

    // Point Ids
    piPart.setupPointIdsAttribute(
            piPrim, options, GT_DataArrayHandle(), processedAttribs);

    // Acceleration, Velocity, Angular Velocity
    piPart.setupKinematicAttributes(
            piPrim, options, GT_DataArrayHandle(), processedAttribs);
    piPart.setupAngVelAttribute(
            piPrim, options, GT_DataArrayHandle(), processedAttribs);

    // Extras
    piPart.setupExtraPrimAttributes(
            piPrim, options, GT_DataArrayHandle(), processedAttribs);

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

// The index refers to the primitive / point on the mesh
static UT_StringHolder
getPartNameAtIndex(
        const GEO_HAPIPart &part,
        HAPI_AttributeOwner owner,
        exint index,
        const GEO_ImportOptions &options)
{
    UT_ASSERT(index >= 0);

    const UT_StringMap<GEO_HAPIAttributeHandle> &attribs
            = part.getAttribMap(owner);

    for (exint i = 0, n = options.myPathAttrNames.entries(); i < n; i++)
    {
        const UT_StringHolder &attrName = options.myPathAttrNames(i);
        if (attribs.contains(attrName))
        {
            const GEO_HAPIAttributeHandle &attr = attribs.at(attrName);

            // Name attributes must contain strings.
            if (index < attr->myData->entries()
                && attr->myDataType == HAPI_STORAGETYPE_STRING)
            {
                const UT_StringHolder &name = attr->myData->getS(index);

                if (!name.isEmpty())
                    return name;
            }
        }
    }

    return UT_StringHolder::theEmptyString;
}

static exint
geoFindPartitions(
        const GEO_HAPIPart &part,
        HAPI_AttributeOwner owner,
        const GEO_ImportOptions &options,
        exint num_elements,
        UT_Array<exint> &element_to_partition)
{
    UT_StringMap<exint> partition_ids;
    element_to_partition.setSizeNoInit(num_elements);
    for (exint i = 0; i < num_elements; ++i)
    {
        UT_StringHolder name = getPartNameAtIndex(part, owner, i, options);
        element_to_partition[i] = UTfindOrInsert(
                partition_ids, name, [&]() { return partition_ids.size(); });
    }

    return partition_ids.size();
}

static void
geoSplitAttribs(
        const GEO_HAPIPart &src_part,
        const GT_DataArrayHandle &point_indirect,
        const GT_DataArrayHandle &vertex_indirect,
        const GT_DataArrayHandle &prim_indirect,
        UT_StringMap<GEO_HAPIAttributeHandle> split_attribs[HAPI_ATTROWNER_MAX])
{
    auto splitAttribs
            = [&](HAPI_AttributeOwner owner, const GT_DataArrayHandle &indirect)
    {
        for (auto &&[name, src_attrib] : src_part.getAttribMap(owner))
        {
            GT_DataArrayHandle split_attrib;
            if (indirect)
            {
                split_attrib = UTmakeIntrusive<GT_DAIndirect>(
                        indirect, src_attrib->myData);
            }
            else
                split_attrib = src_attrib->myData;

            split_attribs[owner][name] = UTmakeUnique<GEO_HAPIAttribute>(
                    name, src_attrib->myOwner, src_attrib->myDataType,
                    split_attrib, src_attrib->myTypeInfo);
        }
    };

    splitAttribs(HAPI_ATTROWNER_POINT, point_indirect);
    splitAttribs(HAPI_ATTROWNER_VERTEX, vertex_indirect);
    splitAttribs(HAPI_ATTROWNER_PRIM, prim_indirect);
    splitAttribs(HAPI_ATTROWNER_DETAIL, nullptr);
}

bool
GEO_HAPIPart::splitMeshByName(
        GEO_HAPIPartArray &split_parts,
        const GEO_ImportOptions &options) const
{
    UT_ASSERT(myType == HAPI_PARTTYPE_MESH);

    auto mesh_data = UTverify_cast<const MeshData *>(myData.get());
    exint num_elements = 0;
    HAPI_AttributeOwner owner = HAPI_ATTROWNER_INVALID;
    if (mesh_data->isOnlyPoints())
    {
        // If we have a points prim, split by a point name attrib.
        num_elements = mesh_data->numPoints;
        owner = HAPI_ATTROWNER_POINT;
    }
    else
    {
        num_elements = mesh_data->faceCounts->entries();
        owner = HAPI_ATTROWNER_PRIM;
    }

    if (num_elements <= 0)
        return false;

    // Find the partition id for each element.
    UT_Array<exint> element_to_partition;
    const exint num_partitions = geoFindPartitions(
            *this, owner, options, num_elements, element_to_partition);

    // No splitting is needed if there is only one partition.
    if (num_partitions <= 1)
        return false;

    struct MeshPartitionData
    {
        // Using int32 arrays because all array length values coming from
        // Houdini Engine are passed as int32. Since indirect arrays just
        // contain array indices, int32 will be large enough
        UT_IntrusivePtr<GT_Int32Array> myVertices;
        UT_IntrusivePtr<GT_Int32Array> myVertexIndirect;
        UT_IntrusivePtr<GT_Int32Array> myPrimIndirect;

        UT_IntrusivePtr<GT_Int32Array> myPointsIndirect;
        UT_Map<exint, exint> myPointIndexMap;

        MeshPartitionData()
            : myVertices(UTmakeIntrusive<GT_Int32Array>(0, 1))
            , myVertexIndirect(UTmakeIntrusive<GT_Int32Array>(0, 1))
            , myPrimIndirect(UTmakeIntrusive<GT_Int32Array>(0, 1))
            , myPointsIndirect(UTmakeIntrusive<GT_Int32Array>(0, 1))
        {
        }
    };

    // Accumulate the primitives and/or points for each partition.
    UT_Array<MeshPartitionData> partitions;
    partitions.setSize(num_partitions);
    exint vertex_idx = 0;
    for (exint i = 0; i < num_elements; ++i)
    {
        MeshPartitionData &partition = partitions[element_to_partition[i]];

        if (owner == HAPI_ATTROWNER_POINT)
            partition.myPointsIndirect->append(i);
        else
        {
            partition.myPrimIndirect->append(i);

            // Add vertices and points to the partition.
            const exint num_vertices = mesh_data->faceCounts->getI32(i);
            for (exint j = 0; j < num_vertices; ++j, ++vertex_idx)
            {
                partition.myVertexIndirect->append(vertex_idx);

                // Add the point to this split mesh if needed.
                const exint src_point_idx
                        = mesh_data->vertices->getI32(vertex_idx);
                const exint dst_point_idx = UTfindOrInsert(
                        partition.myPointIndexMap, src_point_idx,
                        [&]()
                        {
                            partition.myPointsIndirect->append(src_point_idx);
                            return partition.myPointsIndirect->entries() - 1;
                        });

                partition.myVertices->append(dst_point_idx);
            }
        }
    }

    // Finally, assemble the split parts.
    split_parts.setCapacityIfNeeded(partitions.size());
    for (const MeshPartitionData &partition : partitions)
    {
        GEO_HAPIPart &split_part = split_parts[split_parts.append()];
        split_part.myType = myType;

        split_part.myData = UTmakeUnique<MeshData>();
        auto split_data = UTverify_cast<MeshData *>(split_part.myData.get());

        split_data->numPoints = partition.myPointsIndirect->entries();
        if (owner == HAPI_ATTROWNER_PRIM)
        {
            split_data->vertices = partition.myVertices;
            split_data->faceCounts = UTmakeIntrusive<GT_DAIndirect>(
                    partition.myPrimIndirect, mesh_data->faceCounts);
        }

        // Set up the attributes for the split part.
        split_data->extraOwners = myData->extraOwners;
        geoSplitAttribs(
                *this, partition.myPointsIndirect, partition.myVertexIndirect,
                partition.myPrimIndirect, split_part.myAttribs);

        for (int owner = 0; owner < HAPI_ATTROWNER_MAX; ++owner)
            split_part.myAttribNames[owner] = myAttribNames[owner];
    }

    return true;
}

bool
GEO_HAPIPart::splitCurvesByName(
        GEO_HAPIPartArray &split_parts,
        const GEO_ImportOptions &options) const
{
    UT_ASSERT(myType == HAPI_PARTTYPE_CURVE);

    auto src_curve = UTverify_cast<const CurveData *>(myData.get());
    const exint num_curves = src_curve->curveCounts->entries();
    // Split curves by a primitive name attrib.
    const HAPI_AttributeOwner owner = HAPI_ATTROWNER_PRIM;

    if (num_curves <= 0)
        return false;

    // Find the partition id for each curve.
    UT_Array<exint> curve_to_partition;
    const exint num_partitions = geoFindPartitions(
            *this, owner, options, num_curves, curve_to_partition);

    // No splitting is needed if there is only one partition.
    if (num_partitions <= 1)
        return false;

    struct CurvePartitionData
    {
        UT_IntrusivePtr<GT_Int32Array> myPrimIndirect;
        UT_IntrusivePtr<GT_Int32Array> myPointsIndirect;
        UT_IntrusivePtr<GT_Int32Array> myKnotsIndirect;

        CurvePartitionData()
            : myPrimIndirect(UTmakeIntrusive<GT_Int32Array>(0, 1))
            , myPointsIndirect(UTmakeIntrusive<GT_Int32Array>(0, 1))
            , myKnotsIndirect(UTmakeIntrusive<GT_Int32Array>(0, 1))
        {
        }
    };

    // Accumulate the curves for each partition.
    UT_Array<CurvePartitionData> partitions;
    partitions.setSize(num_partitions);
    exint point_idx = 0;
    exint knot_idx = 0;
    for (exint i = 0; i < num_curves; ++i)
    {
        CurvePartitionData &partition = partitions[curve_to_partition[i]];
        partition.myPrimIndirect->append(i);

        const exint num_points = src_curve->curveCounts->getI32(i);
        for (exint j = 0; j < num_points; ++j, ++point_idx)
            partition.myPointsIndirect->append(point_idx);

        if (src_curve->curveKnots)
        {
            const exint order = src_curve->constantOrder ?
                                        src_curve->constantOrder :
                                        src_curve->curveOrders->getI32(i);

            const exint num_knots = num_points + order;
            for (exint j = 0; j < num_knots; ++j, ++knot_idx)
                partition.myKnotsIndirect->append(knot_idx);
        }
    }

    // Finally, assemble the split parts.
    split_parts.setCapacityIfNeeded(partitions.size());
    for (const CurvePartitionData &partition : partitions)
    {
        GEO_HAPIPart &split_part = split_parts[split_parts.append()];
        split_part.myType = myType;

        split_part.myData = UTmakeUnique<CurveData>();
        auto split_curve = UTverify_cast<CurveData *>(split_part.myData.get());

        split_curve->curveType = src_curve->curveType;
        split_curve->periodic = src_curve->periodic;
        split_curve->constantOrder = src_curve->constantOrder;
        split_curve->hasExtractedBasisCurves
                = src_curve->hasExtractedBasisCurves;
        split_curve->hasFixedEndInterpolation
                = src_curve->hasFixedEndInterpolation;

        split_curve->curveCounts = UTmakeIntrusive<GT_DAIndirect>(
                partition.myPrimIndirect, src_curve->curveCounts);
        if (!split_curve->constantOrder)
        {
            split_curve->curveOrders = UTmakeIntrusive<GT_DAIndirect>(
                    partition.myPrimIndirect, src_curve->curveOrders);
        }

        if (src_curve->curveKnots)
        {
            split_curve->curveKnots = UTmakeIntrusive<GT_DAIndirect>(
                    partition.myKnotsIndirect, src_curve->curveKnots);
        }

        // Set up the attributes for the split part. Note that HAPI curves do
        // not have vertex attributes.
        split_curve->extraOwners = myData->extraOwners;
        for (int owner = 0; owner < HAPI_ATTROWNER_MAX; ++owner)
            split_part.myAttribNames[owner] = myAttribNames[owner];

        geoSplitAttribs(
                *this, partition.myPointsIndirect,
                /* vertex_indirect */ nullptr, partition.myPrimIndirect,
                split_part.myAttribs);
    }

    return true;
}

bool
GEO_HAPIPart::splitPartsByName(
        GEO_HAPIPartArray &split_parts,
        const GEO_ImportOptions &options) const
{
    // Only split meshes
    if (myType == HAPI_PARTTYPE_MESH)
        return splitMeshByName(split_parts, options);
    else if (myType == HAPI_PARTTYPE_CURVE)
        return splitCurvesByName(split_parts, options);

    return false;
}

static void
holdXUSDLockedGeo(std::string &lockedGeoPathWithArgs,
        XUSD_LockedGeoPtr lockedGeo)
{
    // LockedGeos remain in the registry as long as their reference count is at
    // least 1.  Ptrs referencing the lockedgeos need to be stored somewhere so
    // the lockedgeos aren't deleted before they are needed by the renderer
    static UT_StringMap<XUSD_LockedGeoPtr> lockedGeoMap;

    lockedGeoMap[lockedGeoPathWithArgs] = lockedGeo;
}

bool
GEO_HAPIPart::setupPrimType(
        GEO_FilePrim &filePrim,
        GEO_FilePrimMap &filePrimMap,
        const GEO_ImportOptions &options,
        const std::string &filePath,
        GT_DataArrayHandle &vertexIndirect,
        GEO_HAPISharedData &sharedData,
        const UT_Matrix4D *parentXform)
{
    // Transform to set
    UT_Matrix4D primXform = getXForm();
    if (parentXform)
        primXform *= *parentXform;

    GEO_HandleOtherPrims other_prim_handling = options.myOtherPrimHandling;

    if (other_prim_handling == GEO_OTHER_XFORM)
    {
        return false;
    }

    // Keep track of which attributes have been added
    UT_ArrayStringSet processedAttribs(options.myProcessedAttribs);

    bool define = (other_prim_handling == GEO_OTHER_DEFINE);
    GEO_FileProp *prop;

    switch (myType)
    {
    case HAPI_PARTTYPE_MESH:
    {
        MeshData *meshData = UTverify_cast<MeshData *>(myData.get());
        GT_DataArrayHandle attribData;

        bool forceConstantInterp = false;

        if (meshData->isOnlyPoints())
        {
            filePrim.setTypeName(GEO_FilePrimTypeTokens->Points);

            // The prim type and kind for points can be specified by an
            // attribute The part is already split by name and paths can be
            // defined by the user, so it is assumed that the type and kind is
            // uniform across all points in this part
            setupTypeAttribute(filePrim, processedAttribs);
            setupKindAttribute(filePrim, processedAttribs);

            // Get the schema definition for the current prim's type.
            const UsdPrimDefinition *primdef
                    = UsdSchemaRegistry::GetInstance()
                              .FindConcretePrimDefinition(
                                      filePrim.getTypeName());

            // Only author the common attributes like points, velocities, etc
            // for
            // prim types that support them.
            const bool isPointBased
                    = primdef ? (bool)primdef->GetSchemaAttributeSpec(
                                        UsdGeomTokens->points) :
                                false;
            if (isPointBased)
            {
                setupCommonAttributes(
                        filePrim, options, vertexIndirect, processedAttribs);
            }

            if (filePrim.getTypeName() == GEO_FilePrimTypeTokens->Points)
            {
                setupPointSizeAttribute(
                        filePrim, options, vertexIndirect, processedAttribs);
                setupPointIdsAttribute(
                        filePrim, options, vertexIndirect, processedAttribs);
                setupBoundsAttribute(filePrim, options, processedAttribs);
                GEOinitXformAttrib(
                        filePrim, primXform, options,
                        /* author_identity */ false);
            }
            else if (
                    primdef
                    && primdef->GetSchemaAttributeSpec(
                               UsdGeomTokens->xformOpOrder))
            {
                // Author a transform from the standard point instancing
                // attributes.
                UT_Matrix4D xform = GEOcomputeStandardPointXform(
                        *this, processedAttribs);
                GEOinitXformAttrib(filePrim, xform, options);
            }

            // Unless we're authoring a point-based primitive, use constant
            // interpolation for the primvars (the default behaviour would be
            // vertex since the source is a point attribute).
            forceConstantInterp = !isPointBased;
            setupColorAttributes(
                    filePrim, options, vertexIndirect, processedAttribs,
                    forceConstantInterp);
        }
        else
        {
            filePrim.setTypeName(GEO_FilePrimTypeTokens->Mesh);

            if (options.myTopologyHandling != GEO_USD_TOPOLOGY_NONE)
            {
                attribData = meshData->faceCounts;
                prop = filePrim.addProperty(
                        UsdGeomTokens->faceVertexCounts,
                        SdfValueTypeNames->IntArray,
                        new GEO_FilePropAttribSource<int>(attribData));
                prop->setValueIsDefault(
                        options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);

                attribData = meshData->vertices;
                if (options.myReversePolygons)
                {
                    vertexIndirect = GEOreverseWindingOrder(
                            meshData->faceCounts, meshData->vertices);
                    attribData = new GT_DAIndirect(vertexIndirect, attribData);
                }

                prop = filePrim.addProperty(
                        UsdGeomTokens->faceVertexIndices,
                        SdfValueTypeNames->IntArray,
                        new GEO_FilePropAttribSource<int>(attribData));
                prop->addCustomData(
                        HUSDgetDataIdToken(), VtValue(attribData->getDataId()));
                prop->setValueIsDefault(
                        options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);

                prop = filePrim.addProperty(
                        UsdGeomTokens->orientation, SdfValueTypeNames->Token,
                        new GEO_FilePropConstantSource<TfToken>(
                                options.myReversePolygons ?
                                        UsdGeomTokens->rightHanded :
                                        UsdGeomTokens->leftHanded));
                prop->setValueIsDefault(true);
                prop->setValueIsUniform(true);

                // Subdivision meshes are not extracted from HAPI
                TfToken subd_scheme = UsdGeomTokens->none;

                prop = filePrim.addProperty(
                        UsdGeomTokens->subdivisionScheme,
                        SdfValueTypeNames->Token,
                        new GEO_FilePropConstantSource<TfToken>(subd_scheme));
                prop->setValueIsDefault(true);
                prop->setValueIsUniform(true);
            }
            else
            {
                vertexIndirect = GEOreverseWindingOrder(
                        meshData->faceCounts, meshData->vertices);
            }

            setupCommonAttributes(
                    filePrim, options, vertexIndirect, processedAttribs);
            setupBoundsAttribute(filePrim, options, processedAttribs);
            setupPurposeAttribute(filePrim, options, processedAttribs);
            GEOinitXformAttrib(
                    filePrim, primXform, options, /* author_identity */ false);
        }

        setupVisibilityAttribute(filePrim, options, processedAttribs);
        setupExtraPrimAttributes(
                filePrim, options, vertexIndirect, processedAttribs,
                forceConstantInterp);

        break;
    }

    case HAPI_PARTTYPE_CURVE:
    {
        CurveData *curve = UTverify_cast<CurveData *>(myData.get());
        GT_DataArrayHandle attribData;

        if (options.myTopologyHandling != GEO_USD_TOPOLOGY_NONE
            && curve->curveCounts)
        {
            int order = curve->constantOrder;
            GT_DataArrayHandle curveCounts = curve->curveCounts;
            HAPI_CurveType type = curve->curveType;
            GEO_FileProp *prop = nullptr;

            bool useNurbs = (type == HAPI_CURVETYPE_NURBS)
                            && (options.myNurbsCurveHandling
                                == GEO_NURBS_NURBSCURVES);

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
                    GT_Offset knotEnd = knotStart
                                        + curve->curveCounts->getI32(i)
                                        + orders[i] - 1;
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
                        new GEO_FilePropConstantSource<VtArray<GfVec2d>>(
                                ranges));
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

                    const bool wrap = curve->periodic;
                    prop = filePrim.addProperty(
                            UsdGeomTokens->wrap, SdfValueTypeNames->Token,
                            new GEO_FilePropConstantSource<TfToken>(
                                    wrap ? UsdGeomTokens->periodic :
                                           UsdGeomTokens->nonperiodic));
                    prop->setValueIsDefault(true);
                    prop->setValueIsUniform(true);

                    // Houdini repeats the first point for closed beziers.
                    // USD does not expect this, so we need to remove the
                    // extra point.
                    if (order == 4 && wrap)
                    {
                        GT_DANumeric<float> *modcounts
                                = new GT_DANumeric<float>(
                                        curveCounts->entries(), 1);

                        for (GT_Size i = 0, n = curveCounts->entries(); i < n;
                             ++i)
                        {
                            modcounts->set(curveCounts->getF32(i) - 4, i);
                        }
                        curveCounts = modcounts;
                    }
                    else
                    {
                        fixCurveEndInterpolation();
                    }
                }
                else
                {
                    // Don't define unsupported curves (return false)
                    define = false;
                    break;
                }
            }

            prop = filePrim.addProperty(
                    UsdGeomTokens->curveVertexCounts,
                    SdfValueTypeNames->IntArray,
                    new GEO_FilePropAttribSource<int>(curveCounts));
            prop->addCustomData(
                    HUSDgetDataIdToken(), VtValue(curveCounts->getDataId()));
            prop->setValueIsDefault(
                    options.myTopologyHandling == GEO_USD_TOPOLOGY_STATIC);
        }
        setupCommonAttributes(
                filePrim, options, vertexIndirect, processedAttribs);
        setupPointSizeAttribute(
                filePrim, options, vertexIndirect, processedAttribs);
        setupBoundsAttribute(filePrim, options, processedAttribs);
        setupVisibilityAttribute(filePrim, options, processedAttribs);
        setupPurposeAttribute(filePrim, options, processedAttribs);
        setupExtraPrimAttributes(
                filePrim, options, vertexIndirect, processedAttribs);
        GEOinitXformAttrib(
                filePrim, primXform, options, /* author_identity */ false);
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

        setupBoundsAttribute(filePrim, options, processedAttribs);
        setupVisibilityAttribute(filePrim, options, processedAttribs);
        setupPurposeAttribute(filePrim, options, processedAttribs);
        setupColorAttributes(
                filePrim, options, vertexIndirect, processedAttribs);
        setupExtraPrimAttributes(
                filePrim, options, vertexIndirect, processedAttribs);
        GEOinitXformAttrib(filePrim, primXform, options);
        break;
    }

    case HAPI_PARTTYPE_VOLUME:
    {
        // Set up a Volume parent and field asset child
        VolumeData *vol = UTverify_cast<VolumeData *>(myData.get());
        filePrim.setTypeName(GEO_FilePrimTypeTokens->Volume);

        UT_StringHolder name = vol->name;
        bool hasName = true;
        if (name.isEmpty())
        {
            // Give this field a default name if it doesn't have one
            static constexpr UT_StringLit theDefaultFieldPrefix("field_");

            name.sprintf(
                    "%s%d", theDefaultFieldPrefix.c_str(),
                    sharedData.defaultFieldNameSuffix);

            // Incrememnt the suffix for the next field in this collection
            sharedData.defaultFieldNameSuffix++;
            hasName = false;
        }

        TfToken nameToken(name.c_str());

        SdfPath fieldPath = filePrim.getPath().AppendChild(nameToken);
        GEO_FilePrim &fieldPrim(filePrimMap[fieldPath]);
        fieldPrim.setPath(fieldPath);

        if (vol->volumeType == HAPI_VOLUMETYPE_HOUDINI)
        {
            fieldPrim.setTypeName(GEO_FilePrimTypeTokens->HoudiniFieldAsset);
        }
        else
        {
            fieldPrim.setTypeName(GEO_FilePrimTypeTokens->OpenVDBAsset);
        }

        // Prepend the HAPI prefix so the lockedgeo registry is used to load
        // this volume
        std::string prependedPath = HUSD_HAPI_PREFIX + filePath;

        fieldPrim.addProperty(
                UsdVolTokens->filePath, SdfValueTypeNames->Asset,
                new GEO_FilePropConstantSource<SdfAssetPath>(
                        SdfAssetPath(prependedPath)));

        // Add this geometry to the lockedgeo registry
        if (!sharedData.lockedGeo)
        {
            std::string path;
            SdfFileFormat::FileFormatArguments args;
            SdfLayer::SplitIdentifier(prependedPath, &path, &args);
            sharedData.lockedGeo = XUSD_LockedGeoRegistry::createLockedGeo(
                    path, args, vol->gdh);

            holdXUSDLockedGeo(prependedPath, sharedData.lockedGeo);
        }

        if (hasName)
        {
            // Assign the field name to this volume's name
            fieldPrim.addProperty(
                    UsdVolTokens->fieldName, SdfValueTypeNames->Token,
                    new GEO_FilePropConstantSource<TfToken>(nameToken));
        }

        // Houdini Native Volumes have a field index to fall back to if the
        // name attribute isn't set.
        if (vol->volumeType == HAPI_VOLUMETYPE_HOUDINI)
        {
            fieldPrim.addProperty(
                    UsdVolTokens->fieldIndex, SdfValueTypeNames->Int,
                    new GEO_FilePropConstantSource<int>(vol->fieldIndex));
        }

        // If the volume save path was specified, record as custom data.
        if (UT_StringHolder save_path = hapiGetStringFromAttrib(
                    myAttribs, theVolumeSavePathName.asRef()))
        {
            fieldPrim.addProperty(
                    HUSDgetSavePathToken(), SdfValueTypeNames->String,
                    new GEO_FilePropConstantSource<std::string>(
                            save_path.toStdString()));
        }

        setupBoundsAttribute(fieldPrim, options, processedAttribs);
        setupVisibilityAttribute(fieldPrim, options, processedAttribs);
        setupPurposeAttribute(fieldPrim, options, processedAttribs);
        setupExtraPrimAttributes(
                fieldPrim, options, vertexIndirect, processedAttribs);
        GEOinitXformAttrib(fieldPrim, primXform, options);

        // Set up the relationship between the volume and field prim
        UT_WorkBuffer fieldBuf;
        fieldBuf = UsdVolTokens->field.GetString();
        fieldBuf.appendSprintf(":%s", name.c_str());
        filePrim.addRelationship(
                TfToken(fieldBuf.buffer()), SdfPathVector({fieldPath}));

        fieldPrim.setIsDefined(define);
        fieldPrim.setInitialized();
        break;
    }

    default:
        break;
    }

    return define;
}

template <class DT, class ComponentDT>
GEO_FileProp *
GEO_HAPIPart::applyAttrib(
        GEO_FilePrim &filePrim,
        const GEO_HAPIAttribute &attrib,
        const TfToken &usdAttribName,
        const SdfValueTypeName &usdTypeName,
        UT_ArrayStringSet &processedAttribs,
        bool createIndicesAttrib,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        const GT_DataArrayHandle &attribDataOverride,
        const bool overrideConstant)
{
    GEO_FileProp *prop = nullptr;
    if (attrib.myData && !processedAttribs.contains(attrib.myName))
    {
        GT_DataArrayHandle srcAttrib = attribDataOverride ? attribDataOverride :
                                                            attrib.myData;
        GT_Owner owner = GEOhapiConvertOwner(attrib.myOwner);

        // In HAPI, curve point attributes appear as point attributes, not
        // vertex attributes, so we don't need the same special handling as SOP
        // Import.
        static constexpr bool prim_is_curve = false;

        UT_ASSERT(!attrib.myData->hasArrayEntries());
        prop = GEOinitProperty<DT, ComponentDT>(
                filePrim, srcAttrib, attrib.myName, attrib.myDecodedName,
                owner, prim_is_curve, options, usdAttribName, usdTypeName,
                createIndicesAttrib,
                /* override_data_id */ nullptr, vertexIndirect,
                overrideConstant);

        processedAttribs.insert(attrib.myName);
    }

    return prop;
}

template <class DT, class ComponentDT>
GEO_FileProp *
GEO_HAPIPart::applyArrayAttrib(
        GEO_FilePrim &filePrim,
        const GEO_HAPIAttribute &attrib,
        const TfToken &usdAttribName,
        const SdfValueTypeName &usdTypeName,
        UT_ArrayStringSet &processedAttribs,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        const bool overrideConstant)
{
    GEO_FileProp *prop = nullptr;
    if (attrib.myData && !processedAttribs.contains(attrib.myName))
    {
        processedAttribs.insert(attrib.myName);
        GT_Owner owner = GEOhapiConvertOwner(attrib.myOwner);

        // In HAPI, curve point attributes appear as point attributes, not
        // vertex attributes, so we don't need the same special handling as SOP
        // Import.
        static constexpr bool prim_is_curve = false;

        UT_ASSERT(attrib.myData->hasArrayEntries());
        prop = GEOinitArrayAttrib<DT, ComponentDT>(
                filePrim, attrib.myData, attrib.myName, attrib.myDecodedName,
                owner, prim_is_curve, options, usdAttribName, usdTypeName,
                vertexIndirect, overrideConstant);
    }

    return prop;
}

void
GEO_HAPIPart::convertExtraAttrib(
        GEO_FilePrim &filePrim,
        const GEO_HAPIAttribute &attrib,
        const TfToken &usdAttribName,
        UT_ArrayStringSet &processedAttribs,
        bool createIndicesAttrib,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        const bool overrideConstant)
{
    bool applied = false; // set in the macro below

    // Factors that determine the property type
    HAPI_AttributeTypeInfo typeInfo = attrib.myTypeInfo;
    HAPI_StorageType storage = attrib.myDataType;
    int tupleSize = attrib.getTupleSize();

#define APPLY_ARRAY_ATTRIB(usdTypeName, type, typeComp)                        \
    applyArrayAttrib<type, typeComp>(                                          \
            filePrim, attrib, usdAttribName, usdTypeName, processedAttribs,    \
            options, vertexIndirect, overrideConstant);                        \
    applied = true;

    if (attrib.myData->hasArrayEntries())
    {
        switch (storage)
        {
        case HAPI_STORAGETYPE_FLOAT_ARRAY:
            APPLY_ARRAY_ATTRIB(SdfValueTypeNames->FloatArray, fpreal32, fpreal32);
            break;

        case HAPI_STORAGETYPE_FLOAT64_ARRAY:
            APPLY_ARRAY_ATTRIB(SdfValueTypeNames->DoubleArray, fpreal64, fpreal64);
            break;

        case HAPI_STORAGETYPE_INT_ARRAY:
            APPLY_ARRAY_ATTRIB(SdfValueTypeNames->IntArray, int, int);
            break;

        case HAPI_STORAGETYPE_INT64_ARRAY:
            APPLY_ARRAY_ATTRIB(SdfValueTypeNames->Int64Array, int64, int64);
            break;

        case HAPI_STORAGETYPE_STRING_ARRAY:
            APPLY_ARRAY_ATTRIB(
                    SdfValueTypeNames->StringArray, std::string, std::string);
            break;

        default:
            UT_ASSERT_MSG(false, "Unsupported array attribute type.");
        }
    }

#define APPLY_ATTRIB(usdTypeName, type, typeComp)                              \
    applyAttrib<type, typeComp>(                                               \
            filePrim, attrib, usdAttribName, usdTypeName, processedAttribs,    \
            createIndicesAttrib, options, vertexIndirect,                      \
            GT_DataArrayHandle(), overrideConstant);                           \
    applied = true;

    // Specific type names
    if (!applied)
    {
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
                APPLY_ATTRIB(
                        SdfValueTypeNames->Color4fArray, GfVec4f, fpreal32);
            }
            else if (typeInfo == HAPI_ATTRIBUTE_TYPE_QUATERNION)
            {
                APPLY_ATTRIB(SdfValueTypeNames->QuatfArray, GfQuatf, fpreal32);
            }
            break;
        }

        case 3:
        {
            if (typeInfo == HAPI_ATTRIBUTE_TYPE_POINT)
            {
                APPLY_ATTRIB(
                        SdfValueTypeNames->Point3fArray, GfVec3f, fpreal32);
            }
            else if (typeInfo == HAPI_ATTRIBUTE_TYPE_HPOINT)
            {
                APPLY_ATTRIB(
                        SdfValueTypeNames->Point3fArray, GfVec3f, fpreal32);
            }
            else if (typeInfo == HAPI_ATTRIBUTE_TYPE_VECTOR)
            {
                APPLY_ATTRIB(
                        SdfValueTypeNames->Vector3fArray, GfVec3f, fpreal32);
            }
            else if (typeInfo == HAPI_ATTRIBUTE_TYPE_NORMAL)
            {
                APPLY_ATTRIB(
                        SdfValueTypeNames->Normal3fArray, GfVec3f, fpreal32);
            }
            else if (typeInfo == HAPI_ATTRIBUTE_TYPE_COLOR)
            {
                APPLY_ATTRIB(
                        SdfValueTypeNames->Color3fArray, GfVec3f, fpreal32);
            }
            else if (typeInfo == HAPI_ATTRIBUTE_TYPE_TEXTURE)
            {
                if (storage == HAPI_STORAGETYPE_FLOAT)
                {
                    APPLY_ATTRIB(
                            SdfValueTypeNames->TexCoord3fArray, GfVec3f,
                            fpreal32);
                }
                else if (storage == HAPI_STORAGETYPE_FLOAT64)
                {
                    APPLY_ATTRIB(
                            SdfValueTypeNames->TexCoord3dArray, GfVec3d,
                            fpreal64);
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
                            SdfValueTypeNames->TexCoord2fArray, GfVec2f,
                            fpreal32);
                }
                else if (storage == HAPI_STORAGETYPE_FLOAT64)
                {
                    APPLY_ATTRIB(
                            SdfValueTypeNames->TexCoord2dArray, GfVec2d,
                            fpreal64);
                }
            }
            break;
        }

        default:
            break;
        }
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
                        SdfValueTypeNames->StringArray, std::string,
                        std::string);
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
GEO_HAPIPart::setupExtraPrimAttributes(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        UT_ArrayStringSet &processedAttribs,
        const bool overrideConstant)
{
    static const std::string thePrimvarPrefix("primvars:");
    UT_Array<HAPI_AttributeOwner> *owners = nullptr;
    if (myData)
        owners = &myData->extraOwners;

    for (exint i = 0; i < HAPI_ATTROWNER_MAX; ++i)
    {
        auto owner = static_cast<HAPI_AttributeOwner>(i);
        if (owners && owners->find(owner) < 0)
            continue;

        auto &&attrib_map = myAttribs[i];
        for (const UT_StringHolder &attrib_name : myAttribNames[i])
        {
            if (processedAttribs.contains(attrib_name))
                continue;

            GEO_HAPIAttributeHandle &attrib = attrib_map[attrib_name];

            if (options.multiMatch(attrib->myName)
                || options.multiMatch(attrib->myDecodedName))
            {
                TfToken usdAttribName;
                bool createIndicesAttrib = true;

                if (attrib->myName.multiMatch(options.myCustomAttribs)
                    || attrib->myDecodedName.multiMatch(
                            options.myCustomAttribs))
                {
                    usdAttribName = TfToken(attrib->myDecodedName.toStdString());
                    createIndicesAttrib = false;
                }
                else
                {
                    usdAttribName = TfToken(
                            thePrimvarPrefix
                            + attrib->myDecodedName.toStdString());
                }

                convertExtraAttrib(
                        filePrim, *attrib, usdAttribName, processedAttribs,
                        createIndicesAttrib, options, vertexIndirect,
                        overrideConstant);
            }
        }
    }
}

SYS_FORCE_INLINE
GEO_HAPIAttribute *
GEO_HAPIPart::findAttrib(
        const UT_StringHolder &attribName,
        const GEO_ImportOptions &options)
{
    if (!options.multiMatch(attribName))
        return nullptr;

    GEO_HAPIAttribute *result = nullptr;
    for (int owner = 0; owner < HAPI_ATTROWNER_MAX && !result; ++owner)
    {
        auto it = myAttribs[owner].find(attribName);
        if (it != myAttribs[owner].end())
            result = it->second.get();
    }

    return result;
}

SYS_FORCE_INLINE
GEO_HAPIAttribute *
GEO_HAPIPart::findAttrib(
        const UT_StringHolder &attribName,
        HAPI_AttributeOwner owner,
        const GEO_ImportOptions &options)
{
    if (!options.multiMatch(attribName))
        return nullptr;

    auto it = myAttribs[owner].find(attribName);
    return (it != myAttribs[owner].end()) ? it->second.get() : nullptr;
}

void
GEO_HAPIPart::setupBoundsAttribute(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        UT_ArrayStringSet &processedAttribs)
{
    const UT_StringHolder &boundsName = theBoundsName.asHolder();
    if (!processedAttribs.contains(boundsName)
        && (boundsName.multiMatch(options.myAttribs)))
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
GEO_HAPIPart::setupColorAttributes(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        UT_ArrayStringSet &processedAttribs,
        const bool overrideConstant)
{
    static const UT_StringHolder &theColorAttrib(GA_Names::Cd);
    static const UT_StringHolder &theAlphaAttrib(GA_Names::Alpha);

    // Color (RGB)
    if (auto col = findAttrib(theColorAttrib, options))
    {
        if (hapiIsFloatAttrib(col->myDataType))
        {
            // HAPI gives us RGBA tuples by default
            // USD expects RGB and Alpha seperately,
            // so make another alpha attribute if
            // it doesn't already exist
            if (col->getTupleSize() >= 4)
            {
                if (!findAttrib(theAlphaAttrib, col->myOwner, options))
                {
                    // Make alpha attrib
                    GT_DANumeric<float> *alphas = new GT_DANumeric<float>(
                            col->entries(), 1);

                    for (exint i = 0; i < col->entries(); i++)
                    {
                        fpreal32 aVal = col->myData->getF32(i, 3);
                        alphas->set(aVal, i);
                    }

                    GEO_HAPIAttributeHandle a(new GEO_HAPIAttribute(
                            theAlphaAttrib, col->myOwner,
                            HAPI_STORAGETYPE_FLOAT, alphas));

                    // Add the alpha attribute
                    myAttribs[col->myOwner][theAlphaAttrib].swap(a);

                    UT_ASSERT(!a.get());
                }
            }

            col->convertTupleSize(3, GEO_FillMethod::Hold);

            applyAttrib<GfVec3f, float>(
                    filePrim, *col, UsdGeomTokens->primvarsDisplayColor,
                    SdfValueTypeNames->Color3fArray, processedAttribs, true,
                    options, vertexIndirect, GT_DataArrayHandle(),
                    overrideConstant);
        }
    }

    // Alpha
    if (auto a = findAttrib(theAlphaAttrib, options))
    {
        if (hapiIsFloatAttrib(a->myDataType))
        {
            a->convertTupleSize(1);

            applyAttrib<float>(
                    filePrim, *a, UsdGeomTokens->primvarsDisplayOpacity,
                    SdfValueTypeNames->FloatArray, processedAttribs, true,
                    options, vertexIndirect);
        }
    }
}

void
GEO_HAPIPart::setupCommonAttributes(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        UT_ArrayStringSet &processedAttribs)
{
    static const UT_StringHolder &thePointsAttrib(GA_Names::P);

    // Points
    if (auto attrib = findAttrib(thePointsAttrib, options))
    {
        if (hapiIsFloatAttrib(attrib->myDataType))
        {
            // point values must be in a vector3 array
            attrib->convertTupleSize(3);

            applyAttrib<GfVec3f, float>(
                    filePrim, *attrib, UsdGeomTokens->points,
                    SdfValueTypeNames->Point3fArray, processedAttribs, false,
                    options, vertexIndirect);
        }
    }

    static const UT_StringHolder &theNormalsAttrib(GA_Names::N);

    // Normals
    if (auto attrib = findAttrib(theNormalsAttrib, options))
    {
        if (hapiIsFloatAttrib(attrib->myDataType))
        {
            // normal values must be in a vector3 array
            attrib->convertTupleSize(3);

            // If N is included in the pattern for indexed attributes, create
            // 'primvars:normals' instead which allows indexing. The
            // documentation of UsdGeomPointBased::GetNormalsAttr() specifies
            // that this is valid.
            TfToken normals_attr = UsdGeomTokens->normals;
            bool normals_indices = false;
            if (theNormalsAttrib.multiMatch(options.myIndexAttribs))
            {
                normals_attr = GEO_FilePrimTokens->primvarsNormals;
                normals_indices = true;
            }

            GEO_FileProp *prop = applyAttrib<GfVec3f, float>(
                    filePrim, *attrib, normals_attr,
                    SdfValueTypeNames->Normal3fArray, processedAttribs,
                    normals_indices, options, vertexIndirect);

            // Normals attribute is not quite the same as primvars in how the
            // interpolation value is set.
            if (prop)
            {
                if (attrib->myOwner == HAPI_ATTROWNER_VERTEX)
                    prop->addMetadata(
                            UsdGeomTokens->interpolation,
                            VtValue(UsdGeomTokens->faceVarying));
                else
                    prop->addMetadata(
                            UsdGeomTokens->interpolation,
                            VtValue(UsdGeomTokens->varying));
            }
        }
    }

    // Color and Alpha
    setupColorAttributes(filePrim, options, vertexIndirect, processedAttribs);

    static const UT_StringHolder &theTexCoordAttrib(GA_Names::uv);

    // Texture Coordinates (UV/ST)
    if (auto tex = findAttrib(theTexCoordAttrib, options);
        tex && options.myTranslateUVToST)
    {
        // Skip renaming if st attrib exists
        UT_StringHolder stName = GusdUSD_Utils::TokenToStringHolder(
                UsdUtilsGetPrimaryUVSetName());
        if (!myAttribs[tex->myOwner].contains(stName))
        {
            UT_WorkBuffer buf;
            buf.format("primvars:{0}", stName);
            TfToken stToken(buf.buffer());

            if (hapiIsFloatAttrib(tex->myDataType))
            {
                tex->convertTupleSize(2);

                if (tex->myDataType == HAPI_STORAGETYPE_FLOAT)
                {
                    applyAttrib<GfVec2f, float>(
                            filePrim, *tex, stToken,
                            SdfValueTypeNames->TexCoord2fArray,
                            processedAttribs, true, options, vertexIndirect);
                }
                else // tex->myDataType == HAPI_STORAGETYPE_FLOAT64
                {
                    applyAttrib<GfVec2d, double>(
                            filePrim, *tex, stToken,
                            SdfValueTypeNames->TexCoord2dArray,
                            processedAttribs, true, options, vertexIndirect);
                }
            }
        }
    }

    // Velocity and Acceleration
    setupKinematicAttributes(
            filePrim, options, vertexIndirect, processedAttribs);
}

void
GEO_HAPIPart::setupAngVelAttribute(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        UT_ArrayStringSet &processedAttribs)
{
    static const UT_StringHolder &theAngularVelocityAttrib(GA_Names::w);

    // Angular Velocity
    if (auto w = findAttrib(theAngularVelocityAttrib, options))
    {
        if (hapiIsFloatAttrib(w->myDataType))
        {
            w->convertTupleSize(3);

            // w is in radians/second, but a point instancer's angular velocity
            // is in degrees/second
            GT_DataArrayHandle wInDegrees = GEOconvertRadToDeg(w->myData);

            applyAttrib<GfVec3f, float>(
                    filePrim, *w, UsdGeomTokens->angularVelocities,
                    SdfValueTypeNames->Vector3fArray, processedAttribs, false,
                    options, vertexIndirect, wInDegrees);
        }
    }
}

// Velocity and Acceleration
void
GEO_HAPIPart::setupKinematicAttributes(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        UT_ArrayStringSet &processedAttribs)
{
    static const UT_StringHolder &theVelocityAttrib(GA_Names::v);

    // Velocity
    if (auto v = findAttrib(theVelocityAttrib, options))
    {
        if (hapiIsFloatAttrib(v->myDataType))
        {
            v->convertTupleSize(3);

            applyAttrib<GfVec3f, float>(
                    filePrim, *v, UsdGeomTokens->velocities,
                    SdfValueTypeNames->Vector3fArray, processedAttribs, false,
                    options, vertexIndirect);
        }
    }

    static const UT_StringHolder &theAccelAttrib(GA_Names::accel);

    // Acceleration
    if (auto a = findAttrib(theAccelAttrib, options))
    {
        if (hapiIsFloatAttrib(a->myDataType))
        {
            a->convertTupleSize(3);

            applyAttrib<GfVec3f, float>(
                    filePrim, *a, UsdGeomTokens->accelerations,
                    SdfValueTypeNames->Vector3fArray, processedAttribs, false,
                    options, vertexIndirect);
        }
    }
}

void
GEO_HAPIPart::setupVisibilityAttribute(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        UT_ArrayStringSet &processedAttribs)
{
    static constexpr UT_StringLit theVisibilityAttrib("usdvisibility");
    if (!theVisibilityName.asRef().multiMatch(options.myAttribs))
        return;

    const TfToken visibility = hapiGetTokenFromAttrib(
            myAttribs, theVisibilityAttrib.asRef());

    if (visibility.IsEmpty())
        return;

    bool makeVisible = (visibility != UsdGeomTokens->invisible);

    GEO_FileProp *prop = filePrim.addProperty(
            UsdGeomTokens->visibility, SdfValueTypeNames->Token,
            new GEO_FilePropConstantSource<TfToken>(
                    makeVisible ? UsdGeomTokens->inherited :
                                  UsdGeomTokens->invisible));

    prop->setValueIsDefault(
            theVisibilityName.asRef().multiMatch(options.myStaticAttribs));
    prop->setValueIsUniform(false);

    processedAttribs.insert(theVisibilityAttrib.asHolder());
}

void
GEO_HAPIPart::setupPurposeAttribute(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        UT_ArrayStringSet &processedAttribs)
{
    static constexpr UT_StringLit thePurposeAttrib(GUSD_PURPOSE_ATTR);

    TfToken purpose = hapiGetTokenFromAttrib(
            myAttribs, thePurposeAttrib.asHolder());
    if (purpose.IsEmpty())
        return;

    GEOinitPurposeAttrib(filePrim, purpose);
    processedAttribs.insert(thePurposeAttrib.asHolder());
}

void
GEO_HAPIPart::setupPointSizeAttribute(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        UT_ArrayStringSet &processedAttribs)
{
    UT_StringHolder widthAttrib = "widths"_sh;
    fpreal widthScale = 1.0;
    if (!findAttrib(widthAttrib, options))
    {
        widthAttrib = GA_Names::width;
    }
    if (!findAttrib(widthAttrib, options))
    {
        // pscale represents radius, but widths represents diameter
        widthAttrib = GA_Names::pscale;
        widthScale = 2.0;
    }
    if (auto w = findAttrib(widthAttrib, options))
    {
        if (hapiIsFloatAttrib(w->myDataType))
        {
            w->convertTupleSize(1);

            GT_DataArrayHandle adjustedWidths = GEOscaleWidthsAttrib(
                    w->myData, widthScale);

            applyAttrib<float>(
                    filePrim, *w, UsdGeomTokens->widths,
                    SdfValueTypeNames->FloatArray, processedAttribs, false,
                    options, vertexIndirect, adjustedWidths);
        }
    }
}

void
GEO_HAPIPart::setupPointIdsAttribute(
        GEO_FilePrim &filePrim,
        const GEO_ImportOptions &options,
        const GT_DataArrayHandle &vertexIndirect,
        UT_ArrayStringSet &processedAttribs)
{
    const UT_StringHolder &theIdsAttrib(GA_Names::id);

    GEO_HAPIAttribute *ids = findAttrib(theIdsAttrib, options);
    if (!ids)
        return;

    if (!hapiIsIntAttrib(ids->myDataType))
        return;

    ids->convertTupleSize(1);
    applyAttrib<int64>(
            filePrim, *ids, UsdGeomTokens->ids, SdfValueTypeNames->Int64Array,
            processedAttribs, false, options, vertexIndirect);
}

void
GEO_HAPIPart::setupTypeAttribute(
        GEO_FilePrim &filePrim,
        UT_ArrayStringSet &processedAttribs)
{
    static constexpr UT_StringLit thePrimTypeAttrib("usdprimtype");

    TfToken typeToken = hapiGetTokenFromAttrib(
            myAttribs, thePrimTypeAttrib.asRef());
    if (typeToken.IsEmpty())
        return;

    filePrim.setTypeName(typeToken);
    processedAttribs.insert(thePrimTypeAttrib);
}

void
GEO_HAPIPart::setupKindAttribute(
        GEO_FilePrim &filePrim,
        UT_ArrayStringSet &processedAttribs)
{
    static constexpr UT_StringLit theKindAttrib("usdkind");

    TfToken kind_token = hapiGetTokenFromAttrib(
            myAttribs, theKindAttrib.asRef());
    if (!kind_token.IsEmpty() && KindRegistry::GetInstance().HasKind(kind_token))
        filePrim.replaceMetadata(SdfFieldKeys->Kind, VtValue(kind_token));

    processedAttribs.insert(theKindAttrib);
}

void
GEO_HAPIPart::createInstancePart(GEO_HAPIPart &partOut, exint attribIndex)
{
    partOut.myType = HAPI_PARTTYPE_INSTANCER;

    for (int owner = 0; owner < HAPI_ATTROWNER_MAX; ++owner)
    {
        partOut.myAttribNames[owner].clear();
        partOut.myAttribs[owner].clear();
    }

    for (const UT_StringHolder &attrib_name : myAttribNames[HAPI_ATTROWNER_PRIM])
    {
        GEO_HAPIAttributeHandle &attr
                = myAttribs[HAPI_ATTROWNER_PRIM][attrib_name];

        GEO_HAPIAttributeHandle newAttr;
        attr->createElementIndirect(attribIndex, newAttr);
        partOut.myAttribNames[HAPI_ATTROWNER_PRIM].append(attrib_name);
        partOut.myAttribs[HAPI_ATTROWNER_PRIM][attrib_name].swap(newAttr);

        UT_ASSERT(!newAttr.get());
    }
}

GT_DataArrayHandle
GEO_HAPIPart::findAttribute(
        const UT_StringRef &attrName,
        GT_Owner &owner,
        exint segment) const
{
    for (int i = 0; i < HAPI_ATTROWNER_MAX; ++i)
    {
        if (myAttribs[i].contains(attrName))
        {
            const GEO_HAPIAttributeHandle &attr = myAttribs[i].at(attrName);

            return GT_DataArrayHandle(attr->myData);
        }
    }

    return GT_DataArrayHandle();
}

//
// Memory usage functions
//

int64
GEO_HAPIPart::CurveData::getMemoryUsage() const 
{
    int64 usage = sizeof(*this);
    usage = curveCounts ? curveCounts->getMemoryUsage() : 0;
    usage += curveOrders ? curveOrders->getMemoryUsage() : 0;
    usage += curveKnots ? curveKnots->getMemoryUsage() : 0;

    return usage;
}

int64
GEO_HAPIPart::InstanceData::getMemoryUsage() const
{
    int64 usage = sizeof(*this);
    usage += instances.getMemoryUsage(false);
    
    for (const GEO_HAPIPart &part : instances)
    {
        usage += part.getMemoryUsage(false);
    }

    usage += instanceTransforms.getMemoryUsage(false);

    return usage;
}

int64
GEO_HAPIPart::MeshData::getMemoryUsage() const
{
    int64 usage = sizeof(*this);
    usage = faceCounts ? faceCounts->getMemoryUsage() : 0;
    usage += vertices ? vertices->getMemoryUsage() : 0;

    return usage;
}

int64
GEO_HAPIPart::VolumeData::getMemoryUsage() const
{
    int64 usage = sizeof(*this);
    usage += gdh ? gdh.gdp()->getMemoryUsage(false) : 0;
    return usage;
}

int64 
GEO_HAPIPart::getMemoryUsage(bool inclusive) const
{
    int64 usage = inclusive ? sizeof(*this) : 0;
    usage += myData ? myData->getMemoryUsage(): 0;

    for (int owner = 0; owner < HAPI_ATTROWNER_MAX; ++owner)
    {
        usage += myAttribNames[owner].getMemoryUsage(false);
        usage += myAttribs[owner].getMemoryUsage(false);

        for (const UT_StringHolder &name : myAttribNames[owner])
            usage += myAttribs[owner].at(name)->getMemoryUsage(false);
    }

    return usage;
}
