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
 * NAME:	XUSD_ImagingEngine.C (HUSD Library, C++)
 */

#include "XUSD_ApexAnimateSceneIndex.h"
#include "XUSD_HdGpMutingSceneIndex.h"
#include "XUSD_ImagingEngine.h"
#include "XUSD_ImagingEngineHusk.h"

#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "XUSD_Format.h"
#include <FS/UT_DSO.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_String.h>
#include <UT/UT_UniquePtr.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/sceneIndexUtil.h>
#include <pxr/imaging/hd/utils.h>
#include <pxr/imaging/hdGp/generativeProceduralResolvingSceneIndex.h>
#include <pxr/imaging/hdsi/sceneGlobalsSceneIndex.h>
#include <pxr/imaging/hdsi/domeLightCameraVisibilitySceneIndex.h>
#include <pxr/usd/usdRender/tokens.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>
#include <pxr/imaging/hdsi/sceneMaterialPruningSceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

typedef XUSD_ImagingEngine *(*XUSD_ImagingEngineCreator)(
        const XUSD_ImagingEngine::Parameters&);

struct XUSD_ImagingEngine::_AppSceneIndices
{
    HdsiSceneGlobalsSceneIndexRefPtr     sceneGlobalsSceneIndex;
    XUSD_ApexAnimateSceneIndexRefPtr     apexAnimateSceneIndex;
    HdsiDomeLightCameraVisibilitySceneIndexRefPtr
                                         domeLightCameraVisibilitySceneIndex;
    HdsiSceneMaterialPruningSceneIndexRefPtr sceneMaterialPruningSceneIndex;
    XUSD_HdGpMutingSceneIndexRefPtr hdGpMutingSceneIndex;

    _AppSceneIndicesSharedPtr
    static New(const TfToken &renderInstanceId)
    {
        // Register application managed scene indices via the callback
        // facility which will be invoked during render index construction.
        static std::once_flag registerOnce;
        std::call_once(registerOnce, _Register);

        auto result = std::make_shared<_AppSceneIndices>(renderInstanceId);
        _GetTracker().RegisterInstance(renderInstanceId, result);
        return result;
    }

    _AppSceneIndices(const TfToken &renderInstanceId)
     : _renderInstanceId(renderInstanceId) { }

    ~_AppSceneIndices()
    {
        _GetTracker().UnregisterInstance(_renderInstanceId);
    }

private:
    const TfToken _renderInstanceId;

    using _Tracker = HdUtils::RenderInstanceTracker<_AppSceneIndices>;
    static _Tracker &_GetTracker()
    {
        static _Tracker tracker;
        return tracker;
    }

    HdSceneIndexBaseRefPtr _Append(
        const HdSceneIndexBaseRefPtr &inputScene)
    {
        HdSceneIndexBaseRefPtr sceneIndex = inputScene;

        sceneIndex =
            sceneGlobalsSceneIndex =
                HdsiSceneGlobalsSceneIndex::New(sceneIndex);

        sceneIndex =
            domeLightCameraVisibilitySceneIndex =
                HdsiDomeLightCameraVisibilitySceneIndex::New(sceneIndex);

        sceneIndex =
            sceneMaterialPruningSceneIndex =
                HdsiSceneMaterialPruningSceneIndex::New(sceneIndex);

        return sceneIndex;
    }

    static HdSceneIndexBaseRefPtr _AppendCallback(
        const std::string &renderInstanceId,
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs)
    {
        _AppSceneIndicesSharedPtr const instance =
            _GetTracker().GetInstance(renderInstanceId);
        if (!instance) {
            TF_CODING_ERROR("Did not find appSceneIndices instance for %s,",
                            renderInstanceId.c_str());
            return inputScene;
        }
        return instance->_Append(inputScene);
    }

