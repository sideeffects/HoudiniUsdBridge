//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "HD_CoordSysCameraDataSceneIndex.h"
#include "HD_CoordSysPrimSceneIndexPlugin.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hdsi/coordSysPrimSceneIndex.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HD_CoordSysPrimSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HD_CoordSysPrimSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<HD_CoordSysPrimSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, HD_CoordSysPrimSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 900;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Houdini GL",
        _tokens->sceneIndexPluginName,
        nullptr,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtEnd);
    
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Karma CPU",
        _tokens->sceneIndexPluginName,
        nullptr,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtEnd);

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Karma XPU",
        _tokens->sceneIndexPluginName,
        nullptr,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtEnd);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

HD_CoordSysPrimSceneIndexPlugin::
    HD_CoordSysPrimSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HD_CoordSysPrimSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    TF_UNUSED(inputArgs);
    // NOTE: we are chaining *two* Scene Indexes into the pipeline here
    //       * HdsiCoordSysPrimSceneIndex
    //       * HD_CoordSysCameraDataSceneIndex
    return HD_CoordSysCameraDataSceneIndex::New(
        HdsiCoordSysPrimSceneIndex::New(inputScene));
}

PXR_NAMESPACE_CLOSE_SCOPE
