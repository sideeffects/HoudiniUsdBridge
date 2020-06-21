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

#ifndef HDKARMA_RENDERER_PLUGIN_H
#define HDKARMA_RENDERER_PLUGIN_H

#include <pxr/pxr.h>
#include <pxr/imaging/hd/rendererPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

///
/// \class BRAY_HdKarma
///
/// A registered child of HdRendererPlugin, this is the class that gets
/// loaded when a hydra application asks to draw with a certain renderer.
/// It supports rendering via creation/destruction of renderer-specific
/// classes. The render delegate is the hydra-facing entrypoint into the
/// renderer; it's responsible for creating specialized implementations of hydra
/// prims (which translate scene data into drawable representations) and hydra
/// renderpasses (which draw the scene to the framebuffer).
///
class BRAY_HdKarma final : public HdRendererPlugin
{
public:
    BRAY_HdKarma() = default;
    ~BRAY_HdKarma() override = default;

    /// Construct a new render delegate of type BRAY_HdDelegate.
    /// Karma render delegates own the scene object, so a new render
    /// delegate should be created for each instance of HdRenderIndex.
    ///   \return A new BRAY_HdDelegate object.
    HdRenderDelegate *CreateRenderDelegate() override;

    /// Construct a new render delegate of type BRAY_HdDelegate,
    /// with the settings specified in the settingsMap.
    HdRenderDelegate *CreateRenderDelegate(
	    HdRenderSettingsMap const& settingsMap) override;

    /// Destroy a render delegate created by this class's CreateRenderDelegate.
    ///   \param renderDelegate The render delegate to delete.
    void DeleteRenderDelegate(
        HdRenderDelegate *renderDelegate) override;

    /// Checks to see if the embree plugin is supported on the running system
    ///
    bool IsSupported() const override;

private:
    // This class does not support copying.
    BRAY_HdKarma(const BRAY_HdKarma&)             = delete;
    BRAY_HdKarma &operator =(const BRAY_HdKarma&) = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDKARMA_RENDERER_PLUGIN_H