    static HdSceneIndexBaseRefPtr
    _AppendApexAnimateCallback(
        const std::string &renderInstanceId,
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs)
    {
        _AppSceneIndicesSharedPtr const instance =
            _GetTracker().GetInstance(renderInstanceId);
        if (!instance) {
            TF_CODING_ERROR("Did not find appSceneIndices instance for %s,",
                            renderInstanceId.c_str());
            return inputScene;
        }
        auto &sceneIndex = instance->apexAnimateSceneIndex;
        sceneIndex = XUSD_ApexAnimateSceneIndex::New(inputScene);
        sceneIndex->SetDisplayName("APEX Animate Scene Index");
        return sceneIndex;
    }

    static HdSceneIndexBaseRefPtr
    _AppendHdGpMutingSceneIndexCallback(
        const std::string &renderInstanceId,
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs)
    {
        _AppSceneIndicesSharedPtr indices =
            _GetTracker().GetInstance(renderInstanceId);
        if (indices)
        {
            auto &mutingSceneIndex = indices->hdGpMutingSceneIndex;
            mutingSceneIndex = XUSD_HdGpMutingSceneIndex::New(inputScene);
            auto resolverSceneIndex = 
                HdGpGenerativeProceduralResolvingSceneIndex::New(mutingSceneIndex);
            auto encapsulation = HdMakeEncapsulatingSceneIndex(
                { inputScene }, resolverSceneIndex);
            encapsulation->SetDisplayName("HdGp Resolver Scene Index");
            return encapsulation;
        }
        TF_CODING_ERROR("Did not find appSceneIndices instance for %s",
                        renderInstanceId.c_str());

        return inputScene;
    }

    static void _Register()
    {
        // Insert earlier so downstream scene indices can query and be notified
        // of changes and also declare their dependencies (e.g., to support
        // rendering color spaces).
        const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

        // Note:
        // The pattern used below registers the static member fn as a callback,
        // which retreives the scene index instance using the
        // renderInstanceId argument of the callback.

        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            std::string(), // empty string implies all renderers
            &_AppendCallback,
            /* inputArgs = */ nullptr,
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart
        );

        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
                std::string(), // empty string implies all renderers
                &_AppendApexAnimateCallback,
                /* inputArgs = */ nullptr,
                insertionPhase,
                HdSceneIndexPluginRegistry::InsertionOrderAtStart
        );

        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            std::string(), // empty string implies all renderers
            &_AppendHdGpMutingSceneIndexCallback,
            /* inputArgs = */ nullptr,
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart
        );
    }
};

namespace
{
    // RAII helper to enable and disable notice batching when using the stage scene
    // index.
    class _ScopedHydraNoticeBatch
    {
    public:
        _ScopedHydraNoticeBatch(
             const HdNoticeBatchingSceneIndexRefPtr &noticeBatchingSceneIndex)
            : _noticeBatchingSceneIndex(noticeBatchingSceneIndex)
        {
            if (_noticeBatchingSceneIndex) {
                _noticeBatchingSceneIndex->SetBatchingEnabled(true);
            }
        }

        ~_ScopedHydraNoticeBatch()
        {
            if (_noticeBatchingSceneIndex) {
                _noticeBatchingSceneIndex->SetBatchingEnabled(false);
            }
        }

    private:
        HdNoticeBatchingSceneIndexRefPtr _noticeBatchingSceneIndex;
    };
}

UT_UniquePtr<XUSD_ImagingEngine>
XUSD_ImagingEngine::createImagingEngine(
        const XUSD_ImagingEngine::Parameters &params)
{
    if (!params.enable_gpu_context)
        return UTmakeUnique<XUSD_ImagingEngineHusk>(params);

    static XUSD_ImagingEngineCreator theCreator;

    if (!theCreator)
    {
        const UT_PathSearch *searchpath;
        UT_String            dsopath;

        searchpath = UT_PathSearch::getInstance(UT_HOUDINI_DSO_PATH);
        searchpath->findFile(dsopath, "usdui/USD_UI" FS_DSO_EXTENSION);
        if (dsopath.isstring())
        {
            UT_DSO           dso;
            UT_StringHolder  fullpath;
            void            *funcptr;

            funcptr = dso.findProcedure(dsopath, "newImagingEngine", fullpath);
            theCreator = (XUSD_ImagingEngineCreator)funcptr;
            if (!funcptr)
            {
                UT_WorkBuffer   msg;
                msg.format("Unable to load DSO {}\n", dsopath);
                msg.appendFormat("System configuration error.  {}\n",
                        "Try running with HOUDINI_DSO_ERROR=1");
                HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
                UTformat("{}\n", msg);
                return UT_UniquePtr<XUSD_ImagingEngine>();
            }
        }
    }

    UT_ASSERT(theCreator);
    return UT_UniquePtr<XUSD_ImagingEngine>(theCreator(params));
}

XUSD_ImagingEngine::XUSD_ImagingEngine(const Parameters &params)
    : _hgi()
    , _hgiDriver(params.driver)
    , _rootPath(params.rootPath)
    , _excludedPrimPaths(params.excludedPaths)
    , _invisedPrimPaths(params.invisedPaths)
    , _sceneDelegateId(params.sceneDelegateID)
    , _isPopulated(false)
    , _useSceneIndices(params.use_scene_indices)
    , _enableUsdDrawModes(params.enableUsdDrawMode)
{
}

XUSD_ImagingEngine::~XUSD_ImagingEngine()
{
}

void
XUSD_ImagingEngine::SetWindowPolicy(CameraUtilConformWindowPolicy policy)
{
    if (_useSceneIndices)
    {
        // XXX(USD-7115): window policy
    }
    else
    {
        _sceneDelegate->SetWindowPolicy(policy);
    }
}

/* static */
TfTokenVector
XUSD_ImagingEngine::GetRendererPlugins()
{
    HfPluginDescVector pluginDescriptors;
    HdRendererPluginRegistry::GetInstance().GetPluginDescs(&pluginDescriptors);

    TfTokenVector plugins;
    for(size_t i = 0; i < pluginDescriptors.size(); ++i) {
        plugins.push_back(pluginDescriptors[i].id);
    }
    return plugins;
}

TfToken
XUSD_ImagingEngine::GetCurrentRendererId() const
{
    return _renderDelegate ? _renderDelegate.GetPluginId() : TfToken();
}

TfTokenVector
XUSD_ImagingEngine::GetRendererAovs(const TfTokenVector &candidates) const
{
    TF_VERIFY(_renderIndex);

    if (_renderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer)) {

        TfTokenVector aovs;
        for (auto const& aov : candidates) {
            if (_renderDelegate->GetDefaultAovDescriptor(aov).format
                    != HdFormatInvalid) {
                aovs.push_back(aov);
            }
        }
        return aovs;
    }
    return TfTokenVector();
}

bool
XUSD_ImagingEngine::SetRendererAovsDescs(TfTokenVector const& ids,
                                    HdAovDescriptorList const& descs)
{
    if (!SetRendererAovs(ids))
        return false;
    UT_ASSERT(ids.size() == descs.size());
    if (ids.size() <= descs.size())
    {
        for (size_t i = 0, n = ids.size(); i < n; ++i)
            SetRenderOutputSettings(ids[i], descs[i]);
    }
    return true;
}

HdAovDescriptor
XUSD_ImagingEngine::GetDefaultAovDescriptor(const TfToken &token)
{
    if (_HasRenderer())
        return _renderDelegate->GetDefaultAovDescriptor(token);
    return HdAovDescriptor();
}

VtValue
XUSD_ImagingEngine::GetRendererSetting(TfToken const& id) const
{
    return _renderDelegate->GetRenderSetting(id);
}

void
XUSD_ImagingEngine::GetRendererCommands(UT_StringArray &command_names,
            UT_StringArray &command_descriptions) const
{
    command_names.clear();
    command_descriptions.clear();

    if (_HasRenderer())
    {
        auto commands = _renderDelegate->GetCommandDescriptors();
        for (auto &&command : commands)
        {
            command_names.append(command.commandName.GetString());
            command_descriptions.append(command.commandDescription);
        }
    }
}

