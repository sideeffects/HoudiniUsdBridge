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

#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_LoadMasks.h"
#include "HUSD_MirrorRootLayer.h"
#include "HUSD_PerfMonAutoCookEvent.h"
#include "HUSD_Preferences.h"
#include "XUSD_Data.h"
#include "XUSD_MirrorRootLayerData.h"
#include "XUSD_OverridesData.h"
#include "XUSD_Utils.h"
#include <OP/OP_Director.h>
#include <UT/UT_Assert.h>
#include <UT/UT_Debug.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_Exit.h>
#include <UT/UT_Set.h>
#include <UT/UT_StringMMPattern.h>
#include <algorithm>
#include <pxr/base/arch/systemInfo.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/variantSets.h>
#include <string.h>

PXR_NAMESPACE_OPEN_SCOPE

static UT_Set<XUSD_Data *>	 theRegisteredData;
static bool			 theExitCallbackRegistered = false;

namespace
{

class xusd_ReferenceInfo
{
public:
                     xusd_ReferenceInfo(const SdfLayerRefPtr &layer)
                     {
                         auto refs = HUSDgetExternalReferences(layer);
                         for (auto &&it : refs)
                            myOriginalRefs.insert(it.first);
                         initFromOriginalRefs(layer);
                     }
                     xusd_ReferenceInfo(const XUSD_LayerAtPathArray &layers)
                     {
                         for (auto &&layeratpath : layers)
                         {
                             SdfLayerRefPtr layer = layeratpath.myLayer;

                             if (!HUSDisLopLayer(layer))
                             {
                                 std::string layerid = layer->GetIdentifier();
                                 myOriginalRefs.insert(layerid);
                             }
                         }
                         // Calculate new paths relative to an anonymous layer,
                         // which means we will treat relative paths as being
                         // relative to the current working directory.
                         initFromOriginalRefs(SdfLayer::CreateAnonymous());
                     }
                    ~xusd_ReferenceInfo()
                     { }

    bool             contains(const std::string &ref) const
                     {
                         if (myOriginalRefs.find(ref) !=
                             myOriginalRefs.end())
                             return true;

                         if (myAbsoluteRefs.find(ref) !=
                             myAbsoluteRefs.end())
                             return true;

                         return false;
                     }

    std::set<std::string> getMatches(const UT_StringMMPattern &pattern) const
                     {
                         std::set<std::string>	 matches;

                         for (auto &&it : myOriginalRefs)
                             if (UT_String(it).multiMatch(pattern))
                                 matches.emplace(it);
                         for (auto &&it : myAbsoluteRefs)
                             if (UT_String(it).multiMatch(pattern))
                                 matches.emplace(it);

                         return matches;
                     }

    const std::string &getAbsolute(const std::string &ref) const
                     {
                         auto mapit = myOriginalToAbsoluteMap.find(ref);

                         if (mapit != myOriginalToAbsoluteMap.end())
                             return mapit->second;

                         return ref;
                     }

    const std::string &getOriginal(const std::string &ref) const
                     {
                         auto mapit = myAbsoluteToOriginalMap.find(ref);

                         if (mapit != myAbsoluteToOriginalMap.end())
                             return mapit->second;

                         return ref;
                     }

    const std::map<std::string, std::string> &getOriginalToAbsoluteMap() const
                     {
                         return myOriginalToAbsoluteMap;
                     }

private:
    void             initFromOriginalRefs(const SdfLayerRefPtr &parentlayer)
                     {
                         UT_ASSERT(parentlayer);
                         for (auto &&ref : myOriginalRefs)
                         {
                             std::string	 absref;

                             absref = parentlayer->ComputeAbsolutePath(ref);
                             myAbsoluteRefs.emplace(absref);
                             myOriginalToAbsoluteMap[ref] = absref;
                             myAbsoluteToOriginalMap[absref] = ref;
                         }
                     }

    std::set<std::string>		 myOriginalRefs;
    std::set<std::string>		 myAbsoluteRefs;
    std::map<std::string, std::string>	 myOriginalToAbsoluteMap;
    std::map<std::string, std::string>	 myAbsoluteToOriginalMap;
};

typedef UT_Map<std::string, xusd_ReferenceInfo>
    xusd_IdentifierToReferenceInfoMap;

// Extracts information about layer references for use in generic layer
// replacement algorithm.
void
addExternalReferenceInfo(const SdfLayerRefPtr &layer,
        xusd_IdentifierToReferenceInfoMap &refmap)
{
    auto	 refit = refmap.find(layer->GetIdentifier());

    if (refit == refmap.end())
    {
        refit = refmap.emplace(layer->GetIdentifier(),
            xusd_ReferenceInfo(layer)).first;

        for (auto &&ref : refit->second.getOriginalToAbsoluteMap())
        {
            auto	 reflayer = SdfLayer::Find(ref.second);

            if (reflayer)
                addExternalReferenceInfo(reflayer, refmap);
        }
    }
}

void
buildExternalReferenceInfo(const XUSD_LayerAtPathArray &sourcelayers,
        xusd_IdentifierToReferenceInfoMap &refmap)
{
    refmap.emplace(UT_StringHolder::theEmptyString,
        xusd_ReferenceInfo(sourcelayers));

    for (auto &&layeratpath : sourcelayers)
        addExternalReferenceInfo(layeratpath.myLayer, refmap);
}

int
getNewLayerColorIndex(const XUSD_LayerAtPathArray &layers, int nodeid)
{
    int	 layer_color_index = nodeid;

    // Adding a new layer onto an existing chain should take the layer
    // id at the end of the chain so far and add one. This ensures as we
    // move down a chain of nodes the colors will keep rotating no matter
    // what the node ids are. We also add a very large number if the index
    // isn't already a very large number so that ids generated this way
    // won't conflict with ids that are copied from node ids (even if the
    // colors may get reused).
    if (layers.size() > 0 && layers.last().isLopLayer())
    {
        static const int VERY_LARGE_NUMBER = 100000000;

        layer_color_index = layers.last().myLayerColorIndex + 1;
        if (layer_color_index < VERY_LARGE_NUMBER)
            layer_color_index += VERY_LARGE_NUMBER;
    }

    return layer_color_index;
}

int
getExistingLayerColorIndex(const XUSD_LayerAtPathArray &layers, int nodeid)
{
    int	 layer_color_index = nodeid;

    if (layers.size() > 0 && layers.last().isLopLayer())
        layer_color_index = layers.last().myLayerColorIndex;

    return layer_color_index;
}

std::vector<std::string>
getSubLayerPaths(const SdfSubLayerProxy &sublayers)
{
    std::vector<std::string> sublayerpaths;

    for (auto &&sublayer : sublayers)
        sublayerpaths.push_back(sublayer);

    return sublayerpaths;
}

} // end namespace

XUSD_LayerAtPath::XUSD_LayerAtPath()
    : myLayerColorIndex(0),
      myRemoveWithLayerBreak(false),
      myLayerIsMissingFile(false)
{
}

XUSD_LayerAtPath::XUSD_LayerAtPath(const SdfLayerRefPtr &layer,
	const SdfLayerOffset &offset,
	int layer_color_index)
    : myLayer(layer),
      myIdentifier(layer->GetIdentifier()),
      myOffset(offset),
      myLayerColorIndex(layer_color_index),
      myRemoveWithLayerBreak(false),
      myLayerIsMissingFile(false)
{
    UT_ASSERT(layer && HUSDisLopLayer(layer));
}

XUSD_LayerAtPath::XUSD_LayerAtPath(const SdfLayerRefPtr &layer,
	const std::string &filepath,
	const SdfLayerOffset &offset,
	int layer_color_index)
    : myLayer(layer),
      myIdentifier(filepath),
      myOffset(offset),
      myLayerColorIndex(layer_color_index),
      myRemoveWithLayerBreak(false),
      myLayerIsMissingFile(false)
{
    if (!myLayer)
    {
	myLayer = HUSDcreateAnonymousLayer();
	myLayerIsMissingFile = true;
    }
}

bool
XUSD_LayerAtPath::hasLayerColorIndex(int &clridx) const
{
    if (isLopLayer() && myLayerColorIndex >= 0)
    {
	clridx = myLayerColorIndex;
	return true;
    }

    return false;
}

bool
XUSD_LayerAtPath::isLopLayer() const
{
    if (myLayerIsMissingFile)
	return false;

    return HUSDisLopLayer(myLayer);
}

XUSD_OverridesInfo::XUSD_OverridesInfo(const UsdStageRefPtr &stage)
    : myVersionId(0)
{
    static const std::string theLayerTags[HUSD_OVERRIDES_NUM_LAYERS] = {
        "Custom",
        "Purpose",
        "Solo Lights",
        "Solo Geometry",
        "Selectability",
        "Visibility and Activation"
    };
    SdfSubLayerProxy sublayers = stage->GetSessionLayer()->GetSubLayerPaths();

    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
    {
	mySessionLayers[i] = HUSDcreateAnonymousLayer(
            SdfLayerHandle(), theLayerTags[i]);
	sublayers.push_back(mySessionLayers[i]->GetIdentifier());
    }
}

XUSD_OverridesInfo::~XUSD_OverridesInfo()
{
}

XUSD_PostLayersInfo::XUSD_PostLayersInfo(const UsdStageRefPtr &stage)
    : myVersionId(0)
{
}

XUSD_PostLayersInfo::~XUSD_PostLayersInfo()
{
}

void
XUSD_Data::exitCallback(void *)
{
    for (auto &&data : theRegisteredData)
	data->reset();
    theRegisteredData.clear();
}

XUSD_Data::XUSD_Data(HUSD_MirroringType mirroring)
    : myActiveLayerIndex(0),
      myOwnsActiveLayer(false),
      myMirrorLoadRulesChanged(false),
      myMirroring(mirroring)
{
    if (!theExitCallbackRegistered)
    {
	UT_Exit::addExitCallback(exitCallback);
	theExitCallbackRegistered = true;
    }
    theRegisteredData.insert(this);
}

XUSD_Data::XUSD_Data(const UsdStageRefPtr &stage)
    : myActiveLayerIndex(0),
      myOwnsActiveLayer(false),
      myMirrorLoadRulesChanged(false),
      myMirroring(HUSD_EXTERNAL_STAGE),
      myStage(stage),
      myLoadMasks(nullptr)
{
    if (!theExitCallbackRegistered)
    {
        UT_Exit::addExitCallback(exitCallback);
        theExitCallbackRegistered = true;
    }
    theRegisteredData.insert(this);

    myRootLayerData = UTmakeShared<XUSD_RootLayerData>(myStage);
    myStageLayers = UTmakeShared<XUSD_LayerArray>();
    myStageLayerAssignments = UTmakeShared<UT_StringArray>();
    myStageLayerCount = UTmakeShared<int>(0);
    myDataLock.reset(new XUSD_DataLock());
}

