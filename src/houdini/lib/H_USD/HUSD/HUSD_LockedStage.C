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

#include "HUSD_LockedStage.h"
#include "HUSD_Constants.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <gusd/stageCache.h>
#include <UT/UT_StringSet.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

class HUSD_LockedStage::husd_LockedStagePrivate {
public:
    UsdStageRefPtr		 myStage;
    XUSD_TicketArray		 myTicketArray;
};

HUSD_LockedStage::HUSD_LockedStage(const HUSD_DataHandle &data,
	bool strip_layers)
    : myPrivate(new HUSD_LockedStage::husd_LockedStagePrivate()),
      myStrippedLayers(false)
{
    lockStage(data, strip_layers);
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

	paths.insert(myRootLayerIdentifier);
	cache.Clear(paths);
        HUSDclearBestRefPathCache(myRootLayerIdentifier.toStdString());
    }
    myPrivate->myStage.Reset();
    myPrivate->myTicketArray.clear();
    myRootLayerIdentifier.clear();
}

bool
HUSD_LockedStage::lockStage(const HUSD_DataHandle &data,
	bool strip_layers)
{
    HUSD_AutoReadLock	 lock(data);
    auto		 indata = lock.data();

    myStrippedLayers = false;
    if (indata && indata->isStageValid())
    {
	auto		 instage = indata->stage();
	auto		&insourcelayers = indata->sourceLayers();

	myPrivate->myTicketArray = indata->tickets();
	myPrivate->myStage = HUSDcreateStageInMemory(indata->loadMasks().get(),
	    OP_INVALID_ITEM_ID, indata->stage());

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
		    HUSDsetCreatorNode(outroot, data.nodeId());
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
    }

    // Add this locked stage to the GusdStageCache, because it is safe to
    // use it for creating GT primitives and transform caches.
    if (isValid())
    {
	GusdStageCacheWriter	 cache;

	cache.InsertStage(myPrivate->myStage,
	    myRootLayerIdentifier,
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

bool
HUSD_LockedStage::strippedLayers() const
{
    return myStrippedLayers;
}

