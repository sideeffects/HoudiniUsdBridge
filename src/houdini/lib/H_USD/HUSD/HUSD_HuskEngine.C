/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	HUSD_HuskEngine.h (HUSD Library, C++)
 *
 * COMMENTS:
 */

#include "HUSD_HuskEngine.h"
#include "HUSD_RenderSettings.h"
#include "HUSD_RendererInfo.h"
#include "HUSD_Path.h"
#include "XUSD_Format.h"
#include "XUSD_Tokens.h"
#include "XUSD_Utils.h"
#include "XUSD_HuskEngine.h"
#include "XUSD_RenderSettings.h"
#include "XUSD_FindPrimsTask.h"
#include <SYS/SYS_Time.h>
#include <IMG/IMG_FileParms.h>
#include <PY/PY_Python.h>
#include <UT/UT_ArenaInfo.h>
#include <UT/UT_Debug.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_VarScan.h>
#include <UT/UT_Options.h>
#include <UT/UT_Matrix2.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Vector2.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_Vector4.h>
#include <UT/UT_JSONPath.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONValueMap.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_PathSearch.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/size2.h>
#include <pxr/base/gf/size3.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdLux/lightAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
#define SCALAR_VALUE(TYPE) \
    if (v.IsHolding<TYPE>()) { iv = v.UncheckedGet<TYPE>(); return true; } \
    /* end macro */
#define STRING_VALUE(TYPE, CONVERT) \
    if (v.IsHolding<TYPE>()) { \
        const auto &s = v.UncheckedGet<TYPE>(); \
        iv = CONVERT;  \
        return true; \
    } \
    /* end macro */
#define VEC_VALUE(TYPE) \
    if (v.IsHolding<TYPE>()) { \
        TYPE tmp = v.UncheckedGet<TYPE>(); \
        std::copy(tmp.data(), tmp.data()+T::tuple_size, iv.data()); \
        return true; \
    } \
    /* end macro */
#define VEC_COPY_VALUE(TYPE) \
    if (v.IsHolding<TYPE>()) { \
        TYPE tmp = v.UncheckedGet<TYPE>(); \
        for (int i = 0; i < T::tuple_size; ++i) { iv.data()[i] = tmp[i]; } \
        return true; \
    } \
    /* end macro */

    template <typename T>
    static bool
    intValue(T &iv, const VtValue &v)
    {
        SCALAR_VALUE(int32)
        SCALAR_VALUE(int64)
        SCALAR_VALUE(uint32)
        SCALAR_VALUE(uint64)
        SCALAR_VALUE(bool)
        SCALAR_VALUE(int16)
        SCALAR_VALUE(uint16)
        SCALAR_VALUE(int8)
        SCALAR_VALUE(uint8)
        return false;
    }

    template <typename T>
    static bool
    realValue(T &iv, const VtValue &v)
    {
        SCALAR_VALUE(fpreal32)
        SCALAR_VALUE(fpreal64)
        SCALAR_VALUE(fpreal16)
        return intValue(iv, v);
    }

    static UT_StringHolder
    sdfToHolder(const SdfPath &p)
    {
        return HUSD_Path(p).pathStr();
    }

    static bool
    stringValue(UT_StringHolder &iv, const VtValue &v)
    {
        STRING_VALUE(std::string, UT_StringHolder(s))
        STRING_VALUE(TfToken, s.GetText())
        STRING_VALUE(UT_StringHolder, s)
        STRING_VALUE(SdfPath, sdfToHolder(s))
        return false;
    }

    template <typename T> static bool
    v2value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfVec2i);
        VEC_VALUE(GfVec2f);
        VEC_VALUE(GfVec2d);
        VEC_VALUE(UT_Vector2i);
        VEC_VALUE(UT_Vector2I);
        VEC_VALUE(UT_Vector2F);
        VEC_VALUE(UT_Vector2D);
        VEC_COPY_VALUE(GfSize2);
        return false;
    }

    template <typename T> static bool
    v3value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfVec3i);
        VEC_VALUE(GfVec3f);
        VEC_VALUE(GfVec3d);
        VEC_VALUE(UT_Vector3i);
        VEC_VALUE(UT_Vector3I);
        VEC_VALUE(UT_Vector3F);
        VEC_VALUE(UT_Vector3D);
        VEC_COPY_VALUE(GfSize3);
        return false;
    }

    template <typename T> static bool
    v4value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfVec4i);
        VEC_VALUE(GfVec4f);
        VEC_VALUE(GfVec4d);
        VEC_VALUE(UT_Vector4i);
        VEC_VALUE(UT_Vector4I);
        VEC_VALUE(UT_Vector4F);
        VEC_VALUE(UT_Vector4D);
        return false;
    }
}