bool
XUSD_ImagingEngine::_HasRenderer() const
{
    return bool(_renderDelegate);
}


HdSceneIndexBaseRefPtr
XUSD_ImagingEngine::_GetTerminalSceneIndex() const
{
    if (!_renderIndex) {
        return nullptr;
    }
    return _renderIndex->GetTerminalSceneIndex();
}

SdfPath
XUSD_ImagingEngine::GetActiveRenderPassPrimPath() const
{
    HdSceneIndexBaseRefPtr const terminalSceneIndex =
        _GetTerminalSceneIndex();
    if (ARCH_UNLIKELY(!terminalSceneIndex)) {
        return SdfPath::EmptyPath();
    }

    SdfPath activeRenderPassPath;
    if (HdUtils::HasActiveRenderPassPrim(
            terminalSceneIndex, &activeRenderPassPath)) {
        return activeRenderPassPath;
            }

    return SdfPath::EmptyPath();
}

SdfPath
XUSD_ImagingEngine::GetActiveRenderSettingsPrimPath() const
{
    HdSceneIndexBaseRefPtr const terminalSceneIndex =
        _GetTerminalSceneIndex();
    if (ARCH_UNLIKELY(!terminalSceneIndex)) {
        return SdfPath::EmptyPath();
    }

    SdfPath activeRenderSettingsPath;
    if (HdUtils::HasActiveRenderSettingsPrim(
            terminalSceneIndex, &activeRenderSettingsPath)) {
        return activeRenderSettingsPath;
            }

    return SdfPath::EmptyPath();
}

/* static */
SdfPathVector
XUSD_ImagingEngine::GetAvailableRenderSettingsPrimPaths(UsdPrim const& root)
{
    // UsdRender OM uses the convention that all render settings prims must
    // live under /Render.
    static const SdfPath renderRoot("/Render");

    const auto stage = root.GetStage();

    SdfPathVector paths;
    if (UsdPrim render = stage->GetPrimAtPath(renderRoot)) {
        for (const UsdPrim child : render.GetChildren()) {
            if (child.IsA<UsdRenderSettings>()) {
                paths.push_back(child.GetPrimPath());
            }
        }
    }
    return paths;
}

void
XUSD_ImagingEngine::SetActiveRenderPassPrimPath(SdfPath const &path)
{
    if (ARCH_UNLIKELY(!_appSceneIndices)) {
        return;
    }
    auto &sgsi = _appSceneIndices->sceneGlobalsSceneIndex;
    if (ARCH_UNLIKELY(!sgsi)) {
        return;
    }

    sgsi->SetActiveRenderPassPrimPath(path);
}

void
XUSD_ImagingEngine::SetActiveRenderSettingsPrimPath(SdfPath const &path)
{
    if (ARCH_UNLIKELY(!_appSceneIndices)) {
        return;
    }
    auto &sgsi = _appSceneIndices->sceneGlobalsSceneIndex;
    if (ARCH_UNLIKELY(!sgsi)) {
        return;
    }

    sgsi->SetActiveRenderSettingsPrimPath(path);
}

void
XUSD_ImagingEngine::SetDomeLightCameraVisibility(bool domeLightCamVisSetting)
{
    if (ARCH_UNLIKELY(!_appSceneIndices)) {
        return;
    }
    auto &sgsi = _appSceneIndices->domeLightCameraVisibilitySceneIndex;
    if (ARCH_UNLIKELY(!sgsi)) {
        return;
    }

    sgsi->SetDomeLightCameraVisibility(domeLightCamVisSetting);
}

void
XUSD_ImagingEngine::updateApexAnimateSceneIndex(
        const UT_StringRef &scene_prim_path,
        const UT_StringMap<GU_ConstDetailHandle> &graph_outputs) const
{
    if (ARCH_UNLIKELY(!_appSceneIndices)) {
        return;
    }
    auto &sceneIndex = _appSceneIndices->apexAnimateSceneIndex;
    if (ARCH_UNLIKELY(!sceneIndex)) {
        return;
    }

    sceneIndex->updateFromGraphOutputs(scene_prim_path, graph_outputs);
}

void
XUSD_ImagingEngine::showHdGpProcedurals(bool show) const
{
    if (ARCH_UNLIKELY(!_appSceneIndices)) {
        return;
    }
    auto &sceneIndex = _appSceneIndices->hdGpMutingSceneIndex;
    if (ARCH_UNLIKELY(!sceneIndex)) {
        return;
    }

    sceneIndex->setActive(!show);
}

VtDictionary
XUSD_ImagingEngine::GetRenderStats() const
{
    TF_VERIFY(_HasRenderer());
    VtDictionary stats = _renderDelegate->GetRenderStats();
    auto delegate_key = HUSD_Constants::getRenderStatsDelegateKey().toStdString();
    if (!VtDictionaryIsHolding<TfToken>(stats, delegate_key))
        stats[delegate_key] = _renderDelegate.GetPluginId();
    return stats;
}

/* static */
std::string
XUSD_ImagingEngine::GetRendererDisplayName(TfToken const &id)
{
    HfPluginDesc pluginDescriptor;
    if (!TF_VERIFY(HdRendererPluginRegistry::GetInstance().
                   GetPluginDesc(id, &pluginDescriptor))) {
        return std::string();
    }

    return pluginDescriptor.displayName;
}

// ------------------  protected methods ------------------------------
bool
XUSD_ImagingEngine::_CanPrepare(const UsdPrim& root)
{
    HD_TRACE_FUNCTION();

    if (!TF_VERIFY(root, "Attempting to draw an invalid/null prim\n")) 
        return false;

    if (!root.GetPath().HasPrefix(_rootPath)) {
        TF_CODING_ERROR("Attempting to draw path <%s>, but engine is rooted"
                    "at <%s>\n",
                    root.GetPath().GetText(),
                    _rootPath.GetText());
        return false;
    }

    return true;
}

void
XUSD_ImagingEngine::_SetActiveRenderSettingsPrimFromStageMetadata(UsdStageWeakPtr stage)
{
    if (TF_VERIFY(stage)) {
        return;
    }

    HdSceneIndexBaseRefPtr const terminalSceneIndex =
        _GetTerminalSceneIndex();
    if (!TF_VERIFY(terminalSceneIndex)) {
        return;
    }

    // If we already have an opinion, skip the stage metadata.
    if (!HdUtils::HasActiveRenderSettingsPrim(terminalSceneIndex)) {

        std::string pathStr;
        if (stage->HasAuthoredMetadata(
                    UsdRenderTokens->renderSettingsPrimPath)) {
            stage->GetMetadata(
                    UsdRenderTokens->renderSettingsPrimPath, &pathStr);
        }
        // Add the delegateId prefix since the scene globals scene index is
        // inserted into the merging scene index.
        if (!pathStr.empty()) {
            SetActiveRenderSettingsPrimPath(
                    SdfPath(pathStr).ReplacePrefix(
                            SdfPath::AbsoluteRootPath(), _sceneDelegateId));
        }
    }
}

void
XUSD_ImagingEngine::_SetSceneGlobalsCurrentFrame(UsdTimeCode const &time)
{
    if (ARCH_UNLIKELY(!_appSceneIndices)) {
        return;
    }
    auto &sgsi = _appSceneIndices->sceneGlobalsSceneIndex;
    if (ARCH_UNLIKELY(!sgsi)) {
        return;
    }

    sgsi->SetCurrentFrame(time.GetValue());
}

