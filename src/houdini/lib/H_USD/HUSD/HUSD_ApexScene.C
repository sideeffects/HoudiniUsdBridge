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

#include "HUSD_ApexScene.h"

#include "HUSD_DataHandle.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_Info.h"
#include "HUSD_PathSet.h"
#include "HUSD_TimeCode.h"
#include "HUSD_Xform.h"
#include "XUSD_ApexBakeSceneUtils.h"
#include "XUSD_ApexScene.h"
#include "XUSD_Data.h"
#include "XUSD_Format.h" // IWYU pragma: keep (for UTdebugPrint)
#include "XUSD_LockedGeoRegistry.h"
#include "XUSD_Tokens.h"
#include "XUSD_Utils.h"

#include "UsdHoudini/houdiniApexCharacterAPI.h"
#include "UsdHoudini/houdiniApexCharacterBindingAPI.h"
#include "UsdHoudini/houdiniApexScene.h"
#include "UsdHoudini/houdiniApexShapeBindingAPI.h"
#include "UsdHoudini/houdiniApexXformBindingAPI.h"

#include <APEXA/APEXA_SceneInvoke.h>
#include <GEO/GEO_PrimCamera.h>
#include <GT/GT_RefineParms.h>
#include <GU/GU_Detail.h>
#include <GU/GU_PackedFolders.h>
#include <OP/OP_Node.h>
#include <UT/UT_Algorithm.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_Map.h>
#include <UT/UT_Tracing.h>
#include <UT/UT_WorkBuffer.h>
#include <gusd/GU_USD.h>
#include <gusd/UT_Gf.h>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/sdf/types.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
/// Determines the shape type for the prim, or none if the prim type is
/// unsupported.
/// Note this should match the supported Hydra prim types in XUSD_HydraApexScene
static inline UT_Optional<HUSD_ApexShapeType>
husdGetShapeType(const UsdPrim &prim)
{
    if (UsdGeomMesh(prim))
        return HUSD_ApexShapeType::Mesh;
    else if (UsdGeomCamera(prim))
        return HUSD_ApexShapeType::Camera;

    return UT_NULLOPT;
}

static inline bool
husdIsTransformingShape(HUSD_ApexShapeType shape_type)
{
    switch (shape_type)
    {
        case HUSD_ApexShapeType::Mesh:
            return false;
        case HUSD_ApexShapeType::Camera:
            return true;
    }

    UT_ASSERT_MSG(false, "Unhandled shape type");
    return false;
}

