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
 * NAME:	TESTHUSD_TimeVaryingAutoColl.C (C++)
 *
 * COMMENTS:	Tests for the %timevarying auto collection, which matches
 *		primitives that have time varying attributes.
 *
 */

#include <HUSD/HUSD_Constants.h>
#include <HUSD/HUSD_DataHandle.h>
#include <HUSD/HUSD_FindPrims.h>
#include <HUSD/HUSD_PathSet.h>
#include <HUSD/HUSD_TimeCode.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_TestManager.h>

PXR_NAMESPACE_OPEN_SCOPE

// Helper: evaluate a pattern against the test stage and return the set of
// matched primitive paths.
static bool
matchPattern(const UT_StringRef &pattern,
    const UT_StringArray &expected)
{
    HUSD_DataHandle dh("test_timevarying.usda");
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock, HUSD_TRAVERSAL_DEFAULT_DEMANDS);
    findprims.addPattern(pattern, 0, tc);

    const HUSD_PathSet &paths = findprims.getExpandedPathSet();

    if (paths.size() != (size_t)expected.size())
        return false;
    for (auto &&path : expected)
    {
        if (!paths.contains(path))
            return false;
    }

    return true;
}

// ---------------------------------------------------------------
// Test: %timevarying with no argument inspects all attributes.
// ---------------------------------------------------------------
static bool
testDefaultAllAttributes()
{
    UT_TestUnit unit("Default inspects all attributes");

    UT_StringArray expected;
    expected.append("/World/AnimatedXform");
    expected.append("/World/AnimatedPoints");

    if (!matchPattern("%timevarying", expected))
        return unit.fail("Expected AnimatedXform and AnimatedPoints");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: the attribute pattern restricts the inspected attributes.
// ---------------------------------------------------------------
static bool
testAttributePatternFilter()
{
    UT_TestUnit unit("Attribute pattern restricts inspection");

    // Only the transform is animated on AnimatedXform.
    {
        UT_StringArray expected;
        expected.append("/World/AnimatedXform");
        if (!matchPattern("%timevarying(xformOp:*)", expected))
            return unit.fail("xformOp:* should match only AnimatedXform");
    }

    // Only the points are animated on AnimatedPoints.
    {
        UT_StringArray expected;
        expected.append("/World/AnimatedPoints");
        if (!matchPattern("%timevarying(points)", expected))
            return unit.fail("points should match only AnimatedPoints");
    }

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: a space separated list of patterns matches the union.
// ---------------------------------------------------------------
static bool
testMultiPattern()
{
    UT_TestUnit unit("Multiple attribute patterns");

    UT_StringArray expected;
    expected.append("/World/AnimatedXform");
    expected.append("/World/AnimatedPoints");

    if (!matchPattern("%timevarying(xformOp:* points)", expected))
        return unit.fail("Expected AnimatedXform and AnimatedPoints");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: a single authored time sample is not time varying.
// ---------------------------------------------------------------
static bool
testSingleSampleNotVarying()
{
    UT_TestUnit unit("Single time sample is not time varying");

    // SingleSample and StaticPrim must not appear in the all-attribute match.
    UT_StringArray expected;
    expected.append("/World/AnimatedXform");
    expected.append("/World/AnimatedPoints");

    if (!matchPattern("%timevarying", expected))
        return unit.fail("SingleSample/StaticPrim should not match");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: the counttimesamples argument uses the authored sample count.
// ---------------------------------------------------------------
static bool
testCountTimeSamples()
{
    UT_TestUnit unit("counttimesamples argument");

    // With multi-sample animation, the strict mode matches the same prims as
    // the default, and still excludes the single-sample and static prims.
    {
        UT_StringArray expected;
        expected.append("/World/AnimatedXform");
        expected.append("/World/AnimatedPoints");
        if (!matchPattern("%timevarying(counttimesamples=true)", expected))
            return unit.fail("Strict mode should match the animated prims");
    }

    // The attribute pattern still applies in strict mode.
    {
        UT_StringArray expected;
        expected.append("/World/AnimatedPoints");
        if (!matchPattern("%timevarying(points, counttimesamples=true)",
                expected))
            return unit.fail("Strict mode should honor the attribute pattern");
    }

    return unit.ok();
}

// ---------------------------------------------------------------
// Main test registration
// ---------------------------------------------------------------
static bool
TESTHUSD_TimeVaryingAutoColl()
{
    bool ok = true;

    ok &= testDefaultAllAttributes();
    ok &= testAttributePatternFilter();
    ok &= testMultiPattern();
    ok &= testSingleSampleNotVarying();
    ok &= testCountTimeSamples();

    return ok;
}

TEST_REGISTER_FN("HUSD_TimeVaryingAutoColl",
    TESTHUSD_TimeVaryingAutoColl)

PXR_NAMESPACE_CLOSE_SCOPE
