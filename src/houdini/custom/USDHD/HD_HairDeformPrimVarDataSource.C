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
            inserted = _cachemap->insert(
                    acc, UT_StringHolder(_primpath.GetText()));
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
            return VtArray<T>();
        }

        auto shaft = getConstPvVal<T>(pvs, _name, 0.0f);
        VtArray<T> outshaft;

        if (!checkHandle(shaft, _name, "groom"))
        {
            UT_ErrorLog::error(
                    "HairDeform: Shaft attribute missing: {}", _name.GetString());
            return VtArray<T>();
        }

        int npts = shaft->size();

        bool barbs_expanded = false;
        VtValue barbl_value, barbr_value;
        int nbarblpts, nbarbrpts;
        const float *barbl, *barbr;

        if (_barbl_name.size() == 0 && _barbr_name.size() == 0)
        {
            VtValue P_barbl_value, P_barbr_value;
            int P_nbarblpts, P_nbarbrpts;
            const float *ldata = getBarbData(
                    P_barbl_value, P_nbarblpts, pvs, TfToken("P_barbl"), npts, 3);
            const float *rdata = getBarbData(
                    P_barbr_value, P_nbarbrpts, pvs, TfToken("P_barbr"), npts, 3);

            if (ldata && rdata)
            {
                UT_ErrorLog::warning(
                        "HairDeform: Expanding barb values from shaft. nbarblpts: {} nbarbrpts: {}",
                        P_nbarblpts,
                        P_nbarbrpts);

                expandBarbs(
                        outshaft, npts, _shaft_dim, P_nbarblpts, P_nbarblpts, nullptr,
                        nullptr, nullptr, *shaft);

                barbs_expanded = true;
            }
        }
        else
        {
            barbl = getBarbData(barbl_value, nbarblpts, pvs, _barbl_name, npts, _shaft_dim);
            barbr = getBarbData(barbr_value, nbarbrpts, pvs, _barbr_name, npts, _shaft_dim);

            if (barbl && barbr)
            {
                expandBarbs(
                        outshaft, npts, _shaft_dim, nbarblpts, nbarbrpts, barbl,
                        barbr, nullptr, *shaft);

                barbs_expanded = true;
            }
            else
            {
                UT_ErrorLog::error(
                        "HairDeform: At least one barb attribute missing of {}, {}",
                        _barbl_name.GetString(), _barbr_name.GetString());
            }

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
