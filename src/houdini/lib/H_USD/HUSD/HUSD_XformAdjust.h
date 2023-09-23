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

#ifndef __HUSD_XformAdjust_h__
#define __HUSD_XformAdjust_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_StringMap.h>

class HUSD_TimeCode;

class HUSD_API HUSD_XformAdjust
{
public:
			 HUSD_XformAdjust(HUSD_AutoAnyLock &lock,
                                const UT_StringHolder &authored_layer_path,
				const UT_StringMap<UT_StringHolder> &
				    authored_layer_args,
                                const GU_DetailHandle &gdh,
				const HUSD_TimeCode &timecode);
			~HUSD_XformAdjust();

                        UT_NON_COPYABLE(HUSD_XformAdjust);

    bool		 adjustXformsForAuthoredPrims(
				const HUSD_AutoWriteLock &lock);

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

