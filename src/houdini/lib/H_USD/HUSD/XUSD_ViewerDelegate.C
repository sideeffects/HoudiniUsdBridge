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
 * NAME:	XUSD_ViewerDelegate.C (HUSD Library, C++)
 *
 * COMMENTS:	Render delegate for the native Houdini viewport renderer
 */
#include "XUSD_ViewerDelegate.h"

#include "XUSD_HydraGeoPrim.h"
#include "XUSD_HydraCamera.h"
#include "XUSD_HydraField.h"
#include "XUSD_HydraInstancer.h"
#include "XUSD_HydraLight.h"
#include "XUSD_HydraMaterial.h"

#include "XUSD_Tokens.h"
#include "HUSD_HydraCamera.h"
#include "HUSD_HydraField.h"
#include "HUSD_HydraLight.h"
#include "HUSD_HydraMaterial.h"
#include "HUSD_Scene.h"
#include "HUSD_Constants.h"

#include <UT/UT_StringHolder.h>
#include <UT/UT_Debug.h>

#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/extComputation.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/tokens.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

// -------------------------------------------------------------------------
// Render pass subclass

class xusd_RenderPass : public HdRenderPass
{
public:
    xusd_RenderPass(HdRenderIndex *index,
		    HdRprimCollection const &collection)
	: HdRenderPass(index, collection)
	{}

    virtual ~xusd_RenderPass() {}

protected:

    virtual void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
                          TfTokenVector const &renderTags) override
	{
	}
};

// -------------------------------------------------------------------------

const TfTokenVector XUSD_ViewerDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->points,
    HdPrimTypeTokens->mesh,
    HdPrimTypeTokens->basisCurves,
    HdPrimTypeTokens->volume,
};

const TfTokenVector XUSD_ViewerDelegate::SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->material,
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->extComputation,
    
    // lights
    HdPrimTypeTokens->cylinderLight,
    HdPrimTypeTokens->diskLight,
    HdPrimTypeTokens->distantLight,
    HdPrimTypeTokens->domeLight,
    HdPrimTypeTokens->rectLight,
    HdPrimTypeTokens->sphereLight,
    HusdHdPrimTypeTokens()->sprimGeometryLight,
};

const TfTokenVector XUSD_ViewerDelegate::SUPPORTED_BPRIM_TYPES =
{
    HusdHdPrimTypeTokens()->openvdbAsset,
    HusdHdPrimTypeTokens()->bprimHoudiniFieldAsset,
};


XUSD_ViewerDelegate::XUSD_ViewerDelegate(HUSD_Scene &scene)
    : myScene(scene),
      myParam(nullptr)
{
}

XUSD_ViewerDelegate::~XUSD_ViewerDelegate()
{
}

TfTokenVector const&
XUSD_ViewerDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const&
XUSD_ViewerDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
XUSD_ViewerDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

void
XUSD_ViewerDelegate::CommitResources(HdChangeTracker *tracker)
{
    ;
}


HdResourceRegistrySharedPtr
XUSD_ViewerDelegate::GetResourceRegistry() const
{
    static HdResourceRegistrySharedPtr _resourceRegistry;
    if(!_resourceRegistry)
	_resourceRegistry.reset(new HdResourceRegistry() );
    return _resourceRegistry;
}

HdRenderParam *
XUSD_ViewerDelegate::GetRenderParam() const
{
    if(!myParam)
    {
	myParam = new XUSD_ViewerRenderParam(myScene);
	myScene.setRenderParam(myParam);
    }

    return myParam;
}

HdRenderPassSharedPtr
XUSD_ViewerDelegate::CreateRenderPass(HdRenderIndex *index,
                                      HdRprimCollection const& collection)
{
    myScene.setRenderIndex(index);
    //UTdebugFormat("Create render pass {}", &collection);
    return HdRenderPassSharedPtr(
        new xusd_RenderPass(index, collection));
}

HdInstancer *
XUSD_ViewerDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                     SdfPath const& id,
                                     SdfPath const& instancerId)
{
    //UTdebugFormat("CreateInstancer: {}", id.GetText(), instancerId.GetText());
    return new XUSD_HydraInstancer(delegate, id, instancerId);
}

void
XUSD_ViewerDelegate::DestroyInstancer(HdInstancer *instancer)
{
    delete instancer;
}

HdRprim *
XUSD_ViewerDelegate::CreateRprim(TfToken const& typeId,
                                 SdfPath const& primId,
                                 SdfPath const& instancerId)
{
    UT_StringHolder path = primId.GetText();
    auto entry = myScene.fetchPendingRemovalPrim(path);
    if(entry)
    {
        auto xprim = static_cast<PXR_NS::XUSD_HydraGeoPrim*>(entry.get());

        if(xprim->primType() == typeId)
        {
            myScene.addGeometry(xprim);
            return xprim->rprim();
        }
    }
    
    auto prim = new PXR_NS::XUSD_HydraGeoPrim(typeId, primId, instancerId,
                                              myScene);
    
    if(prim->isValid())
    {
        myScene.addGeometry(prim);
        return prim->rprim();
    }
    else
    {
        delete prim;
        return nullptr;
    }
}

void
XUSD_ViewerDelegate::DestroyRprim(HdRprim *prim)
{
    UT_StringHolder path = prim->GetId().GetText();
    auto hprim = myScene.geometry().find(path);

    if(hprim != myScene.geometry().end())
    {
	HUSD_HydraGeoPrim	*gprim = hprim->second.get();

        myScene.pendingRemovalPrim(path,hprim->second);
	myScene.removeGeometry(gprim);
    }
}