XUSD_Data::~XUSD_Data()
{
    theRegisteredData.erase(this);
}

void
XUSD_Data::reset()
{
    UT_ASSERT(!myDataLock || !myDataLock->isLocked() || UT_Exit::isExiting());
    myStage.Reset();
    myStageLayerAssignments.reset();
    myStageLayers.reset();
    myStageLayerCount.reset();
    mySourceLayers.clear();
    myRootLayerData.reset();
    myLockedGeoArray.clear();
    myHeldLayers.clear();
    myReplacementLayerArray.clear();
    myLockedStages.clear();
    myActiveLayerIndex = 0;
    myOwnsActiveLayer = false;
    myOverridesInfo.reset();
    myPostLayersInfo.reset();
    myLoadMasks.reset();
    myDataLock.reset();
}

void
XUSD_Data::createInitialPlaceholderSublayers()
{
    int numlayers = UT_EnvControl::getInt(ENV_HOUDINI_LOP_PLACEHOLDER_LAYERS);

    if (numlayers > 0)
    {
        SdfSubLayerProxy sublayers(myStage->GetRootLayer()->GetSubLayerPaths());

        // Append empty sublayers to the stage to be replaced by LOP_Node
        // authored layers without having to edit the sublayers of the
        // stage, which can be very expensive once we add a large on-disk
        // layer to the stage. This ensures that appending the first xform
        // node after loading a large file doesn't cause a huge delay.
        for (int i = 0; i < numlayers; i++)
        {
            myStageLayerAssignments->append(UT_StringHolder::theEmptyString);
            // Copy the stage's root prim metadata to the
            // placeholder to prevent pointless expensive edits.
            myStageLayers->append(HUSDcreateAnonymousLayer(
                myStage->GetRootLayer()));
            HUSDsetSaveControl(myStageLayers->last(),
                HUSD_Constants::getSaveControlPlaceholder());
            myStageLayers->last()->SetPermissionToEdit(false);
            sublayers.insert(sublayers.begin(),
                myStageLayers->last()->GetIdentifier());
        }
    }
}

void
XUSD_Data::createNewData(const HUSD_LoadMasksPtr &load_masks,
	int resolver_context_nodeid,
	const UsdStageWeakPtr &context_stage,
	const ArResolverContext *resolver_context)
{
    // Breand new empty stage, new lock, new layer assignment array, no layers.
    UT_ASSERT(!myDataLock || !myDataLock->isLocked());
    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING);
    reset();
    myStage = HUSDcreateStageInMemory(load_masks.get(),
	context_stage, resolver_context_nodeid, resolver_context);
    myLoadMasks = load_masks;

    myRootLayerData = UTmakeShared<XUSD_RootLayerData>(myStage);
    myStageLayers = UTmakeShared<XUSD_LayerArray>();
    myStageLayerAssignments = UTmakeShared<UT_StringArray>();
    myStageLayerCount = UTmakeShared<int>(0);
    myOverridesInfo = UTmakeShared<XUSD_OverridesInfo>(myStage);
    myPostLayersInfo = UTmakeShared<XUSD_PostLayersInfo>(myStage);
    myDataLock.reset(new XUSD_DataLock());
    createInitialPlaceholderSublayers();
}

void
XUSD_Data::createHardCopy(const XUSD_Data &src)
{
    // This method is called after creating a new XUSD_Data because we
    // couldn't lock an HUSD_DataHandle because its shared data was already
    // locked by someone else. So we create a new block of shared data, and
    // then copy all the unshared parts from the original data, such as the
    // source layers, lockedgeos, and active layer index. This method is also
    // used when creating a new stage with a forced layer replacement.
    UT_ASSERT(!myDataLock || !myDataLock->isLocked());
    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING &&
	      src.myMirroring == HUSD_NOT_FOR_MIRRORING);

    mySourceLayers = src.mySourceLayers;
    myRootLayerData = src.myRootLayerData;
    myLockedGeoArray = src.myLockedGeoArray;
    myHeldLayers = src.myHeldLayers;
    myReplacementLayerArray = src.myReplacementLayerArray;
    myLockedStages = src.myLockedStages;
    myActiveLayerIndex = src.myActiveLayerIndex;
}

void
XUSD_Data::createSoftCopy(const XUSD_Data &src,
	const HUSD_LoadMasksPtr &load_masks,
	bool make_new_implicit_layer)
{
    // Reference the stage, lock, and layer assignment array from the source
    // data. When we lock this data, update the stage and layer assignment
    // array. Copy the layer arrays, and the active layer index from the src.
    UT_ASSERT(!myDataLock || !myDataLock->isLocked());
    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING &&
	      src.myMirroring == HUSD_NOT_FOR_MIRRORING);

    // A null load_masks means we should adopt the src load masks. If
    // load_masks is not null, and the src load masks are equal to *load_masks,
    // then we can still just adopt the src load masks (which is faster than
    // creating a new copy of the stage).
    if (load_masks &&
        ((load_masks->isEmpty() &&
          src.myLoadMasks && !src.myLoadMasks->isEmpty()) ||
         (!load_masks->isEmpty() &&
          (!src.myLoadMasks || *load_masks != *src.myLoadMasks))))
    {
	// If we have been given a load masks structure, we need to make a new
	// stage configured with these load masks. Then we copy the source
	// layers, offsets, and lockedgeos from the source data.
	createNewData(load_masks, OP_INVALID_ITEM_ID, src.myStage, nullptr);
	mySourceLayers = src.mySourceLayers;
        myRootLayerData = src.myRootLayerData;
	myLockedGeoArray = src.myLockedGeoArray;
        myHeldLayers = src.myHeldLayers;
	myReplacementLayerArray = src.myReplacementLayerArray;
	myLockedStages = src.myLockedStages;
    }
    else
    {
	// If we are not passed a load masks structure, we want to use the load
	// masks of the source data, along with the same stage and everything
	// else.
	reset();
	myStage = src.myStage;
	myStageLayers = src.myStageLayers;
	myStageLayerAssignments = src.myStageLayerAssignments;
	myStageLayerCount = src.myStageLayerCount;
	myOverridesInfo = src.myOverridesInfo;
        myPostLayersInfo = src.myPostLayersInfo;
	mySourceLayers = src.mySourceLayers;
        myRootLayerData = src.myRootLayerData;
	myLockedGeoArray = src.myLockedGeoArray;
        myHeldLayers = src.myHeldLayers;
	myReplacementLayerArray = src.myReplacementLayerArray;
	myLockedStages = src.myLockedStages;
	myLoadMasks = src.myLoadMasks;
	myDataLock = src.myDataLock;
    }

    if (make_new_implicit_layer)
	myActiveLayerIndex = src.mySourceLayers.size();
    else
	myActiveLayerIndex = src.myActiveLayerIndex;
}

void
XUSD_Data::createCopyWithReplacement(
	const XUSD_Data &src,
	const UT_StringRef &frompath,
	const UT_StringRef &topath,
	int nodeid,
	HUSD_MakeNewPathFunc make_new_path,
	UT_StringSet &replaced_layers)
{
    // Create a new stage with the same source layers as the source data.
    // But scan for a particular layer we want to replace, and change any
    // references to that layer to point to a new layer. Any layers changed
    // this way must then have any references to them replaced, and so on
    // recursively.
    UT_ASSERT(!myDataLock || !myDataLock->isLocked());
    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING &&
	      src.myMirroring == HUSD_NOT_FOR_MIRRORING);

    createNewData(src.loadMasks(), OP_INVALID_ITEM_ID, src.myStage, nullptr);
    createHardCopy(src);

    UT_Array<std::pair<std::string, std::string> > replacearray;
    ArResolverContextBinder binder(myStage->GetPathResolverContext());
    UT_Map<std::string, SdfLayerRefPtr> newlayermap;
    xusd_IdentifierToReferenceInfoMap refmap;
    std::string topathstr = topath.toStdString();

    // Populate a map of all layer identifiers to the layers they reference.
    buildExternalReferenceInfo(mySourceLayers, refmap);

    // If the "topath" isn't set, we don't want to do anything.
    if (!topathstr.empty())
    {
        if (UT_String::multiMatchCheck(frompath.c_str()))
        {
            UT_StringMMPattern   pattern;

            // The frompath has wildcards, so we have to test it against all
            // the layers on the stage.
            pattern.compile(frompath.c_str());
            for (auto &&refit : refmap)
            {
                for (auto &&fromit : refit.second.getMatches(pattern))
                {
                    replacearray.append(std::make_pair(fromit, topathstr));
                }
            }
        }
        else if (frompath.isstring())
        {
            // We only want to do reaplacement if the frompath is set. The
            // refmap will have an entry for an empty string, but we shouldn't
            // be doing any replacement based on it.
            replacearray.append(
                std::make_pair(frompath.toStdString(), topathstr));
        }
    }

    // Go through the references looking for replacements. Create all the
    // required replacement layers by copying the source layers.
    for (int repidx = 0; repidx < replacearray.size(); repidx++)
    {
	const std::string	&from = replacearray[repidx].first;

	// Go through all layers to see which ones need to be replaced based
	// on the existing set of replacements.
	for (auto &&refit : refmap)
	{
	    // Skip layers that have already been added to the new layer map.
	    if (!newlayermap.contains(refit.first) &&
		refit.second.contains(from))
	    {
                // The refit key may be an empty string if it contains the
                // layers from mySourceLayers. In this case we are using this
                // loop for a slightly different purpose of finding entries in
                // mySourceLayers that match the pattern rather than finding
                // parent layers that need to be replaced because of already
                // know replace requests.
                if (refit.first.empty())
                {
                    std::string  origpath = refit.second.getAbsolute(from);

                    // Test again with the correct path if we have already
                    // registered this layer in the newlayermap.
                    if (!newlayermap.contains(origpath))
                    {
                        replaced_layers.insert(from);
                        newlayermap[origpath] = SdfLayer::Find(topathstr);
                    }
                }
                else
                {
                    SdfLayerRefPtr oldlayer = SdfLayer::Find(refit.first);
                    SdfLayerRefPtr newlayer = HUSDcreateAnonymousCopy(oldlayer);

                    replaced_layers.insert(from);
                    if (!HUSDisLopLayer(oldlayer))
                    {
                        UT_StringHolder  newsavepath;

                        newsavepath = make_new_path(refit.first);
                        HUSDsetSavePath(newlayer, newsavepath, false);
                        HUSDsetCreatorNode(newlayer, nodeid);
                        HUSDsetSaveControl(newlayer,
                            HUSD_Constants::getSaveControlIsFileFromDisk());
                    }

                    newlayermap[refit.first] = newlayer;
                    replacearray.append(std::make_pair(
                        refit.first, newlayer->GetIdentifier()));
                }
	    }
	}
    }

    // Go through the reference map performaing any required updates on the
    // new copies of the layers.
    for (auto &&refit : refmap)
    {
        SdfLayerRefPtr oldlayer = SdfLayer::Find(refit.first);

	for (int repidx = 0; repidx < replacearray.size(); repidx++)
	{
	    const std::string	&from = replacearray[repidx].first;

	    if (refit.second.contains(from))
	    {
		std::map<std::string, std::string>	 replacemap;

		// Convert the replacearray into a map we can pass to
		// HUSDupdateExternalReferences. We don't know if there is
		// a relative or absolute reference to the file being
		// replaced, so add entries for both. We also don't know if
		// the replace array entry is relative or absolute, so do
		// a lookup in both direction (orig to abs and abs to orig).
		for (auto &&repit : replacearray)
		{
		    replacemap[refit.second.getOriginal(repit.first)] =
			repit.second;
		    replacemap[refit.second.getAbsolute(repit.first)] =
			repit.second;
		}

		// Convert any relative references in the file to be absolute
		// since the layer is going to be anonymous now.
		for (auto &&it : refit.second.getOriginalToAbsoluteMap())
		{
		    // Skip any references that are already being updated
		    // to point to anonymous layers.
		    if (replacemap.find(it.first) != replacemap.end())
			continue;

                    replacemap[it.first] =
                        oldlayer->ComputeAbsolutePath(it.second);
		}

		// If we find any reference we want to replace, do all the
		// replacements in one call, then we can break out of this
		// loop because we've done all the replacing we can do. Skip
                // this step if the refit key is an empty string, indicating
                // that the map entries come from mySourceLayers.
                if (!refit.first.empty())
                    HUSDupdateExternalReferences(
                        newlayermap[refit.first], replacemap);
		break;
	    }
	}
    }

    // Go through our source layers and replace any that have new versions.
    for (int srcidx = 0; srcidx < mySourceLayers.size(); srcidx++)
    {
	auto	 srcid = mySourceLayers(srcidx).myLayer->GetIdentifier();
	auto	 it = newlayermap.find(srcid);

	if (it != newlayermap.end())
	{
	    mySourceLayers(srcidx).myLayer = it->second;
	    mySourceLayers(srcidx).myIdentifier = it->second->GetIdentifier();
	    newlayermap.erase(it);
	}
    }

    // Store pointers to any replacement layers we created that were not put
    // in mySourceLayers. Otherwise these layers will get deleted when the
    // newlayermap is destroyed.
    for (auto &&layerit : newlayermap)
	myReplacementLayerArray.append(layerit.second);
}

