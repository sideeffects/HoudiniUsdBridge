/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	XUSD_ImagingEngineHusk.h ( LIB Library, C++)
 *
 * COMMENTS:
 */

#include "XUSD_ImagingEngineHusk.h"
#include "XUSD_HuskTaskManager.h"
#include "XUSD_Format.h"
#include <UT/UT_ErrorLog.h>
#include <UT/UT_Debug.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

#define ENABLE_DRAW_MODES	false

XUSD_ImagingEngineHusk::XUSD_ImagingEngineHusk(const Parameters &params)
    : XUSD_ImagingEngine(params)
    , myRenderTags()
    , myComplexity(8)
    , mySceneMaterials(true)
    , mySceneLights(true)
{
}

XUSD_ImagingEngineHusk::~XUSD_ImagingEngineHusk()
{
}


bool
XUSD_ImagingEngineHusk::isUsingGLCoreProfile() const
{
    return false;
}

void
XUSD_ImagingEngineHusk::doRender()
{
    myTaskManager->SetRenderTags(myRenderTags);

    HdTaskSharedPtrVector tasks = myTaskManager->GetRenderingTasks();
    _engine->Execute(_renderIndex.get(), &tasks);

    // TODO: Check error status of engine to see if there's an error.
}

void
XUSD_ImagingEngineHusk::DispatchRender(const UsdPrim& root,
        const XUSD_ImagingRenderParams &params)
{
    TF_VERIFY(myTaskManager.get());

    myRenderTags = params.myRenderTags;
    myComplexity = SYSclamp(int(params.myComplexity), 0, 9);
    mySceneMaterials = params.myEnableSceneMaterials;
    mySceneLights = params.myEnableSceneLights;

    prepareBatch(root, params.myFrame);

    // XXX(UsdImagingPaths): Is it correct to map USD root path directly
    // to the cachePath here?
    SdfPath cachePath = root.GetPath();
    SdfPathVector roots = {
        root.GetPath().ReplacePrefix(
            SdfPath::AbsoluteRootPath(), _sceneDelegateId)
    };

    updateHydraCollection(_renderCollection, roots);
    myTaskManager->SetCollection(_renderCollection);
}

void
XUSD_ImagingEngineHusk::CompleteRender(const XUSD_ImagingRenderParams &, bool)
{
    doRender();
}

bool
XUSD_ImagingEngineHusk::IsConverged() const
{
    UT_ASSERT(myTaskManager);
    return myTaskManager->IsConverged();
}

HdRenderBuffer *
XUSD_ImagingEngineHusk::GetRenderOutput(TfToken const &name)
{
    UT_ASSERT(myTaskManager);
    return myTaskManager->GetRenderOutput(name);
}

bool
XUSD_ImagingEngineHusk::GetRawResource(HdRenderBuffer *buffer,
        exint &id, exint &width, exint &height)
{
    return false;
}

void
XUSD_ImagingEngineHusk::SetRenderViewport(GfVec4d const& viewport)
{
    myTaskManager->SetRenderViewport(viewport);
}

void
XUSD_ImagingEngineHusk::SetCameraPath(SdfPath const& id)
{
    _cameraPath = id;
    if (myTaskManager)
        myTaskManager->setCamera(_cameraPath);
    setCameraForSampling(id);
}

void
XUSD_ImagingEngineHusk::SetCameraState(const GfMatrix4d& viewMatrix,
        const GfMatrix4d& projectionMatrix)
{
}

void
XUSD_ImagingEngineHusk::SetLightingState(UT_Array<XUSD_GLSimpleLight> const &lights,
        GfVec4f const &sceneAmbient)
{
}

bool
XUSD_ImagingEngineHusk::DecodeIntersections(UT_Array<HUSD_RenderKey> &inOutKeys,
        SdfPathVector &outHitPrimPaths,
        std::vector<HdInstancerContext> &outHitInstancerContexts)
{
    return false;
}

bool
XUSD_ImagingEngineHusk::SetRendererPlugin(TfToken const &id,
        const HdRenderSettingsMap &settings)
{
    // Stash the old controller state
    GfMatrix4d  rootTransform;
    bool        rootVisibility;
    getRoot(rootTransform, rootVisibility);

    HdPluginRenderDelegateUniqueHandle plugin = createDelegate(id, settings);

    if (!plugin)
        return false;

    if (plugin.Get() == _renderDelegate.Get())
    {
        // It's a no-op to load the same plugin twice.
        return true;
    }

    _renderDelegate = std::move(plugin);

    const std::string renderInstanceId = createAppSceneIndices(_renderDelegate);

    _renderIndex.reset(HdRenderIndex::New(_renderDelegate.Get(),
                HdDriverVector(), renderInstanceId));
    myTaskManager = UTmakeUnique<XUSD_HuskTaskManager>(
                _renderIndex.get(),
                _ComputeControllerPath(_renderDelegate),
                _cameraPath);

    createSceneAPI(false, false);

    // Reload the saved state
    setRoot(rootTransform, rootVisibility);

    return true;
}

void
XUSD_ImagingEngineHusk::DestroyHydraResources()
{
    myTaskManager.reset(nullptr);
    destroyCommonHydraResources();
}

