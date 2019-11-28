/*
 * Copyright 2019 Side Effects Software Inc.
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
 *
 * Produced by:
 *	Rafeek Rahamut
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_GetMetadata.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_GetMetadata::HUSD_GetMetadata(HUSD_AutoAnyLock &lock)
    : myReadLock(lock)
{
}

HUSD_GetMetadata::~HUSD_GetMetadata()
{
}

template<typename UtValueType>
bool
HUSD_GetMetadata::getMetadata(const UT_StringRef &object_path,
	const UT_StringRef &metadata_name, UtValueType &value) const
{
    auto outdata = myReadLock.constData();
    if( !outdata || !outdata->isStageValid() )
	return false;

    auto stage = outdata->stage();
    auto obj = stage->GetObjectAtPath( HUSDgetSdfPath( object_path ));
    if( !obj )
	return false;

    TfToken key_path( metadata_name.toStdString() );
    return HUSDgetMetadata(obj, key_path, value);
}

//----------------------------------------------------------------------------
// Instantiate the template types explicitly


#define HUSD_EXPLICIT_INSTANTIATION(UtType)				\
    template HUSD_API_TINST bool HUSD_GetMetadata::getMetadata(		\
	const UT_StringRef	&obj_path,				\
	const UT_StringRef	&metadata_name,				\
	UtType			&value) const;				

#define HUSD_EXPLICIT_INSTANTIATION_PAIR(UtType)			\
    HUSD_EXPLICIT_INSTANTIATION(UtType)					\
    HUSD_EXPLICIT_INSTANTIATION(UT_Array<UtType>)

HUSD_EXPLICIT_INSTANTIATION_PAIR(bool)
HUSD_EXPLICIT_INSTANTIATION_PAIR(int)
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

#undef HUSD_EXPLICIT_INSTANTIATION
#undef HUSD_EXPLICIT_INSTANTIATION_PAIR

