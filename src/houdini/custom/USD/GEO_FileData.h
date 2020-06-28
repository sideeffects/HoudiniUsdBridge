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

#ifndef __GEO_FILEDATA_H__
#define __GEO_FILEDATA_H__

#include "GEO_SceneDescriptionData.h"
#include "GEO_FilePrim.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_Array.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_WEAK_AND_REF_PTRS(GEO_FileData);

/// \class GEO_FileData
///
/// Provides an SdfAbstractData interface to Houdini geometry data.
///
class GEO_FileData : public GEO_SceneDescriptionData
{
public:
    /// Returns a new \c GEO_FileData object.  Without a successful
    /// \c Open() call, the data acts as if it contains a pseudo-root
    /// prim spec at the absolute root path.
    static GEO_FileDataRefPtr New(
	const SdfFileFormat::FileFormatArguments &args);

    /// Opens the Houdini geometry file at \p filePath read-only (closing any
    /// open file).  Houdini geometry is not meant to be used as an in-memory
    /// store for editing so methods that modify the file are not supported.
    bool Open(const std::string &filePath) override;

protected:
			 GEO_FileData();
                        ~GEO_FileData() override;

private:
    GEO_FilePrim			*myLayerInfoPrim;
    SdfFileFormat::FileFormatArguments	 myCookArgs;
    bool				 mySaveSampleFrame;

    friend class GEO_FilePrim;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_FILEDATA_H__
