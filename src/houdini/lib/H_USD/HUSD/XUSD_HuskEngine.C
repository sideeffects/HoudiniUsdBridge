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
#include "XUSD_HuskEngine.h"
#include <UT/UT_Assert.h>
#include <UT/UT_Debug.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONWriter.h>
#include <FS/FS_Info.h>
#include <PY/PY_AutoObject.h>

#include "XUSD_Format.h"

#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/base/gf/size2.h>
#include <pxr/base/tf/pyPtrHelpers.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/usdImaging/usdImaging/delegate.h>

#include "XUSD_HuskTaskManager.h"
#include "HUSD_RenderSettings.h"
#include "XUSD_RenderSettings.h"

#define ENABLE_DRAW_MODES	false

PXR_NAMESPACE_OPEN_SCOPE

//----------------------------------------------------------------------------
// Construction
//----------------------------------------------------------------------------

XUSD_HuskEngine::XUSD_HuskEngine()
    : myRenderIndex()
    , myDelegateId(SdfPath::AbsoluteRootPath())
    , myDelegate()
    , myPlugin(nullptr)
    , myTaskManager()
    , myRootPath(SdfPath::AbsoluteRootPath())
    , myExcludedPrimPaths()
    , myInvisedPrimPaths()
    , myIsPopulated(false)
    , myRenderTags()
    , myComplexity(COMPLEXITY_VERYHIGH)
    , myUSDTimeStamp(0)
    , myPercentDone(0)
{
}

XUSD_HuskEngine::~XUSD_HuskEngine()
{
    deleteHydraResources();
}

//----------------------------------------------------------------------------
// Rendering
//----------------------------------------------------------------------------

void
XUSD_HuskEngine::PrepareBatch(const UsdPrim &root, fpreal frame)
{
    HD_TRACE_FUNCTION();

    TF_VERIFY(myDelegate);

    if (canPrepareBatch(root))
    {
        if (!myIsPopulated)
	{
            myDelegate->SetUsdDrawModesEnabled(ENABLE_DRAW_MODES);
            myDelegate->Populate(root.GetStage()->GetPrimAtPath(myRootPath),
                               myExcludedPrimPaths);
            myDelegate->SetInvisedPrimPaths(myInvisedPrimPaths);
            myIsPopulated = true;
        }

        preSetTime(root);
        // SetTime will only react if time actually changes.
        myDelegate->SetTime(frame);
        postSetTime(root);
    }
}

bool
XUSD_HuskEngine::loadStage(const UT_StringHolder &usdfile,
                        const UT_StringHolder &resolver_context_file)
{
    UT_ErrorLog::format(2, "Loading {}", usdfile);
    ArResolverContext resolver_context;

    if (resolver_context_file.isstring())
    {
        UT_ErrorLog::format(2, "Resolver context: {}", resolver_context_file);
        resolver_context = ArGetResolver().CreateDefaultContextForAsset(
            resolver_context_file.toStdString());
    }
    else
        resolver_context = ArGetResolver().CreateDefaultContext();

    {
        std::string resolved = ArGetResolver().Resolve(usdfile.toStdString());
        myUSDTimeStamp = 0;
        if (!resolved.empty())
        {
            FS_Info     fstat(resolved.c_str());
            myUSDTimeStamp = 0;
            if (fstat.exists())
                myUSDTimeStamp = fstat.getModTime();
        }
    }

    myUSDFile = usdfile;
    myStage = UsdStage::Open(usdfile.toStdString(), resolver_context);
    if (!myStage)
	UT_ErrorLog::error("Unable to load USD file '{}'", usdfile);
    return myStage;
}

bool
XUSD_HuskEngine::isValid() const
{
    return myStage && myStage->GetPseudoRoot();
}

fpreal
XUSD_HuskEngine::stageFPS() const
{
    return myStage ? myStage->GetTimeCodesPerSecond() : 24;
}

PY_PyObject *
XUSD_HuskEngine::pyStage() const
{
    return (PY_PyObject *)TfMakePyPtr<UsdStageWeakPtr>::Execute(myStage).first;
}

