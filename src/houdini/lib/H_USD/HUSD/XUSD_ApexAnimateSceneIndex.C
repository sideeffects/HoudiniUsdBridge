#include "XUSD_ApexAnimateSceneIndex.h"

#include "XUSD_Format.h" // IWYU pragma: keep (for UTdebugPrint)
#include "XUSD_HydraApexScene.h"
#include "XUSD_Tokens.h"

#include <UT/UT_Tracing.h>
#include <SYS/SYS_Math.h>

#include <pxr/base/tf/refPtr.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/sceneGlobalsSchema.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/imaging/hd/sceneIndexPrimView.h>

// Logging for debugging notifications about prims being added / dirtied etc
// #define DEBUG_PRIM_NOTIFICATIONS 1

PXR_NAMESPACE_OPEN_SCOPE

/* static */
XUSD_ApexAnimateSceneIndexRefPtr
XUSD_ApexAnimateSceneIndex::New(
        const HdSceneIndexBaseRefPtr& input_scene_index)
{
    return TfCreateRefPtr(
            new XUSD_ApexAnimateSceneIndex(input_scene_index));
}

XUSD_ApexAnimateSceneIndex::XUSD_ApexAnimateSceneIndex(
        const HdSceneIndexBaseRefPtr &input_scene_index)
    : HdSingleInputFilteringSceneIndexBase(input_scene_index)
{
}

HdSceneIndexPrim
XUSD_ApexAnimateSceneIndex::GetPrim(const SdfPath &prim_path) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(prim_path);

    // Just search through the scenes since we expect to have one, or very few.
    for (auto &&scene : myApexScenes)
    {
        HdContainerDataSourceHandle data_source
                = scene.getPrimOverlay(prim_path);
        if (data_source)
        {
            // Compose with our container of attributes to overlay.
            prim.dataSource = HdOverlayContainerDataSource::New(
                    data_source, prim.dataSource);
            break;
        }
    }

    return prim;
}

void
XUSD_ApexAnimateSceneIndex::updateFromGraphOutputs(
        const UT_StringRef &scene_prim_path_str,
        const UT_StringMap<GU_ConstDetailHandle> &graph_outputs)
{
    utZoneScopedN("XUSD_ApexAnimateSceneIndex override graph outputs");

#ifdef DEBUG_PRIM_NOTIFICATIONS
    UTdebugPrintCd(
            blue, "updateFromGraphOutputs", scene_prim_path_str,
            graph_outputs.size());
#endif

    SdfPath override_scene_path(scene_prim_path_str.toStdString());
    for (XUSD_HydraApexScene &scene : myApexScenes)
    {
        if (scene.getScenePrimPath() == override_scene_path)
        {
            HdSceneIndexObserver::DirtiedPrimEntries dirtied_entries
                    = scene.overrideGraphOutputs(graph_outputs);

            if (_IsObserved())
                _SendPrimsDirtied(dirtied_entries);
        }
    }

    myOverrideGraphOutputs = graph_outputs;
    myOverrideScenePath = !graph_outputs.empty() ? override_scene_path
                                                 : SdfPath::EmptyPath();
}

SdfPathVector
XUSD_ApexAnimateSceneIndex::GetChildPrimPaths(const SdfPath &prim_path) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(prim_path);
}

void
XUSD_ApexAnimateSceneIndex::_PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &added_entries)
{
#ifdef DEBUG_PRIM_NOTIFICATIONS
    for (const HdSceneIndexObserver::AddedPrimEntry &entry : added_entries)
        UTdebugPrintCd(green, "prim added", entry.primPath);
#endif

    XUSD_ApexDirtyPrimMap dirtied_prims;

    // Load scenes for any new houdiniApexScene prims which were
    // discovered, or if any prim dependencies of existing scenes were resynced.
    UT_SortedSet<SdfPath> scenes_to_load;
    for (const HdSceneIndexObserver::AddedPrimEntry &entry : added_entries)
    {
        for (exint i = 0; i < myApexScenes.entries(); ++i)
        {
            if (myApexScenes[i].hasPrimDependency(entry.primPath))
            {
                scenes_to_load.insert(myApexScenes[i].getScenePrimPath());
                myApexScenes[i].removePrimOverlays(dirtied_prims);
                myApexScenes.removeIndex(i);
            }
        }

        if (entry.primType == HusdHdApexTokens->houdiniApexScene)
            scenes_to_load.insert(entry.primPath);
    }

    if (!scenes_to_load.empty())
        loadApexScenes(scenes_to_load, dirtied_prims);

    if (_IsObserved())
    {
        _SendPrimsAdded(added_entries);
        _SendPrimsDirtied(dirtied_prims.filterAddedPrims(added_entries));
    }
}

