// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

//------------------------------------------------------------------------------
// NOTE: This draws inspiration from similar work for Storm & HdPrman:
//       * pxr/imaging/hdSt/dependencySceneIndexPlugin
//       * third_party/renderman-26/plugin/hdPrman/dependencySceneIndexPlugin
//       As such, we should keep an eye on the upstream implementations in case
//       there are important and relevant changes in their approach.
// NOTE: There is a Karma version of this Scene Index in BRAY_HdKarma
//------------------------------------------------------------------------------

#pragma once

#include "pxr/pxr.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HD_DependencySceneIndexPlugin
///
/// This Scene Index introduces dependencies between data source locators
/// so that when one dirties the other dirties too (ultimately the point
/// being that the second dirtying is what HoudiniGL/VK is expecting)
/// 
class HD_DependencySceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HD_DependencySceneIndexPlugin() = default;
    ~HD_DependencySceneIndexPlugin() override = default;

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