void
XUSD_Data::flattenLayers(const XUSD_Data &src, int creator_node_id)
{
    UT_ASSERT(!myDataLock || !myDataLock->isLocked());
    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING &&
	      src.myMirroring == HUSD_NOT_FOR_MIRRORING);

    // We always want to start from scratch when flattening.
    createNewData(src.loadMasks(), OP_INVALID_ITEM_ID, src.myStage, nullptr);
    mySourceLayers.append(XUSD_LayerAtPath(
	src.createFlattenedLayer(HUSD_WARN_STRIPPED_LAYERS),
	SdfLayerOffset(), creator_node_id));
    HUSDclearEditorNodes(mySourceLayers.last().myLayer);
    HUSDsetCreatorNode(mySourceLayers.last().myLayer, creator_node_id);
    HUSDaddEditorNode(mySourceLayers.last().myLayer, creator_node_id);
    myRootLayerData = src.myRootLayerData;
    myLockedGeoArray = src.myLockedGeoArray;
    myHeldLayers = src.myHeldLayers;
    myReplacementLayerArray = src.myReplacementLayerArray;
    myLockedStages = src.myLockedStages;
    myActiveLayerIndex = 0;
}

void
XUSD_Data::flattenStage(const XUSD_Data &src, int creator_node_id)
{
    UT_ASSERT(!myDataLock || !myDataLock->isLocked());
    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING &&
	      src.myMirroring == HUSD_NOT_FOR_MIRRORING);

    // We always want to start from scratch when flattening.
    createNewData(src.loadMasks(), OP_INVALID_ITEM_ID, src.myStage, nullptr);
    mySourceLayers.append(XUSD_LayerAtPath(
	src.createFlattenedStage(HUSD_WARN_STRIPPED_LAYERS),
	SdfLayerOffset(), creator_node_id));
    HUSDclearEditorNodes(mySourceLayers.last().myLayer);
    HUSDsetCreatorNode(mySourceLayers.last().myLayer, creator_node_id);
    HUSDaddEditorNode(mySourceLayers.last().myLayer, creator_node_id);
    myRootLayerData = src.myRootLayerData;
    myLockedGeoArray = src.myLockedGeoArray;
    myHeldLayers = src.myHeldLayers;
    myReplacementLayerArray = src.myReplacementLayerArray;
    myLockedStages = src.myLockedStages;
    myActiveLayerIndex = 0;
}

void
XUSD_Data::mirror(const XUSD_Data &src,
	const HUSD_LoadMasks &load_masks)
{
    UT_ASSERT(!myDataLock || !myDataLock->isLocked());
    UT_ASSERT(myMirroring == HUSD_FOR_MIRRORING &&
	      src.myMirroring == HUSD_NOT_FOR_MIRRORING);

    UsdStagePopulationMask stage_mask =
	HUSDgetUsdStagePopulationMask(load_masks);

    // If the source data also has a stage mask, we want to mirror with the
    // intersection of the two stage masks, so the viewport never shows
    // anything that isn't shown in the scene graph tree.
    if (src.loadMasks())
	stage_mask = stage_mask.GetIntersection(
	    HUSDgetUsdStagePopulationMask(*src.loadMasks()));

    // Then add the passed in load_masks information.
    if (!load_masks.loadAll())
    {
        myMirrorLoadRules = UsdStageLoadRules::LoadNone();
        if (!src.loadMasks() ||
            src.loadMasks()->loadAll() ||
            HUSD_Preferences::allowViewportOnlyPayloads())
        {
            // If the input stage is loading all payloads, or we are allowing
            // payloads to be loaded into the viewport only, then load_masks
            // becomes the source of all payload loading rules.
            for (auto &&path : load_masks.loadPaths())
                myMirrorLoadRules.LoadWithDescendants(HUSDgetSdfPath(path));
        }
        else
        {
            // Otherwise the input stage has payload loading restrictions, and
            // loading payloads into the viewport only isn't allowed, so we
            // only want to load the intersection of the two sets of payloads
            // flagged for loading.
            UsdStageLoadRules stagerules(UsdStageLoadRules::LoadNone());
            UsdStageLoadRules viewportrules(UsdStageLoadRules::LoadNone());

            // Convert the stage load set and the viewport load set into
            // UsdStageLoadRules objects.
            for (auto &&path : src.loadMasks()->loadPaths())
                stagerules.LoadWithDescendants(HUSDgetSdfPath(path));
            for (auto &&path : load_masks.loadPaths())
                viewportrules.LoadWithDescendants(HUSDgetSdfPath(path));

            // First look for any load_mask paths that appear in the stage
            // paths, and check if they are also in the viewport paths.
            for (auto &&path : load_masks.loadPaths())
            {
                auto sdfpath(HUSDgetSdfPath(path));
                if (stagerules.IsLoadedWithAllDescendants(sdfpath))
                    myMirrorLoadRules.LoadWithDescendants(sdfpath);
            }

            // Then look for any source paths that appear in the load_mask
            // paths to load. Containment in either direction is okay.
            for (auto &&path : src.loadMasks()->loadPaths())
            {
                auto sdfpath(HUSDgetSdfPath(path));
                if (viewportrules.IsLoadedWithAllDescendants(sdfpath))
                    myMirrorLoadRules.LoadWithDescendants(sdfpath);
            }
        }
    }
    else if (src.loadMasks() && !src.loadMasks()->loadAll())
    {
        // Viewport says "load all", so copy the load rules from the stage.
        myMirrorLoadRules = UsdStageLoadRules::LoadNone();
        for (auto &&path : src.loadMasks()->loadPaths())
            myMirrorLoadRules.LoadWithDescendants(HUSDgetSdfPath(path));
    }
    else
    {
        // Both the viewport and the stage say "load all".
        myMirrorLoadRules = UsdStageLoadRules::LoadAll();
    }

    // If the stage population mask changes, or the load rules goes from
    // loading all prims to not loading all prims (or vice versa), or the
    // resolver context changes... All of these require rebuilding the
    // mirror stage from scratch.
    bool mirror_stage_is_new = false;

    if (!myStage ||
        (myMirrorLoadRules == UsdStageLoadRules::LoadAll()) !=
            (myStage->GetLoadRules() == UsdStageLoadRules::LoadAll()) ||
	stage_mask != myStage->GetPopulationMask() ||
        load_masks.variantSelectionFallbacks() !=
            myMirrorVariantSelectionFallbacks ||
	src.myStage->GetPathResolverContext() !=
	    myStage->GetPathResolverContext())
    {
	// Make a new stage, and copy the layers from the source. Make a new
	// layer assignment array (equal to the source) so that locking this
	// data will just add the current myStageLayers onto the stage. Set the
	// active layer after all existing layers, because we want to treat
	// everything up to this harden operation as un-editable.
	reset();
        myMirrorVariantSelectionFallbacks =
            load_masks.variantSelectionFallbacks();
        PcpVariantFallbackMap oldfallbacks;
        PcpVariantFallbackMap fallbacks;
        HUSDconvertVariantSelectionFallbacks(
            myMirrorVariantSelectionFallbacks, fallbacks);
        oldfallbacks = UsdStage::GetGlobalVariantFallbacks();
        UsdStage::SetGlobalVariantFallbacks(fallbacks);
	myStage = HUSDcreateStageInMemory(
            myMirrorLoadRules == UsdStageLoadRules::LoadAll()
                ? UsdStage::LoadAll : UsdStage::LoadNone,
	    src.myStage);
        UsdStage::SetGlobalVariantFallbacks(oldfallbacks);
	myStage->SetPopulationMask(stage_mask);
	myStageLayers = UTmakeShared<XUSD_LayerArray>();
	myStageLayerAssignments = UTmakeShared<UT_StringArray>();
	myStageLayerCount = UTmakeShared<int>(0);
	myOverridesInfo = UTmakeShared<XUSD_OverridesInfo>(myStage);
        myPostLayersInfo = UTmakeShared<XUSD_PostLayersInfo>(myStage);
	myDataLock.reset(new XUSD_DataLock());
        myStage->SetLoadRules(myMirrorLoadRules);
        createInitialPlaceholderSublayers();
        mirror_stage_is_new = true;
    }

    // Configure layer muting. This list is managed by the stage itself, so
    // does not need to be checked during stage locking. A change to layer
    // muting does not require a complete recreation of the stage like a
    // change to the stage mask does.
    UT_SortedStringSet	 mutelayers = load_masks.muteLayers();

    if (src.loadMasks())
	mutelayers.insert(src.loadMasks()->muteLayers().begin(),
	    src.loadMasks()->muteLayers().end());

    if (!mutelayers.empty() ||
	!myStage->GetMutedLayers().empty())
    {
	std::vector<std::string>	 newmutelayers;
	std::vector<std::string>	 oldmutelayers;

	for (auto &&identifier : mutelayers)
	    newmutelayers.push_back(identifier.toStdString());
	if (newmutelayers != myStage->GetMutedLayers())
	{
	    std::vector<std::string>	 addlayers;
	    std::vector<std::string>	 removelayers;

	    oldmutelayers = myStage->GetMutedLayers();
	    std::sort(newmutelayers.begin(), newmutelayers.end());
	    std::sort(oldmutelayers.begin(), oldmutelayers.end());
	    std::set_difference(newmutelayers.begin(), newmutelayers.end(),
		oldmutelayers.begin(), oldmutelayers.end(),
		std::inserter(addlayers, addlayers.begin()));
	    std::set_difference(oldmutelayers.begin(), oldmutelayers.end(),
		newmutelayers.begin(), newmutelayers.end(),
		std::inserter(removelayers, removelayers.begin()));
	    myStage->MuteAndUnmuteLayers(addlayers, removelayers);
	}
    }

    if (!mirror_stage_is_new && myMirrorLoadRules != myStage->GetLoadRules())
        myMirrorLoadRulesChanged = true;
    else
        myMirrorLoadRulesChanged = false;

    mySourceLayers = src.mySourceLayers;
    myRootLayerData = src.myRootLayerData;
    myLockedGeoArray = src.myLockedGeoArray;
    myHeldLayers = src.myHeldLayers;
    myReplacementLayerArray = src.myReplacementLayerArray;
    myLockedStages = src.myLockedStages;
    myActiveLayerIndex = mySourceLayers.size();
}