bool
XUSD_ImagingEngineHusk::SetRendererAovs(TfTokenVector const& ids)
{
    TF_VERIFY(_renderIndex);
    if (!_renderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer))
        return false;

    TfTokenVector       aovs;
    HdAovDescriptorList descs;
    for (const auto &id : ids)
    {
        HdAovDescriptor d = _renderDelegate->GetDefaultAovDescriptor(id);
        if (d.format != HdFormatInvalid)
        {
            aovs.push_back(id);
            descs.push_back(d);
        }
    }
    if (!aovs.size())
        return false;

    return SetRendererAovsDescs(aovs, descs);
}

bool
XUSD_ImagingEngineHusk::SetRendererAovsDescs(TfTokenVector const& aovs,
        HdAovDescriptorList const& descs)
{
    TF_VERIFY(_renderIndex);
    if (!_renderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer))
        return false;

    myTaskManager->SetRenderOutputs(aovs, descs);
    return true;
}

HgiTextureHandle
XUSD_ImagingEngineHusk::GetAovTexture(TfToken const& name) const
{
    return HgiTextureHandle();
}

void
XUSD_ImagingEngineHusk::SetRendererSetting(TfToken const& id,
        VtValue const& value)
{
    TF_VERIFY(_renderDelegate);
    _renderDelegate->SetRenderSetting(id, value);
}

void
XUSD_ImagingEngineHusk::SetRenderOutputSettings(TfToken const &name,
        HdAovDescriptor const& desc)
{
    UT_ASSERT(0 && "Unimplemented function - coding error?");
}

bool
XUSD_ImagingEngineHusk::IsPauseRendererSupported() const
{
    TF_VERIFY(_renderDelegate);
    return _renderDelegate->IsPauseSupported();
}

bool
XUSD_ImagingEngineHusk::PauseRenderer()
{
    TF_VERIFY(_renderDelegate);
    return _renderDelegate->Pause();
}

bool
XUSD_ImagingEngineHusk::ResumeRenderer()
{
    TF_VERIFY(_renderDelegate);
    return _renderDelegate->Resume();
}

bool
XUSD_ImagingEngineHusk::IsStopRendererSupported() const
{
    TF_VERIFY(_renderDelegate);
    return _renderDelegate->IsStopSupported();
}

bool
XUSD_ImagingEngineHusk::StopRenderer()
{
    TF_VERIFY(_renderDelegate);
    return _renderDelegate->Stop();
}

bool
XUSD_ImagingEngineHusk::RestartRenderer()
{
    TF_VERIFY(_renderDelegate);
    return _renderDelegate->Restart();
}

void
XUSD_ImagingEngineHusk::InvokeRendererCommand(const UT_StringHolder &command_name) const
{
    if (_renderDelegate)
        _renderDelegate->InvokeCommand(TfToken(command_name.toStdString()));
}

bool
XUSD_ImagingEngineHusk::PollForAsynchronousUpdates() const
{
    return false;
}

// --------------------------------------------------------------------------
//              Protected methods
// --------------------------------------------------------------------------
void
XUSD_ImagingEngineHusk::prepareBatch(const UsdPrim &root, fpreal frame)
{
    HD_TRACE_FUNCTION();

    if (_CanPrepare(root))
    {
        preSetTime(root);
        // SetTime will only react if time actually changes.
        setTime(frame);
        postSetTime(root);

        populateScene(root, ENABLE_DRAW_MODES);
    }
}

bool
XUSD_ImagingEngineHusk::updateHydraCollection(
        HdRprimCollection &collection,
        const SdfPathVector &roots)
{
    // choose repr
    HdReprSelector reprSelector = HdReprSelector(HdReprTokens->refined);

    // By default our main collection will be called geometry
    TfToken colName = HdTokens->geometry;

    // Check if the collection needs to be updated (so we can avoid the sort).
    const SdfPathVector &oldRoots = collection.GetRootPaths();

    // inexpensive comparison first
    bool match = collection.GetName() == colName &&
                 oldRoots.size() == roots.size() &&
                 collection.GetReprSelector() == reprSelector;

    // Only take the time to compare root paths if everything else matches.
    if (match)
    {
        // Note that oldRoots is guaranteed to be sorted.
        for(size_t i = 0; i < roots.size(); i++)
	{
            // Avoid binary search when both vectors are sorted.
            if (oldRoots[i] == roots[i])
                continue;
            // Binary search to find the current root.
            if (!std::binary_search(oldRoots.begin(), oldRoots.end(), roots[i]))
            {
                match = false;
                break;
            }
        }

        // if everything matches, do nothing.
        if (match)
	    return false;
    }

    // Recreate the collection.
    collection = HdRprimCollection(colName, reprSelector);
    collection.SetRootPaths(roots);

    return true;
}

void
XUSD_ImagingEngineHusk::preSetTime(const UsdPrim &root)
{
    HD_TRACE_FUNCTION();

    enableMaterialsLights(mySceneMaterials, mySceneLights);
    setRefineLevel(myComplexity);
    applyPendingUpdates();
}

void
XUSD_ImagingEngineHusk::postSetTime(const UsdPrim &)
{
    HD_TRACE_FUNCTION();
}

PXR_NAMESPACE_CLOSE_SCOPE
