/*
 * Copyright 2025 Side Effects Software Inc.
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
 */

#include "HD_HairDeformPrimVarDataSource.h"
#include "HD_HairDeformUtils.h"

#include <UT/UT_Tracing.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{

const auto &thePrimvarsLocator = HdPrimvarsSchema::GetDefaultLocator();

}

template <typename T>
VtValue
HD_HairDeformPrimVarDataSource<T>::GetValue(const Time shutterOffset)
{
    return VtValue(GetTypedValue(shutterOffset));
}

template <typename T>
VtArray<T>
HD_HairDeformPrimVarDataSource<T>::GetTypedValue(const Time shutterOffset)
{
    using namespace HD_HairDeformUtils;

    utZoneScoped;
    utZoneTextSH(UT_StringHolder(_name));

    UT_StringHolder primvartoken;
    UT_StringHolder cache_key = makePrimVarCacheKey(
            _primpath.GetText(), _name.GetText());

    PrimVarCacheMapType::accessor acc;
    bool inserted = _cachemap->insert(acc, cache_key);

    if (!inserted)
    {
        cacheLog("HairDeform: CACHE HIT primvar '{}'", _name.GetString());

        if (acc->second.myArray.IsHolding<VtArray<T>>())
            return acc->second.myArray.Get<VtArray<T>>();
        else
        {
            _cachemap->erase(acc);
            inserted = _cachemap->insert(acc, cache_key);
        }
    }

    if (inserted)
    {
        cacheLog("HairDeform: CACHE MISS primvar '{}'", _name.GetString());

        // get groom primvars
        HdPrimvarsSchema pvs = HdPrimvarsSchema::GetFromParent(_primds);
        if (!pvs)
        {
            UT_ErrorLog::error("HairDeform: Primvars schema missing on groom");
            _cachemap->erase(acc);
            return VtArray<T>();
        }

        auto shaft = getConstPvVal<T>(pvs, _name, 0.0f);
        VtArray<T> outshaft;

        if (!checkHandle(shaft, _name, "groom"))
        {
            UT_ErrorLog::error(
                    "HairDeform: Shaft attribute missing: {}", _name.GetString());
            _cachemap->erase(acc);
            return VtArray<T>();
        }

        int npts = shaft->size();

        bool barbs_expanded = false;
        VtValue barbl_value, barbr_value;

        // Counts hdMakeRestPoints() captured for this groom. Copied out so the
        // accessor isn't held while expanding. Left zeroed until the points
        // data source has run, in which case we derive them below instead.
        HD_HairDeformBarbLayout barblayout;
        {
            RestPointsCacheMapType::const_accessor rpacc;
            if (_restpointscachemap->find(
                        rpacc, UT_StringHolder(_primpath.GetText())))
                barblayout = rpacc->second.myBarbLayout;
        }

        // The nbarbpts come from P/points, whatever is being expanded.  A
        // layout captured off a different shaft point count is a stale entry
        // for this path, so rederive rather than size the expansion on it.
        VtValue P_barbl_value, P_barbr_value;
        int nbarblpts = 0, nbarbrpts = 0;
        bool have_counts
                = barblayout.hasBarbs() && barblayout.myNumShaftPts == npts;

        if (have_counts)
        {
            nbarblpts = barblayout.myNumBarbL;
            nbarbrpts = barblayout.myNumBarbR;
        }
        else
        {
            BarbFloats ldata = getBarbData(
                    P_barbl_value, nbarblpts, pvs, TfToken("P_barbl"), npts, 3);
            BarbFloats rdata = getBarbData(
                    P_barbr_value, nbarbrpts, pvs, TfToken("P_barbr"), npts, 3);
            have_counts = ldata.isValid() && rdata.isValid()
                    && (nbarblpts > 0 || nbarbrpts > 0)
                    && hasBarbOrient(pvs);
        }

        if (_barbl_name.size() == 0 && _barbr_name.size() == 0)
        {
            // No per-barb data; the fallback below expands from the shaft.
            if (have_counts)
                UT_ErrorLog::warning(
                        "HairDeform: Expanding barb values from shaft. nbarblpts: {} nbarbrpts: {}",
                        nbarblpts,
                        nbarbrpts);
        }
        else if (have_counts)
        {
            // A side the positions gave no barbs is not read at all;
            // expandBarbs() writes nothing for it, so a null stays correct.
            int barbl_dim = 0, barbr_dim = 0;
            bool barbl_one = false, barbr_one = false;
            BarbFloats barbl, barbr;
            bool sides_read = true;

            if (nbarblpts > 0)
            {
                barbl = getBarbDataForCount(
                        barbl_value, barbl_dim, barbl_one, pvs, _barbl_name,
                        npts, nbarblpts);
                sides_read &= barbl.isValid();
            }
            if (nbarbrpts > 0)
            {
                barbr = getBarbDataForCount(
                        barbr_value, barbr_dim, barbr_one, pvs, _barbr_name,
                        npts, nbarbrpts);
                sides_read &= barbr.isValid();
            }

            int barb_dim = barbl_dim ? barbl_dim : barbr_dim;
            bool one_value_per_barb = barbl.isValid() ? barbl_one : barbr_one;

            if (!sides_read)
            {
                UT_ErrorLog::error(
                        "HairDeform: Barb attribute {} or {} is missing, or "
                        "holds neither a whole number of floats for the {}, "
                        "{} barbs per shaft point the positions have nor one "
                        "value per shaft point",
                        _barbl_name.GetString(), _barbr_name.GetString(),
                        nbarblpts, nbarbrpts);
            }
            else if (barbl_dim && barbr_dim && barbl_dim != barbr_dim)
            {
                UT_ErrorLog::error(
                        "HairDeform: Barb attributes {}, {} disagree on how "
                        "many floats they carry per barb point ({}, {})",
                        _barbl_name.GetString(), _barbr_name.GetString(),
                        barbl_dim, barbr_dim);
            }
            else if (barbl.isValid() && barbr.isValid()
                     && barbl_one != barbr_one)
            {
                // expandBarbs() walks both sides the same way, and a groom
                // with one side per barb point and the other shared is a
                // mistake rather than a layout to support.
                UT_ErrorLog::error(
                        "HairDeform: Barb attributes {}, {} are not laid out "
                        "the same way: one holds a value per barb point, the "
                        "other one value per shaft point",
                        _barbl_name.GetString(), _barbr_name.GetString());
            }
            else if (barb_dim < _shaft_dim)
            {
                UT_ErrorLog::error(
                        "HairDeform: Barb attributes {}, {} carry {} floats "
                        "per point, which cannot fill the {} of shaft "
                        "primvar {}",
                        _barbl_name.GetString(), _barbr_name.GetString(),
                        barb_dim, _shaft_dim, _name.GetString());
            }
            else if (barb_dim > theMaxBarbDim)
            {
                // Divides evenly but cannot be a real dim, so the attribute
                // carries a different number of barbs than the positions did.
                // Only catches an overshoot past theMaxBarbDim -- a count off
                // by a small exact factor lands back in range, and packed
                // data cannot tell that from a genuinely wider attribute.
                UT_ErrorLog::error(
                        "HairDeform: Barb attributes {}, {} do not hold the "
                        "{}, {} barbs per shaft point the positions have",
                        _barbl_name.GetString(), _barbr_name.GetString(),
                        nbarblpts, nbarbrpts);
            }
            else
            {
                expandBarbs(
                        outshaft, npts, barb_dim, nbarblpts, nbarbrpts, barbl,
                        barbr, nullptr, *shaft, /* xform_to_object */ false,
                        one_value_per_barb);

                barbs_expanded = true;
            }
        }
        else
        {
            UT_ErrorLog::error(
                    "HairDeform: Cannot expand {}: no barb counts from "
                    "P_barbl, P_barbr",
                    _name.GetString());
        }

        // The points data source expanded on these same counts, so a shaft
        // sized primvar would be short whatever went wrong above.  Repeat the
        // shaft value into every barb point: flat, but the size asked for.
        if (!barbs_expanded && have_counts)
        {
            expandBarbs(
                    outshaft, npts, _shaft_dim, nbarblpts, nbarbrpts,
                    BarbFloats(), BarbFloats(), nullptr, *shaft);

            barbs_expanded = true;
        }

        if (!barbs_expanded)
        {
            resizeUninitialized(outshaft, shaft->size());
            std::uninitialized_copy(
                    shaft->begin(), shaft->end(), outshaft.begin());
        }

        acc->second.myLocator
                = thePrimvarsLocator.Append(HdDataSourceLocator(_name));
        acc->second.myArray = outshaft;
        return outshaft;
    }

    return VtArray<T>();
}

template <typename T>
bool
HD_HairDeformPrimVarDataSource<T>::GetContributingSampleTimesForInterval(
        const Time startTime,
        const Time endTime,
        std::vector<Time> *const outSampleTimes)
{
    return false;
}

template class HD_HairDeformPrimVarDataSource<float>;
template class HD_HairDeformPrimVarDataSource<GfVec2f>;
template class HD_HairDeformPrimVarDataSource<GfVec3f>;

PXR_NAMESPACE_CLOSE_SCOPE
