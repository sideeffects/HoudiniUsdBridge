/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *      Side Effects Software Inc
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 * NAME:    TESTXUSD_Utils.C (C++)
 *
 * COMMENTS:    Module for testing code and macros in XUSD_Utils.C
 *
 */

#include <HUSD/XUSD_Utils.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Options.h>
#include <UT/UT_OptionEntry.h>
#include <UT/UT_TestManager.h>
#include <HUSD/XUSD_Format.h>

PXR_NAMESPACE_OPEN_SCOPE

static bool
testDictionaryBasic()
{
    UT_TestUnit unit("Simple vtDictionary Conversion");

    VtDictionary dict;
    dict["some_key"] = -5;
    dict["another key"] = 8.5f;
    dict["!@1723uasie '/\""] = 3.14;
    dict["Hello"] = "World";

    UT_Options res;
    bool status = HUSDconvertDictionary(res, dict);
    if (!status)
        return unit.fail("HUSDconvertDictionary returned false.");
    if (res.size() != dict.size())
        return unit.fail("Resulting UT_Options has incorrect size.");
    if (res.getOptionI("some_key") != -5 ||
        // UT_Options converts 32 bit floats to doubles
        !SYSalmostEqual(res.getOptionF("another key"), 8.5) ||
        !SYSalmostEqual(res.getOptionF("!@1723uasie '/\""), 3.14) ||
        res.getOptionS("Hello") != "World")
    {
        return unit.fail("Incorrect conversion from vtDictionary to UT_Options.");
    }

    return unit.ok();
}

static bool
testDictionaryOverflow()
{
    UT_TestUnit unit("Test uint64 Overflow");

    VtDictionary dict;
    dict["test"] = 0xFFFFFFFFFFFFFFFF;
    VtValue val = dict["test"];

    UT_Options res;
    bool status = HUSDconvertDictionary(res, dict);
    if (!status)
        return unit.fail("HUSDconvertDictionary returned false.");
    if (res.size() != dict.size())
        return unit.fail("Resulting UT_Options has incorrect size.");
    if (res.getOptionI("test") != 0xFFFFFFFFFFFFFFFF)
    {
        return unit.fail("Incorrect conversion from vtDictionary to UT_Options.");
    }

    return unit.ok();
}

static bool
testDictionaryNested()
{
    UT_TestUnit unit("Nested vtDictionary");

    VtDictionary dict, nested1, nested2;
    nested2["something else"] = -10;
    nested1["nested"] = nested2;
    nested1["something else"] = -1;
    dict["nested"] = nested1;
    dict["something else"] = 1;

    UT_Options res;
    bool status = HUSDconvertDictionary(res, dict);
    if (!status)
        return unit.fail("HUSDconvertDictionary returned false.");
    if (res.size() != dict.size())
        return unit.fail("Resulting UT_Options has incorrect size.");
    
    bool ok = true;
    ok &= res.getOptionI("something else") == 1;
    ok &= res.getOptionDict("nested") != UT_OptionsHolder::theEmptyOptions;
    if (!ok)
        return unit.fail("Incorrect result at checkpoint 1.");
    const UT_Options& nested_options1 =
            *res.getOptionDict("nested").options();
    ok &= nested_options1.getOptionI("something else") == -1;
    ok &= nested_options1.getOptionDict("nested") !=
            UT_OptionsHolder::theEmptyOptions;
    if (!ok)
        return unit.fail("Incorrect result at checkpoint 2.");
    const UT_Options& nested_options2 =
            *nested_options1.getOptionDict("nested").options();
    ok &= nested_options2.getOptionI("something else") == -10;

    if (!ok)
        return unit.fail("Incorrect result at checkpoint 3.");

    return unit.ok();
}

