/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	HUSD_HuskImaging.h (HUSD Library, C++)
 *
 * COMMENTS:
 */

#include "HUSD_HuskImaging.h"
#include "HUSD_Info.h"
#include "HUSD_Path.h"
#include "HUSD_RenderSettings.h"
#include "HUSD_RendererInfo.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Format.h"
#include "XUSD_ImagingEngine.h"
#include "XUSD_ImagingEngineHusk.h"
#include "XUSD_RenderSettings.h"
#include "XUSD_Tokens.h"
#include "XUSD_Utils.h"
#include <IMG/IMG_FileParms.h>
#include <PY/PY_Python.h>
#include <PY/PY_AutoObject.h>
#include <UT/UT_ArenaInfo.h>
#include <UT/UT_Debug.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_EnvControl.h>
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
#include <UT/UT_JSONValueArray.h>
#include <UT/UT_JSONValueMap.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_Regex.h>
#include <FS/FS_Info.h>
#include <SYS/SYS_ParseNumber.h>
#include <SYS/SYS_Time.h>

#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/size2.h>
#include <pxr/base/gf/size2.h>
#include <pxr/base/gf/size3.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/pyPtrHelpers.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/utils.h>
#include <pxr/imaging/hdsi/legacyDisplayStyleOverrideSceneIndex.h>
#include <pxr/imaging/hdsi/primTypePruningSceneIndex.h>
#include <pxr/imaging/hdsi/sceneGlobalsSceneIndex.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/shadowAPI.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImaging/rootOverridesSceneIndex.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/selectionSceneIndex.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>
#include <pxr/usd/usdRender/pass.h>

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

    static bool
    areEqual(const HdAovDescriptor &a, const HdAovDescriptor &b)
    {
        return a.format == b.format
            && a.multiSampled == b.multiSampled
            && a.clearValue == b.clearValue
            && a.aovSettings == b.aovSettings;
    }

    static bool
    areEqual(const HdAovDescriptorList &a, const HdAovDescriptorList &b)
    {
        if (a.size() != b.size())
            return false;
        for (exint i = 0, n = a.size(); i < n; ++i)
        {
            if (!areEqual(a[i], b[i]))
                return false;
        }
        return true;
    }

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

namespace
{
    #define HUSD_CONVERT_FROM(TYPE) \
        if (v.IsHolding<TYPE>()) { result = v.UncheckedGet<TYPE>(); return true; } \
        /* end macro */
    bool
    fromVtValue(const VtValue &v, int64 &result)
    {
        HUSD_CONVERT_FROM(int64);
        HUSD_CONVERT_FROM(int32);
        HUSD_CONVERT_FROM(uint64);
        HUSD_CONVERT_FROM(uint32);
        return false;
    }
    bool
    fromVtValue(const VtValue &v, fpreal64 &result)
    {
        HUSD_CONVERT_FROM(fpreal64);
        HUSD_CONVERT_FROM(fpreal32);
        int64   ival;
        if (fromVtValue(v, ival))
        {
            result = ival;
            return true;
        }
        return false;
    }
    #undef HUSD_CONVERT_FROM
    #define HUSD_CONVERT_FROM(TYPE) \
        if (v.IsHolding<TYPE>()) { \
            result = toStr(v.UncheckedGet<TYPE>()); \
            return true; \
        } \
        /* end macro */

    static UT_StringHolder toStr(const TfToken &t)
    {
        if (t.IsEmpty())
            return UT_StringHolder::theEmptyString;
        return UT_StringHolder(t.GetText());
    }
    static UT_StringHolder toStr(const std::string &t)
        { return UT_StringHolder(t); }
    static UT_StringHolder toStr(const UT_StringHolder &t)
        { return t; }
    static UT_StringHolder toStr(const SdfPath &t)
        { return UT_StringHolder(t.GetAsString()); }

    bool
    fromVtValue(const VtValue &v, UT_StringHolder &result)
    {
        HUSD_CONVERT_FROM(TfToken);
        HUSD_CONVERT_FROM(std::string);
        HUSD_CONVERT_FROM(UT_StringHolder);
        return false;
    }
    #undef HUSD_CONVERT_FROM
}