/// Extract and validate the prims which are bound to geometry shape outputs
/// from a rig.
static UT_Array<XUSD_ApexShapeBinding>
husdReadShapeBindings(
        const UsdStageRefPtr &stage,
        const UsdHoudiniHoudiniApexCharacterAPI &char_api,
        XUSD_ApexSceneValidator &validator)
{
    UT_Array<XUSD_ApexShapeBinding> shape_bindings;

    for (const UsdHoudiniHoudiniApexShapeBindingAPI &shape_binding_api :
         UsdHoudiniHoudiniApexShapeBindingAPI::GetAll(char_api.GetPrim()))
    {
        SdfPathVector shape_targets;
        shape_binding_api.GetBindingRel().GetTargets(&shape_targets);

        if (shape_targets.size() != 1 || !shape_targets.front().IsPrimPath())
            continue;

        SdfPath shape_prim_path = shape_targets.front().GetPrimPath();
        UsdPrim shape_prim = stage->GetPrimAtPath(shape_prim_path);
        if (!shape_prim || !shape_prim.IsActive())
            continue;

        UT_Optional<HUSD_ApexShapeType> shape_type
                = husdGetShapeType(shape_prim);
        if (!shape_type)
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Unsupported prim type '{0}' for <{1}>, skipping.",
                    shape_prim.GetTypeName(), shape_prim_path);
            HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
            continue;
        }

        // We can't edit instance proxies.
        if (shape_prim.IsInstanceProxy())
        {
            HUSD_ErrorScope::addWarning(
                    HUSD_ERR_IGNORING_INSTANCE_PROXY,
                    shape_prim_path.GetText());
            continue;
        }

        XUSD_ApexShapeBinding shape_binding;
        shape_binding.myPrimPath = shape_prim_path;
        shape_binding.myShapeType = *shape_type;

        std::string rig_input;
        shape_binding_api.GetInputAttr().Get(&rig_input);
        shape_binding.myInputName = rig_input;

        std::string rig_output;
        shape_binding_api.GetOutputAttr().Get(&rig_output);
        shape_binding.myOutputName = rig_output;

        // Verify that the shape is only driven by one output.
        if (shape_binding.myOutputName
            && !validator.addOutputPrim(shape_prim_path))
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
husdReadXformBindings(
        const UsdStageRefPtr &stage,
        const UsdHoudiniHoudiniApexCharacterAPI &char_api,
        XUSD_ApexSceneValidator &validator)
{
    UT_Array<XUSD_ApexXformBinding> xform_bindings;

    for (const UsdHoudiniHoudiniApexXformBindingAPI &xform_binding_api :
         UsdHoudiniHoudiniApexXformBindingAPI::GetAll(char_api.GetPrim()))
    {
        SdfPathVector xform_targets;
        xform_binding_api.GetBindingRel().GetTargets(&xform_targets);

        if (xform_targets.size() != 1 || !xform_targets.front().IsPrimPath())
            continue;

        SdfPath xform_prim_path = xform_targets.front().GetPrimPath();
        UsdPrim xform_prim = stage->GetPrimAtPath(xform_prim_path);
        if (!xform_prim || !xform_prim.IsActive())
            continue;

        // Currently we only support adjusting point-based prims by
        // writing back deformed positions and normals.
        if (!UsdGeomXformable(xform_prim))
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Primitive '{0}' is not {1}, skipping.", xform_prim_path,
                    UsdGeomTokens->Xformable);
            HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
            continue;
        }

        // We can't edit instance proxies.
        if (xform_prim.IsInstanceProxy())
        {
            HUSD_ErrorScope::addWarning(
                    HUSD_ERR_IGNORING_INSTANCE_PROXY,
                    xform_prim_path.GetText());
            continue;
        }

        XUSD_ApexXformBinding xform_binding;
        xform_binding.myPrimPath = xform_prim_path;

        std::string joint_name;
        xform_binding_api.GetJointAttr().Get(&joint_name);
        xform_binding.myJointName = joint_name;

        std::string rig_output;
        xform_binding_api.GetOutputAttr().Get(&rig_output);
        xform_binding.mySkelOutputName = rig_output;

        // Verify that the prim is only driven by one output.
        if (!validator.addOutputPrim(xform_prim_path))
            continue;

        xform_bindings.append(std::move(xform_binding));
    }

    return xform_bindings;
}