bool
XUSD_Data::mirrorUpdateRootLayer(const HUSD_MirrorRootLayer &rootlayer)
{
    UT_ASSERT(!myDataLock || !myDataLock->isLocked());
    UT_ASSERT(myMirroring == HUSD_FOR_MIRRORING);

    SdfPath campath(HUSDgetHoudiniFreeCameraSdfPath());

    HUSDcopySpec(rootlayer.data().layer(), campath,
        myStage->GetRootLayer(), campath);

    return true;
}

bool
XUSD_Data::addLayer(const std::string &filepath,
	const SdfLayerOffset &offset,
	int position,
	XUSD_AddLayerOp add_layer_op,
        bool copy_root_prim_metadata)
{
    std::vector<std::string>     paths( { filepath } );
    SdfLayerOffsetVector         offsets( { offset } );

    return addLayers(paths, offsets, position,
        add_layer_op, copy_root_prim_metadata);
}

bool
XUSD_Data::addLayer(const XUSD_LayerAtPath &layer,
	int position,
	XUSD_AddLayerOp add_layer_op,
        bool copy_root_prim_metadata)
{
    XUSD_LayerAtPathArray    layers( { layer } );

    return addLayers(layers, position,
        add_layer_op, copy_root_prim_metadata);
}

bool
XUSD_Data::addLayers(const std::vector<std::string> &filepaths,
        const SdfLayerOffsetVector &offsets,
        int position,
	XUSD_AddLayerOp add_layer_op,
        bool copy_root_prim_metadata)
{
    std::vector<bool> layers_above_layer_break(filepaths.size(), false);

    return addLayers(filepaths,
        layers_above_layer_break,
        offsets,
        position,
        add_layer_op,
        copy_root_prim_metadata);
}

bool
XUSD_Data::addLayers(const std::vector<std::string> &filepaths,
        const std::vector<bool> &layers_above_layer_break,
        const SdfLayerOffsetVector &offsets,
        int position,
	XUSD_AddLayerOp add_layer_op,
        bool copy_root_prim_metadata)
{
    // Can't add a layer to the overrides or post layers.
    UT_ASSERT(myOverridesInfo->isEmpty());
    UT_ASSERT(myPostLayersInfo->isEmpty());
    // We must have a valid locked stage.
    UT_ASSERT(myDataLock->isWriteLocked() && myOwnsActiveLayer);
    UT_ASSERT(isStageValid());

    // Bind the stage's resolver context to help us resolve the file path.
    ArResolverContextBinder	 binder(myStage->GetPathResolverContext());
    XUSD_LayerAtPathArray        layers;

    for (int i = 0, n = filepaths.size(); i < n; i++)
    {
        auto                    &filepath = filepaths[i];
        SdfLayerRefPtr		 layer = SdfLayer::FindOrOpen(filepath);
        SdfLayerOffset           offset;
        bool                     editable = false;

        if (offsets.size() > i)
            offset = offsets[i];

        if (add_layer_op == XUSD_ADD_LAYERS_ALL_EDITABLE ||
            (add_layer_op == XUSD_ADD_LAYERS_LAST_EDITABLE &&
             i == n-1))
            editable = true;
        else if ((add_layer_op == XUSD_ADD_LAYERS_ALL_ANONYMOUS_EDITABLE ||
                  (add_layer_op == XUSD_ADD_LAYERS_LAST_ANONYMOUS_EDITABLE &&
                   i == n-1)) && HUSDisLopLayer(filepath))
            editable = true;

        if (layer)
        {
            // We have been asked to make this layer editable, but it's coming
            // from an external source, so we need to copy it into an anonymous
            // layer that we will be able to edit.
            if (editable)
            {
                SdfLayerRefPtr		 copy;
                std::string              nodepath;

                // Make a copy of the layer, because we don't want to edit the
                // source file. We always want to edit anonymous layers.
                copy = HUSDcreateAnonymousCopy(layer, HUSDgetTag(myDataLock));
                // If the layer doesn't already have a creator node, set it
                // to the LOP node making the copy. The creator node will
                // already be set
                if (!HUSDgetCreatorNode(copy, nodepath))
                    HUSDsetCreatorNode(copy, myDataLock->getLockedNodeId());
                HUSDaddEditorNode(copy, myDataLock->getLockedNodeId());
                // Any layer added as "editable" should be treated as an
                // implicit layer, not as a SOP layer when it comes to
                // flattening operations.
                HUSDsetTreatAsSopLayer(copy, false);

                // Add the modified copy to our list of source layers.
                layers.append(
                    XUSD_LayerAtPath(copy, copy->GetIdentifier(), offset));
            }
            else
                layers.append(
                    XUSD_LayerAtPath(layer, filepath, offset));
        }
        else if (editable)
        {
            // We couldn't open the layer from disk, but we have been asked for
            // an editable layer, so we need to create a new anonymous layer.
            auto empty = HUSDcreateAnonymousLayer(
                myStage->GetRootLayer(), HUSDgetTag(myDataLock));

            layers.append(
                XUSD_LayerAtPath(empty, empty->GetIdentifier(), offset));
        }
        else
        {
            // We couldn't open the layer from disk, but we still want to
            // record the fact that it should have been opened. There will be
            // errors when trying to compose the stage because this layer can't
            // be found, but this allows the user to author layers in a context
            // where not all the referenced layers are available.
            layers.append(
                XUSD_LayerAtPath(SdfLayerRefPtr(), filepath, offset));
        }

        // Copy the bool indicating if this layer is from above a layer break.
        layers.last().myRemoveWithLayerBreak = layers_above_layer_break[i];
        // If the last new layer is editable, set its layer color index based
        // on the node that currently has this data locked.
        if (editable)
            layers.last().myLayerColorIndex = getNewLayerColorIndex(
                mySourceLayers, myDataLock->getLockedNodeId());
    }

    // Call addLayers to add all the XUSD_LayerAtPaths all at once.
    return addLayers(layers, position, add_layer_op, copy_root_prim_metadata);
}

bool
XUSD_Data::addLayers(const XUSD_LayerAtPathArray &layers,
        int position,
	XUSD_AddLayerOp add_layer_op,
        bool copy_root_prim_metadata)
{
    // Can't add a layer to the overrides or post layers.
    UT_ASSERT(myOverridesInfo->isEmpty());
    UT_ASSERT(myPostLayersInfo->isEmpty());
    // We must have a valid locked stage.
    UT_ASSERT(myDataLock->isWriteLocked() && myOwnsActiveLayer);
    UT_ASSERT(isStageValid());

    // If the layers array is empty, we have nothing to do. Report success.
    if (layers.size() == 0)
        return true;

    SdfLayerRefPtr   root_prim_metadata_layer;

    // Don't allow adding the same sublayer twice. We need to stop this here
    // because the problem gets worse once we get to afterLock.
    for (auto &&layer : layers)
    {
        // We should not be adding placeholder layers to our source layers.
        UT_ASSERT(!layer.myLayer || !HUSDisLayerPlaceholder(layer.myLayer));
        if (layer.myLayer && HUSDisLayerPlaceholder(layer.myLayer))
            return false;

        for (int i = 0, n = mySourceLayers.size(); i < n; i++)
        {
            if (layer.myLayer->GetIdentifier() ==
                    mySourceLayers(i).myIdentifier ||
                layer.myIdentifier == mySourceLayers(i).myIdentifier)
            {
                HUSD_ErrorScope::addError(HUSD_ERR_DUPLICATE_SUBLAYER,
                    layer.myIdentifier.c_str());
                return false;
            }
        }

        // If requested, remember the first layer so we can copy the root prim
        // metadata from this layer onto the stage's root layer.
        if (copy_root_prim_metadata && layer.myLayer)
        {
            root_prim_metadata_layer = layer.myLayer;
            copy_root_prim_metadata = false;
        }
    }

    // The position argument is 0 for the strongest layer, -1 for the weakest.
    // Adjust the position to reflect the fact that mySourceLayers is ordered
    // weakest to strongest, so we must reverse the position argument.
    // We figure this out before releasing the lock in case the active layer
    // gets removed when we release the lock. We want the position to be
    // relative to the list of layers including the active layer, otherwise
    // the layer indices won't match up with what the users sees (which always
    // includes an active layer).
    int insertposition = 0;
    bool reverselayers = false;

    if (add_layer_op == XUSD_ADD_LAYERS_ALL_EDITABLE ||
        add_layer_op == XUSD_ADD_LAYERS_LAST_EDITABLE ||
        add_layer_op == XUSD_ADD_LAYERS_ALL_ANONYMOUS_EDITABLE ||
        add_layer_op == XUSD_ADD_LAYERS_LAST_ANONYMOUS_EDITABLE)
    {
        // We were asked to make all or the last layer editable. This is
        // only compatible with putting the layers at the end of
        // mySourceLayers, as the new strongest layers.
        UT_ASSERT(position == 0);
	insertposition = mySourceLayers.size();
	position = insertposition;
        reverselayers = false;
    }
    else if (position < 0 || position > mySourceLayers.size())
    {
        // We were asked to put each layer at the weakest position, rather than
        // at the next strongest position. So we have to reverse the order of
        // the layers passed into us.
        insertposition = 0;
	position = layers.size() - 1;
        reverselayers = true;
    }
    else
    {
	insertposition = mySourceLayers.size() - position;
	position = insertposition;
        reverselayers = false;
    }

    // Release the current write lock.
    afterRelease();

    // If requested, copy the root prim metadata from the first layer being
    // added. We do this after releasing the lock in case the active layer is
    // empty. If we update the root prim metadata before releasing the lock,
    // an empty layer may no longer seem empty because the root prim metadata
    // of the active layer differs from that of the stage's root layer.
    if (root_prim_metadata_layer)
        setStageRootLayerData(root_prim_metadata_layer);

    // Make sure the removal of the active layer didn't make the calculated
    // position value invalid.
    if (!reverselayers && insertposition > mySourceLayers.size())
    {
	insertposition = mySourceLayers.size();
	position = insertposition;
    }

    // Tag the layer with our creator node, if it hasn't been set already.
    // Then disallow further edits of the layer.
    for (auto &&layer : layers)
    {
        std::string		 node_path;

        if (layer.isLopLayer() &&
            !HUSDgetCreatorNode(layer.myLayer, node_path))
            HUSDsetCreatorNode(layer.myLayer, myDataLock->getLockedNodeId());
        // Don't turn off permission to edit for layers from disk. It should
        // already be impossible to access and edit such layers through LOPs,
        // except as part of a USD ROP which _should_ be allowed to edit the
        // layer. SOP layers are protected from edits because they don't
        // support writes, and like files on disk there should be no way to
        // even try to write to them through LOPs.
        if (layer.isLopLayer())
            layer.myLayer->SetPermissionToEdit(false);
    }

    // Add the sublayers to the stack.
    mySourceLayers.multipleInsert(insertposition, layers.size());
    for (auto &&layer : layers)
    {
        mySourceLayers(position) = layer;
        position += reverselayers ? -1 : 1;
    }

    // Advance our active layer to point to this new layer (if we want to be
    // allowed to edit it further, and it is an anonymous layer), or to one
    // layer past this new sublayer. It is up to the caller to decide if it is
    // safe to allow editing this new layer.
    if (add_layer_op == XUSD_ADD_LAYERS_ALL_LOCKED ||
        !mySourceLayers.last().isLopLayer())
	myActiveLayerIndex = mySourceLayers.size();
    else
	myActiveLayerIndex = (mySourceLayers.size() - 1);

    // Re-lock so we can continue editing (in the new layer).
    afterLock(true);

    return true;
}

