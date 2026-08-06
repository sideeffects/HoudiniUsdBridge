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

#pragma once

#include "HD_HairDeformSceneIndex.h"
#include "HD_HairDeformUtils.h"

#include <UT/UT_Tracing.h>

#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

template <typename T>
class HD_HairDeformPrimVarDataSource
    : public HdTypedSampledDataSource<VtArray<T>>
{
public:
    using Time = HdSampledDataSource::Time;
    HD_DECLARE_DATASOURCE(HD_HairDeformPrimVarDataSource);

    VtValue GetValue(const Time shutterOffset) override;
    VtArray<T> GetTypedValue(const Time shutterOffset) override;
    bool GetContributingSampleTimesForInterval(
            const Time startTime,
            const Time endTime,
            std::vector<Time> *const outSampleTimes) override;

private:
    HD_HairDeformPrimVarDataSource(
            SdfPath primpath,
            HdContainerDataSourceHandle primds,
            TfToken name,
            TfToken barbl_name,
            TfToken barbr_name,
            int shaft_dim,
            PrimVarCacheMapPtr cachemap)
        : _primpath(primpath)
        , _primds(primds)
        , _name(name)
        , _barbl_name(barbl_name)
        , _barbr_name(barbr_name)
        , _shaft_dim(shaft_dim)
        , _cachemap(cachemap)
    {
    }

    SdfPath _primpath;
    HdContainerDataSourceHandle _primds;
    TfToken _name;
    TfToken _barbl_name;
    TfToken _barbr_name;
    int _shaft_dim = 1;
    PrimVarCacheMapPtr _cachemap;
};

PXR_NAMESPACE_CLOSE_SCOPE
