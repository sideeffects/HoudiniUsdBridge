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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_ViewerDelegate.h (HUSD Library, C++)
 *
 * COMMENTS:	Render delegate for the native Houdini viewport renderer
 */

#ifndef XUSD_ViewerDelegate_h
#define XUSD_ViewerDelegate_h

#include "HUSD_API.h"

#include <pxr/pxr.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <SYS/SYS_Types.h>

class HUSD_Scene;

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_ViewerRenderParam;

/// Render delegate for the native Houdini viewport renderer
class XUSD_ViewerDelegate  : public HdRenderDelegate
{
public:
	     XUSD_ViewerDelegate(HUSD_Scene &scene);
    virtual ~XUSD_ViewerDelegate();

    virtual HdRenderParam *GetRenderParam() const override;

    virtual const TfTokenVector &GetSupportedRprimTypes() const override;
    virtual const TfTokenVector &GetSupportedSprimTypes() const override;
    virtual const TfTokenVector &GetSupportedBprimTypes() const override;

    virtual HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    virtual HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
                HdRprimCollection const& collection) override;

    virtual HdInstancer *CreateInstancer(HdSceneDelegate *delegate,
                                         SdfPath const& id,
                                         SdfPath const& instancerId) override;
    virtual void DestroyInstancer(HdInstancer *instancer) override;


    // Renderable prim (geometry)
    virtual HdRprim *CreateRprim(TfToken const& typeId,
                                 SdfPath const& rprimId,
                                 SdfPath const& instancerId) override;
    virtual void DestroyRprim(HdRprim *rPrim) override;

    // Cameras & Lights
    virtual HdSprim *CreateSprim(TfToken const& typeId,
                                 SdfPath const& sprimId) override;
    virtual HdSprim *CreateFallbackSprim(TfToken const& typeId) override;
    virtual void DestroySprim(HdSprim *sPrim) override;

    // Buffer prims (textures)
    virtual HdBprim *CreateBprim(TfToken const& typeId,
                                 SdfPath const& bprimId) override;
    virtual HdBprim *CreateFallbackBprim(TfToken const& typeId) override;
    virtual void DestroyBprim(HdBprim *bPrim) override;

    virtual void CommitResources(HdChangeTracker *tracker) override;

    virtual TfToken GetMaterialBindingPurpose() const override;
    virtual TfToken GetMaterialNetworkSelector() const override;
    virtual TfTokenVector GetShaderSourceTypes() const override;
    virtual HdAovDescriptor GetDefaultAovDescriptor(TfToken const& name) const override;
    
    HUSD_Scene &scene() { return myScene; }

private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;
    
    XUSD_ViewerDelegate(const XUSD_ViewerDelegate &) = delete;
    XUSD_ViewerDelegate &operator=(const XUSD_ViewerDelegate &) =delete;
    
    HUSD_Scene &myScene;
    mutable XUSD_ViewerRenderParam *myParam;
};

class XUSD_ViewerRenderParam : public HdRenderParam
{
public:
    XUSD_ViewerRenderParam(HUSD_Scene &scene)  : myScene(scene)    {}
    virtual ~XUSD_ViewerRenderParam() = default;

    HUSD_Scene  &scene()	{ return myScene; }

private:
    HUSD_Scene &myScene;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
