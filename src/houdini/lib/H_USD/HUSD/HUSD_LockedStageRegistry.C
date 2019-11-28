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

#include "HUSD_LockedStageRegistry.h"
#include "HUSD_ErrorScope.h"

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_LockedStageRegistry::HUSD_LockedStageRegistry()
{
}

HUSD_LockedStageRegistry::~HUSD_LockedStageRegistry()
{
}

HUSD_LockedStageRegistry &
HUSD_LockedStageRegistry::getInstance()
{
    static HUSD_LockedStageRegistry	 theRegistry;

    return theRegistry;
}

HUSD_LockedStagePtr
HUSD_LockedStageRegistry::getLockedStage(int nodeid,
	const HUSD_DataHandle &data,
	bool strip_layers,
	HUSD_StripLayerResponse response)
{
    LockedStageId		 locked_stage_id = { nodeid, strip_layers };
    HUSD_LockedStageWeakPtr	 weakptr = myLockedStageMap[locked_stage_id];
    HUSD_LockedStagePtr		 ptr;

    ptr = weakptr.lock();
    if (!ptr)
    {
	ptr.reset(new HUSD_LockedStage(data, strip_layers));
	if (ptr->isValid())
	    myLockedStageMap[locked_stage_id] = ptr;
    }

    // If creating this locked stage involved stripping layers, and we have
    // been asked to provide a warning in this case, add the warning.
    if (strip_layers && ptr->strippedLayers())
	HUSDapplyStripLayerResponse(response);

    return ptr;
}

void
HUSD_LockedStageRegistry::clearLockedStage(int nodeid)
{
    myLockedStageMap.erase({ nodeid, true });
    myLockedStageMap.erase({ nodeid, false });
}

