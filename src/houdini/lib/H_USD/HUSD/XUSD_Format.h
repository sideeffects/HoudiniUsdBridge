/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	XUSD_Format.h (HUSD Library, C++)
 *
 * COMMENTS:
 */

#ifndef __XUSD_Format__
#define __XUSD_Format__

/// @file Implementations for UTformat() style printing

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
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/imaging/hd/types.h>
#include <UT/UT_Format.h>
#include <UT/UT_WorkBuffer.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Format for a TfToken
static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, const TfToken &val)
{
    UT::Format::Writer		writer(buffer, bufsize);
    UT::Format::Formatter<>	f;
    return f.format(writer, "{}", {val.GetString()});
}

static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, const SdfPath &path)
{
    UT::Format::Writer		writer(buffer, bufsize);
    UT::Format::Formatter<>	f;
    return f.format(writer, "{}", {path.GetString()});
}

static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, HdFormat val)
{
    UT::Format::Writer		writer(buffer, bufsize);
    UT::Format::Formatter<>	f;
    const char			*tname = nullptr;
    size_t			 size = HdGetComponentCount(val);
    switch (HdGetComponentFormat(val))
    {
	case HdFormatUNorm8:	tname = "uint8"; break;
	case HdFormatSNorm8:	tname = "int8"; break;
	case HdFormatFloat16:	tname = "fpreal16"; break;
	case HdFormatFloat32:	tname = "fpreal32"; break;
	case HdFormatInt32:	tname = "int32"; break;
	default:		tname = "<undefined_type>"; break;
    }
    return f.format(writer, "{}[{}]", {tname, size});
}

namespace
{
    template <typename T>
    static SYS_FORCE_INLINE size_t
    formatVector(char *buffer, size_t bufsize, const T *data, size_t size)
    {
	UT_WorkBuffer	tmp;
	if (size)
	{
	    tmp.format("{}", data[0]);
	    for (size_t i = 1; i < size; ++i)
		tmp.appendFormat(", {}", data[i]);
	}
	UT::Format::Writer		writer(buffer, bufsize);
	UT::Format::Formatter<>	f;
	return f.format(writer, "[{}]", {tmp});
    }

    template <> size_t
    formatVector<GfHalf>(char *buffer, size_t bufsize,
	    const GfHalf *data, size_t size)
    {
	UT_WorkBuffer	tmp;
	if (size)
	{
	    tmp.format("{}", float(data[0]));
	    for (size_t i = 1; i < size; ++i)
		tmp.appendFormat(", {}", float(data[i]));
	}
	UT::Format::Writer		writer(buffer, bufsize);
	UT::Format::Formatter<>	f;
	return f.format(writer, "[{}]", {tmp});
    }

    template <typename T>
    static SYS_FORCE_INLINE size_t
    formatIterator(char *buffer, size_t bufsize, T begin, const T &end)
    {
	UT_WorkBuffer	tmp;
	if (begin != end)
	{
	    tmp.format("{}", *begin);
	    for (++begin; begin != end; ++begin)
		tmp.appendFormat(", {}", *begin);
	}
	UT::Format::Writer		writer(buffer, bufsize);
	UT::Format::Formatter<>	f;
	return f.format(writer, "[{}]", {tmp});
    }
}


static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, const TfTokenVector &vtok)
{
    return formatVector(buffer, bufsize, &vtok[0], vtok.size());
}

#define FORMAT_TYPE(TYPE, METHOD, SIZE) \
    static SYS_FORCE_INLINE size_t \
    format(char *buffer, size_t bufsize, const TYPE &val) \
    { return formatVector(buffer, bufsize, val.METHOD(), SIZE); }

FORMAT_TYPE(GfVec2h, data, 2)
FORMAT_TYPE(GfVec2i, data, 2)
FORMAT_TYPE(GfVec2f, data, 2)
FORMAT_TYPE(GfVec2d, data, 2)
FORMAT_TYPE(GfVec3h, data, 3)
FORMAT_TYPE(GfVec3i, data, 3)
FORMAT_TYPE(GfVec3f, data, 3)
FORMAT_TYPE(GfVec3d, data, 3)
FORMAT_TYPE(GfVec4h, data, 4)
FORMAT_TYPE(GfVec4i, data, 4)
FORMAT_TYPE(GfVec4f, data, 4)
FORMAT_TYPE(GfVec4d, data, 4)

FORMAT_TYPE(GfMatrix2f, GetArray, 4)
FORMAT_TYPE(GfMatrix2d, GetArray, 4)
FORMAT_TYPE(GfMatrix3f, GetArray, 9)
FORMAT_TYPE(GfMatrix3d, GetArray, 9)
FORMAT_TYPE(GfMatrix4f, GetArray, 16)
FORMAT_TYPE(GfMatrix4d, GetArray, 16)

#undef FORMAT_TYPE

template <typename T>
static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, const VtArray<T> &arr)
{
    return formatIterator(buffer, bufsize, arr.begin(), arr.end());
}

template <typename T>
static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, const VtValue &val)
{
    UT::Format::Writer		writer(buffer, bufsize);
    UT::Format::Formatter<>	f;
    return f.format(writer, "{}", val.Get<T>());
}

namespace
{
    template <typename T>
    static SYS_FORCE_INLINE size_t
    formatQuat(char *buffer, size_t bufsize, const T &q)
    {
	const auto &ii = q.GetImaginary();
	UT::Format::Writer		writer(buffer, bufsize);
	UT::Format::Formatter<>	f;
	return f.format(writer, "{0}+({1},{2},{3})i",
		{q.GetReal(), ii[0], ii[1], ii[2]});
    }
}

static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, const GfQuatd &q)
{
    return formatQuat(buffer, bufsize, q);
}
static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, const GfQuatf &q)
{
    return formatQuat(buffer, bufsize, q);
}
static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, const GfQuath &q)
{
    // Specialization for GfHalf
    return formatQuat(buffer, bufsize, GfQuatf(q.GetReal(), q.GetImaginary()));
}


PXR_NAMESPACE_CLOSE_SCOPE

#endif
