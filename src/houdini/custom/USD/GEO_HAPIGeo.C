/*
 * Copyright 2020 Side Effects Software Inc.
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
 */

#include "GEO_HAPIGeo.h"
#include "GEO_HAPIUtils.h"

GEO_HAPIGeo::GEO_HAPIGeo() : UT_IntrusiveRefCounter<GEO_HAPIGeo>() {}

GEO_HAPIGeo::~GEO_HAPIGeo() {}

bool
GEO_HAPIGeo::loadGeoData(const HAPI_Session &session,
                         HAPI_GeoInfo &geo,
                         UT_WorkBuffer &buf)
{
    UT_ASSERT(myParts.isEmpty());

    // If a GU_Detail is ever retrieved while loading a part, gdh will contain
    // the entire geometry instead of a single part. We use the same gdh when
    // loading every part so the GU_Detail for this geometry only needs to be
    // retrieved once
    GU_DetailHandle gdh;

    HAPI_PartInfo part;
    for (int i = 0; i < geo.partCount; i++)
    {
        ENSURE_SUCCESS(
            HAPI_GetPartInfo(&session, geo.nodeId, i, &part), session);

        // We don't want to save instanced parts at this level.
        // They will be saved within intancer parts
        if (!part.isInstanced)
        {
            myParts.emplace_back();
            CHECK_RETURN(
                myParts.last().loadPartData(session, geo, part, buf, gdh));
        }
    }

    return true;
}

int64
GEO_HAPIGeo::getMemoryUsage(bool inclusive) const
{
    int64 usage = inclusive ? sizeof(*this) : 0;
    usage += myParts.getMemoryUsage(false);

    for (const GEO_HAPIPart& part : myParts)
    {
        usage += part.getMemoryUsage(false);
    }

    return usage;
}
