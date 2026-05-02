/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	TESTHUSD_SizeAutoCollInstances.C (C++)
 *
 * COMMENTS:	Tests for point instance matching in the
 *		%size auto collection.
 *
 */

#include <HUSD/HUSD_Constants.h>
#include <HUSD/HUSD_DataHandle.h>
#include <HUSD/HUSD_FindPrims.h>
#include <HUSD/HUSD_TimeCode.h>
#include <HUSD/XUSD_Data.h>
#include <HUSD/XUSD_Utils.h>
#include <UT/UT_Array.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_TestManager.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/base/gf/frustum.h>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

// Helper: extract instance IDs from a HUSD_FindPrims result for a
// given instancer path.
static UT_Array<int64>
getIdsForPath(const HUSD_FindPrims &findprims,
    const UT_StringRef &path)
{
    const auto &id_map = findprims.getPointInstancerIds();
    auto it = id_map.find(path);

    if (it != id_map.end())
        return UT_Array<int64>(it->second);

    return UT_Array<int64>();
}

// Helper: check whether a specific ID is present in an array.
static bool
containsId(const UT_Array<int64> &ids, int64 id)
{
    for (auto &&v : ids)
    {
        if (v == id)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------
// Instances have unit cube prototype with scales:
//   0: (1,1,1) => volume  1.0
//   1: (2,2,2) => volume  8.0
//   2: (3,3,3) => volume 27.0
//   3: (5,5,5) => volume 125.0
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// Test: %size(minvolume=10) matches volumes >= 10
// ---------------------------------------------------------------
static bool
testSizeMinVolume()
{
    UT_TestUnit unit("Size minvolume=10");

    HUSD_DataHandle dh("test_size_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%size(minvolume=10)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 2) || !containsId(ids, 3))
        return unit.fail("Expected IDs 2, 3");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %size(maxvolume=10) matches volumes <= 10
// ---------------------------------------------------------------
static bool
testSizeMaxVolume()
{
    UT_TestUnit unit("Size maxvolume=10");

    HUSD_DataHandle dh("test_size_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%size(maxvolume=10)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1))
        return unit.fail("Expected IDs 0, 1");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %size(minvolume=5, maxvolume=30) range filter
// ---------------------------------------------------------------
static bool
testSizeMinMaxVolume()
{
    UT_TestUnit unit("Size minvolume=5 maxvolume=30");

    HUSD_DataHandle dh("test_size_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%size(minvolume=5, maxvolume=30)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 1) || !containsId(ids, 2))
        return unit.fail("Expected IDs 1, 2");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %size matching all instances
// ---------------------------------------------------------------
static bool
testSizeAllMatch()
{
    UT_TestUnit unit("Size all match");

    HUSD_DataHandle dh("test_size_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%size(minvolume=0.1)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 4)
        return unit.fail("Expected 4 IDs, got %lld",
            (long long)ids.size());

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %size matching no instances
// ---------------------------------------------------------------
static bool
testSizeNoneMatch()
{
    UT_TestUnit unit("Size none match");

    HUSD_DataHandle dh("test_size_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%size(minvolume=1000)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 0)
        return unit.fail("Expected 0 IDs, got %lld",
            (long long)ids.size());

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %size matching single instance
// ---------------------------------------------------------------
static bool
testSizeSingleMatch()
{
    UT_TestUnit unit("Size single match");

    HUSD_DataHandle dh("test_size_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%size(minvolume=100, maxvolume=200)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 1)
        return unit.fail("Expected 1 ID, got %lld",
            (long long)ids.size());

    if (ids(0) != 3)
        return unit.fail("Expected ID 3, got %lld",
            (long long)ids(0));

    return unit.ok();
}

// ---------------------------------------------------------------
// Screen area tests use test_camera_instances.usda.
// Camera: focalLength=50, hAperture=36, vAperture=24.
// ScreenInstancer has 4 instances at Z=-10 with scales 1,2,4,6.
//
// Measured screen area percentages (from diagnostic below):
//   Instance 0 (scale 1): area=3.21%   clipped=3.21%
//   Instance 1 (scale 2): area=14.29%  clipped=14.29%
//   Instance 2 (scale 4): area=72.34%  clipped=69.44%
//   Instance 3 (scale 6): area=212.59% clipped=100%
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// Diagnostic: compute and print exact screen area percentages
// for each ScreenInstancer instance, replicating the projection
// logic from XUSD_AutoCollection::testBBoxScreenArea.
// Not called from the test registration -- kept here so the
// percentages above can be re-derived if the test data changes.
// ---------------------------------------------------------------
static bool
testSizeScreenAreaDiagnostic()
{
    UT_TestUnit unit("Size screen area diagnostic (prints only)");

    HUSD_DataHandle dh("test_camera_instances.usda");
    HUSD_AutoReadLock lock(dh);

    auto data = lock.constData();
    if (!data || !data->isStageValid())
        return unit.fail("Failed to open stage");

    UsdStageRefPtr stage = data->stage();
    UsdTimeCode timecode = UsdTimeCode::Default();

    UsdGeomCamera cam(
        stage->GetPrimAtPath(HUSDgetSdfPath("/World/Cam")));
    if (!cam)
        return unit.fail("Camera not found");

    GfFrustum frustum = cam.GetCamera(timecode).GetFrustum();
    GfMatrix4d viewmat = frustum.ComputeViewMatrix();

    UsdGeomPointInstancer instancer(
        stage->GetPrimAtPath(
            HUSDgetSdfPath("/World/ScreenInstancer")));
    if (!instancer)
        return unit.fail("ScreenInstancer not found");

    UsdGeomBBoxCache bboxcache(timecode,
        UsdGeomImageable::GetOrderedPurposeTokens(), true, true);

    int count = instancer.GetInstanceCount(timecode);
    std::cout << "\n  ScreenInstancer screen area diagnostics ("
              << count << " instances):\n";

    for (int idx = 0; idx < count; idx++)
    {
        GfBBox3d bbox =
            bboxcache.ComputePointInstanceWorldBound(
                instancer, idx);
        GfRange3d range = bbox.ComputeAlignedRange();

        fpreal64 minx, miny, maxx, maxy;
        for (int j = 0; j < 8; j++)
        {
            GfVec3d corner = range.GetCorner(j);
            GfVec4d fcorner =
                GfVec4d(corner[0], corner[1], corner[2], 1)
                * viewmat;
            fpreal64 dist = fcorner[2];

            std::vector<GfVec3d> fcorners =
                frustum.ComputeCornersAtDistance(dist);
            fpreal64 fleft =
                (GfVec4d(fcorners[0][0], fcorners[0][1],
                         fcorners[0][2], 1)
                 * viewmat)[0];
            fpreal64 fbottom =
                (GfVec4d(fcorners[0][0], fcorners[0][1],
                         fcorners[0][2], 1)
                 * viewmat)[1];

            fpreal64 ncornerx = fcorner[0] / fleft;
            fpreal64 ncornery = fcorner[1] / fbottom;

            if (j == 0)
            {
                minx = maxx = ncornerx;
                miny = maxy = ncornery;
            }
            else
            {
                if (ncornerx < minx) minx = ncornerx;
                else if (ncornerx > maxx) maxx = ncornerx;
                if (ncornery < miny) miny = ncornery;
                else if (ncornery > maxy) maxy = ncornery;
            }
        }

        fpreal64 area =
            100.0 * (maxx - minx) * (maxy - miny) / 4.0;

        // Also compute with clipping.
        fpreal64 cminx, cminy, cmaxx, cmaxy;
        for (int j = 0; j < 8; j++)
        {
            GfVec3d corner = range.GetCorner(j);
            GfVec4d fcorner =
                GfVec4d(corner[0], corner[1], corner[2], 1)
                * viewmat;
            fpreal64 dist = fcorner[2];

            std::vector<GfVec3d> fcorners =
                frustum.ComputeCornersAtDistance(dist);
            fpreal64 fleft =
                (GfVec4d(fcorners[0][0], fcorners[0][1],
                         fcorners[0][2], 1)
                 * viewmat)[0];
            fpreal64 fbottom =
                (GfVec4d(fcorners[0][0], fcorners[0][1],
                         fcorners[0][2], 1)
                 * viewmat)[1];

            fpreal64 ncornerx =
                std::clamp(fcorner[0] / fleft, -1.0, 1.0);
            fpreal64 ncornery =
                std::clamp(fcorner[1] / fbottom, -1.0, 1.0);

            if (j == 0)
            {
                cminx = cmaxx = ncornerx;
                cminy = cmaxy = ncornery;
            }
            else
            {
                if (ncornerx < cminx) cminx = ncornerx;
                else if (ncornerx > cmaxx) cmaxx = ncornerx;
                if (ncornery < cminy) cminy = ncornery;
                else if (ncornery > cmaxy) cmaxy = ncornery;
            }
        }

        fpreal64 clipped_area =
            100.0 * (cmaxx - cminx) * (cmaxy - cminy) / 4.0;

        std::cout << "    Instance " << idx
                  << ": area=" << area << "%"
                  << "  clipped_area=" << clipped_area << "%"
                  << "\n";
    }

    std::cout << std::flush;
    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %size screen area -- minarea only
// ---------------------------------------------------------------
static bool
testSizeScreenAreaMin()
{
    UT_TestUnit unit("Size screen area minarea=10");

    HUSD_DataHandle dh("test_camera_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%size(/World/Cam, minarea=10)", 0, tc);

    // Screen areas: ~3.21, ~14.29, ~72.34, ~212.59.
    // minarea=10 => instances 1,2,3 match.
    auto ids = getIdsForPath(findprims,
        "/World/ScreenInstancer");
    if (ids.size() != 3)
        return unit.fail("Expected 3 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 1) || !containsId(ids, 2) ||
        !containsId(ids, 3))
        return unit.fail("Expected IDs 1, 2, 3");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %size screen area -- minarea and maxarea range
// ---------------------------------------------------------------
static bool
testSizeScreenAreaRange()
{
    UT_TestUnit unit("Size screen area minarea=5 maxarea=73");

    HUSD_DataHandle dh("test_camera_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%size(/World/Cam, minarea=5, maxarea=70)", 0, tc);

    // ~3.21 < 5 (no), ~14.29 in [5,70] (yes),
    // clipped ~69.44 in [5,70] (no), clipped 100 > 70 (no).
    // Instance 2's unclipped area is ~72.34 which would exceeds
    // 70, even though the clipped area is ~69.44 -- verifying
    // that the size collection uses unclipped screen area.
    auto ids = getIdsForPath(findprims,
        "/World/ScreenInstancer");
    if (ids.size() != 1)
        return unit.fail("Expected 1 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 1))
        return unit.fail("Expected ID 1");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %size screen area -- maxarea only (small instances)
// ---------------------------------------------------------------
static bool
testSizeScreenAreaMax()
{
    UT_TestUnit unit("Size screen area maxarea=5");

    HUSD_DataHandle dh("test_camera_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%size(/World/Cam, maxarea=5)", 0, tc);

    // Only instance 0 (~3.21%) is <= 5%.
    auto ids = getIdsForPath(findprims,
        "/World/ScreenInstancer");
    if (ids.size() != 1)
        return unit.fail("Expected 1 ID, got %lld",
            (long long)ids.size());

    if (ids(0) != 0)
        return unit.fail("Expected ID 0, got %lld",
            (long long)ids(0));

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %size screen area with clip -- large instances clamped
// ---------------------------------------------------------------
static bool
testSizeScreenAreaClip()
{
    UT_TestUnit unit("Size screen area clip=true maxarea=30");

    HUSD_DataHandle dh("test_camera_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%size(/World/Cam, maxarea=30, clip=true)", 0, tc);

    // With clip=true, screen coords are clamped to [-1,1].
    // Clipped areas: ~3.21%, ~14.29%, ~69.44%, 100%.
    // Instances with clipped area <= 30: 0 (~3.21%), 1 (~14.29%).
    auto ids = getIdsForPath(findprims,
        "/World/ScreenInstancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1))
        return unit.fail("Expected IDs 0, 1");

    return unit.ok();
}

// ---------------------------------------------------------------
// Main test registration
// ---------------------------------------------------------------
static bool
TESTHUSD_SizeAutoCollInstances()
{
    bool ok = true;

    ok &= testSizeMinVolume();
    ok &= testSizeMaxVolume();
    ok &= testSizeMinMaxVolume();
    ok &= testSizeAllMatch();
    ok &= testSizeNoneMatch();
    ok &= testSizeSingleMatch();
    ok &= testSizeScreenAreaMin();
    ok &= testSizeScreenAreaRange();
    ok &= testSizeScreenAreaMax();
    ok &= testSizeScreenAreaClip();

    return ok;
}

TEST_REGISTER_FN("HUSD_SizeAutoCollInstances",
    TESTHUSD_SizeAutoCollInstances)

PXR_NAMESPACE_CLOSE_SCOPE
