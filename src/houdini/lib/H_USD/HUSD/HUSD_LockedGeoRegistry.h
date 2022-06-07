/*
 * Copyright 2022 Side Effects Software Inc.
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

#ifndef __HUSD_LockedGeoRegistry_h__
#define __HUSD_LockedGeoRegistry_h__

#include "HUSD_API.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>

class HUSD_API HUSD_LockedGeoRegistry
{
public:
    static std::string addLockedGeo(
            const UT_StringHolder &geo_identifier,
            const std::map<std::string, std::string> &args,
            const GU_ConstDetailHandle &gdh);
    static bool removeLockedGeo(const UT_StringHolder &locked_geo_identifier);
};

#endif
