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

#include "HUSD_Merge.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_LoadMasks.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <UT/UT_ErrorManager.h>
#include <UT/UT_Set.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <vector>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

static inline bool
isSeparateLayerStyle(HUSD_MergeStyle mergestyle)
{
    return (mergestyle == HUSD_MERGE_SEPARATE_LAYERS ||
        mergestyle == HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES ||
        mergestyle == HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES_AND_SOPS);
}

class HUSD_Merge::husd_MergePrivate {
public:
    XUSD_LayerAtPathArray	 mySubLayers;
    XUSD_TicketArray		 myTicketArray;
    XUSD_LayerArray		 myReplacementLayerArray;
    HUSD_LockedStageArray	 myLockedStageArray;
    HUSD_LoadMasksPtr		 myLoadMasks;
    UT_Set<std::string>		 mySubLayerIds;
};

HUSD_Merge::HUSD_Merge(HUSD_MergeStyle merge_style,
	HUSD_StripLayerResponse response,
        bool striplayerbreaks)
    : myMergeStyle(merge_style),
      myStripLayerResponse(response),
      myStripLayerBreaks(striplayerbreaks),
      myPrivate(new HUSD_Merge::husd_MergePrivate())
{
}

HUSD_Merge::~HUSD_Merge()
{
}

bool
HUSD_Merge::addHandle(const HUSD_DataHandle &src,
	const UT_StringHolder &dest_path)
{
    HUSD_AutoReadLock	 inlock(src);
    auto		 indata = inlock.data();
    bool		 success = false;

    if (indata && indata->isStageValid())
    {
	success = true;
	if (myMergeStyle == HUSD_MERGE_PERHANDLE_FLATTENED_LAYERS)
	{
	    XUSD_LayerAtPath	 layer(indata->
                createFlattenedLayer(myStripLayerResponse));

	    // We want to flatten all the layers on this data handle together
	    // and add them to our private list of sublayers that will be
	    // combined in the execute method.
	    myPrivate->mySubLayers.insert(layer, 0);
	    myPrivate->mySubLayerIds.insert(layer.myIdentifier);
	}
	else
	{
	    // We want to create an array of layers here ordered strongest to
	    // weakest. But this method will be called using the weakest to
	    // strongest input ordering. So each block of source layers
            // (which are ordered weakest to strongest) must be inserted at
            // the front of all existing layers.
	    for (int i = 0, n = indata->sourceLayers().size(); i < n; i++)
	    {
		const XUSD_LayerAtPath	&layer = indata->sourceLayers()(i);

		// Enforce layer break semantics here, because we don't want
		// any layer breaks from a stronger input to affect the ability
		// of layers from weaker inputs to be merged in. A layer break
		// should only affect the layers of the data handle of which it
		// is a part.
		if (layer.myRemoveWithLayerBreak &&
                    (myStripLayerBreaks || !isSeparateLayerStyle(myMergeStyle)))
		{
		    // If stripping layers is an error, and we stripped some
		    // layers, then treat this function call as a failure.
		    // Continue executing to the end of the method though so
		    // we don't end up with data in an inconsistent state.
		    if (HUSDapplyStripLayerResponse(myStripLayerResponse))
			success = false;
		    continue;
		}

		// If a source layer is already in our list, don't add it
		// again.  If a bunch of layer stacks come in with the first N
		// layers all the same, we don't want to re-apply those layers
		// over and over again (imagine a node that branches out to
		// five node paths, which all merge back together). We are only
		// interested in the first occurence of each unique layer.
		if (!myPrivate->mySubLayerIds.contains(layer.myIdentifier))
		{
		    myPrivate->mySubLayers.insert(layer, 0);
		    myPrivate->mySubLayerIds.insert(layer.myIdentifier);
		}
	    }
	}

	// Hold onto tickets to keep in memory any cooked OP data referenced
	// by the layers being merged.
	myPrivate->myTicketArray.concat(indata->tickets());
	myPrivate->myReplacementLayerArray.concat(indata->replacements());
	myPrivate->myLockedStageArray.concat(indata->lockedStages());
	if (indata->loadMasks())
	{
	    if (!myPrivate->myLoadMasks)
		myPrivate->myLoadMasks.reset(new HUSD_LoadMasks());
	    myPrivate->myLoadMasks->merge(*indata->loadMasks());
	}
    }

    return success;
}

const HUSD_LoadMasksPtr &
HUSD_Merge::mergedLoadMasks() const
{
    return myPrivate->myLoadMasks;
}