struct HUSD_HuskImaging::USDContents
{
    bool        isValid() const { return myStage && myStage->GetPseudoRoot(); }
    void        deleteHydraResources() { myEngine->DestroyHydraResources(); }
    fpreal      stageFPS() const
    {
        return myStage ? myStage->GetTimeCodesPerSecond() : 24;
    }
    template <typename T>
    bool        stageMetadata(const TfToken &token, T &data) const
    {
        if (!myStage)
            return false;
        VtValue val;
        if (myStage->GetMetadata(token, &val))
            return fromVtValue(val, data);
        return false;
    }
    void        newEngine(bool enable_gpu)
    {
        XUSD_ImagingEngine::Parameters      parms;
        parms.use_scene_indices = HUSD_Info::getUsingStageSceneIndex();
        parms.enable_gpu_context = enable_gpu;
        parms.fast_path_color = false;
        myEngine = XUSD_ImagingEngine::createImagingEngine(parms);
        if (!myEngine)
        {
            if (enable_gpu)
                UT_ErrorLog::errorOnce("Unable to create GPU imaging engine for husk");
            myEngine = UTmakeUnique<XUSD_ImagingEngineHusk>(parms);
        }
    }
    void        setRenderSetting(const TfToken &token, const VtValue &value)
    {
        myEngine->SetRendererSetting(token, value);
    }
    void        setAOVs(const TfTokenVector &aovs,
                        const HdAovDescriptorList &aovdescs)
    {
        if (aovs != myAOVs || !areEqual(aovdescs, myAOVDescs))
        {
            myAOVs = aovs;
            myAOVDescs = aovdescs;
            myEngine->SetRendererAovsDescs(aovs, aovdescs);
        }
    }

    UT_UniquePtr<XUSD_ImagingEngine>	myEngine;
    UsdStageRefPtr			myStage;
    TfToken				myRendererId;
    TfTokenVector			myRenderTags;
    TfTokenVector			myAOVs;
    HdAovDescriptorList			myAOVDescs;
    HdRenderSettingsMap			myRenderSettings;
};

const VtValue *
HUSD_HuskImaging::RenderStats::findForImport(const UT_StringRef &name) const
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
HUSD_HuskImaging::RenderStats::size() const
{
    return myStorage ? ((const VtDictionary *)myStorage)->size() : 0;
}

void
HUSD_HuskImaging::RenderStats::dump() const
{
    UT_AutoJSONWriter   w(std::cerr, false);
    dump(*w);
}

void
HUSD_HuskImaging::RenderStats::dump(UT_WorkBuffer &buffer) const
{
    UT_AutoJSONWriter   w(buffer);
    dump(*w);
}

bool
HUSD_HuskImaging::RenderStats::save(UT_JSONWriter &w) const
{
    if (!myStorage)
        return false;

    return HUSDconvertDictionary(w, *(const VtDictionary *)(myStorage));
}

void
HUSD_HuskImaging::RenderStats::setStorage(const VtDictionary &v)
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
HUSD_HuskImaging::RenderStats::freeStorage()
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
HUSD_HuskImaging::RenderStats::jsonStats()
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

class HUSD_HuskImaging::UT_ErrorDelegate::errorImpl final
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

HUSD_HuskImaging::UT_ErrorDelegate::UT_ErrorDelegate(bool all_errors)
    : myImpl(new errorImpl(all_errors))
{
}

HUSD_HuskImaging::UT_ErrorDelegate::~UT_ErrorDelegate()
{
}


HUSD_HuskImaging::HUSD_HuskImaging(bool enable_gpu_context)
    : myContents(new USDContents())
    , myUSDTimeStamp(0)
    , myComplexity(8)
    , mySceneMaterials(true)
    , mySceneLights(true)
    , myEnableGPU(enable_gpu_context)
{
    myContents->newEngine(myEnableGPU);
}

HUSD_HuskImaging::~HUSD_HuskImaging()
{
}

void
HUSD_HuskImaging::setVariantSelectionFallbacks(
        const UT_StringMap<UT_StringArray> &fallbacks)
{
    PcpVariantFallbackMap pcpfallbacks;
    HUSDconvertVariantSelectionFallbacks(fallbacks, pcpfallbacks);
    UsdStage::SetGlobalVariantFallbacks(pcpfallbacks);
}

