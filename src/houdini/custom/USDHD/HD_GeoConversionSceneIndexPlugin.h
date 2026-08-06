//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#ifndef USD_HD_GEO_CONVERSION_SCENE_INDEX_PLUGIN_H
#define USD_HD_GEO_CONVERSION_SCENE_INDEX_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HD_GeoConversionSceneIndexPlugin
///
/// Houdini viewport scene index plugin that configures and instantiates various
/// scene indexes to handle (i.e., convert) unsupported Hydra geometry types.
///
/// At present this includes:
/// * HdsiNurbsApproximatingSceneIndex
/// * HdsiTetMeshConversionSceneIndex
/// * HdsiImplicitSurfaceSceneIndex
///
class HD_GeoConversionSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HD_GeoConversionSceneIndexPlugin();

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

#endif // USD_HD_GEO_CONVERSION_SCENE_INDEX_PLUGIN_H
