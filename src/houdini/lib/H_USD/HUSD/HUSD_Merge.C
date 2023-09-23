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
#include "XUSD_LockedGeoRegistry.h"
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
            mergestyle == HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES_AND_SOPS ||
            mergestyle == HUSD_MERGE_FLATTEN_LOP_LAYERS_INTO_ACTIVE_LAYER);
}

static inline bool
isFlattenIntoActiveLayerStyle(HUSD_MergeStyle mergestyle)
{
    return (mergestyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER ||
            mergestyle == HUSD_MERGE_FLATTEN_LOP_LAYERS_INTO_ACTIVE_LAYER);
}

class HUSD_Merge::husd_MergePrivate {
public:
    XUSD_LayerAtPathArray	         mySubLayers;
    XUSD_LockedGeoArray		         myLockedGeoArray;
    XUSD_LayerArray		         myHeldLayers;
    XUSD_LayerArray		         myReplacementLayerArray;
    HUSD_LockedStageArray	         myLockedStageArray;
    HUSD_LoadMasksPtr		         myLoadMasks;
    UT_Set<std::string>		         mySubLayerIds;
    UT_SharedPtr<XUSD_RootLayerData>     myRootLayerData;
    int                                  myLayersToKeepSeparate = -1;
    bool                                 myReuseActiveLayer = false;
    bool                                 myFirstAddHandleCall = true;
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

        // Copy the root prim metadata from the first stage.
        if (myPrivate->myFirstAddHandleCall)
            myPrivate->myRootLayerData.reset(
                new XUSD_RootLayerData(indata->stage()));

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
		// NOTE: we do not want to strip any layers if we're processing
		//       the first input and we're using the merge style of
		//       "Flatten Into First Input Layer"
		if (!(myMergeStyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER &&
		      myPrivate->myLayersToKeepSeparate < 0) &&
		    layer.myRemoveWithLayerBreak &&
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

	// Hold onto lockedgeos to keep in memory any cooked OP data referenced
	// by the layers being merged.
	myPrivate->myLockedGeoArray.concat(indata->lockedGeos());
	myPrivate->myHeldLayers.concat(indata->heldLayers());
	myPrivate->myReplacementLayerArray.concat(indata->replacements());
	myPrivate->myLockedStageArray.concat(indata->lockedStages());
	if (indata->loadMasks())
	{
	    if (!myPrivate->myLoadMasks)
		myPrivate->myLoadMasks.reset(new HUSD_LoadMasks());
	    myPrivate->myLoadMasks->merge(*indata->loadMasks());
	}
    }

    if (myMergeStyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER &&
        myPrivate->myFirstAddHandleCall)
    {
        // Track the number of layers on the first call to this method.
        // If the indata has a readable active layer, that means we want
        // to leave the last layer out of the layers to keep separate,
        // because it is the active layer into which we want to flatten
        // all subsequent layers. If indata has no active layer, we will
        // flatten all subsequent input layers into a new layer that will
        // become the active layer for this node's output.
        myPrivate->myLayersToKeepSeparate = myPrivate->mySubLayers.size();
        if (myPrivate->mySubLayers.size() > 0 &&
            indata->activeLayerIsReusable())
        {
            myPrivate->myLayersToKeepSeparate--;
            myPrivate->myReuseActiveLayer = true;
        }
    }
    myPrivate->myFirstAddHandleCall = false;

    return success;
}

bool
HUSD_Merge::addLayer(const UT_StringRef &filepath,
        const UT_StringMap<UT_StringHolder> &refargs,
        const GU_DetailHandle &gdh)
{
    bool                 success = false;

    SdfFileFormat::FileFormatArguments	 args;
    HUSDconvertToFileFormatArguments(refargs, args);

    // Even though we will be making a copy of this layer to an
    // new USD lop layer, we must keep the lockedgeo active in case
    // there are volume primitives that need to be kept in memory.
    if (gdh.isValid())
        myPrivate->myLockedGeoArray.append(XUSD_LockedGeoRegistry::
            createLockedGeo(filepath, args, gdh));

    if (filepath.isstring())
    {
        std::string layer_path = SdfLayer::CreateIdentifier(
            filepath.toStdString(), args);

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(layer_path);

        if (gdh.isValid() && layer)
        {
            // Keep the locked geos active for any volume primitives from
            // unpacked details that need to be kept in memory.
            //
            // Note that the lifetime of the layer is very important here!
            // outdata->addLayer() loads the layer and then discards it
            // after copying into an editable layer.
            // We need to grab the locked geos before the layer
            // (GEO_FileData) is destroyed and clears out its locked geo
            // references.
            // So, we load the layer up front and keep it alive for the
            // rest of the scope so that outdata->addLayer() just gets the
            // same cached layer instead of loading it a second time.
            HUSDaddVolumeLockedGeos(myPrivate->myLockedGeoArray, layer);
        }

        if (layer)
        {
            myPrivate->mySubLayers.append(
                XUSD_LayerAtPath(layer, layer->GetIdentifier()));
            myPrivate->mySubLayerIds.insert(layer->GetIdentifier());

            success = true;
        }
    }

    if (myMergeStyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER &&
        myPrivate->myLayersToKeepSeparate < 0)
        myPrivate->myLayersToKeepSeparate = myPrivate->mySubLayers.size();

    return success;
}

