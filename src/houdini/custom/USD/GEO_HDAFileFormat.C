/*
 * Copyright 2020 Side Effects Software Inc.
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

#include "GEO_HAPIUtils.h"
#include "GEO_HDAFileData.h"
#include "GEO_HDAFileFormat.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/trace/trace.h"
#include "pxr/usd/pcp/dynamicFileFormatContext.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/usdaFileFormat.h"
#include <UT/UT_WorkBuffer.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_HDAFileFormatTokens, GEO_HDA_FILE_FORMAT_TOKENS);

// This must match the name of the SdfMetadata dict specified in the
// pluginfo.json file
static const TfToken theParamDictToken("HDAParms");

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, GEO_GEO_HDAFileFormat)
{
    SDF_DEFINE_FILE_FORMAT(GEO_HDAFileFormat, SdfFileFormat);
}

GEO_HDAFileFormat::GEO_HDAFileFormat()
    : SdfFileFormat(GEO_HDAFileFormatTokens->Id,      // id
                    GEO_HDAFileFormatTokens->Version, // version
                    GEO_HDAFileFormatTokens->Target,  // target
                    GEO_HDAFileFormatTokens->Id)      // extension
{
}

GEO_HDAFileFormat::~GEO_HDAFileFormat() {}

bool
GEO_HDAFileFormat::CanRead(const std::string &filePath) const
{
    return (TfGetExtension(filePath) == "hda" ||
            TfGetExtension(filePath) == "otl");
}

bool
GEO_HDAFileFormat::Read(SdfLayer *layer,
                        const std::string &resolvedPath,
                        bool metadataOnly) const
{
    SdfAbstractDataRefPtr data =
        GEO_HDAFileData::New(layer->GetFileFormatArguments());
    GEO_HDAFileDataRefPtr geoData = TfStatic_cast<GEO_HDAFileDataRefPtr>(data);

    bool open_success = true;
    UTisolate([&]() {
        if (!geoData->Open(resolvedPath))
        {
            open_success = false;
        }
    });
    if (!open_success)
        return false;

    _SetLayerData(layer, data);
    return true;
}

bool
GEO_HDAFileFormat::CanFieldChangeAffectFileFormatArguments(
    const TfToken &field,
    const VtValue &oldValue,
    const VtValue &newValue,
    const VtValue &dependencyContextData) const
{
    // The only dynamic field is the dict describing parameter values. If this
    // changes, the format arguments will change as well.
    return true;
}

// Add a numerical entry to args based on the type and value of parmData.
// Nothing happens if the data is not numeric
//
// The added key will follow the form: "PREFIX TUPLE_INDEX NAME"
// Elements of GtVec will be space separated
//
// parmData is passed by value because VtValue.Cast() will mutate it.
static void
addNumericToFileFormatArguments(SdfFileFormat::FileFormatArguments *args,
                                const std::string &parmName,
                                VtValue parmData,
                                UT_WorkBuffer &parmBuf,
                                UT_WorkBuffer &valBuf)
{
    auto addToArgs = [&](const double *data, int count) {
        // Add the prefix to the parameter's name
        parmBuf.sprintf("%s%s", GEO_HDA_PARM_NUMERIC_PREFIX, parmName.c_str());

        // Add each element to the buffer
        valBuf.sprintf("%f", data[0]);
        for (int i = 1; i < count; i++)
        {
            valBuf.appendSprintf("%s%f", GEO_HDA_PARM_SEPARATOR, data[i]);
        }

        // Add the key/value pair to args
        (*args)[parmBuf.toStdString()] = valBuf.toStdString();
    };

    // Try casting to double to ensure the VtValue is numeric

    if (parmData.CanCast<double>())
    {
        const double &val = parmData.Cast<double>().UncheckedGet<double>();
        addToArgs(&val, 1);
    }
    else if (parmData.CanCast<GfVec2d>())
    {
        const GfVec2d &vec = parmData.Cast<GfVec2d>().UncheckedGet<GfVec2d>();
        const double *vals = vec.GetArray();
        addToArgs(vals, 2);
    }
    else if (parmData.CanCast<GfVec3d>())
    {
        const GfVec3d &vec = parmData.Cast<GfVec3d>().UncheckedGet<GfVec3d>();
        const double *vals = vec.GetArray();
        addToArgs(vals, 3);
    }
    else if (parmData.CanCast<GfVec4d>())
    {
        const GfVec4d &vec = parmData.Cast<GfVec4d>().UncheckedGet<GfVec4d>();
        const double *vals = vec.GetArray();
        addToArgs(vals, 4);
    }
}

// All of the metadata fields for this dynamic format must be defined in the
// coressponding pluginfo.json file. Since HDAs can have arbitrary parameters,
// we use a single dict to store the parameter names, types, and values.
void
GEO_HDAFileFormat::ComposeFieldsForFileFormatArguments(
    const std::string &assetPath,
    const PcpDynamicFileFormatContext &context,
    FileFormatArguments *args,
    VtValue *dependencyContextData) const
{
    VtValue paramDictVal;
    if (context.ComposeValue(theParamDictToken, &paramDictVal) &&
        paramDictVal.IsHolding<VtDictionary>())
    {
        // Add each parameter in this dict to args
        VtDictionary paramDict = paramDictVal.UncheckedGet<VtDictionary>();

        UT_WorkBuffer parmBuf;
        UT_WorkBuffer valBuf;
        VtDictionary::iterator end = paramDict.end();
        for (VtDictionary::iterator it = paramDict.begin(); it != end; it++)
        {
            const std::string &parmName = it->first;
            VtValue &data = it->second;

            // Check if we have a string arg
            if (data.IsHolding<std::string>())
            {
                const std::string &strData = data.UncheckedGet<std::string>();

                // Add string type to args
                parmBuf.sprintf(
                    "%s%s", GEO_HDA_PARM_STRING_PREFIX, parmName.c_str());
                (*args)[parmBuf.toStdString()] = strData;
            }
            else
            {
                addNumericToFileFormatArguments(
                    args, parmName, data, parmBuf, valBuf);
            }
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
