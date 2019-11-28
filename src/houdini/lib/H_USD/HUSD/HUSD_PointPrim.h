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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_PointPrim_h__
#define __HUSD_PointPrim_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_Array.h>
#include <UT/UT_ArrayStringSet.h>
#include <GA/GA_Attribute.h>
#include <GU/GU_Detail.h>

class HUSD_TimeCode;
class UT_Options;

class HUSD_API HUSD_PointPrim
{
public:
    static bool		 extractTransforms(HUSD_AutoAnyLock &readlock,
				const UT_StringRef &primpath,
				UT_Vector3FArray &positions,
				UT_Array<UT_QuaternionH> &orientations,
				UT_Vector3FArray &scales,
				const HUSD_TimeCode &timecode,
				bool doorient,
				bool doscale,
				const UT_Matrix4D *transform = nullptr);

    static bool		 extractTransforms(HUSD_AutoAnyLock &readlock,
				const UT_StringRef &primpath,
				UT_Matrix4DArray &xforms,
				const HUSD_TimeCode &timecode,
				bool doorient,
				bool doscale,
				const UT_Matrix4D *transform = nullptr);

    static bool		 transformInstances(HUSD_AutoWriteLock &writelock,
				const UT_StringRef &primpath,
				const UT_IntArray &indices,
				const UT_Array<UT_Matrix4D> &xforms,
				const HUSD_TimeCode &timecode);

    static bool		 scatterArrayAttributes(HUSD_AutoWriteLock &writelock,
				const UT_StringRef &primpath,
				const UT_ArrayStringSet &attribnames,
				const HUSD_TimeCode &timecode,
				const UT_StringArray &targetprimpaths);

    static bool		 scatterSopArrayAttributes(HUSD_AutoWriteLock &writelock,
				const GU_Detail *gdp,
				const GA_PointGroup *group,
				const UT_Array<const GA_Attribute*> &attribs,
				const HUSD_TimeCode &timecode,
				const UT_StringArray &targetprimpaths);

    static bool		 copySopArrayAttributes(HUSD_AutoWriteLock &writelock,
				const GU_Detail *gdp,
				const GA_PointGroup *group,
				const UT_Array<const GA_Attribute*> &attribs,
				const HUSD_TimeCode &timecode,
				const UT_StringRef &targetprimpath);

};

#endif
