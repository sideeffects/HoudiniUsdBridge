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
 * NAME:	TESTHUSD_FindPrimsInstances.C (C++)
 *
 * COMMENTS:	Tests for instance ID resolution in HUSD_FindPrims
 *		via the unified XUSD_PathPattern pipeline.
 *
 */

#include <HUSD/HUSD_Constants.h>
#include <HUSD/HUSD_CreatePrims.h>
#include <HUSD/HUSD_DataHandle.h>
#include <HUSD/HUSD_FindPrims.h>
#include <HUSD/HUSD_PathSet.h>
#include <HUSD/HUSD_TimeCode.h>
#include <HUSD/XUSD_Data.h>
#include <HUSD/XUSD_Utils.h>
#include <UT/UT_Array.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_TestManager.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/xform.h>

PXR_NAMESPACE_OPEN_SCOPE

// Helper: create a stage with a point instancer at the given path.
// The instancer has `count` instances with positional IDs (0..count-1)
// unless explicit_ids is non-empty, in which case those are used.
static HUSD_DataHandle
createInstancerStage(const char *instancer_path,
    int count,
    const UT_Array<int64> &explicit_ids = UT_Array<int64>())
{
    HUSD_DataHandle datahandle;
    datahandle.createNewData();

    {
        HUSD_AutoWriteLock writelock(datahandle);
        auto indata = writelock.data();

        if (indata && indata->isStageValid())
        {
            auto stage = indata->stage();

            // Create a simple prototype.
            UT_StringHolder proto_path;
            proto_path.sprintf("%s/Prototypes/Proto", instancer_path);
            UsdGeomCube::Define(stage,
                HUSDgetSdfPath(proto_path));

            // Create the point instancer.
            UsdGeomPointInstancer instancer =
                UsdGeomPointInstancer::Define(stage,
                    HUSDgetSdfPath(instancer_path));

            // Set up prototype relationships.
            SdfPathVector protos;
            protos.push_back(HUSDgetSdfPath(proto_path));
            instancer.GetPrototypesRel().SetTargets(protos);

            // Set protoIndices (all pointing to prototype 0).
            VtArray<int> proto_indices(count);
            for (int i = 0; i < count; i++)
                proto_indices[i] = 0;
            instancer.CreateProtoIndicesAttr().Set(proto_indices);

            // Set positions along X axis.
            VtArray<GfVec3f> positions(count);
            for (int i = 0; i < count; i++)
                positions[i] = GfVec3f(i * 2.0f, 0.0f, 0.0f);
            instancer.CreatePositionsAttr().Set(positions);

            // Set explicit IDs if provided.
            if (explicit_ids.size() > 0)
            {
                VtArray<int64> ids(count);
                for (int i = 0; i < count; i++)
                    ids[i] = explicit_ids(i);
                instancer.CreateIdsAttr().Set(ids);
            }
        }
    }

    return datahandle;
}

// Helper: create a stage with a simple cube (non-instancer).
static HUSD_DataHandle
createCubeStage()
{
    HUSD_DataHandle datahandle;
    datahandle.createNewData();

    {
        HUSD_AutoWriteLock writelock(datahandle);
        auto indata = writelock.data();

        if (indata && indata->isStageValid())
        {
            auto stage = indata->stage();
            UsdGeomCube::Define(stage,
                HUSDgetSdfPath("/World/Cube"));
        }
    }

    return datahandle;
}

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

