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

#include "XUSD_HydraApexScene.h"

#include "HUSD_HydraApexSceneEvaluator.h"
#include "UsdHoudini/tokens.h"
#include "XUSD_ApexScene.h"
#include "XUSD_Format.h" // IWYU pragma: keep (for UTdebugPrint)
#include "XUSD_HydraApexDataSource.h"
#include "XUSD_HydraGeoImport.h"
#include "XUSD_Tokens.h"

#include <GU/GU_Detail.h>
#include <GU/GU_DetailHandle.h>
#include <UT/UT_Algorithm.h>
#include <UT/UT_Optional.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Tracing.h>

#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/imaging/hd/sceneIndexPrimView.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Read the value from an entry in a container data source at the current time.
/// If the container is empty or the entry does not exist, the value is left
/// as-is (i.e. the caller should first assign a fallback value)
/// TODO - in the future we could also generate HdSchema implementations for the
/// APEX-related types.
template <typename T>
static void
xusdReadTypedData(
        const HdContainerDataSourceHandle &container,
        const TfToken &name,
        T &value)
{
    if (!container)
        return;

    auto data = HdTypedSampledDataSource<T>::Cast(container->Get(name));
    if (!data)
        return;

    value = data->GetTypedValue(0.0f);
}

/// Utility function to iterate over the child containers of a container data
/// sources. This is primarily useful for multi-apply API schemas which
/// translate into a "container of containers".
static UT_SmallArray<std::pair<TfToken, HdContainerDataSourceHandle>>
xusdGetChildContainers(const HdContainerDataSourceHandle &container)
{
    UT_SmallArray<std::pair<TfToken, HdContainerDataSourceHandle>> result;

    if (!container)
        return result;

    TfTokenVector names = container->GetNames();
    result.setCapacity(names.size());

    for (const TfToken &name : names)
    {
        auto child = HdContainerDataSource::Cast(container->Get(name));
        if (child)
            result.emplace_back(name, child);
    }

    return result;
}

/// Determines the shape type for the prim, or none if the prim type is
/// unsupported. Note that meshes are currently the only supported point-based
/// prims. This should be updated once XUSD_HydraGeoImport is extended to
/// support translating more prim types for the rig's input geometry.
static inline UT_Optional<HUSD_ApexShapeType>
xusdGetShapeType(const HdSceneIndexPrim &prim)
{
    const TfToken &prim_type = prim.primType;
    if (prim_type == HdPrimTypeTokens->mesh)
        return HUSD_ApexShapeType::Mesh;
    else if (prim_type == HdPrimTypeTokens->camera)
        return HUSD_ApexShapeType::Camera;

    return UT_NULLOPT;
}

/// Extract and validate the prims which are bound to geometry shape outputs
/// from a rig.
static UT_Array<XUSD_ApexShapeBinding>
xusdReadShapeBindings(
        const HdSceneIndexBase &input_scene,
        const HdSceneIndexPrim &char_prim,
        XUSD_ApexSceneValidator &validator)
{
    UT_Array<XUSD_ApexShapeBinding> shape_bindings;

    auto shape_binding_apis
            = HdContainerDataSource::Cast(char_prim.dataSource->Get(
                    HusdHdApexTokens->houdiniApexShapeBindings));

    for (auto &&[shape_binding_name, shape_binding_api] :
         xusdGetChildContainers(shape_binding_apis))
    {
        XUSD_ApexShapeBinding shape_binding;
        xusdReadTypedData(
                shape_binding_api, HusdHdApexTokens->binding,
                shape_binding.myPrimPath);

        std::string rig_input;
        xusdReadTypedData(
                shape_binding_api, HusdHdApexTokens->input, rig_input);
        shape_binding.myInputName = rig_input;

        std::string rig_output;
        xusdReadTypedData(
                shape_binding_api, HusdHdApexTokens->output, rig_output);
        shape_binding.myOutputName = rig_output;

        // Skip if e.g. the USD prim is inactive.
        HdSceneIndexPrim shape_prim
                = input_scene.GetPrim(shape_binding.myPrimPath);
        if (!shape_prim)
            continue;

        UT_Optional<HUSD_ApexShapeType> shape_type
                = xusdGetShapeType(shape_prim);
        if (!shape_type)
        {
            TF_WARN("Unsupported prim type '%s' for <%s>",
                    shape_prim.primType.GetText(),
                    shape_binding.myPrimPath.GetText());
            continue;
        }

        shape_binding.myShapeType = *shape_type;

        // Verify that the shape is only driven by one output.
        if (shape_binding.myOutputName
            && !validator.addOutputPrim(shape_binding.myPrimPath))
        {
            continue;
        }

        shape_bindings.append(std::move(shape_binding));
    }

    return shape_bindings;
}

