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


#ifndef __GEO_DYNAMIC_FILE_FORMAT_H__
#define __GEO_DYNAMIC_FILE_FORMAT_H__

#include "pxr/pxr.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/base/tf/staticTokens.h"
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

/// \class GEO_HDAFileFormat
///
class GEO_HDAFileFormat : public SdfFileFormat
{
public:
    // SdfFileFormat Overrides
    virtual bool CanRead(const std::string &file) const override;
    virtual bool Read(SdfLayer *layer,
                      const std::string &resolvedPath,
                      bool metadataOnly) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

    GEO_HDAFileFormat();
    virtual ~GEO_HDAFileFormat();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif //__GEO_DYNAMIC_FILE_FORMAT_H__