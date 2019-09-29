/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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

