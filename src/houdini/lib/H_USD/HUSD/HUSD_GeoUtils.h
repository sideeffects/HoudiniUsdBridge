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

#ifndef __HUSD_GeoUtils_h__
#define __HUSD_GeoUtils_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_LockedStage.h"
#include <UT/UT_StringHolder.h>

class GU_Detail;
class HUSD_FindPrims;

bool
HUSD_API HUSDimportUsdIntoGeometry(
	GU_Detail *gdp,
	const HUSD_LockedStagePtr &locked_stage,
	const HUSD_FindPrims &findprims,
	const UT_StringHolder &purpose,
	const UT_StringHolder &traversal,
	const UT_StringHolder &pathattribname,
	const UT_StringHolder &nameattribname,
	const HUSD_TimeCode &timecode);

bool
HUSD_API HUSDimportUsdIntoGeometry(
	GU_Detail *gdp,
	void *stage_ptr,
	const HUSD_FindPrims &findprims,
	const UT_StringHolder &purpose,
	const UT_StringHolder &traversal,
	const UT_StringHolder &pathattribname,
	const UT_StringHolder &nameattribname,
	const HUSD_TimeCode &timecode);

#endif
