/*
 * Copyright 2026 Side Effects Software Inc.
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

#include "XUSD_ApexScene.h"

#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_Map.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_UniquePtr.h>

#include <SYS/SYS_Types.h>

#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/pathTable.h>

class HUSD_HydraApexSceneEvaluator;
using HUSD_HydraApexSceneEvaluatorConstPtr
        = UT_IntrusivePtr<const HUSD_HydraApexSceneEvaluator>;

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneIndexBase;
class XUSD_ApexSceneValidator;

/// Tracks the container data source overlay for a prim affected by the APEX,
/// along with the locators dirtied by the overlay.
struct XUSD_ApexPrimOverlayInfo
{
    HdContainerDataSourceHandle myDataSource;
    HdDataSourceLocatorSet myDirtyLocators;
};

/// Utility for tracking dirtied prims while APEX scenes are being added and
/// removed while handling notifications.
class XUSD_ApexDirtyPrimMap
{
public:
    bool isEmpty() const { return myDirtiedPrims.empty(); }

    /// Inserts a dirtied prim and merges the locator set with any existing
    /// entries.
    void insert(const SdfPath &path, const HdDataSourceLocatorSet &locators);

    /// Returns a list of any dirtied prims which are not already in the list of
    /// prims being added (since that already indicates a full resync).
    HdSceneIndexObserver::DirtiedPrimEntries filterAddedPrims(
            const HdSceneIndexObserver::AddedPrimEntries &added_entries);
    /// Returns a list of any dirtied prims which are not already included in
    /// the list of prims being removed (and their subtrees).
    HdSceneIndexObserver::DirtiedPrimEntries filterRemovedPrims(
            const HdSceneIndexObserver::RemovedPrimEntries &removed_entries);
    /// Returns a list with the union of the dirtied prims and locator sets.
    HdSceneIndexObserver::DirtiedPrimEntries mergeDirtiedPrims(
            const HdSceneIndexObserver::DirtiedPrimEntries &dirtied_entries);

private:
    SdfPathTable<HdDataSourceLocatorSet> myDirtiedPrims;
};

/// Stores an APEX scene loaded from Hydra (a `houdiniApexScene` prim and its
/// dependencies), along with information about the Hydra primitives affected by
/// the APEX scene outputs.
class XUSD_HydraApexScene
{
public:
    XUSD_HydraApexScene(const SdfPath &scene_prim_path);
    ~XUSD_HydraApexScene();

    XUSD_HydraApexScene(const XUSD_HydraApexScene &);
    XUSD_HydraApexScene(XUSD_HydraApexScene &&) noexcept;
    XUSD_HydraApexScene &operator=(const XUSD_HydraApexScene &);
    XUSD_HydraApexScene &operator=(XUSD_HydraApexScene &&) noexcept;

    /// Load the APEX scene from the `houdiniApexScene` prim.
    bool load(
            const HdSceneIndexBaseRefPtr &input_scene,
            XUSD_ApexSceneValidator &validator);

    /// Builds the data sources to overlay for prims affected by APEX outputs.
    void createPrimOverlays(
            const HdSceneIndexBaseRefPtr &input_scene,
            XUSD_ApexDirtyPrimMap &dirtied_prims);

    /// Returns the overlay for the specified prim path, or nullptr.
    HdContainerDataSourceHandle getPrimOverlay(const SdfPath &path) const;

    /// Clears the data sources for prims affected by APEX outputs, and updates
    /// the set of dirtied prims. This should be called when the scene is being
    /// removed.
    void removePrimOverlays(XUSD_ApexDirtyPrimMap &dirtied_prims);

    /// Override the APEX evaluation to use graph outputs from the viewer state.
    /// Returns a list of the dirtied prims.
    HdSceneIndexObserver::DirtiedPrimEntries overrideGraphOutputs(
            const UT_StringMap<GU_ConstDetailHandle> &graph_outputs);

    /// Update the APEX evaluation frame and return the set of dirtied prims.
    void setCurrentFrame(fpreal frame, XUSD_ApexDirtyPrimMap &dirtied_prims);

    /// Path to the `houdiniApexScene` prim.
    const SdfPath &getScenePrimPath() const { return myScenePrimPath; }

    /// Record that the APEX scene depends on the specified prim path.
    void addPrimDependency(const SdfPath &path);

    /// Returns whether the prim path is a dependency of the APEX scene.
    /// If include_children is true, returns whether any children of the prim
    /// path are a dependency (useful for "prim removed" notifications).
    bool hasPrimDependency(const SdfPath &path,
                           bool include_children = false) const;

private:
    SdfPath myScenePrimPath;
    SdfPathTable<bool> myPrimDependencies;

    XUSD_ApexScene mySceneInfo;
    UT_IntrusivePtr<HUSD_HydraApexSceneEvaluator> mySceneEvaluator;
    UT_Map<SdfPath, XUSD_ApexPrimOverlayInfo> myPrimOverlays;
};

PXR_NAMESPACE_CLOSE_SCOPE
