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

