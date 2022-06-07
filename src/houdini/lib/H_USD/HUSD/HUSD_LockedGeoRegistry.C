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

#include "HUSD_LockedGeoRegistry.h"
#include "XUSD_LockedGeoRegistry.h"
#include <UT/UT_Exit.h>
#include <UT/UT_Lock.h>
#include <pxr/usd/sdf/layer.h>
#include <mutex>

PXR_NAMESPACE_USING_DIRECTIVE

static UT_Lock theEntriesLock;
static UT_StringMap<XUSD_LockedGeoPtr> theRegistryEntries;

/*static*/
std::string
HUSD_LockedGeoRegistry::addLockedGeo(
        const UT_StringHolder &geo_identifier,
        const std::map<std::string, std::string> &args,
        const GU_ConstDetailHandle &gdh)
{
    UT_AutoLock l(theEntriesLock);
    
    // Make sure we remove all our references to locked geos
    // before the teardown gets to XUSD_LockedGeoRegistry
    static std::once_flag registered;
    std::call_once(registered, []() {
        UT_Exit::addExitCallback([](void*) {
            UT_AutoLock l(theEntriesLock);
            theRegistryEntries.clear();
        }, nullptr);
    });
    
    // If the following line stops compiling, it's likely because
    // the definition of SdfFileFormat::FileFormatArguments changed
    const XUSD_LockedGeoArgs &lockedgeoargs = args;
    XUSD_LockedGeoPtr lockedgeo = XUSD_LockedGeoRegistry::createLockedGeo(
            geo_identifier, lockedgeoargs, gdh);
    if (lockedgeo)
    {
        std::string locked_geo_identifier =
            SdfLayer::CreateIdentifier(geo_identifier.toStdString(), lockedgeoargs);
        theRegistryEntries[locked_geo_identifier] = lockedgeo;
        return locked_geo_identifier;
    }

    return std::string();
}

/*static*/
bool
HUSD_LockedGeoRegistry::removeLockedGeo(const UT_StringHolder &identifier)
{
    UT_AutoLock l(theEntriesLock);
    return theRegistryEntries.erase(identifier);
}