bool
HUSD_HuskImaging::loadStage(const UT_StringHolder &usdfile,
        const UT_StringHolder &resolver_context_file,
        const UT_StringMap<UT_StringHolder> &resolver_context_strings,
        const char *mask /*=nullptr*/)
{
    UT_ErrorLog::format(2, "Loading {}", usdfile);
    ArResolverContext resolver_context;
    UT_StringMap<UT_StringHolder> combined_resolver_context_strings;

    // Load resolver context strings from the usd file.
    HUSD_Info::getResolverContextStrings(usdfile,
        combined_resolver_context_strings);
    // Combine the strings from the file with the strings specified on the
    // command line.
    combined_resolver_context_strings.insert(resolver_context_strings.begin(),
        resolver_context_strings.end());

    if (!combined_resolver_context_strings.empty())
    {
        std::vector<std::pair<std::string, std::string>> stdstrs;
        for (auto &&it : combined_resolver_context_strings)
        {
            UT_ErrorLog::format(2,
                "Resolver context: {} = {}", it.first, it.second);
            stdstrs.push_back(std::make_pair(
                it.first.toStdString(), it.second.toStdString()));
        }
        resolver_context = ArGetResolver().CreateContextFromStrings(stdstrs);
    }
    else if (resolver_context_file.isstring())
    {
        UT_ErrorLog::format(2, "Resolver context: {}", resolver_context_file);
        resolver_context = ArGetResolver().CreateDefaultContextForAsset(
            resolver_context_file.toStdString());
    }
    else
    {
        resolver_context = ArGetResolver().CreateDefaultContext();
    }

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
    if (!UTisstring(mask))
        myContents->myStage = UsdStage::Open(usdfile.toStdString(), resolver_context);
    else
    {
        UsdStagePopulationMask population_mask;
        // Check and split population mask based on ' ' or ',' delimiters
        const UT_Regex delimiters("[ |,]");
        UT_StringArray prim_paths;
        delimiters.split(mask, prim_paths);
        for (const auto& path: prim_paths)
            population_mask.Add(SdfPath(path.toStdString()));

        myContents->myStage = UsdStage::OpenMasked(usdfile.toStdString(), resolver_context,
                                       population_mask);
    }

    if (!myContents->myStage)
    {
        UT_ErrorLog::error("Unable to load USD file '{}'", usdfile);
        return false;
    }
    return true;
}

bool
HUSD_HuskImaging::isValid() const
{
    return myContents->isValid();
}

const UT_StringHolder &
HUSD_HuskImaging::usdFile() const
{
    return myUSDFile;
}

time_t
HUSD_HuskImaging::usdTimeStamp() const
{
    return myUSDTimeStamp;
}

void
HUSD_HuskImaging::setRenderPassPrimPath(const UT_StringRef &path)
{
    myContents->myEngine->SetActiveRenderPassPrimPath(
        SdfPath(path.toStdString()));
}

void
HUSD_HuskImaging::showHdGpProcedurals(bool show)
{
    myContents->myEngine->showHdGpProcedurals(show);
}