/// Extract and validate the prims which are bound to joints from a rig's
/// skeleton output.
static UT_Array<XUSD_ApexXformBinding>
xusdReadXformBindings(
        const HdSceneIndexBase &input_scene,
        const HdSceneIndexPrim &char_prim,
        XUSD_ApexSceneValidator &validator)
{
    UT_Array<XUSD_ApexXformBinding> xform_bindings;

    auto xform_binding_apis
            = HdContainerDataSource::Cast(char_prim.dataSource->Get(
                    HusdHdApexTokens->houdiniApexXformBindings));

    for (auto &&[xform_binding_name, xform_binding_api] :
         xusdGetChildContainers(xform_binding_apis))
    {
        XUSD_ApexXformBinding xform_binding;
        xusdReadTypedData(
                xform_binding_api, HusdHdApexTokens->binding,
                xform_binding.myPrimPath);

        std::string joint_name;
        xusdReadTypedData(
                xform_binding_api, HusdHdApexTokens->joint, joint_name);
        xform_binding.myJointName = joint_name;

        std::string rig_output;
        xusdReadTypedData(
                xform_binding_api, HusdHdApexTokens->output, rig_output);
        xform_binding.mySkelOutputName = rig_output;

        // Skip if e.g. the USD prim is inactive.
        HdSceneIndexPrim shape_prim
                = input_scene.GetPrim(xform_binding.myPrimPath);
        if (!shape_prim)
            continue;

        // Issue a warning if the prim is not Xformable.
        HdXformSchema xformable
                = HdXformSchema::GetFromParent(shape_prim.dataSource);
        if (!xformable)
        {
            TF_WARN("Prim <%s> is not Xformable",
                    xform_binding.myPrimPath.GetText());
            continue;
        }

        // Verify that the prim is only driven by one output.
        if (!validator.addOutputPrim(xform_binding.myPrimPath))
            continue;

        xform_bindings.append(std::move(xform_binding));
    }

    return xform_bindings;
}

static GU_DetailHandle
xusdLoadSceneGeometry(
        const HdSceneIndexBase &input_scene,
        const XUSD_ApexScene &apex_scene_info)
{
    utZoneScopedN("XUSD_HydraApexScene load scene geometry");

    auto convert_prim = [&](const SdfPath &prim_path)
    {
        XUSD_HydraGeoImportOptions options;
        // Don't apply the current prim transform, since we want geometry at the
        // bind pose.
        options.myApplyPrimXform = false;

        return XUSDimportGeoFromHydraPrim(
                input_scene.GetPrim(prim_path), prim_path, options);
    };

    return apex_scene_info.loadSceneGeometry(convert_prim);
}

XUSD_HydraApexScene::XUSD_HydraApexScene(const SdfPath &scene_prim_path)
    : myScenePrimPath(scene_prim_path)
{
}

XUSD_HydraApexScene::~XUSD_HydraApexScene() = default;

XUSD_HydraApexScene::XUSD_HydraApexScene(const XUSD_HydraApexScene &) = default;

XUSD_HydraApexScene::XUSD_HydraApexScene(XUSD_HydraApexScene &&) noexcept = default;

XUSD_HydraApexScene &XUSD_HydraApexScene::operator=(const XUSD_HydraApexScene &) = default;

XUSD_HydraApexScene &XUSD_HydraApexScene::operator=(XUSD_HydraApexScene &&) noexcept = default;

void
XUSD_HydraApexScene::addPrimDependency(const SdfPath &path)
{
    myPrimDependencies.insert({path, true});
}

bool
XUSD_HydraApexScene::hasPrimDependency(
        const SdfPath &path,
        bool include_children) const
{
    if (include_children)
    {
        // Use FindSubtreeRange() to efficiently check whether there are any
        // paths prefixed by the provided path.
        auto range = myPrimDependencies.FindSubtreeRange(path);
        return range.first != range.second;
    }
    else
    {
        // Note that with SdfPathTable ancestor paths are implicitly inserted,
        // so we need to check the node's value too.
        auto it = myPrimDependencies.find(path);
        return it != myPrimDependencies.end() && it->second;
    }
}