static bool
testDictionaryArrays()
{
    UT_TestUnit unit("Arrays as Values");

    VtDictionary dict;
    VtArray<int32> arr1;
    VtArray<fpreal64> arr2;
    VtArray<int64> arr3;
    VtArray<uint64> arr4;
    VtArray<std::string> arr5;
    arr1.emplace_back(-1);
    arr1.emplace_back(0);
    arr1.emplace_back(1);
    arr2.emplace_back(3.14);
    arr2.emplace_back(9.9);
    // Min signed int64
    arr3.emplace_back(-0x7FFFFFFFFFFFFFFF);
    arr3.emplace_back(0);
    // Max signed int64
    arr3.emplace_back(0x7FFFFFFFFFFFFFFF);
    arr4.emplace_back(0);
    // Max uint64
    arr4.emplace_back(0xFFFFFFFFFFFFFFFF);
    arr5.emplace_back("Hello, World!");
    dict["int array"] = arr1;
    dict["float array"] = arr2;
    dict["long array"] = arr3;
    dict["ulong array"] = arr4;
    dict["string array"] = arr5;

    UT_Options res;
    bool status = HUSDconvertDictionary(res, dict);
    if (!status)
        return unit.fail("HUSDconvertDictionary returned false.");
    if (res.size() != dict.size())
        return unit.fail("Resulting UT_Options has incorrect size.");
    
    UT_Int64Array ut1{-1, 0, 1};
    // Floating point equality comparison is fine for these constants
    UT_Fpreal64Array ut2{3.14, 9.9};
    UT_Int64Array ut3{-0x7FFFFFFFFFFFFFFF, 0, 0x7FFFFFFFFFFFFFFF};
    // Force conversion to signed
    UT_Int64Array ut4{0, (int64) 0xFFFFFFFFFFFFFFFF};
    UT_StringArray ut5{"Hello, World!"};

    UT_WorkBuffer       err;
    if (!(res.getOptionIArray("int array").size() == ut1.size() &&
            res.getOptionIArray("int array") == ut1))
    {
        err.format("Bad VtDictionary -> UT_Options: int array {} {}",
                res.getOptionIArray("int array").size(),
                res.getOptionIArray("int array"));
        return unit.fail("%s", err.buffer());
    }
    if (!(res.getOptionFArray("float array").size() == ut2.size() &&
            res.getOptionFArray("float array") == ut2))
    {
        err.format("Bad VtDictionary -> UT_Options: float array {} {}",
                res.getOptionIArray("float array").size(),
                res.getOptionIArray("float array"));
        return unit.fail("%s", err.buffer());
    }
    if (!(res.getOptionIArray("long array").size() == ut3.size() &&
            res.getOptionIArray("long array") == ut3))
    {
        err.format("Bad VtDictionary -> UT_Options: long array {} {}",
                res.getOptionIArray("long array").size(),
                res.getOptionIArray("long array"));
        return unit.fail("%s", err.buffer());
    }
    if (!(res.getOptionIArray("ulong array").size() == ut4.size() &&
            res.getOptionIArray("ulong array") == ut4))
    {
        err.format("Bad VtDictionary -> UT_Options: ulong array {} {}",
                res.getOptionIArray("ulong array").size(),
                res.getOptionIArray("ulong array"));
        return unit.fail("%s", err.buffer());
    }
    if (!(res.getOptionSArray("string array").size() == ut5.size() &&
            res.getOptionSArray("string array") == ut5))
    {
        err.format("Bad VtDictionary -> UT_Options: string array {} {}",
                res.getOptionIArray("string array").size(),
                res.getOptionIArray("string array"));
        return unit.fail("%s", err.buffer());
    }
    return unit.ok();
}

static bool
TESTXUSD_Utils()
{
    bool ok = true;

    ok &= testDictionaryBasic();
    ok &= testDictionaryOverflow();
    ok &= testDictionaryNested();
    ok &= testDictionaryArrays();

    return ok;
}

TEST_REGISTER_FN("XUSD_Utils", TESTXUSD_Utils)

PXR_NAMESPACE_CLOSE_SCOPE
