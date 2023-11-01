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

#ifndef HDKARMA_RENDER_DELEGATE_H
#define HDKARMA_RENDER_DELEGATE_H

#include <pxr/pxr.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>
#include <mutex>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_StopWatch.h>
#include <BRAY/BRAY_Interface.h>
#include "BRAY_HdParam.h"

PXR_NAMESPACE_OPEN_SCOPE

///
/// @class BRAY_HdDelegate
///
/// Render delegates provide renderer-specific functionality to the render
/// index, the main hydra state management structure. The render index uses
/// the render delegate to create and delete scene primitives, which include
/// geometry and also non-drawable objects. The render delegate is also
/// responsible for creating renderpasses, which know how to draw this
/// renderer's scene primitives.
///
/// Primitives in Hydra are split into Rprims (drawables), Sprims (state
/// objects like cameras and materials), and Bprims (buffer objects like
/// textures). The minimum set of primitives a renderer needs to support is
/// one Rprim (so the scene's not empty) and the "camera" Sprim, which is
/// required by HdxRenderTask, the task implementing basic hydra drawing.
///
/// A render delegate can report which prim types it supports via
/// GetSupportedRprimTypes() (and Sprim, Bprim), and well-behaved applications
/// won't call CreateRprim() (Sprim, Bprim) for prim types that aren't
/// supported. The core hydra prim types are "mesh", "basisCurves", and
/// "points", but a custom render delegate and a custom scene delegate could
/// add support for other prims such as implicit surfaces or volumes.
///
/// HdKarma Rprims create BRAY geometry objects in the render delegate's
/// top-level BRAY scene; and HdKarma's render pass draws by casting rays
/// into the top-level scene. The renderpass writes to the currently bound GL
/// framebuffer.
///
/// The render delegate also has a hook for the main hydra execution algorithm
/// (HdEngine::Execute()): between HdRenderIndex::SyncAll(), which pulls new
/// scene data, and execution of tasks, the engine calls back to
/// CommitResources(). This can be used to commit GPU buffers or, in HdKarma's
/// case, to do a final build of the BVH.
///
class BRAY_HdDelegate final : public HdRenderDelegate
{
public:
    /// Render delegate constructor.
    BRAY_HdDelegate(const HdRenderSettingsMap &settingsMap, bool xpu);
    /// Render delegate destructor. This method destroys the RTC device and
    /// scene.
    ~BRAY_HdDelegate() override;

    /// Return this delegate's render param.
    ///   @return A shared instance of BRAY_HdParam.
    HdRenderParam *GetRenderParam() const override;

    /// Return a list of which Rprim types can be created by this class's
    /// CreateRprim.
    const TfTokenVector &GetSupportedRprimTypes() const override;
    /// Return a list of which Sprim types can be created by this class's
    /// CreateSprim.
    const TfTokenVector &GetSupportedSprimTypes() const override;
    /// Return a list of which Bprim types can be created by this class's
    /// CreateBprim.
    const TfTokenVector &GetSupportedBprimTypes() const override;

    /// Returns the HdResourceRegistry instance used by this render delegate.
    HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    /// Update a renderer setting
    void SetRenderSetting(const TfToken &key,
                          const VtValue &value) override;

    /// Return the descriptor for an AOV
    HdAovDescriptor GetDefaultAovDescriptor(
			const TfToken &name) const override;

    /// Return stats for rendering
    VtDictionary GetRenderStats() const override;

