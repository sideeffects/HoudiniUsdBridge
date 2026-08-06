/*
 * Copyright 2025 Side Effects Software Inc.
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

#include "HD_HairDeformSceneIndex.h"

#include <pxr/imaging/hd/sceneIndexPlugin.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/pxr.h>

class HD_HairDeformSceneIndexPlugin;

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
        _tokens,
        ((sceneIndexPluginName, "HD_HairDeformSceneIndexPlugin")));

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, HD_HairDeformSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            "Karma CPU", _tokens->sceneIndexPluginName, {}, insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart);

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            "Karma XPU", _tokens->sceneIndexPluginName, {}, insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart);

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            "Houdini GL", _tokens->sceneIndexPluginName, {}, insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

class HD_HairDeformSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HD_HairDeformSceneIndexPlugin() = default;

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
            const HdSceneIndexBaseRefPtr &inputScene,
            const HdContainerDataSourceHandle &inputArgs) override
    {
        return HD_HairDeformSceneIndex::New(inputScene);
    }

    HdSceneIndexBaseRefPtr _AppendSceneIndex(
            const std::string &renderInstanceId,
            const HdSceneIndexBaseRefPtr &inputScene,
            const HdContainerDataSourceHandle &inputArgs) override
    {
        return _AppendSceneIndex(inputScene, inputArgs);
    }
};

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HD_HairDeformSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<HD_HairDeformSceneIndexPlugin>();
}

PXR_NAMESPACE_CLOSE_SCOPE
