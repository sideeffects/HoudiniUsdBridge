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

#ifndef __GEO_HAPI_UTILS_H__
#define __GEO_HAPI_UTILS_H__

#include "GEO_FilePrim.h"
#include "GEO_FilePrimUtils.h"
#include "pxr/base/tf/declarePtrs.h"
#include "pxr/pxr.h"
#include "pxr/usd/sdf/abstractData.h"
#include "pxr/usd/sdf/data.h"
#include "pxr/usd/sdf/fileFormat.h"
#include <GEO/GEO_PrimVolume.h>
#include <GU/GU_PrimVDB.h>
#include <GT/GT_DataArray.h>
#include <HAPI/HAPI.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_WorkBuffer.h>
#include <pxr/usd/usdGeom/tokens.h>

class GEO_HAPIPart;

#define GEO_HDA_PARM_ARG_PREFIX "_houdiniParamArg_"

#define GEO_HDA_PARM_NUMERIC_PREFIX GEO_HDA_PARM_ARG_PREFIX "_num_"
#define GEO_HDA_PARM_STRING_PREFIX GEO_HDA_PARM_ARG_PREFIX "_str_"

#define GEO_HDA_PARM_SEPARATOR " "

#define ENSURE_SUCCESS_MESSAGE(result, message)                                \
    if ((result) != HAPI_RESULT_SUCCESS)                                       \
    {                                                                          \
        PXR_NAMESPACE_USING_DIRECTIVE                                          \
        TF_WARN(message);                                                      \
        return false;                                                          \
    }

#define ENSURE_SUCCESS(result, session)                                        \
    if ((result) != HAPI_RESULT_SUCCESS)                                       \
    {                                                                          \
        GEOhapiSendError(session);                                             \
        return false;                                                          \
    }

#define ENSURE_COOK_SUCCESS(result, session)                                   \
    if ((result) != HAPI_STATE_READY)                                          \
    {                                                                          \
        GEOhapiSendCookError(session, assetId);                                \
        return false;                                                          \
    }

#define CHECK_RETURN(result)                                                   \
    if (!result)                                                               \
        return false;

// Utility functions for GEO_HAPI .C files

// This is to keep track of the number of
// prims that have displayed for naming
struct GEO_HAPIPrimCounts
{
    exint boxes = 0;
    exint curves = 0;
    exint instances = 0;
    exint meshes = 0;
    exint spheres = 0;
    exint volumes = 0;

    exint others = 0;
    exint prototypes = 0;
};

// For extracting strings from HAPI StringHandles
bool GEOhapiExtractString(const HAPI_Session &session,
                          HAPI_StringHandle &handle,
                          UT_WorkBuffer &buf);

void GEOhapiSendCookError(const HAPI_Session &session, HAPI_NodeId node_id);

void GEOhapiSendError(const HAPI_Session &session);

template <class T>
void
GEOhapiConvertXform(const HAPI_Transform &hapiXform, UT_Matrix4T<T> &xform)
{
    UT_XformOrder xformOrder;
    xformOrder.rotOrder(UT_XformOrder::ZYX);

    switch (hapiXform.rstOrder)
    {
    case HAPI_TRS:
        xformOrder.mainOrder(UT_XformOrder::rstOrder::TRS);
        break;

    case HAPI_TSR:
        xformOrder.mainOrder(UT_XformOrder::rstOrder::TSR);
        break;

    case HAPI_RTS:
        xformOrder.mainOrder(UT_XformOrder::rstOrder::RTS);
        break;

    case HAPI_RST:
        xformOrder.mainOrder(UT_XformOrder::rstOrder::RST);
        break;

    case HAPI_STR:
        xformOrder.mainOrder(UT_XformOrder::rstOrder::STR);
        break;

    case HAPI_SRT:
        xformOrder.mainOrder(UT_XformOrder::rstOrder::SRT);
        break;

    default:
        UT_ASSERT_P(false && "Unexpected struct value");
    }

    UT_QuaternionT<fpreal32> quat(hapiXform.rotationQuaternion);
    UT_Vector3F rot = quat.computeRotations(xformOrder);
    // convert to degrees
    rot.radToDeg();

    xform.identity();

    SYS_STATIC_ASSERT(sizeof(fpreal32) == sizeof(float));
    const fpreal32 *t = hapiXform.position;
    const fpreal32 *r = rot.data();
    const fpreal32 *s = hapiXform.scale;
    const fpreal32 *sh = hapiXform.shear;
    xform.xform(xformOrder, t[0], t[1], t[2], r[0], r[1], r[2], s[0], s[1],
                s[2], sh[0], sh[1], sh[2], 0, 0, 0);
}

GT_Type GEOhapiAttribType(HAPI_AttributeTypeInfo typeinfo);

// Fills the data of the GEO_PrimVolume with data extracted from HAPI
bool GEOhapiExtractVoxelValues(GEO_PrimVolume *vol,
                               const HAPI_Session &session,
                               HAPI_NodeId nodeId,
                               HAPI_PartId partId,
                               const HAPI_VolumeInfo &vInfo);

// Fills grid pointer with voxel values extracted from HAPI
bool GEOhapiInitVDBGrid(openvdb::GridBase::Ptr &grid,
                        const HAPI_Session &session,
                        HAPI_NodeId nodeId,
                        HAPI_PartId partId,
                        const HAPI_VolumeInfo &vInfo);

//
// USD
//

PXR_NAMESPACE_OPEN_SCOPE

GT_Owner GEOhapiConvertOwner(HAPI_AttributeOwner owner);

const TfToken &GEOhapiCurveTypeToBasisToken(HAPI_CurveType type);

// Functions to help create a primitive path for parts

SdfPath GEOhapiAppendDefaultPathName(HAPI_PartType type,
                                     const SdfPath &parentPath,
                                     GEO_HAPIPrimCounts &counts);

SdfPath GEOhapiNameToNewPath(const UT_StringHolder &name,
                             const SdfPath &parentPath);

SdfPath GEOhapiGetPrimPath(const GEO_HAPIPart &part,
                           const SdfPath &parentPath,
                           GEO_HAPIPrimCounts &counts,
                           const GEO_ImportOptions &options);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_HAPI_UTILS_H__