const VtValue *
HUSD_HuskEngine::RenderStats::findForImport(const UT_StringRef &name) const
{
    if (!myStorage)
        return nullptr;
    const VtDictionary &stats = *(const VtDictionary *)myStorage;
    auto it = stats.find(name);
    if (it != stats.end())
        return &it->second;
    return nullptr;
}

exint
HUSD_HuskEngine::RenderStats::size() const
{
    return myStorage ? ((const VtDictionary *)myStorage)->size() : 0;
}

void
HUSD_HuskEngine::RenderStats::dump() const
{
    UT_AutoJSONWriter   w(std::cerr, false);
    dump(*w);
}

void
HUSD_HuskEngine::RenderStats::dump(UT_WorkBuffer &buffer) const
{
    UT_AutoJSONWriter   w(buffer);
    dump(*w);
}

bool
HUSD_HuskEngine::RenderStats::save(UT_JSONWriter &w) const
{
    if (!myStorage)
        return false;

    return HUSDconvertDictionary(w, *(const VtDictionary *)(myStorage));
}

void
HUSD_HuskEngine::RenderStats::setStorage(const VtDictionary &v)
{
    if (myStorage && *(const VtDictionary *)myStorage == v)
        return;
    freeStorage();
    if (v.size())
    {
        myStorage = new VtDictionary(v);
        UT_ASSERT(myJSONStats == nullptr);
    }
}

void
HUSD_HuskEngine::RenderStats::freeStorage()
{
    if (myStorage)
    {
        delete (VtDictionary *)(myStorage);
        delete myJSONStats;
        myStorage = nullptr;
        myJSONStats = nullptr;
    }
}

const UT_JSONValue &
HUSD_HuskEngine::RenderStats::jsonStats()
{
    if (!myJSONStats)
    {
        UT_ASSERT(myStorage);
        myJSONStats = new UT_JSONValue;
        if (myStorage)
        {
            UT_AutoJSONWriter       w(*myJSONStats);
            HUSDconvertDictionary(*w, *(VtDictionary *)(myStorage));
        }
    }
    return *myJSONStats;
}


//----------------------------------------------------------------------------
// Error delegate
//----------------------------------------------------------------------------

class HUSD_HuskEngine::UT_ErrorDelegate::errorImpl final
    : public TfDiagnosticMgr::Delegate
{
public:
    errorImpl(bool all_errors)
        : myAllErrors(all_errors)
    {
        TfDiagnosticMgr::GetInstance().AddDelegate(this);
    }
    ~errorImpl() override
    {
        TfDiagnosticMgr::GetInstance().RemoveDelegate(this);
    }

    void    IssueError(const PXR_NS::TfError &e) override
    {
	if (myAllErrors || showError(e.GetCommentary()))
	{
	    UT_ErrorLog::error("USD error: {}", e.GetCommentary());
	    //UT_ASSERT(0);
	}
    }
    void    IssueStatus(const PXR_NS::TfStatus &e) override
    {
	if (myAllErrors || showError(e.GetCommentary()))
	{
	    UT_ErrorLog::format(2, "USD: {}", e.GetCommentary());
	    //UT_ASSERT(0);
	}
    }
    void    IssueWarning(const PXR_NS::TfWarning &e) override
    {
	if (myAllErrors || showError(e.GetCommentary()))
	{
	    UT_ErrorLog::warning("USD warning:{}", e.GetCommentary());
	    //UT_ASSERT(0);
	}
    }
    void    IssueFatalError(const PXR_NS::TfCallContext &ctx,
                            const std::string &e) override
    {
	UT_ErrorLog::error("USD Fatal Error{}", e);
	UT_ASSERT(0);
    }

private:
    bool        showError(const std::string &m)
    {
	if (UT_StringWrap(m.c_str()).startsWith("Could not open asset"))
	    return true;
	return false;
    }
    bool    myAllErrors;
};

