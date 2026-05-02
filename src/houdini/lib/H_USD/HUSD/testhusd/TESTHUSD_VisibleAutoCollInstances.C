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
 * NAME:	TESTHUSD_VisibleAutoCollInstances.C (C++)
 *
 * COMMENTS:	Tests for point instance matching in the
 *		%visible auto collection.
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
// Test: %visible on instancer with some invisible instances
// ---------------------------------------------------------------
static bool
testVisiblePartialInvisible()
{
    UT_TestUnit unit("Visible partial invisible instances");

    HUSD_DataHandle dh("test_visible_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%visible", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 3)
        return unit.fail("Expected 3 visible IDs, got %lld",
            (long long)ids.size());

    // IDs 0, 2, 4 should be visible (1 and 3 are in invisibleIds).
    if (!containsId(ids, 0) || !containsId(ids, 2) ||
        !containsId(ids, 4))
        return unit.fail("Expected IDs 0, 2, 4");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %visible(false) on instancer with some invisible instances
// ---------------------------------------------------------------
static bool
testInvisiblePartialInvisible()
{
    UT_TestUnit unit("Invisible partial invisible instances");

    HUSD_DataHandle dh("test_visible_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%visible(false)", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 invisible IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 1) || !containsId(ids, 3))
        return unit.fail("Expected IDs 1, 3");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %visible on instancer with no invisibleIds
// ---------------------------------------------------------------
static bool
testVisibleAllVisible()
{
    UT_TestUnit unit("Visible all visible instances");

    HUSD_DataHandle dh("test_visible_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%visible", 0, tc);

    auto ids = getIdsForPath(findprims,
        "/World/AllVisibleInstancer");
    if (ids.size() != 3)
        return unit.fail("Expected 3 visible IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 0) || !containsId(ids, 1) ||
        !containsId(ids, 2))
        return unit.fail("Expected IDs 0, 1, 2");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %visible on instancer where all instances are invisible
// ---------------------------------------------------------------
static bool
testVisibleAllInvisible()
{
    UT_TestUnit unit("Visible all invisible instances");

    HUSD_DataHandle dh("test_visible_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    // Looking for visible: expect none.
    {
        HUSD_FindPrims findprims(lock,
            HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
        findprims.addPattern("%visible", 0, tc);

        auto ids = getIdsForPath(findprims,
            "/World/AllInvisibleInstancer");
        if (ids.size() != 0)
            return unit.fail(
                "Expected 0 visible IDs, got %lld",
                (long long)ids.size());
    }

    // Looking for invisible: expect all 3.
    {
        HUSD_FindPrims findprims(lock,
            HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
        findprims.addPattern("%visible(false)", 0, tc);

        auto ids = getIdsForPath(findprims,
            "/World/AllInvisibleInstancer");
        if (ids.size() != 3)
            return unit.fail(
                "Expected 3 invisible IDs, got %lld",
                (long long)ids.size());
    }

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %visible on instancer under invisible parent
// ---------------------------------------------------------------
static bool
testVisibleInheritedInvisible()
{
    UT_TestUnit unit("Visible inherited invisible parent");

    HUSD_DataHandle dh("test_visible_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%visible", 0, tc);

    auto ids = getIdsForPath(findprims,
        "/World/InvisibleParent/Instancer");
    // Parent is invisible, so no instances should be visible.
    if (ids.size() != 0)
        return unit.fail("Expected 0 visible IDs, got %lld",
            (long long)ids.size());

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %visible with explicit (non-sequential) instance IDs
// ---------------------------------------------------------------
static bool
testVisibleExplicitIds()
{
    UT_TestUnit unit("Visible explicit IDs");

    HUSD_DataHandle dh("test_explicit_ids_instances.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("%visible", 0, tc);

    // IDs are [100,200,300,400,500], invisibleIds=[200,400].
    // Visible: {100, 300, 500}.
    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 3)
        return unit.fail("Expected 3 visible IDs, got %lld",
            (long long)ids.size());

    if (!containsId(ids, 100) || !containsId(ids, 300) ||
        !containsId(ids, 500))
        return unit.fail("Expected IDs 100, 300, 500");

    return unit.ok();
}

// ---------------------------------------------------------------
// Main test registration
// ---------------------------------------------------------------
static bool
TESTHUSD_VisibleAutoCollInstances()
{
    bool ok = true;

    ok &= testVisiblePartialInvisible();
    ok &= testInvisiblePartialInvisible();
    ok &= testVisibleAllVisible();
    ok &= testVisibleAllInvisible();
    ok &= testVisibleInheritedInvisible();
    ok &= testVisibleExplicitIds();

    return ok;
}

TEST_REGISTER_FN("HUSD_VisibleAutoCollInstances",
    TESTHUSD_VisibleAutoCollInstances)

PXR_NAMESPACE_CLOSE_SCOPE
