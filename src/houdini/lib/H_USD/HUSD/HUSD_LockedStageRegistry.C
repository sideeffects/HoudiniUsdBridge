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

#include "HUSD_LockedStageRegistry.h"
#include "HUSD_ErrorScope.h"
#include <gusd/GU_PackedUSD.h>
#include <gusd/stageCache.h>
#include <OP/OP_Node.h>
#include <UT/UT_Lock.h>
#include <UT/UT_Set.h>

PXR_NAMESPACE_USING_DIRECTIVE

typedef UT_Set<const GusdGU_PackedUSD *> PackedUSDSet;
typedef std::pair<HUSD_LockedStagePtr, PackedUSDSet> LockedStageHolder;
static UT_StringMap<LockedStageHolder> thePackedUSDRegistry;
static UT_Lock thePackedUSDRegistryLock;

void
HUSD_LockedStageRegistry::packedUSDTracker(const GU_PackedImpl *prim,
        bool create)
{
    const GusdGU_PackedUSD *packedusd = (const GusdGU_PackedUSD *)prim;
    OP_Node *lop = nullptr;
    bool strip_layers = false;
    fpreal t = 0.0;

    UT_AutoLock lockscope(thePackedUSDRegistryLock);
    auto regit = thePackedUSDRegistry.find(packedusd->fileName());

    if (create)
    {
        // If we don't have a holder for the locked stage used by this prim,
        // create it now, and set the locked stage pointer inside the holder.
        if (regit == thePackedUSDRegistry.end() &&
            GusdStageCache::SplitLopStageIdentifier(packedusd->fileName(),
                lop, strip_layers, t))
        {
            int      nodeid = lop->getUniqueId();
            auto     mapit = getInstance().myLockedStageMaps.find(nodeid);

            // The Locked Stage for this LOP should always have been created by
            // the time we try to register the prim.
            UT_ASSERT(mapit != getInstance().myLockedStageMaps.end());
            if (mapit != getInstance().myLockedStageMaps.end())
            {
                // Use CreateLopStageIdentifier to generate a string that has
                // the same properties in terms of generating time equality as
                // the GusdStageCache does (which means printing the time with
                // a fixed number of significant digits).
                UT_StringHolder          locked_stage_id =
                    GusdStageCache::CreateLopStageIdentifier(
                        nullptr, strip_layers, t);
                auto              ptrit = mapit->second.find(locked_stage_id);

                if (ptrit != mapit->second.end())
                {
                    HUSD_LockedStagePtr      ptr = ptrit->second.lock();

                    thePackedUSDRegistry[packedusd->fileName()].first = ptr;
                    regit = thePackedUSDRegistry.find(packedusd->fileName());
                }
            }
        }

        if (regit != thePackedUSDRegistry.end())
        {
            LockedStageHolder &holder = regit->second;
            UT_ASSERT(holder.first);
            holder.second.insert(packedusd);
        }
    }
    else
    {
        // If we don't have an entry for this file name, it is probably not
        // a LOP locked stage. We don't need to do anything for regular USD
        // files.
        if (regit != thePackedUSDRegistry.end())
        {
            LockedStageHolder &holder = regit->second;

            // Remove this prim from the set of prims that uses this locked
            // stage. If this is the last USD packed prim pointing to this
            // locked stage pointer, remove this stage from the registry.
            UT_ASSERT(holder.first);
            holder.second.erase(packedusd);
            if (holder.second.empty())
                thePackedUSDRegistry.erase(regit);
        }
    }
}

void
HUSD_LockedStageRegistry::exitCallback(void *)
{
    {
        GusdStageCacheWriter cache;

        cache.Clear();
    }

    thePackedUSDRegistry.clear();
}

HUSD_LockedStageRegistry::HUSD_LockedStageRegistry()
{
}

HUSD_LockedStageRegistry::~HUSD_LockedStageRegistry()
{
}

HUSD_LockedStageRegistry &
HUSD_LockedStageRegistry::getInstance()
{
    static HUSD_LockedStageRegistry	 theRegistry;

    return theRegistry;
}

HUSD_LockedStagePtr
HUSD_LockedStageRegistry::getLockedStage(int nodeid,
	const HUSD_DataHandle &data,
	bool strip_layers,
        fpreal t,
	HUSD_StripLayerResponse response)
{
    // Use CreateLopStageIdentifier to generate a string that has
    // the same properties in terms of generating time equality as
    // the GusdStageCache does (which means printing the time with
    // a fixed number of significant digits).
    UT_StringHolder          locked_stage_id =
        GusdStageCache::CreateLopStageIdentifier(
            nullptr, strip_layers, t);
    LockedStageMap          &locked_stage_map = myLockedStageMaps[nodeid];
    HUSD_LockedStageWeakPtr  weakptr = locked_stage_map[locked_stage_id];
    HUSD_LockedStagePtr      ptr = weakptr.lock();

    if (!ptr)
    {
	ptr.reset(new HUSD_LockedStage(data, nodeid, strip_layers, t));
	if (ptr->isValid())
	    locked_stage_map[locked_stage_id] = ptr;
    }

    // If creating this locked stage involved stripping layers, and we have
    // been asked to provide a warning in this case, add the warning.
    if (strip_layers && ptr->strippedLayers())
	HUSDapplyStripLayerResponse(response);

    return ptr;
}

void
HUSD_LockedStageRegistry::clearLockedStage(int nodeid)
{
    auto it = myLockedStageMaps.find(nodeid);

    // Delete all locked stages for this node, regardless of the time or
    // strip_layers value.
    if (it != myLockedStageMaps.end())
    {
        OP_Node         *node = OP_Node::lookupNode(nodeid);

        myLockedStageMaps.erase(it);
        if (node)
        {
            UT_WorkBuffer registry_prefix;
            registry_prefix.sprintf("op:%s?", node->getFullPath().c_str());

            // Delete all occurrences of locked stages for this node from the
            // registry of USD packed primitives. This method should only be
            // called when any such packed prims will be invalidated anyway
            // (such as when the sourcce LOP node is deleted or changed in a
            // way that will require a recook).
            UT_AutoLock lockscope(thePackedUSDRegistryLock);
            for (auto it = thePackedUSDRegistry.begin();
                      it != thePackedUSDRegistry.end(); )
            {
                if (it->first.startsWith(registry_prefix.buffer()))
                    it = thePackedUSDRegistry.erase(it);
                else
                    ++it;
            }
        }
    }
}