bool
HUSD_HuskImaging::getVerboseCallback(UT_StringHolder &callback,
                                    fpreal &interval) const
{
    callback = myRendererInfo.huskVerboseScript();
    interval = myRendererInfo.huskVerboseInterval();
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
HUSD_HuskImaging::stageFPS() const
{
    return myContents->stageFPS();
}

bool
HUSD_HuskImaging::stageMetadata(const char *name, fpreal64 &data) const
{
    TfToken     token(name);
    return myContents->stageMetadata(token, data);
}

bool
HUSD_HuskImaging::stageMetadata(const char *name, int64 &data) const
{
    TfToken     token(name);
    return myContents->stageMetadata(token, data);
}

bool
HUSD_HuskImaging::stageMetadata(const char *name, UT_StringHolder &data) const
{
    TfToken     token(name);
    return myContents->stageMetadata(token, data);
}

PY_PyObject *
HUSD_HuskImaging::pyStage() const
{
    return (PY_PyObject *)TfMakePyPtr<UsdStageWeakPtr>::Execute(
            myContents->myStage).first;
}

bool
HUSD_HuskImaging::updateHeadlight(const UT_StringHolder &style,
        fpreal frame)
{
    UsdTimeCode time(frame);

    UT_ASSERT(style == HusdHuskTokens->distant
            || style == HusdHuskTokens->dome);

    if (!isValid())
        return false;         // Invalid stage

    static const std::string    theXPathStr("/husk_headlight");
    static const std::string    theLPathStr = theXPathStr + "/__the_headlight";
    static const SdfPath        theXPath(theXPathStr);
    static const SdfPath        theLPath(theLPathStr);

    UsdPrim     xprim = myContents->myStage->GetPrimAtPath(theXPath);
    if (!xprim)
        xprim = myContents->myStage->DefinePrim(theXPath, UsdGeomTokens->Xform);

    UsdGeomXformable    xform(xprim);
    if (!xform)
        return false;

    SdfPath             cam_path = myContents->myEngine->GetCameraPath();
    UsdGeomCamera       cam(myContents->myStage->GetPrimAtPath(cam_path));
    if (!cam)
        return false;

    xform.MakeMatrixXform().Set(cam.ComputeLocalToWorldTransform(time));

    if (!myContents->myStage->GetPrimAtPath(theLPath))
    {
        if (style == HusdHuskTokens->dome)
        {
            auto light = UsdLuxDomeLight::Define(myContents->myStage, theLPath);
            if (const char *defaultDomeLightTex =
                    UT_EnvControl::getString(ENV_HOUDINI_DEFAULT_DOMELIGHT_TEXTURE))
                light.GetTextureFileAttr().Set(SdfAssetPath(defaultDomeLightTex));
        }
        else if (style == HusdHuskTokens->distant)
        {
#if 0
            static constexpr float theIntensity = 15000;   // Hydra default
#else
            static constexpr float theIntensity = 57225;   // Houdini default
#endif
            auto light = UsdLuxDistantLight::Define(myContents->myStage, theLPath);
            light.GetAngleAttr().Set(0.53);
            light.GetIntensityAttr().Set(theIntensity);
            auto shadowAPI = UsdLuxShadowAPI::Apply(myContents->myStage->GetPrimAtPath(theLPath));
            shadowAPI.GetShadowEnableAttr().Set(false);
        }
        else
        {
            return false;   // Unsupported headlight type
        }
    }

    return true;
}

PY_PyObject *
HUSD_HuskImaging::pySettingsDict(const HUSD_RenderSettings &s) const
{
    UT_JSONValue        value;
    {
        UT_AutoJSONWriter       w(value);
        s.myOwner->dump(*w);
    }
    return xusd_PyObject(value);
}

bool
HUSD_HuskImaging::Render(fpreal frame)
{
    const UsdPrim       &root = myContents->myStage->GetPseudoRoot();

    XUSD_ImagingRenderParams rparms;
    rparms.myFrame = frame;
    rparms.myRenderTags = myContents->myRenderTags;
    rparms.myComplexity = myComplexity;
    rparms.myEnableSceneMaterials = mySceneMaterials;
    rparms.myEnableSceneLights = mySceneLights;
    myContents->myEngine->DispatchRender(root, rparms);
    myContents->myEngine->CompleteRender(rparms, false);

    return true;
}

bool
HUSD_HuskImaging::IsConverged() const
{
    return myContents->myEngine->IsConverged();
}

void
HUSD_HuskImaging::setDataWindow(const UT_DimRect &dataWindow)
{
    myContents->myEngine->SetRenderViewport(
            GfVec4d(dataWindow.x(),
		    dataWindow.y(),
		    dataWindow.width(),
		    dataWindow.height()));
}

UT_StringHolder
HUSD_HuskImaging::pluginName() const
{
    return UT_StringHolder(myContents->myRendererId.GetText());
}

bool
HUSD_HuskImaging::setRendererPlugin(const HUSD_RenderSettings &settings,
        const UT_StringHolder &delegate,
        const DelegateParms &rparms)
{
    bool        recreate = false;

    if (mySceneMaterials != rparms.mySceneMaterialsEnabled
            || mySceneLights != rparms.mySceneLightsEnabled)
    {
        mySceneMaterials = rparms.mySceneMaterialsEnabled;
        mySceneLights = rparms.mySceneLightsEnabled;
        recreate = true;
    }
    static const UT_Map<UT_StringHolder, int> theComplexityMap({
	    { "low",	  0 },
	    { "medium",   2 },
	    { "high",	  4 },
	    { "veryhigh", 8 },
    });

    auto complexity = theComplexityMap.find(rparms.myComplexity);
    if (complexity == theComplexityMap.end())
    {
        const char      *start = rparms.myComplexity;
        const char      *end = start + strlen(start);
        if (SYSparseInteger(start, end, myComplexity) != SYS_ParseStatus::Success)
        {
            UT_ErrorLog::error("Unknown complexity option {} - using veryhigh",
                    rparms.myComplexity);
            myComplexity = 8;
        }
        else if (myComplexity < 0 || myComplexity > 8)
        {
            UT_ErrorLog::error("Complexity out of range {} - using {}", myComplexity,
                    SYSclamp(myComplexity, 0, 8));
            myComplexity = SYSclamp(myComplexity, 0, 8);
        }
    }
    else
    {
	myComplexity = complexity->second;
    }

    // Get the rendering purpose
    TfTokenVector       rendertags;
    for (const auto &t : settings.myOwner->purpose())
    {
	if (t == UsdGeomTokens->default_)
	{
	    rendertags.push_back(HdTokens->geometry);
	    rendertags.push_back(UsdGeomTokens->render);
	}
	else
	{
	    rendertags.push_back(t);
	}
    }
    if (rendertags != myContents->myRenderTags)
    {
        myContents->myRenderTags = rendertags;
        recreate = true;
    }

    // Special case: TfToken() selects the first plugin in the list.
    TfToken actualId = TfToken(delegate.c_str());
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
    if (!settings.myOwner->supportedDelegate(actualId))
	return false;

    if (actualId != myContents->myRendererId)
        recreate = true;

    HUSD_RendererInfo::getRendererInfo(
        actualId.GetText(), UT_StringHolder()).preloadLibraries();

    myContents->myAOVs = TfTokenVector();
    myContents->myAOVDescs = HdAovDescriptorList();

    // We need to set the image engine camera before the engine creates the
    // scene delegate
    SdfPath     camera = settings.myOwner->cameraPath(nullptr);
    if (camera.IsEmpty())
    {
	UT_ErrorLog::error("Missing rendering camera");
	return false;
    }
    myContents->myEngine->SetCameraPath(camera);

    // Pull old delegate/task controller state.
    if (recreate)
    {
        UT_ErrorLog::format(8, "Creating renderer plugin: {}", actualId);
        myContents->deleteHydraResources();
        if (!myContents->myEngine->SetRendererPlugin(actualId,
                    settings.myOwner->renderSettings()))
        {
            return false;
        }
    }

    myContents->myRendererId = myContents->myEngine->GetCurrentRendererId();
    myRendererInfo = HUSD_RendererInfo::getRendererInfo(
                            UT_StringHolder(myContents->myRendererId.GetText()),
                            UT_StringHolder());

    myContents->myRenderSettings = settings.myOwner->renderSettings();
    myContents->setRenderSetting(HusdHuskTokens->stageMetersPerUnit,
                    VtValue(UsdGeomGetStageMetersPerUnit(myContents->myStage)));

    return true;
}

bool
HUSD_HuskImaging::restartRendererPlugin(const HUSD_RenderSettings &settings,
        const UT_StringHolder &delegate,
        const DelegateParms &rparms)
{
    myContents->newEngine(myEnableGPU);
    // Clear out renderer to forcibly recreate the renderer plugin
    myContents->myRendererId = TfToken();
    return setRendererPlugin(settings, delegate, rparms);
}


bool
HUSD_HuskImaging::setAOVs(const HUSD_RenderSettings &settings,
        HUSD_CustomProductAction custom_product_action)
{
    TfTokenVector	aovs;
    HdAovDescriptorList aovdescs;
    if (!settings.myOwner->collectAovs(aovs, custom_product_action, aovdescs))
	return false;

    UT_ASSERT(settings.myOwner->products().size());
    UT_ASSERT(aovs.size() == aovdescs.size());
    if (!aovs.size())
    {
	UT_ErrorLog::error("No AOVs defined for render, {}",
                "not all delegates will function properly");
    }
    myContents->setAOVs(aovs, aovdescs);

    return true;
}

void
HUSD_HuskImaging::updateSettings(const HUSD_RenderSettings &settings)
{
    for (const auto &item : settings.myOwner->renderSettings())
    {
        auto &&it = myContents->myRenderSettings.find(item.first);
        if (it != myContents->myRenderSettings.end() && it->second == item.second)
        {
            continue;
        }
        myContents->setRenderSetting(item.first, item.second);
        if (item.first == HusdHuskTokens->renderCameraPath)
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
            myContents->myEngine->SetCameraPath(camera);
        }
    }
    myContents->myRenderSettings = settings.myOwner->renderSettings();
}