namespace
{
    static PY_PyObject *
    xusd_PyObject(const UT_JSONValue &value)
    {
        const UT_StringHolder   *str;
        switch (value.getType())
        {
            case UT_JSONValue::JSON_NULL:
                return PY_Py_None();
            case UT_JSONValue::JSON_BOOL:
                return value.getB() ? PY_Py_True() : PY_Py_False();
            case UT_JSONValue::JSON_INT:
                return PY_PyInt_FromLong(value.getI());
            case UT_JSONValue::JSON_REAL:
                return PY_PyFloat_FromDouble(value.getF());
            case UT_JSONValue::JSON_STRING:
                str = value.getStringHolder();
                UT_ASSERT(str);
                return str
                    ? PY_PyString_FromStringAndSize(str->c_str(), str->length())
                    : PY_Py_None();
            case UT_JSONValue::JSON_KEY:
                str = value.getKeyHolder();
                UT_ASSERT(str);
                return str
                    ? PY_PyString_FromStringAndSize(str->c_str(), str->length())
                    : PY_Py_None();
            case UT_JSONValue::JSON_ARRAY:
            {
                const UT_JSONValueArray *jarr = value.getArray();
                exint           size = jarr ? jarr->size() : 0;
                PY_PyObject     *parr = PY_PyTuple_New(size);
                for (exint i = 0; i < size; ++i)
                {
                    UT_VERIFY(!PY_PyTuple_SetItem(parr,
                                i,
                                xusd_PyObject(*jarr->get(i))));
                }
                return parr;
            }
            case UT_JSONValue::JSON_MAP:
            {
                const UT_JSONValueMap   *jmap = value.getMap();
                UT_StringArray           keys;
                if (jmap)
                    jmap->getKeyReferences(keys);
                exint            size = keys.size();
                PY_PyObject     *pmap = PY_PyDict_New();
                for (exint i = 0; i < size; ++i)
                {
                    UT_VERIFY(!PY_PyDict_SetItemString(pmap,
                                keys[i].c_str(),
                                xusd_PyObject(*jmap->get(i))));
                }
                return pmap;
            }
        }
        UTdebugFormat("NONE???");
        return PY_Py_None();
    }
}

PY_PyObject *
XUSD_HuskEngine::pySettingsDict(const XUSD_RenderSettings &sets) const
{
    UT_JSONValue        value;
    {
        UT_AutoJSONWriter   w(value);
        sets.dump(*w);
    }
    return xusd_PyObject(value);
}

bool
XUSD_HuskEngine::Render(fpreal frame)
{
    TF_VERIFY(myTaskManager.get());

    const UsdPrim       &root = myStage->GetPseudoRoot();

    PrepareBatch(root, frame);

    // XXX(UsdImagingPaths): Is it correct to map USD root path directly
    // to the cachePath here?
    SdfPath cachePath = root.GetPath();
    SdfPathVector roots(1, myDelegate->ConvertCachePathToIndexPath(cachePath));

    updateHydraCollection(myRenderCollection, roots);
    myTaskManager->SetCollection(myRenderCollection);

    return doRender();
}

bool
XUSD_HuskEngine::IsConverged() const
{
    UT_ASSERT(myTaskManager);
    return myTaskManager->IsConverged();
}

//----------------------------------------------------------------------------
// Camera and Light State
//----------------------------------------------------------------------------

void
XUSD_HuskEngine::setDataWindow(const UT_DimRect &dataWindow)
{
    myTaskManager->SetRenderViewport(
	    GfVec4d(dataWindow.x(),
		    dataWindow.y(),
		    dataWindow.width(),
		    dataWindow.height()));
}

//----------------------------------------------------------------------------
// Renderer Plugin Management
//----------------------------------------------------------------------------

void
XUSD_HuskEngine::releaseRendererPlugin()
{
    deleteHydraResources();
}