    /// Create a renderpass. Hydra renderpasses are responsible for drawing
    /// a subset of the scene (specified by the "collection" parameter) to the
    /// current framebuffer. This class creates objects of type
    /// BRAY_HdPass, which draw using a raycasting API.
    ///   @param index The render index this renderpass will be bound to.
    ///   @param collection A specifier for which parts of the scene should
    ///                     be drawn.
    ///   @return A renderpass object.
    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
                HdRprimCollection const& collection) override;

    /// Create an instancer. Hydra instancers store data needed for an
    /// instanced object to draw itself multiple times.
    ///   @param delegate The scene delegate providing data for this
    ///                   instancer.
    ///   @param id The scene graph ID of this instancer, used when pulling
    ///             data from a scene delegate.
    ///   @return An instancer object.
    HdInstancer *CreateInstancer(HdSceneDelegate *delegate,
                        SdfPath const& id) override;

    /// Destroy an instancer created with CreateInstancer.
    ///   @param instancer The instancer to be destroyed.
    void DestroyInstancer(HdInstancer *instancer) override;

    /// Find an instancer of the given path
    HdInstancer	*findInstancer(const SdfPath &id) const;

    /// Create a hydra Rprim, representing scene geometry. This class creates
    /// BRAY specialized geometry containers like HdKarmaMesh which map
    /// scene data to BRAY scene graph objects.
    ///   @param typeId The rprim type to create. This must be one of the types
    ///                 from GetSupportedRprimTypes().
    ///   @param rprimId The scene graph ID of this rprim, used when pulling
    ///                  data from a scene delegate.
    ///   @return An rprim object.
    HdRprim *CreateRprim(TfToken const& typeId,
                         SdfPath const& rprimId) override;

    /// Destroy an Rprim created with CreateRprim.
    ///   @param rPrim The rprim to be destroyed.
    void DestroyRprim(HdRprim *rPrim) override;

    /// Create a hydra Sprim, representing scene or viewport state like cameras
    /// or lights.
    ///   @param typeId The sprim type to create. This must be one of the types
    ///                 from GetSupportedSprimTypes().
    ///   @param sprimId The scene graph ID of this sprim, used when pulling
    ///                  data from a scene delegate.
    ///   @return An sprim object.
    HdSprim *CreateSprim(TfToken const& typeId,
                         SdfPath const& sprimId) override;

    /// Create a hydra Sprim using default values, and with no scene graph
    /// binding.
    ///   @param typeId The sprim type to create. This must be one of the types
    ///                 from GetSupportedSprimTypes().
    ///   @return A fallback sprim object.
    HdSprim *CreateFallbackSprim(TfToken const& typeId) override;

    /// Destroy an Sprim created with CreateSprim or CreateFallbackSprim.
    ///   @param sPrim The sprim to be destroyed.
    void DestroySprim(HdSprim *sPrim) override;

    /// Create a hydra Bprim, representing data buffers such as textures.
    ///   @param typeId The bprim type to create. This must be one of the types
    ///                 from GetSupportedBprimTypes().
    ///   @param bprimId The scene graph ID of this bprim, used when pulling
    ///                  data from a scene delegate.
    ///   @return A bprim object.
    HdBprim *CreateBprim(TfToken const& typeId,
                         SdfPath const& bprimId) override;

    /// Create a hydra Bprim using default values, and with no scene graph
    /// binding.
    ///   @param typeId The bprim type to create. This must be one of the types
    ///                 from GetSupportedBprimTypes().
    ///   @return A fallback bprim object.
    HdBprim *CreateFallbackBprim(TfToken const& typeId) override;

    /// Destroy a Bprim created with CreateBprim or CreateFallbackBprim.
    ///   @param bPrim The bprim to be destroyed.
    void DestroyBprim(HdBprim *bPrim) override;

    /// This function is called after new scene data is pulled during prim
    /// Sync(), but before any tasks (such as draw tasks) are run, and gives the
    /// render delegate a chance to transfer any invalidated resources to the
    /// rendering kernel. This class takes the opportunity to update the BRAY
    /// scene acceleration datastructures.
    ///   @param tracker The change tracker passed to prim Sync().
    void CommitResources(HdChangeTracker *tracker) override;

    /// Return true to deal with full materials
    TfToken GetMaterialBindingPurpose() const override;
    TfTokenVector GetMaterialRenderContexts() const override;
    TfTokenVector GetShaderSourceTypes() const override;

    bool IsPauseSupported() const override { return true; };
    bool Pause() override;
    bool Resume() override;

private:
    void	stopRender(bool inc_version=true);
    void        delegateRenderProducts(const VtValue &value);

    /// Resource registry used in this render delegate
    static std::mutex _mutexResourceRegistry;
    static std::atomic_int _counterResourceRegistry;
    static HdResourceRegistrySharedPtr _resourceRegistry;

    bool	headlightSetting(const TfToken &key, const VtValue &value);

    // This class does not support copying.
    BRAY_HdDelegate(const BRAY_HdDelegate &)             = delete;
    BRAY_HdDelegate &operator =(const BRAY_HdDelegate &) = delete;

    SYS_AtomicInt32		 mySceneVersion;
    BRAY::ScenePtr		 myScene;
    HdSceneDelegate		*mySDelegate;
    HdRenderThread		 myThread;
    BRAY::RendererPtr		 myRenderer;
    UT_UniquePtr<BRAY_HdParam>	 myRenderParam;
    BRAY_InteractionType	 myInteractionMode;
    UT_StringHolder              myUSDFilename;
    int64                        myUSDTimeStamp;
    float			 myVariance;
    int                          myOverrideLighting;
    int                          myHeadlightMode;
    bool                         myHeadlightEnable;
    bool			 myDisableLighting;
    bool			 myEnableDenoise;
    bool                         myXPUDelegate;
    bool                         myUseRenderSettingsPrim;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDKARMA_RENDER_DELEGATE_H
