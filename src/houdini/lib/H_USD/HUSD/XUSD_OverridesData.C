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

#include "XUSD_OverridesData.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include "HUSD_Overrides.h"

PXR_NAMESPACE_OPEN_SCOPE

XUSD_OverridesData::XUSD_OverridesData()
    : myLockedToData(nullptr)
{ 
    for (int layer_idx = 0; layer_idx < HUSD_OVERRIDES_NUM_LAYERS; layer_idx++)
	myLayer[layer_idx] = HUSDcreateAnonymousLayer();
}

XUSD_OverridesData::~XUSD_OverridesData()
{
}

const SdfLayerRefPtr &
XUSD_OverridesData::layer(HUSD_OverridesLayerId layer_id) const
{
    if (myLockedToData)
	return myLockedToData->sessionLayer(layer_id);

    return myLayer[layer_id];
}

void
XUSD_OverridesData::lockToData(XUSD_Data *data)
{
    UT_ASSERT(data && !myLockedToData);
    myLockedToData = data;
}

void
XUSD_OverridesData::unlockFromData(XUSD_Data *data)
{
    UT_ASSERT(data && myLockedToData == data);
    for (int i=0; i<HUSD_OVERRIDES_NUM_LAYERS; i++)
	myLayer[i]->TransferContent(
	    myLockedToData->sessionLayer((HUSD_OverridesLayerId)i));
    myLockedToData = nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE

