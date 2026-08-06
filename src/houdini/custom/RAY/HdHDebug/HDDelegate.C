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

#include "HDDelegate.h"
#include "HDTokens.h"
#include "HDInstancer.h"
#include "HDAOV.h"
#include "HDBPrim.h"
#include "HDRPrim.h"
#include "HDSPrim.h"
#include "HDUtil.h"
#include "HDParam.h"
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/extComputation.h>

#include <UT/UT_SysClone.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    UT_StringHolder
    toString(const HdRenderPassAovBinding &b)
    {
        UT_WorkBuffer   tmp;
        tmp.format("  {{\n");
        tmp.appendFormat("\taovName: {}\n", b.aovName);
        tmp.appendFormat("\trenderBuffer: {}\n", b.renderBuffer);
        tmp.appendFormat("\trenderBufferId: {}\n", b.renderBufferId);
        tmp.appendFormat("\tclearValue: {}\n", b.clearValue);
        tmp.appendFormat("\tsettingsMap: {{\n");
        for (const auto &item : b.aovSettings)
            tmp.appendFormat("\t  {} : {}\n", item.first, item.second);
        tmp.appendFormat("\t}}\n");
        tmp.appendFormat("  }}");
        return tmp;
    }

    class HDPass final : public HdRenderPass
    {
    public:
        static constexpr const char *class_name = "HDPass";

        HDPass(HdRenderIndex *index,
                const HdRprimCollection &collection)
            : HdRenderPass(index, collection)
        {
            HDEBUG_CTOR();
        }
        ~HDPass() override
        {
            HDEBUG_DTOR();
        }

        bool    IsConverged() const override
        {
            HDEBUG_TRACE_FUNCTION();
            return myRendered;
        }
        void    _Execute(const HdRenderPassStateSharedPtr &rstate,
                        const TfTokenVector &rtags) override
        {
            HDEBUG_TRACE_FUNCTION();
            const HdCamera *cam = rstate->GetCamera();
            GfVec4f vp = rstate->GetViewport();
            const auto &displayWindow = rstate->GetFraming().displayWindow;
            const auto &dataWindow = rstate->GetFraming().dataWindow;
            if (HDOptions::image())
            {
                UTformat(stderr, "Render Pass\n");
                if (cam)
                    UTformat(stderr, "         Camera: {}\n", cam->GetId());
                UTformat(stderr, "        Viewport: {}\n", vp);
                UTformat(stderr, "   DisplayWindow: {}\n", displayWindow);
                UTformat(stderr, "      DataWindow: {}\n", dataWindow);
                UTformat(stderr, "     WorldToView: {}\n", rstate->GetWorldToViewMatrix());
                if (HDOptions::projection())
                    UTformat(stderr, "      Projection: {}\n", rstate->GetProjectionMatrix());
                UTformat(stderr, "     Render Tags: [");
                for (const auto &tag : rtags)
                    UTformat(stderr, " {}", tag);
                UTformat(stderr, " ]\n");
            }
            auto bindings = rstate->GetAovBindings();
            if (HDOptions::image())
                UTformat(stderr, "Render AOVS\n");
            for (const auto &b : bindings)
            {
                HDAOV   *aov = dynamic_cast<HDAOV *>(b.renderBuffer);
                HDEBUG_ASSERT(aov);
                if (aov)
                    aov->setConverged();
                if (HDOptions::image())
                    UTformat(stderr, "{}\n", toString(b));
            }
            myRendered = true;
            if (HDOptions::sleep() > 0)
            {
                float   sec = HDOptions::sleep();
                UTformat(stderr, "Sleeping for {} seconds ", sec);
                while (sec > 0)
                {
                    UTnap(SYSmin(1.0f, sec) * 1000);
                    sec -= 1;
                    if (sec > 0)
                        UTformat(stderr, ".");
                }
                UTformat(stderr, "\n");
            }
        }
        void    _MarkCollectionDirty() override
        {
            HDEBUG_TRACE_FUNCTION();
        }
    private:
        bool    myRendered = false;
    };

    static const TfTokenVector  SUPPORTED_BPRIM_TYPES =
    {
        HdPrimTypeTokens->renderBuffer,
        HDebugTokens->openvdbAsset,
        HDebugTokens->houdiniFieldAsset,
    };
    static const TfTokenVector  SUPPORTED_SPRIM_TYPES =
    {
        HdPrimTypeTokens->camera,
        HdPrimTypeTokens->coordSys,
        HdPrimTypeTokens->material,

        HdPrimTypeTokens->cylinderLight,
        HdPrimTypeTokens->diskLight,
        HdPrimTypeTokens->distantLight,
        HdPrimTypeTokens->domeLight,
        HdPrimTypeTokens->light,
        HdPrimTypeTokens->lightFilter,
        HdPrimTypeTokens->meshLight,
        HdPrimTypeTokens->rectLight,
        HdPrimTypeTokens->sphereLight,

        HdPrimTypeTokens->extComputation,
    };
    static const TfTokenVector  SUPPORTED_RPRIM_TYPES =
    {
        HdPrimTypeTokens->basisCurves,
        HdPrimTypeTokens->mesh,
        HdPrimTypeTokens->points,
        HdPrimTypeTokens->volume,
    };
}

