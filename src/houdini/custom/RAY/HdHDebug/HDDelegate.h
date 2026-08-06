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

#include <atomic>
#include <pxr/pxr.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>

PXR_NAMESPACE_OPEN_SCOPE        // [

class HDParam;

/// HdHDebug render delegate
class HDDelegate final : public HdRenderDelegate
{
public:
    static constexpr const char *class_name = "HDDelegate";

    HDDelegate(const HdRenderSettingsMap &seting);
    ~HDDelegate() override;

    /// Return render parameters
    HdRenderParam *GetRenderParam() const override;

    /// Return list of supported rprims
    const TfTokenVector &GetSupportedRprimTypes() const override;
    /// Return list of supported sprims
    const TfTokenVector &GetSupportedSprimTypes() const override;
    /// Return list of supported bprims
    const TfTokenVector &GetSupportedBprimTypes() const override;

    /// Returns the HdResourceRegistry instance
    HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    /// Update a renderer setting
    void SetRenderSetting(const TfToken &key, const VtValue &value) override;

    /// Return the descriptor for an AOV
    HdAovDescriptor GetDefaultAovDescriptor(const TfToken &name) const override;

    /// Return stats for rendering
    VtDictionary GetRenderStats() const override;

    /// Create a render pass
    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
            const HdRprimCollection & collection) override;
    /// @{
    /// Create/Destroy an instancer
    HdInstancer *CreateInstancer(HdSceneDelegate *sd,
            const SdfPath &id) override;
    void DestroyInstancer(HdInstancer *instancer) override;
    /// @}

    /// @{
    /// Create/destroy a primitive
    HdRprim *CreateRprim(const TfToken &typeId,
                         const SdfPath &rprimId) override;
    void DestroyRprim(HdRprim *prim) override;
    HdSprim *CreateSprim(const TfToken &typeId,
                         const SdfPath &rprimId) override;
    HdSprim *CreateFallbackSprim(const TfToken &typeId) override;
    void DestroySprim(HdSprim *prim) override;
    HdBprim *CreateBprim(const TfToken &typeId,
                         const SdfPath &rprimId) override;
    HdBprim *CreateFallbackBprim(const TfToken &typeId) override;
    void DestroyBprim(HdBprim *prim) override;
    /// @}

    /// Commit resources
    void CommitResources(HdChangeTracker *tracker) override;

    /// @{
    /// Material interface
    TfToken GetMaterialBindingPurpose() const override;
    TfTokenVector GetMaterialRenderContexts() const override;
    TfTokenVector GetShaderSourceTypes() const override;
    /// @}

    /// @{
    /// Pause/Resume interface
    bool IsPauseSupported() const override { return true; };
    bool Pause() override;
    bool Resume() override;
    /// @}
private:
    std::atomic<int>            myPaused = 0;
    HdRenderThread              myThread;
    std::unique_ptr<HDParam>    myParam;
};

PXR_NAMESPACE_CLOSE_SCOPE       // ]
