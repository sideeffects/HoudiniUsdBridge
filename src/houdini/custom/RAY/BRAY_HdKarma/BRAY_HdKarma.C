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

#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include "BRAY_HdDelegate.h"
#include <tools/henv.h>
#include <UT/UT_Debug.h>

PXR_NAMESPACE_OPEN_SCOPE

// Register the plugin with the renderer plugin system.
TF_REGISTRY_FUNCTION_WITH_TAG(TfType, BRAY_BRAY_HdKarma)
{
    HdRendererPluginRegistry::Define<BRAY_HdKarma>();
}

HdRenderDelegate *
BRAY_HdKarma::CreateRenderDelegate()
{
    HdRenderSettingsMap	renderSettings;
    return new BRAY_HdDelegate(renderSettings);
}

HdRenderDelegate *
BRAY_HdKarma::CreateRenderDelegate(HdRenderSettingsMap const& settingsMap)
{
    return new BRAY_HdDelegate(settingsMap);
}

void
BRAY_HdKarma::DeleteRenderDelegate(HdRenderDelegate *renderDelegate)
{
    delete renderDelegate;
}

bool
BRAY_HdKarma::IsSupported() const
{
    // Nothing more to check for now, we assume if the plugin loads correctly
    // it is supported.
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
