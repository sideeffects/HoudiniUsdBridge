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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "XUSD_AutoCollection.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Info.h"
#include "HUSD_Path.h"
#include "HUSD_PathSet.h"
#include "XUSD_AttributeUtils.h"
#include <UT/UT_Interrupt.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_StringMMPattern.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <UT/UT_WorkArgs.h>
#include <SYS/SYS_Math.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/base/gf/frustum.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    constexpr UT_StringLit theTestAllArg("all");
    constexpr UT_StringLit theTestNoneArg("none");
    constexpr UT_StringLit theInclusiveArg("inclusive");

    class TypedValues
    {
    public:
        void clear()
        {
            myValueStrings.clear();
            myValuePatterns.clear();
            myValueFloats.clear();
            myValueVec2s.clear();
            myValueVec3s.clear();
            myValueVec4s.clear();
        }
        int maxArraySize() const
        {
            return SYSmax(myValueStrings.size(),
                SYSmax(myValueFloats.size(),
                    myValueVec2s.size(),
                    myValueVec3s.size(),
                    myValueVec4s.size()));
        }
        bool countPopulatedArrays() const
        {
            return (myValueStrings.isEmpty() ? 0 : 1) +
                   (myValueFloats.isEmpty() ? 0 : 1) +
                   (myValueVec2s.isEmpty() ? 0 : 1) +
                   (myValueVec3s.isEmpty() ? 0 : 1) +
                   (myValueVec4s.isEmpty() ? 0 : 1);
        }

        bool equals(const TypedValues &other, fpreal tol) const
        {
            auto compare_fn = [tol](const auto &a, const auto &b) {
                return SYSisEqual(a, b, tol);
            };

            if (!myValueStrings.isEmpty() &&
                myValueStrings == other.myValueStrings)
                return true;
            if (!myValueFloats.isEmpty() &&
                myValueFloats.isEqual(other.myValueFloats, compare_fn))
                return true;
            if (!myValueVec2s.isEmpty() &&
                myValueVec2s.isEqual(other.myValueVec2s, compare_fn))
                return true;
            if (!myValueVec3s.isEmpty() &&
                myValueVec3s.isEqual(other.myValueVec3s, compare_fn))
                return true;
            if (!myValueVec4s.isEmpty() &&
                myValueVec4s.isEqual(other.myValueVec4s, compare_fn))
                return true;

            return false;
        }
        bool anyEntryEqualsSingleEntry(const TypedValues &other, fpreal tol) const
        {
            auto compare_fn = [tol](const auto &a, const auto &b) {
                return SYSisEqual(a, b, tol);
            };

            if (!myValueStrings.isEmpty() && other.myValueStrings.size() == 1 &&
                myValueStrings.find(other.myValueStrings[0]) >= 0)
                return true;
            if (!myValueFloats.isEmpty() && other.myValueFloats.size() == 1 &&
                std::find_if(myValueFloats.begin(), myValueFloats.end(),
                    std::bind(compare_fn, std::placeholders::_1,
                        other.myValueFloats[0])) != myValueFloats.end())
                return true;
            if (!myValueVec2s.isEmpty() && other.myValueVec2s.size() == 1 &&
                std::find_if(myValueVec2s.begin(), myValueVec2s.end(),
                    std::bind(compare_fn, std::placeholders::_1,
                        other.myValueVec2s[0])) != myValueVec2s.end())
                return true;
            if (!myValueVec3s.isEmpty() && other.myValueVec3s.size() == 1 &&
                std::find_if(myValueVec3s.begin(), myValueVec3s.end(),
                    std::bind(compare_fn, std::placeholders::_1,
                        other.myValueVec3s[0])) != myValueVec3s.end())
                return true;
            if (!myValueVec4s.isEmpty() && other.myValueVec4s.size() == 1 &&
                std::find_if(myValueVec4s.begin(), myValueVec4s.end(),
                    std::bind(compare_fn, std::placeholders::_1,
                        other.myValueVec4s[0])) != myValueVec4s.end())
                return true;

            return false;
        }
        bool anyEntryEqualsAnyEntry(const TypedValues &other, fpreal tol) const
        {
            auto compare_fn = [tol](const auto &a, const auto &b) {
                return SYSisEqual(a, b, tol);
            };

            if (!other.myValueStrings.isEmpty() && !myValueStrings.isEmpty())
            {
                for (auto &&value : other.myValueStrings)
                    if (myValueStrings.find(value) >= 0)
                        return true;
            }
            if (!other.myValueFloats.isEmpty() && !myValueFloats.isEmpty())
            {
                for (auto &&value : other.myValueFloats)
                    if (std::find_if(myValueFloats.begin(), myValueFloats.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) != myValueFloats.end())
                        return true;
            }
            if (!other.myValueVec2s.isEmpty() && !myValueVec2s.isEmpty())
            {
                for (auto &&value : other.myValueVec2s)
                    if (std::find_if(myValueVec2s.begin(), myValueVec2s.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) != myValueVec2s.end())
                        return true;
            }
            if (!other.myValueVec3s.isEmpty() && !myValueVec3s.isEmpty())
            {
                for (auto &&value : other.myValueVec3s)
                    if (std::find_if(myValueVec3s.begin(), myValueVec3s.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) != myValueVec3s.end())
                        return true;
            }
            if (!other.myValueVec4s.isEmpty() && !myValueVec4s.isEmpty())
            {
                for (auto &&value : other.myValueVec4s)
                    if (std::find_if(myValueVec4s.begin(), myValueVec4s.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) != myValueVec4s.end())
                        return true;
            }

            return false;
        }
        bool everyEntryEqualsAnyEntry(const TypedValues &other, fpreal tol) const
        {
            auto compare_fn = [tol](const auto &a, const auto &b) {
                return SYSisEqual(a, b, tol);
            };

            if (!other.myValueStrings.isEmpty())
            {
                for (auto &&value : other.myValueStrings)
                    if (myValueStrings.find(value) < 0)
                        return false;
                return true;
            }
            if (!other.myValueFloats.isEmpty())
            {
                for (auto &&value : other.myValueFloats)
                    if (std::find_if(myValueFloats.begin(), myValueFloats.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) == myValueFloats.end())
                        return false;
                return true;
            }
            if (!other.myValueVec2s.isEmpty())
            {
                for (auto &&value : other.myValueVec2s)
                    if (std::find_if(myValueVec2s.begin(), myValueVec2s.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) == myValueVec2s.end())
                        return false;
                return true;
            }
            if (!other.myValueVec3s.isEmpty())
            {
                for (auto &&value : other.myValueVec3s)
                    if (std::find_if(myValueVec3s.begin(), myValueVec3s.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) == myValueVec3s.end())
                        return false;
                return true;
            }
            if (!other.myValueVec4s.isEmpty())
            {
                for (auto &&value : other.myValueVec4s)
                    if (std::find_if(myValueVec4s.begin(), myValueVec4s.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) == myValueVec4s.end())
                        return false;
                return true;
            }

            return (maxArraySize() == 0);
        }
        bool everyEntryNotEqualsAnyEntry(const TypedValues &other, fpreal tol) const
        {
            auto compare_fn = [tol](const auto &a, const auto &b) {
                return SYSisEqual(a, b, tol);
            };

            if (!other.myValueStrings.isEmpty())
            {
                for (auto &&value : other.myValueStrings)
                    if (myValueStrings.find(value) >= 0)
                        return false;
                return true;
            }
            if (!other.myValueFloats.isEmpty())
            {
                for (auto &&value : other.myValueFloats)
                    if (std::find_if(myValueFloats.begin(), myValueFloats.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) == myValueFloats.end())
                        return false;
                return true;
            }
            if (!other.myValueVec2s.isEmpty())
            {
                for (auto &&value : other.myValueVec2s)
                    if (std::find_if(myValueVec2s.begin(), myValueVec2s.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) == myValueVec2s.end())
                        return false;
                return true;
            }
            if (!other.myValueVec3s.isEmpty())
            {
                for (auto &&value : other.myValueVec3s)
                    if (std::find_if(myValueVec3s.begin(), myValueVec3s.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) == myValueVec3s.end())
                        return false;
                return true;
            }
            if (!other.myValueVec4s.isEmpty())
            {
                for (auto &&value : other.myValueVec4s)
                    if (std::find_if(myValueVec4s.begin(), myValueVec4s.end(),
                        std::bind(compare_fn, std::placeholders::_1,
                            value)) == myValueVec4s.end())
                        return false;
                return true;
            }

            return (maxArraySize() == 0);
        }

        UT_Array<UT_StringHolder>        myValueStrings;
        UT_Array<UT_StringMMPattern>     myValuePatterns;
        UT_Fpreal64Array                 myValueFloats;
        UT_Array<UT_Vector2D>            myValueVec2s;
        UT_Array<UT_Vector3D>            myValueVec3s;
        UT_Array<UT_Vector4D>            myValueVec4s;
    };

    class BaseComparator
    {
    public:
        BaseComparator() = default;
        virtual ~BaseComparator() = default;

        virtual void parseArguments(const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            const TypedValues &parsedvalues,
            UT_StringArray &errors)
        {
            auto toleranceit = namedargs.find("tol");
            if (toleranceit != namedargs.end())
                XUSD_AutoCollection::parseFloat(toleranceit->second, myTol);
        }

    protected:
        fpreal64         myTol = SYS_FTOLERANCE;
    };

    class EqualityComparator : public BaseComparator
    {
    public:
        bool compare(const TypedValues &myvalues,
                const TypedValues &attribvalues)
        {
            return myvalues.equals(attribvalues, myTol) ||
                myvalues.anyEntryEqualsSingleEntry(attribvalues, myTol);
        }
    };

    class LessThanComparator : public BaseComparator
    {
    public:
        void parseArguments(const UT_StringArray &orderedargs,
                const UT_StringMap<UT_StringHolder> &namedargs,
                const TypedValues &parsedvalues,
                UT_StringArray &errors) override
        {
            BaseComparator::parseArguments(
                orderedargs, namedargs, parsedvalues, errors);

            auto inclusiveit = namedargs.find(theInclusiveArg.asRef());
            if (inclusiveit != namedargs.end())
                myInclusive = XUSD_AutoCollection::parseBool(inclusiveit->second);

            if (parsedvalues.myValueFloats.size() == 1)
                myNumericValue = parsedvalues.myValueFloats(0);
            else
                errors.append(
                    "Less than comparison requires a single numeric value");
        }
        bool compare(const TypedValues &myvalues,
            const TypedValues &attribvalues)
        {
            if (attribvalues.myValueFloats.size() != 1)
                return false;
            if (myInclusive)
                return SYSisLessOrEqual(
                    attribvalues.myValueFloats(0), myNumericValue);
            else
                return SYSisLess(
                    attribvalues.myValueFloats(0), myNumericValue);
        }

        fpreal           myNumericValue = 0.0;
        bool             myInclusive = false;
    };

    class GreaterThanComparator : public BaseComparator
    {
    public:
        void parseArguments(const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            const TypedValues &parsedvalues,
            UT_StringArray &errors) override
        {
            BaseComparator::parseArguments(
                orderedargs, namedargs, parsedvalues, errors);

            auto inclusiveit = namedargs.find(theInclusiveArg.asRef());
            if (inclusiveit != namedargs.end())
                myInclusive = XUSD_AutoCollection::parseBool(inclusiveit->second);

            if (parsedvalues.myValueFloats.size() == 1)
                myNumericValue = parsedvalues.myValueFloats(0);
            else
                errors.append(
                    "Greater than comparison requires a single numeric value");
        }
        bool compare(const TypedValues &myvalues,
            const TypedValues &attribvalues)
        {
            if (attribvalues.myValueFloats.size() != 1)
                return false;
            if (myInclusive)
                return SYSisGreaterOrEqual(
                    attribvalues.myValueFloats(0), myNumericValue);
            else
                return SYSisGreater(
                    attribvalues.myValueFloats(0), myNumericValue);
        }

        fpreal           myNumericValue = 0.0;
        bool             myInclusive = false;
    };

    class InRangeComparator : public BaseComparator
    {
    public:
        void parseArguments(const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            const TypedValues &parsedvalues,
            UT_StringArray &errors) override
        {
            BaseComparator::parseArguments(
                orderedargs, namedargs, parsedvalues, errors);

            auto inclusiveit = namedargs.find(theInclusiveArg.asRef());
            if (inclusiveit != namedargs.end())
                myInclusive = XUSD_AutoCollection::parseBool(inclusiveit->second);

            if (parsedvalues.myValueFloats.size() == 2)
            {
                myNumericMinValue = parsedvalues.myValueFloats(0);
                myNumericMaxValue = parsedvalues.myValueFloats(1);
                if (myNumericMinValue > myNumericMaxValue)
                    UTswap(myNumericMinValue, myNumericMaxValue);
            }
            else
                errors.append(
                    "Range comparison requires exactly two numeric values");
        }
        bool compare(const TypedValues &myvalues,
            const TypedValues &attribvalues)
        {
            if (attribvalues.myValueFloats.size() != 1)
                return false;
            if (myInclusive)
                return SYSisGreaterOrEqual(
                        attribvalues.myValueFloats(0), myNumericMinValue) &&
                    SYSisLessOrEqual(
                        attribvalues.myValueFloats(0), myNumericMaxValue);
            else
                return SYSisGreater(
                        attribvalues.myValueFloats(0), myNumericMinValue) &&
                    SYSisLess(
                        attribvalues.myValueFloats(0), myNumericMaxValue);
        }

        fpreal           myNumericMinValue = SYS_FPREAL_MAX;
        fpreal           myNumericMaxValue = -SYS_FPREAL_MAX;
        bool             myInclusive = false;
    };

    class OutsideRangeComparator : public BaseComparator
    {
    public:
        void parseArguments(const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            const TypedValues &parsedvalues,
            UT_StringArray &errors) override
        {
            BaseComparator::parseArguments(
                orderedargs, namedargs, parsedvalues, errors);

            auto inclusiveit = namedargs.find(theInclusiveArg.asRef());
            if (inclusiveit != namedargs.end())
                myInclusive = XUSD_AutoCollection::parseBool(inclusiveit->second);

            if (parsedvalues.myValueFloats.size() == 2)
            {
                myNumericMinValue = parsedvalues.myValueFloats(0);
                myNumericMaxValue = parsedvalues.myValueFloats(1);
                if (myNumericMinValue > myNumericMaxValue)
                    UTswap(myNumericMinValue, myNumericMaxValue);
            }
            else
                errors.append(
                    "Range comparison requires exactly two numeric values");
        }
        bool compare(const TypedValues &myvalues,
            const TypedValues &attribvalues)
        {
            if (attribvalues.myValueFloats.size() != 1)
                return false;
            if (myInclusive)
                return SYSisLessOrEqual(
                        attribvalues.myValueFloats(0), myNumericMinValue) ||
                    SYSisGreaterOrEqual(
                        attribvalues.myValueFloats(0), myNumericMaxValue);
            else
                return SYSisLess(
                        attribvalues.myValueFloats(0), myNumericMinValue) ||
                    SYSisGreater(
                        attribvalues.myValueFloats(0), myNumericMaxValue);
        }

        fpreal           myNumericMinValue = SYS_FPREAL_MAX;
        fpreal           myNumericMaxValue = -SYS_FPREAL_MAX;
        bool             myInclusive = false;
    };

    class ContainsComparator : public BaseComparator
    {
    public:
        void parseArguments(const UT_StringArray &orderedargs,
                const UT_StringMap<UT_StringHolder> &namedargs,
                const TypedValues &parsedvalues,
                UT_StringArray &errors) override
        {
            BaseComparator::parseArguments(
                orderedargs, namedargs, parsedvalues, errors);

            auto testit = namedargs.find("test");

            if (testit != namedargs.end())
            {
                if (testit->second == theTestAllArg.asRef())
                    myRequireAll = true;
                else if (testit->second == theTestNoneArg.asRef())
                    myRequireNone = true;
            }
        }

        bool compare(const TypedValues &myvalues,
                const TypedValues &attribvalues)
        {
            if (myRequireNone)
                return myvalues.everyEntryNotEqualsAnyEntry(attribvalues, myTol);
            else if (myRequireAll)
                return myvalues.everyEntryEqualsAnyEntry(attribvalues, myTol);
            else
                return myvalues.anyEntryEqualsAnyEntry(attribvalues, myTol);
        }

        bool                             myRequireAll = false;
        bool                             myRequireNone = false;
    };
};