/// Collect a list of the APEX scenes (and their characters / shapes), and
/// perform some validation (e.g. a prim can't be affected by multiple APEX
/// scenes).
static UT_Array<XUSD_ApexScene>
husdFindScenes(
        const HUSD_AutoAnyLock &read_lock,
        const HUSD_PathSet &scene_prim_paths)
{
    utZoneScopedN("Finding APEX scenes on stage");

    UT_ASSERT(read_lock.isStageValid());
    UsdStageRefPtr stage = read_lock.constData()->stage();

    UT_Array<XUSD_ApexScene> scenes;
    XUSD_ApexSceneValidator validator;

    for (const HUSD_Path &scene_prim_path : scene_prim_paths)
    {
        auto scene_prim = UsdHoudiniHoudiniApexScene::Get(
                stage, scene_prim_path.sdfPath());
        if (!scene_prim)
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Primitive '{0}' is not a {1}", scene_prim_path.sdfPath(),
                    UsdHoudiniTokens->HoudiniApexScene);
            HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
            continue;
        }

        if (!scene_prim.GetPrim().IsActive())
            continue;

        validator.setCurrentScenePath(scene_prim_path.sdfPath());

        XUSD_ApexScene scene;
        scene_prim.GetSceneFilesAttr().Get(&scene.myFiles);
        scene_prim.GetInheritAnimationLayersAttr().Get(&scene.myInheritLayers);

        if (scene.myInheritLayers.size() != scene.myFiles.size())
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Invalid number of values for '{0}' attribute on primitive "
                    "'{1}', expected {2} but got {3}",
                    UsdHoudiniTokens->inheritAnimationLayers,
                    scene_prim_path.sdfPath(), scene.myFiles.size(),
                    scene.myInheritLayers.size());
            HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
            continue;
        }

        // Record all of the characters bound to this scene.
        // The referenced prim must have the HoudiniApexCharacterAPI applied.
        for (const UsdHoudiniHoudiniApexCharacterBindingAPI &char_binding :
             UsdHoudiniHoudiniApexCharacterBindingAPI::GetAll(
                     scene_prim.GetPrim()))
        {
            XUSD_ApexCharacter character(char_binding.GetName().GetString());

            SdfPathVector char_targets;
            char_binding.GetBindingRel().GetForwardedTargets(&char_targets);
            if (char_targets.size() != 1 || !char_targets.front().IsPrimPath())
                continue;

            SdfPath char_prim_path = char_targets.front().GetPrimPath();
            auto char_api = UsdHoudiniHoudiniApexCharacterAPI::Get(
                    stage, char_prim_path);
            if (!char_api)
            {
                UT_WorkBuffer msg;
                msg.format(
                        "Primitive '{0}' is missing required {1} schema",
                        char_prim_path,
                        UsdHoudiniTokens->HoudiniApexCharacterAPI);
                HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
                continue;
            }

            if (!char_api.GetPrim().IsActive())
                continue;

            char_api.GetFilesAttr().Get(&character.myFiles);

            // The CharacterBindingAPI takes precedence for the rig selection.
            std::string rig_name;
            char_binding.GetRigAttr().Get(&rig_name);
            if (rig_name.empty())
                char_api.GetRigAttr().Get(&rig_name);

            character.myRigName = rig_name;

            character.myXformBindings = husdReadXformBindings(
                    stage, char_api, validator);
            character.myShapeBindings = husdReadShapeBindings(
                    stage, char_api, validator);

            scene.myCharacters.append(std::move(character));
        }

        scenes.append(std::move(scene));
    }

    return scenes;
}

static GU_ConstDetailHandle
husdConvertPrimToGeo(
        const UsdStageConstRefPtr &stage,
        const SdfPath &prim_path,
        const HUSD_ApexScene::GeoImportOptions &import_options)
{
    UsdPrim prim = stage->GetPrimAtPath(prim_path);
    UT_ASSERT(prim.IsValid());
    if (!prim)
        return GU_ConstDetailHandle();

    GU_DetailHandle shape_gdh;
    shape_gdh.allocateAndSet(new GU_Detail());

    // In the future we could provide a property similar to UsdSkel's
    // geomBindTransform, but for now we require that the shapes' local
    // points are already at the bind pose.
    UT_Matrix4D bind_xform(1.0);

    GT_RefineParms refine_parms;
    // Skip adding the file path attribute, which is random (pointer
    // value) for stages from LOPs. We also don't need the usdxform
    // or usdmaterialpath point attributes which are used for SOP Modify
    // workflows.
    refine_parms.set(GUSD_REFINE_ADDXFORMATTRIB, false);
    refine_parms.set(GUSD_REFINE_ADDMATERIALPATHATTRIB, false);
    refine_parms.set(GUSD_REFINE_ADDPATHATTRIB, false);

    if (import_options.myPathAttrib)
    {
        refine_parms.set(GUSD_REFINE_ADDPRIMPATHATTRIB, true);
        refine_parms.set(
                GUSD_REFINE_PRIMPATHATTRIB, import_options.myPathAttrib);
    }
    else
        refine_parms.set(GUSD_REFINE_ADDPRIMPATHATTRIB, false);
    

    const GusdPurposeSet purposes = GusdPurposeSetFromMask("*");

    if (!GusdGU_USD::ImportPrimUnpacked(
                *shape_gdh.gdpNC(), prim,
                /*time=*/UsdTimeCode::EarliestTime(),
                /*lod=*/nullptr, purposes, /*primvarPattern=*/"*"_UTsh,
                /*attributePattern=*/UT_StringHolder::theEmptyString,
                /*translateSTtoUV=*/true,
                /*nonTransformingPrimvarPattern=*/GA_Names::rest, &bind_xform,
                &refine_parms))
    {
        return GU_ConstDetailHandle();
    }

    return shape_gdh;
}

