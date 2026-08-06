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

#ifndef __HD_HdGpSopEngine__
#define __HD_HdGpSopEngine__

#include "HD_HdGpScatterEngine.h"

#include <GU/GU_DetailHandle.h>

PXR_NAMESPACE_OPEN_SCOPE

class HD_HdGpSopScatterEngine : public HD_HdGpScatterEngine
{
public:
    void loadGraph(const std::string &graphpath) override;
    VtVec3fArray cook(
        const HdSceneIndexPrim &prim,
        const UT_Options &overrides) override;

private:
    GU_ConstDetailHandle myGraph;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
