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

#include "XUSD_HydraGeoImport.h"

#include "XUSD_Format.h"
#include "XUSD_Tokens.h"
#include "XUSD_Utils.h"

#include <gusd/GT_VtArray.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Gf.h>
#include <gusd/attribListsBuilder.h>
#include <gusd/primvarUtils.h>

#include <GT/GT_PrimCamera.h>
#include <GT/GT_PrimPointMesh.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_RefineParms.h>
#include <GT/GT_Util.h>
#include <GU/GU_Detail.h>
#include <GU/GU_DetailHandle.h>
#include <UT/UT_VarEncode.h>

#include <pxr/base/gf/camera.h>
#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/containerSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/usd/usdVol/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Convert from the Hydra interpolation tokens to GT_Owner.
static GT_Owner
xusdConvertInterpolation(const TfToken &interp)
{
    if (interp == HdPrimvarSchemaTokens->varying
        || interp == HdPrimvarSchemaTokens->vertex)
    {
        return GT_OWNER_POINT;
    }
    else if (interp == HdPrimvarSchemaTokens->faceVarying)
        return GT_OWNER_VERTEX;
    else if (interp == HdPrimvarSchemaTokens->uniform)
        return GT_OWNER_PRIMITIVE;
    else if (interp == HdPrimvarSchemaTokens->constant)
        return GT_OWNER_DETAIL;
    else
        return GT_OWNER_INVALID;
}

/// Convert from the Hydra primvar role tokens to GT_Type.
static GT_Type
xusdConvertRole(const TfToken &role, const VtValue &value)
{
    // Matches UsdImagingUsdtoHdRole()
    if (role == HdPrimvarRoleTokens->point)
        return GT_TYPE_POINT;
    else if (role == HdPrimvarRoleTokens->normal)
        return GT_TYPE_NORMAL;
    else if (role == HdPrimvarRoleTokens->vector)
        return GT_TYPE_VECTOR;
    else if (role == HdPrimvarRoleTokens->color)
        return GT_TYPE_COLOR;
    else if (role == HdPrimvarRoleTokens->textureCoordinate)
        return GT_TYPE_TEXTURE;
    // Set type info for quaternion values to avoid translating to vector4.
    else if (
            value.IsHolding<VtQuatdArray>() || value.IsHolding<VtQuatfArray>()
            || value.IsHolding<VtQuathArray>() || value.IsHolding<GfQuatd>()
            || value.IsHolding<GfQuatf>() || value.IsHolding<GfQuath>())
    {
        return GT_TYPE_QUATERNION;
    }
    else
        return GT_TYPE_NONE;
}

static UT_StringHolder
xusdConvertPrimvarName(const TfToken &name)
{
    // Handle some conversions for standard attribute names.
    static const UT_Map<TfToken, UT_StringHolder> theNameMap =
    {
        {HdPrimvarsSchemaTokens->points, GA_Names::P},
        {UsdVolTokens->positions, GA_Names::P},
        {HdPrimvarsSchemaTokens->normals, GA_Names::N},
        {UsdVolTokens->orientations, GA_Names::orient},
        {UsdVolTokens->scales, GA_Names::scale},
        {HdPrimvarsSchemaTokens->widths, GA_Names::pscale},
        {HdTokens->velocities, GA_Names::v},
        {HdTokens->accelerations, GA_Names::accel},
        {HusdHdApexTokens->houdiniApexDeformJointIndices, GA_Names::boneCapture},
        {HdTokens->displayColor, GA_Names::Cd},
    };

    auto it = theNameMap.find(name);
    if (it != theNameMap.end())
        return it->second;

    // Otherwise, just use the primvar name directly (with encoding to
    // round-trip namespaced primvars through SOPs)
    UT_StringHolder attr_name = GusdUSD_Utils::TokenToStringHolder(name);
    attr_name = UT_VarEncode::encodeAttrib(attr_name);
    return attr_name;
}