HUSD_HuskEngine::UT_ErrorDelegate::UT_ErrorDelegate(bool all_errors)
    : myImpl(new errorImpl(all_errors))
{
}

HUSD_HuskEngine::UT_ErrorDelegate::~UT_ErrorDelegate()
{
}


HUSD_HuskEngine::HUSD_HuskEngine()
    : myEngine(new XUSD_HuskEngine())
{
}

HUSD_HuskEngine::~HUSD_HuskEngine()
{
}

void
HUSD_HuskEngine::setVariantSelectionFallbacks(
        const UT_StringMap<UT_StringArray> &fallbacks)
{
    PcpVariantFallbackMap pcpfallbacks;
    HUSDconvertVariantSelectionFallbacks(fallbacks, pcpfallbacks);
    UsdStage::SetGlobalVariantFallbacks(pcpfallbacks);
}

bool
HUSD_HuskEngine::loadStage(const UT_StringHolder &usdfile,
        const UT_StringHolder &resolver_context_file,
        const UT_StringMap<UT_StringHolder> &resolver_context_strings,
        const char *mask /*=nullptr*/)
{
    return myEngine->loadStage(usdfile,
                               resolver_context_file,
                               resolver_context_strings,
                               mask);
}

bool
HUSD_HuskEngine::isValid() const
{
    return myEngine->isValid();
}

const UT_StringHolder &
HUSD_HuskEngine::usdFile() const
{
    return myEngine->usdFile();
}

time_t
HUSD_HuskEngine::usdTimeStamp() const
{
    return myEngine->usdTimeStamp();
}

bool
HUSD_HuskEngine::getVerboseCallback(UT_StringHolder &callback,
                                    fpreal &interval) const
{
    const HUSD_RendererInfo     &rinfo = myEngine->rendererInfo();
    callback = rinfo.huskVerboseScript();
    interval = rinfo.huskVerboseInterval();
    if (!callback)
    {
        interval = SYS_FP32_MAX;
        return false;
    }
    UT_String   full_path;
    if (!HoudiniFindFile(callback, full_path))
    {
        UT_WorkBuffer   tmp;
        tmp.format("{}/{}", PYgetPythonLibsSubdir(), callback);
        if (!HoudiniFindFile(tmp.buffer(), full_path))
        {
            UT_ErrorLog::error("Unable to find Python callback script: {}",
                    callback);
            callback.clear();
            interval = SYS_FP32_MAX;
            return false;
        }
        callback = UT_StringHolder(full_path);
    }
    return true;
}

fpreal
HUSD_HuskEngine::stageFPS() const
{
    return myEngine->stageFPS();
}

PY_PyObject *
HUSD_HuskEngine::pyStage() const
{
    return myEngine->pyStage();
}

void
HUSD_HuskEngine::updateHeadlight(const UT_StringHolder &style,
        fpreal frame)
{
    UsdTimeCode time(frame);
    if (style == HusdHuskTokens->distant)
        myEngine->updateHeadlight(HusdHuskTokens->distant, time);
    else if (style == HusdHuskTokens->dome)
        myEngine->updateHeadlight(HusdHuskTokens->dome, time);
}

PY_PyObject *
HUSD_HuskEngine::pySettingsDict(const HUSD_RenderSettings &s) const
{
    return myEngine->pySettingsDict(*s.myOwner);
}

bool
HUSD_HuskEngine::Render(fpreal frame)
{
    return myEngine->Render(frame);
}

bool
HUSD_HuskEngine::IsConverged() const
{
    return myEngine->IsConverged();
}

