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
 * NAME:	TESTHUSD_InstanceSetAlgebra.C (C++)
 *
 * COMMENTS:	Tests for set algebra (union, intersection, difference) between
 *		point instancer instance selections in path patterns.
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

// Helper: return the matched instance ids for an instancer path (sorted, as
// returned by HUSD_FindPrims). Returns an empty array if the instancer is not
// present in the result at all.
static UT_Array<int64>
getIds(const HUSD_FindPrims &findprims, const UT_StringRef &path)
{
    const auto &id_map = findprims.getPointInstancerIds();
    auto it = id_map.find(path);

    if (it != id_map.end())
        return UT_Array<int64>(it->second);

    return UT_Array<int64>();
}

// Helper: compare an id array against an expected list.
static bool
idsEqual(const UT_Array<int64> &ids, const std::initializer_list<int64> &expected)
{
    if (ids.size() != (exint)expected.size())
        return false;

    exint i = 0;
    for (auto &&v : expected)
    {
        if (ids(i) != v)
            return false;
        ++i;
    }
    return true;
}

// Helper: evaluate a pattern (instance matching is enabled on findprims).
static void
evaluate(const UT_StringRef &pattern, HUSD_FindPrims &findprims)
{
    HUSD_TimeCode tc;
    findprims.addPattern(pattern, 0, tc);
}

