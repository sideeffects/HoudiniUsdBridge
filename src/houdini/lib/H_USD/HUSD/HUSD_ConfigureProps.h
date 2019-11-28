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

#ifndef __HUSD_ConfigureProps_h__
#define __HUSD_ConfigureProps_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Utils.h"
#include <UT/UT_StringHolder.h>

class HUSD_FindProps;

class HUSD_API HUSD_ConfigureProps
{
public:
			 HUSD_ConfigureProps(HUSD_AutoWriteLock &lock);
			~HUSD_ConfigureProps();

    bool		 setVariability(const HUSD_FindProps &findprops,
				HUSD_Variability variability) const;
    bool		 setColorSpace(const HUSD_FindProps &findprops,
				const UT_StringRef &colorspace) const;
    bool		 setInterpolation(const HUSD_FindProps &findprops,
				const UT_StringRef &interpolation) const;
    bool		 setElementSize(const HUSD_FindProps &findprops,
				int element_size) const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

