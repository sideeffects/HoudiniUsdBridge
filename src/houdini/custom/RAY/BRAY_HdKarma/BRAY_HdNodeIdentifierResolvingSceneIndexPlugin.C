//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "BRAY_HdNodeIdentifierResolvingSceneIndexPlugin.h"
#include "BRAY_HdNodeIdentifierResolvingSceneIndex.h"

#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "BRAY_HdNodeIdentifierResolvingSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, BRAY_HdNodeIdentifierResolvingSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<
        BRAY_HdNodeIdentifierResolvingSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, BRAY_HdNodeIdentifierResolvingSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 40;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Karma CPU", _tokens->sceneIndexPluginName, nullptr,
        insertionPhase, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdSceneIndexBaseRefPtr
BRAY_HdNodeIdentifierResolvingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputSceneIndex,
    const HdContainerDataSourceHandle& inputArgs)
{
    TF_UNUSED(inputArgs);
    return BRAY_HdNodeIdentifierResolvingSceneIndex::New(inputSceneIndex);
}

PXR_NAMESPACE_CLOSE_SCOPE
