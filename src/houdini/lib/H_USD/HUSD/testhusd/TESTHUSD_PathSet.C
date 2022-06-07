/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects
 *	477 Richmond Street West
 *	Toronto, Ontario
 *	Canada   M5V 3E7
 *	416-504-9876
 *
 * NAME:	TESTHUSD_Path.C (C++)
 *
 * COMMENTS:	Module for testing code and macros in HUSD_Path.C
 *
 */

#include <HUSD/HUSD_PathSet.h>
#include <HUSD/HUSD_Path.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_TestManager.h>
#include <pxr/usd/sdf/path.h>
#include <utility>

static const HUSD_PathSet &
getSimpleTestSet(UT_TestUnit &unit)
{
    static HUSD_PathSet theTestSet;

    if (theTestSet.size() == 0)
    {
        // Create a set for doing testing.
        UT_StringArray thePathStrs({
            "/a",
            "/aa",
            "/a/b",
            "/a/bb",
            "/aa/b",
            "/aa/bb",
            "/a/b/c",
            "/a/b/cc",
            "/a/b/c/d",
            "/a/b/c/dd",
            "/a/b/c/d/e",
            "/a/b/c/d/ee",
            "/b/c",
            "/b/c/d/e",
            "/x/y/z",
        });

        for (auto &&str : thePathStrs)
            theTestSet.insert(str);
        if (theTestSet.size() != thePathStrs.size())
            unit.fail("Incorrect set size: %d != %d",
                int(theTestSet.size()), int(thePathStrs.size()));
    }

    return theTestSet;
}

static bool
testContainment()
{
    UT_TestUnit unit("Containment");
    const HUSD_PathSet &testset = getSimpleTestSet(unit);

    UT_Array<std::pair<HUSD_Path, bool>> theTestPairs({
        { HUSD_Path("/"), false },
        { HUSD_Path("/a/cc"), false },
        { HUSD_Path("/aa/a"), false },
        { HUSD_Path("/a/b/c/d"), true },
        { HUSD_Path("/a/b/c/d/e"), true },
        { HUSD_Path("/b/c/d"), false },
        { HUSD_Path("/x"), false },
        { HUSD_Path("/x/y"), false },
        { HUSD_Path("/x/y/z"), true },
        { HUSD_Path("/z"), false },
    });
    for (auto &&pair : theTestPairs)
    {
        if (testset.contains(pair.first) != pair.second)
            return unit.fail("contains(%s) != %s",
                pair.first.pathStr().c_str(),
                pair.second ? "true" : "false");
    }

    return unit.ok();
}

static bool
testAncestorContainment()
{
    UT_TestUnit unit("Ancestor Containment");
    const HUSD_PathSet &testset = getSimpleTestSet(unit);

    UT_Array<std::pair<HUSD_Path, bool>> theTestPairs({
        { HUSD_Path("/"), false },
        { HUSD_Path("/a"), false },
        { HUSD_Path("/a/cc"), true },
        { HUSD_Path("/aa/a"), true },
        { HUSD_Path("/a/b/c"), true },
        { HUSD_Path("/a/b/c/d/e"), true },
        { HUSD_Path("/b/c/d"), true },
        { HUSD_Path("/b/d"), false },
        { HUSD_Path("/x/y/z"), false },
        { HUSD_Path("/x/y/z/zz/zzz"), true },
        { HUSD_Path("/z"), false },
    });
    for (auto &&pair : theTestPairs)
    {
        if (testset.containsAncestor(pair.first) != pair.second)
            return unit.fail("containsAncestor(%s) != %s",
                pair.first.pathStr().c_str(),
                pair.second ? "true" : "false");
    }

    return unit.ok();
}

static bool
testDescendantContainment()
{
    UT_TestUnit unit("Descendant Containment");
    const HUSD_PathSet &testset = getSimpleTestSet(unit);

    UT_Array<std::pair<HUSD_Path, bool>> theTestPairs({
        { HUSD_Path("/"), true },
        { HUSD_Path("/a"), true },
        { HUSD_Path("/a/cc"), false },
        { HUSD_Path("/aa/a"), false },
        { HUSD_Path("/a/b/c"), true },
        { HUSD_Path("/a/b/c/d/e"), false },
        { HUSD_Path("/b/c/d"), true },
        { HUSD_Path("/b/d"), false },
        { HUSD_Path("/x"), true },
        { HUSD_Path("/x/y/z"), false },
        { HUSD_Path("/x/y/z/zz/zzz"), false },
        { HUSD_Path("/z"), false },
    });
    for (auto &&pair : theTestPairs)
    {
        if (testset.containsDescendant(pair.first) != pair.second)
            return unit.fail("containsDescendant(%s) != %s",
                pair.first.pathStr().c_str(),
                pair.second ? "true" : "false");
    }

    return unit.ok();
}

static bool
testPathOrAncestorContainment()
{
    UT_TestUnit unit("Path or Ancestor Containment");
    const HUSD_PathSet &testset = getSimpleTestSet(unit);

    UT_Array<std::pair<HUSD_Path, bool>> theTestPairs({
        { HUSD_Path("/"), false },
        { HUSD_Path("/a"), true },
        { HUSD_Path("/a/cc"), true },
        { HUSD_Path("/aa/a"), true },
        { HUSD_Path("/a/b/c"), true },
        { HUSD_Path("/a/b/c/d/e"), true },
        { HUSD_Path("/b/c/d"), true },
        { HUSD_Path("/b/d"), false },
        { HUSD_Path("/x/y/z"), true },
        { HUSD_Path("/x/y/z/zz/zzz"), true },
        { HUSD_Path("/z"), false },
    });
    for (auto &&pair : theTestPairs)
    {
        if (testset.containsPathOrAncestor(pair.first) != pair.second)
            return unit.fail("containsPathOrAncestor(%s) != %s",
                pair.first.pathStr().c_str(),
                pair.second ? "true" : "false");
    }

    return unit.ok();
}

static bool
testPathOrDescendantContainment()
{
    UT_TestUnit unit("Path or Descendant Containment");
    const HUSD_PathSet &testset = getSimpleTestSet(unit);

    UT_Array<std::pair<HUSD_Path, bool>> theTestPairs({
        { HUSD_Path("/"), true },
        { HUSD_Path("/a"), true },
        { HUSD_Path("/a/cc"), false },
        { HUSD_Path("/aa/a"), false },
        { HUSD_Path("/a/b/c"), true },
        { HUSD_Path("/a/b/c/d/e"), true },
        { HUSD_Path("/b/c/d"), true },
        { HUSD_Path("/b/d"), false },
        { HUSD_Path("/x"), true },
        { HUSD_Path("/x/y/z"), true },
        { HUSD_Path("/x/y/z/zz/zzz"), false },
        { HUSD_Path("/z"), false },
    });
    for (auto &&pair : theTestPairs)
    {
        if (testset.containsPathOrDescendant(pair.first) != pair.second)
            return unit.fail("containsPathOrDescendant(%s) != %s",
                pair.first.pathStr().c_str(),
                pair.second ? "true" : "false");
    }

    return unit.ok();
}

static bool
TESTHUSD_PathSet()
{
    bool ok = true;

    ok &= testContainment();
    ok &= testAncestorContainment();
    ok &= testDescendantContainment();
    ok &= testPathOrAncestorContainment();
    ok &= testPathOrDescendantContainment();

    return ok;
}

TEST_REGISTER_FN("HUSD_PathSet", TESTHUSD_PathSet)