static GusdPrimvarInfo
xusdGetPrimvarInfo(
        const SdfPath &prim_path,
        const HdPrimvarsSchema &primvars,
        const TfToken &primvar_name)
{
    HdPrimvarSchema primvar = primvars.GetPrimvar(primvar_name);

    UT_StringHolder attr_name = xusdConvertPrimvarName(primvar_name);

    HdSampledDataSourceHandle value_ds = primvar.GetFlattenedPrimvarValue();
    if (!value_ds)
        return {};

    VtValue flattened_value = value_ds->GetValue(0.0f);

    TfToken interp_token;
    if (HdTokenDataSourceHandle interp_source = primvar.GetInterpolation())
        interp_token = interp_source->GetTypedValue(0.0f);

    GT_Owner interp = xusdConvertInterpolation(interp_token);
    if (interp == GT_OWNER_INVALID)
        return {};

    int element_size = 1;
    if (HdIntDataSourceHandle size_source = primvar.GetElementSize())
        element_size = size_source->GetTypedValue(0.0f);

    TfToken role_token;
    if (HdTokenDataSourceHandle role_source = primvar.GetRole())
        role_token = role_source->GetTypedValue(0.0f);

    GT_Type role = xusdConvertRole(role_token, flattened_value);

    UT_Optional<fpreal> scale;
    if (primvar_name == HdPrimvarsSchemaTokens->widths)
        scale = 0.5; // Widths (diameter) are translated to pscale (radius)

    GusdPrimvarInfo primvar_info = {
        .myPrimPath = prim_path,
        .myName = attr_name,
        .myOrigName = primvar_name,
        .myFlattenedValue = std::move(flattened_value),
        .myIsIndexed = primvar.IsIndexed(),
        .myElementSize = element_size,
        .myOwner = interp,
        .myTypeInfo = role,
        .myValueScale = scale
    };

    return primvar_info;
}

/// Assemble the boneCapture attribute from the separate primvars it is
/// represented with in USD.
static void
xusdConvertCaptureWeights(
        GusdAttribListsBuilder &attrib_lists,
        const HdSceneIndexPrim &prim,
        const SdfPath &prim_path,
        const HdPrimvarsSchema &primvars)
{
    GusdPrimvarInfo indices_primvar = xusdGetPrimvarInfo(
            prim_path, primvars,
            HusdHdApexTokens->houdiniApexDeformJointIndices);
    GusdPrimvarInfo weights_primvar = xusdGetPrimvarInfo(
            prim_path, primvars,
            HusdHdApexTokens->houdiniApexDeformJointWeights);

    VtTokenArray joints;
    auto deform_api = HdContainerDataSource::Cast(
            prim.dataSource->Get(HusdHdApexTokens->houdiniApexShapeDeform));
    if (deform_api)
    {
        auto joints_data = HdTypedSampledDataSource<VtTokenArray>::Cast(
                deform_api->Get(HusdHdApexTokens->joints));
        if (joints_data)
            joints = joints_data->GetTypedValue(0.0f);
    }

    GT_DataArrayHandle bone_capture_data = GusdConvertPrimvarsToIndexPairData(
            indices_primvar, weights_primvar, joints);
    if (!bone_capture_data
        || !attrib_lists.add(indices_primvar, bone_capture_data))
    {
        TF_WARN("<%s>: failed to convert primvar '%s'", prim_path.GetText(),
                HusdHdApexTokens->houdiniApexDeformJointIndices.GetText());
    }
}

/// Convert all primvars to GT data arrays.
static void
xusdConvertPrimvars(
        GusdAttribListsBuilder &attrib_lists,
        const HdSceneIndexPrim &prim,
        const SdfPath &prim_path)
{
    auto primvars = HdPrimvarsSchema::GetFromParent(prim.dataSource);
    bool has_capture_weights = false;
    
    for (const TfToken &primvar_name : primvars.GetPrimvarNames())
    {
        // Skip primvars which translate into boneCapture. These are separately
        // handled below.
        if (primvar_name == HusdHdApexTokens->houdiniApexDeformJointIndices
            || primvar_name == HusdHdApexTokens->houdiniApexDeformJointWeights)
        {
            has_capture_weights = true;
            continue;
        }

        GusdPrimvarInfo primvar_info = xusdGetPrimvarInfo(
                prim_path, primvars, primvar_name);
        // Skip (without warning) if the primvar had no value. This can happen
        // for primvars added by the gprim adapter, e.g. nonlinearSampleCount.
        if (!primvar_info)
            continue;

        GT_DataArrayHandle primvar_data = GusdConvertPrimvarData(primvar_info);
        if (!primvar_data || !attrib_lists.add(primvar_info, primvar_data))
        {
            TF_WARN("<%s>: failed to convert primvar '%s'", prim_path.GetText(),
                    primvar_name.GetText());
            continue;
        }
    }

    if (has_capture_weights)
        xusdConvertCaptureWeights(attrib_lists, prim, prim_path, primvars);
}