void
HUSD_HuskImaging::delegateRenderProducts(const HUSD_RenderSettings &settings,
        int pgroup)
{
    myContents->setRenderSetting(HusdHuskTokens->delegateRenderProducts,
            settings.myOwner->delegateRenderProducts(pgroup));
}

void
HUSD_HuskImaging::rasterRenderProducts(const HUSD_RenderSettings &settings,
        int pgroup)
{
    myContents->setRenderSetting(HusdHuskTokens->rasterRenderProducts,
            settings.myOwner->rasterRenderProducts(pgroup));
}

bool
HUSD_HuskImaging::lightOnStage(const HUSD_TimeCode &tc) const
{
    return HUSDhasAnyVisibleLights(myContents->myStage, tc);
}

bool
HUSD_HuskImaging::isDiskRenderProduct(const UT_StringRef &productType) const
{
    return myRendererInfo.diskProductTypes().contains(productType);
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
HUSD_HuskImaging::addMetadata(IMG_Metadata &storage,
        const UT_StringMap<UT_StringHolder> &husk_metadata,
        const UT_JSONValue &value) const
{
    husd_JSONExpander   json_values(value);
    UT_WorkBuffer       tmp;
    for (auto item : husk_metadata)
    {
        tmp.clear();
        json_values.myNumFound = 0;
        UTVariableScan(tmp, item.second.c_str(), expandJSONPath, &json_values);
        if (json_values.myNumFound || item.second.findCharIndex('$') < 0)
        {
            // Only set metadata if the items are found (or there's no variable)
            storage.addTypedString(item.first.c_str(), tmp.buffer());
            //UTdebugFormat("Add typed string: {} {}", item.first, tmp);
        }
    }
}

void
HUSD_HuskImaging::addMetadata(IMG_FileParms &fparms,
        const UT_JSONValue &base_dict,
        const char *render_stats) const
{
    addMetadata(fparms.metadata(), base_dict, render_stats);
}

void
HUSD_HuskImaging::addMetadata(IMG_Metadata &storage,
        const UT_JSONValue &base_dict,
        const char *render_stats) const
{
    const auto  &husk_metadata = myRendererInfo.huskMetadata();
    const auto  &stats_metadata = myRendererInfo.huskStatsMetadata();

    if (!stats_metadata && !husk_metadata.size())
        return;         // Nothing to do

    // Convert VtDictionary to a UT_JSONValue
    UT_JSONValue         stats;
    {
        UT_AutoJSONWriter       w(stats);
        HUSDconvertDictionary(*w, myContents->myEngine->GetRenderStats(), nullptr);
    }

    if (stats_metadata)
    {
        // If the delegate doesn't specify any specific data, convert all the
        // stats to metadata.
        const UT_JSONValueMap   *smap = stats.getMap();
        if (smap && smap->size())
        {
            UT_StringArray      keys;
            smap->getKeys(keys);
            for (exint i = 0, n = smap->size(); i < n; ++i)
            {
                if (keys[i].multiMatch(stats_metadata))
                    storage.add(keys[i], *smap->get(i));
            }
        }
    }

    if (!husk_metadata.size())
        return;

    UT_JSONValue         combined;
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
    map->insert(render_stats, stats);

    addMetadata(storage, husk_metadata, combined);
}

bool
HUSD_HuskImaging::rendererName(RenderStats &stats,
        UT_StringHolder &sval) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->rendererName.GetText());
    if (j && j->getStringHolder())
        sval = *j->getStringHolder();
    else
        sval = myRendererInfo.menuLabel();
    return true;
}