SdfPath
XUSD_ImagingEngine::_ComputeControllerPath(
        const TfToken &pluginId)
{
    const std::string pluginIdStr =
        TfMakeValidIdentifier(pluginId.GetText());
    const TfToken rendererName(
        TfStringPrintf("_UsdImaging_%s_%p", pluginIdStr.c_str(), this));

    return _sceneDelegateId.AppendChild(rendererName);
}

SdfPath
XUSD_ImagingEngine::_ComputeControllerPath(
        const HdPluginRenderDelegateUniqueHandle &renderDelegate)
{
    return _ComputeControllerPath(renderDelegate.GetPluginId());
}

HdSceneIndexBaseRefPtr
XUSD_ImagingEngine::_AppendOverridesSceneIndices(
        HdSceneIndexBaseRefPtr const &inputScene)
{
    HdSceneIndexBaseRefPtr sceneIndex = inputScene;

    static HdContainerDataSourceHandle const lightPruningInputArgs =
            HdRetainedContainerDataSource::New(
                    HdsiPrimTypeAndPathPruningSceneIndexTokens->primTypes,
                    HdRetainedTypedSampledDataSource<TfTokenVector>::New(
                            HdLightPrimTypeTokens()));

    sceneIndex = _lightPruningSceneIndex =
            HdsiPrimTypeAndPathPruningSceneIndex::New(
                    sceneIndex, lightPruningInputArgs);

    // _lightPruningSceneIndex comes with empty preidcate which corresponds to
    // enableSceneLights = true.
    _lightPruningSceneIndexEnableSceneLights = true;

    sceneIndex = _rootOverridesSceneIndex =
            UsdImagingRootOverridesSceneIndex::New(sceneIndex);

    sceneIndex = _legacyRenderSettingsSceneIndex =
            UsdImagingLegacyRenderSettingsSceneIndex::New(sceneIndex);

    return sceneIndex;
}

std::string
XUSD_ImagingEngine::createAppSceneIndices(
        const HdPluginRenderDelegateUniqueHandle &delegate)
{
    std::string renderInstanceId =
        TfStringPrintf("%s (%p)",
                delegate->GetRendererDisplayName().c_str(),
                (void *)delegate.Get());
    _appSceneIndices = _AppSceneIndices::New(
        TfToken(renderInstanceId.c_str()));

    return renderInstanceId;
}

void
XUSD_ImagingEngine::createSceneAPI(bool display_unloaded,
        bool enable_usd_drawmodes)
{
    // Create the new scene API
    if (_useSceneIndices) {
        UsdImagingCreateSceneIndicesInfo info;
        info.addDrawModeSceneIndex = enable_usd_drawmodes;
        info.displayUnloadedPrimsWithBounds = display_unloaded;
        info.overridesSceneIndexCallback =
                std::bind(
                        &XUSD_ImagingEngine::_AppendOverridesSceneIndices,
                        this, std::placeholders::_1);

        const UsdImagingSceneIndices sceneIndices =
                UsdImagingCreateSceneIndices(info);

        _stageSceneIndex = sceneIndices.stageSceneIndex;
        _postInstancingNoticeBatchingSceneIndex =
            sceneIndices.postInstancingNoticeBatchingSceneIndex;
        _selectionSceneIndex = sceneIndices.selectionSceneIndex;
        _sceneIndex = sceneIndices.finalSceneIndex;

        _sceneIndex = _displayStyleSceneIndex =
                HdsiLegacyDisplayStyleOverrideSceneIndex::New(_sceneIndex);

        _renderIndex->InsertSceneIndex(_sceneIndex, _sceneDelegateId);
    } else {
        _sceneDelegate = std::make_unique<UsdImagingDelegate>(
                _renderIndex.get(), _sceneDelegateId);
        _sceneDelegate->SetDisplayUnloadedPrimsWithBounds(display_unloaded);
        _sceneDelegate->SetUsdDrawModesEnabled(enable_usd_drawmodes);
        _sceneDelegate->SetCameraForSampling(_cameraPath);
    }

    _engine = std::make_unique<HdEngine>();
}