bool
XUSD_Data::addLayer()
{
    // Can't add a layer to the overrides or post layers.
    UT_ASSERT(myOverridesInfo->isEmpty());
    UT_ASSERT(myPostLayersInfo->isEmpty());
    // We must have a valid locked stage.
    UT_ASSERT(myDataLock->isWriteLocked() && myOwnsActiveLayer);
    UT_ASSERT(isStageValid());

    // Release the current write lock.
    afterRelease();

    // Add a new sublayer to this data. Just advance to the next active layer
    // index. When we lock for writing, we will be editing a fresh new layer.
    myActiveLayerIndex = mySourceLayers.size();

    // Re-lock so we can continue editing (in the new layer).
    afterLock(true);

    return true;
}

bool
XUSD_Data::removeLayers(const std::set<std::string> &filepaths)
{
    // Can't remove a layer from the overrides or post layers.
    UT_ASSERT(myOverridesInfo->isEmpty());
    UT_ASSERT(myPostLayersInfo->isEmpty());
    // We must have a valid locked stage.
    UT_ASSERT(myDataLock->isWriteLocked() && myOwnsActiveLayer);
    UT_ASSERT(isStageValid());

    bool     released_lock = false;

    // Run in reverse because we might be removing entries from mySourceLayers.
    for (int i = mySourceLayers.size(); i --> 0;)
    {
	if (filepaths.find(mySourceLayers(i).myIdentifier) != filepaths.end())
	{
	    // The stage sublayer paths are in strongest to weakest order, so
	    // we have to flip the index value when deciding which stage
	    // sublayer to remove. Do this before releasing the lock in case
	    // the active layer gets removed, which does not affect the stage
	    // sublayers. Note that we don't need to worry about placeholder
            // layers, because they are always the strongest layers. And what
            // we know is that we want to remove the i'th weakest layer. This
            // can be safely assumed to be i spots from the end of the array
            // of layers on the stage.
	    int stagesize = myStage->GetRootLayer()->GetNumSubLayerPaths();
	    int stageidx = stagesize - i - 1;

	    // Release the current write lock.
	    // Note that we don't need to add a layer to the write lock tag.
	    // We are either removing some non-current layer, in which case
	    // we will resume editing the same layer, or we are removing the
	    // current layer, in which case we can safely create a new layer
	    // with the same tag.
            if (!released_lock)
            {
                afterRelease();
                released_lock = true;
            }

	    // If we are being asked to remove the active layer, and that
	    // active layer is empty, calling afterRelease will remove it.
	    // So we don't need to do anything further.
	    if (i < mySourceLayers.size())
	    {
		// Remove the requested layer from our source layers.
		mySourceLayers.removeIndex(i);

		// Remove the corresponding layer from the stage root layer,
		// and our stage layer assignments.
		myStage->GetRootLayer()->RemoveSubLayerPath(stageidx);
		myStageLayerAssignments->removeIndex(i);
		myStageLayers->removeIndex(i);
		(*myStageLayerCount)--;

		// Decrement the active layer index.
		myActiveLayerIndex--;

		// If we are removing the last source layer, and that last
		// source layer was anonymous, our active layer may now be
		// pointing at a layer for an external file, which we aren't
		// allowed to edit. So check for a non-anonymous active layer,
		// and if it is, advance the active layer index so that we'll
		// allocate a new layer next time we lock.
		if (myActiveLayerIndex == mySourceLayers.size() - 1 &&
		    !mySourceLayers(myActiveLayerIndex).isLopLayer())
		    myActiveLayerIndex++;
	    }
	}
    }

    // Re-lock so we can continue editing.
    if (released_lock)
        afterLock(true);

    // Even if we didn't find the layer, that counts as successfully removing
    // it.
    return true;
}

bool
XUSD_Data::replaceAllSourceLayers(const XUSD_LayerAtPathArray &layers,
        const XUSD_LockedGeoArray &locked_geos,
        const XUSD_LayerArray &held_layers,
        const XUSD_LayerArray &replacement_layers,
        const HUSD_LockedStageArray &locked_stages,
        const UT_SharedPtr<XUSD_RootLayerData> &root_layer_data,
        bool last_sublayer_is_editable)
{
    // Can't add a layer to the overrides or post layers.
    UT_ASSERT(myOverridesInfo->isEmpty());
    UT_ASSERT(myPostLayersInfo->isEmpty());
    // We must have a valid locked stage.
    UT_ASSERT(myDataLock->isWriteLocked() && myOwnsActiveLayer);
    UT_ASSERT(isStageValid());

    afterRelease();

    // Tag the layer with our creator node, if it hasn't been set already.
    // Then disallow further edits of the layer.
    for (auto &&layer : layers)
    {
        std::string		 node_path;

        if (layer.isLopLayer() &&
            !HUSDgetCreatorNode(layer.myLayer, node_path))
            HUSDsetCreatorNode(layer.myLayer, myDataLock->getLockedNodeId());
        // Don't turn off permission to edit for layers from disk. It should
        // already be impossible to access and edit such layers through LOPs,
        // except as part of a USD ROP which _should_ be allowed to edit the
        // layer. SOP layers are protected from edits because they don't
        // support writes, and like files on disk there should be no way to
        // even try to write to them through LOPs.
        if (layer.isLopLayer())
            layer.myLayer->SetPermissionToEdit(false);
    }

    mySourceLayers = layers;
    myLockedGeoArray = locked_geos;
    myHeldLayers = held_layers;
    myReplacementLayerArray = replacement_layers;
    myLockedStages = locked_stages;
    myRootLayerData = root_layer_data;

    // Advance our active layer to point to this new layer (if we want to be
    // allowed to edit it further, and it is an anonymous layer), or to one
    // layer past this new sublayer. It is up to the caller to decide if it is
    // safe to allow editing this new layer.
    if (last_sublayer_is_editable &&
        !mySourceLayers.isEmpty() &&
        mySourceLayers.last().isLopLayer())
        myActiveLayerIndex = (mySourceLayers.size() - 1);
    else
        myActiveLayerIndex = mySourceLayers.size();

    afterLock(true);

    return true;
}

bool
XUSD_Data::applyLayerBreak()
{
    // Can't add a layer to the overrides or post layers.
    UT_ASSERT(myOverridesInfo->isEmpty());
    UT_ASSERT(myPostLayersInfo->isEmpty());
    // We must have a valid locked stage.
    UT_ASSERT(myDataLock->isWriteLocked() && myOwnsActiveLayer);
    UT_ASSERT(isStageValid());

    // Release the current write lock.
    afterRelease();

    // Tag all existing layers as being part of a layer break.
    for (auto &&layer : mySourceLayers)
	layer.myRemoveWithLayerBreak = true;

    // Add a new sublayer to this data. Just advance to the next active layer
    // index. When we lock for writing, we will be editing a fresh new layer.
    myActiveLayerIndex = mySourceLayers.size();

    // Re-lock so we can continue editing (in the new layer).
    afterLock(true);

    return true;
}

void
XUSD_Data::addLockedGeo(const XUSD_LockedGeoPtr &lockedgeo)
{
    myLockedGeoArray.append(lockedgeo);
}

void
XUSD_Data::addLockedStage(const HUSD_LockedStagePtr &locked_stage)
{
    myLockedStages.append(locked_stage);
}

void
XUSD_Data::addHeldLayer(const SdfLayerRefPtr &layer)
{
    myHeldLayers.append(layer);
}

void
XUSD_Data::addLockedGeos(const XUSD_LockedGeoArray &lockedgeos)
{
    myLockedGeoArray.concat(lockedgeos);
}

void
XUSD_Data::addLockedStages(const HUSD_LockedStageArray &locked_stages)
{
    myLockedStages.concat(locked_stages);
}

void
XUSD_Data::addHeldLayers(const XUSD_LayerArray &layers)
{
    myHeldLayers.concat(layers);
}

