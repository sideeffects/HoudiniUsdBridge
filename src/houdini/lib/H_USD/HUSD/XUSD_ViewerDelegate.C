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
#include "XUSD_Format.h"

#include "XUSD_Tokens.h"
#include "HUSD_HydraCamera.h"
#include "HUSD_HydraField.h"
#include "HUSD_HydraLight.h"
#include "HUSD_HydraMaterial.h"
#include "HUSD_Path.h"
#include "HUSD_Scene.h"
#include "HUSD_Constants.h"

#include <UT/UT_EnvControl.h>
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

    ~xusd_RenderPass() override {}

protected:

    void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
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
    HusdHdPrimTypeTokens->boundingBox,
    HusdHdPrimTypeTokens->metaCurves,
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
};

const TfTokenVector XUSD_ViewerDelegate::SUPPORTED_BPRIM_TYPES =
{
    HusdHdPrimTypeTokens->openvdbAsset,
    HusdHdPrimTypeTokens->bprimHoudiniFieldAsset,
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
                                     SdfPath const& id)
{
    HUSD_Path path(id);

    //UTdebugFormat("CreateInstancer: {}", id.GetText());
    XUSD_HydraInstancer *inst =
        myScene.fetchPendingRemovalInstancer(path.pathStr());

    // It's possible the scene delegate has been replaced, in which case the
    // scene delegate pointer in the HdInstancer is no longer valid. We can't
    // actually reach inside HdInstancer and change that private member
    // variable, so delete this instancer instead of reusing it, and make
    // a new one instead.
    if (inst && inst->GetDelegate() != delegate)
    {
        delete inst;
        inst = nullptr;
    }

    // When we reuse an HdInstancer object, we have to use this ugly
    // const cast to clear a private member variable value that will still
    // be holding the value that was there when the instancer was removed
    // from the render index.
    if(inst)
        SYSconst_cast(inst->GetParentId()) = SdfPath();
    else
        inst = new XUSD_HydraInstancer(delegate, id);

    myScene.addInstancer(path.pathStr(), inst);

    return inst;
}

void
XUSD_ViewerDelegate::DestroyInstancer(HdInstancer *inst)
{
    HUSD_Path path(inst->GetId());
    myScene.removeInstancer(path.pathStr());
    myScene.pendingRemovalInstancer(path.pathStr(),
                                    static_cast<XUSD_HydraInstancer*>(inst));
}

HdRprim *
XUSD_ViewerDelegate::CreateRprim(TfToken const& typeId,
                                 SdfPath const& primId)
{
    HUSD_Path hpath(primId);
    UT_StringHolder path = hpath.pathStr();
    auto entry = myScene.fetchPendingRemovalGeom(path, typeId.GetText());
    if(entry)
    {
        auto xprim = static_cast<PXR_NS::XUSD_HydraGeoPrim*>(entry.get());
        // When we reuse an HdRprim object, we have to use this ugly
        // const cast to clear a private member variable value that will still
        // be holding the value that was there when the rprim was removed
        // from the render index.
        SYSconst_cast(xprim->rprim()->GetInstancerId()) = SdfPath();
        myScene.addGeometry(xprim, false);
        return xprim->rprim();
    }
    
    auto prim = new PXR_NS::XUSD_HydraGeoPrim(typeId, primId, myScene);
    
    if(prim->isValid())
    {
        myScene.addGeometry(prim, true);
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
    HUSD_Path hpath(prim->GetId());
    UT_StringHolder path = hpath.pathStr();
    auto hprim = myScene.geometry().find(path);

    if(hprim != myScene.geometry().end())
        myScene.pendingRemovalGeom(path, hprim->second);
}

HdSprim *
XUSD_ViewerDelegate::CreateSprim(TfToken const& typeId,
                                 SdfPath const& primId)
{
    //UTdebugFormat("Sprim: {} {}", typeId, primId);
    HdSprim *sprim = nullptr;
    HUSD_Path hpath(primId);
    UT_StringHolder path = hpath.pathStr();
   
    if (typeId == HdPrimTypeTokens->camera)
    {
	// default free cam. Hydra requires this be non-null or it crashes.
	// we do not want to include it in our list of cameras though.
	if(strstr(path,  HUSD_Constants::getHoudiniRendererPluginName()) ||
	   !strcmp(path, HUSD_Constants::getHoudiniFreeCameraPrimPath()))
        {
           return new PXR_NS::HdCamera(primId);
        }
        
        auto entry = myScene.fetchPendingRemovalCamera(path);
        if(entry)
        {
            myScene.addCamera(entry.get(), false);
            sprim = entry->hydraCamera();
        }
        else
        {
            HUSD_HydraCamera *hcam =
                new HUSD_HydraCamera(typeId, primId, myScene);
            myScene.addCamera(hcam, true);
            sprim = hcam->hydraCamera();
        }
    }
    else if (typeId == HdPrimTypeTokens->cylinderLight ||
	     typeId == HdPrimTypeTokens->diskLight ||
	     typeId == HdPrimTypeTokens->distantLight ||
	     typeId == HdPrimTypeTokens->domeLight ||
	     typeId == HdPrimTypeTokens->rectLight ||
	     typeId == HdPrimTypeTokens->sphereLight)
    {
        auto entry = myScene.fetchPendingRemovalLight(path);
        if(entry)
        {
            myScene.addLight(entry.get(), false);
            sprim = entry->hydraLight();
            entry->hydraLight()->updateType(typeId);
        }
        else
        {
            auto lentry = myScene.lights().find(path);
            if(lentry == myScene.lights().end())
            {
                HUSD_HydraLight *hlight =
                    new HUSD_HydraLight(typeId,primId,myScene);
                myScene.addLight( hlight, true );
                sprim = hlight->hydraLight();
            }
        }
    }
    else if (typeId == HdPrimTypeTokens->material)
    {
	auto entry = myScene.materials().find(path);
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
        HUSD_Path hpath(sPrim->GetId());
	UT_StringHolder id = hpath.pathStr();
	
	auto cam = myScene.cameras().find(id);
	if(cam != myScene.cameras().end())
	{
            myScene.pendingRemovalCamera(id, cam->second);	
            return;
	}
	
	auto light = myScene.lights().find(id);
	if(light != myScene.lights().end())
	{
            myScene.pendingRemovalLight(id, light->second);
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

    if (typeId == HusdHdPrimTypeTokens->openvdbAsset ||
	typeId == HusdHdPrimTypeTokens->bprimHoudiniFieldAsset)
    {
	HUSD_HydraField *hfield =
	    new HUSD_HydraField(typeId, bprimId, myScene);
        myScene.addField(hfield);
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
    if (bPrim)
    {
        HUSD_Path hpath(bPrim->GetId());
	UT_StringHolder id = hpath.pathStr();

        auto field = myScene.fields().find(id);
        if (field != myScene.fields().end())
        {
            HUSD_HydraField *fprim = field->second.get();
            myScene.removeField(fprim);
            return;
        }

        // Unknown bprim type?
        delete bPrim;
    }
}
TfToken
XUSD_ViewerDelegate::GetMaterialBindingPurpose() const
{
    return HdTokens->preview;
}


TfTokenVector
XUSD_ViewerDelegate::GetMaterialRenderContexts() const
{
    static const TfToken theMtlxToken("mtlx", TfToken::Immortal);
    static int theUseMtlx(UT_EnvControl::getInt(ENV_HOUDINI_GL_USE_MATERIALX));

    if(theUseMtlx != 0)
        return { theMtlxToken };
    else
        return { };
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
    static TfTokenVector theSourceTypes;

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