const HUSD_LoadMasksPtr &
HUSD_Merge::mergedLoadMasks() const
{
    return myPrivate->myLoadMasks;
}

bool
HUSD_Merge::execute(HUSD_AutoWriteLock &lock, bool replace_all) const
{
    auto			 outdata = lock.data();
    bool                         success = false;

    if (outdata && outdata->isStageValid())
    {
        XUSD_LayerAtPathArray    replace_all_sublayers;
        UT_StringSet             outlayers;

        success = true;
        if (!replace_all)
        {
            // Create a set of layer ids already on the output layer stack to
            // avoid adding duplicate layers.
            for (int i = 0, n = outdata->sourceLayers().size(); i < n; i++)
                outlayers.insert(outdata->sourceLayers()(i).myIdentifier);

            // Transfer lockedgeo ownership from ourselves to the output data.
            outdata->addLockedGeos(myPrivate->myLockedGeoArray);
            outdata->addHeldLayers(myPrivate->myHeldLayers);
            outdata->addReplacements(myPrivate->myReplacementLayerArray);
            outdata->addLockedStages(myPrivate->myLockedStageArray);
        }
        if (myMergeStyle == HUSD_MERGE_FLATTENED_LAYERS ||
            myMergeStyle == HUSD_MERGE_FLATTEN_LOP_LAYERS_INTO_ACTIVE_LAYER)
            myPrivate->myReuseActiveLayer = true;

        // Add some separate layers to the output. This happens for any merge
        // styles that call out "separate" layers, or when we have flattened
        // each input into layers that should be kept separate, or in the
        // special case where we are replacing all of outdata's source layers,
        // and we are flattening into the active layer of the first input, we
        // keep the first input's layers separated (as recorded in the first
        // call to addHandle, stored in myPrivate->myLayersToKeepSeparate.
        if (isSeparateLayerStyle(myMergeStyle) ||
            myMergeStyle == HUSD_MERGE_PERHANDLE_FLATTENED_LAYERS ||
            (replace_all &&
                myMergeStyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER))
        {
            XUSD_LayerAtPathArray sublayers;

            // Add layers in reverse order from how they appear in mySubLayers,
            // because a series of addLayer calls add the layers in weakest to
            // strongest order, but mySubLayers is in strongest to weakest
            // order.
            if (myMergeStyle == HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES ||
                myMergeStyle == HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES_AND_SOPS ||
                myMergeStyle == HUSD_MERGE_FLATTEN_LOP_LAYERS_INTO_ACTIVE_LAYER)
            {
                // If we have been asked to rearrange the layers to put file
                // or SOP layers as the weakest layers, do a pre-pass through
                // the layers, adding files and/or sop layers to the stage
                // and removing these layers from our array of sublayers.
                for (int i = myPrivate->mySubLayers.size(); i --> 0;)
                {
                    const XUSD_LayerAtPath &layer = myPrivate->mySubLayers(i);

                    if (layer.isLopLayer())
                        continue;
                    if (myMergeStyle == HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES &&
                        HUSDisSopLayer(layer.myLayer))
                        continue;
                    // Skip layers that are already in the output layer stack.
                    if (outlayers.find(layer.myIdentifier) != outlayers.end())
                        continue;
                    sublayers.append(layer);
                    myPrivate->mySubLayers.removeIndex(i);
                }
            }

            if (myMergeStyle != HUSD_MERGE_FLATTEN_LOP_LAYERS_INTO_ACTIVE_LAYER)
            {
                int i = myPrivate->mySubLayers.size();
                for (; i-- > 0;)
                {
                    // In "flatten into active layer" mode, break out of this
                    // loop after we've added all the sublayers from the first
                    // input.
                    if (myMergeStyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER &&
                        sublayers.size() >= myPrivate->myLayersToKeepSeparate)
                        break;

                    const XUSD_LayerAtPath &layer = myPrivate->mySubLayers(i);

                    // Skip layers that are already in the output layer stack.
                    if (outlayers.find(layer.myIdentifier) != outlayers.end())
                        continue;
                    sublayers.append(layer);
                }

                // In "flatten into active layer" mode, remove layers from
                // myPrivate->mySublayers and layers that we already added
                // to sublayers (to be kept separate). Do this outside the
                // loop in case all the layers are from the first input and
                // there is no active layer to be flattened.
                if (myMergeStyle == HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER &&
                    sublayers.size() >= myPrivate->myLayersToKeepSeparate)
                    myPrivate->mySubLayers.truncate(i+1);
            }

            if (!replace_all)
            {
                if (!outdata->addLayers(sublayers,
                        0, XUSD_ADD_LAYERS_ALL_LOCKED, false))
                    success = false;
            }
            else
                replace_all_sublayers = sublayers;
        }

        // Flatten together all layers left in myPrivate->mySubLayers
        // (if there are any - they may have all been turned into separate
        // layers in "flatten into active layer" mode).
        if (success &&
            myPrivate->mySubLayers.size() > 0 &&
            (myMergeStyle == HUSD_MERGE_FLATTENED_LAYERS ||
             isFlattenIntoActiveLayerStyle(myMergeStyle)))
        {
            std::vector<std::string>	 sublayers;
            std::vector<SdfLayerOffset>	 sublayeroffsets;

            for (int i = 0, n = myPrivate->mySubLayers.size(); i < n; i++)
            {
                auto layer = myPrivate->mySubLayers(i);

                // Skip layers that are already in the output layer stack.
                if (outlayers.find(layer.myIdentifier) != outlayers.end())
                    continue;

                // Insert each source layer at the end of the sublayer list
                // because sourceLayers is already ordered strongest to
                // weakest, just as sublayers wants.
                sublayers.push_back(layer.myIdentifier);
                sublayeroffsets.push_back(layer.myOffset);
            }

            // If we are flattening into the active layer, the active layer
            // should be the weakest (last) layer, so append it after all the
            // others have been appended. We don't want to include the active
            // layer of the output handle if we are doing a full replacement,
            // because that isn't really the active layer. It is probably the
            // active layer from the last time we cooked. The active layer of
            // the first call to addHandle will have already been added to the
            // right place in the sublayers array to act as the active layer
            // (look at replace_all-specific code in the large if block for
            // adding separate layers).
            if (!replace_all && isFlattenIntoActiveLayerStyle(myMergeStyle))
            {
                sublayers.push_back(outdata->activeLayer()->GetIdentifier());
                sublayeroffsets.push_back(SdfLayerOffset());
            }

            UsdStageRefPtr       stage_to_flatten;

            stage_to_flatten = HUSDcreateStageInMemory(
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

                stage_to_flatten->GetRootLayer()->SetSubLayerPaths(sublayers);
                for (int i = 0, n = sublayers.size(); i < n; i++)
                    stage_to_flatten->GetRootLayer()->SetSubLayerOffset(
                        sublayeroffsets[i], i);
            }

            SdfLayerRefPtr flattened = HUSDflattenLayers(stage_to_flatten);

            if (!replace_all)
            {
                // Either copy the flattened layer into the active layer, or
                // add the flattened layer as a new layer.
                if (isFlattenIntoActiveLayerStyle(myMergeStyle))
                {
                    outdata->activeLayer()->TransferContent(flattened);
                }
                else
                {
                    if (!outdata->addLayer(
                            XUSD_LayerAtPath(flattened, SdfLayerOffset(),
                                lock.dataHandle().nodeId()),
                            0, XUSD_ADD_LAYERS_LAST_EDITABLE, false))
                        success = false;
                }
            }
            else
                replace_all_sublayers.append(XUSD_LayerAtPath(flattened));
        }

        if (success && replace_all)
        {
            if (!myPrivate->myRootLayerData)
            {
                auto stage = HUSDcreateStageInMemory(
                    UsdStage::InitialLoadSet::LoadNone);
                myPrivate->myRootLayerData =
                    UTmakeShared<XUSD_RootLayerData>(stage);
            }
            if (!outdata->replaceAllSourceLayers(
                    replace_all_sublayers,
                    myPrivate->myLockedGeoArray,
                    myPrivate->myHeldLayers,
                    myPrivate->myReplacementLayerArray,
                    myPrivate->myLockedStageArray,
                    myPrivate->myRootLayerData,
                    myPrivate->myReuseActiveLayer))
                success = false;
        }
    }

    return success;
}
