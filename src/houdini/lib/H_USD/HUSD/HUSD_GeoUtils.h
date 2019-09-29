/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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
#include <gusd/UT_Gf.h>

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
	fpreal t);

#endif
