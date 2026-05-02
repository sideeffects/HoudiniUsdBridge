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

#pragma once

#include "HUSD_API.h"

#include <pxr/pxr.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(XUSD_HdGpMutingSceneIndex);

/// \class XUSD_HdGpMutingSceneIndex
///
/// Solaris scene index plugin that conditionally mutes unresolved
/// Hydra Generative Procedurals, by changing their primType.
/// 
/// This is similar to HdGpGenerativeProceduralFilteringSceneIndex but
/// provides an API for (de)activation and keeps a cache of known procedural
/// paths for efficiency.
///
class HUSD_API XUSD_HdGpMutingSceneIndex final
    : public HdSingleInputFilteringSceneIndexBase
{
public:
    static
    XUSD_HdGpMutingSceneIndexRefPtr
    New(HdSceneIndexBaseRefPtr const &inputSceneIndex);
    
    // Unique external API
    void setActive(bool active);

    // HdFilteringSceneIndexBase overrides
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;
    
protected:
    void _PrimsAdded(
            const HdSceneIndexBase &sender,
            const HdSceneIndexObserver::AddedPrimEntries &entries) override;
    void _PrimsRemoved(
            const HdSceneIndexBase &sender,
            const HdSceneIndexObserver::RemovedPrimEntries &entries) override;
    void _PrimsDirtied(
            const HdSceneIndexBase &sender,
            const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;

private:
    XUSD_HdGpMutingSceneIndex(const HdSceneIndexBaseRefPtr &scene);

    SdfPathSet myKnownProceduralPaths;
    bool myActive;
};

PXR_NAMESPACE_CLOSE_SCOPE
