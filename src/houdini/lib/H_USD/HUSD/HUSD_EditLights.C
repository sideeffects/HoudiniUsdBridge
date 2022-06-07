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
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_EditLights.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_PathSet.h"
#include "XUSD_Data.h"
#include <pxr/usd/usdLux/lightAPI.h>


PXR_NAMESPACE_USING_DIRECTIVE


HUSD_EditLights::HUSD_EditLights(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_EditLights::~HUSD_EditLights()
{
}

static inline bool
husdAddLightFiltersToLight( UsdStageRefPtr &stage,
	const SdfPath &light_path, 
	const HUSD_FindPrims &light_filters )
{
    auto prim = stage->GetPrimAtPath( light_path );
    auto light_api = UsdLuxLightAPI::Apply( prim );
    if( !light_api )
	return false;

    bool ok = true;
    auto filters_rel = light_api.CreateFiltersRel();
    for( auto &&sdfpath : light_filters.getExpandedPathSet().sdfPathSet() )
	if( !filters_rel.AddTarget( sdfpath ))
	    ok = false;

    return ok;
}

bool
HUSD_EditLights::addLightFilters(const HUSD_FindPrims &lights,
	const HUSD_FindPrims &light_filters) const
{
    auto data = myWriteLock.data();
    if (!data || !data->isStageValid())
    {
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, "Invalid stage.");
	return false;
    }

    auto stage = data->stage();
    bool ok = true;
    for( auto &&sdfpath : lights.getExpandedPathSet().sdfPathSet() )
    {
	if( !husdAddLightFiltersToLight( stage, sdfpath, light_filters ))
	{
	    UT_WorkBuffer msg;
	    msg.format( "Could not add light filters to '{0}'.",
		    sdfpath.GetText());
	    HUSD_ErrorScope::addError( HUSD_ERR_STRING, msg.buffer() );
	    ok = false;
	}
    }

    return ok;
}


