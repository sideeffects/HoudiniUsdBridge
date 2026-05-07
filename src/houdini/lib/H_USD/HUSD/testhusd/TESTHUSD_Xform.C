/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *      Side Effects Software Inc
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 * NAME:    TESTXUSD_Utils.C (C++)
 *
 * COMMENTS:    Module for testing code and macros in XUSD_Utils.C
 *
 */

#include "HUSD/HUSD_CreatePrims.h"

#include <gusd/UT_Gf.h>
#include <HUSD/XUSD_Utils.h>
#include <HUSD/UsdHoudini/houdiniXformCommonAPI.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_TestManager.h>
#include <HUSD/HUSD_Constants.h>
#include <HUSD/HUSD_CreatePrims.h>
#include <HUSD/HUSD_FindPrims.h>
#include <HUSD/HUSD_Xform.h>
#include <HUSD/XUSD_Data.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/base/tf/stringUtils.h>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

static HUSD_DataHandle
createStage()
{
    // Create a stage and pre-populate it with a selection of prims.
    HUSD_DataHandle datahandle;
    datahandle.createNewData();
    HUSD_AutoWriteLock writelock(datahandle);
    HUSD_AutoLayerLock layerlock(writelock);
    HUSD_CreatePrims createprims(layerlock);

    createprims.createPrim("/geo/cube", "UsdGeomCube", UT_StringRef(),
        HUSD_Constants::getPrimSpecifierDefine(),
        HUSD_Constants::getXformPrimType());

    return datahandle;
}

template <typename T>
static bool
isEqual(const T &vec3, const UT_Vector3D &expected, fpreal64 tolerance = 1e-6)
{
    if constexpr (std::is_same_v<T, UT_Vector3D>)
    {
        return vec3.isEqual(expected, tolerance);
    }
    else if constexpr (std::is_same_v<T, UT_Vector3F>)
    {
        return UT_Vector3D(vec3).isEqual(expected, tolerance);
    }
    else if constexpr (std::is_same_v<T, GfVec3d>)
    {
        return GusdUT_Gf::Cast(vec3).isEqual(expected, tolerance);
    }
    else if constexpr (std::is_same_v<T, GfVec3f>)
    {
        return UT_Vector3D(GusdUT_Gf::Cast(vec3)).isEqual(expected, tolerance);
    }
    else
    {
        UTdebugPrint("Un-supported type");
        return false;
    }
}

static bool
testBasicEntry(int entry_num, HUSD_AutoWriteLock& writelock, 
            UT_Vector3D& translate_val, UT_Vector3D& rotate_val,
            UT_Vector3D& scale_val, UT_Vector3D& shear_val,
            UT_Vector3D& pivot_val, UT_Vector3D& pivot_rotate_val,
            UT_XformOrder& xform_order)
{
    UT_TestUnit unit("Basic HUSD Xform test Entry %d", entry_num);
    UT_Matrix4D xform(1.0);
    HUSD_Xform xformer(writelock);
    HUSD_XformEntryMap xform_map;
    xformer.appendToXformMap(HUSD_FindPrims(writelock, "/geo/cube"),
        &xform, nullptr, HUSD_TimeCode(),
        nullptr, nullptr, nullptr, xform_map);
    if (!xformer.applyXforms(xform_map,
        UT_StringRef(), HUSD_XFORM_ABSOLUTE))
    {
        return unit.fail("Failed to wipe out xform.");
    }

    UT_Matrix4D::PivotSpace  pivot_space(pivot_val, pivot_rotate_val);

    // Order: SRT
    xform.xform(xform_order,
        translate_val[0], translate_val[1], translate_val[2],
        rotate_val[0],    rotate_val[1],    rotate_val[2],
        scale_val[0],     scale_val[1],     scale_val[2],
        shear_val[0],     shear_val[1],     shear_val[2],
        pivot_space);

    // UTdebugPrint("The final xform is: ", xform);
    xform_map.clear();
    xformer.appendToXformMap(HUSD_FindPrims(writelock, "/geo/cube"),
        &xform, nullptr, HUSD_TimeCode(),
        &pivot_val, &pivot_rotate_val, &xform_order, xform_map);
    if (!xformer.applyXforms(xform_map,
        UT_StringRef(), HUSD_XFORM_COMMON_API_APPEND))
    {
        return unit.fail("Failed to author xform common api.");
    }

    bool does_reset = false;
    UsdStageRefPtr stage = writelock.data()->stage();
    auto cubeprim = stage->GetPrimAtPath(SdfPath("/geo/cube"));
    UsdGeomCube cube(cubeprim);
    UsdGeomXformable	 xformable(cubeprim);
    UsdHoudiniHoudiniXformCommonAPI common(cubeprim);

    std::vector<UsdGeomXformOp> xformop_ordered = xformable.GetOrderedXformOps(&does_reset);
    // for(UsdGeomXformOp& op : xformop_ordered)
    // {
    //     UTdebugPrint("Contains: ", op.GetOpName().GetText());
    // }
    GfVec3d translation;
    UsdHoudiniHoudiniXformCommonAPI::Rotation rotation;
    GfVec3f scale;
    GfVec3f shear;
    GfVec3f pivot;
    GfVec3f rotpivot;

    if(!common)
        unit.fail("Failed at common check.");

    UsdGeomXformOp t, p, rp, r, sh, s;
    if (!common.GetXformVectors(&translation, &rotation,
                                &scale, &shear, &pivot,
                                &rotpivot, HUSDgetUsdTimeCode(HUSD_TimeCode())))
        return unit.fail("Failed to get back xform ops.");
    
    if(!isEqual(translation, translate_val))
    {
        UTdebugPrint("Expected: ", translate_val, " Got: ", GusdUT_Gf::Cast(translation));
        return unit.fail("Translate value mismatch");
    }

    if(!isEqual(scale, scale_val))
    {
        UTdebugPrint("Expected: ", scale_val, " Got: ", GusdUT_Gf::Cast(scale));
        return unit.fail("Scale value mismatch");
    }

    if(!isEqual(shear, shear_val))
    {
        UTdebugPrint("Expected: ", shear_val, " Got: ", GusdUT_Gf::Cast(shear));
        return unit.fail("shear value mismatch");
    }

    if(!isEqual(pivot, pivot_val))
    {
        UTdebugPrint("Expected: ", pivot_val, " Got: ", GusdUT_Gf::Cast(pivot));
        return unit.fail("Pivot value mismatch");
    }

    if(!isEqual(rotpivot, pivot_rotate_val))
    {
        UTdebugPrint("Expected: ", pivot_rotate_val, " Got: ", GusdUT_Gf::Cast(rotpivot));
        return unit.fail("Pivot Rotate value mismatch");
    }

    if(HUSDcastRotOrder(rotation.GetRotationOrder()) != xform_order)
        return unit.fail("Rotate order value mismatch");

    if(!isEqual(rotation.GetEulerAngles(), rotate_val))
    {
        UTdebugPrint("Expected: ", rotate_val, " Got: ", GusdUT_Gf::Cast(rotation.GetEulerAngles()));
        return unit.fail("Rotate value mismatch");
    }

    return unit.ok();
}