bool
XUSD_HuskEngine::setRendererPlugin(const XUSD_RenderSettings &settings,
			const char *complexity_name)
{
    static const TfToken unitsToken("stageMetersPerUnit", TfToken::Immortal);
    static UT_Map<UT_StringHolder, RenderComplexity> theComplexityMap({
	    { "low",	COMPLEXITY_LOW },
	    { "medium", COMPLEXITY_MEDIUM },
	    { "high",	COMPLEXITY_HIGH },
	    { "veryhigh", COMPLEXITY_VERYHIGH },
    });
    HdRendererPlugin *plugin = nullptr;
    TfToken actualId = settings.renderer();

    auto complexity = theComplexityMap.find(complexity_name);
    if (complexity == theComplexityMap.end())
    {
	UT_ErrorLog::warning("Unknown complexity option {} - using veryhigh",
		complexity_name);
	myComplexity = COMPLEXITY_VERYHIGH;
    }
    else
    {
	myComplexity = complexity->second;
    }

    // Get the rendering purpose
    myRenderTags.clear();
    for (const auto &t : settings.purpose())
    {
	if (t == UsdGeomTokens->default_)
	{
	    myRenderTags.push_back(HdTokens->geometry);
	    myRenderTags.push_back(UsdGeomTokens->render);
	}
	else
	{
	    myRenderTags.push_back(t);
	}
    }

    // Special case: TfToken() selects the first plugin in the list.
    if (actualId.IsEmpty())
    {
        actualId = HdRendererPluginRegistry::GetInstance().
            GetDefaultPluginId();
	if (actualId.IsEmpty())
	{
	    UT_ErrorLog::error("No rendering delegates found");
	    return false;
	}
	UT_ErrorLog::warning("Selected {} as the render delegate", actualId);
    }
    if (!settings.supportedDelegate(actualId))
	return false;

    auto &&reg = HdRendererPluginRegistry::GetInstance();
    plugin = reg.GetRendererPlugin(actualId);
    HfPluginDescVector	plugins;
    if (!plugin)
    {
	// Try to match description
	reg.GetPluginDescs(&plugins);
	for (auto &&p : plugins)
	{
	    if (p.displayName == actualId)
	    {
		plugin = reg.GetRendererPlugin(p.id);
		if (plugin)
		{
		    actualId = p.id;	// Make sure actualId is the token
		    break;
		}
	    }
	}
    }

    if (!plugin)
    {
	UT_ErrorLog::error("Can't find Hydra plugin '{}'. Choose one of:",
		actualId);
	for (auto &&p : plugins)
	    UT_ErrorLog::error("  - {} ({})", p.displayName, p.id);

        return false;
    }
    else if (plugin == myPlugin)
    {
        // It's a no-op to load the same plugin twice.
        reg.ReleasePlugin(plugin);
        return true;
    }
    else if (!plugin->IsSupported())
    {
        // Don't do anything if the plugin isn't supported on the running
        // system, just return that we're not able to set it.
        reg.ReleasePlugin(plugin);
	UT_ErrorLog::error("Hydra plugin {} is not supported", actualId);
        return false;
    }

    // Pull old delegate/task controller state.
    GfMatrix4d rootTransform = GfMatrix4d(1.0);
    bool isVisible = true;
    if (myDelegate)
    {
        rootTransform = myDelegate->GetRootTransform();
        isVisible = myDelegate->GetRootVisibility();
    }

    // Delete hydra state.
    deleteHydraResources();

    // Recreate the render index.
    myPlugin = plugin;
    myRendererId = actualId;

    // Pass the viewport dimensions into CreateRenderDelegate, for backends that
    // need to allocate the viewport early.
    SdfPath     camera = settings.cameraPath(nullptr);
    if (camera.IsEmpty())
    {
	UT_ErrorLog::error("Missing rendering camera");
	return false;
    }

    // After the camera has been locked down, we can now create the delegate
    myRenderSettings = settings.renderSettings();
    HdRenderDelegate *renderDelegate =
        myPlugin->CreateRenderDelegate(myRenderSettings);
    myRenderIndex.reset(HdRenderIndex::New(renderDelegate, HdDriverVector()));

    myRenderIndex->GetRenderDelegate()->SetRenderSetting(unitsToken,
	    VtValue(UsdGeomGetStageMetersPerUnit(myStage)));

    // Create the new delegate & task controller.
    myDelegate = UTmakeUnique<UsdImagingDelegate>(myRenderIndex.get(),
	    myDelegateId);
    myIsPopulated = false;

    myTaskManager = UTmakeUnique<XUSD_HuskTaskManager>(myRenderIndex.get(),
	    myDelegateId.AppendChild(TfToken(TfStringPrintf("_UsdImaging_%s_%p",
		    TfMakeValidIdentifier(actualId.GetText()).c_str(), this))),
	    camera);

    // Rebuild state in the new delegate/task controller.
    myDelegate->SetRootVisibility(isVisible);
    myDelegate->SetRootTransform(rootTransform);
    myDelegate->SetCameraForSampling(camera);

    return true;
}

void
XUSD_HuskEngine::updateSettings(const XUSD_RenderSettings &settings)
{
    static TfToken      theRenderCameraPath("renderCameraPath", TfToken::Immortal);
    auto &&del = myRenderIndex->GetRenderDelegate();
    for (const auto &item : settings.renderSettings())
    {
        auto &&it = myRenderSettings.find(item.first);
        if (it != myRenderSettings.end() && it->second == item.second)
        {
            continue;
        }
        del->SetRenderSetting(item.first, item.second);
        if (item.first == theRenderCameraPath)
        {
            SdfPath     camera;
            if (item.second.IsHolding<SdfPath>())
                camera = item.second.UncheckedGet<SdfPath>();
            else
            {
                if (item.second.IsHolding<TfToken>())
                    camera = SdfPath(item.second.UncheckedGet<TfToken>().GetString());
                else if (item.second.IsHolding<SdfPath>())
                    camera = item.second.UncheckedGet<SdfPath>();
                else if (item.second.IsHolding<std::string>())
                    camera = SdfPath(item.second.UncheckedGet<std::string>());
            }
            UT_ASSERT(!camera.IsEmpty());
            myTaskManager->setCamera(camera);
            myDelegate->SetCameraForSampling(camera);
        }
    }
    myRenderSettings = settings.renderSettings();
}

