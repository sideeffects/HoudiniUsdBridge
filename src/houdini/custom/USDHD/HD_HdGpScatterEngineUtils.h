/*
 * Copyright 2025 Side Effects Software Inc.
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
 */

#ifndef __HD_HdGpEngineUtils__
#define __HD_HdGpEngineUtils__

#include "HD_HdGpScatterEngine.h"

#include <GU/GU_DetailHandle.h>
#include <UT/UT_Options.h>

#include <pxr/pxr.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/sceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

GU_ConstDetailHandle
HD_HdGpMakeGuDetailFromMesh(const HdSceneIndexPrim &prim);

GU_ConstDetailHandle
HD_HdGpMakeGuDetailFromOptions(const UT_Options &opts);

HD_HdGpScatterEngine::Result
HD_HdGpExtractResultFromGuDetail(GU_ConstDetailHandle gdh);

GU_ConstDetailHandle
HD_HdGpLoadGraph(const std::string &graph);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