static bool
testBasicComponentEntry(int entry_num, HUSD_AutoWriteLock& writelock,
            UT_Vector3D& translate_val, UT_Vector3D& rotate_val,
            UT_Vector3D& scale_val, UT_Vector3D& shear_val,
            UT_Vector3D& pivot_val, UT_Vector3D& pivot_rotate_val,
            UT_XformOrder& xform_order)
{
    UT_TestUnit unit("Component HUSD Xform test Entry %d", entry_num);
    UT_Matrix4D xform(1.0);
    HUSD_Xform xformer(writelock);
    HUSD_XformEntryMap xform_map;
    // Wipe existing xform
    xformer.appendToXformMap(HUSD_FindPrims(writelock, "/geo/cube"),
        &xform, nullptr, HUSD_TimeCode(),
        nullptr, nullptr, nullptr, xform_map);
    if (!xformer.applyXforms(xform_map,
        UT_StringRef(), HUSD_XFORM_ABSOLUTE))
    {
        return unit.fail("Failed to wipe out xform.");
    }

    // Apply via component path instead of matrix
    HUSD_XformEntry::HUSD_XformEntryComponents comps =
        {translate_val, rotate_val, scale_val, shear_val};
    xform_map.clear();
    xformer.appendToXformMap(HUSD_FindPrims(writelock, "/geo/cube"),
        nullptr, &comps, HUSD_TimeCode(),
        &pivot_val, &pivot_rotate_val, &xform_order, xform_map);
    if (!xformer.applyXforms(xform_map,
        UT_StringRef(), HUSD_XFORM_COMMON_API_OVERWRITE))
    {
        return unit.fail("Failed to author xform common api.");
    }

    UsdStageRefPtr stage = writelock.data()->stage();
    auto cubeprim = stage->GetPrimAtPath(SdfPath("/geo/cube"));
    UsdHoudiniHoudiniXformCommonAPI common(cubeprim);

    GfVec3d translation;
    UsdHoudiniHoudiniXformCommonAPI::Rotation rotation;
    GfVec3f scale;
    GfVec3f shear;
    GfVec3f pivot;
    GfVec3f rotpivot;

    if(!common)
        unit.fail("Failed at common check.");

    if (!common.GetXformVectors(&translation, &rotation,
                                &scale, &shear, &pivot,
                                &rotpivot, HUSDgetUsdTimeCode(HUSD_TimeCode())))
        return unit.fail("Failed to get back xform ops.");

    if(!isEqual(translation, translate_val))
    {
        UTdebugPrint("Expected: ", translate_val, " Got: ", GusdUT_Gf::Cast(translation));
        return unit.fail("Translate value mismatch");
    }

    if(!isEqual(scale, scale_val))
    {
        UTdebugPrint("Expected: ", scale_val, " Got: ", GusdUT_Gf::Cast(scale));
        return unit.fail("Scale value mismatch");
    }

    if(!isEqual(shear, shear_val))
    {
        UTdebugPrint("Expected: ", shear_val, " Got: ", GusdUT_Gf::Cast(shear));
        return unit.fail("shear value mismatch");
    }

    if(!isEqual(pivot, pivot_val))
    {
        UTdebugPrint("Expected: ", pivot_val, " Got: ", GusdUT_Gf::Cast(pivot));
        return unit.fail("Pivot value mismatch");
    }

    if(!isEqual(rotpivot, pivot_rotate_val))
    {
        UTdebugPrint("Expected: ", pivot_rotate_val, " Got: ", GusdUT_Gf::Cast(rotpivot));
        return unit.fail("Pivot Rotate value mismatch");
    }

    if(HUSDcastRotOrder(rotation.GetRotationOrder()) != xform_order)
        return unit.fail("Rotate order value mismatch");

    if(!isEqual(rotation.GetEulerAngles(), rotate_val))
    {
        UTdebugPrint("Expected: ", rotate_val, " Got: ", GusdUT_Gf::Cast(rotation.GetEulerAngles()));
        return unit.fail("Rotate value mismatch");
    }

    return unit.ok();
}

static bool
testTimeSampleVal()
{
    UT_TestUnit unit("Basic HUSD Xform time-sample test");
    HUSD_DataHandle datahandle = createStage();
    HUSD_AutoWriteLock writelock(datahandle);
    UsdStageRefPtr stage = writelock.data()->stage();
    UsdPrim prim = stage->GetPrimAtPath(SdfPath("/geo/cube"));
    UsdHoudiniHoudiniXformCommonAPI common(prim);
    UsdGeomXformable xformable(prim);

    // 1 default + 1 sample at time 1.0 + 1 sample at time 2,0 
    common.SetTranslate(GfVec3d(1.0, 2.0, 3.0), UsdTimeCode::Default());
    common.SetTranslate(GfVec3d(10.0, 20.0, 30.0), UsdTimeCode(1.0));
    common.SetTranslate(GfVec3d(-5.0, -4.0, -3.0), UsdTimeCode(2.0));

    struct SampleCase
    {
        UsdTimeCode time;
        UT_Vector3D expect;
    } cases[] = {
        {UsdTimeCode::Default(), UT_Vector3D(1.0, 2.0, 3.0)},    // default
        {UsdTimeCode(1.0),       UT_Vector3D(10.0, 20.0, 30.0)}, // has sample
        {UsdTimeCode(2.0),       UT_Vector3D(-5.0, -4.0, -3.0)}, // has sample
        {UsdTimeCode(3.0),       UT_Vector3D(-5.0, -4.0, -3.0)}, // no sample → hold last
    };

    auto sampling = HUSDgetLocalTransformTimeSampling(xformable.GetPrim());
    for (const auto &c : cases)
    {
        GfVec3d translation;
        UsdHoudiniHoudiniXformCommonAPI::Rotation rotation;
        GfVec3f scale, shear, pivot, rotpivot;

        if (!common.GetXformVectors(&translation, &rotation, &scale,
                                    &shear, &pivot, &rotpivot,
                                    c.time))
        {
            return unit.fail("GetXformVectors failed at time %g",
                             c.time.GetValue());
        }

        if (!isEqual(translation, c.expect))
        {
            UTdebugPrint("time ", c.time.GetValue(),
                         " expected ", c.expect,
                         " got ", GusdUT_Gf::Cast(translation));
            return unit.fail("Translate time-sample mismatch");
        }
        
        if (!HUSDisTimeSampled(sampling))
        {
            return unit.fail("Default time detected on prim that should be time varying");
        }
    }

    return unit.ok();
}