////////////////////////////////////////////////////////////////////////////
// XUSD_PropAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_PropAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_PropAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
        {
            if (!UT_String::wildcardMatchCheck(orderedargs[0]))
            {
                UT_WorkArgs args;
                UT_String tokenstr(orderedargs[0]);
                tokenstr.tokenize(args);
                for (auto &&arg : args)
                    myPropNames.push_back(TfToken(arg));
            }
            else
            {
                myPropPattern.compile(orderedargs[0], true, ", \t\n");
            }
        }

        auto     timeit = namedargs.find("t");
        fpreal64 tstep = 1.0;

        myStartSample = myUsdTimeCode.GetValue();
        myEndSample = myUsdTimeCode.GetValue();
        if (timeit != namedargs.end())
        {
            if (!parseTimeRange(timeit->second,
                    myStartSample, myEndSample, tstep))
                myTokenParsingError = "Invalid `t` argument specified.";
            myTimeCodesOverridden = true;
        }

        auto     authoredit = namedargs.find("authored");
        if (authoredit != namedargs.end())
            myAuthoredOnly = parseBool(authoredit->second);
    }
    ~XUSD_PropAutoCollection() override
    { }

    bool getMayBeTimeVarying() const override
    {
        if (XUSD_AutoCollection::getMayBeTimeVarying())
            return true;

        for (auto &&maybetimevarying : myMayBeTimeVarying)
            if (maybetimevarying)
                return true;
        return false;
    }

protected:
    std::vector<UsdProperty> getMatchingProperties(const UsdPrim &prim) const
    {
        std::vector<UsdProperty> props;

        if (myPropPattern.isEmpty())
        {
            for (auto &&name : myPropNames)
            {
                UsdProperty prop = prim.GetProperty(name);

                if (myAuthoredOnly)
                {
                    if (prop && prop.IsAuthored())
                        props.push_back(prop);
                }
                else
                {
                    if (prop)
                        props.push_back(prop);
                }
            }
        }
        else
        {
            auto predicate = [this](const TfToken &name) {
                return UT_String(name.GetText()).multiMatch(myPropPattern);
            };

            if (myAuthoredOnly)
                props = prim.GetAuthoredProperties(predicate);
            else
                props = prim.GetProperties(predicate);
        }

        return props;
    }

    int parseValues(const UT_StringArray &orderedargs,
            bool allow_strings,
            bool allow_floats,
            bool allow_vectors)
    {
        static const int theFixedArgCount = 1;
        int numargs = orderedargs.size() - theFixedArgCount;
        bool all_strings = allow_strings;
        bool all_floats = allow_floats;
        bool all_vec2s = allow_vectors;
        bool all_vec3s = allow_vectors;
        bool all_vec4s = allow_vectors;

        if (all_strings)
        {
            myValues.myValueStrings.setSize(numargs);
            myValues.myValuePatterns.setSize(numargs);
        }
        if (all_floats)
            myValues.myValueFloats.setSize(numargs);
        if (all_vec2s)
            myValues.myValueVec2s.setSize(numargs);
        if (all_vec3s)
            myValues.myValueVec3s.setSize(numargs);
        if (all_vec4s)
            myValues.myValueVec4s.setSize(numargs);

        for (int i = 0; i < numargs; i++)
        {
            const UT_StringHolder &arg = orderedargs[i+theFixedArgCount];

            if (all_floats && !parseFloat(arg, myValues.myValueFloats[i]))
                all_floats = false;
            if (all_vec2s && !parseVector2(arg, myValues.myValueVec2s[i]))
                all_vec2s = false;
            if (all_vec3s && !parseVector3(arg, myValues.myValueVec3s[i]))
                all_vec3s = false;
            if (all_vec4s && !parseVector4(arg, myValues.myValueVec4s[i]))
                all_vec4s = false;

            if (all_strings)
            {
                myValues.myValueStrings[i] = arg;
                if (UT_String::wildcardMatchCheck(arg))
                    myValues.myValuePatterns[i].compile(arg, true, " \t\n");
            }
        }

        if (!all_strings)
        {
            myValues.myValueStrings.clear();
            myValues.myValuePatterns.clear();
        }
        if (!all_floats)
            myValues.myValueFloats.clear();
        if (!all_vec2s)
            myValues.myValueVec2s.clear();
        if (!all_vec3s)
            myValues.myValueVec3s.clear();
        if (!all_vec4s)
            myValues.myValueVec4s.clear();

        return myValues.maxArraySize();
    }

    bool getTypedValues(const VtValue &value, TypedValues &typed_values) const
    {
        if (value.IsArrayValued())
        {
            if (HUSDgetValue(value, typed_values.myValueStrings))
                ;
            else if (HUSDgetValue(value, typed_values.myValueFloats))
                ;
            else if (HUSDgetValue(value, typed_values.myValueVec2s))
                ;
            else if (HUSDgetValue(value, typed_values.myValueVec3s))
                ;
            else if (HUSDgetValue(value, typed_values.myValueVec4s))
                ;
            else
                return false;
        }
        else
        {
            UT_StringHolder  sval;
            fpreal64         fval;
            UT_Vector2D      v2val;
            UT_Vector3D      v3val;
            UT_Vector4D      v4val;

            if (HUSDgetValue(value, sval))
                typed_values.myValueStrings.append(sval);
            else if (HUSDgetValue(value, fval))
                typed_values.myValueFloats.append(fval);
            else if (HUSDgetValue(value, v2val))
                typed_values.myValueVec2s.append(v2val);
            else if (HUSDgetValue(value, v3val))
                typed_values.myValueVec3s.append(v3val);
            else if (HUSDgetValue(value, v4val))
                typed_values.myValueVec4s.append(v4val);
            else
                return false;
        }

        return true;
    }

    UT_StringMMPattern                   myPropPattern;
    TfTokenVector                        myPropNames;
    TypedValues                          myValues;
    mutable UT_ThreadSpecificValue<bool> myMayBeTimeVarying;
    fpreal64                             myStartSample = -SYS_FP64_MAX;
    fpreal64                             myEndSample = SYS_FP64_MAX;
    bool                                 myAuthoredOnly = true;
    bool                                 myTimeCodesOverridden = false;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AttribAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_AttribAutoCollection : public XUSD_PropAutoCollection
{
public:
    XUSD_AttribAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_PropAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
    }
    ~XUSD_AttribAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch,
        UT_Array<int64> *instance_ids) const override
    {
        std::vector<UsdProperty> props = getMatchingProperties(prim);

        for (auto &&prop : props)
        {
            UsdAttribute attrib = prop.As<UsdAttribute>();
            if (attrib && matchesAttribute(attrib))
                return true;
        }

        return false;
    }