bool
XUSD_HuskEngine::setAOVs(const XUSD_RenderSettings &settings)
{
    TfTokenVector	aovs;
    HdAovDescriptorList aovdescs;
    if (!settings.collectAovs(aovs, aovdescs))
	return false;

    UT_ASSERT(settings.products().size());

    UT_ASSERT(aovs.size() == aovdescs.size());
    if (!aovs.size())
    {
	UT_ErrorLog::error("No AOVs defined for render, {}",
                "not all delegates will function properly");
    }
    myTaskManager->SetRenderOutputs(aovs, aovdescs);

    return true;
}

void
XUSD_HuskEngine::delegateRenderProducts(const XUSD_RenderSettings &settings,
        int product_group)
{
    static const TfToken drpToken("delegateRenderProducts", TfToken::Immortal);
    myRenderIndex->GetRenderDelegate()->SetRenderSetting(drpToken,
            settings.delegateRenderProducts(product_group));
}

//----------------------------------------------------------------------------
// AOVs and Renderer Settings
//----------------------------------------------------------------------------
void
XUSD_HuskEngine::setKarmaRandomSeed(int seed)
{
    static const TfToken seedtoken("randomseed", TfToken::Immortal);
    VtValue		val(seed);
    myRenderIndex->GetRenderDelegate()->SetRenderSetting(seedtoken, val);
}

void
XUSD_HuskEngine::mplayMouseClick(int x, int y) const
{
    static const TfToken mplayClick("viewerMouseClick", TfToken::Immortal);
    GfVec2i     mouse(x, y);
    myRenderIndex->GetRenderDelegate()->SetRenderSetting(mplayClick,
            VtValue(mouse));
}

HdAovDescriptor
XUSD_HuskEngine::defaultAovDescriptor(const TfToken &name) const
{
    return myRenderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(name);
}

HdRenderBuffer *
XUSD_HuskEngine::GetRenderOutput(const TfToken &name) const
{
    return myTaskManager->GetRenderOutput(name);
}

VtDictionary
XUSD_HuskEngine::renderStats() const
{
    return myRenderIndex->GetRenderDelegate()->GetRenderStats();
}

namespace
{
    static void
    dumpNode(int indent, const UsdPrim &prim)
    {
        UT_WorkBuffer   space;
        space.sprintf("%*s", indent, " ");
        UTdebugFormat("{}{}", space, prim.GetPath());
        for (auto &&kid : prim.GetAllChildren())
            dumpNode(indent+2, kid);
    }

    static void
    getAllRenderSettings(const UsdStageRefPtr &stage,
	    VtArray<UsdRenderSettings> &list)
    {
	list.clear();
	UsdPrim	render = stage->GetPrimAtPath(SdfPath("/Render"));
	if (render)
	{
	    for (auto &&k : render.GetAllChildren())
	    {
		UsdRenderSettings	sets(k);
		if (sets)
		    list.push_back(sets);
	    }
	}
    }
}


void
XUSD_HuskEngine::dumpUSD() const
{
    UTdebugFormat("USD Tree");
    if (myStage && myStage->GetPseudoRoot())
        dumpNode(0, myStage->GetPseudoRoot());
}

void
XUSD_HuskEngine::listSettings(UT_StringArray &list) const
{
    VtArray<UsdRenderSettings>  sets;
    getAllRenderSettings(myStage, sets);

    for (const auto &s : sets)
        list.append(HUSD_Path(s.GetPath()).pathStr());
}

void
XUSD_HuskEngine::listCameras(UT_StringArray &list) const
{
    UT_Array<SdfPath>   cams;
    XUSD_RenderSettings::findCameras(cams, myStage->GetPseudoRoot());

    for (const auto &c : cams)
        list.append(HUSD_Path(c).pathStr());
}

void
XUSD_HuskEngine::listDelegates(UT_StringArray &delegates)
{
    HfPluginDescVector    plugins;

    HdRendererPluginRegistry::GetInstance().GetPluginDescs(&plugins);

    UT_WorkBuffer       tmp;
    for (auto &&p : plugins)
    {
        tmp.format("{} ({})", p.id, p.displayName);
        delegates.append(UT_StringHolder(tmp));
    }
}