/// Returns the number of points for a point-based prim.
static exint
xusdGetNumPoints(const HdSceneIndexPrim &prim)
{
    auto primvars = HdPrimvarsSchema::GetFromParent(prim.dataSource);
    if (!primvars)
    {
        UT_ASSERT_MSG(false, "Prim does not have any primvars!");
        return 0;
    }

    HdPrimvarSchema points
            = primvars.GetPrimvar(HdPrimvarsSchemaTokens->points);
    if (!points)
    {
        UT_ASSERT_MSG(false, "Prim does not have a 'points' primvar!");
        return 0;
    }

    VtValue points_value = points.GetPrimvarValue()->GetValue(0.0);
    return points_value.GetArraySize();
}

/// Helper function to convert a GT primitive to a GU_Detail.
static GU_DetailHandle
xusdImportGTPrim(const GT_Primitive &gt_prim)
{
    GT_RefineParms refine_parms;
    // Translate to regular polygons, not polysoups.
    refine_parms.setAllowPolySoup(false);

    UT_SmallArray<GU_DetailHandle> result;
    GT_Util::makeGEO(result, gt_prim, &refine_parms);

    if (result.isEmpty())
    {
        UT_ASSERT_MSG(false, "Failed to convert GT prim to geometry!");
        return GU_DetailHandle();
    }
    else if (result.size() > 1)
    {
        // We shouldn't get multiple details from converting simple primitive
        // types.
        UT_ASSERT_MSG(
                false,
                "GT_Util::makeGEO unexpectedly produced multiple details");
        return GU_DetailHandle();
    }

#if 0
    // Save out the geometry to a .bgeo file for debugging.
    static int theCounter = 0;
    UT_StringHolder save_path;
    save_path.format("/tmp/hydra_geo_{}.bgeo", theCounter++);
    result[0].gdp()->save(save_path.c_str(), nullptr);
#endif

    return result[0];
}

/// Translate the Hydra primitive's transform to the GT primitive's transform.
static void
xusdConvertPrimXform(const HdSceneIndexPrim &prim, GT_Primitive &gt_prim)
{
    HdXformSchema xform_schema = HdXformSchema::GetFromParent(prim.dataSource);
    if (!xform_schema || !xform_schema.GetMatrix()
        || !xform_schema.GetResetXformStack())
    {
        return;
    }

    // We expect to be running after the flattening scene index, so the
    // transform is in world space.
    UT_ASSERT(xform_schema.GetResetXformStack()->GetTypedValue(0.0));

    UT_Matrix4D xform = GusdUT_Gf::Cast(
            xform_schema.GetMatrix()->GetTypedValue(0.0f));
    gt_prim.setPrimitiveTransform(UTmakeIntrusive<GT_Transform>(&xform, 1));
}