protected:
    virtual bool matchesAttribute(const UsdAttribute &attrib) const = 0;

    template <class CompareFuncType>
    bool matchAnyTimeSample(const UsdAttribute &attrib,
                            const CompareFuncType &compare_func) const
    {
        GfInterval           interval(myStartSample, myEndSample);
        std::vector<double>  times;
        TypedValues          attribvalues;
        VtValue              value;
        bool                *maybetimevarying_ptr = nullptr;

        attrib.GetTimeSamplesInInterval(interval, &times);
        if (times.empty())
            times.push_back(myStartSample);
        if (!myTimeCodesOverridden)
            maybetimevarying_ptr = &myMayBeTimeVarying.get();

        for (auto &&t : times)
        {
            if (maybetimevarying_ptr && !*maybetimevarying_ptr)
                *maybetimevarying_ptr = attrib.ValueMightBeTimeVarying();
            if (attrib.Get(&value, UsdTimeCode(t)))
            {
                // getTypedValues returns false if the attribute contains an
                // unsupported data type.
                if (!getTypedValues(value, attribvalues))
                    return false;
                if (compare_func(attribvalues))
                    return true;
                attribvalues.clear();
            }
        }

        return false;
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AttribExistsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_AttribExistsAutoCollection : public XUSD_AttribAutoCollection
{
public:
    XUSD_AttribExistsAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_AttribAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
    }
    ~XUSD_AttribExistsAutoCollection() override
    { }

protected:
    bool matchesAttribute(const UsdAttribute &attrib) const override
    {
        return true;
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AttribComparatorAutoCollection
////////////////////////////////////////////////////////////////////////////

template<class Comparator>
class XUSD_AttribComparatorAutoCollection : public XUSD_AttribAutoCollection
{
public:
    XUSD_AttribComparatorAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_AttribAutoCollection(collectionname, orderedargs, namedargs,
            lock, demands, nodeid, timecode)
    {
        UT_StringArray errors;

        if (parseValues(orderedargs, true, true, true) == 0)
            errors.append("No values found for comparison.");

        myComparator = UTmakeUnique<Comparator>();
        myComparator->parseArguments(orderedargs, namedargs, myValues, errors);

        if (errors.size() > 0)
        {
            UT_WorkBuffer buf;
            errors.join("\n", buf);
            myTokenParsingError = buf;
        }
    }
    ~XUSD_AttribComparatorAutoCollection() override
    { }

protected:
    bool matchesAttribute(const UsdAttribute &attrib) const override
    {
        return matchAnyTimeSample(attrib,
            [this](const TypedValues &attribvalues) {
                return myComparator->compare(myValues, attribvalues);
            });
    }

    UT_UniquePtr<Comparator>     myComparator;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RelAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RelAutoCollection : public XUSD_PropAutoCollection
{
public:
    XUSD_RelAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_PropAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
        auto     forwardedit = namedargs.find("forwarded");
        if (forwardedit != namedargs.end())
            myGetForwardedTargets = parseBool(forwardedit->second);
    }
    ~XUSD_RelAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch,
        UT_Array<int64> *instance_ids) const override
    {
        std::vector<UsdProperty> props = getMatchingProperties(prim);

        for (auto &&prop : props)
        {
            UsdRelationship rel = prop.As<UsdRelationship>();
            if (rel && matchesRelationship(rel))
                return true;
        }

        return false;
    }

protected:
    virtual bool matchesRelationship(const UsdRelationship &rel) const = 0;

    SdfPathVector getTargets(const UsdRelationship &rel) const
    {
        SdfPathVector reltargets;

        if (myGetForwardedTargets)
            rel.GetForwardedTargets(&reltargets);
        else
            rel.GetTargets(&reltargets);

        return reltargets;
    }
    SdfPathSet getTargetsSet(const UsdRelationship &rel) const
    {
        SdfPathVector reltargets = getTargets(rel);
        SdfPathSet reltargetset;

        for (auto &&reltarget : reltargets)
            reltargetset.emplace(reltarget);

        return reltargetset;
    }

    void buildPathsFromStringArgs(HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
    {
        UT_WorkBuffer badpaths;

        for (auto &&str : myValues.myValueStrings)
        {
            // Treat the string as a pattern.
            parsePattern(str,
                lock, demands, nodeid, timecode, false, myPaths,
                &myMayBeTimeVaryingSubPattern);
        }
    }

    XUSD_PathSet                     myPaths;
    bool                             myGetForwardedTargets = false;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RelExistsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RelExistsAutoCollection : public XUSD_RelAutoCollection
{
public:
    XUSD_RelExistsAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_RelAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
    }
    ~XUSD_RelExistsAutoCollection() override
    { }

protected:
    bool matchesRelationship(const UsdRelationship &rel) const override
    {
        return true;
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RelEqualsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RelEqualsAutoCollection : public XUSD_RelAutoCollection
{
public:
    XUSD_RelEqualsAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_RelAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
        if (parseValues(orderedargs, true, true, true) == 0)
            myTokenParsingError = "No values found for comparison.";
        else
            buildPathsFromStringArgs(lock, demands, nodeid, timecode);
    }
    ~XUSD_RelEqualsAutoCollection() override
    { }

protected:
    bool matchesRelationship(const UsdRelationship &rel) const override
    {
        SdfPathVector reltargets = getTargets(rel);

        // If we have a different number of targets and paths, we don't match.
        if (reltargets.size() != myPaths.size())
            return false;

        // If any of the targets are not among our paths, we don't match.
        for (auto &&reltarget : reltargets)
            if (myPaths.find(reltarget) == myPaths.end())
                return false;

        return true;
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RelContainsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RelContainsAutoCollection : public XUSD_RelAutoCollection
{
public:
    XUSD_RelContainsAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_RelAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
        if (parseValues(orderedargs, true, true, true) == 0)
            myTokenParsingError = "No values found for comparison.";
        else
            buildPathsFromStringArgs(lock, demands, nodeid, timecode);

        auto     testit = namedargs.find("test");
        if (testit != namedargs.end())
        {
            if (testit->second == theTestAllArg.asRef())
                myRequireAll = true;
            else if (testit->second == theTestNoneArg.asRef())
                myRequireNone = true;
        }
    }
    ~XUSD_RelContainsAutoCollection() override
    { }

protected:
    bool matchesRelationship(const UsdRelationship &rel) const override
    {
        // If no paths have been specified, the relationship cannot contain
        // any or all of the targets, so treat this as not matching.
        if (myPaths.empty())
            return false;

        SdfPathSet reltargetset = getTargetsSet(rel);

        for (auto &&path : myPaths)
        {
            if (reltargetset.find(path) == reltargetset.end())
            {
                if (myRequireAll)
                    return false;
            }
            else if (myRequireNone)
                return false;
            else if (!myRequireAll)
                return true;
        }

        // If we require all (or none) of our paths in the rel targets, then
        // we early exit in the "false" case, so getting here means we match.
        // If we don't require all (or none) of our paths in the rel target,
        // then we early exit in the "true" case, so getting here means we
        // don't match.
        return (myRequireAll || myRequireNone);
    }

    bool                             myRequireAll = false;
    bool                             myRequireNone = false;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_PrimvarAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_PrimvarAutoCollection : public XUSD_PropAutoCollection
{
public:
    XUSD_PrimvarAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_PropAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
        auto     inheritedit = namedargs.find("inherited");
        if (inheritedit != namedargs.end())
            myAcceptInherited = parseBool(inheritedit->second);
    }
    ~XUSD_PrimvarAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch,
        UT_Array<int64> *instance_ids) const override
    {
        UsdGeomPrimvarsAPI primvarsapi(prim);
        bool primvar_found_on_prim = false;
        bool match_is_inheritable = false;
        bool matches = false;

        if (primvarsapi)
        {
            std::vector<UsdGeomPrimvar> primvars;
            if (myAuthoredOnly)
                primvars = primvarsapi.GetAuthoredPrimvars();
            else
                primvars = primvarsapi.GetPrimvars();

            for (auto &&primvar : primvars)
            {
                if (myPropPattern.isEmpty())
                {
                    if (std::find(myPropNames.begin(), myPropNames.end(),
                            primvar.GetPrimvarName()) == myPropNames.end())
                        continue;
                }
                else
                {
                    if (!UT_String(primvar.GetPrimvarName().GetText()).
                            multiMatch(myPropPattern))
                        continue;
                }

                primvar_found_on_prim = true;
                if (matchesPrimvar(primvar))
                {
                    // Only constant interpolation primvars with an authored
                    // value are inheritable.
                    match_is_inheritable = (primvar.HasAuthoredValue() &&
                        primvar.GetInterpolation() == UsdGeomTokens->constant);
                    matches = true;
                    break;
                }
            }

            // If we didn't find the primvar on this prim and we are
            // interested in inherited primvars, check them here (hopefully
            // our direct parent is already in the cache).
            if (!primvar_found_on_prim && !matches && myAcceptInherited)
            {
                auto &matchmap = myInheritableMatchMap.get();
                auto parent = prim.GetParent();

                if (parent)
                {
                    auto it = matchmap.find(parent.GetPath());

                    // If we don't have a value for this primvar, then we
                    // match this condition if our parent matches it, and
                    // the match is on an inheritable primvar.
                    if (it == matchmap.end())
                    {
                        matchPrimitive(parent, prune_branch, instance_ids);
                        it = matchmap.find(parent.GetPath());
                    }
                    if (it != matchmap.end() && it->second.second)
                    {
                        // Our parent has an inheritable matching primvar.
                        // This means we match as well, and our match is
                        // also inheritable.
                        matches = it->second.first;
                        match_is_inheritable = true;
                    }
                }
            }
        }

        // If we are interested in inherited primvars, record
        // our response for this prim in the cache.
        if (myAcceptInherited)
            myInheritableMatchMap.get().emplace(prim.GetPath(),
                std::make_pair(matches, match_is_inheritable));

        return matches;
    }

protected:
    typedef UT_Map<SdfPath, std::pair<bool, bool>> PrimvarMap;

    virtual bool matchesPrimvar(const UsdGeomPrimvar &primvar) const = 0;

    template <class CompareFuncType>
    bool matchAnyTimeSample(const UsdGeomPrimvar &primvar,
            const CompareFuncType &compare_func) const
    {
        GfInterval           interval(myStartSample, myEndSample);
        std::vector<double>  times;
        TypedValues          attribvalues;
        VtValue              value;
        bool                *maybetimevarying_ptr = nullptr;

        primvar.GetTimeSamplesInInterval(interval, &times);
        if (times.empty())
            times.push_back(myStartSample);
        if (!myTimeCodesOverridden)
            maybetimevarying_ptr = &myMayBeTimeVarying.get();

        for (auto &&t : times)
        {
            if (primvar.ComputeFlattened(&value, UsdTimeCode(t)))
            {
                if (maybetimevarying_ptr && !*maybetimevarying_ptr)
                    *maybetimevarying_ptr = primvar.ValueMightBeTimeVarying();
                // getTypedValues returns false if the attribute contains an
                // unsupported data type.
                if (!getTypedValues(value, attribvalues))
                    return false;
                if (compare_func(attribvalues))
                    return true;
                attribvalues.clear();
            }
        }

        return false;
    }

    mutable UT_ThreadSpecificValue<PrimvarMap>   myInheritableMatchMap;
    bool                                         myAcceptInherited = true;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_PrimvarExistsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_PrimvarExistsAutoCollection : public XUSD_PrimvarAutoCollection
{
public:
    XUSD_PrimvarExistsAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_PrimvarAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
    }
    ~XUSD_PrimvarExistsAutoCollection() override
    { }

protected:
    bool matchesPrimvar(const UsdGeomPrimvar &primvar) const override
    {
        return true;
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_PrimvarComparatorAutoCollection
////////////////////////////////////////////////////////////////////////////

template<class Comparator>
class XUSD_PrimvarComparatorAutoCollection : public XUSD_PrimvarAutoCollection
{
public:
    XUSD_PrimvarComparatorAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_PrimvarAutoCollection(collectionname, orderedargs, namedargs,
            lock, demands, nodeid, timecode)
    {
        UT_StringArray errors;

        if (parseValues(orderedargs, true, true, true) == 0)
            errors.append("No values found for comparison.");

        myComparator = UTmakeUnique<Comparator>();
        myComparator->parseArguments(orderedargs, namedargs, myValues, errors);

        if (errors.size() > 0)
        {
            UT_WorkBuffer buf;
            errors.join("\n", buf);
            myTokenParsingError = buf;
        }
    }
    ~XUSD_PrimvarComparatorAutoCollection() override
    { }

protected:
    bool matchesPrimvar(const UsdGeomPrimvar &primvar) const override
    {
        return matchAnyTimeSample(primvar,
            [this](const TypedValues &attribvalues) {
                return myComparator->compare(myValues, attribvalues);
            });
    }

    UT_UniquePtr<Comparator>     myComparator;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AutoCollection registration
////////////////////////////////////////////////////////////////////////////

void
XUSD_AutoCollection::registerPropertyPlugins()
{
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AttribExistsAutoCollection>("attribexists"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AttribComparatorAutoCollection<EqualityComparator>>(
            "attribequals"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AttribComparatorAutoCollection<ContainsComparator>>(
            "attribcontains"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AttribComparatorAutoCollection<LessThanComparator>>(
            "attriblessthan"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AttribComparatorAutoCollection<GreaterThanComparator>>(
            "attribgreaterthan"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AttribComparatorAutoCollection<InRangeComparator>>(
            "attribinrange"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AttribComparatorAutoCollection<OutsideRangeComparator>>(
            "attriboutsiderange"));

    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PrimvarExistsAutoCollection>("primvarexists"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PrimvarComparatorAutoCollection<EqualityComparator>>(
            "primvarequals"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PrimvarComparatorAutoCollection<ContainsComparator>>(
            "primvarcontains"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PrimvarComparatorAutoCollection<LessThanComparator>>(
            "primvarlessthan"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PrimvarComparatorAutoCollection<GreaterThanComparator>>(
            "primvargreaterthan"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PrimvarComparatorAutoCollection<InRangeComparator>>(
            "primvarinrange"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PrimvarComparatorAutoCollection<OutsideRangeComparator>>(
            "primvaroutsiderange"));

    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RelExistsAutoCollection>("relexists"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RelEqualsAutoCollection>("relequals"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RelContainsAutoCollection>("relcontains"));
}

PXR_NAMESPACE_CLOSE_SCOPE
