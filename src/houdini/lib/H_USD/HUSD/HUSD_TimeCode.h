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

#ifndef __HUSD_TimeCode_h__
#define __HUSD_TimeCode_h__

#include "HUSD_API.h"
#include <SYS/SYS_Types.h>

class HUSD_API HUSD_TimeCode
{
public:
    enum TimeFormat {
	TIME,
	FRAME
    };

    // Constructs a pure default time code. Use this only if there is really
    // no sensible fallback time/frame value available.
			 HUSD_TimeCode()
			     : myFrame(0.0),
			       myIsDefault(true)
			 { }
    // Constructs a time code at a specific frame. The time code can still be
    // marked as "default", in which case we still record the provided frame
    // number for cases where a default time is not acceptable (such as when
    // querying an attribute from a stage).
    explicit		 HUSD_TimeCode(fpreal frame,
				bool is_default = false)
			     : myFrame(frame),
			       myIsDefault(is_default)
			 { }
    // Constructs a time code at a specific time or frame. The time code can
    // still be marked as "default", in which case we still record the provided
    // frame number for cases where a default time is not acceptable (such as
    // when querying an attribute from a stage).
    explicit		 HUSD_TimeCode(fpreal time,
				TimeFormat format,
				bool is_default = false);

    /// Returns a time code with the same specific time/frame as this one,
    /// but whose default flag is cleared. This is needed for cases where
    /// a default time is not acceptable (such as when setting a time-varying
    /// attribute on a stage).
    HUSD_TimeCode	 getNonDefaultTimeCode() const
			 { return HUSD_TimeCode(myFrame, false); }

    bool		 operator==(const HUSD_TimeCode &other) const
			 { return other.myIsDefault ? myIsDefault :
			     (other.myFrame == myFrame); }
    bool		 operator!=(const HUSD_TimeCode &other) const
			 { return !(other == *this); }

    fpreal		 time() const;
    fpreal		 frame() const
			 { return myFrame; }
    bool		 isDefault() const
			 { return myIsDefault; }

private:
    fpreal		 myFrame;
    bool		 myIsDefault;
};

#endif