HdSprim *
XUSD_ViewerDelegate::CreateSprim(TfToken const& typeId,
                                 SdfPath const& primId)
{
    //UTdebugFormat("Sprim: {}", typeId.GetText(), primId.GetText());
    HdSprim *sprim = nullptr;
    
    if (typeId == HdPrimTypeTokens->camera)
    {
	// default free cam. Hydra requires this be non-null or it crashes.
	// we do not want to include it in our list of cameras though.
	if(strstr(primId.GetText(),
		  HUSD_Constants::getHoudiniRendererPluginName()))
	    return new PXR_NS::HdCamera(primId);

	HUSD_HydraCamera *hcam = new HUSD_HydraCamera(typeId, primId, myScene);
	myScene.addCamera(hcam);
	sprim = hcam->hydraCamera();
    }
    else if (typeId == HdPrimTypeTokens->cylinderLight ||
	     typeId == HdPrimTypeTokens->diskLight ||
	     typeId == HdPrimTypeTokens->distantLight ||
	     typeId == HdPrimTypeTokens->domeLight ||
	     typeId == HdPrimTypeTokens->rectLight ||
	     typeId == HdPrimTypeTokens->sphereLight ||
	     typeId == HusdHdPrimTypeTokens()->sprimGeometryLight)
    {
	UT_StringHolder name(primId.GetText());
	    
	auto entry = myScene.lights().find(name);
	if(entry == myScene.lights().end())
	{
	    HUSD_HydraLight *hlight= new HUSD_HydraLight(typeId,primId,myScene);
	    myScene.addLight( hlight );
	    sprim = hlight->hydraLight();
	}
    }
    else if (typeId == HdPrimTypeTokens->material)
    {
	UT_StringHolder name(primId.GetText());

	auto entry = myScene.materials().find(name);
	if(entry == myScene.materials().end())
	{
	    auto hmat = new HUSD_HydraMaterial(primId, myScene);
	    myScene.addMaterial( hmat );
	    sprim = hmat->hydraMaterial();
	}
    }
    else if (typeId == HdPrimTypeTokens->extComputation)
    {
        sprim =  new HdExtComputation(primId);
    }
	
    return sprim;
}

HdSprim *
XUSD_ViewerDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    //UTdebugFormat("Fallback Sprim: {}", typeId.GetText());
    // For fallback sprims, create objects with an empty scene path.
    // They'll use default values and won't be updated by a scene delegate.
    // if (typeId == HdPrimTypeTokens->camera) {
    //     return new HdStCamera(SdfPath::EmptyPath());
    // } else {
    //     TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    // }

    return nullptr;
}

void
XUSD_ViewerDelegate::DestroySprim(HdSprim *sPrim)
{
    if(sPrim)
    {
	UT_StringHolder id = sPrim->GetId().GetText();
	
	auto cam = myScene.cameras().find(id);
	if(cam != myScene.cameras().end())
	{
	    HUSD_HydraCamera	*cprim = cam->second.get();
	    myScene.removeCamera(cprim);
	    return;
	}
	
	auto light = myScene.lights().find(id);
	if(light != myScene.lights().end())
	{
	    HUSD_HydraLight	*lprim = light->second.get();
	    myScene.removeLight(lprim);
	    return;
	}
	
	auto mat = myScene.materials().find(id);
	if(mat != myScene.materials().end())
	{
	    HUSD_HydraMaterial	*mprim = mat->second.get();
	    myScene.removeMaterial(mprim);
	    return;
	}
    
	delete sPrim; // unknown ?
    }
}

HdBprim *
XUSD_ViewerDelegate::CreateBprim(TfToken const& typeId,
                                 SdfPath const& bprimId)
{
    //UTdebugFormat("Bprim: {}", typeId.GetText());
    HdBprim *bprim = nullptr;

    if (typeId == HusdHdPrimTypeTokens()->openvdbAsset ||
	typeId == HusdHdPrimTypeTokens()->bprimHoudiniFieldAsset)
    {
	HUSD_HydraField *hfield =
	    new HUSD_HydraField(typeId, bprimId, myScene);

	bprim = hfield->hydraField();
    }

    return bprim;
}

HdBprim *
XUSD_ViewerDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    //UTdebugFormat("Fallback Bprim: {}", typeId.GetText());
    // For fallback bprims, create objects with an empty scene path.
    // They'll use default values and won't be updated by a scene delegate.
    // if (typeId == HdPrimTypeTokens->camera) {
    //     return new HdStCamera(SdfPath::EmptyPath());
    // } else {
    //     TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    // }

    return nullptr;
}

void
XUSD_ViewerDelegate::DestroyBprim(HdBprim *bPrim)
{
    delete bPrim;
}
TfToken
XUSD_ViewerDelegate::GetMaterialBindingPurpose() const
{
    return HdTokens->full;
}

TfToken
XUSD_ViewerDelegate::GetMaterialNetworkSelector() const
{
    static TfToken theUniversalRenderContextToken("");

    return theUniversalRenderContextToken;
}

TfTokenVector
XUSD_ViewerDelegate::GetShaderSourceTypes() const
{
    static TfTokenVector theSourceTypes({HdShaderTokens->commonShaderSource});

    return theSourceTypes;
}

HdAovDescriptor
XUSD_ViewerDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    if(name == HdAovTokens->color)
    {
        VtValue cval;
        return HdAovDescriptor(HdFormatFloat16, true, cval);
    }
    return HdAovDescriptor();
}

PXR_NAMESPACE_CLOSE_SCOPE

