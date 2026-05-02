//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#pragma once

#include "XUSD_HydraApexScene.h"

#include <GU/GU_DetailHandle.h>
#include <UT/UT_Array.h>
#include <UT/UT_Set.h>
#include <UT/UT_StringMap.h>

#include <pxr/base/tf/declarePtrs.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

class GU_ConstDetailHandle;
class UT_StringRef;

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(XUSD_ApexAnimateSceneIndex);

class XUSD_ApexAnimateSceneIndex :
    public HdSingleInputFilteringSceneIndexBase
{
public:
    static XUSD_ApexAnimateSceneIndexRefPtr New(
            const HdSceneIndexBaseRefPtr& input_scene_index);
    
    HdSceneIndexPrim GetPrim(const SdfPath &prim_path) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath &prim_path) const override;

    /// Update the scene index from the APEX rig outputs evaluated by the
    /// Animate state. This is invoked by the viewer state after any changes are
    /// made to its APEX scene.
    void updateFromGraphOutputs(
            const UT_StringRef &scene_prim_path,
            const UT_StringMap<GU_ConstDetailHandle> &graph_outputs);

protected:
    XUSD_ApexAnimateSceneIndex(const HdSceneIndexBaseRefPtr &input_scene_index);

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
    /// Read the current frame from the input scene index (using the
    /// HdSceneGlobalsSchema).
    fpreal getCurrentFrame() const;

    /// Load one or more new APEX scenes from the specified prims.
    void loadApexScenes(
            const UT_SortedSet<SdfPath> &apex_scene_prim_paths,
            XUSD_ApexDirtyPrimMap &dirtied_prims);
    /// Update the current frame for the APEX scenes.
    void updateCurrentFrame(XUSD_ApexDirtyPrimMap &dirtied_prims);

    /// List of APEX scenes loaded from `houdiniApexScene` primitives.
    UT_Array<XUSD_HydraApexScene> myApexScenes;

    /// When the Animate state is active, store the last evaluated graph outputs
    /// it sent us for its scene. This allows us to reapply them if we happen to
    /// reload our scene (e.g. after the Animate LOP's stash has been touched)
    SdfPath myOverrideScenePath;
    UT_StringMap<GU_ConstDetailHandle> myOverrideGraphOutputs;
};

PXR_NAMESPACE_CLOSE_SCOPE
