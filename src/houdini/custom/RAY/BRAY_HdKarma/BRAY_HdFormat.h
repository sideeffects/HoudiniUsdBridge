/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdFormat.h (BRAY Library, C++)
 *
 * COMMENTS:
 */

#ifndef __BRAY_HdFormat__
#define __BRAY_HdFormat__

// Helper functions for UTformat()

#include <pxr/base/tf/token.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec2h.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/gf/vec4h.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/range1d.h>
#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/range2d.h>
#include <pxr/base/gf/range3f.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/timeCode.h>
#include <pxr/imaging/hd/types.h>
#include <UT/UT_Format.h>
#include <UT/UT_WorkBuffer.h>
#include <UT/UT_StringStream.h>

PXR_NAMESPACE_OPEN_SCOPE

#define FORMAT_BASIC_TYPE(TYPE) \
    static SYS_FORCE_INLINE size_t \
    format(char *buffer, size_t bufsize, const TYPE &v) \
    { \
        UT::Format::Writer      writer(buffer, bufsize); \
        UT::Format::Formatter<> f; \
        UT_OStringStream        os; \
        os << v; \
        return f.format(writer, "{}", {os.str()}); \
    } \
    /* end macro */

#define FORMAT_TYPE(TYPE) \
    FORMAT_BASIC_TYPE(TYPE) \
    FORMAT_BASIC_TYPE(VtArray<TYPE>) \
    /* end macro */

FORMAT_TYPE(VtValue)
FORMAT_TYPE(TfToken)
FORMAT_TYPE(SdfPath)
FORMAT_TYPE(SdfAssetPath)
FORMAT_TYPE(SdfTimeCode)
FORMAT_TYPE(GfVec2h)
FORMAT_TYPE(GfVec2i)
FORMAT_TYPE(GfVec2f)
FORMAT_TYPE(GfVec2d)
FORMAT_TYPE(GfVec3h)
FORMAT_TYPE(GfVec3i)
FORMAT_TYPE(GfVec3f)
FORMAT_TYPE(GfVec3d)
FORMAT_TYPE(GfVec4h)
FORMAT_TYPE(GfVec4i)
FORMAT_TYPE(GfVec4f)
FORMAT_TYPE(GfVec4d)
FORMAT_TYPE(GfQuath)
FORMAT_TYPE(GfQuatf)
FORMAT_TYPE(GfQuatd)
FORMAT_TYPE(GfMatrix2f)
FORMAT_TYPE(GfMatrix2d)
FORMAT_TYPE(GfMatrix3f)
FORMAT_TYPE(GfMatrix3d)
FORMAT_TYPE(GfMatrix4f)
FORMAT_TYPE(GfMatrix4d)
FORMAT_TYPE(GfRange1f)
FORMAT_TYPE(GfRange1d)
FORMAT_TYPE(GfRange2f)
FORMAT_TYPE(GfRange2d)
FORMAT_TYPE(GfRange3f)
FORMAT_TYPE(GfRange3d)
FORMAT_TYPE(GfRect2i)

// Special handling for POD VtArray types
FORMAT_BASIC_TYPE(VtArray<bool>)
FORMAT_BASIC_TYPE(VtArray<int8>)
FORMAT_BASIC_TYPE(VtArray<int16>)
FORMAT_BASIC_TYPE(VtArray<int32>)
FORMAT_BASIC_TYPE(VtArray<int64>)
FORMAT_BASIC_TYPE(VtArray<uint8>)
FORMAT_BASIC_TYPE(VtArray<uint16>)
FORMAT_BASIC_TYPE(VtArray<uint32>)
FORMAT_BASIC_TYPE(VtArray<uint64>)
FORMAT_BASIC_TYPE(VtArray<fpreal32>)
FORMAT_BASIC_TYPE(VtArray<fpreal64>)
FORMAT_BASIC_TYPE(VtArray<std::string>)
FORMAT_BASIC_TYPE(VtArray<UT_StringHolder>)

#undef FORMAT_TYPE

PXR_NAMESPACE_CLOSE_SCOPE

#endif

