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
#include <OP/OP_Context.h>
#include <OP/OP_Node.h>
#include <UT/UT_ArraySet.h>
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
    UT_Options opts;

    UT_AutoLock lockscope(thePackedUSDRegistryLock);
    auto regit = thePackedUSDRegistry.find(packedusd->fileName());

    if (create)
    {
        // A new USD packed primitive is being created.
        // If we don't have a holder for the locked stage used by this prim,
        // create it now, and set the locked stage pointer inside the holder.
        if (regit == thePackedUSDRegistry.end() &&
            GusdStageCache::SplitLopStageIdentifier(packedusd->fileName(),
                lop, strip_layers, t, opts))
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
                        nullptr, strip_layers, t, opts);
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
        // A USD packed primitive is being deleted.
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
HUSD_LockedStageRegistry::getLockedStage(OP_Node *node,
	const HUSD_DataHandle &data,
	bool strip_layers,
        fpreal t,
	HUSD_StripLayerResponse response)
{
    int nodeid = node->getUniqueId();
    auto optshandle = node->dataMicroNode().getLastUsedContextOptions();
    UT_Options opts;

    if (!optshandle.isNull())
        optshandle->cloneOptionsInto(opts);

    // Use CreateLopStageIdentifier to generate a string that has
    // the same properties in terms of generating time equality as
    // the GusdStageCache does (which means printing the time with
    // a fixed number of significant digits).
    UT_StringHolder          locked_stage_id =
        GusdStageCache::CreateLopStageIdentifier(
            nullptr, strip_layers, t, opts);
    LockedStageMap          &locked_stage_map = myLockedStageMaps[nodeid];
    HUSD_LockedStageWeakPtr  weakptr = locked_stage_map[locked_stage_id];
    HUSD_LockedStagePtr      ptr = weakptr.lock();

    if (!ptr)
    {
	ptr.reset(new HUSD_LockedStage(data, nodeid, strip_layers, t, opts));
	if (ptr->isValid())
	    locked_stage_map[locked_stage_id] = ptr;
    }

    // If creating this locked stage involved stripping layers, and we have
    // been asked to provide a warning in this case, add the warning.
    if (strip_layers && ptr->strippedLayers())
	HUSDapplyStripLayerResponse(response);

    return ptr;
}

static void
husdRemoveFromPackedUSDRegistry(
        const UT_ArraySet<HUSD_LockedStagePtr> &to_remove)
{
    if (to_remove.empty())
        return;

    UT_AutoLock lockscope(thePackedUSDRegistryLock);

    for (auto it = thePackedUSDRegistry.begin();
         it != thePackedUSDRegistry.end();)
    {
        const HUSD_LockedStagePtr &locked_stage = it->second.first;
        if (to_remove.contains(locked_stage))
            it = thePackedUSDRegistry.erase(it);
        else
            ++it;
    }
}

void
HUSD_LockedStageRegistry::clearLockedStage(OP_Node *node,
        const OP_Context &context)
{
    int nodeid = node->getUniqueId();
    auto it = myLockedStageMaps.find(nodeid);

    // Delete all locked stages for this node, regardless of the time or
    // strip_layers value.
    if (it != myLockedStageMaps.end())
    {
        UT_Options opts;
        UT_StringHolder  stripped_locked_stage_id =
            GusdStageCache::CreateLopStageIdentifier(
                nullptr, true, context.getTime(), opts);
        UT_StringHolder  unstripped_locked_stage_id =
            GusdStageCache::CreateLopStageIdentifier(
                nullptr, false, context.getTime(), opts);

        UT_ArraySet<HUSD_LockedStagePtr> stages_to_remove;
        OP_Node     *key_lop = nullptr;
        fpreal       key_t = 0.0;
        UT_Options   key_opts;
        bool         key_strip_layers = false;

        for (auto stageit = it->second.begin(); stageit != it->second.end();)
        {
            auto    &key = stageit->first;
            auto    &stage_weak_ptr = stageit->second;
            bool     matched = false;

            if (key.startsWith(stripped_locked_stage_id) ||
                key.startsWith(unstripped_locked_stage_id))
            {
                if (opts.getNumOptions() !=
                    context.getContextOptions()->getNumOptions())
                    context.getContextOptions()->cloneOptionsInto(opts);
                GusdStageCache::SplitLopStageIdentifier(key,
                    key_lop, key_strip_layers, key_t, key_opts);

                // In the absence of context options on the key, this locked
                // stage matches the node and context.
                matched = true;
                if (key_opts.getNumOptions() > 0)
                {
                    // If there are options in the key, we match only if the
                    // supplied context has all the same option values. The
                    // context can also have additional options, and this
                    // still counts as a match.
                    for (auto optit = key_opts.begin(); !optit.atEnd(); ++optit)
                    {
                        const UT_OptionEntry *opt_entry =
                            opts.getOptionEntry(optit.name());
                        if (!opt_entry || !opt_entry->isEqual(*optit.entry()))
                        {
                            matched = false;
                            break;
                        }
                    }
                }
            }
            if (matched)
            {
                // If we match the node and time and context options from
                // the context, we want to eliminate this locked stage.
                if (auto stage_ptr = stage_weak_ptr.lock())
                    stages_to_remove.insert(stage_ptr);
                stageit = it->second.erase(stageit);
            }
            else
                ++stageit;
        }

        // Final cleanup.
        if (it->second.empty())
            myLockedStageMaps.erase(it);
        husdRemoveFromPackedUSDRegistry(stages_to_remove);
    }
}

void
HUSD_LockedStageRegistry::clearLockedStage(OP_Node *node)
{
    int nodeid = node->getUniqueId();
    auto it = myLockedStageMaps.find(nodeid);

    // Delete all locked stages for this node, at this time, regardless of the
    // strip_layers value.
    if (it != myLockedStageMaps.end())
    {
        LockedStageMap &locked_stage_map = it->second;

        // Delete all occurrences of locked stages for this node from the
        // registry of USD packed primitives. This method should only be
        // called when any such packed prims will be invalidated anyway
        // (such as when the sourcce LOP node is deleted or changed in a
        // way that will require a recook).
        UT_ArraySet<HUSD_LockedStagePtr> locked_stages;
        for (auto &&[key, stage_weak_ptr] : locked_stage_map)
        {
            if (auto stage_ptr = stage_weak_ptr.lock())
                locked_stages.insert(stage_ptr);
        }

        husdRemoveFromPackedUSDRegistry(locked_stages);

        myLockedStageMaps.erase(it);
    }
}

