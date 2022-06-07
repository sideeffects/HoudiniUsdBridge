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
#include "HUSD_Path.h"
#include "XUSD_Format.h"
#include "XUSD_Tokens.h"
#include "XUSD_HuskEngine.h"
#include "XUSD_RenderSettings.h"
#include <SYS/SYS_Time.h>
#include <UT/UT_ArenaInfo.h>
#include <UT/UT_Debug.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_Options.h>
#include <UT/UT_Matrix2.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Vector2.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_Vector4.h>
#include <UT/UT_StackBuffer.h>
#include <UT/UT_JSONWriter.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/size2.h>
#include <pxr/base/gf/size3.h>

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
#define ARRAY_VALUE(TYPE, ATYPE, GETVAL) \
    if (v.IsHolding<ATYPE<TYPE>>()) { \
        const ATYPE<TYPE>       &arr = v.UncheckedGet<ATYPE<TYPE>>(); \
        for (auto &&item : arr) \
            iv.append(GETVAL); \
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

    static bool
    stringArray(UT_StringArray &iv, const VtValue &v)
    {
        ARRAY_VALUE(std::string, VtArray, UT_StringHolder(item))
        ARRAY_VALUE(TfToken, VtArray, UT_StringHolder(item.GetText()))
        ARRAY_VALUE(SdfPath, VtArray, sdfToHolder(item));
        ARRAY_VALUE(UT_StringHolder, VtArray, item);
        ARRAY_VALUE(UT_StringHolder, UT_Array, item);
        return false;
    }
    static bool
    intArray(UT_Int64Array &iv, const VtValue &v)
    {
        ARRAY_VALUE(int32, VtArray, item)
        ARRAY_VALUE(int64, VtArray, item)
        ARRAY_VALUE(int32, UT_Array, item)
        ARRAY_VALUE(int64, UT_Array, item)
        return false;
    }
    static bool
    realArray(UT_Fpreal64Array &iv, const VtValue &v)
    {
        ARRAY_VALUE(fpreal32, VtArray, item)
        ARRAY_VALUE(fpreal64, VtArray, item)
        ARRAY_VALUE(fpreal32, UT_Array, item)
        ARRAY_VALUE(fpreal64, UT_Array, item)
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

    template <typename T> static bool
    m2value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfMatrix2f);
        VEC_VALUE(GfMatrix2d);
        VEC_VALUE(UT_Matrix2F);
        VEC_VALUE(UT_Matrix2D);
        return false;
    }

    template <typename T> static bool
    m3value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfMatrix3f);
        VEC_VALUE(GfMatrix3d);
        VEC_VALUE(UT_Matrix3F);
        VEC_VALUE(UT_Matrix3D);
        return false;
    }

    template <typename T> static bool
    m4value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfMatrix4f);
        VEC_VALUE(GfMatrix4d);
        VEC_VALUE(UT_Matrix4F);
        VEC_VALUE(UT_Matrix4D);
        return false;
    }
}

#define RS_IMPORT(TYPE, METHOD) \
    bool HUSD_HuskEngine::RenderStats::import( \
            TYPE &val, const UT_StringRef &name) const { \
        if (!myStorage) return false; \
        auto it = ((const VtDictionary *)myStorage)->find(name); \
        if (it == ((const VtDictionary *)myStorage)->end()) return false; \
        return METHOD(val, it->second); \
    } \
    /* end macro */

RS_IMPORT(int32, intValue)
RS_IMPORT(int64, intValue)
RS_IMPORT(fpreal32, realValue)
RS_IMPORT(fpreal64, realValue)
RS_IMPORT(UT_StringHolder, stringValue)
RS_IMPORT(UT_Vector2i, v2value)
RS_IMPORT(UT_Vector2I, v2value)
RS_IMPORT(UT_Vector2F, v2value)
RS_IMPORT(UT_Vector2D, v2value)
RS_IMPORT(UT_Vector3i, v3value)
RS_IMPORT(UT_Vector3I, v3value)
RS_IMPORT(UT_Vector3F, v3value)
RS_IMPORT(UT_Vector3D, v3value)
RS_IMPORT(UT_Vector4i, v4value)
RS_IMPORT(UT_Vector4I, v4value)
RS_IMPORT(UT_Vector4F, v4value)
RS_IMPORT(UT_Vector4D, v4value)
#undef RS_IMPORT

#define STAT_TOKEN(NAME) \
        (HusdHdRenderStatsTokens->NAME.GetText())

const char *
HUSD_HuskEngine::RenderStats::countType(CountType type)
{
    switch (type)
    {
        case POLYGON:           return STAT_TOKEN(polyCounts);
        case CURVE:             return STAT_TOKEN(curveCounts);
        case POINT:             return STAT_TOKEN(pointCounts);
        case POINT_MESH:        return STAT_TOKEN(pointMeshCounts);
        case VOLUME:            return STAT_TOKEN(volumeCounts);
        case PROCEDURAL:        return STAT_TOKEN(proceduralCounts);
        case LIGHT:             return STAT_TOKEN(lightCounts);
        case CAMERA:            return STAT_TOKEN(cameraCounts);
        case COORDSYS:          return STAT_TOKEN(coordSysCounts);
        case PRIMARY:           return STAT_TOKEN(cameraRays);
        case INDIRECT:          return STAT_TOKEN(indirectRays);
        case OCCLUSION:         return STAT_TOKEN(occlusionRays);
        case LIGHT_GEO:         return STAT_TOKEN(lightGeoRays);
        case PROBE:             return STAT_TOKEN(probeRays);
    }
    UT_ASSERT(0);
    return "<>";
}

