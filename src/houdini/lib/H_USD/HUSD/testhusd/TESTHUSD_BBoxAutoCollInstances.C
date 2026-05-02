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
 * NAME:	TESTHUSD_BBoxAutoCollInstances.C (C++)
 *
 * COMMENTS:	Tests for point instance matching in the
 *		%bbox auto collection.
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
// Test: %bbox insidepartial (default) matches overlapping instances
// Instances at X: 0, 5, 10, 50, 100.  Box [-1,6]x[-1,1]x[-1,1].
// ---------------------------------------------------------------
static bool
testBBoxInsidePartial()
{
    UT_TestUnit unit("BBox insidepartial");

    HUSD_DataHandle dh("test_bbox_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bbox(min=(-1,-1,-1), max=(6,1,1))", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1))
        return unit.fail("Expected IDs 0, 1");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bbox containment=inside
// ---------------------------------------------------------------
static bool
testBBoxInside()
{
    UT_TestUnit unit("BBox inside");

    HUSD_DataHandle dh("test_bbox_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bbox(min=(-1,-1,-1), max=(6,1,1), containment=inside)",
        0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1))
        return unit.fail("Expected IDs 0, 1");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bbox containment=outside
// ---------------------------------------------------------------
static bool
testBBoxOutside()
{
    UT_TestUnit unit("BBox outside");

    HUSD_DataHandle dh("test_bbox_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bbox(min=(-1,-1,-1), max=(6,1,1), containment=outside)",
        0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 3)
        return unit.fail("Expected 3 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 2) || !containsId(ids, 3) ||
        !containsId(ids, 4))
        return unit.fail("Expected IDs 2, 3, 4");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bbox containment=outsidepartial
// ---------------------------------------------------------------
static bool
testBBoxOutsidePartial()
{
    UT_TestUnit unit("BBox outsidepartial");

    HUSD_DataHandle dh("test_bbox_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bbox(min=(-1,-1,-1), max=(6,1,1), containment=outsidepartial)",
        0, tc);

    // Instances 0,1 are fully inside the box, so they are NOT
    // outsidepartial.  Instances 2,3,4 are not fully inside.
    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 3)
        return unit.fail("Expected 3 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 2) || !containsId(ids, 3) ||
        !containsId(ids, 4))
        return unit.fail("Expected IDs 2, 3, 4");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bbox matching all instances
// ---------------------------------------------------------------
static bool
testBBoxAllMatch()
{
    UT_TestUnit unit("BBox all match");

    HUSD_DataHandle dh("test_bbox_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bbox(min=(-200,-200,-200), max=(200,200,200))", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 5)
        return unit.fail("Expected 5 IDs, got %lld",
            (long long)ids.size());

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bbox matching no instances
// ---------------------------------------------------------------
static bool
testBBoxNoneMatch()
{
    UT_TestUnit unit("BBox none match");

    HUSD_DataHandle dh("test_bbox_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bbox(min=(200,200,200), max=(300,300,300))", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 0)
        return unit.fail("Expected 0 IDs, got %lld",
            (long long)ids.size());

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bbox using center/size syntax
// Box centered at origin with size (12,2,2) => [-6,6]x[-1,1]x[-1,1]
// ---------------------------------------------------------------
static bool
testBBoxCenterSize()
{
    UT_TestUnit unit("BBox center/size syntax");

    HUSD_DataHandle dh("test_bbox_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bbox(center=(0,0,0), size=(12,2,2))", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1))
        return unit.fail("Expected IDs 0, 1");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %bbox on instancer with zero instances
// ---------------------------------------------------------------
static bool
testBBoxEmptyInstancer()
{
    UT_TestUnit unit("BBox empty instancer");

    HUSD_DataHandle dh("test_bbox_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "%bbox(min=(-1,-1,-1), max=(1,1,1))", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/EmptyInstancer");
    if (ids.size() != 0)
        return unit.fail("Expected 0 IDs, got %lld",
            (long long)ids.size());

    return unit.ok();
}

// ---------------------------------------------------------------
// Main test registration
// ---------------------------------------------------------------
static bool
TESTHUSD_BBoxAutoCollInstances()
{
    bool ok = true;

    ok &= testBBoxInsidePartial();
    ok &= testBBoxInside();
    ok &= testBBoxOutside();
    ok &= testBBoxOutsidePartial();
    ok &= testBBoxAllMatch();
    ok &= testBBoxNoneMatch();
    ok &= testBBoxCenterSize();
    ok &= testBBoxEmptyInstancer();

    return ok;
}

TEST_REGISTER_FN("HUSD_BBoxAutoCollInstances",
    TESTHUSD_BBoxAutoCollInstances)

PXR_NAMESPACE_CLOSE_SCOPE
