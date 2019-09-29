/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *      Calvin Gu
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_SetMetadata.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_SetMetadata::HUSD_SetMetadata(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_SetMetadata::~HUSD_SetMetadata()
{
}

static inline bool
husdGetObjAndKeyPath(UsdObject &obj, TfToken &keypath, HUSD_AutoWriteLock &lock,
	const UT_StringRef &object_path, const UT_StringRef &metadata_name)
{
    auto	 outdata = lock.data();
    if (!outdata || !outdata->isStageValid())
	return false;

    auto stage = outdata->stage();

    obj = stage->GetObjectAtPath( HUSDgetSdfPath( object_path ));
    if( !obj )
	return false;

    keypath = TfToken( metadata_name.toStdString() );
    return true;
}

template<typename UtValueType>
bool
HUSD_SetMetadata::setMetadata(const UT_StringRef &path,
	const UT_StringRef &name, const UtValueType &value) const
{
    UsdObject	obj;
    TfToken	key_path;
    if( !husdGetObjAndKeyPath( obj, key_path, myWriteLock, path, name))
	return false;

    return HUSDsetMetadata(obj, key_path, value);
}

bool
HUSD_SetMetadata::clearMetadata(const UT_StringRef &path,
	const UT_StringRef &name) const
{
    UsdObject	obj;
    TfToken	key_path;
    if( !husdGetObjAndKeyPath( obj, key_path, myWriteLock, path, name ))
	return false;

    return HUSDclearMetadata(obj, key_path);
}

#define HUSD_EXPLICIT_INSTANTIATION(UtType)				\
    template HUSD_API_TINST bool HUSD_SetMetadata::setMetadata(		\
	const UT_StringRef	&primpath,				\
	const UT_StringRef	&metadata_name,				\
	const UtType		&value) const;				\

#define HUSD_EXPLICIT_INSTANTIATION_PAIR(UtType)			\
    HUSD_EXPLICIT_INSTANTIATION(UtType)					\
    HUSD_EXPLICIT_INSTANTIATION(UT_Array<UtType>)

HUSD_EXPLICIT_INSTANTIATION_PAIR(bool)
HUSD_EXPLICIT_INSTANTIATION_PAIR(int32)
HUSD_EXPLICIT_INSTANTIATION_PAIR(int64)
HUSD_EXPLICIT_INSTANTIATION_PAIR(fpreal32)
HUSD_EXPLICIT_INSTANTIATION_PAIR(fpreal64)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_StringHolder)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector2i)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector3i)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector4i)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector2F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector3F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector4F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector2D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector3D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Vector4D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_QuaternionH)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_QuaternionF)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_QuaternionD)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix2F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix3F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix4F)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix2D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix3D)
HUSD_EXPLICIT_INSTANTIATION_PAIR(UT_Matrix4D)

// Special case for `const char *` arguments.
HUSD_EXPLICIT_INSTANTIATION(char * const)
HUSD_EXPLICIT_INSTANTIATION(UT_Array<const char *>)

#undef HUSD_EXPLICIT_INSTANTIATION
#undef HUSD_EXPLICIT_INSTANTIATION_PAIR

