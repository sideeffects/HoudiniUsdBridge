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

#include "HD_HairDeformPointsDataSource.h"
#include "HD_HairDeformPrimVarDataSource.h"
#include "HD_HairDeformPrimVarsDataSource.h"

#include <UT/UT_Array.h>
#include <UT/UT_ConcurrentHashMap.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_Set.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_Tracing.h>

#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>

#include <regex>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{

const TfToken thePosName("P");
const TfToken theUvName("uv");
const TfToken theStName("st");
const TfToken theWidthName("width");
const TfToken theCdName("Cd");

} // namespace

TfTokenVector
HD_HairDeformPrimVarsDataSource::GetNames()
{
    HdPrimvarsSchema pvschema = HdPrimvarsSchema::GetFromParent(_primds);
    TfTokenVector names = pvschema.GetPrimvarNames();

    for (const TfToken &name : names)
    {
        std::string shaftattrib = getShaftAttribName(name.GetString());
        if (!shaftattrib.empty())
        {
            if (shaftattrib == thePosName)
                shaftattrib = HdTokens->points;
            else if (shaftattrib == theUvName)
                shaftattrib = theStName;
            else if (shaftattrib == theWidthName)
                shaftattrib = HdTokens->widths;
            else if (shaftattrib == theCdName)
                shaftattrib = HdTokens->displayColor;


            if (std::find(names.begin(), names.end(), shaftattrib)
                == names.end())
            {
                UT_ErrorLog::error(
                        "HairDeform: Found barb attribute {} but missing corresponding "
                        "shaft attribute {}",
                        name.GetString(), shaftattrib);
            }
        }
    }

    return names;
}

HdDataSourceBaseHandle
HD_HairDeformPrimVarsDataSource::Get(const TfToken &name)
{
    HdPrimvarsSchema pvschema = HdPrimvarsSchema::GetFromParent(_primds);
    HdContainerDataSourceHandle input_container;
    HdDataSourceBaseHandle value_handle;

    auto pv = pvschema.GetPrimvar(name);

    if (!pv)
        return HdDataSourceBaseHandle();

    input_container = pv.GetContainer();
    TfTokenVector names = pvschema.GetPrimvarNames();
    auto barbattribs = getBarbAttribNames(name);
    if (name == HdTokens->points)
    {
        return makePrimvarOverlay(_pointsds, input_container);
    }
    else if (
            std::find(names.begin(), names.end(), barbattribs.first)
                    != names.end()
            && std::find(names.begin(), names.end(), barbattribs.second)
                       != names.end())
    {
        UT_ErrorLog::warning(
                "Expanding barb values from {} and {} to shaft primvar {}.",
                barbattribs.first,
                barbattribs.second,
                name.GetString());

        VtValue value = pv.GetPrimvarValue()->GetValue(0.0f);

        HdDataSourceBaseHandle handle;
        if (value.IsHolding<VtFloatArray>())
        {
            handle = HD_HairDeformPrimVarDataSource<float>::New(
                    _primpath, _primds, name, TfToken(barbattribs.first),
                    TfToken(barbattribs.second), 1, _cachemap);
        }
        else if (value.IsHolding<VtVec2fArray>())
        {
            handle = HD_HairDeformPrimVarDataSource<GfVec2f>::New(
                    _primpath, _primds, name, TfToken(barbattribs.first),
                    TfToken(barbattribs.second), 2, _cachemap);
        }
        else if (value.IsHolding<VtVec3fArray>())
        {
            handle = HD_HairDeformPrimVarDataSource<GfVec3f>::New(
                    _primpath, _primds, name, TfToken(barbattribs.first),
                    TfToken(barbattribs.second), 3, _cachemap);
        }
        else
        {
            UT_ErrorLog::warning(
                    "HairDeform: Shaft attribute {}'s type s not supported\n",
                    name.GetString());
            return input_container;
        }

        return makePrimvarOverlay(handle, input_container);
    }
    else if (name == HdTokens->widths)
    {
        // Only expand barb widths if P_barbl and P_barbr exist
        auto posbarbattribs = getBarbAttribNames(thePosName);
        if (!pvschema.GetPrimvar(TfToken(posbarbattribs.first))
            || !pvschema.GetPrimvar(TfToken(posbarbattribs.second)))
        {
            return input_container;
        }

        VtValue value = pv.GetPrimvarValue()->GetValue(0.0f);

        HdDataSourceBaseHandle handle;
        if (value.IsHolding<VtFloatArray>())
        {
            UT_ErrorLog::warning(
                    "HairDeform: Expanding barb widths from shaft widths.");

            handle = HD_HairDeformPrimVarDataSource<float>::New(
                    _primpath, _primds, name, TfToken(),
                    TfToken(), 1, _cachemap);

            return makePrimvarOverlay(handle, input_container);
        }

        return input_container;
    }
    else
    {
        return input_container;
    }
}

/*static*/ std::string
HD_HairDeformPrimVarsDataSource::getShaftAttribName(const std::string &name)
{
    static const std::regex pattern{R"((.*)_barb[lr])"};

    std::smatch match;
    if (std::regex_match(name, match, pattern))
    {
        TfToken shaftattrib;
        return match[1].str();
    }
    return std::string();
}

/*static*/ std::pair<std::string, std::string>
HD_HairDeformPrimVarsDataSource::getBarbAttribNames(const std::string &name)
{
    std::string _name(name);

    if (_name == HdTokens->points)
        _name = thePosName;
    else if (_name == theStName)
        _name = theUvName;
    else if (_name == HdTokens->widths)
        _name = theWidthName;
    else if (_name == HdTokens->displayColor)
        _name = theCdName;

    static const std::string theBarbLSuffix = "_barbl";
    static const std::string theBarbRSuffix = "_barbr";

    return std::make_pair(_name + theBarbLSuffix, _name + theBarbRSuffix);
}

HdOverlayContainerDataSourceHandle
HD_HairDeformPrimVarsDataSource::makePrimvarOverlay(
        HdDataSourceBaseHandle primvar_value,
        HdContainerDataSourceHandle container)
{
    std::vector<TfToken> names = {HdPrimvarSchemaTokens->primvarValue};
    std::vector<HdDataSourceBaseHandle> sources = {primvar_value};

    HdContainerDataSourceHandle handles[2] = {
            HdRetainedContainerDataSource::New(
                    names.size(), names.data(), sources.data()),
            container};
    return HdOverlayContainerDataSource::New(2, handles);
}

PXR_NAMESPACE_CLOSE_SCOPE
