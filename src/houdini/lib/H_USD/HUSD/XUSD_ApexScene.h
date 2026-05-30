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

#include <UT/UT_Array.h>
#include <UT/UT_Function.h>
#include <UT/UT_Map.h>
#include <UT/UT_StringHolder.h>

#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>

class GU_ConstDetailHandle;
class GU_DetailHandle;

/// The supported prim types (USD and Hydra) for shape bindings.
enum class HUSD_ApexShapeType : uint8
{
    Mesh,
    Points,
    GSplats,
    Camera
};

PXR_NAMESPACE_OPEN_SCOPE

// Structs to store information from the APEX-related prim types and API
// schemas. This allows sharing some common code between USD and Hydra for
// assembling APEX animation scenes, and also provides a more convenient
// intermediate format to work with.

/// Records data for providing additional validation checks and warnings, e.g.
/// a prim cannot be affected by two different HoudiniApexScene's.
class XUSD_ApexSceneValidator
{
public:
    /// Sets the path of the HoudiniApexScene which is currently being loaded.
    /// This is used for warning messages.
    void setCurrentScenePath(const SdfPath &scene_path);

    /// Records a primitive as being driven by a rig output from the current
    /// scene. Returns false and emits a warning if the primitive is already
    /// affected by another scene.
    bool addOutputPrim(const SdfPath &prim_path);

private:
    /// For prims that are driven by a rig output, maps from the prim path to
    /// the owning HoudiniApexScene prim path.
    UT_Map<SdfPath, SdfPath> myOutputPrimToSceneMap;
    /// Path to the HoudiniApexScene which is currently being loaded.
    SdfPath myScenePrimPath;
};

/// Stores information about a shape binding (from HoudiniApexShapeBindingAPI).
struct XUSD_ApexShapeBinding
{
    /// Path to a primitive which is bound to a rig input or output.
    SdfPath myPrimPath;
    /// Name of the rig input to bind the primitive to.
    UT_StringHolder myInputName;
    /// Name of the rig output which affects the primitive.
    UT_StringHolder myOutputName;
    /// Records the type of shape involved in the binding.
    HUSD_ApexShapeType myShapeType;
};

/// Stores information about an xform binding (from HoudiniApexXformBindingAPI).
struct XUSD_ApexXformBinding
{
    /// Path to the Xformable primitive.
    SdfPath myPrimPath;
    /// Name of the rig output which contains the skeleton.
    UT_StringHolder mySkelOutputName;
    /// Name of the joint which the xform is bound to.
    UT_StringHolder myJointName;
};

/// Represents a character (from HoudiniApexCharacterAPI) and its shapes.
struct XUSD_ApexCharacter
{
    /// The character's name is defined by the instance name for the multi-apply
    /// API schema.
    explicit XUSD_ApexCharacter(const UT_StringRef &char_name);

    /// Returns the scene output path to evaluate the rig.
    /// If myRigName is empty, the default rig is selected by matching *.rig in
    /// the same manner as APEXA_Scene::loadCharacterFromGeometry().
    /// The empty string is returned if the character does not have a rig.
    UT_StringHolder getRigOutputPath(
            const GU_ConstDetailHandle &scene_gdh) const;

    /// List of xform bindings applied via HoudiniApexXformBindingAPI.
    UT_Array<XUSD_ApexXformBinding> myXformBindings;
    /// List of shape bindings applied via HoudiniApexShapeBindingAPI.
    UT_Array<XUSD_ApexShapeBinding> myShapeBindings;
    /// Resolved paths of .bgeo file(s) to load the character's rig, skeleton,
    /// etc from.
    VtArray<SdfAssetPath> myFiles;
    /// Optionally select which of the character's rigs to use.
    UT_StringHolder myRigName;
    /// APEX scene path of the character, e.g. "/electra.char"
    UT_StringHolder myScenePath;
};

/// Data associated with a HoudiniApexScene prim and its characters.
/// This also provides lower-level utilities for assembling the APEX scene
/// geometry, which are common to assembling a scene from USD prims or from
/// Hydra prims.
struct XUSD_ApexScene
{
    using ShapeGeometryConverter
            = UT_Function<GU_ConstDetailHandle (const SdfPath &)>;

    /// Assemble the APEX scene input geometry (packed folders containing the
    /// rigs, shapes, animation etc).
    /// This involves several stages:
    ///  - Loading external .bgeo assets which are referenced by the characters
    ///   or the HoudiniApexScene prim.
    ///  - Converting USD or Hydra prims to GU_Detail's for any prims that
    ///   are bound to a rig as a shape input (HoudiniApexShapeBindingAPI).
    ///  - Recording dictionary attributes (`usdinputbindings` and
    ///   `usdoutputbindings`) to describe the shape bindings, for use by the
    ///    Animate state and the Hydra scene index.
    ///
    /// The provided function is expected to return a GU_Detail for the
    /// specified prim path, (e.g. a USD prim path if the APEX scene is being
    /// assembled from a USD stage)
    GU_DetailHandle loadSceneGeometry(
            const ShapeGeometryConverter &convert_prim_to_geo) const;

    /// List of characters which are referenced by the scene.
    UT_Array<XUSD_ApexCharacter> myCharacters;
    /// Resolved paths of .bgeo file(s) to load animation, etc from.
    VtArray<SdfAssetPath> myFiles;
    /// The "Inherit Animation Layers from Input" setting for each scene delta.
    VtArray<bool> myInheritLayers;
};

PXR_NAMESPACE_CLOSE_SCOPE