static bool
testTimeSampleVal(bool original, bool applied)
{
    std::string original_time = original ? "non-default" : "default";
    std::string applied_time = applied ? "non-default" : "default";
    UT_TestUnit unit("Test time varing %s on %s.",
                     applied_time.c_str(), original_time.c_str());
    HUSD_DataHandle datahandle = createStage();
    HUSD_AutoWriteLock writelock(datahandle);
    UsdStageRefPtr stage = writelock.data()->stage();
    UsdPrim prim = stage->GetPrimAtPath(SdfPath("/geo/cube"));
    UsdHoudiniHoudiniXformCommonAPI common(prim);
    UsdGeomXformable xformable(prim);
    UT_Matrix4D xform(1.0);
    HUSD_Xform xformer(writelock);
    UT_XformOrder xform_order(UT_XformOrder::SRT, UT_XformOrder::XYZ);
    UT_Vector3D pivot_val(0.0, 0.0, 0.0);
    UT_Vector3D pivot_rotate_val(0.0, 0.0, 0.0);
    HUSD_TimeCode husd_time;

    // 0 stands for default time code, 1 stands for non-default
    if(!original)
        common.SetTranslate(GfVec3d(1.0, 2.0, 3.0), UsdTimeCode::Default());
    else
        common.SetTranslate(GfVec3d(1.0, 2.0, 3.0), UsdTimeCode(1.0));
    
    if(!applied)
        husd_time = HUSD_TimeCode(); //default
    else
        husd_time = HUSD_TimeCode(1.0); //non-default
    
    // Order: SRT
    xform.xform(xform_order, 
        1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
        1.0, 1.0, 1.0, 1.0, 1.0, 1.0);
    
    HUSD_XformEntryMap xform_map;
    xformer.appendToXformMap(HUSD_FindPrims(writelock, "/geo/cube"),
        &xform, nullptr, husd_time,
        &pivot_val, &pivot_rotate_val, &xform_order, xform_map);
    if (!xformer.applyXforms(xform_map,
        UT_StringRef(), HUSD_XFORM_COMMON_API_APPEND))
    {
        return unit.fail("Failed to author xform common api.");
    }

    auto sampling = HUSDgetLocalTransformTimeSampling(prim);
    // only default + default should be non-time varying here
    if(!original && !applied && HUSDisTimeSampled(sampling))
        return unit.fail("Failed at default + default");
    else if((original || applied) && !HUSDisTimeSampled(sampling))
        return unit.fail("Failed at %s on %s.",
                         applied_time.c_str(), original_time.c_str());

    return unit.ok();
}

static bool
testTimeSamples()
{
    bool ret_val = true;
    // Test if GetXformVectors can return correct value depending on time
    ret_val &= testTimeSampleVal();
    // Test 1 apply default on default
    ret_val &= testTimeSampleVal(0, 0);
    // Test 2 apply default on non-default
    ret_val &= testTimeSampleVal(1, 0);
    // Test 3 apply non-default on default
    ret_val &= testTimeSampleVal(0, 1);
    // Test 4 apply non-default on non-default
    ret_val &= testTimeSampleVal(1, 1);

    return ret_val;
}

static bool
testBasic()
{
    UT_TestUnit unit("Basic HUSD Xform test");
    HUSD_DataHandle datahandle = createStage();
    HUSD_AutoWriteLock writelock(datahandle);

    UT_Vector3D translate_val(1.0, 2.0, 3.0);
    UT_Vector3D rotate_val(0.0, 0.0, 0.0);
    UT_Vector3D scale_val(1.0, 2.0, 3.0);
    UT_Vector3D shear_val(0.0, 0.0, 0.0);
    UT_Vector3D pivot_val(0.0, 0.0, 0.0);
    UT_Vector3D pivot_rotate_val(0.0, 0.0, 0.0);
    UT_XformOrder xform_order(UT_XformOrder::SRT, UT_XformOrder::XYZ);
    
    bool entry1 = testBasicEntry(1, writelock, translate_val, rotate_val, scale_val, 
                        shear_val, pivot_val, pivot_rotate_val, xform_order);

    translate_val = UT_Vector3D(1.0, 2.0, 3.0);
    rotate_val = UT_Vector3D(0.0, 0.0, 0.0);
    scale_val = UT_Vector3D(1.0, 2.0, 3.0);
    shear_val = UT_Vector3D(1.0, 2.0, 3.0);
    pivot_val = UT_Vector3D(0.0, 0.0, 0.0);
    pivot_rotate_val = UT_Vector3D(0.0, 0.0, 0.0);
    xform_order = UT_XformOrder(UT_XformOrder::SRT, UT_XformOrder::ZYX);

    bool entry2 = testBasicEntry(2, writelock, translate_val, rotate_val, scale_val, 
                        shear_val, pivot_val, pivot_rotate_val, xform_order);

    translate_val = UT_Vector3D(1.0, 2.0, 3.0);
    rotate_val = UT_Vector3D(15.0, 25.0, 35.0);
    scale_val = UT_Vector3D(1.0, 2.0, 3.0);
    shear_val = UT_Vector3D(1.0, 2.0, 3.0);
    pivot_val = UT_Vector3D(2.0, 3.0, 4.0);
    pivot_rotate_val = UT_Vector3D(5.0, 6.0, 8.0);
    xform_order = UT_XformOrder(UT_XformOrder::SRT, UT_XformOrder::XZY);

    bool entry3 = testBasicEntry(3, writelock, translate_val, rotate_val, scale_val, 
                        shear_val, pivot_val, pivot_rotate_val, xform_order);

    if(!entry1 || !entry2 || !entry3)
        return unit.fail("Basic tests on value assignment failed.");

    // Repeat entry 1 and 3 through the component path (overwrite)
    translate_val = UT_Vector3D(1.0, 2.0, 3.0);
    rotate_val = UT_Vector3D(0.0, 0.0, 0.0);
    scale_val = UT_Vector3D(1.0, 2.0, 3.0);
    shear_val = UT_Vector3D(0.0, 0.0, 0.0);
    pivot_val = UT_Vector3D(0.0, 0.0, 0.0);
    pivot_rotate_val = UT_Vector3D(0.0, 0.0, 0.0);
    xform_order = UT_XformOrder(UT_XformOrder::SRT, UT_XformOrder::XYZ);

    bool entry4 = testBasicComponentEntry(4, writelock, translate_val, rotate_val, scale_val,
                        shear_val, pivot_val, pivot_rotate_val, xform_order);

    translate_val = UT_Vector3D(1.0, 2.0, 3.0);
    rotate_val = UT_Vector3D(15.0, 25.0, 35.0);
    scale_val = UT_Vector3D(1.0, 2.0, 3.0);
    shear_val = UT_Vector3D(1.0, 2.0, 3.0);
    pivot_val = UT_Vector3D(2.0, 3.0, 4.0);
    pivot_rotate_val = UT_Vector3D(5.0, 6.0, 8.0);
    xform_order = UT_XformOrder(UT_XformOrder::SRT, UT_XformOrder::XZY);

    bool entry5 = testBasicComponentEntry(5, writelock, translate_val, rotate_val, scale_val,
                        shear_val, pivot_val, pivot_rotate_val, xform_order);

    // Explode-drift cases: rotations that matrix decomposition may rearrange
    translate_val = UT_Vector3D(5.0, 10.0, 15.0);
    rotate_val = UT_Vector3D(0.0, 180.0, 0.0);
    scale_val = UT_Vector3D(1.0, 1.0, 1.0);
    shear_val = UT_Vector3D(0.0, 0.0, 0.0);
    pivot_val = UT_Vector3D(0.0, 0.0, 0.0);
    pivot_rotate_val = UT_Vector3D(0.0, 0.0, 0.0);
    xform_order = UT_XformOrder(UT_XformOrder::SRT, UT_XformOrder::XYZ);

    bool entry6 = testBasicComponentEntry(6, writelock, translate_val, rotate_val, scale_val,
                        shear_val, pivot_val, pivot_rotate_val, xform_order);

    rotate_val = UT_Vector3D(90.0, 90.0, 90.0);

    bool entry7 = testBasicComponentEntry(7, writelock, translate_val, rotate_val, scale_val,
                        shear_val, pivot_val, pivot_rotate_val, xform_order);

    translate_val = UT_Vector3D(10.0, 0.0, 0.0);
    rotate_val = UT_Vector3D(180.0, 0.0, 180.0);

    bool entry8 = testBasicComponentEntry(8, writelock, translate_val, rotate_val, scale_val,
                        shear_val, pivot_val, pivot_rotate_val, xform_order);

    if(!entry4 || !entry5 || !entry6 || !entry7 || !entry8)
        return unit.fail("Component path tests on value assignment failed.");

    if (!testTimeSamples())
        return unit.fail("Time-sample verification failed");

    return unit.ok();
}

