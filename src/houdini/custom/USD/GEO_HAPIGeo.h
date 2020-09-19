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

#ifndef __GEO_HAPI_GEO_H__
#define __GEO_HAPI_GEO_H__

#include "GEO_HAPIPart.h"
#include <UT/UT_Array.h>
#include <UT/UT_IntrusivePtr.h>

/// \class GEO_HAPIGeo
///
/// Wrapper class for Houdini Engine Geometry
///
class GEO_HAPIGeo : public UT_IntrusiveRefCounter<GEO_HAPIGeo>
{
public:
    GEO_HAPIGeo();
    ~GEO_HAPIGeo();

    bool loadGeoData(const HAPI_Session &session,
                     HAPI_GeoInfo &geo,
                     UT_WorkBuffer &buf);

    GEO_HAPIPartArray &getParts() { return myParts; }

    int64 getMemoryUsage(bool inclusive) const;

private:
    GEO_HAPIPartArray myParts;
};

typedef UT_IntrusivePtr<GEO_HAPIGeo> GEO_HAPIGeoHandle;

#endif // __GEO_HAPI_GEO_H__