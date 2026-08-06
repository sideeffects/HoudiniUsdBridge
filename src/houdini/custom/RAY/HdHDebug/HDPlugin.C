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

#include "HDPlugin.h"
#include "HDDelegate.h"
#include "HDUtil.h"
#include <tools/henv.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/usdImaging/usdImaging/debugCodes.h>

PXR_NAMESPACE_OPEN_SCOPE        // [

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, HdHDebug)
{
    HdRendererPluginRegistry::Define<HdHDebug>();
}

HdRenderDelegate *
HdHDebug::CreateRenderDelegate()
{
    HDEBUG_TRACE_FUNCTION();
    HdRenderSettingsMap settingsMap;
    return new HDDelegate(settingsMap);
}

HdRenderDelegate *
HdHDebug::CreateRenderDelegate(HdRenderSettingsMap const& settingsMap)
{
    HDEBUG_TRACE_FUNCTION();
    return new HDDelegate(settingsMap);
}

void
HdHDebug::DeleteRenderDelegate(HdRenderDelegate *renderDelegate)
{
    HDEBUG_TRACE_FUNCTION();
    delete renderDelegate;
}

bool
HdHDebug::IsSupported(bool gpuEnabled) const
{
    HDEBUG_TRACE_FUNCTION();
    if (!HoudiniGetenv("HUSD_HDEBUG_DELEGATE"))
    {
        TF_DEBUG(USDIMAGING_PLUGINS).Msg(
            "Set HUSD_HDEBUG_DELEGATE to enable the debug client\n");
        return false;
    }
    return true;
}

bool
HdHDebug::IsSupported(HdRendererCreateArgs const &rendererCreateArgs,
                      std::string *reasonWhyNot) const
{
    HDEBUG_TRACE_FUNCTION();
    if (!HoudiniGetenv("HUSD_HDEBUG_DELEGATE"))
    {
        TF_DEBUG(USDIMAGING_PLUGINS).Msg(
            "Set HUSD_HDEBUG_DELEGATE to enable the debug client\n");
        return false;
    }
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE       // ]