static UT_CameraParms
xusdConvertCameraParms(const HdCameraSchema &hydra_camera)
{
    // Build a GfCamera from a Hydra camera, ignoring the prim transform and
    // clipping planes which are not required for conversion to UT_CameraParms.
    // Note that this scales certain camera parameters back to 1/10 scene
    // units, undoing Hydra's scaling, to match UsdGeomCamera so that we can
    // use the same conversion routines to UT_CamerParms.
    GfCamera gf_camera;

    UT_ASSERT(hydra_camera.IsDefined());

    if (HdTokenDataSourceHandle projection = hydra_camera.GetProjection())
    {
        const TfToken projection_token = projection->GetTypedValue(0.0f);
        gf_camera.SetProjection(
                projection_token == HdCameraSchemaTokens->orthographic
                        ? GfCamera::Orthographic
                        : GfCamera::Perspective);
    }

    if (HdFloatDataSourceHandle aperture = hydra_camera.GetHorizontalAperture())
    {
        gf_camera.SetHorizontalAperture(
                aperture->GetTypedValue(0.0f) / GfCamera::APERTURE_UNIT);
    }

    if (HdFloatDataSourceHandle aperture = hydra_camera.GetVerticalAperture())
    {
        gf_camera.SetVerticalAperture(
                aperture->GetTypedValue(0.0f) / GfCamera::APERTURE_UNIT);
    }

    if (HdFloatDataSourceHandle offset
        = hydra_camera.GetHorizontalApertureOffset())
    {
        gf_camera.SetHorizontalApertureOffset(
                offset->GetTypedValue(0.0f) / GfCamera::APERTURE_UNIT);
    }

    if (HdFloatDataSourceHandle offset
        = hydra_camera.GetVerticalApertureOffset())
    {
        gf_camera.SetVerticalApertureOffset(
                offset->GetTypedValue(0.0f) / GfCamera::APERTURE_UNIT);
    }

    if (HdFloatDataSourceHandle focal_length = hydra_camera.GetFocalLength())
    {
        gf_camera.SetFocalLength(
                focal_length->GetTypedValue(0.0f)
                / GfCamera::FOCAL_LENGTH_UNIT);
    }

    if (HdVec2fDataSourceHandle clipping_range
        = hydra_camera.GetClippingRange())
    {
        const GfVec2f range = clipping_range->GetTypedValue(0.0f);
        gf_camera.SetClippingRange(GfRange1f(range[0], range[1]));
    }

    if (HdFloatDataSourceHandle f_stop = hydra_camera.GetFStop())
        gf_camera.SetFStop(f_stop->GetTypedValue(0.0f));

    if (HdFloatDataSourceHandle focus_distance
        = hydra_camera.GetFocusDistance())
    {
        gf_camera.SetFocusDistance(focus_distance->GetTypedValue(0.0f));
    }

    // TODO - we don't have access to the stage units through Hydra.
    static constexpr fpreal64 theDefaultMetersPerUnit = 0.01;

    UT_CameraParms camera_parms;
    HUSDgetCameraParms(gf_camera, theDefaultMetersPerUnit, camera_parms);

    // Convert remaining camera parameters.
    // TODO - could fetch resolution from the render settings. For now we use
    // on the fallback from HUSDgetCameraParms().
    // TODO - convert imaging distance and guide scale, and translate any custom
    // attribs into metadata. The imaging distance and guide scale aren't
    // currently translating into Hydra under
    // HdCameraSchema::GetNamespacedProperties().

    if (HdDoubleDataSourceHandle shutter_open = hydra_camera.GetShutterOpen())
        camera_parms.shutteropen = shutter_open->GetTypedValue(0.0f);

    if (HdDoubleDataSourceHandle shutter_close = hydra_camera.GetShutterClose())
        camera_parms.shutterclose = shutter_close->GetTypedValue(0.0f);

    return camera_parms;
}

/// Translate a Hydra mesh prim to geometry.
static GU_DetailHandle
xusdConvertMeshToGeo(
        const HdSceneIndexPrim &prim,
        const SdfPath &prim_path,
        const XUSD_HydraGeoImportOptions &options)
{
    auto mesh = HdMeshSchema::GetFromParent(prim.dataSource);

    HdMeshTopologySchema topology = mesh.GetTopology();
    VtIntArray counts_data
            = topology.GetFaceVertexCounts()->GetTypedValue(0.0f);
    auto gt_counts = UTmakeIntrusive<GusdGT_VtArray<int32>>(counts_data);

    VtIntArray indices_data
            = topology.GetFaceVertexIndices()->GetTypedValue(0.0f);
    auto gt_indices = UTmakeIntrusive<GusdGT_VtArray<int32>>(indices_data);

    const exint num_points = xusdGetNumPoints(prim);

    GusdAttribListsBuilder attrib_lists;
    attrib_lists.enablePointAttribs(num_points);
    attrib_lists.enableVertexAttribs(indices_data.size());
    attrib_lists.enableUniformAttribs(counts_data.size());
    attrib_lists.enableDetailAttribs();
    xusdConvertPrimvars(attrib_lists, prim, prim_path);

    auto gt_mesh = UTmakeIntrusive<GT_PrimPolygonMesh>(
            gt_counts, gt_indices,
            attrib_lists.buildPointAttribs(),
            attrib_lists.buildVertexAttribs(),
            attrib_lists.buildUniformAttribs(),
            attrib_lists.buildDetailAttribs());

    if (options.myApplyPrimXform)
        xusdConvertPrimXform(prim, *gt_mesh);

    return xusdImportGTPrim(*gt_mesh);
}