bool
HUSD_HuskImaging::activeBuckets(RenderStats &stats,
        UT_Array<ActiveBucket> &buckets) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->activeBuckets.GetText());
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

int
HUSD_HuskImaging::huskErrorStatus(RenderStats &stats) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->huskErrorStatus.GetText());
    if (!j)
        return 0;
    int64       val;
    if (!j->import(val))
        return 0;
    return val;
}

bool
HUSD_HuskImaging::percentDone(RenderStats &stats,
        fpreal &pct, bool final) const
{
    if (final)
    {
        pct = 100;
        return true;
    }
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->percentDone.GetText());
    if (j && j->isNumber())
    {
        pct = j->getF();
        return true;
    }
    j = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->fractionDone.GetText());
    if (j && j->isNumber())
    {
        pct = j->getF() * 100;
        return true;
    }
    pct = 0;
    return false;
}

bool
HUSD_HuskImaging::renderStage(RenderStats &stats, UT_StringHolder &stage) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->rendererStage.GetText());
    if (j && j->import(stage))
        return true;
    stage.clear();
    return false;
}

bool
HUSD_HuskImaging::renderTime(RenderStats &stats,
        fpreal &wall, fpreal &user, fpreal &sys) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *jw = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->totalClockTime.GetText());
    const UT_JSONValue  *ju = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->totalUTime.GetText());
    const UT_JSONValue  *js = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->totalSTime.GetText());
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
HUSD_HuskImaging::renderMemory(RenderStats &stats) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->totalMemory.GetText());
    if (j && j->isNumber())
        return j->getI();
    return UT_ArenaInfo::arenaSize();
}