bool
XUSD_HydraApexScene::load(
        const HdSceneIndexBaseRefPtr &input_scene,
        XUSD_ApexSceneValidator &validator)
{
    utZoneScopedN("XUSD_HydraApexScene parse scene info");

    validator.setCurrentScenePath(myScenePrimPath);

    HdSceneIndexPrim scene_prim = input_scene->GetPrim(myScenePrimPath);
    if (!scene_prim)
        return false;

    addPrimDependency(myScenePrimPath);

    auto scene_prim_data = HdContainerDataSource::Cast(
            scene_prim.dataSource->Get(HusdHdApexTokens->houdiniApexScene));
    mySceneInfo = XUSD_ApexScene();
    xusdReadTypedData(
            scene_prim_data, UsdHoudiniTokens->sceneFiles,
            mySceneInfo.myFiles);
    xusdReadTypedData(
            scene_prim_data, UsdHoudiniTokens->inheritAnimationLayers,
            mySceneInfo.myInheritLayers);

    if (mySceneInfo.myInheritLayers.size()
        != mySceneInfo.myFiles.size())
    {
        TF_WARN("Invalid number of values for '%s' attribute on primitive "
                "<%s>",
                UsdHoudiniTokens->inheritAnimationLayers.GetText(),
                myScenePrimPath.GetText());
        return false;
    }

    auto char_bindings = HdContainerDataSource::Cast(scene_prim.dataSource->Get(
            HusdHdApexTokens->houdiniApexCharacterBindings));

    for (auto &&[char_name, char_binding] :
         xusdGetChildContainers(char_bindings))
    {
        XUSD_ApexCharacter apex_char(char_name.GetString());

        SdfPath char_prim_path;
        xusdReadTypedData(
                char_binding, HusdHdApexTokens->binding, char_prim_path);

        std::string rig_name;
        xusdReadTypedData(char_binding, HusdHdApexTokens->rig, rig_name);

        HdSceneIndexPrim char_prim = input_scene->GetPrim(char_prim_path);
        // If the prim is missing, it might e.g. be deactivated on the USD
        // side.
        if (!char_prim)
            continue;

        addPrimDependency(char_prim_path);

        auto char_api = HdContainerDataSource::Cast(char_prim.dataSource->Get(
                HusdHdApexTokens->houdiniApexCharacter));
        if (!char_api)
        {
            TF_WARN("Prim <%s> is missing required '%s' schema",
                    char_prim_path.GetText(),
                    UsdHoudiniTokens->HoudiniApexCharacterAPI.GetText());
            continue;
        }

        xusdReadTypedData(char_api, HusdHdApexTokens->files, apex_char.myFiles);

        // The CharacterBindingAPI takes precedence for the rig selection.
        if (rig_name.empty())
            xusdReadTypedData(char_api, HusdHdApexTokens->rig, rig_name);

        apex_char.myRigName = rig_name;

        apex_char.myXformBindings = xusdReadXformBindings(
                *input_scene, char_prim, validator);
        apex_char.myShapeBindings = xusdReadShapeBindings(
                *input_scene, char_prim, validator);

        for (const XUSD_ApexShapeBinding &binding : apex_char.myShapeBindings)
            addPrimDependency(binding.myPrimPath);

        for (const XUSD_ApexXformBinding &binding : apex_char.myXformBindings)
            addPrimDependency(binding.myPrimPath);

        mySceneInfo.myCharacters.append(std::move(apex_char));
    }

    GU_DetailHandle scene_gdh = xusdLoadSceneGeometry(
            *input_scene, mySceneInfo);
    // A non-null detail is expected to be returned.
    UT_ASSERT(scene_gdh.isValid());

#if 0 // Debugging code to inspect the scene geometry.
    if (scene_gdh.isValid())
    {
        static int theCounter = 0;
        UT_StringHolder path;
        path.format("/tmp/scene_{0}.bgeo", theCounter++);
        scene_gdh.gdp()->save(path, nullptr);
    }
#endif

    mySceneEvaluator = UTmakeIntrusive<HUSD_HydraApexSceneEvaluator>();
    mySceneEvaluator->setSceneGeometry(scene_gdh);

    return true;
}

/// Helper function to extract a prim's world xform.
static bool
xusdGetPrimXform(const HdSceneIndexPrim &prim, GfMatrix4d &xform)
{
    HdXformSchema xform_schema = HdXformSchema::GetFromParent(prim.dataSource);
    if (!xform_schema || !xform_schema.GetMatrix()
        || !xform_schema.GetResetXformStack())
    {
        xform.SetIdentity();
        return false;
    }

    // We expect to be running after the flattening scene index.
    UT_ASSERT(xform_schema.GetResetXformStack()->GetTypedValue(0.0));

    xform = xform_schema.GetMatrix()->GetTypedValue(0.0);
    return true;
}

static const auto theResetXformSource
        = HdRetainedTypedSampledDataSource<bool>::New(true);

