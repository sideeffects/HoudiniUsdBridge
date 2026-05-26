/*
 * Copyright 2019 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Produced by:
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#ifndef BRAY_HD_COLLECTION_TRACESET_SCENE_INDEX_PLUGIN_H
#define BRAY_HD_COLLECTION_TRACESET_SCENE_INDEX_PLUGIN_H

#include <pxr/pxr.h>
#include <pxr/imaging/hd/sceneIndexPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

/// \class BRAY_HdCollectionTracesetSceneIndexPlugin
///
/// Karma scene index plugin that adds custom traceset property based on
/// collection with "karma:traceset:" prefix
///
class BRAY_HdCollectionTracesetSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    BRAY_HdCollectionTracesetSceneIndexPlugin();

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

#endif // BRAY_HD_COLLECTION_TRACESET_SCENE_INDEX_PLUGIN_H
