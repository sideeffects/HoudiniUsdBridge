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

#ifndef __HUSD_XformAdjust_h__
#define __HUSD_XformAdjust_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_UniquePtr.h>
#include <UT/UT_StringMap.h>

class HUSD_TimeCode;

class HUSD_API HUSD_XformAdjust
{
public:
			 HUSD_XformAdjust(HUSD_AutoAnyLock &lock,
				const HUSD_TimeCode &timecode);
			~HUSD_XformAdjust();

    bool		 adjustXformsForAuthoredPrims(
				const HUSD_AutoWriteLock &lock,
				const UT_StringHolder &authored_layer_path,
				const UT_StringMap<UT_StringHolder> &
				    authored_layer_args) const;

    void                 setAuthorDefaultValues(bool author_default_values)
                         { myAuthorDefaultValues = author_default_values; }
    bool                 authorDefaultValues() const
                         { return myAuthorDefaultValues; }

    bool		 getIsTimeVarying() const;

private:
    class husd_XformAdjustPrivate;

    UT_UniquePtr<husd_XformAdjustPrivate>	 myPrivate;
    bool                                         myAuthorDefaultValues;
};

#endif

