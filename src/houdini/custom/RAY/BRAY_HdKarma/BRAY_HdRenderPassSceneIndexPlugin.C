//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "BRAY_HdRenderPassSceneIndexPlugin.h"
#include "BRAY_HdRenderPassSceneIndex.h"
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "BRAY_HdRenderPassSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, BRAY_HdRenderPassSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<BRAY_HdRenderPassSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, BRAY_HdRenderPassSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Karma CPU",
        _tokens->sceneIndexPluginName,
        {},
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Karma XPU",
        _tokens->sceneIndexPluginName,
        {},
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

///////////////////////////////////////////////////////////////////////////////

BRAY_HdRenderPassSceneIndexPlugin::
    BRAY_HdRenderPassSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
BRAY_HdRenderPassSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return BRAY_HdRenderPassSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
