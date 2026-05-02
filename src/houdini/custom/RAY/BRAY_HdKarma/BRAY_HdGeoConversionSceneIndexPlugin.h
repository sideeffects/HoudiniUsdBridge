//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#ifndef BRAY_HD_GEO_CONVERSION_SCENE_INDEX_PLUGIN_H
#define BRAY_HD_GEO_CONVERSION_SCENE_INDEX_PLUGIN_H

#include <pxr/pxr.h>
#include <pxr/imaging/hd/sceneIndexPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

/// \class BRAY_HdGeoConversionSceneIndexPlugin
///
/// Karma scene index plugin that configures and instantiates various scene
/// indexes to handle (i.e., convert) unsupported Hydra geometry types.
///
/// At present this includes:
/// * HdsiNurbsApproximatingSceneIndex
/// * HdsiTetMeshConversionSceneIndex
/// * HdsiImplicitSurfaceSceneIndex
///
class BRAY_HdGeoConversionSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    BRAY_HdGeoConversionSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const std::string &renderInstanceId,
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override
    { return _AppendSceneIndex(inputScene, inputArgs); }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // BRAY_HD_GEO_CONVERSION_SCENE_INDEX_PLUGIN_H