HDDelegate::HDDelegate(const HdRenderSettingsMap &seting)
    : HdRenderDelegate()
    , myThread()
{
    HDEBUG_CTOR();
    myParam = std::make_unique<HDParam>(myThread);
    myThread.StartThread();
}

HDDelegate::~HDDelegate()
{
    HDEBUG_DTOR();
    myParam.reset(nullptr);     // Release memory before dumping stats
    if (HDOptions::object())
        UTformat(stderr, "Objects:\n{}\n", HDUtil::dumpObjects());
    if (HDOptions::memory())
    {
        UTformat(stderr, "Memory:\n{}\n", HDUtil::dumpMemory());
        UTformat(stderr, "Primvars:\n{}\n", HDUtil::dumpPrimvarMemory());
    }
    UTformat(stderr, "{}\n", HDUtil::systemUsage());
    HDUtil::clearTables();
}

HdRenderParam *
HDDelegate::GetRenderParam() const
{
    HDEBUG_TRACE_FUNCTION();
    return myParam.get();
}

const TfTokenVector &
HDDelegate::GetSupportedRprimTypes() const
{
    HDEBUG_TRACE_FUNCTION();
    return SUPPORTED_RPRIM_TYPES;
}

const TfTokenVector &
HDDelegate::GetSupportedSprimTypes() const
{
    HDEBUG_TRACE_FUNCTION();
    return SUPPORTED_SPRIM_TYPES;
}

const TfTokenVector &
HDDelegate::GetSupportedBprimTypes() const
{
    HDEBUG_TRACE_FUNCTION();
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr
HDDelegate::GetResourceRegistry() const
{
    HDEBUG_TRACE_FUNCTION();
    static HdResourceRegistrySharedPtr _resourceRegistry;
    return _resourceRegistry;
}

void
HDDelegate::SetRenderSetting(const TfToken &key, const VtValue &value)
{
    HDEBUG_TRACE_FUNCTION();
}

HdAovDescriptor
HDDelegate::GetDefaultAovDescriptor(const TfToken &name) const
{
    HDEBUG_TRACE_FUNCTION();
    if (name == HdAovTokens->color)
	return HdAovDescriptor(HdFormatFloat16Vec4, true, VtValue(GfVec4h(0)));
    if (name == HdAovTokens->normal || name == HdAovTokens->Neye)
	return HdAovDescriptor(HdFormatFloat16Vec3, false, VtValue(GfVec3f(-1)));
    if (name == HdAovTokens->depth)
	return HdAovDescriptor(HdFormatFloat32, false, VtValue(1e17f));
    if (name == HdAovTokens->primId)
	return HdAovDescriptor(HdFormatInt32, false, VtValue(0));
    if (name == HdAovTokens->elementId)
	return HdAovDescriptor(HdFormatInt32, false, VtValue(0));
    if (name == HdAovTokens->instanceId)
	return HdAovDescriptor(HdFormatInt32, false, VtValue(0));

    HdParsedAovToken	aov(name);
    if (aov.isLpe)
	return HdAovDescriptor(HdFormatFloat16Vec3, true, VtValue(GfVec3f(0)));
    if (aov.isPrimvar)
	return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0)));

    return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
}

VtDictionary
HDDelegate::GetRenderStats() const
{
    HDEBUG_TRACE_FUNCTION();
    return VtDictionary();
}

HdRenderPassSharedPtr
HDDelegate::CreateRenderPass(HdRenderIndex *index,
        const HdRprimCollection &collection)
{
    HDEBUG_TRACE_FUNCTION();
    return std::make_shared<HDPass>(index, collection);
}

HdInstancer *
HDDelegate::CreateInstancer(HdSceneDelegate *sd, const SdfPath &id)
{
    HDEBUG_TRACE_FUNCTION();
    if (HDOptions::rprim()) UTformat("Create Instancer {}\n", id);
    return new HDInstancer(sd, id);
}

void
HDDelegate::DestroyInstancer(HdInstancer *instancer)
{
    HDEBUG_TRACE_FUNCTION();
    if (HDOptions::rprim()) UTformat("Destroy Instancer {}\n", instancer->GetId());
    delete instancer;
}

