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

#include "GEO_HDAFileFormat.h"
#include "GEO_HDAFileData.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/trace/trace.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/usdaFileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(GEO_HDAFileFormat, SdfFileFormat);
}

GEO_HDAFileFormat::GEO_HDAFileFormat()
    : SdfFileFormat(TfToken("hda"),		      // id
                    TfToken("1.0"),                   // version
                    TfToken("usd"),                   // target
                    "hda")                            // extension
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
    SdfAbstractDataRefPtr data = GEO_HDAFileData::New(layer->GetFileFormatArguments());
    GEO_HDAFileDataRefPtr geoData = TfStatic_cast<GEO_HDAFileDataRefPtr>(data);

    bool    open_success = true;
    UTisolate([&]()
    {
        if (!geoData->Open(resolvedPath)) {
            open_success = false;
        }
    });
    if (!open_success)
        return false;

    _SetLayerData(layer, data);
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE