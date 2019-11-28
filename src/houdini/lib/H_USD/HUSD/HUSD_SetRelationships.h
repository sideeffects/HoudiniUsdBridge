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

#ifndef __HUSD_SetRelationships_h__
#define __HUSD_SetRelationships_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>

class HUSD_API HUSD_SetRelationships
{
public:
			 HUSD_SetRelationships(HUSD_AutoWriteLock &lock);
			~HUSD_SetRelationships();

    bool		 setRelationship(const UT_StringRef &primpath,
				const UT_StringRef &relationship_name,
				const UT_StringArray &target_paths) const;

    bool		 blockRelationship(
                                const UT_StringRef &primpath,
				const UT_StringRef &relationship_name) const;

    bool		 addRelationshipTarget(
                                const UT_StringRef &primpath,
				const UT_StringRef &relationship_name,
				const UT_StringRef &target_path) const;

    bool		 removeRelationshipTarget(
                                const UT_StringRef &primpath,
				const UT_StringRef &relationship_name,
				const UT_StringRef &target_path) const;


private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

