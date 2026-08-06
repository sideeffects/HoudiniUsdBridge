//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "HD_DependencyForwardingSceneIndexPlugin.h"

#include "pxr/imaging/hd/dependencyForwardingSceneIndex.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HD_DependencyForwardingSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HD_DependencyForwardingSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<
        HD_DependencyForwardingSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, HD_DependencyForwardingSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 1000;

    for(auto &&pluginDisplayName : { "Karma CPU", "Karma XPU", "Houdini GL" })
    {
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            pluginDisplayName,
            _tokens->sceneIndexPluginName,
            nullptr,
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtEnd);
    }
}

HD_DependencyForwardingSceneIndexPlugin::
HD_DependencyForwardingSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HD_DependencyForwardingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return HdDependencyForwardingSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
