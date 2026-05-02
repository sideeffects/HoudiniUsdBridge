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
 * NAME:	TESTHUSD_BoundAutoCollInstances.C (C++)
 *
 * COMMENTS:	Tests for point instance matching in the
 *		%bound auto collection.
 *
 */

#include <HUSD/HUSD_Constants.h>
#include <HUSD/HUSD_DataHandle.h>
#include <HUSD/HUSD_FindPrims.h>
#include <HUSD/HUSD_TimeCode.h>
#include <UT/UT_Array.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_TestManager.h>

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
// Test: %bound insidepartial (default) against reference prim
// RefBox size=20 => bounds [-10,10]^3.
// Instances at X: 0, 5, 9, 50, 100.
// ---------------------------------------------------------------
static bool
testBoundInsidePartialPrim()
{
    UT_TestUnit unit("Bound insidepartial prim");

    HUSD_DataHandle dh("test_bound_ref_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%bound(/World/RefBox)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 3)
        return unit.fail("Expected 3 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1) ||
        !containsId(ids, 2))
        return unit.fail("Expected IDs 0, 1, 2");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bound containment=inside
// ---------------------------------------------------------------
static bool
testBoundInsidePrim()
{
    UT_TestUnit unit("Bound inside prim");

    HUSD_DataHandle dh("test_bound_ref_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bound(/World/RefBox, containment=inside)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 3)
        return unit.fail("Expected 3 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1) ||
        !containsId(ids, 2))
        return unit.fail("Expected IDs 0, 1, 2");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bound containment=outside
// ---------------------------------------------------------------
static bool
testBoundOutsidePrim()
{
    UT_TestUnit unit("Bound outside prim");

    HUSD_DataHandle dh("test_bound_ref_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bound(/World/RefBox, containment=outside)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 3) || !containsId(ids, 4))
        return unit.fail("Expected IDs 3, 4");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bound containment=outsidepartial
// ---------------------------------------------------------------
static bool
testBoundOutsidePartialPrim()
{
    UT_TestUnit unit("Bound outsidepartial prim");

    HUSD_DataHandle dh("test_bound_ref_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bound(/World/RefBox, containment=outsidepartial)",
        0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 3) || !containsId(ids, 4))
        return unit.fail("Expected IDs 3, 4");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bound with min/max fallback (no target prim)
// Uses test_bbox_instances.usda.  Box [-1,6]x[-1,1]x[-1,1].
// Instances at X: 0, 5, 10, 50, 100.
// ---------------------------------------------------------------
static bool
testBoundFallbackMinMax()
{
    UT_TestUnit unit("Bound fallback min/max");

    HUSD_DataHandle dh("test_bbox_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bound(min=(-1,-1,-1), max=(6,1,1))", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1))
        return unit.fail("Expected IDs 0, 1");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bound with camera frustum -- insidepartial (default)
// Camera at origin looking down -Z.  Instances at:
//   0: (0,0,-5)     in frustum
//   1: (0,0,-20)    in frustum
//   2: (0,0,-100)   in frustum
//   3: (50,0,-20)   outside frustum (far right)
//   4: (0,0,-2000)  beyond far clip
// ---------------------------------------------------------------
static bool
testBoundCameraInsidePartial()
{
    UT_TestUnit unit("Bound camera frustum insidepartial");

    HUSD_DataHandle dh("test_camera_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bound(/World/Cam)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    // Instances 0,1,2 are within the frustum.
    // Instance 3 is far outside.  Instance 4 is past the far clip.
    if (ids.size() != 3)
        return unit.fail("Expected 3 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1) ||
        !containsId(ids, 2))
        return unit.fail("Expected IDs 0, 1, 2");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bound with camera frustum -- outside
// ---------------------------------------------------------------
static bool
testBoundCameraOutside()
{
    UT_TestUnit unit("Bound camera frustum outside");

    HUSD_DataHandle dh("test_camera_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bound(/World/Cam, containment=outside)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    // Instances 3 and 4 are outside the frustum.
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 3) || !containsId(ids, 4))
        return unit.fail("Expected IDs 3, 4");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bound with camera frustum -- inside (fully contained)
// ---------------------------------------------------------------
static bool
testBoundCameraInside()
{
    UT_TestUnit unit("Bound camera frustum inside");

    HUSD_DataHandle dh("test_camera_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bound(/World/Cam, containment=inside)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    // Unit cubes at (0,0,-5), (0,0,-20), (0,0,-100) are small
    // relative to the frustum and fully contained.
    if (ids.size() != 3)
        return unit.fail("Expected 3 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1) ||
        !containsId(ids, 2))
        return unit.fail("Expected IDs 0, 1, 2");

    return unit.ok();
}

// ---------------------------------------------------------------
// Main test registration
// ---------------------------------------------------------------
static bool
TESTHUSD_BoundAutoCollInstances()
{
    bool ok = true;

    ok &= testBoundInsidePartialPrim();
    ok &= testBoundInsidePrim();
    ok &= testBoundOutsidePrim();
    ok &= testBoundOutsidePartialPrim();
    ok &= testBoundFallbackMinMax();
    ok &= testBoundCameraInsidePartial();
    ok &= testBoundCameraOutside();
    ok &= testBoundCameraInside();

    return ok;
}

TEST_REGISTER_FN("HUSD_BoundAutoCollInstances",
    TESTHUSD_BoundAutoCollInstances)

PXR_NAMESPACE_CLOSE_SCOPE
