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

#ifndef __GEO_HDA_FILE_FORMAT_H__
#define __GEO_HDA_FILE_FORMAT_H__

#include "GEO_HDAFileData.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/pxr.h"
#include "pxr/usd/pcp/dynamicFileFormatInterface.h"
#include "pxr/usd/sdf/fileFormat.h"
#include <string>
#include <UT/UT_UniquePtr.h>

PXR_NAMESPACE_OPEN_SCOPE

#define GEO_HDA_FILE_FORMAT_TOKENS                                             \
    ((Id, "hda"))((Version, "1.0"))((Target, "usd"))

TF_DECLARE_PUBLIC_TOKENS(GEO_HDAFileFormatTokens, GEO_HDA_FILE_FORMAT_TOKENS);
TF_DECLARE_WEAK_AND_REF_PTRS(GEO_HDAFileFormat);

/// \class GEO_HDAFileFormat
///
class GEO_HDAFileFormat : public SdfFileFormat,
                          public PcpDynamicFileFormatInterface
{
public:
    // SdfFileFormat Overrides
    virtual bool CanRead(const std::string &file) const override;
    virtual bool Read(SdfLayer *layer,
                      const std::string &resolvedPath,
                      bool metadataOnly) const override;

    // PcpDynamicFileFormatInterface Overrides
    virtual void ComposeFieldsForFileFormatArguments(
        const std::string &assetPath,
        const PcpDynamicFileFormatContext &context,
        FileFormatArguments *args,
        VtValue *dependencyContextData) const override;
    virtual bool CanFieldChangeAffectFileFormatArguments(
        const TfToken &field,
        const VtValue &oldValue,
        const VtValue &newValue,
        const VtValue &dependencyContextData) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

    GEO_HDAFileFormat();
    virtual ~GEO_HDAFileFormat();

private:
    // Cache for file data
    UT_UniquePtr<GEO_HAPIReaderCache> myReadersCache;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif //__GEO_HDA_FILE_FORMAT_H__