static bool
testInvalid()
{
    UT_TestUnit unit("Invalid HUSD Xform tests");
    HUSD_DataHandle datahandle = createStage();
    HUSD_AutoWriteLock writelock(datahandle);
    UsdStageRefPtr stage = writelock.data()->stage();
    UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
    UsdHoudiniHoudiniXformCommonAPI common(xformable.GetPrim());
    bool resetXformStack = false;

    UsdGeomXformOp pivot_trans_op       = xformable.AddTranslateOp(
                                            UsdGeomXformOp::PrecisionFloat,
                                            UsdHoudiniTokens->pivot);
    UsdGeomXformOp pivot_rot_op         = xformable.AddXformOp(
                                            UsdGeomXformOp::TypeRotateXYZ,
                                            UsdGeomXformOp::PrecisionFloat,
                                            UsdHoudiniTokens->pivot);
    UsdGeomXformOp translate_op         = xformable.AddTranslateOp();
    UsdGeomXformOp rot_op               = xformable.AddXformOp(
                                            UsdGeomXformOp::TypeRotateXYZ,
                                            UsdGeomXformOp::PrecisionFloat);
    UsdGeomXformOp shear_op             = xformable.AddTransformOp(
                                            UsdGeomXformOp::PrecisionDouble,
                                            TfToken("shear"));
    UsdGeomXformOp scale_op             = xformable.AddScaleOp();
    UsdGeomXformOp inv_pivot_rot_op     = xformable.AddXformOp(
                                            UsdGeomXformOp::TypeRotateXYZ,
                                            UsdGeomXformOp::PrecisionFloat,
                                            UsdHoudiniTokens->pivot,
                                            /* isInverseOp */ true);
    UsdGeomXformOp inv_pivot_trans_op   = xformable.AddTranslateOp(
                                            UsdGeomXformOp::PrecisionFloat,
                                            UsdHoudiniTokens->pivot,
                                            /* isInverseOp */ true);

    //Set initial values but the value themselves will not be used
    translate_op.Set(GfVec3d(6.5, 1.5, 1.5));
    pivot_trans_op.Set(GfVec3f(1.0f, 1.0f, 1.0f));
    pivot_rot_op.Set(GfVec3f(10.0f, 10.0f, 10.0f));
    rot_op.Set(GfVec3f(30.0f, 20.0f, 10.0f));
    scale_op.Set(GfVec3d(2.0, 1.5, 2.0));
    UT_Matrix4D shear4D(1.0);
    shear4D[1][0] = 1.0;
    shear4D[2][0] = 2.0;
    shear4D[2][1] = 3.0;
    shear_op.Set(GusdUT_Gf::Cast(shear4D));

    std::vector<UsdGeomXformOp>	 xformops{translate_op, pivot_rot_op};
    xformable.SetXformOpOrder(xformops, resetXformStack);
    if(common)
        return unit.fail("Failed at translate + pivot rot");
    
    xformable.ClearXformOpOrder();
    xformops = {inv_pivot_rot_op, rot_op};
    xformable.SetXformOpOrder(xformops, resetXformStack);
    if(common)
        return unit.fail("Failed at inv pivot rot + rot");

    xformable.ClearXformOpOrder();
    xformops = {translate_op, translate_op, translate_op};
    xformable.SetXformOpOrder(xformops, resetXformStack);
    if(common)
        return unit.fail("Failed at three repeated translate");

    xformable.ClearXformOpOrder();
    xformops = {translate_op, pivot_trans_op, pivot_rot_op, rot_op,
                shear_op, scale_op, inv_pivot_rot_op, inv_pivot_trans_op};
    xformable.SetXformOpOrder(xformops, resetXformStack);
    if(common)
        return unit.fail("Failed at translate before pivot + rot pivot");

    xformable.ClearXformOpOrder();
    xformops = {translate_op, pivot_trans_op, pivot_rot_op, translate_op, rot_op,
                shear_op, scale_op, inv_pivot_rot_op, inv_pivot_trans_op};
    xformable.SetXformOpOrder(xformops, resetXformStack);
    if(common)
        return unit.fail("Failed at two translate + rot pivot");
    
    xformable.ClearXformOpOrder();
    xformops = {pivot_trans_op, translate_op, rot_op,
                shear_op, scale_op, inv_pivot_trans_op};
    xformable.SetXformOpOrder(xformops, resetXformStack);
    if(common)
        return unit.fail("Failed at translate before rot + no rp");

    xformable.ClearXformOpOrder();
    xformops = {translate_op, rot_op,
                shear_op, scale_op, inv_pivot_trans_op};
    xformable.SetXformOpOrder(xformops, resetXformStack);
    if(common)
        return unit.fail("Failed at missing pivot when inv pivot is presented");

    // Basic common API component path should suppress shear and pivot rotate
    {
        xformable.ClearXformOpOrder();
        HUSD_Xform xformer(writelock);
        UT_XformOrder order(UT_XformOrder::SRT, UT_XformOrder::XYZ);
        UT_Vector3D pv(5, 10, 15);
        UT_Vector3D pr(10, 20, 30);  // should be ignored for basic

        HUSD_XformEntry::HUSD_XformEntryComponents comps = {
            UT_Vector3D(1, 2, 3),     // T
            UT_Vector3D(45, 60, 90),  // R
            UT_Vector3D(2, 3, 4),     // S
            UT_Vector3D(1, 2, 3)      // Shear — should be zeroed
        };
        HUSD_XformEntryMap xform_map;
        xformer.appendToXformMap(HUSD_FindPrims(writelock, "/geo/cube"),
                nullptr, &comps, HUSD_TimeCode(),
                &pv, &pr, &order, xform_map);
        if (!xformer.applyXforms(xform_map,
                UT_StringRef(), HUSD_XFORM_BASIC_COMMON_API_OVERWRITE))
            return unit.fail("Failed basic common API component overwrite");

        GfVec3d got_t;
        UsdHoudiniHoudiniXformCommonAPI::Rotation got_rot;
        GfVec3f got_s, got_sh, got_p, got_pr;
        common.GetXformVectors(&got_t, &got_rot, &got_s, &got_sh,
            &got_p, &got_pr, UsdTimeCode::Default());
        GfVec3f got_r = got_rot.IsEuler()
            ? got_rot.GetEulerAngles() : GfVec3f(0.0f);

        if (!isEqual(got_t, UT_Vector3D(1, 2, 3)))
            return unit.fail("Basic API component T mismatch");
        if (!isEqual(got_r, UT_Vector3D(45, 60, 90)))
            return unit.fail("Basic API component R mismatch");
        if (!isEqual(got_s, UT_Vector3D(2, 3, 4)))
            return unit.fail("Basic API component S mismatch");
        if (!isEqual(got_sh, UT_Vector3D(0, 0, 0)))
            return unit.fail("Shear should be zero for basic common API");
        if (!isEqual(got_pr, UT_Vector3D(0, 0, 0)))
            return unit.fail("PivotRotate should be zero for basic common API");
    }

    return unit.ok();
}