void
XUSD_ApexAnimateSceneIndex::_PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &removed_entries)
{
#ifdef DEBUG_PRIM_NOTIFICATIONS
    for (const HdSceneIndexObserver::RemovedPrimEntry &entry : removed_entries)
        UTdebugPrintCd(red, "prim removed", entry.primPath);
#endif

    // We need to remove the scene entirely if a `houdiniApexScene` prim was
    // removed. Otherwise, we can attempt to reload the scene if any of its
    // dependencies were removed.
    XUSD_ApexDirtyPrimMap dirtied_prims;
    UT_SortedSet<SdfPath> scenes_to_reload;
    for (exint i = 0; i < myApexScenes.entries(); ++i)
    {
        const XUSD_HydraApexScene &scene = myApexScenes[i];

        bool remove = false;
        bool reload = false;
        for (const HdSceneIndexObserver::RemovedPrimEntry &entry :
             removed_entries)
        {
            if (scene.getScenePrimPath().HasPrefix(entry.primPath))
            {
                remove = true;
                reload = false;
                break;
            }
            else if (!reload &&
                     scene.hasPrimDependency(
                            entry.primPath, /*include_children=*/true))
            {
                reload = true;
                // Note we need to continue going through the entries to see
                // if the houdiniApexScene prim itself is being removed
            }
        }

#ifdef DEBUG_PRIM_NOTIFICATIONS
        if (remove)
            UTdebugPrint("removing scene", myApexScenes[i].getScenePrimPath());
#endif

        if (reload)
            scenes_to_reload.insert(myApexScenes[i].getScenePrimPath());

        if (remove || reload)
        {
            myApexScenes[i].removePrimOverlays(dirtied_prims);
            myApexScenes.removeIndex(i);
        }
    }

    if (!scenes_to_reload.empty())
        loadApexScenes(scenes_to_reload, dirtied_prims);

    if (_IsObserved())
    {
        _SendPrimsRemoved(removed_entries);
        _SendPrimsDirtied(dirtied_prims.filterRemovedPrims(removed_entries));
    }
}

void
XUSD_ApexAnimateSceneIndex::_PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
#ifdef DEBUG_PRIM_NOTIFICATIONS
    for (const HdSceneIndexObserver::DirtiedPrimEntry &entry : entries)
        UTdebugPrintCd(yellow, "prim dirtied", entry.primPath);
#endif

    // Reload any APEX scenes which depend on a dirtied prim, and also check for
    // frame changes.
    XUSD_ApexDirtyPrimMap dirty_prim_map;
    UT_SortedSet<SdfPath> scenes_to_reload;
    bool frame_changed = false;
    for (const HdSceneIndexObserver::DirtiedPrimEntry &entry : entries)
    {
        for (exint i = 0; i < myApexScenes.entries(); ++i)
        {
            if (myApexScenes[i].hasPrimDependency(entry.primPath))
            {
                scenes_to_reload.insert(myApexScenes[i].getScenePrimPath());
                myApexScenes[i].removePrimOverlays(dirty_prim_map);
                myApexScenes.removeIndex(i);
            }
        }

        if (entry.primPath == HdSceneGlobalsSchema::GetDefaultPrimPath()
            && entry.dirtyLocators.Contains(
                    HdSceneGlobalsSchema::GetCurrentFrameLocator()))
        {
            frame_changed = true;
        }
    }

    if (frame_changed)
        updateCurrentFrame(dirty_prim_map);

    if (!scenes_to_reload.empty())
        loadApexScenes(scenes_to_reload, dirty_prim_map);

    if (_IsObserved())
    {
        // Fast path to avoid any overhead if nothing APEX-related was dirtied.
        if (dirty_prim_map.isEmpty())
            _SendPrimsDirtied(entries);
        else
            _SendPrimsDirtied(dirty_prim_map.mergeDirtiedPrims(entries));
    }
}

void
XUSD_ApexAnimateSceneIndex::loadApexScenes(
        const UT_SortedSet<SdfPath> &apex_scene_prim_paths,
        XUSD_ApexDirtyPrimMap &dirtied_prims)
{
    XUSD_ApexSceneValidator validator;
    // TODO - populate validator from the outputs of scenes that aren't being
    // reloaded.

    const fpreal current_frame = getCurrentFrame();

    for (const SdfPath &apex_scene_prim_path : apex_scene_prim_paths)
    {
#ifdef DEBUG_PRIM_NOTIFICATIONS
        UTdebugPrint("loading scene from", apex_scene_prim_path);
#endif

        XUSD_HydraApexScene scene(apex_scene_prim_path);

        if (!scene.load(_GetInputSceneIndex(), validator))
            continue;

        scene.setCurrentFrame(current_frame, dirtied_prims);
        scene.createPrimOverlays(_GetInputSceneIndex(), dirtied_prims);

        // If we reload a scene while the viewer state is active, restore the
        // output overrides from the viewer state. These may be different if
        // e.g. there are pending keys, and this also ensures that we don't
        // unnecessarily evaluate the outputs again from our internal
        // APEXA_SceneInvoke's.
        if (myOverrideScenePath == apex_scene_prim_path)
            scene.overrideGraphOutputs(myOverrideGraphOutputs);

        myApexScenes.append(std::move(scene));
    }
}

void
XUSD_ApexAnimateSceneIndex::updateCurrentFrame(
        XUSD_ApexDirtyPrimMap &dirtied_prims)
{
    const fpreal current_frame = getCurrentFrame();

    for (XUSD_HydraApexScene &apex_scene : myApexScenes)
    {
#ifdef DEBUG_PRIM_NOTIFICATIONS
        UTdebugPrint(
                "updating current frame for", apex_scene.getScenePrimPath());
#endif

        apex_scene.setCurrentFrame(current_frame, dirtied_prims);
    }
}

fpreal
XUSD_ApexAnimateSceneIndex::getCurrentFrame() const
{
    HdSceneGlobalsSchema globals
            = HdSceneGlobalsSchema::GetFromSceneIndex(_GetInputSceneIndex());
    if (HdDoubleDataSourceHandle frame_ds = globals.GetCurrentFrame())
    {
        const double frame = frame_ds->GetTypedValue(0.0f);

        // Note the default time is encoded as NaN, but the scene time should
        // already have been set before the scene was populated.
        UT_ASSERT(!SYSisNan(frame));

        return frame;
    }

    // The scene globals scene index is registered as an input of us so this
    // should always succeed.
    UT_ASSERT_MSG(false, "Unable to read current frame from scene globals!");
    return 1.0;
}

PXR_NAMESPACE_CLOSE_SCOPE