UT_StringHolder
XUSD_HuskEngine::settingsPath(const char *path) const
{
    UsdRenderSettings	sets;
    if (UTisstring(path))
    {
        sets = UsdRenderSettings::Get(myStage, SdfPath(path));
        if (!sets)
        {
            UT_WorkBuffer	tmp;
            tmp.sprintf("/Render/%s", path);
            sets = UsdRenderSettings::Get(myStage, SdfPath(tmp.buffer()));
        }
        if (sets)
            return HUSD_Path(sets.GetPrim().GetPath()).pathStr();
        return UT_StringHolder(path);
    }
    // Try to get the default settings
    sets = UsdRenderSettings::GetStageRenderSettings(myStage);
    if (sets)
    {
        UT_ErrorLog::format(1, "Using stage default settings: {}",
                sets.GetPrim().GetPath());
        return HUSD_Path(sets.GetPrim().GetPath()).pathStr();
    }
    // There's no default setting - but if there's only one setting, use it
    // instead.
    VtArray<UsdRenderSettings>	allsets;
    getAllRenderSettings(myStage, allsets);
    if (allsets.size() == 1)
    {
        UT_ErrorLog::format(1, "Defaulting to use settings found at {}",
                allsets[0].GetPath());
        return HUSD_Path(allsets[0].GetPath()).pathStr();
    }
    if (allsets.size() > 1)
    {
        UT_ErrorLog::format(1,
                "Found {} render settings, use -s option to select",
                allsets.size());
        if (UT_ErrorLog::isMantraVerbose(3))
        {
            for (auto &&k : allsets)
                UT_ErrorLog::format(1, "  - {}", k.GetPath());
        }
    }
    return UT_StringHolder();
}

//----------------------------------------------------------------------------
// Private/Protected
//----------------------------------------------------------------------------

bool
XUSD_HuskEngine::doRender()
{
    myPercentDone = 0;
    TF_VERIFY(myDelegate);

    myTaskManager->SetRenderTags(myRenderTags);

    HdTaskSharedPtrVector tasks = myTaskManager->GetRenderingTasks();
    myEngine.Execute(myRenderIndex.get(), &tasks);

    // TODO: Check error status of engine to see if there's an error.

    return true;
}

bool
XUSD_HuskEngine::canPrepareBatch(const UsdPrim &root)
{
    HD_TRACE_FUNCTION();

    if (!TF_VERIFY(root, "Attempting to draw an invalid/null prim\n"))
        return false;

    if (!root.GetPath().HasPrefix(myRootPath))
    {
        TF_CODING_ERROR("Attempting to draw path <%s>, but engine is rooted"
                    "at <%s>\n",
                    root.GetPath().GetText(),
                    myRootPath.GetText());
        return false;
    }

    return true;
}

void
XUSD_HuskEngine::preSetTime(const UsdPrim &root)
{
    HD_TRACE_FUNCTION();

    // Set the fallback refine level, if this changes from the existing value,
    // all prim refine levels will be dirtied.
    int refineLevel;
    switch (myComplexity)
    {
	case COMPLEXITY_LOW:		refineLevel = 0; break;
	case COMPLEXITY_MEDIUM:		refineLevel = 2; break;
	case COMPLEXITY_HIGH:		refineLevel = 4; break;
	case COMPLEXITY_VERYHIGH:	refineLevel = 8; break;
    }
    myDelegate->SetRefineLevelFallback(refineLevel);

    // Apply any queued up scene edits.
    myDelegate->ApplyPendingUpdates();
}

void
XUSD_HuskEngine::postSetTime(const UsdPrim &root)
{
    HD_TRACE_FUNCTION();
}

/* static */
bool
XUSD_HuskEngine::updateHydraCollection(
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
XUSD_HuskEngine::deleteHydraResources()
{
    // Unwinding order: remove data sources first (task controller, scene
    // delegate); then render index; then render delegate; finally the
    // renderer plugin used to manage the render delegate.
    myTaskManager.reset(nullptr);
    myDelegate.reset(nullptr);

    HdRenderDelegate *renderDelegate = nullptr;
    if (myRenderIndex)
    {
        renderDelegate = myRenderIndex->GetRenderDelegate();
	myRenderIndex.reset(nullptr);
    }
    if (myPlugin)
    {
        if (renderDelegate)
            myPlugin->DeleteRenderDelegate(renderDelegate);

        HdRendererPluginRegistry::GetInstance().ReleasePlugin(myPlugin);
        myPlugin = nullptr;
        myRendererId = TfToken();
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