int64
HUSD_HuskImaging::renderPeakMemory(RenderStats &stats) const
{
    const UT_JSONValue  &jstat = stats.jsonStats();
    const UT_JSONValue  *j = myRendererInfo.findStatsData(jstat,
            HusdHdRenderStatsTokens->peakMemory.GetText());
    if (j && j->isNumber())
        return j->getI();
    static int64        thePeakMemory = 0;
    thePeakMemory = SYSmax(thePeakMemory, renderMemory(stats));
    return thePeakMemory;
}

void
HUSD_HuskImaging::setKarmaRandomSeed(int64 seed) const
{
    myContents->setRenderSetting(HusdHuskTokens->randomseed, VtValue(seed));
}

void
HUSD_HuskImaging::mplayMouseClick(int x, int y) const
{
    GfVec2i     mouse(x, y);
    myContents->setRenderSetting(HusdHuskTokens->viewerMouseClick, VtValue(mouse));
}

void
HUSD_HuskImaging::huskSnapshot() const
{
    myContents->setRenderSetting(HusdHuskTokens->husk_snapshot, VtValue(true));
}

void
HUSD_HuskImaging::huskInteractive() const
{
    myContents->setRenderSetting(HusdHuskTokens->houdini_interactive,
            VtValue(HusdHuskTokens->husk_mplay));
}

void
HUSD_HuskImaging::huskConvergedMetadata(const UT_JSONValue &base_dict) const
{
    IMG_Metadata tmpstorage;
    addMetadata(tmpstorage, base_dict);
    UT_JSONValue value;
    {
        UT_AutoJSONWriter w(value);
        tmpstorage.save(w);
    }
    myContents->setRenderSetting(HusdHuskTokens->husk_productmetadata,
        VtValue(value.toString()));
}

HUSD_RenderBuffer
HUSD_HuskImaging::GetRenderOutput(const UT_StringRef &name) const
{
    return HUSD_RenderBuffer(myContents->myEngine->GetRenderOutput(TfToken(name.c_str())));
}

void
HUSD_HuskImaging::fillStats(RenderStats &stats) const
{
    stats.setStorage(myContents->myEngine->GetRenderStats());
}

bool
HUSD_HuskImaging::initSettings(XUSD_RenderSettings &settings,
        const char *settings_path,
        HUSD_RenderSettingsContext &ctx) const
{
    if (!isValid())
        return false;
    return settings.init(myContents->myStage,
            SdfPath(settings_path),
            ctx.impl());
}

bool
HUSD_HuskImaging::updateSettings(XUSD_RenderSettings &settings,
        HUSD_RenderSettingsContext &ctx,
        HUSD_CustomProductAction custom_product_action) const
{
    return settings.updateFrame(myContents->myStage,
            ctx.impl(),
            custom_product_action);
}

bool
HUSD_HuskImaging::resolveProducts(XUSD_RenderSettings &settings,
        HUSD_RenderSettingsContext &ctx,
        HUSD_CustomProductAction custom_product_action) const
{
    return settings.resolveProducts(myContents->myStage,
            ctx.impl(),
            custom_product_action);
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
        // Rather than using XUSDfindPrimitives(), we can easily find the
        // render settings under the /Render path.
	list.clear();
	UsdPrim	render = stage->GetPrimAtPath(SdfPath("/Render"));
	if (render)
	{
	    for (auto &&k : render.GetAllDescendants())
	    {
		UsdRenderSettings	sets(k);
		if (sets)
		    list.push_back(sets);
	    }
	}
    }

    static void
    getAllRenderPasses(const UsdStageRefPtr &stage,
	    VtArray<UsdRenderPass> &list)
    {
        // Rather than using XUSDfindPrimitives(), we can easily find the
        // render passes under the /Render path.
	list.clear();
	UsdPrim	render = stage->GetPrimAtPath(SdfPath("/Render"));
	if (render)
	{
	    for (auto &&k : render.GetAllDescendants())
	    {
		UsdRenderPass	pass(k);
		if (pass)
		    list.push_back(pass);
	    }
	}
    }
}

void
HUSD_HuskImaging::dumpUSD() const
{
    UTdebugFormat("USD Tree");
    if (myContents->isValid())
        dumpNode(0, myContents->myStage->GetPseudoRoot());
}