// ---------------------------------------------------------------
// Test: bracket wildcard [*] matches all positional indices
// ---------------------------------------------------------------
static bool
testBracketWildcard()
{
    UT_TestUnit unit("Bracket wildcard [*]");

    HUSD_DataHandle dh =
        createInstancerStage("/World/Instancer", 10);
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("/World/Instancer[*]", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 10)
        return unit.fail("Expected 10 IDs, got %lld",
            (long long)ids.size());

    for (int i = 0; i < 10; i++)
    {
        if (ids(i) != i)
            return unit.fail("Expected ID %d at index %d, got %lld",
                i, i, (long long)ids(i));
    }

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: bracket range [2-5] matches a range of indices
// ---------------------------------------------------------------
static bool
testBracketRange()
{
    UT_TestUnit unit("Bracket range [2-5]");

    HUSD_DataHandle dh =
        createInstancerStage("/World/Instancer", 10);
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("/World/Instancer[2-5]", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 4)
        return unit.fail("Expected 4 IDs, got %lld",
            (long long)ids.size());

    UT_Array<int64> expected({2, 3, 4, 5});
    for (int i = 0; i < 4; i++)
    {
        bool found = false;
        for (auto &&id : ids)
        {
            if (id == expected(i))
            {
                found = true;
                break;
            }
        }
        if (!found)
            return unit.fail("Missing expected ID %lld",
                (long long)expected(i));
    }

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: bracket negation [0-9,^3,^7]
// ---------------------------------------------------------------
static bool
testBracketNegation()
{
    UT_TestUnit unit("Bracket negation [0-9,^3,^7]");

    HUSD_DataHandle dh =
        createInstancerStage("/World/Instancer", 10);
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("/World/Instancer[0-9,^3,^7]", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 8)
        return unit.fail("Expected 8 IDs, got %lld",
            (long long)ids.size());

    // Verify 3 and 7 are excluded.
    for (auto &&id : ids)
    {
        if (id == 3 || id == 7)
            return unit.fail("ID %lld should be excluded",
                (long long)id);
    }

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: explicit IDs on instancer with non-sequential ids
// ---------------------------------------------------------------
static bool
testBracketExplicitIds()
{
    UT_TestUnit unit("Bracket explicit IDs [200,400]");

    UT_Array<int64> explicit_ids({100, 200, 300, 400, 500});
    HUSD_DataHandle dh =
        createInstancerStage("/World/Instancer", 5, explicit_ids);
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("/World/Instancer[200,400]", 0, tc);

    auto ids = getIdsForPath(findprims, "/World/Instancer");
    if (ids.size() != 2)
        return unit.fail("Expected 2 IDs, got %lld",
            (long long)ids.size());

    bool found200 = false, found400 = false;
    for (auto &&id : ids)
    {
        if (id == 200) found200 = true;
        if (id == 400) found400 = true;
    }
    if (!found200 || !found400)
        return unit.fail("Expected IDs 200 and 400");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: bracket on non-instancer prim
// ---------------------------------------------------------------
static bool
testBracketOnNonInstancer()
{
    UT_TestUnit unit("Bracket on non-instancer");

    HUSD_DataHandle dh = createCubeStage();
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("/World/Cube[0-5]", 0, tc);

    auto &id_map = findprims.getPointInstancerIds();
    if (!id_map.empty())
        return unit.fail("Expected no instance IDs for non-instancer");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: excluded instance IDs
// ---------------------------------------------------------------
static bool
testExcludedInstanceIds()
{
    UT_TestUnit unit("Excluded instance IDs");

    HUSD_DataHandle dh =
        createInstancerStage("/World/Instancer", 10);
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("/World/Instancer[2-5]", 0, tc);

    UT_StringMap<UT_Array<int64>> excluded;
    findprims.getExcludedPointInstancerIds(excluded, tc);

    auto it = excluded.find("/World/Instancer");
    if (it == excluded.end())
        return unit.fail("Expected excluded entry for instancer");

    auto &excl_ids = it->second;
    // Excluded should be {0,1,6,7,8,9}
    if (excl_ids.size() != 6)
        return unit.fail("Expected 6 excluded IDs, got %lld",
            (long long)excl_ids.size());

    for (auto &&id : excl_ids)
    {
        if (id >= 2 && id <= 5)
            return unit.fail("ID %lld should not be excluded",
                (long long)id);
    }

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: pattern without find_point_instancer_ids flag
// ---------------------------------------------------------------
static bool
testPatternWithoutInstanceFlag()
{
    UT_TestUnit unit("Pattern without instance flag");

    HUSD_DataHandle dh =
        createInstancerStage("/World/Instancer", 10);
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    // find_point_instancer_ids = false
    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, false);
    findprims.addPattern("/World/Instancer", 0, tc);

    auto &id_map = findprims.getPointInstancerIds();
    if (!id_map.empty())
        return unit.fail(
            "Expected no instance IDs when flag is false");

    // The instancer should be in the expanded path set as a regular prim.
    bool found = false;
    for (auto &&path : findprims.getExpandedPathSet())
    {
        if (path.pathStr() == "/World/Instancer")
        {
            found = true;
            break;
        }
    }
    if (!found)
        return unit.fail(
            "Instancer should be in expanded path set");

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: multiple instancers in a single pattern
// ---------------------------------------------------------------
static bool
testMultipleInstancers()
{
    UT_TestUnit unit("Multiple instancers in pattern");

    HUSD_DataHandle datahandle;
    datahandle.createNewData();

    {
        HUSD_AutoWriteLock writelock(datahandle);
        auto indata = writelock.data();

        if (indata && indata->isStageValid())
        {
            auto stage = indata->stage();

            // Create instancer A with 5 instances.
            {
                UsdGeomCube::Define(stage,
                    HUSDgetSdfPath(
                        "/World/A/Prototypes/Proto"));
                auto inst = UsdGeomPointInstancer::Define(stage,
                    HUSDgetSdfPath("/World/A"));
                SdfPathVector protos;
                protos.push_back(
                    HUSDgetSdfPath("/World/A/Prototypes/Proto"));
                inst.GetPrototypesRel().SetTargets(protos);

                VtArray<int> pi(5);
                VtArray<GfVec3f> pos(5);
                for (int i = 0; i < 5; i++)
                {
                    pi[i] = 0;
                    pos[i] = GfVec3f(i, 0, 0);
                }
                inst.CreateProtoIndicesAttr().Set(pi);
                inst.CreatePositionsAttr().Set(pos);
            }

            // Create instancer B with 3 explicit IDs.
            {
                UsdGeomCube::Define(stage,
                    HUSDgetSdfPath(
                        "/World/B/Prototypes/Proto"));
                auto inst = UsdGeomPointInstancer::Define(stage,
                    HUSDgetSdfPath("/World/B"));
                SdfPathVector protos;
                protos.push_back(
                    HUSDgetSdfPath("/World/B/Prototypes/Proto"));
                inst.GetPrototypesRel().SetTargets(protos);

                VtArray<int> pi(3);
                VtArray<GfVec3f> pos(3);
                VtArray<int64> ids(3);
                for (int i = 0; i < 3; i++)
                {
                    pi[i] = 0;
                    pos[i] = GfVec3f(i, 0, 0);
                    ids[i] = (i + 1) * 100;
                }
                inst.CreateProtoIndicesAttr().Set(pi);
                inst.CreatePositionsAttr().Set(pos);
                inst.CreateIdsAttr().Set(ids);
            }
        }
    }

    HUSD_AutoReadLock lock(datahandle);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern(
        "/World/A[0-2] /World/B[200]", 0, tc);

    auto ids_a = getIdsForPath(findprims, "/World/A");
    if (ids_a.size() != 3)
        return unit.fail("Expected 3 IDs for A, got %lld",
            (long long)ids_a.size());

    auto ids_b = getIdsForPath(findprims, "/World/B");
    if (ids_b.size() != 1)
        return unit.fail("Expected 1 ID for B, got %lld",
            (long long)ids_b.size());

    if (ids_b(0) != 200)
        return unit.fail("Expected ID 200 for B, got %lld",
            (long long)ids_b(0));

    return unit.ok();
}

// ---------------------------------------------------------------
// Test: empty bracket result creates map entry
// ---------------------------------------------------------------
static bool
testEmptyBracketResult()
{
    UT_TestUnit unit("Empty bracket result [999]");

    HUSD_DataHandle dh =
        createInstancerStage("/World/Instancer", 10);
    HUSD_AutoReadLock lock(dh);
    HUSD_TimeCode tc;

    HUSD_FindPrims findprims(lock,
        HUSD_TRAVERSAL_DEFAULT_DEMANDS, true);
    findprims.addPattern("/World/Instancer[999]", 0, tc);

    auto &id_map = findprims.getPointInstancerIds();
    auto it = id_map.find("/World/Instancer");
    if (it == id_map.end())
        return unit.fail(
            "Expected map entry for instancer even with no matches");

    if (it->second.size() != 0)
        return unit.fail("Expected 0 matched IDs, got %lld",
            (long long)it->second.size());

    return unit.ok();
}

// ---------------------------------------------------------------
// Main test registration
// ---------------------------------------------------------------
static bool
TESTHUSD_FindPrimsInstances()
{
    bool ok = true;

    ok &= testBracketWildcard();
    ok &= testBracketRange();
    ok &= testBracketNegation();
    ok &= testBracketExplicitIds();
    ok &= testBracketOnNonInstancer();
    ok &= testExcludedInstanceIds();
    ok &= testPatternWithoutInstanceFlag();
    ok &= testMultipleInstancers();
    ok &= testEmptyBracketResult();

    return ok;
}

TEST_REGISTER_FN("HUSD_FindPrimsInstances", TESTHUSD_FindPrimsInstances)

PXR_NAMESPACE_CLOSE_SCOPE
