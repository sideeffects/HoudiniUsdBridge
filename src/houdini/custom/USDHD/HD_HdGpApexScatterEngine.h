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

#ifndef __HD_HdGpApexEngine__
#define __HD_HdGpApexEngine__

#include "HD_HdGpScatterEngine.h"

#include <APEX/APEX_Graph.h>

PXR_NAMESPACE_OPEN_SCOPE

class HD_HdGpApexScatterEngine : public HD_HdGpScatterEngine
{
public:
    ~HD_HdGpApexScatterEngine() override = default;

protected:
    void loadGraphImpl(const std::string &path) override;
    void setParmsImpl(const UT_Options &parms) override;
    Result cookImpl(const HdSceneIndexPrim &prim) override;

private:
    apex::APEX_Graph myGraph;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
