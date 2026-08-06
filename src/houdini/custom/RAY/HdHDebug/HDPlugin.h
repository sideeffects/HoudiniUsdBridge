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

#pragma once

#include <pxr/pxr.h>
#include <pxr/imaging/hd/rendererPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE        // [

/// Defines the Hydra Houdini Debug plugin (HdHDebug)
class HdHDebug final : public HdRendererPlugin
{
public:
    static constexpr const char *class_name = "HdHDebug";

    HdHDebug() = default;
    ~HdHDebug() override = default;

    /// @{
    /// Create delegate instance
    HdRenderDelegate *CreateRenderDelegate() override;
    HdRenderDelegate *CreateRenderDelegate(
	    HdRenderSettingsMap const& settingsMap) override;
    /// @}

    /// Destroy a delegate
    void DeleteRenderDelegate(HdRenderDelegate *renderDelegate) override;

    /// Check to see if it's supported
    using HdRendererPlugin::IsSupported;
    bool IsSupported(HdRendererCreateArgs const &rendererCreateArgs,
                     std::string *reasonWhyNot=nullptr) const override;
    bool IsSupported(bool gpuEnabled = true) const override;

private:
    // This class does not support copying.
    HdHDebug(const HdHDebug&)             = delete;
    HdHDebug &operator =(const HdHDebug&) = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE       // ]