bool
HUSD_HuskEngine::RenderStats::rendererName(UT_StringHolder &sval) const
{
    return import(sval, STAT_TOKEN(rendererName)) && sval.isstring();
}

bool
HUSD_HuskEngine::RenderStats::percentDone(fpreal &pct, bool final) const
{
    if (final)
    {
        pct = 100;
        return true;
    }
    if (import(pct, STAT_TOKEN(percentDone)))
        return true;
    if (import(pct, STAT_TOKEN(fractionDone)))
    {
        pct *= 100;
        return true;
    }
    pct = 0;
    return false;
}

bool
HUSD_HuskEngine::RenderStats::renderTime(fpreal &wall, fpreal &user, fpreal &sys) const
{
    bool	found_wall = true;
    if (!import(wall, STAT_TOKEN(totalClockTime)))
    {
	wall = -1;
	found_wall = false;
    }
    if (!import(user, STAT_TOKEN(totalUTime)))
	user = -1;
    if (!import(sys, STAT_TOKEN(totalSTime)))
	sys = -1;

    if (wall < 0 || user < 0 || sys < 0)
    {
	SYS_TimeVal	pusr, psys;
	SYSrusage(pusr, psys);
	if (user < 0)
	    user = SYStime(pusr);
	if (sys < 0)
	    sys = SYStime(psys);
	if (wall < 0)
	    wall = user + sys;
    }
    return found_wall;
}

int64
HUSD_HuskEngine::RenderStats::getMemory() const
{
    int64       mem;
    if (import(mem, STAT_TOKEN(totalMemory)))
        return mem;
    return UT_ArenaInfo::arenaSize();
}

int64
HUSD_HuskEngine::RenderStats::getPeakMemory() const
{
    int64       mem;
    if (import(mem, STAT_TOKEN(peakMemory)))
        return mem;

    static int64        thePeakMemory = 0;
    thePeakMemory = SYSmax(thePeakMemory, getMemory());
    return thePeakMemory;
}

bool
HUSD_HuskEngine::RenderStats::importCount(UT_Vector2I &val, CountType type) const
{
    const char  *token = countType(type);
    if (import(val, token))
        return true;
    if (import(val.x(), token))
    {
        val.y() = val.x();
        return true;
    }
    return false;
}

bool
HUSD_HuskEngine::RenderStats::importCount(int64 &val, CountType type) const
{
    return import(val, countType(type));
}

exint
HUSD_HuskEngine::RenderStats::size() const
{
    return myStorage ? ((const VtDictionary *)myStorage)->size() : 0;
}

void
HUSD_HuskEngine::RenderStats::fillOptions(UT_Options &opts) const
{
    if (!myStorage)
        return;

    const VtDictionary  &dict = *(const VtDictionary *)myStorage;
    int64               iv;
    fpreal64            fv;
    UT_StringHolder     sv;
    UT_Vector2D         v2;
    UT_Vector3D         v3;
    UT_Vector4D         v4;
    UT_Matrix2D         m2;
    UT_Matrix3D         m3;
    UT_Matrix4D         m4;
    UT_StringArray      sa;
    UT_Int64Array       ia;
    UT_Fpreal64Array    fa;

    for (auto &&item : dict)
    {
        UT_StringHolder key(item.first);
        const VtValue   &v = item.second;
        if (intValue(iv, v))
            opts.setOptionI(key, iv);
        else if (realValue(fv, v))
            opts.setOptionF(key, fv);
        else if (stringValue(sv, v))
            opts.setOptionS(key, sv);

        else if (v2value(v2, v))
            opts.setOptionV2(key, v2);
        else if (v3value(v3, v))
            opts.setOptionV3(key, v3);
        else if (v4value(v4, v))
            opts.setOptionV4(key, v4);

        else if (m2value(m2, v))
            opts.setOptionM2(key, m2);
        else if (m3value(m3, v))
            opts.setOptionM3(key, m3);
        else if (m4value(m4, v))
            opts.setOptionM4(key, m4);

        else if (intArray(ia, v))
            opts.setOptionIArray(key, ia);
        else if (realArray(fa, v))
            opts.setOptionFArray(key, fa);
        else if (stringArray(sa, v))
            opts.setOptionSArray(key, sa);
        else
        {
            UT_OStringStream    sos;
            sos << v << std::ends;
            opts.setOptionS(key, sos.str());
        }
    }
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

void
HUSD_HuskEngine::RenderStats::dump(UT_JSONWriter &w) const
{
    UT_Options      opts;
    fillOptions(opts);

    w.jsonBeginMap();
    for (UT_Options::ordered_iterator it = opts.obegin(); !it.atEnd(); ++it)
    {
        w.jsonKeyToken(it.name());
        it.entry()->saveJSON(w, true);
    }
    w.jsonEndMap();
}

void
HUSD_HuskEngine::RenderStats::setStorage(const VtDictionary &v)
{
    freeStorage();
    if (v.size())
        myStorage = new VtDictionary(v);
}

void
HUSD_HuskEngine::RenderStats::freeStorage()
{
    if (myStorage)
        delete (VtDictionary *)(myStorage);
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

bool
HUSD_HuskEngine::loadStage(const UT_StringHolder &usdfile,
        const UT_StringHolder &resolver_context_file)
{
    return myEngine->loadStage(usdfile, resolver_context_file);
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
        const char *complexity)
{
    myEngine->releaseRendererPlugin();
    return myEngine->setRendererPlugin(*settings.myOwner, complexity);
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