void
XUSD_Data::setStageRootPrimMetadata(const TfToken &field, const VtValue &value)
{
    // We must have a valid locked stage.
    UT_ASSERT(myDataLock->isWriteLocked() && myOwnsActiveLayer);
    UT_ASSERT(isStageValid());

    // If the value is already set, don't bother changing anything.
    if (!myRootLayerData->isMetadataValueSet(field, value))
    {
        if (myRootLayerData.use_count() > 1)
        {
            myRootLayerData
                    = UTmakeShared<XUSD_RootLayerData>(*myRootLayerData);
        }
        myRootLayerData->setMetadataValue(field, value);

        SdfLayerRefPtr layer = myStage->GetRootLayer();
        if (SdfPrimSpecHandle rootspec = layer->GetPseudoRoot())
        {
            if (value.IsEmpty())
                rootspec->ClearField(field);
            else
                rootspec->SetField(field, value);
        }
    }
}

void
XUSD_Data::applyRootLayerDataToStage()
{
    SdfChangeBlock changeblock;

    if (myRootLayerData->toStage(myStage))
    {
        // If there were any changes, we now want to go through and
        // update the root prim metadata on all placeholder layers to
        // match.
        for (auto &&layer : (*myStageLayers))
        {
            if (HUSDisLayerPlaceholder(layer))
            {
                layer->SetPermissionToEdit(true);
                HUSDcopyMinimalRootPrimMetadata(layer, myStage->GetRootLayer());
                layer->SetPermissionToEdit(false);
            }
        }
    }
}

void
XUSD_Data::setStageRootLayerData(
        const UT_SharedPtr<XUSD_RootLayerData> &rootlayerdata)
{
    // We must have a valid locked stage.
    UT_ASSERT(myDataLock->isWriteLocked() && myOwnsActiveLayer);
    UT_ASSERT(isStageValid());

    myRootLayerData = rootlayerdata;
    applyRootLayerDataToStage();
}

void
XUSD_Data::setStageRootLayerData(const SdfLayerRefPtr &layer)
{
    auto data = UTmakeShared<XUSD_RootLayerData>(layer);
    setStageRootLayerData(data);
}

const XUSD_LockedGeoArray &
XUSD_Data::lockedGeos() const
{
    return myLockedGeoArray;
}

void
XUSD_Data::addReplacements(const XUSD_LayerArray &replacements)
{
    myReplacementLayerArray.concat(replacements);
}

const XUSD_LayerArray &
XUSD_Data::replacements() const
{
    return myReplacementLayerArray;
}

const HUSD_LockedStageArray &
XUSD_Data::lockedStages() const
{
    return myLockedStages;
}

const XUSD_LayerArray	&
XUSD_Data::heldLayers() const
{
    return myHeldLayers;
}

bool
XUSD_Data::isStageValid() const
{
    return myStage && myStage->GetPseudoRoot() &&
	myDataLock && myDataLock->isLocked();
}

UsdStageRefPtr
XUSD_Data::stage() const
{
    if (myDataLock && myDataLock->isLocked())
	return myStage;

    UT_ASSERT(!"stage() can only be called on locked data.");
    return HUSDcreateStageInMemory(UsdStage::LoadNone);
}

SdfLayerRefPtr
XUSD_Data::activeLayer() const
{
    if (myDataLock && myDataLock->isLayerLocked())
    {
	UT_ASSERT(myActiveLayerIndex >= 0);
	UT_ASSERT(myActiveLayerIndex < mySourceLayers.size());
	return mySourceLayers(myActiveLayerIndex).myLayer;
    }
    else if (myDataLock && myDataLock->isWriteLocked())
    {
	// If we have a write overrides value set, that's the layer we are
	// editing. Because we have a write overrides applied, we know that
	// the overrides object is locked to this XUSD_Data object, which
	// means that all edits actually need to be applied to this stage's
	// session layers directly. Any changes made will be copied back
	// into the overrides object when we unlock the overrides objects
	// from this XUSD_Data.
	if (myOverridesInfo->myWriteOverrides)
	    return myOverridesInfo->
		mySessionLayers[HUSD_OVERRIDES_CUSTOM_LAYER];

	UT_ASSERT(myActiveLayerIndex >= 0);
	UT_ASSERT(myActiveLayerIndex < *myStageLayerCount);
	return (*myStageLayers)(myActiveLayerIndex);
    }
    else if (myDataLock && myDataLock->isLocked())
    {
	// If we have been read locked, we may not actually have any layers,
	// in which case we return null (but without an assertion).
	if (myActiveLayerIndex >= 0 &&
	    myActiveLayerIndex < *myStageLayerCount)
	    return (*myStageLayers)(myActiveLayerIndex);
	else
	    return SdfLayerRefPtr();
    }
    
    UT_ASSERT(!"activeLayer() can only be called on locked data.");
    return SdfLayerRefPtr();
}

bool
XUSD_Data::activeLayerIsReusable() const
{
    if (myDataLock && myDataLock->isReadLocked())
    {
        if (myActiveLayerIndex >= 0 &&
            myActiveLayerIndex < mySourceLayers.size())
            return true;
        else
            return false;
    }

    UT_ASSERT(!"activeLayerIsReusable() only callable on read locked data.");
    return false;
}

ArResolverContext
XUSD_Data::resolverContext() const
{
    if (myStage)
	return myStage->GetPathResolverContext();

    UT_ASSERT(!"resolverContext() can only be called if we have a stage.");
    return ArGetResolver().CreateDefaultContext();
}

UsdStageRefPtr
XUSD_Data::getOrCreateStageForFlattening(
	HUSD_StripLayerResponse response,
	UsdStage::InitialLoadSet loadset) const
{
    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING);
    if (!myDataLock || !myDataLock->isLocked())
    {
	// This shouldn't happen, but we never want to return null here.
	UT_ASSERT(!"getOrCreateStageForFlattening(): data not locked.");
	return HUSDcreateStageInMemory(UsdStage::LoadNone);
    }

    std::vector<std::string>	 outsublayerpaths;
    SdfLayerOffsetVector	 outsublayeroffsets;
    SdfLayerRefPtrVector         layercopies;
    bool			 requires_new_stage = false;

    // mySourceLayers are in weakest to strongest order, but when we set the
    // sublayer paths on an SdfLayer, they are expected in strongest to
    // weakest order, so go through the layers in reverse. This also causes
    // the loop to easily handle Layer Breaks properly by simply breaking
    // out of the loop.
    for (int i = mySourceLayers.size(); i --> 0;)
    {
	const XUSD_LayerAtPath	&sourcelayer = mySourceLayers(i);

	// If we reach a layer that indicates a layer break, then exit the
	// loop to avoid adding the remaining layers to the locked stage.
	if (sourcelayer.myRemoveWithLayerBreak)
	{
	    // If stripping layers should be an error, and we stripped layers,
	    // return an empty stage. We never want to return null from here.
	    if (HUSDapplyStripLayerResponse(response))
		return HUSDcreateStageInMemory(loadset, myStage);
	    requires_new_stage = true;
	    continue;
	}

        outsublayerpaths.push_back(mySourceLayers(i).myIdentifier);
        outsublayeroffsets.push_back(mySourceLayers(i).myOffset);
    }

    if (requires_new_stage)
    {
	auto	 stage = HUSDcreateStageInMemory(loadset, myStage);

	stage->GetRootLayer()->SetSubLayerPaths(outsublayerpaths);
	for (int i = 0, n = outsublayerpaths.size(); i < n; i++)
	    stage->GetRootLayer()->SetSubLayerOffset(outsublayeroffsets[i], i);

	return stage;
    }

    return myStage;
}

std::set<std::string>
XUSD_Data::getStageLayersToRemoveFromLayerBreak() const
{
    std::set<std::string>	 identifiers;

    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING);
    if (myDataLock && myDataLock->isLocked())
    {
	for (int i = 0, n = mySourceLayers.size(); i < n; i++)
	{
	    if (mySourceLayers(i).myRemoveWithLayerBreak)
	    {
		identifiers.insert((*myStageLayers)(i)->GetIdentifier());
	    }
	}
    }
    else
    {
	// This shouldn't happen, but we never want to return null here.
	UT_ASSERT(!"getStageLayersToRemoveFromLayerBreak(): data not locked.");
    }

    return identifiers;
}

SdfLayerRefPtr
XUSD_Data::createFlattenedLayer(
	HUSD_StripLayerResponse response) const
{
    // Don't need to load payloads. We are just flattening the layers, so we'd
    // stop processing at payloads/references anyway.
    SdfLayerRefPtr flattened = HUSDflattenLayers(getOrCreateStageForFlattening(
	response, UsdStage::LoadNone));
    std::string    savepath;

    // Clear the save control. Mostly we want to ensure this layer is never
    // marked as a Placeholder. But we don't really want to preserve any of
    // the other possible values either. Make an exception if the flattened
    // layer has a save path set. Then we can assume the layer was intended
    // to be saved explicitly.
    if (HUSDgetSavePath(flattened, savepath))
        HUSDsetSaveControl(flattened, HUSD_Constants::getSaveControlExplicit());
    else
        HUSDsetSaveControl(flattened, UT_StringHolder::theEmptyString);

    return flattened;
}

SdfLayerRefPtr
XUSD_Data::createFlattenedStage(
	HUSD_StripLayerResponse response) const
{
    // We must load all payloads, or the stage flattening stops at the prim
    // with the payload.
    SdfLayerRefPtr flattened = getOrCreateStageForFlattening(
	response, UsdStage::LoadAll)->Flatten();
    std::string    savepath;

    // Clear the save control. Mostly we want to ensure this layer is never
    // marked as a Placeholder. But we don't really want to preserve any of
    // the other possible values either. Make an exception if the flattened
    // layer has a save path set. Then we can assume the layer was intended
    // to be saved explicitly.
    if (HUSDgetSavePath(flattened, savepath))
        HUSDsetSaveControl(flattened, HUSD_Constants::getSaveControlExplicit());
    else
        HUSDsetSaveControl(flattened, UT_StringHolder::theEmptyString);

    return flattened;
}

const XUSD_LayerAtPathArray &
XUSD_Data::sourceLayers() const
{
    return mySourceLayers;
}

const HUSD_ConstOverridesPtr &
XUSD_Data::overrides() const
{
    if (myOverridesInfo)
	return myOverridesInfo->myReadOverrides;

    static HUSD_ConstOverridesPtr	 theEmptyPtr;

    return theEmptyPtr;
}

const HUSD_ConstPostLayersPtr &
XUSD_Data::postLayers() const
{
    if (myPostLayersInfo)
        return myPostLayersInfo->myPostLayers;

    static HUSD_ConstPostLayersPtr	 theEmptyPtr;

    return theEmptyPtr;
}

const SdfLayerRefPtr &
XUSD_Data::sessionLayer(HUSD_OverridesLayerId id) const
{
    if (myOverridesInfo)
	return myOverridesInfo->mySessionLayers[id];

    static SdfLayerRefPtr		 theEmptyPtr;

    return theEmptyPtr;
}

const HUSD_LoadMasksPtr &
XUSD_Data::loadMasks() const
{
    return myLoadMasks;
}

