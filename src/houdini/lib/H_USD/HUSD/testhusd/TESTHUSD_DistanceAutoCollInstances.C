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
 * NAME:	TESTHUSD_DistanceAutoCollInstances.C (C++)
 *
 * COMMENTS:	Tests for point instance matching in the
 *		%closerthan and %fartherthan auto collections.
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
// Target is at origin.  Instances (unit cube) at X positions:
//   0: (0,0,0)  minDist=0     maxDist~0.87
//   1: (3,0,0)  minDist=2.5   maxDist~3.57
//   2: (7,0,0)  minDist=6.5   maxDist~7.53
//   3: (15,0,0) minDist=14.5  maxDist~15.52
//   4: (30,0,0) minDist=29.5  maxDist~30.51
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// Test: %closerthan basic -- threshold 5
// ---------------------------------------------------------------
static bool
testCloserThanBasic()
{
    UT_TestUnit unit("closerthan threshold=5");

    HUSD_DataHandle dh("test_distance_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%closerthan(/World/Target, 5)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1))
        return unit.fail("Expected IDs 0, 1");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %closerthan wider -- threshold 10
// ---------------------------------------------------------------
static bool
testCloserThanWide()
{
    UT_TestUnit unit("closerthan threshold=10");

    HUSD_DataHandle dh("test_distance_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%closerthan(/World/Target, 10)", 0, tc);

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
// Test: %closerthan matching all
// ---------------------------------------------------------------
static bool
testCloserThanAll()
{
    UT_TestUnit unit("closerthan all match");

    HUSD_DataHandle dh("test_distance_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%closerthan(/World/Target, 200)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 5)
        return unit.fail("Expected 5 IDs, got %lld",
            (long long)ids.size());

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %closerthan with threshold 0 (only touching instances)
// ---------------------------------------------------------------
static bool
testCloserThanTight()
{
    UT_TestUnit unit("closerthan threshold=0");

    HUSD_DataHandle dh("test_distance_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%closerthan(/World/Target, 0)", 0, tc);

    // Instance 0 contains the origin (minDist=0), so minDist2 <= 0.
    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 1)
        return unit.fail("Expected 1 ID, got %lld",
            (long long)ids.size());

    if (ids(0) != 0)
        return unit.fail("Expected ID 0, got %lld",
            (long long)ids(0));

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %fartherthan basic -- threshold 10
// ---------------------------------------------------------------
static bool
testFartherThanBasic()
{
    UT_TestUnit unit("fartherthan threshold=10");

    HUSD_DataHandle dh("test_distance_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%fartherthan(/World/Target, 10)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 3) || !containsId(ids, 4))
        return unit.fail("Expected IDs 3, 4");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %fartherthan loose -- threshold 1
// ---------------------------------------------------------------
static bool
testFartherThanLoose()
{
    UT_TestUnit unit("fartherthan threshold=1");

    HUSD_DataHandle dh("test_distance_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%fartherthan(/World/Target, 1)", 0, tc);

    // Instance 0 maxDist ~0.87 < 1, so excluded.
    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 4)
        return unit.fail("Expected 4 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 1) || !containsId(ids, 2) ||
        !containsId(ids, 3) || !containsId(ids, 4))
        return unit.fail("Expected IDs 1, 2, 3, 4");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %fartherthan matching none
// ---------------------------------------------------------------
static bool
testFartherThanNone()
{
    UT_TestUnit unit("fartherthan none match");

    HUSD_DataHandle dh("test_distance_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%fartherthan(/World/Target, 200)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 0)
        return unit.fail("Expected 0 IDs, got %lld",
            (long long)ids.size());

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %closerthan with explicit (non-sequential) instance IDs
// ---------------------------------------------------------------
static bool
testDistanceExplicitIds()
{
    UT_TestUnit unit("Distance explicit IDs");

    HUSD_DataHandle dh("test_explicit_ids_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%closerthan(/World/Target, 5)", 0, tc);

    // IDs [100,200,300,400,500] at positions [0,3,7,15,30].
    // closerthan(5): positions 0 and 3 => IDs 100 and 200.
    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 100) || !containsId(ids, 200))
        return unit.fail("Expected IDs 100, 200");

    return unit.ok();
}

// ---------------------------------------------------------------
// Main test registration
// ---------------------------------------------------------------
static bool
TESTHUSD_DistanceAutoCollInstances()
{
    bool ok = true;

    ok &= testCloserThanBasic();
    ok &= testCloserThanWide();
    ok &= testCloserThanAll();
    ok &= testCloserThanTight();
    ok &= testFartherThanBasic();
    ok &= testFartherThanLoose();
    ok &= testFartherThanNone();
    ok &= testDistanceExplicitIds();

    return ok;
}

TEST_REGISTER_FN("HUSD_DistanceAutoCollInstances",
    TESTHUSD_DistanceAutoCollInstances)

PXR_NAMESPACE_CLOSE_SCOPE
