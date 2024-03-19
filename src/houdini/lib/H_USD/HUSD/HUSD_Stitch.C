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

#include "HUSD_ErrorScope.h"
#include "HUSD_PathSet.h"
#include "HUSD_Stitch.h"
#include "HUSD_Constants.h"
#include "XUSD_Data.h"
#include "XUSD_ExistenceTracker.h"
#include "XUSD_RootLayerData.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usd/stage.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class HUSD_Stitch::husd_StitchPrivate {
public:
    UsdStageRefPtr		         myStage;
    XUSD_ExistenceTracker                myExistenceTracker;
    XUSD_LockedGeoSet		         myLockedGeos;
    XUSD_LayerSet		         myReplacementLayers;
    HUSD_LockedStageSet 	         myLockedStages;
    XUSD_LayerSet		         myHeldLayers;
    UT_SharedPtr<XUSD_RootLayerData>     myRootLayerData;
    UT_StringSet                         myLayersAboveLayerBreak;
    HUSD_PathSet                         myVaryingDefaultPaths;
};

HUSD_Stitch::HUSD_Stitch()
    : myPrivate(new HUSD_Stitch::husd_StitchPrivate()),
      myTrackPrimExistence(false)
{
}

HUSD_Stitch::~HUSD_Stitch()
{
}

bool
HUSD_Stitch::addHandle(const HUSD_DataHandle &src,
        const HUSD_TimeCode &timecode)
{
    HUSD_AutoReadLock	 inlock(src);
    auto		 indata = inlock.data();
    bool		 success = false;

    if (indata && indata->isStageValid())
    {
	if (!myPrivate->myStage)
	    myPrivate->myStage = HUSDcreateStageInMemory(
		UsdStage::LoadNone, indata->stage());
	// Stitch the input handle into our stage. Set the
        // force_notifiable_file_format parameter to true because we need
        // accurate fine-grained notifications to author the combined stage
        // correctly.
	HUSDaddStageTimeSample(indata->stage(), myPrivate->myStage,
            HUSDgetUsdTimeCode(timecode), myPrivate->myHeldLayers, true, false,
            trackPrimExistence() ? &myPrivate->myExistenceTracker : nullptr,
            &myPrivate->myVaryingDefaultPaths);
	// Hold onto lockedgeos to keep in memory any cooked OP data referenced
	// by the layers being merged.
	myPrivate->myLockedGeos.insert(indata->lockedGeos().begin(),
            indata->lockedGeos().end());
	myPrivate->myReplacementLayers.insert(indata->replacements().begin(),
            indata->replacements().end());
	myPrivate->myLockedStages.insert(indata->lockedStages().begin(),
            indata->lockedStages().end());
	myPrivate->myHeldLayers.insert(indata->heldLayers().begin(),
            indata->heldLayers().end());
        myPrivate->myRootLayerData.reset(
            new XUSD_RootLayerData(indata->stage()));

        // Get all layers from the source marked as above a layer break.
        // We record these layers using their "save location" for lop
        // layers or the identifier for other layers. This is because
        // lop layers are matched up in the stitch functions based on
        // their save location (and other layer files will have the same
        // identifier if they are the same layer).
        for (auto &&layer_at_path : indata->sourceLayers())
        {
            if (layer_at_path.myRemoveWithLayerBreak)
            {
                UT_StringHolder  saveloc;

                if (layer_at_path.isLopLayer())
                    saveloc = HUSDgetLayerSaveLocation(layer_at_path.myLayer);
                else
                    saveloc = layer_at_path.myLayer->GetIdentifier();
                myPrivate->myLayersAboveLayerBreak.insert(saveloc);
            }
        }

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
        std::vector<bool>        layers_above_layer_break;
	SdfLayerOffsetVector	 offsets_to_add;

	// Transfer lockedgeos ownership from ourselves to the output data.
	outdata->addLockedGeos(myPrivate->myLockedGeos);
	outdata->addReplacements(myPrivate->myReplacementLayers);
	outdata->addLockedStages(myPrivate->myLockedStages);
	outdata->addHeldLayers(myPrivate->myHeldLayers);
        outdata->setStageRootLayerData(myPrivate->myRootLayerData);

	// Transfer the sublayers of the our combined stage into the
	// destination data handle.
	for (int i = sublayers.size(); i --> 0; )
	{
	    std::string		 path = sublayers[i];

            // Don't add placeholder layers.
            if (HUSDisLayerPlaceholder(path))
                continue;

            SdfLayerRefPtr       layer = SdfLayer::Find(path);

            paths_to_add.push_back(path);
            offsets_to_add.push_back(offsets[i]);
            // Check if the layer is in the set of layers we recorded as
            // having been authored above layer breaks. If so, they should
            // still be marked as coming from above a layer break after
            // this stitch operation.
            if (layer && HUSDisLopLayer(layer))
                layers_above_layer_break.push_back(myPrivate->
                    myLayersAboveLayerBreak.contains(
                        HUSDgetLayerSaveLocation(layer)));
            else if (layer)
                layers_above_layer_break.push_back(myPrivate->
                    myLayersAboveLayerBreak.contains(layer->GetIdentifier()));
            else
                layers_above_layer_break.push_back(myPrivate->
                    myLayersAboveLayerBreak.contains(path));
	}

        // If the strongest layer is a lop layer, allow it to be edited
        // further after the combine operation. If we have been asked to
        // copy all stitched layers, mark the layer as editable so the
        // addLayer operation will make a copy.
        XUSD_AddLayerOp	addop = copy_stitched_layers
            ? XUSD_ADD_LAYERS_ALL_ANONYMOUS_EDITABLE
            : XUSD_ADD_LAYERS_LAST_ANONYMOUS_EDITABLE;

        success = outdata->addLayers(paths_to_add,
            layers_above_layer_break, offsets_to_add,
            0, addop, false);

        if (myPrivate->myExistenceTracker.getVisibilityLayer())
        {
            // We have an existence visibility layer. In case we want to make
            // future edits (adding more time samples), we have to make a copy
            // of the visibility layer to add to the stage.
            SdfLayerRefPtr layercopy = HUSDcreateAnonymousCopy(
                myPrivate->myExistenceTracker.getVisibilityLayer());

            success &= outdata->addLayer(XUSD_LayerAtPath(layercopy),
                0, XUSD_ADD_LAYERS_ALL_EDITABLE, false);
        }
        else if (!layers_above_layer_break.empty() &&
                 layers_above_layer_break.back())
        {
            // Add a final empty new layer if the last layer was above a layer
            // break. This is because we don't want to allow the addition of new
            // data to this layer from above a layer break now that we are below
            // the layer break.
            success &= outdata->addLayer();
        }
    }
    
    for (const auto &&path : myPrivate->myVaryingDefaultPaths)
        HUSD_ErrorScope::addWarning(HUSD_ERR_DEFAULT_VALUE_IS_VARYING, path.pathStr());

    return success;
}