static bool
testAccumulate()
{
    UT_TestUnit unit("HUSD Xform accumulate reduction tests");

    auto validate = [&](const char *label,
                        UsdGeomXformable &xformable,
                        const UT_Vector3D &target_translation,
                        const UT_Vector3F &target_rotation,
                        const UT_Vector3F &target_scale,
                        const UT_Vector3F &target_pivot,
                        UsdHoudiniHoudiniXformCommonAPI::RotationOrder target_rot_order,
                        // shear and pivot roate are added by XUSD Xform Common API
                        const UT_Vector3F &target_shear = UT_Vector3F(0.0, 0.0, 0.0),
                        const UT_Vector3F &target_pivot_rot = UT_Vector3F(0.0, 0.0, 0.0))
    {
        GfVec3d got_translation;
        UsdHoudiniHoudiniXformCommonAPI::Rotation got_rotation;
        GfVec3f got_shear, got_scale, got_pivot, got_pivot_rot;
        UsdHoudiniHoudiniXformCommonAPI api(xformable.GetPrim());
        if (!api.GetXformVectorsByAccumulation(&got_translation, &got_rotation,
                                               &got_scale, &got_shear,
                                               &got_pivot, &got_pivot_rot,
                                               UsdTimeCode::Default()))
        {
            return unit.fail("%s: GetXformVectorsByAccumulation failed", label);
        }

        GfVec3f got_rot_euler = got_rotation.IsEuler()
            ? got_rotation.GetEulerAngles() : GfVec3f(0.0f);
        UsdHoudiniHoudiniXformCommonAPI::RotationOrder got_rot_order =
            got_rotation.IsEuler()
            ? got_rotation.GetRotationOrder()
            : UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ;

        if (!isEqual(got_translation, target_translation, 1e-5) ||
            !isEqual(got_rot_euler, target_rotation, 1e-5) ||
            !isEqual(got_scale, target_scale, 1e-5) ||
            !isEqual(got_pivot, target_pivot, 1e-5) ||
            !isEqual(got_shear, target_shear, 1e-5) ||
            !isEqual(got_pivot_rot, target_pivot_rot, 1e-5) ||
            got_rot_order != target_rot_order)
        {
            UTdebugPrint(label, " mismatch:",
                         " t ", GusdUT_Gf::Cast(got_translation),
                         " r ", GusdUT_Gf::Cast(got_rot_euler),
                         " s ", GusdUT_Gf::Cast(got_scale),
                         " p ", GusdUT_Gf::Cast(got_pivot),
                         " sh ", GusdUT_Gf::Cast(got_shear),
                         " pr ", GusdUT_Gf::Cast(got_pivot_rot),
                         " ro ", got_rot_order);
            UTdebugPrint(label, " expecting:",
                         " target_t ", target_translation,
                         " target_r ", target_rotation,
                         " target_s ", target_scale,
                         " target_p ", target_pivot,
                         " target_sh ", target_shear,
                         " target_pr", target_pivot_rot,
                         " target_ro ", target_rot_order);
            return unit.fail("%s: accumulated values mismatch", label);
        }

        return true;
    };

    //-----------------<Pixar Test Cases Starts>-----------------
    //Source: https://github.com/PixarAnimationStudios/OpenUSD/blob/dev/pxr/usd/usdGeom/testenv/testUsdGeomXformCommonAPI.py

    // Single axis rotation about X
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddRotateXOp().Set(45.0f);
        if (!validate("RotX", xformable, UT_Vector3D(0, 0, 0),
                      UT_Vector3D(45, 0, 0), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(0, 0, 0),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }
    // Single axis rotation about Y
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddRotateYOp().Set(60.0f);
        if (!validate("RotY", xformable, UT_Vector3D(0, 0, 0),
                      UT_Vector3D(0, 60, 0), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(0, 0, 0),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }
    // Single axis rotation about Z
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddRotateZOp().Set(115.0f);
        if (!validate("RotZ", xformable, UT_Vector3D(0, 0, 0),
                      UT_Vector3D(0, 0, 115), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(0, 0, 0),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }

    // Three axis rotation with a non-default (non-XYZ) rotation order.
    // Note that we have to add a pivot with a non-conforming name for
    // it to be incompatible.
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("myRotatePivot"))
                 .Set(GfVec3f(-3.0f, -2.0f, -1.0f));
        xformable.AddRotateZYXOp(UsdGeomXformOp::PrecisionFloat)
                 .Set(GfVec3f(15.0f, 60.0f, 30.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("myRotatePivot"),
                                 /* isInverseOp */ true);
        if (!validate("RotZYX pivot", xformable, UT_Vector3D(0, 0, 0),
                      UT_Vector3D(15, 60, 30), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(-3, -2, -1),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderZYX))
            return false;
    }

    // Accumulation of translation ops
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble, TfToken("transOne"))
                 .Set(GfVec3d(1.0, 2.0, 3.0));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble, TfToken("transTwo"))
                 .Set(GfVec3d(9.0, 8.0, 7.0));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble, TfToken("transThree"))
                 .Set(GfVec3d(10.0, 20.0, 30.0));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble, TfToken("transFour"))
                 .Set(GfVec3d(90.0, 80.0, 70.0));
        if (!validate("TranslationsOnly", xformable, UT_Vector3D(110, 110, 110),
                      UT_Vector3D(0, 0, 0), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(0, 0, 0),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }

    // Rotate op with a pivot
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"))
                 .Set(GfVec3f(3.0f, 6.0f, 9.0f));
        xformable.AddRotateXYZOp(UsdGeomXformOp::PrecisionFloat)
                 .Set(GfVec3f(0.0f, 45.0f, 0.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"),
                                 /* isInverseOp */ true);
        if (!validate("RotateWithPivot", xformable, UT_Vector3D(0, 0, 0),
                      UT_Vector3D(0, 45, 0), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(3, 6, 9),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }

    // Accumulation of scale ops
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleOne"))
                 .Set(GfVec3f(1.0f, 2.0f, 3.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleTwo"))
                 .Set(GfVec3f(2.0f, 4.0f, 6.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleThree"))
                 .Set(GfVec3f(10.0f, 20.0f, 30.0f));
        if (!validate("ScalesOnly", xformable, UT_Vector3D(0, 0, 0),
                      UT_Vector3D(0, 0, 0), UT_Vector3D(20, 160, 540),
                      UT_Vector3D(0, 0, 0),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }

    // Accumulation of scale ops with a pivot
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"))
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleOne"))
                 .Set(GfVec3f(10.0f, 20.0f, 30.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleTwo"))
                 .Set(GfVec3f(0.5f, 0.5f, 0.5f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"),
                                 /* isInverseOp */ true);
        if (!validate("ScalesWithPivot", xformable, UT_Vector3D(0, 0, 0),
                      UT_Vector3D(0, 0, 0), UT_Vector3D(5, 10, 15),
                      UT_Vector3D(15, 25, 35),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }

    // Accumulation of scale ops with a pivot and translation
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(123.0, 456.0, 789.0));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"))
                 .Set(GfVec3f(-222.0f, -444.0f, -666.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleOne"))
                 .Set(GfVec3f(2.0f, 2.0f, 2.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleTwo"))
                 .Set(GfVec3f(2.0f, 2.0f, 2.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleThree"))
                 .Set(GfVec3f(2.0f, 2.0f, 2.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"),
                                 /* isInverseOp */ true);
        if (!validate("ScalesWithPivotAndTranslate", xformable,
                      UT_Vector3D(123, 456, 789), UT_Vector3D(0, 0, 0),
                      UT_Vector3D(8, 8, 8), UT_Vector3D(-222, -444, -666),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }

    // Typical xformOp order as exported from Maya. Start out with an xformable
    // that DOES conform (xformOp naming is important).
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        bool does_reset = false;

        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(1.0, 2.0, 3.0));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatepivot"))
                 .Set(GfVec3f(10.0f, 20.0f, 30.0f));
        xformable.AddRotateXYZOp(UsdGeomXformOp::PrecisionFloat)
                 .Set(GfVec3f(0.0f, 45.0f, 0.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatepivot"),
                                 /* isInverseOp */ true);
        if (!validate("Maya conform", xformable, UT_Vector3D(1, 2, 3),
                      UT_Vector3D(0, 45, 0), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(10, 20, 30),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;

        // Now add a scale pivot and a scale. The scale pivot will initially have
        // the same position as the rotate pivot.
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"))
                 .Set(GfVec3f(10.0f, 20.0f, 30.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("mayaScale"))
                 .Set(GfVec3f(2.0f, 4.0f, 6.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"),
                                 /* isInverseOp */ true);
        if (!validate("Maya + scale pivot", xformable, UT_Vector3D(1, 2, 3),
                      UT_Vector3D(0, 45, 0), UT_Vector3D(2, 4, 6),
                      UT_Vector3D(10, 20, 30),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
        
        // Now change the scalePivot position so that it no longer matches the
        // rotate pivot. This means the xformOps both do not conform to common AND
        // also cannot be reduced to conform by accumulation. We should resort to
        // local transform decomposition and get a zero pivot in that case.
        std::vector<UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&does_reset);
        if (ops.size() > 4)
            ops[4].Set(GfVec3f(200.0f, 300.0f, 400.0f));
        xformable.SetXformOpOrder(ops);

        if (!validate("Maya + mismatched scale pivot", xformable,
                      UT_Vector3D(-1572.9191898578665, -898.0, -1253.9343417595162),
                      UT_Vector3D(0, 45, 0), UT_Vector3D(2, 4, 6),
                      UT_Vector3D(0, 0, 0),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }

    // An xformOp order as exported from Maya with a translate and rotate and
    // scale pivots specified, but only rotation and no scaling.
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(20.0, 40.0, 60.0));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"))
                 .Set(GfVec3f(50.0f, 150.0f, 250.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"),
                                 /* isInverseOp */ true);
        if (!validate("RotatePivotOnly", xformable, UT_Vector3D(20, 40, 60),
                      UT_Vector3D(0, 0, 0), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(50, 150, 250),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }

    // An xformOp order as exported from Maya with a translate and rotate and
    // scale pivots specified, but only rotation and no scaling.
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(11.0, 22.0, 33.0));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"))
                 .Set(GfVec3f(111.0f, 222.0f, 333.0f));
        xformable.AddRotateZOp().Set(44.0f);
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"),
                                 /* isInverseOp */ true);
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"))
                 .Set(GfVec3f(111.0f, 222.0f, 333.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"),
                                 /* isInverseOp */ true);
        if (!validate("RotateNoScale", xformable, UT_Vector3D(11, 22, 33),
                      UT_Vector3D(0, 0, 44), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(111, 222, 333),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }

    // An xformOp order as exported from Maya with a translate and identical
    // rotate and scale pivots specified, but no actual rotation or scaling.
    // The xformOp order contains only translations.
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(300.0, 600.0, 900.0));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"))
                 .Set(GfVec3f(-100.0f, -300.0f, -500.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"),
                                 /* isInverseOp */ true);
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"))
                 .Set(GfVec3f(-100.0f, -300.0f, -500.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"),
                                 /* isInverseOp */ true);
        if (!validate("IdenticalPivots", xformable, UT_Vector3D(300, 600, 900),
                      UT_Vector3D(0, 0, 0), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(-100, -300, -500),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ))
            return false;
    }
    //-----------------<Pixar Test Cases Ends>-----------------

    // Pivot rotate present; real rotate is non-XYZ.
    // Repeated Translate
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));

        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot)
                 .Set(GfVec3f(5.0f, 6.0f, 7.0f));
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f)); // pivot rotate
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(11.0, 12.0, 13.0));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble,
                  TfToken("SecondTranslate"))
                 .Set(GfVec3d(11.0, 12.0, 13.0));
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateZYX,
                             UsdGeomXformOp::PrecisionFloat)
                 .Set(GfVec3f(9.0f, 8.0f, 7.0f)); // real rotate (non-XYZ)
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot,
                             /* isInverseOp */ true)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot,
                                 /* isInverseOp */ true)
                 .Set(GfVec3f(5.0f, 6.0f, 7.0f));

        if (!validate("PivotRotateWithYZX + RepeatedTranslate", xformable,
                      UT_Vector3D(22, 24, 26), UT_Vector3D(9, 8, 7),
                      UT_Vector3D(1, 1, 1), UT_Vector3D(5, 6, 7),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderZYX,
                      UT_Vector3D(0, 0, 0),
                      UT_Vector3D(15, 25, 35)))
            return false;
    }

    // Pivot rotate pair with no main rotate(should pass is_compatible)
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));

        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot)
                 .Set(GfVec3f(2.0f, 3.0f, 4.0f));
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot)
                 .Set(GfVec3f(0.0f, 90.0f, 0.0f)); // pivot rotate
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(10.0, 0.0, 0.0));
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot,
                             /* isInverseOp */ true)
                 .Set(GfVec3f(0.0f, 90.0f, 0.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, UsdHoudiniTokens->pivot,
                                 /* isInverseOp */ true)
                 .Set(GfVec3f(2.0f, 3.0f, 4.0f));

        if (!validate("PivotRotateOnly", xformable,
                      UT_Vector3D(10, 0, 0), UT_Vector3D(0, 0, 0),
                      UT_Vector3D(1, 1, 1), UT_Vector3D(2, 3, 4),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ,
                      UT_Vector3D(0, 0, 0),
                      UT_Vector3D(0, 90, 0)))
            return false;
    }

    // Multiple sclae + shear should be preserved
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"))
                 .Set(GfVec3f(-222.0f, -444.0f, -666.0f));
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat,
                             UsdHoudiniTokens->pivot)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));    // pivot rotate
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(11.0, 12.0, 13.0));       // translate

        UsdGeomXformOp shearOp =  xformable.AddTransformOp(UsdGeomXformOp::PrecisionDouble,
                                     TfToken("shear"));
        UT_Matrix4D shearM(1.0);
        shearM[1][0] = 1.0; shearM[2][0] = 2.0; shearM[2][1] = 3.0;
        shearOp.Set(GusdUT_Gf::Cast(shearM)); //shear

        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleOne"))
                 .Set(GfVec3f(2.0f, 2.0f, 2.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleTwo"))
                 .Set(GfVec3f(2.0f, 2.0f, 2.0f));
        xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat, TfToken("scaleThree"))
                 .Set(GfVec3f(2.0f, 2.0f, 2.0f));
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat,
                             UsdHoudiniTokens->pivot,
                             /* isInverseOp */ true)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));    // pivot rotate
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"),
                            /* isInverseOp */ true);    // pivot
        if (!validate("Accum Scale + Preserve Shear with PR Presented", xformable,
                      UT_Vector3D(11, 12, 13), UT_Vector3D(0, 0, 0),
                      UT_Vector3D(8, 8, 8), UT_Vector3D(-222, -444, -666),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ,
                      UT_Vector3D(1, 2, 3),   // shear
                      UT_Vector3D(15, 25, 35))) // pivotRotate
            return false;
    }

    // Make sure when pivot rotate is presented it could accept missing pivot translate
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat,
                             UsdHoudiniTokens->pivot)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));    // pivot rotate
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(300.0, 300.0, 300.0));   // translate
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat,
                             UsdHoudiniTokens->pivot,
                             /* isInverseOp */ true)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));    // inverse pivot rotate
        if (!validate("Only translate and pivot rotate", xformable, 
                      UT_Vector3D(300.0, 300.0, 300.0),
                      UT_Vector3D(0, 0, 0), UT_Vector3D(1, 1, 1),
                      UT_Vector3D(0, 0, 0),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ,
                      UT_Vector3D(0, 0, 0),
                      UT_Vector3D(15, 25, 35)
                    ))
            return false;
    }

    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat,
                             UsdHoudiniTokens->pivot)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));    // pivot rotate
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(300.0, 600.0, 900.0));   // translate
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"))
                 .Set(GfVec3f(-100.0f, -300.0f, -500.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"),
                                 /* isInverseOp */ true);
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"))
                 .Set(GfVec3f(-100.0f, -300.0f, -500.0f));
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"),
                                 /* isInverseOp */ true);
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat,
                             UsdHoudiniTokens->pivot,
                             /* isInverseOp */ true)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));    // inverse pivot rotate    

        if (!validate("Pivot Rotate + Maya Style pivots Won't be Reducible", xformable, 
                      UT_Vector3D(378.62243567607214, 688.2571635991746, 801.8398393427066),
                      UT_Vector3D(-3.9756934e-16, 3.1805547e-15, 1.0933157e-15), 
                      UT_Vector3D(1, 1, 1),
                      UT_Vector3D(0, 0, 0),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ,
                      UT_Vector3D(3.8163916e-17, -1.110223e-16, -1.3877788e-17),
                      UT_Vector3D(0, 0, 0)
                    ))
            return false;
    }

    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdGeomXformable xformable(stage->GetPrimAtPath(SdfPath("/geo/cube")));
        
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"))
                 .Set(GfVec3f(-100.0f, -300.0f, -500.0f));
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat,
                             UsdHoudiniTokens->pivot)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));    // pivot rotate
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble)
                 .Set(GfVec3d(300.0, 600.0, 900.0));   // translate
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("rotatePivot"),
                                 /* isInverseOp */ true);
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"))
                 .Set(GfVec3f(-100.0f, -300.0f, -500.0f));
        xformable.AddXformOp(UsdGeomXformOp::TypeRotateXYZ,
                             UsdGeomXformOp::PrecisionFloat,
                             UsdHoudiniTokens->pivot,
                             /* isInverseOp */ true)
                 .Set(GfVec3f(15.0f, 25.0f, 35.0f));    // inverse pivot rotate 
        xformable.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, TfToken("scalePivot"),
                                 /* isInverseOp */ true);   

        if (!validate("Reducible Maya Pivots with Pivot Rotate", xformable, 
                      UT_Vector3D(300.0, 600.0, 900.0),
                      UT_Vector3D(0, 0, 0), 
                      UT_Vector3D(1, 1, 1),
                      UT_Vector3D(-100.0, -300.0, -500.0),
                      UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ,
                      UT_Vector3D(0, 0, 0),
                      UT_Vector3D(15, 25, 35)
                    ))
            return false;
    }

    // Component path: test combination
    {
        HUSD_DataHandle datahandle = createStage();
        HUSD_AutoWriteLock writelock(datahandle);
        UsdStageRefPtr stage = writelock.data()->stage();
        UsdPrim prim = stage->GetPrimAtPath(SdfPath("/geo/cube"));
        UsdHoudiniHoudiniXformCommonAPI common(prim);
        HUSD_Xform xformer(writelock);
        UT_XformOrder order(UT_XformOrder::SRT, UT_XformOrder::XYZ);
        UT_Vector3D pivot(0, 0, 0), pivot_rot(0, 0, 0);

        // Seed with known values
        common.SetTranslate(GfVec3d(10, 20, 30), UsdTimeCode::Default());
        common.SetRotate(
            UsdHoudiniHoudiniXformCommonAPI::Rotation(
                GfVec3f(15, 25, 35),
                UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ),
            UsdTimeCode::Default());
        common.SetScale(GfVec3f(2, 3, 4), UsdTimeCode::Default());
        common.SetShear(GfVec3f(1, 0, 0), UsdTimeCode::Default());

        // Apply delta
        HUSD_XformEntry::HUSD_XformEntryComponents comps = {
            UT_Vector3D(5, 5, 5),     // T delta (additive)
            UT_Vector3D(10, 10, 10),  // R delta (additive)
            UT_Vector3D(2, 2, 2),     // S delta (multiplicative)
            UT_Vector3D(0, 1, 0)      // Shear delta (additive)
        };
        HUSD_XformEntryMap xform_map;
        xformer.appendToXformMap(HUSD_FindPrims(writelock, "/geo/cube"),
                nullptr, &comps, HUSD_TimeCode(),
                &pivot, &pivot_rot, &order, xform_map);
        if (!xformer.applyXforms(xform_map,
                UT_StringRef(), HUSD_XFORM_COMMON_API_APPEND))
            return unit.fail("Component append: applyXforms failed");

        GfVec3d got_t;
        UsdHoudiniHoudiniXformCommonAPI::Rotation got_rot;
        GfVec3f got_s, got_sh, got_p, got_pr;
        common.GetXformVectors(&got_t, &got_rot, &got_s, &got_sh,
            &got_p, &got_pr, UsdTimeCode::Default());
        GfVec3f got_r = got_rot.IsEuler()
            ? got_rot.GetEulerAngles() : GfVec3f(0.0f);

        if (!isEqual(got_t, UT_Vector3D(15, 25, 35)) ||  // T: additive
            !isEqual(got_r, UT_Vector3D(25, 35, 45)) ||  // R: additive
            !isEqual(got_s, UT_Vector3D(4, 6, 8))    ||  // S: multiplicative
            !isEqual(got_sh, UT_Vector3D(1, 1, 0)))      // Shear: additive
        {
            UTdebugPrint("Component append mismatch:",
                " t ", GusdUT_Gf::Cast(got_t),
                " r ", GusdUT_Gf::Cast(got_r),
                " s ", GusdUT_Gf::Cast(got_s),
                " sh ", GusdUT_Gf::Cast(got_sh));
            return unit.fail("Component append combine mismatch");
        }
    }

    return unit.ok();
}

