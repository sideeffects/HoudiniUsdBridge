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

#include "HUSD_LockedStage.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <gusd/stageCache.h>
#include <OP/OP_Node.h>
#include <UT/UT_ErrorManager.h>
#include <UT/UT_StringSet.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

class HUSD_LockedStage::husd_LockedStagePrivate {
public:
    UsdStageRefPtr		 myStage;
    XUSD_TicketArray		 myTicketArray;
};

HUSD_LockedStage::HUSD_LockedStage(const HUSD_DataHandle &data,
        int nodeid,
	bool strip_layers,
        fpreal t)
    : myPrivate(new HUSD_LockedStage::husd_LockedStagePrivate()),
      myTime(t),
      myStrippedLayers(false)
{
    lockStage(data, nodeid, strip_layers, t);
}

HUSD_LockedStage::~HUSD_LockedStage()
{
    // Clear this locked stage out of the GusdStageCache. We should not be
    // making any new USD packed primitives from here, because it no longer
    // represents the current state of any LOP node cook.
    if (isValid())
    {
	GusdStageCacheWriter	 cache;
	UT_StringSet		 paths;

	paths.insert(myStageCacheIdentifier);
	cache.Clear(paths);
        HUSDclearBestRefPathCache(myRootLayerIdentifier.toStdString());
    }

    myPrivate->myStage.Reset();
    myPrivate->myTicketArray.clear();
    myStageCacheIdentifier.clear();
    myRootLayerIdentifier.clear();
}

bool
HUSD_LockedStage::lockStage(const HUSD_DataHandle &data,
        int nodeid,
	bool strip_layers,
        fpreal t)
{
    HUSD_AutoReadLock	 lock(data);
    auto		 indata = lock.data();

    myStrippedLayers = false;
    if (indata && indata->isStageValid())
    {
        // We don't care about any errors generated assembling the locked
        // stage. This compose operation isn't really part of any LOP cook
        // process.
        UT_ErrorManager  ignore_errors_mgr;
        HUSD_ErrorScope  ignore_errors(&ignore_errors_mgr);
	auto		 instage = indata->stage();
	auto		&insourcelayers = indata->sourceLayers();

	myPrivate->myTicketArray = indata->tickets();
	myPrivate->myStage = HUSDcreateStageInMemory(
            indata->loadMasks().get(), indata->stage());

	auto			 outroot = myPrivate->myStage->GetRootLayer();
	std::vector<std::string> outsublayerpaths;
	SdfLayerOffsetVector	 outsublayeroffsets;

	// Copy the metadata from the first sublayer to the root layer of the
	// new stage. We do this because we want the strongest layer's
	// configuration (save path and default prim in particular) to be
	// adopted by the root layer. This means when we save with the USD ROP,
	// references to this layer will be saved as expected, without the
	// near-empty, unconfigured root layer we would otherwise create from
	// the root layer. This is very much like what we do in the saveStage
	// function in HUSD_Save.
	//
	// Source Layers are stored in weakest to strongest order, so we need
	// to add them to the sublayer paths array in reverse order.
	for (int i = insourcelayers.size(); i --> 0;)
	{
	    const XUSD_LayerAtPath	&insourcelayer = insourcelayers(i);

	    // If we have been told to strip layers, and we reach a layer that
	    // indicates a layer break, then exit the loop to avoid adding the
	    // remaining layers to the locked stage.
	    if (strip_layers)
	    {
		if (insourcelayer.myRemoveWithLayerBreak)
		{
		    myStrippedLayers = true;
		    continue;
		}
	    }

	    if (i == insourcelayers.size()-1 &&
		insourcelayer.myLayer->IsAnonymous())
	    {
		// If our first (strongest) layer is an anonymous layer, we
		// want to transfer it into the root layer for the reasons
		// described at the top of this loop.
		outroot->TransferContent(insourcelayer.myLayer);
	    }
	    else
	    {
		// If the strongest layer is not an anonymous layer, we must
		// have just added a file as a sublayer.  In this case, we act
		// as if the "strongest layer metadata" is blank, and don't
		// copy any layers into the root layer. But we have to at least
		// set a creator node on the root layer or else when it comes
		// times to save this layer, we won't generate a valid name for
		// it.
		if (i == insourcelayers.size()-1)
		    HUSDsetCreatorNode(outroot, nodeid);
		outsublayerpaths.push_back(insourcelayer.myIdentifier);
		outsublayeroffsets.push_back(insourcelayer.myOffset);
	    }
	}

	// Add the sublayers to the root layer along with the matching offsets.
	for (int i = 0, n = outsublayerpaths.size(); i < n; i++)
	{
	    outroot->InsertSubLayerPath(outsublayerpaths[i]);
	    outroot->SetSubLayerOffset(outsublayeroffsets[i],
		outroot->GetNumSubLayerPaths() - 1);
	}

	myRootLayerIdentifier = outroot->GetIdentifier();

        OP_Node      *lop = OP_Node::lookupNode(nodeid);
        if (CAST_LOPNODE(lop))
            myStageCacheIdentifier =
                GusdStageCache::CreateLopStageIdentifier(lop, strip_layers, t);
        else
            myStageCacheIdentifier = myRootLayerIdentifier;
    }

    // Add this locked stage to the GusdStageCache, because it is safe to
    // use it for creating GT primitives and transform caches.
    if (isValid())
    {
	GusdStageCacheReader	 cache;

	cache.InsertStage(myPrivate->myStage,
	    myStageCacheIdentifier,
	    GusdStageOpts(),
	    GusdStageEditPtr());
    }

    return isValid();
}

bool
HUSD_LockedStage::isValid() const
{
    return myPrivate->myStage;
}

