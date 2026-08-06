//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#pragma once

#include "pxr/pxr.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HD_CoordSysPrimSceneIndexPlugin
///
/// Houdini scene index plugin that adds coordinate system prims.
///
class HD_CoordSysPrimSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HD_CoordSysPrimSceneIndexPlugin();

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