HdRprim *
HDDelegate::CreateRprim(const TfToken &type, const SdfPath &id)
{
    HDEBUG_TRACE_FUNCTION();
    if (HDOptions::rprim()) UTformat("Create RPrim {} {}\n", type, id);
    if (type == HdPrimTypeTokens->basisCurves)
        return new HDCurves(id);
    if (type == HdPrimTypeTokens->mesh)
        return new HDMesh(id);
    if (type == HdPrimTypeTokens->points)
        return new HDPoints(id);
    if (type == HdPrimTypeTokens->volume)
        return new HDVolume(id);

    TF_CODING_ERROR("Unknown Rprim type: %s", type.GetText());
    return nullptr;
}

void
HDDelegate::DestroyRprim(HdRprim *prim)
{
    HDEBUG_TRACE_FUNCTION();
    if (HDOptions::rprim()) UTformat("Destroy RPrim {}\n", prim->GetId());
    delete prim;
}

HdSprim *
HDDelegate::CreateSprim(const TfToken &type, const SdfPath &id)
{
    HDEBUG_TRACE_FUNCTION();
    if (HDOptions::sprim()) UTformat("Create SPrim {} {}\n", type, id);
    if (type == HdPrimTypeTokens->extComputation)
        return new HdExtComputation(id);
    if (type == HdPrimTypeTokens->material)
        return new HDMaterial(id);
    if (type == HdPrimTypeTokens->camera)
        return new HDCamera(id);
    if (type == HdPrimTypeTokens->coordSys)
        return new HDCoordSys(id);
    if (type == HdPrimTypeTokens->cylinderLight
            || type == HdPrimTypeTokens->diskLight
            || type == HdPrimTypeTokens->distantLight
            || type == HdPrimTypeTokens->domeLight
            || type == HdPrimTypeTokens->light
            || type == HdPrimTypeTokens->rectLight
            || type == HdPrimTypeTokens->sphereLight)
        return new HDLight(id);

    TF_CODING_ERROR("Unknown Sprim type: %s", type.GetText());
    return nullptr;
}

HdSprim *
HDDelegate::CreateFallbackSprim(const TfToken &type)
{
    HDEBUG_TRACE_FUNCTION();
    return CreateSprim(type, SdfPath::EmptyPath());
}

void
HDDelegate::DestroySprim(HdSprim *prim)
{
    HDEBUG_TRACE_FUNCTION();
    if (HDOptions::sprim()) UTformat("Destroy SPrim {}\n", prim->GetId());
    delete prim;
}

HdBprim *
HDDelegate::CreateBprim(const TfToken &type, const SdfPath &id)
{
    HDEBUG_TRACE_FUNCTION();
    if (HDOptions::bprim()) UTformat("Create BPrim {} {}\n", type, id);
    if (type == HdPrimTypeTokens->renderBuffer)
        return new HDAOV(id);
    if (type == HDebugTokens->openvdbAsset
        || type == HDebugTokens->houdiniFieldAsset)
        return new HDField(type, id);

    TF_CODING_ERROR("Unknown Bprim type: %s", type.GetText());
    return nullptr;
}

HdBprim *
HDDelegate::CreateFallbackBprim(const TfToken &type)
{
    HDEBUG_TRACE_FUNCTION();
    return CreateBprim(type, SdfPath::EmptyPath());
}

void
HDDelegate::DestroyBprim(HdBprim *prim)
{
    HDEBUG_TRACE_FUNCTION();
    if (HDOptions::bprim()) UTformat("Destroy BPrim {}\n", prim->GetId());
    delete prim;
}

void
HDDelegate::CommitResources(HdChangeTracker *tracker)
{
    HDEBUG_TRACE_FUNCTION();
}

TfToken
HDDelegate::GetMaterialBindingPurpose() const
{
    HDEBUG_TRACE_FUNCTION();
    return HdTokens->full;
}

TfTokenVector
HDDelegate::GetMaterialRenderContexts() const
{
    HDEBUG_TRACE_FUNCTION();
    // For now, just the karma materials
    return {
        HDebugTokens->kma,
        HDebugTokens->karma_xpu,        // Deprecated
        HDebugTokens->mltx,
        HDebugTokens->vex,
        HDebugTokens->karma,            // Deprecated
    };
}

TfTokenVector
HDDelegate::GetShaderSourceTypes() const
{
    HDEBUG_TRACE_FUNCTION();
    // For now, just the karma materials
    return {
        HDebugTokens->VEX,      // Compiled VEX
        HDebugTokens->kma,      // Built-in nodes
    };
}

bool
HDDelegate::Pause()
{
    HDEBUG_TRACE_FUNCTION();
    return myPaused.fetch_add(1) == 0;
}

bool
HDDelegate::Resume()
{
    HDEBUG_TRACE_FUNCTION();
    return myPaused.fetch_add(-1) == 1;
}

PXR_NAMESPACE_CLOSE_SCOPE