GU_DetailHandle
husdLoadSceneGeometry(
        const XUSD_ApexScene &scene,
        const HUSD_AutoAnyLock &read_lock,
        const HUSD_ApexScene::GeoImportOptions &import_options)
{
    utZoneScopedN("HUSD_ApexScene load scene geometry");

    UT_ASSERT(read_lock.isStageValid());
    UsdStageConstRefPtr stage = read_lock.constData()->stage();
    auto convert_prim = [&](const SdfPath &prim_path)
    {
        return husdConvertPrimToGeo(stage, prim_path, import_options);
    };

    return scene.loadSceneGeometry(convert_prim);
}

template <typename T>
static bool
husdSetAttrib(
        UsdPrim &prim,
        const TfToken &attrib_name,
        const SdfValueTypeName &type_name,
        const T &attrib_value,
        UsdTimeCode time_code)
{
    UsdAttribute attrib = prim.CreateAttribute(
            attrib_name, type_name,
            /*custom=*/false);
    if (!attrib)
        return false;

    if (!attrib.Set(attrib_value, time_code))
        return false;

    HUSDclearDataId(attrib);
    return true;
}

/// Basic implementation of writing back deformed point positions and
/// normals from the APEX output geometry.
void
husdUpdateMeshFromGeo(
        const UsdStageRefPtr &stage,
        const HUSD_Path &prim_path,
        const UT_Matrix4D &prim_world_xform,
        const UsdTimeCode &time_code,
        const GU_Detail &detail)
{
    utZoneScopedN("HUSD_ApexScene writeback to mesh");

    UT_Matrix4D inv_prim_world_xform;
    prim_world_xform.invert(inv_prim_world_xform);

    // Read attributes from the APEX output geometry.

    GA_ROHandleV3 p_attrib = detail.getP();
    VtVec3fArray positions;
    XUSD_ApexBakeSceneUtils::convertAttribute(
            detail, p_attrib, &inv_prim_world_xform, positions);

    const GfRange3f extent
            = XUSD_ApexBakeSceneUtils::computeExtentFromPoints(positions);

    GA_ROHandleV3 n_attrib(
            detail.findFloatTuple(GA_ATTRIB_VERTEX, GA_Names::N, 3));
    if (!n_attrib.isValid())
        n_attrib = detail.findFloatTuple(GA_ATTRIB_POINT, GA_Names::N, 3);

    VtVec3fArray normals;
    if (n_attrib.isValid())
    {
        // TODO - for vertex attribs, this assumes the SOP winding order matches
        // the USD prim.
        XUSD_ApexBakeSceneUtils::convertAttribute(
                detail, n_attrib, &inv_prim_world_xform, normals);
    }

    // Write out the USD attributes.

    UsdPrim prim = stage->OverridePrim(prim_path.sdfPath());

    husdSetAttrib(
            prim, UsdGeomTokens->points, SdfValueTypeNames->Vector3fArray,
            positions, time_code);

    VtVec3fArray extent_array = {extent.GetMin(), extent.GetMax()};
    husdSetAttrib(
            prim, UsdGeomTokens->extent, SdfValueTypeNames->Vector3fArray,
            extent_array, time_code);

    if (n_attrib.isValid())
    {
        husdSetAttrib(
                prim, UsdGeomTokens->normals, SdfValueTypeNames->Vector3fArray,
                normals, time_code);
    }
}

