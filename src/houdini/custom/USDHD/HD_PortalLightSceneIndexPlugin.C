//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "HD_PortalLightSceneIndexPlugin.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "HD_PortalLightSceneIndex.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HD_PortalLightSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HD_PortalLightSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<HD_PortalLightSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, HD_PortalLightSceneIndexPlugin)
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

HD_PortalLightSceneIndexPlugin::
    HD_PortalLightSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HD_PortalLightSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return HD_PortalLightSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
