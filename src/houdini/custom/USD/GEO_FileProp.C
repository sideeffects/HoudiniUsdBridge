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

