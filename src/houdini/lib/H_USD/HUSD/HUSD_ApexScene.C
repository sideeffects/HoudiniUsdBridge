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
#include "XUSD_UsdApexScene.h"
#include "XUSD_Utils.h"

#include <APEXA/APEXA_SceneInvoke.h>
#include <GEO/GEO_PrimCamera.h>
#include <GU/GU_Detail.h>
#include <UT/UT_Algorithm.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_Map.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_Tracing.h>
#include <UT/UT_WorkBuffer.h>
#include <gusd/UT_Gf.h>

#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/usd/sdf/types.h>

PXR_NAMESPACE_USING_DIRECTIVE

struct HUSD_ApexScene::ShapeOutputInfo
{
    exint mySceneOutputIdx = -1;
    HUSD_Path myPrimPath;
    HUSD_ApexShapeType myShapeType;
    UT_Array<XUSD_ApexShapePrimvarInfo> myPrimvars;
};

struct HUSD_ApexScene::XformOutputInfo
{
    exint mySceneOutputIdx = -1;
    HUSD_Path myPrimPath;
    UT_StringHolder myJointName;
};

namespace
{
static inline bool
husdIsTransformingShape(HUSD_ApexShapeType shape_type)
{
    switch (shape_type)
    {
        case HUSD_ApexShapeType::Mesh:
        case HUSD_ApexShapeType::Points:
        case HUSD_ApexShapeType::BasisCurves:
        case HUSD_ApexShapeType::GSplats:
            return false;
        case HUSD_ApexShapeType::Camera:
            return true;
    }

    UT_ASSERT_MSG(false, "Unhandled shape type");
    return false;
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

template <typename T>
static bool
husdSetPrimvar(
        UsdPrim &prim,
        const TfToken &attrib_name,
        const SdfValueTypeName &type_name,
        const T &attrib_value,
        const TfToken &interpolation,
        UsdTimeCode time_code,
        bool author_interpolation)
{
    if (!husdSetAttrib(prim, attrib_name, type_name, attrib_value, time_code))
        return false;

    if (author_interpolation)
    {
        UsdAttribute attrib = prim.GetAttribute(attrib_name);
        attrib.SetMetadata(UsdGeomTokens->interpolation, interpolation);
    }

    return true;
}

/// Writes back the deformed primvars (e.g. point positions and normals) for a
/// point-based prim from the APEX output geometry, and updates its extent.
void
husdUpdatePointBasedPrimFromGeo(
        const UsdStageRefPtr &stage,
        const HUSD_Path &prim_path,
        UT_Span<const XUSD_ApexShapePrimvarInfo> primvars,
        const UT_Matrix4D &prim_world_xform,
        const UsdTimeCode &time_code,
        const GU_Detail &detail)
{
    utZoneScopedN("HUSD_ApexScene writeback to point-based prim");

    UT_Matrix4D inv_prim_world_xform;
    prim_world_xform.invert(inv_prim_world_xform);

    UsdPrim prim = stage->OverridePrim(prim_path.sdfPath());

    VtVec3fArray positions;
    for (const XUSD_ApexShapePrimvarInfo &primvar_info : primvars)
    {
        GA_ROHandleV3 attrib(detail.findFloatTuple(
                primvar_info.myOwner, primvar_info.myName, 3));
        if (!attrib.isValid())
            continue;

        // TODO - for vertex attribs, this assumes the SOP winding order matches
        // the USD prim.
        VtVec3fArray values;
        XUSD_ApexBakeSceneUtils::convertAttribute(
                detail, attrib, &inv_prim_world_xform, values);

        const bool is_points = primvar_info.myName == GA_Names::P;
        husdSetPrimvar(
                prim, primvar_info.myUsdName, SdfValueTypeNames->Vector3fArray,
                values, primvar_info.myInterpolation, time_code,
                /*author_interpolation=*/!is_points);

        if (is_points)
            positions = std::move(values);
    }

    if (!positions.empty())
    {
        // TODO - incorporate widths when computing extent for points / curves
        const GfRange3f extent
                = XUSD_ApexBakeSceneUtils::computeExtentFromPoints(positions);

        VtVec3fArray extent_array = {extent.GetMin(), extent.GetMax()};
        husdSetAttrib(
                prim, UsdGeomTokens->extent, SdfValueTypeNames->Vector3fArray,
                extent_array, time_code);
    }
}

/// Write back animated gaussian splat positions and orientations.
void
husdUpdateGSplatsFromGeo(
        const UsdStageRefPtr &stage,
        const HUSD_Path &prim_path,
        const UT_Matrix4D &prim_world_xform,
        const UsdTimeCode &time_code,
        const GU_Detail &detail)
{
    utZoneScopedN("HUSD_ApexScene writeback GSplats");

    UT_Matrix4D inv_prim_world_xform;
    prim_world_xform.invert(inv_prim_world_xform);

    UsdPrim prim = stage->OverridePrim(prim_path.sdfPath());

    // Write back 16 or 32-bit positions depending on the P attrib's precision.
    GA_ROHandleV3 p_attrib = detail.getP();

    GfRange3f extent;
    if (p_attrib->getStorage() == GA_STORE_REAL16)
    {
        GA_ROHandleT<UT_Vector3H> ph_attrib(detail.getP());

        VtArray<GfVec3h> positionsh;
        XUSD_ApexBakeSceneUtils::convertAttribute(
                detail, ph_attrib, &inv_prim_world_xform, positionsh);

        husdSetAttrib(
                prim, UsdVolTokens->positionsh,
                SdfValueTypeNames->Vector3hArray, positionsh, time_code);

        extent = XUSD_ApexBakeSceneUtils::computeExtentFromPoints(positionsh);
    }
    else
    {
        VtVec3fArray positions;
        XUSD_ApexBakeSceneUtils::convertAttribute(
                detail, p_attrib, &inv_prim_world_xform, positions);

        husdSetAttrib(
                prim, UsdVolTokens->positions,
                SdfValueTypeNames->Vector3fArray, positions, time_code);

        extent = XUSD_ApexBakeSceneUtils::computeExtentFromPoints(positions);
    }

    // Author the updated extent.
    VtVec3fArray extent_array = {extent.GetMin(), extent.GetMax()};
    husdSetAttrib(
            prim, UsdGeomTokens->extent, SdfValueTypeNames->Vector3fArray,
            extent_array, time_code);

    GA_ROHandleQ orient_attrib = detail.findAttribute(
            GA_ATTRIB_POINT, GA_Names::orient);
    if (!orient_attrib.isValid())
        return;

    // Write back 16 or 32-bit orientations.
    if (orient_attrib->getStorage() == GA_STORE_REAL16)
    {
        GA_ROHandleT<UT_QuaternionH> orienth_attrib(
                orient_attrib.getAttribute());

        VtArray<GfQuath> orientationsh;
        XUSD_ApexBakeSceneUtils::convertAttribute(
                detail, orienth_attrib, &inv_prim_world_xform, orientationsh);
        husdSetAttrib(
                prim, UsdVolTokens->orientationsh,
                SdfValueTypeNames->QuathArray, orientationsh, time_code);
    }
    else
    {
        VtArray<GfQuatf> orientations;
        XUSD_ApexBakeSceneUtils::convertAttribute(
                detail, orient_attrib, &inv_prim_world_xform, orientations);
        husdSetAttrib(
                prim, UsdVolTokens->orientations,
                SdfValueTypeNames->QuatfArray, orientations, time_code);
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
    UT_ASSERT(read_lock.isStageValid());
    const UsdStageRefPtr stage = read_lock.constData()->stage();

    XUSD_UsdApexSceneImportOptions usd_import_options;
    usd_import_options.myPathAttrib = import_options.myPathAttrib;
    XUSD_ApexSceneValidator validator;

    for (const HUSD_Path &scene_path : find_prims.getExpandedPathSet())
    {
        XUSD_UsdApexScene usd_scene(scene_path.sdfPath());
        if (!usd_scene.load(stage, validator, usd_import_options))
            continue;

        const XUSD_ApexScene &scene_data = usd_scene.getSceneInfo();
        HUSD_ApexScene &scene = scenes[scenes.append()];

        const GU_DetailHandle &scene_gdh = usd_scene.getSceneGeometry();
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
                        {output_idx, binding.myPrimPath, binding.myShapeType,
                         UT_Array<XUSD_ApexShapePrimvarInfo>(
                                 binding.myPrimvars)});

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
    UT_ASSERT(read_lock.isStageValid());

    XUSD_UsdApexSceneImportOptions usd_import_options;
    usd_import_options.myPathAttrib = import_options.myPathAttrib;
    XUSD_ApexSceneValidator validator;
    XUSD_UsdApexScene usd_scene(scene_path.sdfPath());
    if (!usd_scene.load(
                read_lock.constData()->stage(), validator, usd_import_options))
    {
        return GU_DetailHandle();
    }

    return usd_scene.getSceneGeometry();
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
                // Avoid reporting duplicate errors.
                if (sample_idx == 0)
                {
                    const APEXA_SceneInvoke::Output &output
                            = myScene->getOutputs()[i];
                    UT_WorkBuffer msg;
                    msg.format(
                            "Failed to evaluate output {0} {1}\n{2}",
                            output.myPath,
                            output.myKey.value_or(
                                    UT_StringHolder::theEmptyString),
                            error);
                    HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
                }
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

            switch (shape_info.myShapeType)
            {
                case HUSD_ApexShapeType::Mesh:
                case HUSD_ApexShapeType::Points:
                case HUSD_ApexShapeType::BasisCurves:
                {
                    const UT_Matrix4D prim_world_xform = info.getWorldXform(
                            prim_path.pathStr(), time_code);

                    husdUpdatePointBasedPrimFromGeo(
                            stage, prim_path, shape_info.myPrimvars,
                            prim_world_xform, HUSDgetUsdTimeCode(time_code),
                            *output_gdh.gdp());
                    break;
                }
                case HUSD_ApexShapeType::GSplats:
                {
                    const UT_Matrix4D prim_world_xform = info.getWorldXform(
                            prim_path.pathStr(), time_code);

                    husdUpdateGSplatsFromGeo(
                            stage, prim_path, prim_world_xform,
                            HUSDgetUsdTimeCode(time_code), *output_gdh.gdp());
                    break;
                }
                case HUSD_ApexShapeType::Camera:
                    husdUpdateCameraFromGeo(
                            stage, prim_path, HUSDgetUsdTimeCode(time_code),
                            *output_gdh.gdp());
                    break;
            }
        }

        ++sample_idx;
    }

    return true;
}
