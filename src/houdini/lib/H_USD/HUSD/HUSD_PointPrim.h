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
