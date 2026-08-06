//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "HD_MeshLightSceneIndexPlugin.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "HD_MeshLightSceneIndex.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HD_MeshLightSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HD_MeshLightSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<HD_MeshLightSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, HD_MeshLightSceneIndexPlugin)
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

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Houdini GL",
        _tokens->sceneIndexPluginName,
        {},
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

///////////////////////////////////////////////////////////////////////////////

HD_MeshLightSceneIndexPlugin::
    HD_MeshLightSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HD_MeshLightSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return HD_MeshLightSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
