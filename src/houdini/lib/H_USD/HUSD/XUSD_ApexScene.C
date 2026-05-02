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

#include "XUSD_ApexScene.h"

#include "HUSD_ErrorScope.h"
#include "HUSD_GeoUtils.h"
#include "XUSD_Format.h"
#include "XUSD_LockedGeoRegistry.h"

#include <APEXA/APEXA_SceneUtils.h>
#include <GA/GA_Handle.h>
#include <GA/GA_Types.h>
#include <GU/GU_Detail.h>
#include <GU/GU_DetailHandle.h>
#include <GU/GU_PackedFolders.h>
#include <SYS/SYS_Types.h>
#include <UT/UT_Array.h>
#include <UT/UT_Options.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringUtils.h>
#include <UT/UT_Tracing.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_WorkBuffer.h>

#include <pxr/usd/sdf/layer.h>
#include <pxr/base/tf/pathUtils.h>

static constexpr UT_StringLit theOutputName("output");

PXR_NAMESPACE_OPEN_SCOPE

void
XUSD_ApexSceneValidator::setCurrentScenePath(const SdfPath &scene_path)
{
    myScenePrimPath = scene_path;
}

bool
XUSD_ApexSceneValidator::addOutputPrim(const SdfPath &output_prim_path)
{
    UT_ASSERT(!myScenePrimPath.IsEmpty());

    if (auto it = myOutputPrimToSceneMap.find(output_prim_path);
        it != myOutputPrimToSceneMap.end())
    {
        UT_WorkBuffer msg;
        msg.format(
                "Primitive '{0}' is affected by multiple "
                "outputs (scenes '{1}' and '{2}'). Scene '{1}' "
                "will be used.",
                output_prim_path, it->second, myScenePrimPath);
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    myOutputPrimToSceneMap.emplace(output_prim_path, myScenePrimPath);
    return true;
}

XUSD_ApexCharacter::XUSD_ApexCharacter(const UT_StringRef &char_name)
{
    myScenePath.format("/{0}.char", char_name);
}

UT_StringHolder
XUSD_ApexCharacter::getRigOutputPath(
        const GU_ConstDetailHandle &scene_gdh) const
{
    UT_StringHolder rig_name = myRigName;
    if (!rig_name)
    {
        UT_WorkBuffer pattern;
        pattern.format("{0}/*.rig", myScenePath);

        GU_PackedFoldersRO scene_folders(scene_gdh);
        UT_Array<const GU_PackedFoldersRO::FileInfo *> rig_files;
        UT_StringHolder error;
        if (!scene_folders.findFilesByPattern(
                    rig_files, pattern, /*sorted=*/true, error)
            || rig_files.isEmpty())
        {
            return UT_StringHolder::theEmptyString;
        }

        rig_name = UTstringFileName(rig_files.last()->path());
    }

    UT_StringHolder rig_output_path;
    rig_output_path.format(
            "{0}/{1}/{2}", myScenePath, rig_name, theOutputName.asRef());
    return rig_output_path;
}

/// Loads geometry from an asset path, including locked geometry from a SOP node
/// (e.g. 'op:/path/to/node.sop') similar to SOP Import.
static GU_ConstDetailHandle
xusdLoadGeometryAsset(const SdfAssetPath &asset_path)
{
    std::string resolved_path = asset_path.GetResolvedPath();
    if (resolved_path.empty())
        resolved_path = asset_path.GetAssetPath();

    std::string node_path;
    SdfLayer::FileFormatArguments args;
    if (SdfLayer::SplitIdentifier(resolved_path, &node_path, &args)
        && TfGetExtension(node_path) == "sop")
    {
        GU_ConstDetailHandle gdh = XUSD_LockedGeoRegistry::getGeometry(
                node_path, args);
        if (!gdh.isValid())
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Failed to get locked geometry from '{0}'", resolved_path);
            HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        }

        return gdh;
    }

    return HUSDloadGeometryFromAsset(resolved_path);
}

static void
xusdLoadAssetFiles(const XUSD_ApexScene &scene, GU_DetailHandle &scene_gdh)
{
    utZoneScopedN("XUSD_ApexScene load asset files");

    GU_PackedFolders scene_folders(scene_gdh);

    // Load the characters' files first, under each character's path.
    // The scene asset are merged later since they are allowed to override the
    // character elements.
    for (const XUSD_ApexCharacter &character : scene.myCharacters)
    {
        GU_DetailHandle char_gdh;
        char_gdh.allocateAndSet(new GU_Detail(), /*own=*/true);

        // Merge the packed folders from the character's files.
        for (const SdfAssetPath &asset_path : character.myFiles)
        {
            GU_ConstDetailHandle gdh = xusdLoadGeometryAsset(asset_path);
            if (!gdh.isValid())
                continue;

            APEXAmergeSceneDelta(
                    char_gdh, gdh, /*inherit_animation_layers=*/false);
        }

        // Insert under the character's path, e.g. '/electra.char'.
        scene_folders.insert(
                character.myScenePath, char_gdh, /*pack=*/true,
                /*is_folder=*/true, /*is_visible=*/true);
    }

    // Next, load the scene files at the root of the hierarchy.
    for (exint i = 0, n = scene.myFiles.size(); i < n; ++i)
    {
        GU_ConstDetailHandle gdh = xusdLoadGeometryAsset(scene.myFiles[i]);
        if (!gdh.isValid())
            continue;

        APEXAmergeSceneDelta(scene_gdh, gdh, scene.myInheritLayers[i]);
    }

#if 0 // Output the result to a bgeo for debugging
    mySceneGdh.gdp()->save("/tmp/scene.bgeo", nullptr);
#endif
}

