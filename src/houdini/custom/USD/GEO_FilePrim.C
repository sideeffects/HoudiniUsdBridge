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

#include "GEO_FilePrim.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_FilePrimTokens, GEO_FILE_PRIM_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(GEO_FilePrimTypeTokens, GEO_FILE_PRIM_TYPE_TOKENS);

GEO_FilePrim::GEO_FilePrim()
    : myInitialized(false),
      myIsDefined(true)
{
}

GEO_FilePrim::~GEO_FilePrim()
{
}

const GEO_FileProp *
GEO_FilePrim::getProp(const SdfPath& id) const
{
    if (id.IsPropertyPath())
    {
	auto it = myProps.find(id.GetNameToken());

	if (it != myProps.end())
	    return &it->second;
    }

    return nullptr;
}

void
GEO_FilePrim::addChild(const TfToken &child_name)
{
    myChildNames.push_back(child_name);
}

GEO_FileProp *
GEO_FilePrim::addProperty(const TfToken &prop_name,
	const SdfValueTypeName &type_name,
	GEO_FilePropSource *prop_source)
{
    myPropNames.push_back(prop_name);
    auto it = myProps.emplace(prop_name, GEO_FileProp(type_name, prop_source));
    return &it.first->second;
}

GEO_FileProp *
GEO_FilePrim::addRelationship(const TfToken &prop_name,
	const SdfPathVector &targets)
{
    GEO_FilePropSource *prop_source;
    SdfPathListOp path_list;

    myPropNames.push_back(prop_name);
    path_list.SetAppendedItems(targets);
    prop_source = new GEO_FilePropConstantSource<SdfPathListOp>(path_list);
    auto it = myProps.emplace(prop_name,
	GEO_FileProp(SdfValueTypeName(), prop_source));
    it.first->second.setIsRelationship(true);
    return &it.first->second;
}

void
GEO_FilePrim::addMetadata(const TfToken &key, const VtValue &value)
{
    myMetadata.emplace(key, value);
}

void
GEO_FilePrim::replaceMetadata(const TfToken &key, const VtValue &value)
{
    myMetadata[key] = value;
}

void
GEO_FilePrim::addCustomData(const TfToken &key, const VtValue &value)
{
    myCustomData.emplace(key, value);
}

PXR_NAMESPACE_CLOSE_SCOPE