/// Write back animated camera properties from the APEX output geometry.
void
husdUpdateCameraFromGeo(
        const UsdStageRefPtr &stage,
        const HUSD_Path &prim_path,
        const UsdTimeCode &time_code,
        const GU_Detail &detail)
{
    utZoneScopedN("HUSD_ApexScene writeback camera to prim");

    const GEO_PrimCamera *geo_camera = nullptr;
    if (detail.getNumPrimitives() == 1)
    {
        GA_Offset primoff = detail.primitiveOffset(0);
        if (detail.getPrimitiveTypeId(primoff) == GA_PRIMCAMERA)
        {
            geo_camera = UTverify_cast<const GEO_PrimCamera *>(
                    detail.getPrimitive(primoff));
        }
    }

    if (!geo_camera)
    {
        UT_WorkBuffer msg;
        msg.format(
                "{0}: output geometry is expected to contain a single camera",
                prim_path.sdfPath());
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        return;
    }

    XUSD_CameraParms camera_parms;
    HUSDconvertCameraParms(geo_camera->getParms(), camera_parms);

    UsdPrim prim = stage->OverridePrim(prim_path.sdfPath());

    // Note: for now we just support animating typical camera parameters,
    // ignoring shutter range and any custom attributes.
    TfToken projection;
    switch (camera_parms.myCamera.GetProjection())
    {
        case GfCamera::Perspective:
            projection = UsdGeomTokens->perspective;
            break;
        case GfCamera::Orthographic:
            projection = UsdGeomTokens->orthographic;
            break;
    }
    husdSetAttrib(
            prim, UsdGeomTokens->projection, SdfValueTypeNames->Token,
            projection, time_code);

    husdSetAttrib(
            prim, UsdGeomTokens->focalLength, SdfValueTypeNames->Float,
            camera_parms.myCamera.GetFocalLength(), time_code);
    husdSetAttrib(
            prim, UsdGeomTokens->horizontalAperture, SdfValueTypeNames->Float,
            camera_parms.myCamera.GetHorizontalAperture(), time_code);
    husdSetAttrib(
            prim, UsdGeomTokens->verticalAperture, SdfValueTypeNames->Float,
            camera_parms.myCamera.GetVerticalAperture(), time_code);
    husdSetAttrib(
            prim, UsdGeomTokens->horizontalApertureOffset,
            SdfValueTypeNames->Float,
            camera_parms.myCamera.GetHorizontalApertureOffset(), time_code);
    husdSetAttrib(
            prim, UsdGeomTokens->verticalApertureOffset,
            SdfValueTypeNames->Float,
            camera_parms.myCamera.GetVerticalApertureOffset(), time_code);
    husdSetAttrib(
            prim, UsdGeomTokens->clippingRange, SdfValueTypeNames->Float2,
            GfVec2f(camera_parms.myCamera.GetClippingRange().GetMin(),
                    camera_parms.myCamera.GetClippingRange().GetMax()),
            time_code);
    husdSetAttrib(
            prim, UsdGeomTokens->focusDistance, SdfValueTypeNames->Float,
            camera_parms.myCamera.GetFocusDistance(), time_code);
    husdSetAttrib(
            prim, UsdGeomTokens->fStop, SdfValueTypeNames->Float,
            camera_parms.myCamera.GetFStop(), time_code);
}