bool
HUSD_Merge::execute(HUSD_AutoWriteLock &lock) const
{
    bool			 success = false;
    auto			 outdata = lock.data();

    if (outdata && outdata->isStageValid())
    {
	// Transfer ticket ownership from ourselves to the output data.
	outdata->addTickets(myPrivate->myTicketArray);
	outdata->addReplacements(myPrivate->myReplacementLayerArray);
	outdata->addLockedStages(myPrivate->myLockedStageArray);

	if (isSeparateLayerStyle(myMergeStyle) ||
	    myMergeStyle == HUSD_MERGE_PERHANDLE_FLATTENED_LAYERS)
	{
            XUSD_LayerAtPathArray        sublayers;
	    success = true;

	    // Add layers in reverse order from how they appear in mySubLayers,
	    // because a series of addLayer calls add the layers in weakest to
	    // strongest order, but mySubLayers is in strongest to weakest
	    // order.
            if (myMergeStyle == HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES ||
                myMergeStyle == HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES_AND_SOPS)
            {
                // If we have been asked to rearrange the layers to put file
                // or SOP layers as the weakest layers, do a pre-pass through
                // the layers, adding files and/or sop layers to the stage
                // and removing these layers from our array of sublayers.
                for (int i = myPrivate->mySubLayers.size(); success && i --> 0;)
                {
                    const XUSD_LayerAtPath &layer = myPrivate->mySubLayers(i);

                    if (layer.isLayerAnonymous())
                        continue;
                    if (myMergeStyle == HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES &&
                        HUSDisSopLayer(layer.myLayer))
                        continue;
                    sublayers.append(layer);
                    myPrivate->mySubLayers.removeIndex(i);
                }
            }

	    for (int i = myPrivate->mySubLayers.size(); success && i --> 0;)
                sublayers.append(myPrivate->mySubLayers(i));

            if (!outdata->addLayers(sublayers,
                    0, XUSD_ADD_LAYERS_ALL_LOCKED, false))
                success = false;
	}
	else if (myMergeStyle == HUSD_MERGE_FLATTENED_LAYERS ||
	         myMergeStyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER)
	{
	    std::vector<std::string>	 sublayers;
	    std::vector<SdfLayerOffset>	 sublayeroffsets;

	    for (int i = 0, n = myPrivate->mySubLayers.size(); i < n; i++)
	    {
		auto layer = myPrivate->mySubLayers(i);

		// Insert each source layer at the end of the sublayer list
		// because sourceLayers is already ordered strongest to
		// weakest, just as sublayers wants.
		sublayers.push_back(layer.myIdentifier);
		sublayeroffsets.push_back(layer.myOffset);
	    }

            // If we are flattening into the active layer, the active layer
            // should be the weakest (last) layer, so append it after all the
            // others have been appended.
            if (myMergeStyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER)
            {
                sublayers.push_back(outdata->activeLayer()->GetIdentifier());
                sublayeroffsets.push_back(SdfLayerOffset());
            }

	    UsdStageRefPtr       stage;

	    stage = HUSDcreateStageInMemory(
                UsdStage::LoadNone, outdata->stage());

            // Create an error scope as we compose this temporary stage,
            // which exists only as a holder for the layers we wish to
            // flatten together. If there are warnings or errors during
            // this composition, either they are safe to ignore, or they
            // will show up agai when we compose the flattened layer is
            // composed onto the main stage.
            {
                UT_ErrorManager      ignore_errors_mgr;
                HUSD_ErrorScope      ignore_errors(&ignore_errors_mgr);

                stage->GetRootLayer()->SetSubLayerPaths(sublayers);
                for (int i = 0, n = sublayers.size(); i < n; i++)
                    stage->GetRootLayer()->SetSubLayerOffset(
                        sublayeroffsets[i], i);
            }

            SdfLayerRefPtr       flattened = HUSDflattenLayers(stage);

            // Either copy the flattened layer into the active layer, or
            // add the flattened layer as a new layer.
            if (myMergeStyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER)
            {
                outdata->activeLayer()->TransferContent(flattened);
                success = true;
            }
            else
            {
                success = outdata->addLayer(
                    XUSD_LayerAtPath(flattened,
                        SdfLayerOffset(), lock.dataHandle().nodeId()),
                    0, XUSD_ADD_LAYERS_LAST_EDITABLE, false);
            }
	}
    }

    return success;
}