/// Translate a Hydra particleField prim to geometry.
static GU_DetailHandle
xusdConvertGSplatsToGeo(
        const HdSceneIndexPrim &prim,
        const SdfPath &prim_path,
        const XUSD_HydraGeoImportOptions &options)
{
    // GSplats have a 'positions' attribute, not 'points.'
    auto primvars = HdPrimvarsSchema::GetFromParent(prim.dataSource);
    HdPrimvarSchema positions = primvars.GetPrimvar(UsdVolTokens->positions);
    if (!positions)
    {
        UT_ASSERT_MSG(false, "Prim does not have a 'positions' primvar!");
        return GU_DetailHandle();
    }

    VtValue positions_value = positions.GetPrimvarValue()->GetValue(0.0);
    const exint num_points = positions_value.GetArraySize();

    // TODO - translate spherical harmonics and opacities.

    GusdAttribListsBuilder attrib_lists;
    attrib_lists.enablePointAttribs(num_points);
    attrib_lists.enableDetailAttribs();
    xusdConvertPrimvars(attrib_lists, prim, prim_path);

    auto gt_points = UTmakeIntrusive<GT_PrimPointMesh>(
            attrib_lists.buildPointAttribs(),
            attrib_lists.buildDetailAttribs());

    if (options.myApplyPrimXform)
        xusdConvertPrimXform(prim, *gt_points);

    return xusdImportGTPrim(*gt_points);
}

/// Translate a Hydra points prim to geometry.
static GU_DetailHandle
xusdConvertPointsToGeo(
        const HdSceneIndexPrim &prim,
        const SdfPath &prim_path,
        const XUSD_HydraGeoImportOptions &options)
{
    const exint num_points = xusdGetNumPoints(prim);

    // Note: the 'ids' attrib is not translated to Hydra currently.
    GusdAttribListsBuilder attrib_lists;
    attrib_lists.enablePointAttribs(num_points);
    attrib_lists.enableDetailAttribs();
    xusdConvertPrimvars(attrib_lists, prim, prim_path);

    auto gt_points = UTmakeIntrusive<GT_PrimPointMesh>(
            attrib_lists.buildPointAttribs(),
            attrib_lists.buildDetailAttribs());

    if (options.myApplyPrimXform)
        xusdConvertPrimXform(prim, *gt_points);

    return xusdImportGTPrim(*gt_points);
}

/// Translate a Hydra camera prim to geometry.
static GU_DetailHandle
xusdConvertCameraToGeo(
        const HdSceneIndexPrim &prim,
        const SdfPath &prim_path,
        const XUSD_HydraGeoImportOptions &options)
{
    auto hydra_camera = HdCameraSchema::GetFromParent(prim.dataSource);
    if (!hydra_camera)
        return GU_DetailHandle();

    UT_CameraParms camera_parms = xusdConvertCameraParms(hydra_camera);

    GusdAttribListsBuilder attrib_lists;
    attrib_lists.enableUniformAttribs(1);
    attrib_lists.enablePointAttribs(1);
    attrib_lists.enableVertexAttribs(1);
    
    xusdConvertPrimvars(attrib_lists, prim, prim_path);
    GT_AttributeListHandle attribs = attrib_lists.buildUniformAttribs();

    auto gt_camera = UTmakeIntrusive<GT_PrimCamera>(
            UT_Matrix4D::getIdentityMatrix(), camera_parms,
            attribs);

    if (options.myApplyPrimXform)
        xusdConvertPrimXform(prim, *gt_camera);

    return xusdImportGTPrim(*gt_camera);
}

GU_DetailHandle
XUSDimportGeoFromHydraPrim(
        const HdSceneIndexPrim &prim,
        const SdfPath &prim_path,
        const XUSD_HydraGeoImportOptions &options)
{
    if (!prim)
        return GU_DetailHandle();

    if (prim.primType == HdPrimTypeTokens->mesh)
        return xusdConvertMeshToGeo(prim, prim_path, options);
    else if (prim.primType == HdPrimTypeTokens->points)
        return xusdConvertPointsToGeo(prim, prim_path, options);
    else if (prim.primType == HdPrimTypeTokens->particleField)
        return xusdConvertGSplatsToGeo(prim, prim_path, options);
    else if (prim.primType == HdPrimTypeTokens->camera)
        return xusdConvertCameraToGeo(prim, prim_path, options);
    else
    {
        TF_WARN("<%s>: conversion to geometry not supported for type '%s'",
                prim_path.GetText(), prim.primType.GetText());
        return GU_DetailHandle();
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