/// Use the evaluated skeleton geometry to adjust the transforms of Xformable
/// prims bound to the joints.
HUSD_XformEntryMap
husdComputeXformAdjustments(
        const APEXA_SceneInvoke &scene,
        const UT_Map<HUSD_Path, exint> &xforming_prim_index,
        const UT_Array<exint> &xforming_prim_parents,
        const UT_Array<HUSD_ApexScene::ShapeOutputInfo> &shape_outputs,
        const UT_Array<HUSD_ApexScene::XformOutputInfo> &xform_outputs,
        const UT_Span<const UT_Matrix4D> &orig_world_xforms,
        const UT_Span<const UT_Matrix4D> &orig_local_xforms,
        HUSD_TimeCode time_code,
        bool use_xform_common_api)
{
    UT_Array<UT_Matrix4D> prim_world_xforms;
    prim_world_xforms.setSize(xforming_prim_index.size());

    // Evaluate world transforms for prims which are bound to skeleton joints.
    for (const HUSD_ApexScene::XformOutputInfo &xform_info : xform_outputs)
    {
        const exint prim_idx = xforming_prim_index.get(
                xform_info.myPrimPath, -1);
        UT_ASSERT(prim_idx >= 0);
        UT_Matrix4D &prim_world_xform = prim_world_xforms[prim_idx];

        const APEXA_SceneInvoke::Output &output
                = scene.getOutputs()[xform_info.mySceneOutputIdx];

        GU_ConstDetailHandle output_gdh;
        if (output.myGeometry)
            output_gdh = output.myGeometry->asConstHandle();

        if (!output_gdh.isValid())
        {
            prim_world_xform.identity();
            continue;
        }

        const GU_Detail &skel_geo = *output_gdh.gdp();

        if (!XUSD_ApexBakeSceneUtils::getSkelJointXform(
                    xform_info.myPrimPath.sdfPath(), skel_geo,
                    xform_info.myJointName, prim_world_xform))
        {
            prim_world_xform.identity();
        }
    }

    // Handle any shapes which are transforming prims (e.g. cameras)
    for (const HUSD_ApexScene::ShapeOutputInfo &shape_info : shape_outputs)
    {
        if (!husdIsTransformingShape(shape_info.myShapeType))
            continue;

        const exint prim_idx = xforming_prim_index.get(
                shape_info.myPrimPath, -1);
        UT_ASSERT(prim_idx >= 0);
        UT_Matrix4D &prim_world_xform = prim_world_xforms[prim_idx];

        const APEXA_SceneInvoke::Output &output
                = scene.getOutputs()[shape_info.mySceneOutputIdx];

        GU_ConstDetailHandle output_gdh;
        if (output.myGeometry)
            output_gdh = output.myGeometry->asConstHandle();

        if (!output_gdh.isValid())
        {
            prim_world_xform.identity();
            continue;
        }

        const GU_Detail &shape_geo = *output_gdh.gdp();
        UT_ASSERT(shape_geo.getNumPrimitives() == 1);
        if (shape_geo.getNumPrimitives() != 1)
        {
            prim_world_xform.identity();
            continue;
        }

        const GA_Primitive *prim
                = shape_geo.getPrimitive(shape_geo.primitiveOffset(0));
        UT_ASSERT(prim->hasLocalTransform());
        prim->getLocalTransform4(prim_world_xform);
    }

    // Compute the local transform deltas from the evaluated world transforms
    // and populate the HUSD_XformEntryMap.
    HUSD_XformEntryMap xform_map;
    xform_map.reserve(prim_world_xforms.size());
    for (auto &&[prim_path, prim_idx] : xforming_prim_index)
    {
        UT_Matrix4D xform = prim_world_xforms[prim_idx];

        const exint parent_idx = xforming_prim_parents[prim_idx];
        if (parent_idx >= 0)
        {
            UT_Matrix4D parent_xform_inv;
            prim_world_xforms[parent_idx].invert(parent_xform_inv);
            xform *= parent_xform_inv;
        }

        UT_Matrix4D orig_xform = orig_world_xforms[prim_idx];
        if (parent_idx >= 0)
        {
            UT_Matrix4D orig_parent_xform_inv;
            orig_world_xforms[parent_idx].invert(orig_parent_xform_inv);
            orig_xform *= orig_parent_xform_inv;
        }

        UT_Matrix4D orig_prim_xform_inv;
        orig_xform.invert(orig_prim_xform_inv);
        xform *= orig_prim_xform_inv;

        // For XformCommonAPI, we author an updated local transform rather than
        // adding a new xformOp.
        if (use_xform_common_api)
            xform *= orig_local_xforms[prim_idx];

        HUSD_XformEntry entry{xform, time_code};
        xform_map[prim_path.pathStr()].append(entry);
    }

    return xform_map;
}

} // namespace

HUSD_ApexScene::HUSD_ApexScene()
{
    myScene = UTmakeUnique<APEXA_SceneInvoke>();
}

HUSD_ApexScene::~HUSD_ApexScene() = default;

