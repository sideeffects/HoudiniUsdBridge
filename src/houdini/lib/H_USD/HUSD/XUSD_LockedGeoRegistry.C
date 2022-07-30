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

#include "XUSD_LockedGeoRegistry.h"
#include "HUSD_Constants.h"
#include "XUSD_LockedGeo.h"
#include "XUSD_Utils.h"
#include <UT/UT_Array.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_Lock.h>
#include <SYS/SYS_Math.h>

PXR_NAMESPACE_OPEN_SCOPE

static UT_Lock theEntriesLock;
static UT_Array<XUSD_LockedGeo *> theRegistryEntries;

XUSD_LockedGeoPtr
XUSD_LockedGeoRegistry::createLockedGeo(
        const UT_StringHolder &nodepath,
	const XUSD_LockedGeoArgs &args,
	const GU_DetailHandle &gdh)
{
    UT_AutoLock l(theEntriesLock);

    for (int i = 0, n = theRegistryEntries.size(); i < n; i++)
    {
	if (theRegistryEntries(i)->matches(nodepath, args))
	{
            // This call will no nothing (and return false) if the gdh is
            // unchanged. But if the gdh has changed, then this node's parms
            // have changed and it has been recooked. So we update our gdh
            // and reload the associated layer.
	    theRegistryEntries(i)->setGdh(gdh);

	    return XUSD_LockedGeoPtr(theRegistryEntries(i));
	}
    }

    theRegistryEntries.append(new XUSD_LockedGeo(nodepath, args, gdh));
    return XUSD_LockedGeoPtr(theRegistryEntries.last());
}

GU_DetailHandle
XUSD_LockedGeoRegistry::getGeometry(const UT_StringHolder &nodepath,
	const XUSD_LockedGeoArgs &args)
{
    UT_AutoLock l(theEntriesLock);
    UT_StringHolder testnodepath(nodepath);

    if (testnodepath.endsWith(HUSD_Constants::getVolumeSopSuffix()))
        testnodepath = UT_StringHolder(nodepath.c_str(),
            nodepath.length() - HUSD_Constants::getVolumeSopSuffix().length());

    for (int i = 0, n = theRegistryEntries.size(); i < n; i++)
    {
	if (theRegistryEntries(i)->matches(testnodepath, args))
	{
	    return theRegistryEntries(i)->getGdh();
	}
    }

    return GU_DetailHandle();
}

void
XUSD_LockedGeoRegistry::returnLockedGeo(XUSD_LockedGeo *lockedgeo)
{
    UT_AutoLock l(theEntriesLock);

    for (int i = 0, n = theRegistryEntries.size(); i < n; i++)
    {
	if (theRegistryEntries(i) == lockedgeo)
	{
            HUSDclearBestRefPathCache(
                theRegistryEntries(i)->getLayerIdentifier());
            theRegistryEntries.removeIndex(i);
	    break;
	}
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

