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
        std::vector<std::string> paths_to_add;
	SdfLayerOffsetVector	 offsets_to_add;

	// Transfer ticket ownership from ourselves to the output data.
	outdata->addTickets(myPrivate->myTicketArray);
	outdata->addReplacements(myPrivate->myReplacementLayerArray);
	outdata->addLockedStages(myPrivate->myLockedStageArray);

	// Transfer the layers of the our combined stage into the
	// destination data handle.
	SdfLayerHandleVector layers = myPrivate->myStage->GetLayerStack(false);
	for (int i = sublayers.size(); i --> 0; )
	{
	    std::string		 path = sublayers[i];

            // Don't add placeholder layers.
            if (HUSDisLayerPlaceholder(path))
                continue;

            paths_to_add.push_back(path);
            offsets_to_add.push_back(offsets[i]);
	}

        // If the strongest layer is anonymous, allow it to be edited
        // further after the combine operation. If we have been asked to
        // copy all stitched layers, mark the layer as editable so the
        // addLayer operation will make a copy.
        XUSD_AddLayerOp	addop = copy_stitched_layers
            ? XUSD_ADD_LAYERS_ALL_ANONYMOUS_EDITABLE
            : XUSD_ADD_LAYERS_LAST_ANONYMOUS_EDITABLE;

        success = outdata->addLayers(paths_to_add, offsets_to_add, 0, addop);
    }

    return success;
}