// ---------------------------------------------------------------
// Test: union of two explicit instance selections.
// ---------------------------------------------------------------
static bool
testUnion()
{
    UT_TestUnit unit("Union of instance selections");

    HUSD_DataHandle dh("test_instance_algebra.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

    evaluate("/World/Inst1[0-2] /World/Inst1[3-4]", findprims);

    if (!idsEqual(getIds(findprims, "/World/Inst1"), {0, 1, 2, 3, 4}))
        return unit.fail("Expected Inst1 ids {0,1,2,3,4}");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: intersection of two explicit instance selections.
// ---------------------------------------------------------------
static bool
testIntersection()
{
    UT_TestUnit unit("Intersection of instance selections");

    HUSD_DataHandle dh("test_instance_algebra.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

    evaluate("/World/Inst1[0-3] & /World/Inst1[2-4]", findprims);

    if (!idsEqual(getIds(findprims, "/World/Inst1"), {2, 3}))
        return unit.fail("Expected Inst1 ids {2,3}");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: difference of two explicit instance selections.
// ---------------------------------------------------------------
static bool
testDifference()
{
    UT_TestUnit unit("Difference of instance selections");

    HUSD_DataHandle dh("test_instance_algebra.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

    evaluate("/World/Inst1[0-4] - /World/Inst1[1 3]", findprims);

    if (!idsEqual(getIds(findprims, "/World/Inst1"), {0, 2, 4}))
        return unit.fail("Expected Inst1 ids {0,2,4}");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: subtracting a whole instancer removes all of its instances.
// ---------------------------------------------------------------
static bool
testWholeInstancerSubtraction()
{
    UT_TestUnit unit("Whole-instancer subtraction");

    HUSD_DataHandle dh("test_instance_algebra.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

    // Select all instances of both instancers, then remove every instance of
    // Inst1. Inst1 should disappear entirely; Inst2 should be untouched.
    evaluate(
        "(/World/Inst1[0-4] /World/Inst2[0-4]) - /World/Inst1", findprims);

    if (!getIds(findprims, "/World/Inst1").isEmpty())
        return unit.fail("Expected Inst1 to be removed entirely");
    if (!idsEqual(getIds(findprims, "/World/Inst2"), {0, 1, 2, 3, 4}))
        return unit.fail("Expected Inst2 ids {0,1,2,3,4}");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: set operations are scoped per instancer (no cross-instancer mixing).
// ---------------------------------------------------------------
static bool
testPerInstancerScoping()
{
    UT_TestUnit unit("Per-instancer scoping");

    HUSD_DataHandle dh("test_instance_algebra.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

    // Intersecting instances of Inst1 with instances of Inst2 must yield
    // nothing: instance ids are only meaningful within a single instancer.
    evaluate("/World/Inst1[0-2] & /World/Inst2[0-2]", findprims);

    if (!getIds(findprims, "/World/Inst1").isEmpty())
        return unit.fail("Expected no Inst1 instances");
    if (!getIds(findprims, "/World/Inst2").isEmpty())
        return unit.fail("Expected no Inst2 instances");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: union across two different instancers keeps both id sets.
// ---------------------------------------------------------------
static bool
testMixedInstancers()
{
    UT_TestUnit unit("Union across instancers");

    HUSD_DataHandle dh("test_instance_algebra.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

    evaluate("/World/Inst1[0-1] /World/Inst2[3-4]", findprims);

    if (!idsEqual(getIds(findprims, "/World/Inst1"), {0, 1}))
        return unit.fail("Expected Inst1 ids {0,1}");
    if (!idsEqual(getIds(findprims, "/World/Inst2"), {3, 4}))
        return unit.fail("Expected Inst2 ids {3,4}");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: an auto collection (matching all instances) intersected with an
// explicit selection, exercising the auto-collection instance payload.
// ---------------------------------------------------------------
static bool
testAutoCollectionIntersection()
{
    UT_TestUnit unit("Auto collection intersection");

    HUSD_DataHandle dh("test_instance_algebra.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

    // A large bounding box contains every instance of both instancers.
    // Intersecting with an explicit Inst1 selection should leave just those
    // Inst1 instances, and nothing from Inst2.
    evaluate(
        "%bbox(min=(-100,-100,-100),max=(100,100,100)) & /World/Inst1[1-3]",
        findprims);

    if (!idsEqual(getIds(findprims, "/World/Inst1"), {1, 2, 3}))
        return unit.fail("Expected Inst1 ids {1,2,3}");
    if (!getIds(findprims, "/World/Inst2").isEmpty())
        return unit.fail("Expected no Inst2 instances");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: union still works the same as before (regression).
// ---------------------------------------------------------------
static bool
testUnionRegression()
{
    UT_TestUnit unit("Union regression");

    HUSD_DataHandle dh("test_instance_algebra.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

    // Whole instancer plus a few explicit instances from the other.
    evaluate("/World/Inst1 /World/Inst2[2]", findprims);

    if (!idsEqual(getIds(findprims, "/World/Inst1"), {0, 1, 2, 3, 4}))
        return unit.fail("Expected Inst1 ids {0,1,2,3,4}");
    if (!idsEqual(getIds(findprims, "/World/Inst2"), {2}))
        return unit.fail("Expected Inst2 ids {2}");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: %keep whose sub-pattern contains instance set algebra. This exercises
// the path where an auto collection evaluates a sub-pattern (via parsePattern)
// that itself performs intersection/difference on instance selections, and
// then operates on the resulting per-instancer instance sets.
// ---------------------------------------------------------------
static bool
testKeepWithInstanceAlgebra()
{
    UT_TestUnit unit("keep over instance set algebra");

    // Difference in the sub-pattern, keep everything (interval=1 keeps every
    // matched entity). The %keep result must equal the raw difference.
    {
        HUSD_DataHandle dh("test_instance_algebra.usda");
        HUSD_AutoReadLock lock(dh);
        HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

        evaluate("%keep(/World/Inst1[0-4] - /World/Inst1[1 3], interval=1)",
            findprims);

        if (!idsEqual(getIds(findprims, "/World/Inst1"), {0, 2, 4}))
            return unit.fail("keep over difference: expected Inst1 {0,2,4}");
    }

    // Intersection in the sub-pattern, keep everything.
    {
        HUSD_DataHandle dh("test_instance_algebra.usda");
        HUSD_AutoReadLock lock(dh);
        HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

        evaluate("%keep(/World/Inst1[1-4] & /World/Inst1[2-3], interval=1)",
            findprims);

        if (!idsEqual(getIds(findprims, "/World/Inst1"), {2, 3}))
            return unit.fail("keep over intersection: expected Inst1 {2,3}");
    }

    // Algebra spanning two instancers, with %keep trimming by global index.
    // The sub-pattern yields Inst1 {0,2,4} (entity indices 0,1,2) and Inst2
    // {0,1,2,3,4} (indices 3..7), ordered by instancer path then id. Keeping
    // the range [2,5) leaves Inst1 id 4 and Inst2 ids 0 and 1.
    {
        HUSD_DataHandle dh("test_instance_algebra.usda");
        HUSD_AutoReadLock lock(dh);
        HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);

        evaluate("%keep((/World/Inst1[0-4] /World/Inst2[0-4]) - "
                 "/World/Inst1[1 3], start=2, end=5, interval=1)", findprims);

        if (!idsEqual(getIds(findprims, "/World/Inst1"), {4}))
            return unit.fail("keep trimming: expected Inst1 {4}");
        if (!idsEqual(getIds(findprims, "/World/Inst2"), {0, 1}))
            return unit.fail("keep trimming: expected Inst2 {0,1}");
    }

    return unit.ok();
}

// ---------------------------------------------------------------
// Main test registration
// ---------------------------------------------------------------
static bool
TESTHUSD_InstanceSetAlgebra()
{
    bool ok = true;

    ok &= testUnion();
    ok &= testIntersection();
    ok &= testDifference();
    ok &= testWholeInstancerSubtraction();
    ok &= testPerInstancerScoping();
    ok &= testMixedInstancers();
    ok &= testAutoCollectionIntersection();
    ok &= testUnionRegression();
    ok &= testKeepWithInstanceAlgebra();

    return ok;
}

TEST_REGISTER_FN("HUSD_InstanceSetAlgebra",
    TESTHUSD_InstanceSetAlgebra)

PXR_NAMESPACE_CLOSE_SCOPE
