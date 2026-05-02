//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "BRAY_HdGeoConversionSceneIndexPlugin.h"
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdsi/implicitSurfaceSceneIndex.h>
#include <pxr/imaging/hdsi/nurbsApproximatingSceneIndex.h>
#include <pxr/imaging/hdsi/tetMeshConversionSceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "BRAY_HdGeoConversionSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, BRAY_HdGeoConversionSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<BRAY_HdGeoConversionSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, BRAY_HdGeoConversionSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    // Configure the scene index to generate the mesh for each of the implicit
    // primitives since Storm doesn't natively support any.
    HdDataSourceBaseHandle const toMeshSrc =
        HdRetainedTypedSampledDataSource<TfToken>::New(
            HdsiImplicitSurfaceSceneIndexTokens->toMesh);

    HdContainerDataSourceHandle const cpuInputArgs =
        HdRetainedContainerDataSource::New(
            HdPrimTypeTokens->sphere, toMeshSrc,
            HdPrimTypeTokens->cube, toMeshSrc,
            HdPrimTypeTokens->cone, toMeshSrc,
            HdPrimTypeTokens->cylinder, toMeshSrc,
            HdPrimTypeTokens->capsule, toMeshSrc,
            HdPrimTypeTokens->plane, toMeshSrc);

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Karma CPU",
        _tokens->sceneIndexPluginName,
        cpuInputArgs,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);

    HdContainerDataSourceHandle const xpuInputArgs =
        HdRetainedContainerDataSource::New(
            HdPrimTypeTokens->sphere, toMeshSrc,
            HdPrimTypeTokens->cube, toMeshSrc,
            HdPrimTypeTokens->cone, toMeshSrc,
            HdPrimTypeTokens->cylinder, toMeshSrc,
            HdPrimTypeTokens->capsule, toMeshSrc,
            HdPrimTypeTokens->plane, toMeshSrc);

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Karma XPU",
        _tokens->sceneIndexPluginName,
        xpuInputArgs,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

BRAY_HdGeoConversionSceneIndexPlugin::
    BRAY_HdGeoConversionSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
BRAY_HdGeoConversionSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return HdsiNurbsApproximatingSceneIndex::New(
               HdsiTetMeshConversionSceneIndex::New(
                   HdsiImplicitSurfaceSceneIndex::New(inputScene, inputArgs)));
}

PXR_NAMESPACE_CLOSE_SCOPE