const std::string &
XUSD_Data::rootLayerIdentifier() const
{
    static const std::string	 theEmptyString;

    if (myStage && myStage->GetRootLayer())
	return myStage->GetRootLayer()->GetIdentifier();

    return theEmptyString;
}

void
XUSD_Data::afterLock(bool for_write,
	const HUSD_ConstOverridesPtr &read_overrides,
	const HUSD_OverridesPtr &write_overrides,
	const HUSD_ConstPostLayersPtr &postlayers,
	bool remove_layer_breaks)
{
    // Don't do anything in this function if:
    //     1. We have no stage (some kind of error occurred)
    //     2. Cooking is disabled (we don't want to trigger recomposition)
    //     3. We are a wrapper for a USD stage that we shouldn't be modifying
    if (isStageValid() &&
        OPgetDirector()->cookEnabled() &&
        myMirroring != HUSD_EXTERNAL_STAGE)
    {
        const char *msg = "Composing stage for reading from {}";
        int msg_nodeid = myDataLock->getLockedNodeId();
        if (myDataLock->isLayerLocked())
        {
            msg = "Composing stage for layer editing";
            msg_nodeid = OP_INVALID_NODE_ID;
        }
        else if (myDataLock->isWriteLocked())
        {
            msg = "Composing stage for editing";
            msg_nodeid = OP_INVALID_NODE_ID;
        }
        HUSD_PerfMonAutoCookEvent perf(msg, msg_nodeid);

        // All these operations on the stage can be put in a single Sdf Change
        // Block, since they are all Sdf-only operations.
        {
	    HUSD_ConstOverridesPtr	 overrides;
	    SdfChangeBlock               changeblock;

            // We don't support (or at least haven't tested) locking for write
            // with layer breaks removed.
            UT_ASSERT(!(for_write && remove_layer_breaks));

            // If we have been given a different postlayers pointer to fill in
            // our sessions layers, set that up here.
            if (postlayers)
            {
                if (myPostLayersInfo->myPostLayers != postlayers ||
                    myPostLayersInfo->myVersionId != postlayers->versionId())
                {
                    SdfSubLayerProxy sublayers(
                        myStage->GetSessionLayer()->GetSubLayerPaths());

                    // Copy layer contents from the source post layers
                    // into the session layer sublayers reserved for them.
                    for (int i = 0; i < postlayers->layerCount(); i++)
                    {
                        // Create a new session layer sublayer if required.
                        if (i == myPostLayersInfo->mySessionLayers.size())
                        {
                            SdfLayerRefPtr layer = HUSDcreateAnonymousLayer(
                                SdfLayerHandle(),
                                postlayers->layerName(i).toStdString());
                            myPostLayersInfo->mySessionLayers.push_back(layer);
                            sublayers.Insert(HUSD_OVERRIDES_NUM_LAYERS,
                                layer->GetIdentifier());
                        }
                        myPostLayersInfo->mySessionLayers[i]->TransferContent(
                            postlayers->layer(i)->layer());
                    }
                    // Clear any session layer sublayers reserved for post
                    // layers that don't have corresponding post layers.
                    for (int i = postlayers->layerCount();
                         i < myPostLayersInfo->mySessionLayers.size(); i++)
                        myPostLayersInfo->mySessionLayers[i]->Clear();
                    myPostLayersInfo->myVersionId = postlayers->versionId();
                }
            }
            else
            {
                // Clear all the postlayers placeholder layers (which are all
                // sublayers on the session layer).
                for (int i = 0;
                     i < myPostLayersInfo->mySessionLayers.size(); i++)
                    myPostLayersInfo->mySessionLayers[i]->Clear();
                myPostLayersInfo->myVersionId = 0;
            }
            myPostLayersInfo->myPostLayers = postlayers;

            // If we have been given a different overrides pointer to place in
            // our session layer, set that up here. This layer remains as a
            // sublayer of our session layer until we are passed a new value
            // here, which means edits to these overrides layer will be applied
            // immediately, since they are on an open stage.
            if (read_overrides)
                overrides = read_overrides;
            else
                overrides = write_overrides;

            if (overrides)
            {
                if (myOverridesInfo->myReadOverrides != overrides ||
                    myOverridesInfo->myVersionId != overrides->versionId())
                {
                    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
                    {
                        SdfLayerRefPtr layer = overrides->data().
                            layer((HUSD_OverridesLayerId)i);
                        myOverridesInfo->mySessionLayers[i]->
                            TransferContent(layer);
                    }
                    myOverridesInfo->myVersionId = overrides->versionId();
                }
            }
            else if (myOverridesInfo->myReadOverrides)
            {
                for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
                    myOverridesInfo->mySessionLayers[i]->Clear();
                myOverridesInfo->myVersionId = 0;
            }
            myOverridesInfo->myReadOverrides = overrides;
            myOverridesInfo->myWriteOverrides = write_overrides;
            if (myOverridesInfo->myWriteOverrides)
                myOverridesInfo->myWriteOverrides->lockToData(this);

            if (for_write)
            {
                UT_ASSERT(myActiveLayerIndex <= mySourceLayers.size());
                if (myActiveLayerIndex >= mySourceLayers.size())
                {
                    int layer_color_index = getNewLayerColorIndex(
                        mySourceLayers, myDataLock->getLockedNodeId());

                    // We have been asked to create a new layer to edit.
                    mySourceLayers.append(XUSD_LayerAtPath(
                        HUSDcreateAnonymousLayer(myStage->GetRootLayer(),
                            HUSDgetTag(myDataLock))));
                    HUSDsetCreatorNode(mySourceLayers.last().myLayer,
                        myDataLock->getLockedNodeId());
                    mySourceLayers.last().myLayer->SetPermissionToEdit(false);
                    mySourceLayers.last().myLayerColorIndex = layer_color_index;
                    myOwnsActiveLayer = true;
                }
            }

            int new_placeholder_count = 0;
            int placeholder_increment =
                UT_EnvControl::getInt(ENV_HOUDINI_LOP_PLACEHOLDER_LAYERS);

	    // Remove sublayers from the root layer until we are only left with
            // the ones that have corresponding source layers. For LOP layers,
            // we don't actually remove them, we just clear them and mark them
            // as "placeholders" that we can reuse later.
	    while (mySourceLayers.size() < *myStageLayerCount)
	    {
		(*myStageLayerCount)--;
		if (HUSDisLopLayer((*myStageLayers)[*myStageLayerCount]))
		{
		    (*myStageLayerAssignments)[*myStageLayerCount].clear();
                    (*myStageLayers)[*myStageLayerCount]->
                        SetPermissionToEdit(true);
                    // Copy the stage's root prim metadata to the
                    // placeholder to prevent pointless expensive edits.
                    (*myStageLayers)[*myStageLayerCount]->TransferContent(
                        HUSDcreateAnonymousLayer(myStage->GetRootLayer()));
		    HUSDsetSaveControl((*myStageLayers)[*myStageLayerCount],
			HUSD_Constants::getSaveControlPlaceholder());
                    (*myStageLayers)[*myStageLayerCount]->
                        SetPermissionToEdit(false);
		}
		else
		{
		    myStageLayerAssignments->removeIndex(*myStageLayerCount);
		    myStageLayers->removeIndex(*myStageLayerCount);
		    myStage->GetRootLayer()->RemoveSubLayerPath(
			(myStage->GetRootLayer()->GetNumSubLayerPaths() - 1) -
			*myStageLayerCount);
                    // Add a placeholder to replace this layer from disk.
                    // Otherwise adding then removing a disk layer "eats away"
                    // at the available list of placeholder layers.
                    new_placeholder_count++;
		}
	    }

	    // Transfer content from source layers to stage layers if they
	    // don't already match (according to myStageLayerAssignments). Make
	    // new stage layers if we have more source layers than stage
	    // layers.
	    SdfSubLayerProxy	 sublayers(
		myStage->GetRootLayer()->GetSubLayerPaths());
            SdfLayerOffsetVector offsets;
	    int			 sublayeridx = sublayers.size()-1;

	    // There should be a one to one (but reversed) mapping of
	    // the myStageLayers array and the sublayers on the stage's
	    // root layer.
            offsets.resize(sublayers.size());
	    UT_ASSERT(sublayers.size() == myStageLayers->size());
	    UT_ASSERT(myStageLayerAssignments->size() == myStageLayers->size());
	    for (int i = 0; i < mySourceLayers.size(); i++)
	    {
		const XUSD_LayerAtPath	&src = mySourceLayers(i);
		SdfLayerRefPtr		 layer = src.myLayer;
		std::string		 identifier = src.myIdentifier;

		// If we have been asked to remove "layer break" layers from
		// the stage as we lock it, replace such source layers with
		// empty layers marked as "placeholders" so they will be
		// ignored or stripped out by any save operation.
		if (src.myRemoveWithLayerBreak && remove_layer_breaks)
		{
                    // Copy the stage's root prim metadata to the
                    // placeholder to prevent pointless expensive edits.
		    layer = HUSDcreateAnonymousLayer(myStage->GetRootLayer());
		    HUSDsetSaveControl(layer,
			HUSD_Constants::getSaveControlPlaceholder());
		    layer->SetPermissionToEdit(false);
		    identifier = layer->GetIdentifier();
		}

		if (i >= myStageLayerAssignments->size())
		{
		    myStageLayerAssignments->append(identifier);
		    if (src.isLopLayer())
		    {
			// The source layer is one we want to copy.
			myStageLayers->append(HUSDcreateAnonymousLayer());
			myStageLayers->last()->TransferContent(layer);
			myStageLayers->last()->SetPermissionToEdit(false);
			sublayers.insert(sublayers.begin(),
			    myStageLayers->last()->GetIdentifier());
		    }
		    else
		    {
			// The source layer is a file on disk,
			// so we can just point directly to the
			// source layer.
			myStageLayers->append(layer);
			sublayers.insert(sublayers.begin(), identifier);
		    }
                    offsets.insert(offsets.begin(), src.myOffset);

                    // As long as we're adding new layers, add a few extra.
                    // But we don't want to increment by that number for each
                    // additional layer we are adding this time through, so
                    // set the increment value to zero.
                    new_placeholder_count += placeholder_increment;
                    placeholder_increment = 0;

		    // myStageLayerCount should always be less than or equal
		    // to myStageLayers->size(). But if we are growing
		    // myStageLayers, they should be equal.
		    (*myStageLayerCount)++;
		    UT_ASSERT(myStageLayers->size() == *myStageLayerCount);

		    // We should be at the front of the root layer's sub-layer
		    // list at this point.
		    UT_ASSERT(sublayeridx == -1);
		}
		else
		{
		    UT_ASSERT(sublayeridx >= 0);
		    if ((*myStageLayerAssignments)(i) != identifier)
		    {
			SdfLayerRefPtr	&dest = (*myStageLayers)(i);

			if (HUSDisLopLayer(dest) && src.isLopLayer())
			{
                            // The dest layer is anonymous, and the source
			    // layer is one we want to copy, so copy over
			    // whatever is there now.
			    dest->SetPermissionToEdit(true);
                            dest->TransferContent(layer);
			    dest->SetPermissionToEdit(false);
			}
			else
			{
			    if (src.isLopLayer())
			    {
				// The dest layer is not one we cannot write
				// to, but the source layer is one we want to
				// copy. So make a new layer and copy to it.
				dest = HUSDcreateAnonymousLayer();
                                dest->TransferContent(layer);
				dest->SetPermissionToEdit(false);
			    }
			    else
			    {
				// The source layer is a file on disk,
				// so we can just point directly to the
				// source layer.
				dest = layer;
			    }

			    // It is illegal to set the same sublayer path
			    // more than once in a sub layer paths array.
			    // If you try, USD just rejects the request. So
			    // before setting any path as a sublayer, look at
			    // the existing set of paths for a duplicate. We
			    // only need to check further along the array
			    // because we can assume that the source layers
			    // don't have any duplicates.
			    for (int dupidx = sublayeridx, j = i + 1;
				 dupidx --> 0;
				 j++)
			    {
				if (sublayers[dupidx] == identifier ||
				    sublayers[dupidx] == dest->GetIdentifier())
				{
				    // We found a duplicate. Set it to some
				    // value that we can be sure won't conflict
				    // with any real or subsequent dummy layer
				    // names. We will be resetting this path
				    // shortly anyway as we proceed through the
				    // rest of this loop over mySourceLayers.
				    UT_String    p;
				    p.sprintf("__dummy__%d__.usd", j);
				    sublayers[dupidx] = p.toStdString();
				    (*myStageLayerAssignments)(j) = p;
				    break;
				}
			    }
			    if (src.isLopLayer())
				sublayers[sublayeridx] = dest->GetIdentifier();
			    else
				sublayers[sublayeridx] = identifier;
			}
			(*myStageLayerAssignments)(i) = identifier;
		    }
                    offsets[sublayeridx] = src.myOffset;
		    if (i >= *myStageLayerCount)
			(*myStageLayerCount)++;
		    sublayeridx--;
		}
	    }

            // Set the layer offset values of all layers on the stage (if they
            // differ from the current values).
            for (int i = 0, n = offsets.size(); i < n; i++)
            {
                if (myStage->GetRootLayer()->GetSubLayerOffset(i) != offsets[i])
                    myStage->GetRootLayer()->SetSubLayerOffset(offsets[i], i);
            }

            // Update the root layer's root prim metadata.
            applyRootLayerDataToStage();

            // Append extra place holder layers if requested.
            for (int k = 0; k < new_placeholder_count; k++)
            {
                myStageLayerAssignments->append();
                // Copy the stage's root prim metadata to the
                // placeholder to prevent pointless expensive edits.
                myStageLayers->append(HUSDcreateAnonymousLayer(
                    myStage->GetRootLayer()));
                HUSDsetSaveControl(myStageLayers->last(),
                    HUSD_Constants::getSaveControlPlaceholder());
                myStageLayers->last()->SetPermissionToEdit(false);
                sublayers.insert(sublayers.begin(),
                    myStageLayers->last()->GetIdentifier());
                offsets.insert(offsets.begin(), SdfLayerOffset());
            }

            // End of the SdfChangeBlock.
	}

	if (for_write)
	{
	    // If this is the first time we are editing the active layer for
	    // this data, and the active layer isn't brand new, allocate a new
	    // source layer. Leave it empty for now. It will get filled by
	    // copying the stage layer once we release the write lock.
	    if (!myOwnsActiveLayer)
	    {
		int layer_color_index = getExistingLayerColorIndex(
		    mySourceLayers, myDataLock->getLockedNodeId());

		mySourceLayers(myActiveLayerIndex) = XUSD_LayerAtPath(
		    HUSDcreateAnonymousLayer(myStage->GetRootLayer(),
                        HUSDgetTag(myDataLock)));
		mySourceLayers(myActiveLayerIndex).myLayer->
		    SetPermissionToEdit(false);
		mySourceLayers(myActiveLayerIndex).myLayerColorIndex =
		    layer_color_index;
	    }
	    myOwnsActiveLayer = true;

	    // Allow editing of the active layer, and set it as the stage's
	    // edit target.
            UT_ASSERT(HUSDisLopLayer(activeLayer()));
	    activeLayer()->SetPermissionToEdit(true);
	    myStage->SetEditTarget(activeLayer());
	}
	else if (myOverridesInfo->myWriteOverrides)
	{
	    // We don't need to set edit permission on the active layer if
	    // it is part of myOverridesInfo->myWriteOverrides, since the
	    // overrides are always writable.
	    myStage->SetEditTarget(activeLayer());
	}
	else if(myMirroring == HUSD_FOR_MIRRORING)
	{
            // We never need to worry about load rules changing for
            // non-mirrored data because any changes to the load rules
            // for regular (LOP node) stages results in a new stage
            // being created from scratch with the new rules put in
            // place before adding any content to the stage.
	    if (myMirrorLoadRulesChanged)
	    {
                // We only need to do anything if the load rules changed
                // since our last stage composition.
                SdfPathSet   loadpaths;
                SdfPathSet   unloadpaths;
                const auto  &current_rules = myStage->GetLoadRules();

                for (auto &&rule : myMirrorLoadRules.GetRules())
                {
                    if (rule.second != UsdStageLoadRules::NoneRule)
                    {
                        if (!current_rules.
                            IsLoadedWithAllDescendants(rule.first))
                            loadpaths.insert(rule.first);
                    }
                }
                for (auto &&rule : current_rules.GetRules())
                {
                    if (rule.second != UsdStageLoadRules::NoneRule)
                    {
                        if (!myMirrorLoadRules.
                            IsLoadedWithAllDescendants(rule.first))
                            unloadpaths.insert(rule.first);
                    }
                }

		myStage->LoadAndUnload(loadpaths, unloadpaths);
                myStage->SetLoadRules(myMirrorLoadRules);
		myMirrorLoadRulesChanged = false;
	    }
	}
    }
}

