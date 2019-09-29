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

#ifndef __HUSD_FindInstanceIds_h__
#define __HUSD_FindInstanceIds_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Utils.h"
#include <UT/UT_IntArray.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>
#include <pxr/pxr.h>

class HUSD_API HUSD_FindInstanceIds
{
public:
				 HUSD_FindInstanceIds(HUSD_AutoAnyLock &lock,
					 const UT_StringRef &primpath =
					    UT_StringHolder::theEmptyString,
					 const UT_StringRef &instanceidpattern =
					    UT_StringHolder::theEmptyString);
				~HUSD_FindInstanceIds();

    const UT_StringHolder	&instanceIdPattern() const
				 { return myInstanceIdPattern; }
    void			 setInstanceIdPattern(
					const UT_StringHolder &pattern);

    const UT_StringHolder	&primPath() const
				 { return myPrimPath; }
    void			 setPrimPath(const UT_StringHolder &primpath);

    const UT_IntArray		&getInstanceIds(const HUSD_TimeCode &tc) const;

private:
    class husd_FindInstanceIdsPrivate;

    UT_UniquePtr<husd_FindInstanceIdsPrivate>	 myPrivate;
    HUSD_AutoAnyLock				&myAnyLock;
    UT_StringHolder				 myPrimPath;
    UT_StringHolder				 myInstanceIdPattern;
};

#endif

