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

