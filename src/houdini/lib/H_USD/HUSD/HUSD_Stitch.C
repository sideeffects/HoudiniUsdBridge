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

#include "HUSD_Stitch.h"
#include "HUSD_Constants.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usd/stage.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class HUSD_Stitch::husd_StitchPrivate {
public:
    UsdStageRefPtr		 myStage;
    XUSD_TicketArray		 myTicketArray;
    XUSD_LayerArray		 myReplacementLayerArray;
    HUSD_LockedStageArray	 myLockedStageArray;
    SdfLayerRefPtrVector	 myHoldLayers;
};

HUSD_Stitch::HUSD_Stitch()
    : myPrivate(new HUSD_Stitch::husd_StitchPrivate())
{
}

HUSD_Stitch::~HUSD_Stitch()
{
}

bool
HUSD_Stitch::addHandle(const HUSD_DataHandle &src)
{
    HUSD_AutoReadLock	 inlock(src);
    auto		 indata = inlock.data();
    bool		 success = false;

    if (indata && indata->isStageValid())
    {
	if (!myPrivate->myStage)
	    myPrivate->myStage = HUSDcreateStageInMemory(
		UsdStage::LoadNone, OP_INVALID_ITEM_ID, indata->stage());
	// Stitch the input handle into our stage.
	HUSDaddStageTimeSample(indata->stage(), myPrivate->myStage,
	    myPrivate->myHoldLayers);
	// Hold onto tickets to keep in memory any cooked OP data referenced
	// by the layers being merged.
	myPrivate->myTicketArray.concat(indata->tickets());
	myPrivate->myReplacementLayerArray.concat(indata->replacements());
	myPrivate->myLockedStageArray.concat(indata->lockedStages());
	success = true;
    }

    return success;
}

bool
HUSD_Stitch::execute(HUSD_AutoWriteLock &lock,
        bool copy_stitched_layers) const
{
    bool			 success = false;
    auto			 outdata = lock.data();

    if (outdata && outdata->isStageValid())
    {
	SdfLayerRefPtr		 rootlayer = myPrivate->myStage->GetRootLayer();
	SdfSubLayerProxy	 sublayers = rootlayer->GetSubLayerPaths();
	SdfLayerOffsetVector	 offsets = rootlayer->GetSubLayerOffsets();

	// Transfer ticket ownership from ourselves to the output data.
	outdata->addTickets(myPrivate->myTicketArray);
	outdata->addReplacements(myPrivate->myReplacementLayerArray);
	outdata->addLockedStages(myPrivate->myLockedStageArray);
	// Transfer the layers of the our combined stage into the
	// destination data handle.
	SdfLayerHandleVector layers = myPrivate->myStage->GetLayerStack(false);
	for (int i = sublayers.size(); i --> 0; )
	{
	    XUSD_AddLayerOp	 addop = XUSD_ADD_LAYER_LOCKED;
	    std::string		 path = sublayers[i];

            // Don't add placeholder layers.
            if (HUSDisLayerPlaceholder(path))
                continue;
	    // If the strongest layer is anonymous, allow it to be edited
	    // further after the combine operation. If we have been asked to
            // copy all stitched layers, mark the layer as editable so the
            // addLayer operation will make a copy.
	    if ((i == 0 || copy_stitched_layers) &&
                SdfLayer::IsAnonymousLayerIdentifier(path))
		addop = XUSD_ADD_LAYER_EDITABLE;
	    outdata->addLayer(path, offsets[i], 0, addop);
	}
	success = true;
    }

    return success;
}