static void
xusdBuildXformOverlays(
        const HdSceneIndexBaseRefPtr &input_scene,
        const UT_Set<SdfPath> &paths_with_bindings,
        const SdfPath &root_prim_path,
        const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
        const exint output_idx,
        const UT_StringHolder &joint_name,
        UT_Map<SdfPath, XUSD_ApexPrimOverlayInfo> &prim_overlays)
{
    // xusdReadXformBindings() validates that prims with xform bindings are
    // transformable, so this should succeed.
    GfMatrix4d root_xform;
    UT_VERIFY(xusdGetPrimXform(input_scene->GetPrim(root_prim_path), root_xform));

    GfMatrix4d root_xform_inv = root_xform.GetInverse();

    HdSceneIndexPrimView view(input_scene, root_prim_path);
    for (auto it = view.begin(); it != view.end(); ++it)
    {
        const SdfPath &prim_path = *it;

        // If we reached a child which has a binding to a different joint in the
        // skeleton, or a shape binding (which outputs the geo in world space),
        // skip since we don't need to modify it based on this joint.
        if (prim_path != root_prim_path
            && paths_with_bindings.contains(prim_path))
        {
            it.SkipDescendants();
            continue;
        }

        HdSceneIndexPrim input_prim = input_scene->GetPrim(prim_path);

        GfMatrix4d prim_xform;
        if (!xusdGetPrimXform(input_prim, prim_xform))
            continue; // Prim type isn't xformable - nothing to do.

        GfMatrix4d child_xform;
        if (prim_path == root_prim_path)
            child_xform.SetIdentity();
        else
            child_xform = prim_xform * root_xform_inv;

        auto xform_source = XUSD_HydraApexXformDataSource::New(
                prim_path, evaluator, output_idx, joint_name, child_xform);

        HdContainerDataSourceHandle xform_override
                = HdXformSchema::Builder()
                          .SetMatrix(xform_source)
                          .SetResetXformStack(theResetXformSource)
                          .Build();

        HdDataSourceLocatorSet dirty_locators;
        dirty_locators.append(HdXformSchema::GetDefaultLocator());

        HdContainerDataSourceHandle overlay
                = HdRetainedContainerDataSource::New(
                        HdXformSchema::GetSchemaToken(), xform_override);

        prim_overlays[prim_path] = {overlay, dirty_locators};
    }
}

/// Build a set of the paths with xform / shape bindings for efficient
/// lookups when building the xform overlays.
static UT_Set<SdfPath>
xusdGetBindingPaths(const XUSD_ApexCharacter &character)
{
    UT_Set<SdfPath> paths_with_bindings;

    for (const XUSD_ApexShapeBinding &binding : character.myShapeBindings)
    {
        if (binding.myOutputName)
            paths_with_bindings.insert(binding.myPrimPath);
    }

    for (const XUSD_ApexXformBinding &binding : character.myXformBindings)
        paths_with_bindings.insert(binding.myPrimPath);

    return paths_with_bindings;
}

void
XUSD_HydraApexScene::createPrimOverlays(
        const HdSceneIndexBaseRefPtr &input_scene,
        XUSD_ApexDirtyPrimMap &dirtied_prims)
{
    UT_ASSERT(myPrimOverlays.empty());

    for (const XUSD_ApexCharacter &character : mySceneInfo.myCharacters)
    {
        // Determine which rig the character should use.
        UT_StringHolder rig_output_path = character.getRigOutputPath(
                mySceneEvaluator->getSceneGeometry());
        if (!rig_output_path)
            continue;

        const UT_Set<SdfPath> paths_with_bindings
                = xusdGetBindingPaths(character);

        UT_StringMap<exint> known_outputs;
        for (const XUSD_ApexShapeBinding &binding : character.myShapeBindings)
        {
            if (!binding.myOutputName)
                continue;

            exint output_idx = mySceneEvaluator->registerOutput(
                    rig_output_path, binding.myOutputName);

            auto overlay = XUSD_HydraApexShapeDataSource::New(
                    binding.myPrimPath, binding.myShapeType, mySceneEvaluator,
                    output_idx);
            HdDataSourceLocatorSet dirty_locators
                    = XUSD_HydraApexShapeDataSource::getDefaultLocators(
                            binding.myShapeType);

            myPrimOverlays[binding.myPrimPath] = {overlay, dirty_locators};
        }

        for (const XUSD_ApexXformBinding &binding : character.myXformBindings)
        {
            // It's expected we may have multiple prims bound to different
            // joints of the same skeleton, so avoid registering many duplicate
            // outputs.
            const exint output_idx = UTfindOrInsert(
                    known_outputs, binding.mySkelOutputName,
                    [&]()
                    {
                        return mySceneEvaluator->registerOutput(
                                rig_output_path, binding.mySkelOutputName);
                    });

            xusdBuildXformOverlays(
                    input_scene, paths_with_bindings, binding.myPrimPath,
                    mySceneEvaluator, output_idx, binding.myJointName,
                    myPrimOverlays);
        }
    }

    // Output the list of dirtied prims.
    for (auto &&[path, overlay] : myPrimOverlays)
        dirtied_prims.insert(path, overlay.myDirtyLocators);
}