static void
xusdLoadGeometryInputsFromPrims(
        const XUSD_ApexScene &scene,
        GU_DetailHandle &scene_gdh,
        const XUSD_ApexScene::ShapeGeometryConverter &convert_to_geo)
{
    utZoneScopedN("husdApexSceneData load prim geometry inputs");

    auto input_bindings = UTmakeUnique<UT_Options>();

    GU_PackedFolders scene_folders(scene_gdh);

    for (const XUSD_ApexCharacter &character : scene.myCharacters)
    {
        for (const XUSD_ApexShapeBinding &binding : character.myShapeBindings)
        {
            if (!binding.myInputName)
                continue;

            GU_ConstDetailHandle shape_gdh = convert_to_geo(binding.myPrimPath);

            // Insert the shape into the APEX scene, with the name (e.g.
            // 'Base.shp') as the rig input name. This will be auto-bound to the
            // rig input via APEXA_SceneCharacter::getInputGeos().
            UT_WorkBuffer shape_path;
            shape_path.format(
                    "{0}/{1}", character.myScenePath, binding.myInputName);

            input_bindings->setOptionS(
                    shape_path, binding.myPrimPath.GetAsString());

            scene_folders.insert(shape_path, shape_gdh);
        }
    }

    // Record a detail attribute describing which shapes in the APEX scene were
    // imported from a USD prim, for use by the Animate state.
    GA_RWHandleDict input_bindings_attrib = scene_gdh.gdpNC()->addDictTuple(
            GA_ATTRIB_DETAIL, "usdinputbindings"_UTsh, 1);
    input_bindings_attrib.set(
            GA_DETAIL_OFFSET, UT_OptionsHolder(std::move(input_bindings)));

#if 0 // Output the result to a bgeo for debugging
    mySceneGdh.gdp()->save("/tmp/scene.bgeo", nullptr);
#endif
}

static void
xusdRecordOutputBindings(
        const XUSD_ApexScene &scene,
        GU_DetailHandle &scene_gdh)
{
    utZoneScopedN("XUSD_ApexScene record output bindings");

    static constexpr UT_StringLit theTypeName("type");
    static constexpr UT_StringLit theShapeTypeName("shape");
    static constexpr UT_StringLit theXformTypeName("xform");

    static constexpr UT_StringLit theOutputPathName("output_path");
    static constexpr UT_StringLit theOutputKeyName("output_key");
    static constexpr UT_StringLit theJointName("joint");

    auto output_bindings = UTmakeUnique<UT_Options>();

    for (const XUSD_ApexCharacter &character : scene.myCharacters)
    {
        const UT_StringHolder rig_output_path
                = character.getRigOutputPath(scene_gdh);
        if (!rig_output_path)
            continue;

        for (const XUSD_ApexShapeBinding &binding : character.myShapeBindings)
        {
            if (!binding.myOutputName)
                continue;

            auto output_info = UTmakeUnique<UT_Options>();
            output_info->setOptionS(
                    theTypeName.asHolder(), theShapeTypeName.asHolder());
            output_info->setOptionS(
                    theOutputPathName.asHolder(), rig_output_path);
            output_info->setOptionS(
                    theOutputKeyName.asHolder(), binding.myOutputName);

            output_bindings->setOptionDict(
                    binding.myPrimPath.GetAsString(),
                    UT_OptionsHolder(std::move(output_info)));
        }

        for (const XUSD_ApexXformBinding &binding : character.myXformBindings)
        {
            auto output_info = UTmakeUnique<UT_Options>();
            output_info->setOptionS(
                    theTypeName.asHolder(), theXformTypeName.asHolder());
            output_info->setOptionS(
                    theOutputPathName.asHolder(), rig_output_path);
            output_info->setOptionS(
                    theOutputKeyName.asHolder(), binding.mySkelOutputName);
            output_info->setOptionS(
                    theJointName.asHolder(), binding.myJointName);

            output_bindings->setOptionDict(
                    binding.myPrimPath.GetAsString(),
                    UT_OptionsHolder(std::move(output_info)));
        }
    }


    // Add a dictionary attribute describing the USD prims bound to rig outputs,
    // for use by the Animate state.
    GA_RWHandleDict output_bindings_attrib = scene_gdh.gdpNC()->addDictTuple(
            GA_ATTRIB_DETAIL, "usdoutputbindings"_UTsh, 1);
    output_bindings_attrib.set(
            GA_DETAIL_OFFSET, UT_OptionsHolder(std::move(output_bindings)));
}

GU_DetailHandle
XUSD_ApexScene::loadSceneGeometry(
        const ShapeGeometryConverter &convert_prim_to_geo) const
{
    GU_DetailHandle scene_gdh;
    scene_gdh.allocateAndSet(new GU_Detail(), /*own=*/true);

    xusdLoadAssetFiles(*this, scene_gdh);
    xusdLoadGeometryInputsFromPrims(*this, scene_gdh, convert_prim_to_geo);
    xusdRecordOutputBindings(*this, scene_gdh);

    return scene_gdh;
}

PXR_NAMESPACE_CLOSE_SCOPE