void
XUSD_ImagingEngine::destroyCommonHydraResources()
{
    _engine = nullptr;
    if (_useSceneIndices) {
        if (_renderIndex && _sceneIndex) {
            // Remove the terminal scene index of the UsdImaging scene
            // index graph from the render index's merging scene index.
            // This should result in removed/added notices that are
            // processed by downstream scene index plugins.
            _renderIndex->RemoveSceneIndex(_sceneIndex);

            // The destruction order below is the reverse of the creation
            // order.
            _sceneIndex = nullptr;
            _displayStyleSceneIndex = nullptr;
            _selectionSceneIndex = nullptr;

            // "Override" scene indices.
            _legacyRenderSettingsSceneIndex = nullptr;
            _rootOverridesSceneIndex = nullptr;
            _lightPruningSceneIndex = nullptr;

            _stageSceneIndex = nullptr;
        }
    } else {
        _sceneDelegate = nullptr;
    }

    _appSceneIndices = nullptr;

    _renderIndex = nullptr;
    _renderDelegate = nullptr;
}

void
XUSD_ImagingEngine::populateScene(const UsdPrim &root, bool enable_usd_draw_modes)
{
    if (!_isPopulated)
    {
        auto stage = root.GetStage();
        if (_useSceneIndices)
        {
            _ScopedHydraNoticeBatch noticeBatch(
                    _postInstancingNoticeBatchingSceneIndex);
            TF_VERIFY(_stageSceneIndex);
            _stageSceneIndex->SetStage(stage);

            // XXX(USD-7113): Add pruning based on _rootPath,

            // XXX(USD-7114): Add draw mode support based on
            // params.enableUsdDrawModes.

            // XXX(USD-7115): Add invis overrides from _invisedPrimPaths.
        }
        else
        {
            TF_VERIFY(_sceneDelegate);
            _sceneDelegate->SetUsdDrawModesEnabled(enable_usd_draw_modes);
            _sceneDelegate->Populate(root.GetStage()->GetPrimAtPath(_rootPath),
                _excludedPrimPaths);
            _sceneDelegate->SetInvisedPrimPaths(_invisedPrimPaths);

            // This is only necessary when using the legacy scene delegate.
            // The stage scene index provides this functionality.
            _SetActiveRenderSettingsPrimFromStageMetadata(stage);
        }
        _isPopulated = true;
    }
}

void
XUSD_ImagingEngine::setTime(fpreal frame)
{
    if (_useSceneIndices)
        _stageSceneIndex->SetTime(frame);
    else
        _sceneDelegate->SetTime(frame);

    _SetSceneGlobalsCurrentFrame(frame);
}

void
XUSD_ImagingEngine::setCameraForSampling(SdfPath const &id)
{
    // The camera that is set for viewing will also be used for
    // time sampling.
    // XXX(HYD-2304): motion blur shutter window.
    if (_useSceneIndices)
        setPrimaryCameraPathOnSceneGlobals(id);
    else if (_sceneDelegate)
        _sceneDelegate->SetCameraForSampling(id);
}

void
XUSD_ImagingEngine::setPrimaryCameraPathOnSceneGlobals(SdfPath const &id)
{
    // Set camera path on HdsiSceneGlobalsSceneIndex. Only meaningful on the
    // scene-index path; deliberately a no-op otherwise so callers (e.g. free
    // camera in XUSD_ImagingEngineGL::SetCameraState) don't accidentally route
    // to the legacy delegate's SetCameraForSampling for cameras that don't
    // exist there.
    if (_appSceneIndices)
    {
        if (auto &sgsi = _appSceneIndices->sceneGlobalsSceneIndex)
            sgsi->SetPrimaryCameraPrimPath(id);
    }
}

