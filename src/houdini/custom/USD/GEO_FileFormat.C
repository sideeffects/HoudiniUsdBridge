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
 */

#include "GEO_FileFormat.h"
#include "GEO_FileData.h"
#include <GU/GU_Detail.h>
#include <UT/UT_ParallelUtil.h>
#include "pxr/usd/usd/usdaFileFormat.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/staticData.h"
#include <ostream>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_FileFormatTokens, GEO_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(GEO_FileFormat, SdfFileFormat);
}

GEO_FileFormat::GEO_FileFormat()
    : SdfFileFormat(
        GEO_FileFormatTokens->Id,
        GEO_FileFormatTokens->Version,
        GEO_FileFormatTokens->Target,
        GEO_FileFormatTokens->Id),
    myUsda(SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id))
{
}

GEO_FileFormat::~GEO_FileFormat()
{
}

SdfAbstractDataRefPtr
GEO_FileFormat::InitData(const FileFormatArguments& args) const
{
    return GEO_FileData::New(args);
}

bool
GEO_FileFormat::CanRead(const std::string& filePath) const
{
    if (TfGetExtension(filePath) == "sop")
	return true;

    return GU_Detail::isFormatSupported(filePath.c_str());
}

bool
GEO_FileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    SdfAbstractDataRefPtr data = InitData(layer->GetFileFormatArguments());
    GEO_FileDataRefPtr geoData = TfStatic_cast<GEO_FileDataRefPtr>(data);

    // This function will be called from a TBB task when composing a stage.
    // While calling this Read method, the SdfLayer::_initializationMutex is
    // locked.
    //
    // The Open operation below spawns subtasks (filling GT arrays, for
    // example). If this operation ends up waiting, it may invoke another
    // task that is part of the stage composition, which may try to open
    // this layer again. At which point on this thread we will try to lock
    // the _initializationMutex again, which will raise an exception.
    //
    // So we isolate this thread to ensure that no tasks outside this scope
    // will be invoked on this thread.
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

bool
GEO_FileFormat::WriteToFile(
    const SdfLayer& layer,
    const std::string& filePath,
    const std::string& comment,
    const FileFormatArguments& args) const
{
    return false;
}

bool 
GEO_FileFormat::ReadFromString(
    SdfLayer* layer,
    const std::string& str) const
{
    // XXX: For now, defer to the usda file format for this. May need to
    //      revisit this as the alembic reader gets fully fleshed out.
    return myUsda->ReadFromString(layer, str);
}

bool 
GEO_FileFormat::WriteToString(
    const SdfLayer& layer,
    std::string* str,
    const std::string& comment) const
{
    // XXX: For now, defer to the usda file format for this. May need to
    //      revisit this as the alembic reader gets fully fleshed out.
    return myUsda->WriteToString(layer, str, comment);
}

bool
GEO_FileFormat::WriteToStream(
    const SdfSpecHandle &spec,
    std::ostream& out,
    size_t indent) const
{
    // XXX: Because WriteToString() uses the usda file format and because
    //      a spec will always use it's own file format for writing we'll
    //      get here trying to write an Alembic layer as usda.  So we
    //      turn around call usda.
    return myUsda->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
