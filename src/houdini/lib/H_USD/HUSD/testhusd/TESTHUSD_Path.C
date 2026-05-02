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

#include <HUSD/HUSD_Path.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_TestManager.h>
#include <pxr/usd/sdf/path.h>

static bool
testPathConversion()
{
    UT_TestUnit unit("Round Trip Conversions");

    UT_StringArray thePathStrs({
        "/a/b/c/d",
        "/something/b/c/d.property",
        "/something/b/c/d.property:with:namespace",
        "/something/b{model=LodHigh}d.property:with:namespace",

        // These tests come from the SdfPath unit tests in the USD baseline.
        "/Foo/Bar.baz",
        "Foo",
        "Foo/Bar",
        "Foo.bar",
        "Foo/Bar.bar",
        ".bar",
        "/Some/Kinda/Long/Path/Just/To/Make/Sure",
        "Some/Kinda/Long/Path/Just/To/Make/Sure.property",
        "../Some/Kinda/Long/Path/Just/To/Make/Sure",
        "../../Some/Kinda/Long/Path/Just/To/Make/Sure.property",
        "/Foo/Bar.baz[targ].boom",
        "Foo.bar[targ].boom",
        ".bar[targ].boom",
        "Foo.bar[targ.attr].boom",
        "/A/B/C.rel3[/Blah].attr3",
        "A/B.rel2[/A/B/C.rel3[/Blah].attr3].attr2",
        "/A.rel1[/A/B.rel2[/A/B/C.rel3[/Blah].attr3].attr2].attr1"
    });

    for (auto &&str : thePathStrs)
    {
        HUSD_Path        path(str);
        UT_StringHolder  pathtostr = path.pathStr();

        if (pathtostr != path.sdfPath().GetString())
            return unit.fail("Conversion mismatch: %s != %s",
                pathtostr.c_str(), path.sdfPath().GetString().c_str());
        if (pathtostr != str)
            return unit.fail("Round tripping failed: %s != %s",
                pathtostr.c_str(), str.c_str());
    }

    return unit.ok();
}

static bool
TESTHUSD_Path()
{
    bool ok = true;

    ok &= testPathConversion();

    return ok;
}

TEST_REGISTER_FN("HUSD_Path", TESTHUSD_Path)

