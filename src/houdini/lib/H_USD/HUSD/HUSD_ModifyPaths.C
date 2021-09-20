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

#include "HUSD_ModifyPaths.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_PathSet.h"
#include "HUSD_Path.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <PY/PY_CPythonAPI.h>
#include <PY/PY_CompiledCode.h>
#include <PY/PY_EvaluationContext.h>
#include <UT/UT_Regex.h>
#include <UT/UT_String.h>
#include <UT/UT_WorkBuffer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/clipsAPI.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/assetPath.h>
#include "pxr/usd/sdf/layerUtils.h"
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    typedef UT_SharedPtr<PY_CompiledCode> PythonExpr;
    typedef UT_Array<PythonExpr> PythonExprArray;

    SdfLayerHandle
    layerHoldingOpinion(const UsdAttribute& attr, const UsdTimeCode& time)
    {
        for (const auto& spec: attr.GetPropertyStack(time))
        {
            if (spec->HasDefaultValue() ||
                spec->GetLayer()->GetNumTimeSamplesForPath(spec->GetPath()) > 0)
                return spec->GetLayer();
        }

        return TfNullPtr;
    }

    bool
    modifyPath(SdfAssetPath &assetpath,
        const UT_Array<UT_Regex> &prefixregex,
        const UT_StringArray &replaceprefix,
        const UT_Array<UT_Regex> &suffixregex,
        const UT_StringArray &replacesuffix,
        const PythonExprArray &pythonexpr,
        PY_EvaluationContext *pycontext,
        bool allowchained,
        UT_WorkBuffer *pythonerrors)
    {
        UT_String test(assetpath.GetAssetPath());
        UT_WorkBuffer result;
        UT_String pattern;
        bool changed = false;

        for (int i = 0; i < prefixregex.size(); i++)
        {
            if (prefixregex(i).isValid())
            {
                if (prefixregex(i).replace(result, test.c_str(), replaceprefix(i)))
                {
                    result.stealIntoString(test);
                    changed = true;
                }
                else
                    result.clear();
            }

            if (suffixregex(i).isValid())
            {
                if (suffixregex(i).replace(result, test.c_str(), replacesuffix(i)))
                {
                    result.stealIntoString(test);
                    changed = true;
                }
                else
                    result.clear();
            }

            if (pycontext && pythonexpr(i))
            {
                PY_Result pythonresult;

                PY_PyObject *pystr = PY_PyString_FromString(test.buffer());
                PY_PyDict_SetItemString(
                    (PY_PyObject *)pycontext->getGlobalsDict(),
                    "assetpath", pystr);
                PY_Py_DECREF(pystr);
                pythonexpr(i)->evaluateInContext(PY_Result::NONE,
                    *pycontext, pythonresult);

                if (pythonresult.myResultType != PY_Result::NONE)
                {
                    if (pythonresult.myResultType == PY_Result::STRING)
                    {
                        test = pythonresult.myStringValue;
                        changed = true;
                    }
                    else if (pythonerrors)
                    {
                        if (pythonresult.myResultType == PY_Result::ERR)
                            *pythonerrors = pythonresult.myDetailedErrValue;
                        else
                            pythonerrors->strcpy(
                                "Python rules must return a string.");
                    }
                }
            }

            if (changed && !allowchained)
                break;
        }

        if (changed)
            assetpath = SdfAssetPath(test.toStdString());

        return changed;
    }

    void
    updateAssetPathAttrib(UsdAttribute &attrib,
        const UT_Array<UT_Regex> &prefixregex,
        const UT_StringArray &replaceprefix,
        const UT_Array<UT_Regex> &suffixregex,
        const UT_StringArray &replacesuffix,
        const PythonExprArray &pythonexpr,
        PY_EvaluationContext *pycontext,
        bool allowchained,
        UT_WorkBuffer *pythonerrors)
    {
        SdfAssetPath path;
        std::vector<double> timesamples;
        std::vector<UsdTimeCode> timecodes;

        timecodes.push_back(UsdTimeCode::Default());
        if (attrib.GetTimeSamples(&timesamples))
            for (auto &&timesample : timesamples)
                timecodes.push_back(UsdTimeCode(timesample));
        for (auto &&timecode : timecodes)
        {
            attrib.Get(&path, timecode);
            if (modifyPath(path, prefixregex, replaceprefix,
                suffixregex, replacesuffix, pythonexpr, pycontext,
                allowchained, pythonerrors))
            {
                SdfLayerHandle layer = layerHoldingOpinion(attrib, timecode);
                if (layer)
                    path = SdfAssetPath(
                        SdfComputeAssetPathRelativeToLayer(
                            layer, path.GetAssetPath()));
                attrib.Set(path, timecode);
            }
        }
    }

    void
    updateAssetPathArrayAttrib(UsdAttribute &attrib,
        const UT_Array<UT_Regex> &prefixregex,
        const UT_StringArray &replaceprefix,
        const UT_Array<UT_Regex> &suffixregex,
        const UT_StringArray &replacesuffix,
        const PythonExprArray &pythonexpr,
        PY_EvaluationContext *pycontext,
        bool allowchained,
        UT_WorkBuffer *pythonerrors)
    {
        VtArray<SdfAssetPath> paths;
        std::vector<double> timesamples;
        std::vector<UsdTimeCode> timecodes;

        timecodes.push_back(UsdTimeCode::Default());
        if (attrib.GetTimeSamples(&timesamples))
            for (auto &&timesample : timesamples)
                timecodes.push_back(UsdTimeCode(timesample));
        for (auto &&timecode : timecodes)
        {
            bool changed = false;

            attrib.Get(&paths, timecode);
            for (SdfAssetPath &path : paths)
            {
                if (modifyPath(path, prefixregex, replaceprefix,
                    suffixregex, replacesuffix, pythonexpr, pycontext,
                    allowchained, pythonerrors))
                    changed = true;
            }
            if (changed)
            {
                SdfLayerHandle layer = layerHoldingOpinion(attrib, timecode);
                if (layer)
                {
                    for (int i = 0, n = paths.size(); i < n; i++)
                        paths[i] = SdfAssetPath(
                            SdfComputeAssetPathRelativeToLayer(
                                layer, paths[i].GetAssetPath()));
                }
                attrib.Set(paths, timecode);
            }
        }
    }
};

