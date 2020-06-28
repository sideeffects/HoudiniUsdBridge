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

#ifndef __GEO_FILE_FORMAT_H__
#define __GEO_FILE_FORMAT_H__
 
#include "pxr/pxr.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/base/tf/staticTokens.h"
#include <iosfwd>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

#define GEO_FILE_FORMAT_TOKENS  \
    ((Id,      "geo"))		\
    ((Version, "1.0"))		\
    ((Target,  "usd"))

TF_DECLARE_PUBLIC_TOKENS(GEO_FileFormatTokens, GEO_FILE_FORMAT_TOKENS);
TF_DECLARE_WEAK_AND_REF_PTRS(GEO_FileFormat);
TF_DECLARE_WEAK_AND_REF_PTRS(SdfLayerBase);

/// \class GEO_FileFormat
///
class GEO_FileFormat : public SdfFileFormat
{
public:
    // SdfFileFormat overrides
    SdfAbstractDataRefPtr        InitData(const FileFormatArguments&)
                                        const override;
    bool                         CanRead(const std::string &file)
                                        const override;
    bool                         Read(SdfLayer* layer,
					const std::string& resolvedPath,
					bool metadataOnly) const override;
    bool                         WriteToFile(const SdfLayer& layer,
					const std::string& filePath,
					const std::string& comment =
					    std::string(),
					const FileFormatArguments& args =
					    FileFormatArguments())
                                        const override;
    bool                         ReadFromString(
					SdfLayer* layer,
					const std::string& str) const override;
    bool                         WriteToString(const SdfLayer& layer,
					std::string* str,
					const std::string& comment =
					    std::string()) const override;
    bool                         WriteToStream(const SdfSpecHandle &spec,
					std::ostream& out,
					size_t indent) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

				 GEO_FileFormat();
                                ~GEO_FileFormat() override;

private:
    SdfFileFormatConstPtr	myUsda;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_FILE_FORMAT_H__
