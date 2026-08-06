/*
 * Copyright 2019 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Produced by:
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#include "HDOptions.h"
#include <tools/henv.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_String.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Format.h>

namespace
{
    static const char   *theOptionsVar = "HUSD_HDEBUG_DELEGATE";

    static constexpr UT_StringView  theNo("no-");
    static constexpr UT_StringView  theTrace("trace");
    static constexpr UT_StringView  theMemory("memory");
    static constexpr UT_StringView  theObject("object");
    static constexpr UT_StringView  theImage("image");
    static constexpr UT_StringView  theStore("store");
    static constexpr UT_StringView  theProjection("projection");
    static constexpr UT_StringView  theBPrim("bprim");
    static constexpr UT_StringView  theRPrim("rprim");
    static constexpr UT_StringView  theSPrim("sprim");
    static constexpr UT_StringView  theGeo("geo");
    static constexpr UT_StringView  theMaterial("material");
    static constexpr UT_StringView  theSleep("sleep");

    static constexpr UT_StringLit   theHelp("help");
    static constexpr UT_StringLit   theAsterisk("*");
    static constexpr UT_StringLit   theAll("all");
    static constexpr UT_StringLit   theNoAll("no-all");

    static void
    printHelp()
    {
        static bool     printed = false;
        if (printed)
            return;
        printed = true;
        UTformat(R"(
The variable "{0}" is parsed to set options for the Hydra Debugger.

Options can be enabled using "trace", "trace=true", "trace=1" or "trace=on".
Options can be disabled using "no-trace", "trace=false", "trace=0" or "trace=off".

For example: {0}=trace=on,geo=on,object=0

The current options are:
    Name                Default     Meaning
    bool trace          false       Print out called functions
    bool memory         true        Track memory usage (primvar, buffer, etc.)
    bool object         true        Print out creation/destruction of objects
    bool image          true        Print out AOV information
    bool store          true        Retain primvars in memory
    bool projection     false       Print out camera projection
    bool geo            false       Print out geometry information
    bool material       true        Print out material assignments
    bool bprim          false       Print out a creation/destruction of bprims
    bool rprim          false       Print out a creation/destruction of rprims
    bool sprim          false       Print out a creation/destruction of sprims
    float sleep         0           "render" (spin) for this number of seconds

Special tokens are:
    help        Print this help
    all, *      Enable all boolean options
    no-all      Disable all options

-------------------------------------------------------------------------------
)",
            theOptionsVar);
    };


    struct Options
    {
        Options();
        void    setAll(bool enable)
        {
            myMemory = enable;
            myObject = enable;
            myTrace = enable;
            myImage = enable;
            myStore = enable;
            myProjection = enable;
            myGeo = enable;
            myMaterial = enable;
            myBPrim = enable;
            myRPrim = enable;
            mySPrim = enable;
        }
        float   mySleep = 0;
        bool    myMemory = true;
        bool    myObject = true;
        bool    myTrace = false;
        bool    myImage = true;
        bool    myStore = true;
        bool    myProjection = false;
        bool    myGeo = false;
        bool    myMaterial = false;
        bool    myBPrim = false;
        bool    myRPrim = false;
        bool    mySPrim = false;
    };

    static bool
    parseBool(const UT_String &token, bool def)
    {
        const char      *eq = token.findChar('=');
        if (!eq)
            return def;
        if (!SYSstrcasecmp(eq, "=on"))
            return true;
        if (!SYSstrcasecmp(eq, "=off"))
            return false;
        return SYSatoi(eq+1) != 0;
    }

    template <typename T>
    static T
    parseNumber(const UT_String &token, T def)
    {
        const char *eq = token.findChar('=');
        if (!eq)
            return def;
        return SYSatof(eq+1);
    }

    Options::Options()
    {
        UT_String       var = HoudiniGetenv(theOptionsVar);
        UT_WorkArgs     args;

        var.tokenize(args, ",");
        for (int i = 0, n = args.getArgc(); i < n; ++i)
        {
            UT_String   holder(args[i]);
            holder.trimBoundingSpace();
            if (!holder.isstring())
                continue;
            if (theAsterisk.asRef() == holder || theAll.asRef() == holder)
            {
                setAll(true);
                continue;
            }
            if (theHelp.asRef() == holder)
            {
                printHelp();
                continue;
            }
            if (theNoAll.asRef() == holder)
            {
                setAll(false);
                continue;
            }
            UT_String   token = holder;
            bool        bflag = true;
            if (token.startsWith(theNo))
            {
                token = holder + theNo.length();
                bflag = false;
            }
            if (token.startsWith(theTrace))
                myTrace = parseBool(token, bflag);
            else if (token.startsWith(theMemory))
                myMemory = parseBool(token, bflag);
            else if (token.startsWith(theObject))
                myObject = parseBool(token, bflag);
            else if (token.startsWith(theImage))
                myImage = parseBool(token, bflag);
            else if (token.startsWith(theStore))
                myStore = parseBool(token, bflag);
            else if (token.startsWith(theProjection))
                myProjection = parseBool(token, bflag);
            else if (token.startsWith(theGeo))
                myGeo = parseBool(token, bflag);
            else if (token.startsWith(theMaterial))
                myMaterial = parseBool(token, bflag);
            else if (token.startsWith(theBPrim))
                myBPrim = parseBool(token, bflag);
            else if (token.startsWith(theRPrim))
                myRPrim = parseBool(token, bflag);
            else if (token.startsWith(theSPrim))
                mySPrim = parseBool(token, bflag);
            else if (token.startsWith(theSleep))
                mySleep = parseNumber(token, mySleep);
            else
                UTformat(stderr, "HDebug - unknown option: {}\n", holder);
        }
    }

    const Options &
    options()
    {
        static Options  o;
        return o;
    }
}

bool HDOptions::memory()        { return options().myMemory; }
bool HDOptions::trace()         { return options().myTrace; }
bool HDOptions::object()        { return options().myObject; }
bool HDOptions::image()         { return options().myImage; }
bool HDOptions::store()         { return options().myStore; }
bool HDOptions::projection()    { return options().myProjection; }
bool HDOptions::geo()           { return options().myGeo; }
bool HDOptions::material()      { return options().myMaterial; }
bool HDOptions::bprim()         { return options().myBPrim; }
bool HDOptions::rprim()         { return options().myRPrim; }
bool HDOptions::sprim()         { return options().mySPrim; }
float HDOptions::sleep()        { return options().mySleep; }
