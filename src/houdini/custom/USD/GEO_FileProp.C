//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "GEO_FileProp.h"
#include "GEO_FilePropSource.h"
#include "GEO_FileFieldValue.h"
#include <GU/GU_DetailHandle.h>
#include <GU/GU_Detail.h>
#include <pxr/base/vt/array.h>

PXR_NAMESPACE_OPEN_SCOPE

GEO_FileProp::GEO_FileProp(const SdfValueTypeName &type_name,
	GEO_FilePropSource *prop_source)
    : myTypeName(type_name),
      myPropSource(prop_source),
      myValueIsDefault(false),
      myIsRelationship(false),
      myValueIsUniform(false)
{
}

GEO_FileProp::~GEO_FileProp()
{
}

bool
GEO_FileProp::copyData(const GEO_FileFieldValue &value) const
{
    return myPropSource->copyData(value);
}

void
GEO_FileProp::addMetadata(const TfToken &key, const VtValue &value)
{
    myMetadata.emplace(key, value);
}

void
GEO_FileProp::addCustomData(const TfToken &key, const VtValue &value)
{
    myCustomData.emplace(key, value);
}

PXR_NAMESPACE_CLOSE_SCOPE

