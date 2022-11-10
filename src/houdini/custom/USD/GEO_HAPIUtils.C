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
#include <GT/GT_CountArray.h>
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DANumeric.h>
#include <HUSD/HUSD_Utils.h>
#include <UT/UT_Map.h>
#include <UT/UT_Quaternion.h>
#include <gusd/USD_Utils.h>
#include <openvdb/tools/SignedFloodFill.h>
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

    if (retSize == 0)
    {
        buf.clear();
        return true;
    }

    char *str = buf.lock(0, retSize);
    ENSURE_SUCCESS(HAPI_GetString(&session, handle, str, retSize), session);

    // Note that HAPI_GetStringBufLength includes the null terminator, so
    // subtracting 1 gives the actual string length.
    buf.releaseSetLength(retSize - 1);

    return true;
}

void
GEOhapiSendCookError(const HAPI_Session &session, HAPI_NodeId node_id)
{
    PXR_NAMESPACE_USING_DIRECTIVE
    UT_WorkBuffer buf;

    int len;
    HAPI_GetStatusStringBufLength(
        &session, HAPI_STATUS_COOK_RESULT, HAPI_STATUSVERBOSITY_ERRORS, &len);

    char *str = buf.lock(0, len);
    HAPI_GetStatusString(&session, HAPI_STATUS_COOK_RESULT, str, len);
    // HAPI_GetStatusStringBufLength included the null terminator.
    buf.releaseSetLength(len - 1);

    TF_WARN("%s", buf.buffer());

    // Also add any node warnings / errors.
    HAPI_Result result = HAPI_ComposeNodeCookResult(
            &session, node_id, HAPI_STATUSVERBOSITY_WARNINGS, &len);
    if (result != HAPI_RESULT_SUCCESS)
    {
        GEOhapiSendError(session);
        return;
    }

    str = buf.lock(0, len);
    result = HAPI_GetComposedNodeCookResult(&session, str, len);
    buf.releaseSetLength(len - 1);
    if (result != HAPI_RESULT_SUCCESS)
    {
        GEOhapiSendError(session);
        return;
    }

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
    // HAPI_GetStatusStringBufLength included the null terminator.
    buf.releaseSetLength(len - 1);

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

static bool
getVoxelData(const HAPI_Session &session,
             HAPI_NodeId nodeId,
             HAPI_PartId partId,
             const HAPI_VolumeTileInfo &tile,
             int *buf,
             exint tileBufSize)
{
    ENSURE_SUCCESS(HAPI_GetVolumeTileIntData(
                       &session, nodeId, partId, 0, &tile, buf, tileBufSize),
                   session);

    return true;
}

static bool
getVoxelData(const HAPI_Session &session,
             HAPI_NodeId nodeId,
             HAPI_PartId partId,
             const HAPI_VolumeTileInfo &tile,
             float *buf,
             exint tileBufSize)
{
    ENSURE_SUCCESS(HAPI_GetVolumeTileFloatData(
                       &session, nodeId, partId, 0.f, &tile, buf, tileBufSize),
                   session);
    return true;
}

template <class T>
static bool
extractVoxels(const UT_VoxelArrayWriteHandleF &vox,
              const HAPI_Session &session,
              HAPI_NodeId nodeId,
              HAPI_PartId partId,
              const HAPI_VolumeInfo &vInfo)
{
    vox->size(vInfo.xLength, vInfo.yLength, vInfo.zLength);
    const int tileLength = vInfo.tileSize;

    const exint tileBufSize = tileLength * tileLength * tileLength;
    UT_UniquePtr<T> buf(new T[tileBufSize]);

    // Get the first tile
    HAPI_VolumeTileInfo tile;
    ENSURE_SUCCESS(
        HAPI_GetFirstVolumeTile(&session, nodeId, partId, &tile), session);

    while (tile.isValid)
    {
        // Get the values from HAPI
        CHECK_RETURN(getVoxelData(
            session, nodeId, partId, tile, buf.get(), tileBufSize));

        // The vox array is zero-indexed whle the Houdini volume
        // data is indexed by vInfo.min*
        exint voxOffsetX = tile.minX - vInfo.minX;
        exint voxOffsetY = tile.minY - vInfo.minY;
        exint voxOffsetZ = tile.minZ - vInfo.minZ;

        // Need to check if the tile ends or if the volume ends or if the bounds
        // of the Volume have been hit
        exint maxX = (voxOffsetX + tileLength > vInfo.xLength) ?
                         (vInfo.xLength - voxOffsetX) :
                         tileLength;

        exint maxY = (voxOffsetY + tileLength > vInfo.yLength) ?
                         (vInfo.yLength - voxOffsetY) :
                         tileLength;

        exint maxZ = (voxOffsetZ + tileLength > vInfo.zLength) ?
                         (vInfo.zLength - voxOffsetZ) :
                         tileLength;

        // Add the tile data to the vox array
        for (exint z = 0; z < maxZ; z++)
        {
            exint zTileOffset = z * tileLength * tileLength;

            for (exint y = 0; y < maxY; y++)
            {
                exint yTileOffset = y * tileLength;

                for (exint x = 0; x < maxX; x++)
                {
                    exint tileOffset = x + yTileOffset + zTileOffset;

                    vox->setValue(x + voxOffsetX, y + voxOffsetY,
                                  z + voxOffsetZ, buf.get()[tileOffset]);
                }
            }
        }

        // Get the next tile
        ENSURE_SUCCESS(
            HAPI_GetNextVolumeTile(&session, nodeId, partId, &tile), session);
    }

    return true;
}

bool
GEOhapiExtractVoxelValues(GEO_PrimVolume *vol,
                          const HAPI_Session &session,
                          HAPI_NodeId nodeId,
                          HAPI_PartId partId,
                          const HAPI_VolumeInfo &vInfo)
{
    // GEO_PrimVolumes should only have scalar values
    UT_ASSERT(vInfo.tupleSize == 1);
    
    UT_VoxelArrayWriteHandleF vox = vol->getVoxelWriteHandle();

    // Storage type is guaranteed to be float or int
    if (vInfo.storage == HAPI_STORAGETYPE_FLOAT)
    {
        CHECK_RETURN(
            extractVoxels<float>(vox, session, nodeId, partId, vInfo));
    }
    else // vInfo.storage == HAPI_STORAGETYPE_INT
    {
        UT_ASSERT(vInfo.storage == HAPI_STORAGETYPE_INT);
        CHECK_RETURN(
            extractVoxels<int>(vox, session, nodeId, partId, vInfo));
    }

    return true;
}

template <class GridType>
static bool
fillVectorGrid(GridType &grid,
               const HAPI_Session &session,
               HAPI_NodeId nodeId,
               HAPI_PartId partId,
               const HAPI_VolumeInfo &vInfo)
{
    typedef typename GridType::ValueType TVec;
    typedef typename TVec::ValueType T;

    const exint tileLength = vInfo.tileSize;
    const exint tileBufSize = tileLength * tileLength * tileLength *
                              vInfo.tupleSize;
    UT_UniquePtr<T> buf(new T[tileBufSize]);

    // Access the voxels on the grid
    typename GridType::Accessor accessor = grid.getAccessor();

    // Get the first tile
    HAPI_VolumeTileInfo tile;
    ENSURE_SUCCESS(
        HAPI_GetFirstVolumeTile(&session, nodeId, partId, &tile), session);

    while (tile.isValid)
    {
        // Get the values from HAPI
        CHECK_RETURN(getVoxelData(
            session, nodeId, partId, tile, buf.get(), tileBufSize));

        // Add the tile data to the grid
        const int maxX = SYSmin(
            tile.minX + tileLength, vInfo.minX + vInfo.xLength);
        const int maxY = SYSmin(
            tile.minY + tileLength, vInfo.minY + vInfo.yLength);
        const int maxZ = SYSmin(
            tile.minZ + tileLength, vInfo.minZ + vInfo.zLength);
        const int minX = SYSmax(vInfo.minX, tile.minX);
        const int minY = SYSmax(vInfo.minY, tile.minY);
        const int minZ = SYSmax(vInfo.minZ, tile.minZ);

        openvdb::Coord xyz;
        int &x = xyz[0];
        int &y = xyz[1];
        int &z = xyz[2];

        for (z = minZ; z < maxZ; z++)
        {
            exint zOffset = (z - tile.minZ) * tileLength * tileLength;

            for (y = minY; y < maxY; y++)
            {
                exint yOffset = (y - tile.minY) * tileLength;

                for (x = minX; x < maxX; x++)
                {
                    // Get the index into the tile buffer
                    exint tileOffset = (x - tile.minX) + yOffset + zOffset;

                    T *startOfVec = buf.get() + (tileOffset * vInfo.tupleSize);

                    accessor.setValueOn(
                        xyz, TVec(startOfVec[0], startOfVec[1], startOfVec[2]));
                }
            }
        }

        // Get the next tile
        ENSURE_SUCCESS(
            HAPI_GetNextVolumeTile(&session, nodeId, partId, &tile), session);
    }

    return true;
}

template <class GridType>
static bool
fillScalarGrid(GridType &grid,
               const HAPI_Session &session,
               HAPI_NodeId nodeId,
               HAPI_PartId partId,
               const HAPI_VolumeInfo &vInfo)
{
    typedef typename GridType::ValueType T;

    const exint tileLength = vInfo.tileSize;
    const exint tileBufSize = tileLength * tileLength * tileLength;
    UT_UniquePtr<T> buf(new T[tileBufSize]);

    // Access the voxels on the grid
    typename GridType::Accessor accessor = grid.getAccessor();

    // Get the first tile
    HAPI_VolumeTileInfo tile;
    ENSURE_SUCCESS(
        HAPI_GetFirstVolumeTile(&session, nodeId, partId, &tile), session);

    while (tile.isValid)
    {
        // Get the values from HAPI
        CHECK_RETURN(getVoxelData(
            session, nodeId, partId, tile, buf.get(), tileBufSize));

        // Add the tile data to the grid
        const int maxX = SYSmin(
            tile.minX + tileLength, vInfo.minX + vInfo.xLength);
        const int maxY = SYSmin(
            tile.minY + tileLength, vInfo.minY + vInfo.yLength);
        const int maxZ = SYSmin(
            tile.minZ + tileLength, vInfo.minZ + vInfo.zLength);
        const int minX = SYSmax(vInfo.minX, tile.minX);
        const int minY = SYSmax(vInfo.minY, tile.minY);
        const int minZ = SYSmax(vInfo.minZ, tile.minZ);

        openvdb::Coord xyz;
        int &x = xyz[0];
        int &y = xyz[1];
        int &z = xyz[2];

        for (z = minZ; z < maxZ; z++)
        {
            exint zOffset = (z - tile.minZ) * tileLength * tileLength;

            for (y = minY; y < maxY; y++)
            {
                exint yOffset = (y - tile.minY) * tileLength;

                for (x = minX; x < maxX; x++)
                {
                    // Get the index into the tile buffer
                    exint tileOffset = (x - tile.minX) + yOffset + zOffset;

                    accessor.setValueOn(xyz, buf.get()[tileOffset]);
                }
            }
        }

        // Get the next tile
        ENSURE_SUCCESS(
            HAPI_GetNextVolumeTile(&session, nodeId, partId, &tile), session);
    }

    return true;
}

bool
GEOhapiInitVDBGrid(openvdb::GridBase::Ptr &grid,
                   const HAPI_Session &session,
                   HAPI_NodeId nodeId,
                   HAPI_PartId partId,
                   const HAPI_VolumeInfo &vInfo)
{
    UT_ASSERT(vInfo.storage == HAPI_STORAGETYPE_FLOAT ||
              vInfo.storage == HAPI_STORAGETYPE_INT);

    // TupleSize is either 3 or 1
    if (vInfo.tupleSize == 3)
    {
        if (vInfo.storage == HAPI_STORAGETYPE_FLOAT)
        {
            openvdb::Vec3fGrid::Ptr fltGrid = openvdb::Vec3fGrid::create();
            CHECK_RETURN(
                fillVectorGrid(*fltGrid, session, nodeId, partId, vInfo));

            grid = fltGrid;
        }
        else // vInfo.storage == HAPI_STORAGETYPE_INT
        {
            openvdb::Vec3IGrid::Ptr intGrid = openvdb::Vec3IGrid::create();
            CHECK_RETURN(
                fillVectorGrid(*intGrid, session, nodeId, partId, vInfo));

            grid = intGrid;
        }
    }
    else // vInfo.tupleSize == 1
    {
        UT_ASSERT(vInfo.tupleSize == 1);

        if (vInfo.storage == HAPI_STORAGETYPE_FLOAT)
        {
            openvdb::FloatGrid::Ptr fltGrid = openvdb::FloatGrid::create();
            CHECK_RETURN(
                fillScalarGrid(*fltGrid, session, nodeId, partId, vInfo));

            grid = fltGrid;
        }
        else // vInfo.storage == HAPI_STORAGETYPE_INT
        {
            openvdb::Int32Grid::Ptr intGrid = openvdb::Int32Grid::create();
            CHECK_RETURN(
                fillScalarGrid(*intGrid, session, nodeId, partId, vInfo));

            grid = intGrid;
        }
    }

    return true;
}

// USD functions
PXR_NAMESPACE_OPEN_SCOPE

GT_Owner
GEOhapiConvertOwner(HAPI_AttributeOwner owner)
{
    switch (owner)
    {
    case HAPI_ATTROWNER_POINT:
        return GT_OWNER_POINT;
    case HAPI_ATTROWNER_VERTEX:
        return GT_OWNER_VERTEX;
    case HAPI_ATTROWNER_PRIM:
        return GT_OWNER_PRIMITIVE;
    case HAPI_ATTROWNER_DETAIL:
        return GT_OWNER_DETAIL;
    case HAPI_ATTROWNER_INVALID:
    default:
        return GT_OWNER_INVALID;
    }
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

SdfPath
GEOhapiNameToNewPath(const UT_StringHolder &name, const SdfPath &parentPath)
{
    UT_String out = name.c_str();

    // The passed in name is allowed to be a relative path.
    HUSDmakeValidUsdPath(out, false, true);

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
GEOhapiAppendDefaultPathName(HAPI_PartType type,
                             const SdfPath &parentPath,
                             GEO_HAPIPrimCounts &counts)
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

SdfPath
GEOhapiGetPrimPath(
        const GEO_HAPIPart &part,
        HAPI_AttributeOwner partition_attrib_owner,
        const SdfPath &parentPath,
        GEO_HAPIPrimCounts &counts,
        const GEO_ImportOptions &options)
{
    const UT_StringMap<GEO_HAPIAttributeHandle> &attrs
            = part.getAttribMap(partition_attrib_owner);

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

    return GEOhapiAppendDefaultPathName(part.getType(), parentPath, counts);
}

PXR_NAMESPACE_CLOSE_SCOPE
