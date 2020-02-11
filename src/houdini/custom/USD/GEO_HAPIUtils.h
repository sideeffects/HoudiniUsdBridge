

#ifndef __GEO_HAPI_UTILS_H__
#define __GEO_HAPI_UTILS_H__

#include "GEO_FilePrim.h"
#include "GEO_FilePrimUtils.h"
#include "pxr/base/tf/declarePtrs.h"
#include "pxr/pxr.h"
#include "pxr/usd/sdf/abstractData.h"
#include "pxr/usd/sdf/data.h"
#include "pxr/usd/sdf/fileFormat.h"
#include <HAPI/HAPI.h>
#include <UT/UT_WorkBuffer.h>
#include <pxr/usd/usdGeom/tokens.h>

#define CLEANUP(session)                                                       \
    HAPI_Cleanup(&session);                                                    \
    HAPI_CloseSession(&session);

#define ENSURE_SUCCESS(result, session)                                        \
    if ((result) != HAPI_RESULT_SUCCESS)                                       \
    {                                                                          \
        CLEANUP(session);                                                      \
        return false;                                                          \
    }

#define CHECK_RETURN(result)                                                   \
    if (!result)                                                               \
    {                                                                          \
        return false;                                                          \
    }

// Utility functions for GEO_HAPI .C files

// for extracting strings from HAPI StringHandles
bool GEOhapiExtractString(const HAPI_Session &session,
                          HAPI_StringHandle &handle,
                          UT_WorkBuffer &buf);

// USD functions

PXR_NAMESPACE_OPEN_SCOPE

const TfToken &GEOhapiCurveOwnerToInterpToken(HAPI_AttributeOwner owner);

const TfToken &GEOhapiMeshOwnerToInterpToken(HAPI_AttributeOwner owner);

const TfToken &GEOhapiCurveTypeToBasisToken(HAPI_CurveType type);

void GEOhapiInitXformAttrib(GEO_FilePrim &fileprim,
                            const UT_Matrix4D &prim_xform,
                            const GEO_ImportOptions &options);

SYS_FORCE_INLINE
void GEOhapiInitKind(GEO_FilePrim &fileprim,
    GEO_KindSchema kindschema,
    GEO_KindGuide kindguide)
{
    GEOsetKind(fileprim, kindschema, kindguide);
}

void GEOhapiReversePolygons(GT_DataArrayHandle &vertArrOut,
                            const GT_DataArrayHandle &faceCounts,
                            const GT_DataArrayHandle &vertices);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_HAPI_UTILS_H__