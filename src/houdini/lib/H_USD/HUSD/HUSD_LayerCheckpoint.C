/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	HUSD_LayerCheckpoint.C (HUSD Library, C++)
 *
 * COMMENTS:	Data structure for hold a copy of the active layer.
 */

#include "HUSD_LayerCheckpoint.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/sdf/layer.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_LayerCheckpoint::HUSD_LayerCheckpoint()
{
}

HUSD_LayerCheckpoint::~HUSD_LayerCheckpoint()
{
}

void
HUSD_LayerCheckpoint::create(const HUSD_AutoAnyLock &lock)
{
    SdfLayerRefPtr active_layer;

    if (lock.constData())
        active_layer = lock.constData()->activeLayer();

    if (active_layer)
    {
        if (!myLayer)
            myLayer.reset(new XUSD_Layer(HUSDcreateAnonymousLayer(), false));
        myLayer->layer()->TransferContent(active_layer);
    }
    else
        myLayer.reset();
}

bool
HUSD_LayerCheckpoint::restore(const HUSD_AutoLayerLock &layerlock)
{
    if (layerlock.layer() && layerlock.layer()->layer())
    {
        if (myLayer && myLayer->layer())
            layerlock.layer()->layer()->TransferContent(myLayer->layer());
        else
            layerlock.layer()->layer()->Clear();

        return true;
    }

    return false;
}

