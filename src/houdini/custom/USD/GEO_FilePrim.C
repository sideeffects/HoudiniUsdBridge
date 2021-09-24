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

#include "GEO_FilePrim.h"

#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/usdLux/light.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_FilePrimTokens, GEO_FILE_PRIM_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(GEO_FilePrimTypeTokens, GEO_FILE_PRIM_TYPE_TOKENS);

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

bool
GEO_FilePrim::isLightType() const
{
    TfType type = UsdSchemaRegistry::GetConcreteTypeFromSchemaTypeName(myTypeName);
    // TODO: watch for possible breaking changes in USD light identification
    //       (https://graphics.pixar.com/usd/docs/Adapting-UsdLux-to-Accommodate-Geometry-Lights.html)
    return type.IsA<UsdLuxLight>();
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
    auto it = myProps.try_emplace(prop_name, type_name, prop_source);
    if (it.second) // Newly inserted
        myPropNames.push_back(prop_name);
    else // Already exists - overwrite.
        it.first->second = GEO_FileProp(type_name, prop_source);

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

