//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#ifndef HDKARMA_RENDER_PASS_H
#define HDKARMA_RENDER_PASS_H

#include <pxr/pxr.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderThread.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hdx/compositor.h>
#include <pxr/imaging/glf/glContext.h>
#include <pxr/base/gf/matrix4d.h>
#include <SYS/SYS_AtomicInt.h>
#include <UT/UT_UniquePtr.h>
#include "BRAY_HdAOVBuffer.h"

#include <atomic>

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdParam;

/// @class BRAY_HdPass
///
/// HdRenderPass represents a single render iteration, rendering a view of the
/// scene (the HdRprimCollection) for a specific viewer (the camera/viewport
/// parameters in HdRenderPassState) to the current draw target.
///
/// This class does so by raycasting into the scene.
///
class BRAY_HdPass final : public HdRenderPass
{
public:
    /// Renderpass constructor.
    ///   @param index The render index containing scene data to render.
    ///   @param collection The initial rprim collection for this renderpass.
    ///   @param scene The scene to raycast into.
    BRAY_HdPass(HdRenderIndex *index,
                       const HdRprimCollection &collection,
		       BRAY_HdParam &renderParam,
		       BRAY::RendererPtr &renderer,
		       HdRenderThread &renderThread,
		       SYS_AtomicInt32 &sceneVersion,
		       BRAY::ScenePtr &scene);

    /// Renderpass destructor.
    virtual ~BRAY_HdPass();

    // -----------------------------------------------------------------------
    // HdRenderPass API

    /// Determine whether the sample buffer has enough samples.
    ///   @return True if the image has enough samples to be considered final.
    virtual bool IsConverged() const override;

protected:

    // -----------------------------------------------------------------------
    // HdRenderPass API

    /// Draw the scene with the bound renderpass state.
    ///   @param renderPassState Input parameters (including viewer parameters)
    ///                          for this renderpass.
    ///   @param renderTags Which rendertags should be drawn this pass.
    virtual void _Execute(const HdRenderPassStateSharedPtr &renderPassState,
                          const TfTokenVector &renderTags) override;

    /// Update internal tracking to reflect a dirty collection.
    virtual void _MarkCollectionDirty() override {}

private:
    /// Validate AOVs and add them to the renderer.
    bool validateAOVs(HdRenderPassAovBindingVector &bindings) const;
    bool validateRenderSettings(const HdRenderPassAovBinding &aov,
				HdRenderBuffer *abuf) const;

    void	stopRendering()
    {
	myRenderer.prepareForStop();
	myThread.StopRender();
	UT_ASSERT(!myRenderer.isRendering());
    }
    void	updateSceneResolution();

    HdRenderPassAovBindingVector	 myAOVBindings;
    HdRenderPassAovBindingVector	 myFullAOVBindings;
    BRAY::ScenePtr			&myScene;
    BRAY_HdParam			&myRenderParam;
    BRAY::RendererPtr			&myRenderer;
    UT_UniquePtr<BRAY_HdAOVBuffer>	 myColorBuffer;
    SdfPath				 myCameraPath;
    HdRenderThread			&myThread;
    SYS_AtomicInt32			&mySceneVersion;
    GfMatrix4d				 myView, myProj; // Camera space
    GfVec2i				 myResolution;
    GfVec4f				 myDataWindow;
    double				 myPixelAspect;
    uint				 myWidth, myHeight; // Viewport
    int					 myLastVersion;
    BRAY_RayVisibility			 myCameraMask;
    BRAY_RayVisibility			 myShadowMask;
    bool				 myValidAOVs;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDKARMA_RENDER_PASS_H
