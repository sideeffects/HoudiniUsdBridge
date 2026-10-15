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

#include <GU/GU_DetailHandle.h>
#include <UT/UT_Set.h>
#include <UT/UT_StringHolder.h>

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/pathTable.h>
#include <pxr/usd/usd/common.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Options for converting bound USD primitives to APEX input geometry.
struct XUSD_UsdApexSceneImportOptions
{
    /// If non-empty, adds a primitive attribute containing the source USD
    /// primitive path.
    UT_StringHolder myPathAttrib;
};

/// Tracks prims / properties whose changes may require rebuilding the APEX
/// scene.
class XUSD_UsdApexSceneDependencies
{
public:
    void addPrim(const SdfPath &prim_path)
    {
        myPathDependencies[prim_path] = true;
    }

    void addProperty(const SdfPath &property_path)
    {
        myPathDependencies[property_path] = true;
    }

    /// Returns whether the path, or optionally also any of its children, are a
    /// dependency of the APEX scene.
    bool hasDependency(const SdfPath &path, bool include_children) const;

private:
    SdfPathTable<bool> myPathDependencies;
};

/// An APEX scene assembled from a USD stage, along with information about the
/// USD objects it depends on.
class XUSD_UsdApexScene
{
public:
    explicit XUSD_UsdApexScene(const SdfPath &scene_prim_path);

    /// Finds active HoudiniApexScene primitives rooted at any of the supplied
    /// prim paths.
    static UT_SortedSet<SdfPath> findScenePrimPaths(
            const UsdStageRefPtr &stage,
            const SdfPathVector &root_prim_paths);

    /// Parses the scene and assembles its packed-folder geometry.
    bool load(
            const UsdStageRefPtr &stage,
            XUSD_ApexSceneValidator &validator,
            const XUSD_UsdApexSceneImportOptions &import_options);

    /// Adds this scene's accepted output paths to a validator. This is used to
    /// preserve deterministic output ownership when only some scenes are
    /// rebuilt.
    void addOutputsToValidator(XUSD_ApexSceneValidator &validator) const;

    const SdfPath &getScenePrimPath() const { return myScenePrimPath; }
    const XUSD_ApexScene &getSceneInfo() const { return mySceneInfo; }
    const GU_DetailHandle &getSceneGeometry() const { return mySceneGeometry; }
    const XUSD_UsdApexSceneDependencies &getDependencies() const
    {
        return myDependencies;
    }

private:
    SdfPath myScenePrimPath;
    XUSD_ApexScene mySceneInfo;
    GU_DetailHandle mySceneGeometry;
    XUSD_UsdApexSceneDependencies myDependencies;
};

PXR_NAMESPACE_CLOSE_SCOPE