void
HUSD_HuskEngine::setDataWindow(const UT_DimRect &dataWindow)
{
    myEngine->setDataWindow(dataWindow);
}

UT_StringHolder
HUSD_HuskEngine::pluginName() const
{
    return UT_StringHolder(myEngine->pluginName().GetText());
}

bool
HUSD_HuskEngine::setRendererPlugin(const HUSD_RenderSettings &settings,
        const DelegateParms &rparms)
{
    myEngine->releaseRendererPlugin();
    return myEngine->setRendererPlugin(*settings.myOwner, rparms);
}

bool
HUSD_HuskEngine::setAOVs(const HUSD_RenderSettings &settings)
{
    return myEngine->setAOVs(*settings.myOwner);
}

void
HUSD_HuskEngine::updateSettings(const HUSD_RenderSettings &settings)
{
    myEngine->updateSettings(*settings.myOwner);
}

void
HUSD_HuskEngine::delegateRenderProducts(const HUSD_RenderSettings &settings,
        int pgroup)
{
    myEngine->delegateRenderProducts(*settings.myOwner, pgroup);
}

namespace
{
    class xusd_FindLightPrim final : public XUSD_FindPrimsTaskData
    {
    public:
        void    addToThreadData(const UsdPrim &prim, bool *prune) override
        {
            UsdLuxLightAPI      lux(prim);
            if (lux)
            {
                // TODO: Check the prim is invisible
                myFound = true;
            }
            if (prune)
                *prune = myFound;
        }
        bool    myFound = false;
    };
}

bool
HUSD_HuskEngine::lightOnStage() const
{
    UsdPrim     root = myEngine->stage()->GetPseudoRoot();
    auto        predicate = HUSDgetUsdPrimPredicate(HUSD_PrimTraversalDemands(
                          HUSD_TRAVERSAL_ACTIVE_PRIMS
                        | HUSD_TRAVERSAL_DEFINED_PRIMS
                        | HUSD_TRAVERSAL_NONABSTRACT_PRIMS
                        | HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES));

    xusd_FindLightPrim          data;
    XUSDfindPrims(root, data, predicate, nullptr, nullptr);
    return data.myFound;
}

namespace
{
    struct husd_JSONExpander
    {
        husd_JSONExpander(const UT_JSONValue &v)
            : myValue(v)
        {
        }
        const UT_JSONValue      &myValue;
        UT_WorkBuffer            myBuffer;
        int                      myNumFound = 0;
    };

    const char *
    expandJSONPath(const char *src, void *userdata)
    {
        husd_JSONExpander               *x = (husd_JSONExpander *)userdata;
        UT_Set<const UT_JSONValue *>     matches;
        UT_JSONPath::find(matches, x->myValue, src);
        if (matches.size() != 1)
            return "";

        x->myBuffer.clear();
        x->myNumFound++;
        for (auto it : matches)
        {
            const UT_StringHolder       *s = it->getStringHolder();
            if (s)
                return s->c_str();
            x->myBuffer.strcpy(it->toString());
        }
        return x->myBuffer.buffer();
    }
}

void
HUSD_HuskEngine::addMetadata(IMG_FileParms &fparms,
        const UT_StringMap<UT_StringHolder> &metadata,
        const UT_JSONValue &value) const
{
    husd_JSONExpander   json_values(value);
    UT_WorkBuffer       tmp;
    for (auto item : metadata)
    {
        tmp.clear();
        json_values.myNumFound = 0;
        UTVariableScan(tmp, item.second.c_str(), expandJSONPath, &json_values);
        if (json_values.myNumFound || item.second.findCharIndex('$') < 0)
        {
            // Only set metadata if the items are found (or there's no variable)
            fparms.setOption(item.first.c_str(), tmp.buffer());
            //UTdebugFormat("Add metadata: {} {}", item.first, tmp);
        }
    }
}

