/*
 * Copyright 2025 Side Effects Software Inc.
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
 */

#include "XUSD_HdGpMutingSceneIndex.h"

#include <pxr/imaging/hdGp/generativeProcedural.h>

PXR_NAMESPACE_USING_DIRECTIVE

static const TfToken
    theHdGpToken = HdGpGenerativeProceduralTokens->generativeProcedural,
    theSkippedToken = HdGpGenerativeProceduralTokens->skippedGenerativeProcedural;

/* static */
XUSD_HdGpMutingSceneIndexRefPtr
XUSD_HdGpMutingSceneIndex::New(
        const HdSceneIndexBaseRefPtr& input_scene_index)
{
    return TfCreateRefPtr(
            new XUSD_HdGpMutingSceneIndex(input_scene_index));
}

XUSD_HdGpMutingSceneIndex::XUSD_HdGpMutingSceneIndex(
        const HdSceneIndexBaseRefPtr &scene)
    : HdSingleInputFilteringSceneIndexBase(scene)
    , myActive(true)
{
}

void
XUSD_HdGpMutingSceneIndex::setActive(bool active)
{
    // Early-out if there's no change
    if (active == myActive)
        return;
    
    myActive = active;

    // Remove all the known procedurals, and re-add them (and their children)
    // using the data from the observed scene
    HdSceneIndexObserver::RemovedPrimEntries removed;
    HdSceneIndexObserver::AddedPrimEntries added;
    HdSceneIndexBaseConstRefPtr inputScene = _GetInputSceneIndex();
    auto recursiveAddPath = [inputScene, active, &added](
        const SdfPath &path,
        auto &&recurseFn) -> void
    {
        HdSceneIndexPrim prim = inputScene->GetPrim(path);
        if (active && prim.primType == theHdGpToken)
            prim.primType = theSkippedToken;
        added.emplace_back(path, prim.primType);
        for (SdfPath childPath : inputScene->GetChildPrimPaths(path))
            recurseFn(childPath, recurseFn);
        
    };
    for (auto &&procPath : myKnownProceduralPaths)
    {
        if (HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(procPath);
            prim && prim.primType == theHdGpToken)
        {
            removed.emplace_back(HdSceneIndexObserver::RemovedPrimEntry(procPath));
            recursiveAddPath(procPath, recursiveAddPath);
        }
        else
        {
            // We really shouldn't be here ... somehow we've missed an update
            // (_PrimsAdded and _PrimsRemoved should have caught this)
            TF_WARN("%s no longer exists, or has the wrong type (expected %s)",
                procPath.GetAsString().c_str(), theHdGpToken.GetText());
            myKnownProceduralPaths.erase(procPath);
        }
    }
    
    if (!removed.empty())
        _SendPrimsRemoved(removed);
    if (!added.empty())
        _SendPrimsAdded(added);
}

HdSceneIndexPrim
XUSD_HdGpMutingSceneIndex::GetPrim(const SdfPath &primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (myActive && prim.primType == theHdGpToken)
        prim.primType = theSkippedToken;
    return prim;
}

SdfPathVector
XUSD_HdGpMutingSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
XUSD_HdGpMutingSceneIndex::_PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    HdSceneIndexObserver::AddedPrimEntries modifiedEntries(entries);
    for (auto &&entry : modifiedEntries)
    {
        if (entry.primType == theHdGpToken)
        {
            myKnownProceduralPaths.insert(entry.primPath);
            if (myActive)
                entry.primType = theSkippedToken;
        }
        else
        {
            // It's possible a procedural prim has been retyped,
            // in which case we should stop tracking it.
            myKnownProceduralPaths.erase(entry.primPath);
        }
    }
    _SendPrimsAdded(modifiedEntries);
}

void
XUSD_HdGpMutingSceneIndex::_PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    // Forget about any procedurals which have been removed from the scene
    // (this includes removal via the removal of a parent prim)
    for (auto &&entry : entries)
    {
        // We're not necessarily removing a procedural prim, but possibly a
        // whole hierarchy that contains one or more procedural prims, so we
        // need to identify the entire matching range and remove them all.
        SdfPathSet::iterator first =
            myKnownProceduralPaths.lower_bound(entry.primPath); 
        SdfPathSet::iterator last = first;
        SdfPathSet::iterator end = myKnownProceduralPaths.end();
        while (last != end && last->HasPrefix(entry.primPath))
            ++last;
        myKnownProceduralPaths.erase(first, last);
    }
    _SendPrimsRemoved(entries);
}
void
XUSD_HdGpMutingSceneIndex::_PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    _SendPrimsDirtied(entries);
}