HdContainerDataSourceHandle
XUSD_HydraApexScene::getPrimOverlay(const SdfPath &path) const
{
    auto it = myPrimOverlays.find(path);
    return (it != myPrimOverlays.end()) ? it->second.myDataSource : nullptr;
}

void
XUSD_HydraApexScene::removePrimOverlays(XUSD_ApexDirtyPrimMap &dirtied_prims)
{
    for (auto &&[prim_path, info] : myPrimOverlays)
        dirtied_prims.insert(prim_path, info.myDirtyLocators);

    myPrimOverlays.clear();
}

HdSceneIndexObserver::DirtiedPrimEntries
XUSD_HydraApexScene::overrideGraphOutputs(
        const UT_StringMap<GU_ConstDetailHandle> &graph_outputs)
{
    mySceneEvaluator->setOutputOverrides(graph_outputs);

    HdSceneIndexObserver::DirtiedPrimEntries dirtied_entries;
    dirtied_entries.reserve(myPrimOverlays.size());
    for (auto &&[prim_path, info] : myPrimOverlays)
        dirtied_entries.emplace_back(prim_path, info.myDirtyLocators);

    return dirtied_entries;
}

void
XUSD_HydraApexScene::setCurrentFrame(
        fpreal frame,
        XUSD_ApexDirtyPrimMap &dirtied_prims)
{
    UT_ASSERT(mySceneEvaluator);
    mySceneEvaluator->setCurrentFrame(frame);

    for (auto &&[prim_path, info] : myPrimOverlays)
        dirtied_prims.insert(prim_path, info.myDirtyLocators);
}

void
XUSD_ApexDirtyPrimMap::insert(
        const SdfPath &path,
        const HdDataSourceLocatorSet &locators)
{
    myDirtiedPrims[path].insert(locators);
}

static HdSceneIndexObserver::DirtiedPrimEntries
xusdBuildDirtiedEntries(
        const SdfPathTable<HdDataSourceLocatorSet> &dirtied_prims)
{
    HdSceneIndexObserver::DirtiedPrimEntries dirtied_entries;
    for (auto &&[path, locators] : dirtied_prims)
    {
        // Skip any empty entries, since SdfPathTable includes all ancestor
        // prims.
        if (locators.IsEmpty())
            continue;

        dirtied_entries.emplace_back(path, locators);
    }

    return dirtied_entries;
}

HdSceneIndexObserver::DirtiedPrimEntries
XUSD_ApexDirtyPrimMap::filterAddedPrims(
        const HdSceneIndexObserver::AddedPrimEntries &added_entries)
{
    for (const HdSceneIndexObserver::AddedPrimEntry &added_entry :
         added_entries)
    {
        auto it = myDirtiedPrims.find(added_entry.primPath);
        if (it == myDirtiedPrims.end())
            continue;

        // Note that erase() removes the entire subtree, so just clear out the
        // prim's locator set instead if there are children.
        if (it.HasChild())
            it->second = {};
        else
            myDirtiedPrims.erase(it);
    }

    return xusdBuildDirtiedEntries(myDirtiedPrims);
}

HdSceneIndexObserver::DirtiedPrimEntries
XUSD_ApexDirtyPrimMap::filterRemovedPrims(
        const HdSceneIndexObserver::RemovedPrimEntries &removed_entries)
{
    for (const HdSceneIndexObserver::RemovedPrimEntry &removed_entry :
         removed_entries)
    {
        myDirtiedPrims.erase(removed_entry.primPath);
    }

    return xusdBuildDirtiedEntries(myDirtiedPrims);
}

HdSceneIndexObserver::DirtiedPrimEntries
XUSD_ApexDirtyPrimMap::mergeDirtiedPrims(
        const HdSceneIndexObserver::DirtiedPrimEntries &dirtied_entries)
{
    for (const HdSceneIndexObserver::DirtiedPrimEntry &entry : dirtied_entries)
        myDirtiedPrims[entry.primPath].insert(entry.dirtyLocators);

    return xusdBuildDirtiedEntries(myDirtiedPrims);
}

PXR_NAMESPACE_CLOSE_SCOPE
