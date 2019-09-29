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

#ifndef __HUSD_Blend_h__
#define __HUSD_Blend_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>

class HUSD_TimeCode;

class HUSD_API HUSD_Blend
{
public:
			 HUSD_Blend();
			~HUSD_Blend();

    bool		 setBlendHandle(const HUSD_DataHandle &src);
    bool		 execute(HUSD_AutoWriteLock &lock,
				fpreal blend,
				const HUSD_TimeCode &timecode,
				UT_StringArray &modified_prims) const;

    bool		 getIsTimeVarying() const
			 { return myTimeVarying; }

private:
    class husd_BlendPrivate;

    UT_UniquePtr<husd_BlendPrivate>	 myPrivate;
    mutable bool			 myTimeVarying;
};

#endif