UT_StringHolder
HUSD_HuskImaging::settingsPath(const char *path, const char *pass_path) const
{
    UsdRenderSettings	sets;
    if (UTisstring(path))
    {
        sets = UsdRenderSettings::Get(myContents->myStage, SdfPath(path));
        if (!sets)
        {
            UT_WorkBuffer	tmp;
            tmp.sprintf("/Render/%s", path);
            sets = UsdRenderSettings::Get(myContents->myStage, SdfPath(tmp.buffer()));
        }
        if (sets)
            return HUSD_Path(sets.GetPrim().GetPath()).pathStr();
        return UT_StringHolder();
    }
    // Second priority goes to the renderSource attribute specified on the
    // RenderPass prim
    if (UTisstring(pass_path))
    {
        UsdRenderPass pass = UsdRenderPass::Get(myContents->myStage, SdfPath(pass_path));
        if (pass)
        {
            SdfPathVector targets;
            if (pass.GetRenderSourceRel().GetForwardedTargets(&targets) &&
                targets.size() >= 1)
            {
                UT_ASSERT_MSG(targets.size() == 1,
                    "Multiple RenderSettings prims given in renderSource");
                const SdfPath &settings_path = targets[0]; 
                sets = UsdRenderSettings::Get(myContents->myStage, settings_path);
                if (sets)
                    return HUSD_Path(settings_path).pathStr();
                return UT_StringHolder();
            }
        }
    }
    // Try to get the default settings
    sets = UsdRenderSettings::GetStageRenderSettings(myContents->myStage);
    if (sets)
    {
        UT_ErrorLog::format(1, "Using stage default settings: {}",
                sets.GetPrim().GetPath());
        return HUSD_Path(sets.GetPrim().GetPath()).pathStr();
    }
    // There's no default setting - but if there's only one setting, use it
    // instead.
    VtArray<UsdRenderSettings>	allsets;
    getAllRenderSettings(myContents->myStage, allsets);
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

UT_StringHolder
HUSD_HuskImaging::passPath(const char *path) const
{
    if (!UTisstring(path))
        return UT_StringHolder();
    
    UsdRenderPass pass = UsdRenderPass::Get(myContents->myStage, SdfPath(path));
    // If we couldn't find a pass at the given location,
    // try searching again with a prefix of /Render
    if (!pass)
    {
        UT_WorkBuffer	tmp;
        tmp.sprintf("/Render/%s", path);
        pass = UsdRenderPass::Get(myContents->myStage, SdfPath(tmp.buffer()));
    }
    // If we've still not found a pass, give up
    if (!pass)
        return UT_StringHolder();
    
    return pass.GetPrim().GetPath().GetText();
}

void
HUSD_HuskImaging::listSettings(UT_StringArray &list) const
{
    VtArray<UsdRenderSettings>  sets;
    getAllRenderSettings(myContents->myStage, sets);

    for (const auto &s : sets)
        list.append(HUSD_Path(s.GetPath()).pathStr());
}

void
HUSD_HuskImaging::listPasses(UT_StringArray &list) const
{
    VtArray<UsdRenderPass>  passes;
    getAllRenderPasses(myContents->myStage, passes);

    for (const auto &p : passes)
        list.append(HUSD_Path(p.GetPath()).pathStr());
}

void
HUSD_HuskImaging::listCameras(UT_StringArray &list) const
{
    UT_Array<SdfPath>   cams;
    XUSD_RenderSettings::findCameras(cams, myContents->myStage->GetPseudoRoot());

    for (const auto &c : cams)
        list.append(HUSD_Path(c).pathStr());
}

void
HUSD_HuskImaging::defaultAOVDescriptor(const char *name,
        HdAovDescriptor &desc) const
{
   myContents->myEngine->GetDefaultAovDescriptor(TfToken(name));
}

bool
HUSD_HuskImaging::listDelegates(UT_StringArray &delegates)
{
    HfPluginDescVector    plugins;

    auto &&reg = HdRendererPluginRegistry::GetInstance();
    HdRendererPluginRegistry::GetInstance().GetPluginDescs(&plugins);

    UT_WorkBuffer	tmp;
    bool		some_unsupported = false;
    for (auto &&p : plugins)
    {
        // Make sure we preload any required extra libraries.
        HUSD_RendererInfo::getRendererInfo(
            p.id.GetText(), UT_StringHolder()).preloadLibraries();

        UT_StringHolder unsupported;
        auto plugin = reg.GetRendererPlugin(p.id);
        if (!plugin)
            unsupported = " - unable to allocate plugin";
        else if (!plugin->IsSupported())
        {
            unsupported = " - unsupported";
            some_unsupported = true;
        }
        tmp.format("{} ({}){}", p.id, p.displayName, unsupported);
        delegates.append(UT_StringHolder(tmp));
    }
    return some_unsupported;
}