HUSD_ModifyPaths::HUSD_ModifyPaths(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_ModifyPaths::~HUSD_ModifyPaths()
{
}

bool
HUSD_ModifyPaths::modifyPaths(const HUSD_FindPrims &findprims,
        const UT_StringArray &findprefix,
        const UT_StringArray &replaceprefix,
        const UT_StringArray &findsuffix,
        const UT_StringArray &replacesuffix,
        const UT_StringArray &pythoncode,
        PY_EvaluationContext *pycontext,
        bool modifyassetpaths,
	bool modifylayerpaths,
        bool allowchained) const
{
    auto     outdata = myWriteLock.data();
    bool     success = false;

    if (outdata && outdata->isStageValid())
    {
        auto                 stage = outdata->stage();
        UT_Array<UT_Regex>   prefixregex;
        UT_Array<UT_Regex>   suffixregex;
        PythonExprArray      pythonexpr;
        UT_WorkBuffer        expr;
        bool                 haspythonexpr = false;

        // Convert the findprefix and findsuffix arrays to UT_Regex arrays.
        // Convert the pythoncode to compiled representations.
        success = true;
        for (int i = 0; i < findprefix.size(); i++)
        {
            UT_String pattern;

            expr.clear();
            if (findprefix(i).isstring() &&
                UT_Regex::convertGlobToExpr(expr, findprefix(i).c_str()) &&
                expr.length() > 0)
            {
                if (expr.first() != '^')
                    expr.insert(0, "^", 1);
                if (expr.last() == '$')
                    expr.truncate(expr.length() - 1);
                prefixregex.append(UT_Regex(expr.buffer()));
            }
            else
                prefixregex.append();

            expr.clear();
            if (findsuffix(i).isstring() &&
                UT_Regex::convertGlobToExpr(expr, findsuffix(i).c_str()) &&
                expr.length() > 0)
            {
                if (expr.first() == '^')
                    expr.erase(0, 1);
                if (expr.last() != '$')
                    expr.append('$');
                suffixregex.append(UT_Regex(expr.buffer()));
            }
            else
                suffixregex.append();

            if (pycontext && pythoncode(i).hasNonSpace())
            {
                PythonExpr pyexpr(new PY_CompiledCode(
                    pythoncode(i).c_str(), PY_CompiledCode::EXPRESSION,
                    nullptr, true));

                if (pyexpr->hasSyntaxErrors())
                {
                    HUSD_ErrorScope::addError(HUSD_ERR_PYTHON_ERROR,
                        pyexpr->syntaxErrors().c_str());
                    pythonexpr.append();
                    // Indicate that we failed, but keep trying to compile
                    // the other rules so the user can see all their errors
                    // at once.
                    success = false;
                }
                else
                {
                    haspythonexpr = true;
                    pythonexpr.append(pyexpr);
                }
            }
            else
                pythonexpr.append();
        }

        if (success)
        {
            UT_WorkBuffer        pythonerrors;

            for (auto &&path : findprims.getExpandedPathSet())
            {
                auto prim = stage->GetPrimAtPath(path.sdfPath());

                if (!prim)
                    continue;

                if (modifyassetpaths)
                {
                    for (auto &&attrib : prim.GetAttributes())
                    {
                        // If we might be running any python code on this
                        // attribute, add the attribute's Sdf path to the
                        // python context's globals dictionary.
                        if (haspythonexpr &&
                            (attrib.GetTypeName() ==
                                SdfValueTypeNames->Asset ||
                                attrib.GetTypeName() ==
                                 SdfValueTypeNames->AssetArray))
                        {
                            PY_PyObject *pystr = PY_PyString_FromString(
                                HUSD_Path(attrib.GetPath()).pathStr());
                            PY_PyDict_SetItemString(
                                (PY_PyObject *)pycontext->getGlobalsDict(),
                                "attributepath", pystr);
                            PY_Py_DECREF(pystr);
                        }

                        if (attrib.GetTypeName() == SdfValueTypeNames->Asset)
                        {
                            updateAssetPathAttrib(attrib, prefixregex,
                                replaceprefix, suffixregex, replacesuffix,
                                pythonexpr, pycontext,
                                allowchained, &pythonerrors);
                        }
                        else if (attrib.GetTypeName() ==
                                 SdfValueTypeNames->AssetArray)
                        {
                            updateAssetPathArrayAttrib(attrib, prefixregex,
                                replaceprefix, suffixregex, replacesuffix,
                                pythonexpr, pycontext,
                                allowchained, &pythonerrors);
                        }
                        if (pythonerrors.isstring())
                            break;
                    }
                    if (pythonerrors.isstring())
                        break;
                }
            }

            // Promote any python execution errors so the user will see them.
            if (pythonerrors.isstring())
            {
                HUSD_ErrorScope::addError(HUSD_ERR_PYTHON_ERROR,
                    pythonerrors.buffer());
                success = false;
            }
        }
    }

    return success;
}