XUSD_LayerPtr
XUSD_Data::editActiveSourceLayer(bool create_change_block)
{
    UT_ASSERT(myActiveLayerIndex <= mySourceLayers.size());
    if (myActiveLayerIndex >= mySourceLayers.size())
    {
	// We have been asked to create a new layer to edit.
	int layer_color_index = getNewLayerColorIndex(
	    mySourceLayers, myDataLock->getLockedNodeId());
	mySourceLayers.append(XUSD_LayerAtPath(
	    HUSDcreateAnonymousLayer(myStage->GetRootLayer(),
                HUSDgetTag(myDataLock))));
	HUSDsetCreatorNode(mySourceLayers.last().myLayer,
	    myDataLock->getLockedNodeId());
	mySourceLayers(myActiveLayerIndex).myLayerColorIndex =
            layer_color_index;
    }
    else
    {
        HUSD_PerfMonAutoCookEvent perf("Copying active layer for editing");

	// We have been asked to edit an existing layer. We can't actually
	// edit this layer directly, as we likely have copied the source
	// layers from our input node. So we need to make a copy of the
	// input's source layer, and edit that copy.
	int layer_color_index = getExistingLayerColorIndex(
	    mySourceLayers, myDataLock->getLockedNodeId());
	SdfLayerRefPtr inlayer = mySourceLayers(myActiveLayerIndex).myLayer;
	mySourceLayers(myActiveLayerIndex) = XUSD_LayerAtPath(
	    HUSDcreateAnonymousLayer(myStage->GetRootLayer(),
                HUSDgetTag(myDataLock)));
	mySourceLayers(myActiveLayerIndex).myLayer->TransferContent(inlayer);
	mySourceLayers(myActiveLayerIndex).myLayerColorIndex =
            layer_color_index;
    }

    HUSDaddEditorNode(mySourceLayers(myActiveLayerIndex).myLayer,
	myDataLock->getLockedNodeId());

    return new XUSD_Layer(mySourceLayers(myActiveLayerIndex).myLayer,
        create_change_block);
}

void
XUSD_Data::afterRelease()
{
    if (myOverridesInfo &&
	myOverridesInfo->myWriteOverrides)
    {
	// If we were locked to an HUSD_Overrides, we need to unlock here.
	// This will also copy the data from our session layers onto the
	// overrides object. Also match our overrides version id to the
	// latest version id of the overrides object. We know they match
	// because they were ade equal during the unlock operation.
	myOverridesInfo->myWriteOverrides->unlockFromData(this);
	myOverridesInfo->myVersionId =
	    myOverridesInfo->myWriteOverrides->versionId();
    }
    else if (myDataLock &&
	     myDataLock->isWriteLocked() &&
	     myOwnsActiveLayer &&
	     isStageValid())
    {
	// Stash the newly modified active layer into our stashed layer
	// array. The sub layer assignment will still (accurately) claim
	// that the active sub layer is equal to the stashed layer.
	//
	// Note that we don't do this if myOverridesInfo->myWriteOverrides is
	// set, because that means we were editing an overrides layer, not any
	// of our source or stage layers, so there is nothing to preserve here.
	if (!HUSDisLayerEmpty(activeLayer(), myStage))
	{
            HUSD_PerfMonAutoCookEvent perf("Stashing active layer after edit");

	    HUSDaddEditorNode(activeLayer(), myDataLock->getLockedNodeId());
	    mySourceLayers(myActiveLayerIndex).myLayer->
		SetPermissionToEdit(true);
	    mySourceLayers(myActiveLayerIndex).myLayer->
		TransferContent(activeLayer());
	    mySourceLayers(myActiveLayerIndex).myLayer->
		SetPermissionToEdit(false);
	    (*myStageLayerAssignments)(myActiveLayerIndex) =
		mySourceLayers(myActiveLayerIndex).myLayer->GetIdentifier();
	}
	else
	{
	    mySourceLayers.removeLast();
	    (*myStageLayerAssignments)(myActiveLayerIndex).clear();
	}
	activeLayer()->SetPermissionToEdit(false);
    }
    else if (myDataLock &&
	     myDataLock->isLayerLocked())
    {
	// We were editing the source layer directly. Now that we're done,
	// just make it read-only again, and clear out the stage layer
	// assignment because all we know for sure is that it does not
	// equal the source layer any more.
        SdfLayerRefPtr layer = mySourceLayers(myActiveLayerIndex).myLayer;
	if (!HUSDisLayerEmpty(layer, myStage))
	    layer->SetPermissionToEdit(false);
	else
	    mySourceLayers.removeLast();
	if (myActiveLayerIndex < *myStageLayerCount)
	    (*myStageLayerAssignments)(myActiveLayerIndex).clear();
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