void
HUSD_HuskEngine::addMetadata(IMG_FileParms &fparms,
        const UT_JSONValue &base_dict,
        const char *render_stats) const
{
    const auto          &metadata = myEngine->rendererInfo().huskMetadata();

    if (!metadata.size())
        return;

    UT_JSONValue         combined;
    UT_JSONValue         stats;
    UT_JSONValueMap     *map = combined.startMap();

    const UT_JSONValueMap       *src_map = base_dict.getMap();
    if (src_map)
    {
        // Copy over the base map entries
        UT_StringArray  keys;
        src_map->getKeys(keys);
        for (exint i = 0, n = src_map->size(); i < n; ++i)
            map->insert(keys[i], *src_map->get(i));
    }
    {
        UT_AutoJSONWriter       w(stats);
        HUSDconvertDictionary(*w, myEngine->renderStats(), nullptr);
    }
    map->insert(render_stats, stats);

    addMetadata(fparms, metadata, combined);
}

bool
HUSD_HuskEngine::rendererName(RenderStats &stats,
        UT_StringHolder &sval) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myEngine->rendererInfo().findStatsData(jstat, "rendererName");
    if (j && j->getStringHolder())
        sval = *j->getStringHolder();
    else
        sval = myEngine->rendererInfo().menuLabel();
    return true;
}

bool
HUSD_HuskEngine::activeBuckets(RenderStats &stats,
        UT_Array<ActiveBucket> &buckets) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myEngine->rendererInfo().findStatsData(jstat, "activeBuckets");
    const UT_JSONValueArray     *barr = j ? j->getArray() : nullptr;
    buckets.clear();
    if (!barr)
        return false;

    static constexpr UT_StringLit       theXKey("x");
    static constexpr UT_StringLit       theYKey("y");
    static constexpr UT_StringLit       theWidthKey("width");
    static constexpr UT_StringLit       theHeightKey("height");
    for (int i = 0, n = barr->size(); i < n; ++i)
    {
        const UT_JSONValue      *item = barr->get(i);
        const UT_JSONValueMap   *b = item ? item->getMap() : nullptr;
        if (!b)
            continue;
	int64	x, y, width, height;
        if (   !b->import(theXKey.asRef(), x)
            || !b->import(theYKey.asRef(), y)
            || !b->import(theWidthKey.asRef(), width)
            || !b->import(theHeightKey.asRef(), height))
        {
            // Not a valid bucket description
            UT_ErrorLog::errorOnce("Invalid active bucket format from delegate");
            continue;
        }
        ActiveBucket    bucket;
        UT_StringArray  keys;
        int64           ival;
        fpreal64        fval;
        UT_StringHolder sval;

        bucket.myBounds.setX(x);
        bucket.myBounds.setY(y);
        bucket.myBounds.setWidth(width);
        bucket.myBounds.setHeight(height);
        b->getKeys(keys);
        static const UT_Set<UT_StringHolder>        theKeys({
                theXKey.asHolder(),
                theYKey.asHolder(),
                theWidthKey.asHolder(),
                theHeightKey.asHolder(),
        });
        for (const UT_StringHolder &key : keys)
        {
            if (theKeys.contains(key))
                continue;
            const UT_JSONValue  *val = b->get(key);
            UT_ASSERT(val);
            if (!val)
                continue;
            switch (val->getType())
            {
                case UT_JSONValue::JSON_BOOL:
                case UT_JSONValue::JSON_INT:
                    UT_VERIFY(val->import(ival));
                    bucket.myOptions.setOptionI(key, ival);
                    break;
                case UT_JSONValue::JSON_REAL:
                    UT_VERIFY(val->import(fval));
                    bucket.myOptions.setOptionF(key, fval);
                    break;
                case UT_JSONValue::JSON_STRING:
                    UT_VERIFY(val->import(sval));
                    bucket.myOptions.setOptionS(key, sval);
                    break;
                default:
                    bucket.myOptions.setOptionS(key, val->toString());
                    break;
            }
        }
        buckets.append(bucket);
    }
    return buckets.size() > 0;
}

bool
HUSD_HuskEngine::percentDone(RenderStats &stats,
        fpreal &pct, bool final) const
{
    if (final)
    {
        pct = 100;
        return true;
    }
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myEngine->rendererInfo().findStatsData(jstat, "percentDone");
    if (j && j->isNumber())
    {
        pct = j->getF();
        return true;
    }
    j = myEngine->rendererInfo().findStatsData(jstat, "fractionDone");
    if (j && j->isNumber())
    {
        pct = j->getF() * 100;
        return true;
    }
    pct = 0;
    return false;
}