HdPluginRenderDelegateUniqueHandle
XUSD_ImagingEngine::createDelegate(const TfToken &token,
                                        const HdRenderSettingsMap &settings)
{
    auto &&reg = HdRendererPluginRegistry::GetInstance();
    TfToken     actualId = token;

    if (actualId.IsEmpty())
    {
        actualId = reg.GetDefaultPluginId();
        if (actualId.IsEmpty())
            return HdPluginRenderDelegateUniqueHandle();
        UT_ErrorLog::warning("Selected {} as the render delegate", actualId);
    }

    // Preload libraries
    HUSD_RendererInfo::getRendererInfo(
            actualId.GetText(), UT_StringHolder()).preloadLibraries();
    // Try to create the delegate
    auto plugin = reg.CreateRenderDelegate(actualId, settings);
    if (!plugin)
    {
        HfPluginDescVector      plugins;
        reg.GetPluginDescs(&plugins);
        for (const auto &p : plugins)
        {
            if (p.displayName == actualId)
            {
                // Preload libraries
                HUSD_RendererInfo::getRendererInfo(
                        p.id.GetText(), UT_StringHolder()).preloadLibraries();
                plugin = reg.CreateRenderDelegate(p.id, settings);
                if (plugin)
                {
                    actualId = p.id;
                    break;
                }
            }
        }
    }
    return plugin;
}

void
XUSD_ImagingEngine::enableMaterialsLights(bool enable_materials, bool enable_lights)
{
    if (_useSceneIndices)
    {
        if (_appSceneIndices) {
            if (HdsiSceneMaterialPruningSceneIndexRefPtr const &si =
                _appSceneIndices->sceneMaterialPruningSceneIndex)
            {
                si->SetEnabled(!enable_materials);
            }
        }
        if (_lightPruningSceneIndexEnableSceneLights != enable_lights) {
            _lightPruningSceneIndex->SetPathPredicate(
                enable_lights
                    // Empty predicate means we prune nothing.
                    ? HdsiPrimTypeAndPathPruningSceneIndex::PathPredicate()
                    // Predicate matches every path.
                    // Thus, scene index prunes every prim
                    // matching the prim types given earlier.
                    : [](const SdfPath &a) { return true; });
            _lightPruningSceneIndexEnableSceneLights = enable_lights;
        }
    }
    else
    {
        _sceneDelegate->SetSceneMaterialsEnabled(enable_materials);
        _sceneDelegate->SetSceneLightsEnabled(enable_lights);
    }
}

void
XUSD_ImagingEngine::setRefineLevel(int refineLevel)
{
    if (_useSceneIndices)
        _displayStyleSceneIndex->SetRefineLevelFallback(refineLevel);
    else
        _sceneDelegate->SetRefineLevelFallback(refineLevel);
}

void
XUSD_ImagingEngine::applyPendingUpdates()
{
    if (_useSceneIndices)
    {
        _ScopedHydraNoticeBatch noticeBatch(
                _postInstancingNoticeBatchingSceneIndex);
        _stageSceneIndex->ApplyPendingUpdates();
    }
    else
        _sceneDelegate->ApplyPendingUpdates();
}

void
XUSD_ImagingEngine::getRoot(GfMatrix4d &xform, bool &visible) const
{
    xform = GfMatrix4d(1.0);
    visible = true;
    if (_useSceneIndices)
    {
        if (_rootOverridesSceneIndex)
        {
            xform = _rootOverridesSceneIndex->GetRootTransform();
            visible = _rootOverridesSceneIndex->GetRootVisibility();
        }
    }
    else
    {
        if (_sceneDelegate)
        {
            xform = _sceneDelegate->GetRootTransform();
            visible = _sceneDelegate->GetRootVisibility();
        }
    }
}

void
XUSD_ImagingEngine::setRoot(const GfMatrix4d &xform, bool visible)
{
    if (_useSceneIndices)
    {
        _rootOverridesSceneIndex->SetRootTransform(xform);
        _rootOverridesSceneIndex->SetRootVisibility(visible);
    }
    else
    {
        _sceneDelegate->SetRootTransform(xform);
        _sceneDelegate->SetRootVisibility(visible);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
