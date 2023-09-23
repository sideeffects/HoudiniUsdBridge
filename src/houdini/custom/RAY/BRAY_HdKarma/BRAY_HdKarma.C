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

#include "BRAY_HdKarma.h"
#include "BRAY_HdFormat.h"

#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include "BRAY_HdDelegate.h"
#include <BRAY/BRAY_Interface.h>
#include <tools/henv.h>
#include <UT/UT_Debug.h>
#include <UT/UT_ErrorLog.h>

namespace
{
    static bool
    isXPUSupported()
    {
        return BRAY::ScenePtr::isEngineSupported("xpu");
    }
}

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdKarmaXPU final : public BRAY_HdKarma
{
public:
    BRAY_HdKarmaXPU() = default;
    ~BRAY_HdKarmaXPU() override = default;
    bool        isXPU() const override { return true; }
};

// Register the plugin with the renderer plugin system.
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, BRAY_BRAY_HdKarma)
{
    HdRendererPluginRegistry::Define<BRAY_HdKarma>();
    HdRendererPluginRegistry::Define<BRAY_HdKarmaXPU>();
}

HdRenderDelegate *
BRAY_HdKarma::CreateRenderDelegate()
{
    if (isXPU() && !isXPUSupported())
        return nullptr;

    HdRenderSettingsMap	renderSettings;
    return new BRAY_HdDelegate(renderSettings, isXPU());
}

HdRenderDelegate *
BRAY_HdKarma::CreateRenderDelegate(HdRenderSettingsMap const& settingsMap)
{
    if (isXPU() && !isXPUSupported())
        return nullptr;

    return new BRAY_HdDelegate(settingsMap, isXPU());
}

void
BRAY_HdKarma::DeleteRenderDelegate(HdRenderDelegate *renderDelegate)
{
    delete renderDelegate;
}

bool
BRAY_HdKarma::IsSupported(bool gpuEnabled) const
{
    // Nothing more to check for now, we assume if the plugin loads correctly
    // it is supported.
    if (isXPU() && !BRAY::ScenePtr::isEngineSupported("xpu"))
    {
        UT_ErrorLog::errorOnce("Karma XPU delegate not supported on this machine");
        return false;
    }
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