bool
HUSD_HuskEngine::renderStage(RenderStats &stats, UT_StringHolder &stage) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myEngine->rendererInfo().findStatsData(jstat, "rendererStage");
    if (j && j->import(stage))
        return true;
    stage.clear();
    return false;
}

bool
HUSD_HuskEngine::renderTime(RenderStats &stats,
        fpreal &wall, fpreal &user, fpreal &sys) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *jw = myEngine->rendererInfo().findStatsData(jstat, "totalClockTime");
    const UT_JSONValue  *ju = myEngine->rendererInfo().findStatsData(jstat, "totalUTime");
    const UT_JSONValue  *js = myEngine->rendererInfo().findStatsData(jstat, "totalSTime");
    bool                 found_wall = false;
    wall = user = sys = -1;
    if (jw && jw->isNumber())
    {
        wall = jw->getF();
        found_wall = true;
    }
    if (ju && ju->isNumber())
        user = ju->getF();
    if (js && js->isNumber())
        sys = js->getF();
    if (wall < 0 || user < 0 || sys < 0)
    {
        SYS_TimeVal     pusr, psys;
        SYSrusage(pusr, psys);
        if (user < 0)
            user = SYStime(pusr);
        if (sys < 0)
            sys = SYStime(psys);
        if (wall < 0)
            wall = SYSclock();
    }
    return found_wall;
}

int64
HUSD_HuskEngine::renderMemory(RenderStats &stats) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myEngine->rendererInfo().findStatsData(jstat, "totalMemory");
    if (j && j->isNumber())
        return j->getI();
    return UT_ArenaInfo::arenaSize();
}

int64
HUSD_HuskEngine::renderPeakMemory(RenderStats &stats) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myEngine->rendererInfo().findStatsData(jstat, "peakMemory");
    if (j && j->isNumber())
        return j->getI();
    static int64        thePeakMemory = 0;
    thePeakMemory = SYSmax(thePeakMemory, renderMemory(stats));
    return thePeakMemory;
}

void
HUSD_HuskEngine::setKarmaRandomSeed(int seed) const
{
    myEngine->setRenderSetting(HusdHuskTokens->randomseed, VtValue(seed));
}

void
HUSD_HuskEngine::mplayMouseClick(int x, int y) const
{
    GfVec2i     mouse(x, y);
    myEngine->setRenderSetting(HusdHuskTokens->viewerMouseClick, VtValue(mouse));
}

void
HUSD_HuskEngine::huskSnapshot() const
{
    myEngine->setRenderSetting(HusdHuskTokens->husk_snapshot, VtValue(true));
}

void
HUSD_HuskEngine::huskInteractive() const
{
    myEngine->setRenderSetting(HusdHuskTokens->houdini_interactive,
            VtValue(HusdHuskTokens->husk_mplay));
}

HUSD_RenderBuffer
HUSD_HuskEngine::GetRenderOutput(const UT_StringRef &name) const
{
    return HUSD_RenderBuffer(myEngine->GetRenderOutput(TfToken(name.c_str())));
}

void
HUSD_HuskEngine::fillStats(RenderStats &stats) const
{
    stats.setStorage(myEngine->renderStats());
}

void
HUSD_HuskEngine::dumpUSD() const
{
    myEngine->dumpUSD();
}

UT_StringHolder
HUSD_HuskEngine::settingsPath(const char *path) const
{
    return myEngine->settingsPath(path);
}

void
HUSD_HuskEngine::listSettings(UT_StringArray &settings) const
{
    myEngine->listSettings(settings);
}

void
HUSD_HuskEngine::listCameras(UT_StringArray &cameras) const
{
    myEngine->listCameras(cameras);
}

void
HUSD_HuskEngine::listDelegates(UT_StringArray &delegates)
{
    XUSD_HuskEngine::listDelegates(delegates);
}
