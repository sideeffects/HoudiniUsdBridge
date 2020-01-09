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

#ifndef __HUSD_CreateVariants_h__
#define __HUSD_CreateVariants_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_UniquePtr.h>

class HUSD_TimeCode;

class HUSD_API HUSD_CreateVariants
{
public:
			 HUSD_CreateVariants();
			~HUSD_CreateVariants();

    bool		 addHandle(const HUSD_DataHandle &src,
				const UT_StringHolder &srcpath,
				const UT_StringHolder &variantname);
    bool		 execute(HUSD_AutoWriteLock &lock,
				const UT_StringRef &primpath,
				const UT_StringRef &variantset,
                                bool checkopinions,
                                const HUSD_TimeCode &checkopinionstimecode,
                                UT_StringArray &weakeropinions) const;

private:
    class husd_CreateVariantsPrivate;

    UT_UniquePtr<husd_CreateVariantsPrivate>	 myPrivate;
    UT_StringArray				 mySrcPaths;
    UT_StringArray				 myVariantNames;
};

#endif

