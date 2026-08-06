//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hdsi/lightLinkingSceneIndex.h"

#include "HD_LightLinkingSceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HD_LightLinkingSceneIndexPlugin"))
);

////////////////////////////////////////////////////////////////////////////////
// Plugin registration
////////////////////////////////////////////////////////////////////////////////

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HD_LightLinkingSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<HD_LightLinkingSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, HD_LightLinkingSceneIndexPlugin)
{
    // XXX Picking an arbitrary phase for now. If a procedural were to
    //     generate light prims, we'd want this to be after it.
    //
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 50;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Houdini GL",
        _tokens->sceneIndexPluginName,
        // XXX Update inputArgs to provide the list of geometry types
        //     supported by the delegate.
        nullptr, 
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Karma CPU",
        _tokens->sceneIndexPluginName,
        // XXX Update inputArgs to provide the list of geometry types
        //     supported by the delegate.
        nullptr, 
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Karma XPU",
        _tokens->sceneIndexPluginName,
        // XXX Update inputArgs to provide the list of geometry types
        //     supported by the delegate.
        nullptr, 
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

////////////////////////////////////////////////////////////////////////////////
// Scene Index Implementation
////////////////////////////////////////////////////////////////////////////////

HdSceneIndexBaseRefPtr HD_LightLinkingSceneIndexPlugin::_AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs)
{
    return HdsiLightLinkingSceneIndex::New(inputScene, inputArgs);
}

PXR_NAMESPACE_CLOSE_SCOPE