static bool
testOrient()
{
    UT_TestUnit unit("HUSD Orient (quaternion) rotation tests");

    HUSD_DataHandle datahandle = createStage();
    HUSD_AutoWriteLock writelock(datahandle);
    UsdStageRefPtr stage = writelock.data()->stage();

    using XCA = UsdHoudiniHoudiniXformCommonAPI;

    // Test 1: Orient basic round-trip via SetRotate/GetXformVectors
    {
        UsdPrim prim = stage->DefinePrim(SdfPath("/orient_basic"),
                                          TfToken("Xform"));
        XCA common(prim);

        // 45 degree rotation around Y axis as quaternion
        float angle = 45.0f * M_PI / 180.0f;
        GfQuatf quat(cosf(angle / 2.0f),
                      GfVec3f(0.0f, sinf(angle / 2.0f), 0.0f));

        if (!common.SetRotate(XCA::Rotation(quat), UsdTimeCode::Default()))
            return unit.fail("SetRotate with orient failed");

        GfVec3d got_t;
        XCA::Rotation got_rot;
        GfVec3f got_s, got_sh, got_p, got_pr;
        if (!common.GetXformVectors(&got_t, &got_rot, &got_s, &got_sh,
                                    &got_p, &got_pr, UsdTimeCode::Default()))
            return unit.fail("GetXformVectors failed for orient prim");

        if (!got_rot.IsOrient())
            return unit.fail("Expected orient rotation, got Euler");

        GfQuatf got_q = got_rot.GetQuaternion();
        if (!GfIsClose(got_q.GetReal(), quat.GetReal(), 1e-5f) ||
            !GfIsClose(got_q.GetImaginary(), quat.GetImaginary(), 1e-5f))
        {
            return unit.fail("Orient round-trip quaternion mismatch");
        }
    }

    // Test 2: Orient with full SetXformVectors
    {
        UsdPrim prim = stage->DefinePrim(SdfPath("/orient_full"),
                                          TfToken("Xform"));
        XCA common(prim);

        GfQuatf quat = GfQuatf(0.5f, 0.5f, 0.5f, 0.5f).GetNormalized();
        GfVec3d translate(1.0, 2.0, 3.0);
        GfVec3f scale(2.0f, 3.0f, 4.0f);

        if (!common.SetXformVectors(translate, XCA::Rotation(quat),
                scale, GfVec3f(0.0f), GfVec3f(0.0f), GfVec3f(0.0f),
                UsdTimeCode::Default()))
            return unit.fail("SetXformVectors with orient failed");

        // Verify xformOpOrder contains orient
        UsdGeomXformable xformable(prim);
        bool resetStack;
        auto ops = xformable.GetOrderedXformOps(&resetStack);
        bool foundOrient = false;
        for (auto &op : ops)
        {
            if (op.GetOpType() == UsdGeomXformOp::TypeOrient)
                foundOrient = true;
        }
        if (!foundOrient)
            return unit.fail("xformOpOrder should contain orient op");
    }

    // Test 3: Mutual exclusivity of OpRotate and OpOrient
    {
        UsdPrim prim = stage->DefinePrim(SdfPath("/orient_mutex"),
                                          TfToken("Xform"));
        XCA common(prim);

        // This should fail with a coding error
        TfErrorMark errMark;
        XCA::Ops ops = common.CreateXformOps(
            (XCA::OpFlags)(XCA::OpRotate | XCA::OpOrient));
        if (ops.rotateOp)
            return unit.fail("OpRotate|OpOrient should fail");
        errMark.Clear();
    }

    // Test 4: Cannot create orient on prim with existing Euler rotate
    {
        UsdPrim prim = stage->DefinePrim(SdfPath("/orient_conflict"),
                                          TfToken("Xform"));
        XCA common(prim);

        // Create Euler rotate first
        common.CreateXformOps(XCA::RotationOrderXYZ, XCA::OpRotate);

        // Try to create orient - should fail
        TfErrorMark errMark;
        XCA::Ops ops = common.CreateXformOps(XCA::OpOrient);
        if (ops.rotateOp)
            return unit.fail("Orient on Euler prim should fail");
        errMark.Clear();
    }

    // Test 5: Rotation::GetQuaternion (starting from Euler angles)
    {
        GfVec3f euler(0.0f, 90.0f, 0.0f);  // 90 deg around Y
        XCA::Rotation rot(euler, XCA::RotationOrderXYZ);
        GfQuatf q = rot.GetQuaternion();
        // 90 deg around Y: quat should be (cos(45), 0, sin(45), 0)
        float expected_real = cosf(45.0f * M_PI / 180.0f);
        float expected_imag = sinf(45.0f * M_PI / 180.0f);
        if (!GfIsClose(fabsf(q.GetReal()), expected_real, 1e-4f) ||
            !GfIsClose(fabsf(q.GetImaginary()[1]), expected_imag, 1e-4f))
        {
            UTdebugPrint("GetQuaternion mismatch: got ",
                q.GetReal(), " ", GusdUT_Gf::Cast(q.GetImaginary()));
            return unit.fail("Rotation::GetQuaternion mismatch");
        }
    }

    // Test 6: Compatibility check with orient
    {
        UsdPrim prim = stage->DefinePrim(SdfPath("/orient_compat"),
                                          TfToken("Xform"));
        XCA common(prim);

        common.CreateXformOps(XCA::OpTranslate, XCA::OpOrient, XCA::OpScale);
        // Prim should be compatible
        if (!common)
            return unit.fail("Orient prim should be compatible");
    }

    return unit.ok();
}

static bool
TESTHUSD_Xform()
{
    bool ok = true;

    ok &= testBasic();
    ok &= testInvalid();
    ok &= testAccumulate();
    ok &= testOrient();
    return ok;
}

TEST_REGISTER_FN("HUSD_Xform", TESTHUSD_Xform)

PXR_NAMESPACE_CLOSE_SCOPE