bool
HUSD_ApexScene::loadFromGeometry(const GU_ConstDetailHandle &scene_gdh)
{
    UT_StringHolder errors;
    if (!myScene->updateSourceGeometry(*scene_gdh.gdp(), errors))
    {
        UT_WorkBuffer msg;
        msg.format("Failed to load APEX scene:\n{0}", errors);
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    return true;
}

bool
HUSD_ApexScene::loadScenes(
        const HUSD_AutoAnyLock &read_lock,
        const HUSD_FindPrims &find_prims,
        const GeoImportOptions &import_options,
        UT_Array<HUSD_ApexScene> &scenes)
{
    UT_Array<XUSD_ApexScene> scene_datas = husdFindScenes(
            read_lock, find_prims.getExpandedPathSet());

    for (const XUSD_ApexScene &scene_data : scene_datas)
    {
        HUSD_ApexScene &scene = scenes[scenes.append()];

        GU_DetailHandle scene_gdh = husdLoadSceneGeometry(
                scene_data, read_lock, import_options);
        if (!scene.loadFromGeometry(scene_gdh))
            return false;

        // Add the outputs we're interested in evaluating.
        exint num_xforming_prims = 0;
        for (const XUSD_ApexCharacter &character : scene_data.myCharacters)
        {
            // Determine which rig the character should use.
            UT_StringHolder rig_output_path
                    = character.getRigOutputPath(scene_gdh);
            if (!rig_output_path)
            {
                UT_WorkBuffer msg;
                msg.format(
                        "Character '{0}' does not have any rigs.",
                        character.myScenePath);
                HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
                continue;
            }

            // Set up the output shape bindings for the character.
            for (const XUSD_ApexShapeBinding &binding :
                 character.myShapeBindings)
            {
                if (!binding.myOutputName)
                    continue;

                exint output_idx = scene.myScene->addOutput(
                        rig_output_path, binding.myOutputName,
                        /*copies_geo=*/false, /*track_output=*/true);
                scene.myShapeOutputs.append(
                        {output_idx, binding.myPrimPath, binding.myShapeType});

                if (husdIsTransformingShape(binding.myShapeType))
                {
                    scene.myXformingPrimIndex[binding.myPrimPath]
                            = num_xforming_prims++;
                }
            }

            // It's likely that we may have multiple prims bound to different
            // joints of the same skeleton, so avoid registering duplicate
            // outputs.
            UT_StringMap<exint> known_skel_outputs;
            for (const XUSD_ApexXformBinding &binding :
                 character.myXformBindings)
            {
                const exint output_idx = UTfindOrInsert(
                        known_skel_outputs, binding.mySkelOutputName,
                        [&]()
                        {
                            return scene.myScene->addOutput(
                                    rig_output_path, binding.mySkelOutputName,
                                    /*copies_geo=*/false,
                                    /*track_output=*/true);
                        });

                scene.myXformOutputs.append(
                        {output_idx, binding.myPrimPath, binding.myJointName});
                scene.myXformingPrimIndex[binding.myPrimPath]
                        = num_xforming_prims++;
            }
        }

        // Final pass to record parenting between transforming prims, to ensure
        // that transform changes end up in the correct space.
        scene.myXformingPrimParents.setSizeNoInit(
                scene.myXformingPrimIndex.size());
        scene.myXformingPrimParents.constant(-1);

        for (auto &&[path, path_idx] : scene.myXformingPrimIndex)
        {
            HUSD_Path parent_path = path.parentPath();
            while (!parent_path.isEmpty())
            {
                auto it = scene.myXformingPrimIndex.find(parent_path);
                if (it != scene.myXformingPrimIndex.end())
                {
                    scene.myXformingPrimParents[path_idx] = it->second;
                    break;
                }

                parent_path = parent_path.parentPath();
            }
        }
    }

    return true;
}

GU_DetailHandle
HUSD_ApexScene::loadSceneGeometry(
        const HUSD_AutoAnyLock &read_lock,
        const HUSD_Path &scene_path,
        const GeoImportOptions &import_options)
{
    HUSD_PathSet path_set;
    path_set.insert(scene_path);

    UT_Array<XUSD_ApexScene> scene_datas = husdFindScenes(
            read_lock, path_set);
    if (scene_datas.size() != 1)
        return GU_DetailHandle();

    const XUSD_ApexScene &scene_data = scene_datas[0];
    return husdLoadSceneGeometry(scene_data, read_lock, import_options);
}

bool
HUSD_ApexScene::evaluateOutputs(
        HUSD_AutoWriteLock &write_lock,
        const UT_SortedSet<fpreal> &samples,
        const UT_StringRef &xform_description,
        bool use_xform_common_api)
{
    utZoneScopedN("HUSD_ApexScene evaluate outputs");

    UT_ASSERT(write_lock.isStageValid());
    UsdStageRefPtr stage = write_lock.data()->stage();

    // For prims that are transforming, store the original world xform at each
    // of our time samples so that we can apply suitable delta xformOps.
    UT_PackedArrayOfArrays<UT_Matrix4D> old_world_xforms;
    UT_PackedArrayOfArrays<UT_Matrix4D> old_local_xforms;
    for (fpreal frame : samples)
    {
        const HUSD_TimeCode time_code(frame);
        UsdGeomXformCache xform_cache(HUSDgetUsdTimeCode(time_code));

        UT_Matrix4D *world_xform_list
                = old_world_xforms.appendArray(myXformingPrimIndex.size());
        UT_Matrix4D *local_xform_list
                = old_local_xforms.appendArray(myXformingPrimIndex.size());

        for (auto &&[prim_path, prim_idx] : myXformingPrimIndex)
        {
            UsdPrim prim = stage->GetPrimAtPath(prim_path.sdfPath());
            UT_ASSERT(prim);

            world_xform_list[prim_idx] = GusdUT_Gf::Cast(
                    xform_cache.GetLocalToWorldTransform(prim));

            bool is_reset;
            local_xform_list[prim_idx] = GusdUT_Gf::Cast(
                    xform_cache.GetLocalTransformation(prim, &is_reset));
        }
    }

    UT_WorkBuffer interrupt_msg;

    exint sample_idx = 0;
    for (fpreal frame : samples)
    {
        interrupt_msg.format("Baking frame {0}", frame);
        UT_AutoInterrupt interrupt(interrupt_msg.buffer());
        if (interrupt.wasInterrupted())
            return false;

        // Set the evaluation time to the next sample frame, and evaluate all
        // our outputs.
        myScene->updateEvaluationTime(frame);

        for (exint i = 0, n = myScene->getOutputs().size(); i < n; ++i)
        {
            UT_StringHolder error;
            if (!myScene->evaluateOutput(i, error))
            {
                const APEXA_SceneInvoke::Output &output
                        = myScene->getOutputs()[i];
                UT_WorkBuffer msg;
                msg.format(
                        "Failed to evaluate output {0} {1}\n{2}", output.myPath,
                        output.myKey.value_or(UT_StringHolder::theEmptyString),
                        error);
                HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
                return false;
            }
        }

        // Update the primitives bound to a scene output.
        const HUSD_TimeCode time_code(frame);

        // First, update any transforming prims.
        if (!myXformingPrimIndex.empty())
        {
            HUSD_XformEntryMap xform_map = husdComputeXformAdjustments(
                    *myScene, myXformingPrimIndex, myXformingPrimParents,
                    myShapeOutputs, myXformOutputs,
                    old_world_xforms.span(sample_idx),
                    old_local_xforms.span(sample_idx), time_code,
                    use_xform_common_api);

            HUSD_Xform xformer(write_lock);
            xformer.setClearExistingFlag(sample_idx == 0);
            xformer.applyXforms(
                    xform_map, xform_description, 
                    use_xform_common_api ? HUSD_XFORM_COMMON_API_OVERWRITE : 
                        HUSD_XFORM_OVERWRITE_APPEND);
        }

        // Next, update any deforming shapes.
        HUSD_Info info(write_lock);
        for (const ShapeOutputInfo &shape_info : myShapeOutputs)
        {
            const APEXA_SceneInvoke::Output &output
                    = myScene->getOutputs()[shape_info.mySceneOutputIdx];

            GU_ConstDetailHandle output_gdh;
            if (output.myGeometry)
                output_gdh = output.myGeometry->asConstHandle();

            if (!output_gdh.isValid())
                continue;

            const HUSD_Path &prim_path = shape_info.myPrimPath;

            if (shape_info.myShapeType == HUSD_ApexShapeType::Mesh)
            {
                const UT_Matrix4D prim_world_xform = info.getWorldXform(
                        prim_path.pathStr(), time_code);

                husdUpdateMeshFromGeo(
                        stage, prim_path, prim_world_xform,
                        HUSDgetUsdTimeCode(time_code), *output_gdh.gdp());
            }
            else
            {
                husdUpdateCameraFromGeo(
                        stage, prim_path, HUSDgetUsdTimeCode(time_code),
                        *output_gdh.gdp());
            }
        }

        ++sample_idx;
    }

    return true;
}
