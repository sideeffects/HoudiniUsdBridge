/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	HUSD_RenderSettings.h (HUSD Library, C++)
 *
 * COMMENTS:
 */

#include "HUSD_RenderSettings.h"
#include "HUSD_HuskEngine.h"
#include "HUSD_Path.h"
#include "XUSD_RenderSettings.h"
#include "XUSD_Format.h"
#include "XUSD_HuskEngine.h"
#include "XUSD_Tokens.h"
#include <pxr/base/gf/size2.h>
#include <pxr/base/gf/size3.h>

#include <IMG/IMG_FileParms.h>
#include <FS/FS_Info.h>
#include <UT/UT_Debug.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_FileStat.h>
#include <UT/UT_Function.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_Rect.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_Vector2.h>
#include <UT/UT_Vector4.h>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    static constexpr UT_StringLit       theKarmaCheckpoint("karma:checkpoint");
    static constexpr UT_StringLit       theKarmaDeep("karma:deep");

    class husd_RenderSettingsContext final : public XUSD_RenderSettingsContext
    {
    public:
        husd_RenderSettingsContext(HUSD_RenderSettingsContext *impl)
            : myImpl(impl)
        {
        }

        void             initFromUSD(UsdRenderSettings &s) override;
        void             setDefaultSettings(const XUSD_RenderSettings &set,
                                    HdRenderSettingsMap &settings) const override;
        void             overrideSettings(const XUSD_RenderSettings &set,
                                    HdRenderSettingsMap &settings) const override;
        HdAovDescriptor defaultAovDescriptor(const TfToken &aov) const override;

        TfToken          renderer() const override;
        SdfPath          overrideCamera() const override;
        GfVec2i          defaultResolution() const override;
        GfVec2i          overrideResolution(const GfVec2i &res) const override;
        fpreal           overridePixelAspect(fpreal pa) const override;
        GfVec4f          overrideDataWindow(const GfVec4f &w) const override;
        bool             overrideDisableMotionBlur(bool v) const override;
        const char      *defaultPurpose() const override;
        const char      *overridePurpose() const override;
        fpreal           startFrame() const override;
        fpreal           frameInc() const override;
        int              frameCount() const override;
        fpreal           fps() const override;
        UsdTimeCode      evalTime() const override;
        const char      *defaultProductName() const override;
        const char      *overrideProductName(const XUSD_RenderProduct &p,
                                int pidx) const override;
        const char      *overrideSnapshotPath(const XUSD_RenderProduct &p,
                                int pidx) const override;
        const char      *overrideSnapshotSuffix(const XUSD_RenderProduct &p,
                                int pidx) const override;
        const char      *tileSuffix() const override;
        int              tileIndex() const override;
        bool             allowCameraless() const override;

    private:
        HUSD_RenderSettingsContext      *myImpl;
    };

    //
    // The relationship between husd_RenderVar/Product and
    // HUSD_RenderVar/Product is a little bit complicated.  This is because
    // objects can be created by the user (HUSD) or internally (XUSD/husd).
    //
    // We only ever create HUSD objects, which create the husd/XUSD objects in
    // their c-tor.  These XUSD objects start off life owned by the HUSD
    // object.  At a later time, ownership is passed to the XUSD_RenderSettings
    // through a unique ptr.
    //
    // If ownership is never passed, the HUSD object must delete the husd/XUSD
    // object.  If ownership is passed, the husd/XUSD object needs to delete the
    // HUSD object.
    class husd_RenderVar final : public XUSD_RenderVar
    {
    public:
        husd_RenderVar(HUSD_RenderVar *impl)
            : myImpl(impl)
            , myBound(false)
        {
        }
        ~husd_RenderVar() override
        {
            UT_ASSERT(myImpl && myImpl->myOwner == this);
            if (bound())
                delete myImpl;
        }
        UT_UniquePtr<XUSD_RenderVar>        bind()
        {
            UT_ASSERT(!bound());
            myBound = true;
            return UT_UniquePtr<XUSD_RenderVar>(this);
        }

        UT_UniquePtr<XUSD_RenderVar>    clone() const override
        {
            HUSD_RenderVar      *hv = myImpl->clone().release();
            husd_RenderVar      *xv = UTverify_cast<husd_RenderVar *>(hv->myOwner);
            return xv->bind();
        }
        void    copyDataFrom(const husd_RenderVar &src)
        {
            myHdDesc = src.myHdDesc;
            myAovName = src.myAovName;
            myAovToken = src.myAovToken;
            myDataFormat = src.myDataFormat;
            myPacking = src.myPacking;
        }
        bool    bound() const { return myBound; }

        HUSD_RenderVar  *myImpl;
        bool             myBound;
    };

    class husd_RenderProduct final : public XUSD_RenderProduct
    {
    public:
        husd_RenderProduct(HUSD_RenderProduct *impl)
            : myImpl(impl)
            , myBound(false)
        {
            impl->myOwner = this;
            myFilename = impl->defaultFilename();
            myPartname = impl->defaultFilename();
        }
        ~husd_RenderProduct() override
        {
            UT_ASSERT(myImpl && myImpl->myOwner == this);
            if (bound())
                delete myImpl;
        }

        UT_UniquePtr<XUSD_RenderProduct>        bind()
        {
            UT_ASSERT(!bound());
            myBound = true;
            return UT_UniquePtr<XUSD_RenderProduct>(this);
        }

        bool    bound() const { return myBound; }

        HdRenderSettingsMap     *renderSettingsPtr() { return &mySettings; }
        void    storeSetting(const TfToken &name, const VtValue &item)
        {
            mySettings[name] = item;
        }

        virtual UT_UniquePtr<XUSD_RenderVar>    newRenderVar() const override
        {
            HUSD_RenderVar      *hv = myImpl->newRenderVar().release();
            auto *xv = UTverify_cast<husd_RenderVar *>(hv->myOwner);
            return xv->bind();
        }
        void    bumpCapacity(exint n) { myVars.bumpCapacity(n); }
        void    addVar(const HUSD_RenderVar *var)
        {
            HUSD_RenderVar      *hv = var->clone().release();
            auto *src = UTverify_cast<husd_RenderVar *>(hv->myOwner);
            myVars.append(src->bind());
        }
        const UT_StringHolder   &filename() const { return myFilename; }
        const UT_StringHolder   &partname() const { return myPartname; }
        HUSD_RenderProduct      *myImpl;
        bool                     myBound;
    };

    class husd_RenderSettings final : public XUSD_RenderSettings
    {
    public:
        husd_RenderSettings(HUSD_RenderSettings *impl,
                const UT_StringHolder &prim_path,
                const UT_StringHolder &filename,
                time_t file_timestamp)
            : XUSD_RenderSettings(prim_path, filename, file_timestamp)
            , myImpl(impl)
        {
            myImpl->myOwner = this;
        }
        ~husd_RenderSettings() override
        {
            UT_ASSERT(myImpl && myImpl->myOwner == this);
            myImpl->myOwner = nullptr;
        }

        bool    supportedDelegate(const TfToken &token) const override
        {
            return myImpl->supportedDelegate(UT_StringRef(token.GetText()));
        }
        UT_UniquePtr<XUSD_RenderProduct>        newRenderProduct() const override
        {
            HUSD_RenderProduct  *hp = myImpl->newRenderProduct().release();
            auto        *xp = UTverify_cast<husd_RenderProduct *>(hp->myOwner);
            return xp->bind();
        }
        HdRenderSettingsMap     *renderSettingsPtr() { return &mySettings; }

        void    removeProduct(exint pnum)
        {
            myProducts.removeIndex(pnum);
            // Now, adjust the product groups.  The product groups are stored
            // as indices to the product list.
            for (int pg = 0, n = myProductGroups.size(); pg < n; ++pg)
            {
                auto &pgroup = myProductGroups[pg];
                // Traverse backwards since we may remove items
                for (int i = pgroup.size(); i-- > 0; )
                {
                    if (pgroup[i] == pnum)
                        pgroup.removeIndex(i);
                    else if (pgroup[i] > pnum)
                        pgroup[i]--;
                }
            }
        }
        void    addProduct(UT_UniquePtr<XUSD_RenderProduct> xp, int pgroup)
        {
            myProductGroups[pgroup].append(myProducts.size());
            myProducts.append(std::move(xp));
        }
        const HUSD_RenderSettings       &impl() const { return *myImpl; }
    private:
        HUSD_RenderSettings     *myImpl;
    };

    static bool
    toStr(UT_StringHolder &dest, const VtValue &v)
    {
        if (v.IsHolding<std::string>())
        {
            dest = v.UncheckedGet<std::string>();
            return true;
        }
        if (v.IsHolding<TfToken>())
        {
            dest = v.UncheckedGet<TfToken>().GetText();
            return true;
        }
        if (v.IsHolding<SdfPath>())
        {
            dest = HUSD_Path(v.UncheckedGet<SdfPath>()).pathStr();
            return true;
        }
        if (v.IsHolding<SdfAssetPath>())
        {
            const SdfAssetPath &path = v.UncheckedGet<SdfAssetPath>();
            const std::string &r = path.GetResolvedPath();
            if (r.empty())
                dest = path.GetAssetPath();
            else
                dest = r;
            return true;
        }
        return false;
    }

    template <typename T, typename V>
    static bool
    getValue(T &dest, const VtValue &v)
    {
        if (v.IsHolding<V>())
        {
            dest = v.UncheckedGet<V>();
            return true;
        }
        return false;
    }

    template <typename T, typename V, typename NEXT, typename... Types>
    static bool
    getValue(T &dest, const VtValue &v)
    {
        if (getValue<T, V>(dest, v))
            return true;
        return getValue<T, NEXT, Types...>(dest, v);
    }

    template <typename T, typename V>
    static bool
    getValue(T &dest, const UsdAttribute &attr)
    {
        V       value;
        if (attr.Get(&value))
        {
            dest = value;
            return true;
        }
        return false;
    }

    template <typename T, typename V, typename NEXT, typename... Types>
    static bool
    getValue(T &dest, const UsdAttribute &attr)
    {
        if (getValue<T, V>(dest, attr))
            return true;
        return getValue<T, NEXT, Types...>(dest, attr);
    }

    template <typename T, typename V, typename... Types>
    static bool
    doLookup(const UsdPrim &prim, T &val, const TfToken &name)
    {
        UsdAttribute    attr = prim.GetAttribute(name);
        if (!attr)
            return false;
        return getValue<T, V, Types...>(val, attr);
    }

    static bool
    mapLookup(const HdAovSettingsMap &map, const char *name, int64 &val)
    {
        auto it = map.find(TfToken(name));
        if (it == map.end())
            return false;
        return getValue<int64, bool, int32, int64>(val, it->second);
    }
    static bool
    mapLookup(const HdAovSettingsMap &map, const char *name, fpreal64 &val)
    {
        auto it = map.find(TfToken(name));
        if (it == map.end())
            return false;
        return getValue<fpreal64, fpreal32, fpreal64, bool, int32, int64>(val, it->second);
    }
    static bool
    mapLookup(const HdAovSettingsMap &map, const char *name, UT_Vector2i &val)
    {
        auto it = map.find(TfToken(name));
        if (it == map.end())
            return false;
        GfVec2i     tmp;
        if (!getValue<GfVec2i, GfVec2i>(tmp, it->second))
            return false;
        val = UT_Vector2i(tmp[0], tmp[1]);
        return true;
    }
    static bool
    mapLookup(const HdAovSettingsMap &map, const char *name, UT_StringHolder &val)
    {
        auto it = map.find(TfToken(name));
        if (it == map.end())
            return false;
        return toStr(val, it->second);
    }

    template <typename T> bool
    saveJSONFunc(UT_WorkBuffer &kbuf, UT_WorkBuffer &vbuf,
	    const char *prefix, const char *key, const VtValue &v)
    {
	if (!v.IsHolding<T>())
	    return false;
	if (prefix)
	    kbuf.sprintf("%s %s", prefix, key);
	else
	    kbuf.sprintf("%s", key);
	vbuf.format("{}", v.UncheckedGet<T>());
	return true;
    }

    template <> bool
    saveJSONFunc<GfHalf>(UT_WorkBuffer &kbuf, UT_WorkBuffer &vbuf,
	    const char *prefix, const char *key, const VtValue &v)
    {
	if (!v.IsHolding<GfHalf>())
	    return false;
	if (prefix)
	    kbuf.sprintf("%s %s", prefix, key);
	else
	    kbuf.sprintf("%s", key);
	vbuf.format("{}", float(v.UncheckedGet<GfHalf>()));
	return true;
    }

#define QUAT_SPECIALIZATION(QTYPE, V4TYPE, V3TYPE)\
    template <> bool \
    saveJSONFunc<QTYPE>(UT_WorkBuffer &kbuf, UT_WorkBuffer &vbuf, \
	    const char *prefix, const char *key, const VtValue &v) \
    { \
	if (!v.IsHolding<QTYPE>()) \
	    return false; \
	if (prefix) \
	    kbuf.sprintf("%s %s", prefix, key); \
	else \
	    kbuf.sprintf("%s", key); \
	const QTYPE &quat = v.UncheckedGet<QTYPE>(); \
	const V3TYPE &vec = quat.GetImaginary(); \
	vbuf.format("{}", V4TYPE(quat.GetReal(), vec[0], vec[1], vec[2])); \
	return true; \
    }; \
    /* end of macro */

    QUAT_SPECIALIZATION(GfQuath, GfVec4h, GfVec3h);
    QUAT_SPECIALIZATION(GfQuatf, GfVec4f, GfVec3f);
    QUAT_SPECIALIZATION(GfQuatd, GfVec4d, GfVec3d);

#undef QUAT_SPECIALIZATION

    using saveJSONFuncT = bool(UT_WorkBuffer &kbuf, UT_WorkBuffer &vbuf,
		const char *prefix, const char *key, VtValue &v);

    struct MetaDataType
    {
	MetaDataType(const char *prefix, const UT_Function<saveJSONFuncT> &func)
	    : myPrefix(prefix)
	    , myFunc(func)
	{
	}
	const char * myPrefix;
	UT_Function<saveJSONFuncT> myFunc;
    };

    static MetaDataType theMetaDataTypes[] = {
	MetaDataType(nullptr,	    saveJSONFunc<std::string>),
	MetaDataType(nullptr,	    saveJSONFunc<TfToken>),
	MetaDataType(nullptr,	    saveJSONFunc<SdfPath>),
	MetaDataType(nullptr,	    saveJSONFunc<SdfAssetPath>),
	MetaDataType("bool",	    saveJSONFunc<bool>),
	MetaDataType("int8",	    saveJSONFunc<int8>),
	MetaDataType("int32",	    saveJSONFunc<int32>),
	MetaDataType("int64",	    saveJSONFunc<int64>),
	MetaDataType("vec2i",	    saveJSONFunc<GfVec2i>),
	MetaDataType("vec3i",	    saveJSONFunc<GfVec3i>),
	MetaDataType("vec4i",	    saveJSONFunc<GfVec4i>),
	MetaDataType("uint8",	    saveJSONFunc<uint8>),
	MetaDataType("uint32",	    saveJSONFunc<uint32>),
	MetaDataType("uint64",	    saveJSONFunc<uint64>),
	MetaDataType("vec2u",	    saveJSONFunc<GfSize2>),
	MetaDataType("vec3u",	    saveJSONFunc<GfSize3>),
	MetaDataType("half",	    saveJSONFunc<fpreal16>),
	MetaDataType("half",	    saveJSONFunc<GfHalf>),
	MetaDataType("vec2h",	    saveJSONFunc<GfVec2h>),
	MetaDataType("vec3h",	    saveJSONFunc<GfVec3h>),
	MetaDataType("vec4h",	    saveJSONFunc<GfVec4h>),
	MetaDataType("vec4h",	    saveJSONFunc<GfQuath>),
	MetaDataType("float",	    saveJSONFunc<fpreal32>),
	MetaDataType("vec2f",	    saveJSONFunc<GfVec2f>),
	MetaDataType("vec3f",	    saveJSONFunc<GfVec3f>),
	MetaDataType("vec4f",	    saveJSONFunc<GfVec4f>),
	MetaDataType("vec4f",	    saveJSONFunc<GfQuatf>),
	MetaDataType("matrix3f",    saveJSONFunc<GfMatrix3f>),
	MetaDataType("matrix4f",    saveJSONFunc<GfMatrix4f>),
	MetaDataType("double",	    saveJSONFunc<fpreal64>),
	MetaDataType("double",	    saveJSONFunc<SdfTimeCode>),
	MetaDataType("vec2d",	    saveJSONFunc<GfVec2d>),
	MetaDataType("vec3d",	    saveJSONFunc<GfVec3d>),
	MetaDataType("vec4d",	    saveJSONFunc<GfVec4d>),
	MetaDataType("vec4d",	    saveJSONFunc<GfQuatd>),
	MetaDataType("matrix3d",    saveJSONFunc<GfMatrix3d>),
	MetaDataType("matrix4d",    saveJSONFunc<GfMatrix4d>),
    };

    // Extract the XUSD_RenderProduct from an HUSD_RenderProduct
    static const XUSD_RenderProduct *
    xusdProduct(const HUSD_RenderProduct *p)
    {
        return p ? p->myOwner : nullptr;
    }
}

namespace HUSD_RenderTokens
{
#define DECL_TOKEN(NAME) \
    const char *NAME() { return UsdRenderTokens->NAME.GetText(); } \
    /* end macro */
    DECL_TOKEN(productName);
    DECL_TOKEN(productType);
    DECL_TOKEN(dataType);
    DECL_TOKEN(aspectRatioConformPolicy);
    DECL_TOKEN(dataWindowNDC);
    DECL_TOKEN(disableMotionBlur);
    DECL_TOKEN(pixelAspectRatio);
    DECL_TOKEN(resolution);
    DECL_TOKEN(raster);
#undef DECL_TOKEN

#define DECL_TOKEN(NAME) \
    const char *NAME() { return HdAovTokens->NAME.GetText(); } \
    /* end macro */
    DECL_TOKEN(color);
    DECL_TOKEN(cameraDepth);
#undef DECL_TOKEN
}

bool
HUSD_RenderSettingsContext::lookupSetting::lookup(
        const char *token,
        int64 &val) const
{
    if (!myData)
        return false;
    UT_ASSERT(myData);
    return doLookup<int64, bool, int32, int64>(
            *(const UsdPrim *)myData, val, TfToken(token));
}

bool
HUSD_RenderSettingsContext::lookupSetting::lookup(
        const char *token,
        fpreal64 &val) const
{
    if (!myData)
        return false;
    return doLookup<fpreal64, bool, int32, int64, fpreal32, fpreal64>(
            *(const UsdPrim *)myData, val, TfToken(token));
}

bool
HUSD_RenderSettingsContext::lookupSetting::lookup(
        const char *token,
        UT_Vector2i &val) const
{
    if (!myData)
        return false;
    GfVec2i     tmp;
    if (!doLookup<GfVec2i, GfVec2i>(
                *(const UsdPrim *)myData, tmp, TfToken(token)))
    {
        return false;
    }
    val = UT_Vector2i(tmp[0], tmp[1]);
    return true;
}

#if 0
        void    store(const char *token, const char *val);
        void    store(const char *token, const UT_StringArray &val);
#endif

#define SIMPLE_STORE(TYPE) \
    void HUSD_RenderSettingsContext::storeProperty::store( \
            const char *name, TYPE v) { \
        TfToken token(name); \
        (*(HdRenderSettingsMap *)(myData))[token] = v; \
    } \
    /* end macro */

SIMPLE_STORE(bool)
SIMPLE_STORE(int32)
SIMPLE_STORE(int64)
SIMPLE_STORE(fpreal32)
SIMPLE_STORE(fpreal64)
SIMPLE_STORE(const std::string &)
#undef SIMPLE_STORE

void
HUSD_RenderSettingsContext::storeProperty::store(const char *n, const char *v)
{
    TfToken token(n);
    (*(HdRenderSettingsMap *)(myData))[token] = std::string(v);
}

void
HUSD_RenderSettingsContext::storeProperty::store(const char *n,
        const UT_Array<const char *> &v)
{
    TfToken token(n);
    VtArray<std::string>        vv;
    vv.assign(v.begin(), v.end());
    (*(HdRenderSettingsMap *)(myData))[token] = vv;
}

void
HUSD_RenderSettingsContext::storeProperty::storeTfToken(const char *n, const char *v)
{
    TfToken token(n);
    (*(HdRenderSettingsMap *)(myData))[token] = TfToken(v);
}

HUSD_RenderSettingsContext::HUSD_RenderSettingsContext()
    : myImpl(new husd_RenderSettingsContext(this))
{
}

HUSD_RenderSettingsContext::~HUSD_RenderSettingsContext()
{
    UT_ASSERT(myImpl && dynamic_cast<husd_RenderSettingsContext *>(myImpl));
    delete myImpl;
}

HdAovDescriptor
husd_RenderSettingsContext::defaultAovDescriptor(const TfToken &aov) const
{
    const HUSD_HuskEngine       *engine = myImpl->huskEngine();
    if (!engine)
        return HdAovDescriptor();
    return engine->impl()->defaultAovDescriptor(aov);
}

void
husd_RenderSettingsContext::initFromUSD(UsdRenderSettings &settings)
{
    UsdPrim              prim;
    const UsdPrim       *pptr = nullptr;
    if (settings)
    {
        prim = settings.GetPrim();
        pptr = &prim;
    }
    HUSD_RenderSettingsContext::lookupSetting       lookup(pptr);
    myImpl->initFromSettings(lookup);
}

void
husd_RenderSettingsContext::setDefaultSettings(const XUSD_RenderSettings &xs,
        HdRenderSettingsMap &settings) const
{
    const auto    *hs = dynamic_cast<const husd_RenderSettings *>(&xs);
    if (hs)
    {
        HUSD_RenderSettingsContext::storeProperty       writer(&settings);
        myImpl->setDefaultSettings(hs->impl(), writer);
    }
}

void
husd_RenderSettingsContext::overrideSettings(const XUSD_RenderSettings &xs,
        HdRenderSettingsMap &settings) const
{
    const auto    *hs = dynamic_cast<const husd_RenderSettings *>(&xs);
    if (hs)
    {
        HUSD_RenderSettingsContext::storeProperty       writer(&settings);
        myImpl->overrideSettings(hs->impl(), writer);
    }
}

TfToken
husd_RenderSettingsContext::renderer() const
{
    return TfToken(myImpl->renderer().c_str());
}

SdfPath
husd_RenderSettingsContext::overrideCamera() const
{
    return SdfPath(myImpl->overrideCamera().toStdString());
}

GfVec2i
husd_RenderSettingsContext::defaultResolution() const
{
    UT_Vector2i v = myImpl->defaultResolution();
    return GfVec2i(v.x(), v.y());
}

GfVec2i
husd_RenderSettingsContext::overrideResolution(const GfVec2i &res) const
{
    UT_Vector2i v(res[0], res[1]);
    v = myImpl->overrideResolution(v);
    return GfVec2i(v.x(), v.y());
}

fpreal
husd_RenderSettingsContext::overridePixelAspect(fpreal pa) const
{
    return myImpl->overridePixelAspect(pa);
}

GfVec4f
husd_RenderSettingsContext::overrideDataWindow(const GfVec4f &w) const
{
    UT_Vector4  v(w[0], w[1], w[2], w[3]);
    v = myImpl->overrideDataWindow(v);
    return GfVec4f(v[0], v[1], v[2], v[3]);
}

bool
husd_RenderSettingsContext::overrideDisableMotionBlur(bool v) const
{
    return myImpl->overrideDisableMotionBlur(v);
}

const char *
husd_RenderSettingsContext::defaultPurpose() const
{
    return myImpl->defaultPurpose();
}

const char *
husd_RenderSettingsContext::overridePurpose() const
{
    return myImpl->overridePurpose();
}

fpreal
husd_RenderSettingsContext::startFrame() const
{
    return myImpl->startFrame();
}

fpreal
husd_RenderSettingsContext::frameInc() const
{
    return myImpl->frameInc();
}

int
husd_RenderSettingsContext::frameCount() const
{
    return myImpl->frameCount();
}

fpreal
husd_RenderSettingsContext::fps() const
{
    return myImpl->fps();
}

UsdTimeCode
husd_RenderSettingsContext::evalTime() const
{
    return UsdTimeCode(myImpl->evalTime());
}

const char *
husd_RenderSettingsContext::defaultProductName() const
{
    return myImpl->defaultProductName();
}

const char *
husd_RenderSettingsContext::overrideProductName(
        const XUSD_RenderProduct &xp, int pidx) const
{
    const auto    *hp = dynamic_cast<const husd_RenderProduct *>(&xp);
    return hp ? myImpl->overrideProductName(*hp->myImpl, pidx) : nullptr;
}

const char *
husd_RenderSettingsContext::overrideSnapshotPath(
        const XUSD_RenderProduct &xp, int pidx) const
{
    const auto    *hp = dynamic_cast<const husd_RenderProduct *>(&xp);
    return hp ? myImpl->overrideSnapshotPath(*hp->myImpl, pidx) : nullptr;
}

const char *
husd_RenderSettingsContext::overrideSnapshotSuffix(
        const XUSD_RenderProduct &xp, int pidx) const
{
    const auto    *hp = dynamic_cast<const husd_RenderProduct *>(&xp);
    return hp ? myImpl->overrideSnapshotSuffix(*hp->myImpl, pidx) : nullptr;
}

const char *
husd_RenderSettingsContext::tileSuffix() const
{
    return myImpl->tileSuffix();
}

int
husd_RenderSettingsContext::tileIndex() const
{
    return myImpl->tileIndex();
}

bool
husd_RenderSettingsContext::allowCameraless() const
{
    return myImpl->allowCameraless();
}


HUSD_RenderVar::HUSD_RenderVar()
    : myOwner(new husd_RenderVar(this))
{
}

HUSD_RenderVar::~HUSD_RenderVar()
{
    auto *owner = UTverify_cast<husd_RenderVar *>(myOwner);
    if (!owner->bound())        // Never passed to XUSD
        delete myOwner;
}

void
HUSD_RenderVar::copyDataFrom(const HUSD_RenderVar &src)
{
    const auto *sxv = UTverify_cast<const husd_RenderVar *>(src.myOwner);
    auto *dxv = UTverify_cast<husd_RenderVar *>(myOwner);
    dxv->copyDataFrom(*sxv);
}

UT_StringHolder
HUSD_RenderVar::aovName() const
{
    return UT_StringHolder(myOwner->aovName());
}

UT_StringHolder
HUSD_RenderVar::aovToken() const
{
    return UT_StringHolder(myOwner->aovToken().GetText());
}

UT_StringHolder
HUSD_RenderVar::dataType() const
{
    return UT_StringHolder(myOwner->dataType().GetText());
}

UT_StringHolder
HUSD_RenderVar::sourceName() const
{
    return UT_StringHolder(myOwner->sourceName());
}

UT_StringHolder
HUSD_RenderVar::sourceType() const
{
    return UT_StringHolder(myOwner->sourceType().GetText());
}

PXL_DataFormat
HUSD_RenderVar::pxlFormat() const
{
    return myOwner->pxlFormat();
}

PXL_Packing
HUSD_RenderVar::pxlPacking() const
{
    return myOwner->pxlPacking();
}

bool
HUSD_RenderVar::lookup(const char *token, int64 &val) const
{
    return mapLookup(myOwner->desc().aovSettings, token, val);
}

bool
HUSD_RenderVar::lookup(const char *token, fpreal64 &val) const
{
    return mapLookup(myOwner->desc().aovSettings, token, val);
}

bool
HUSD_RenderVar::lookup(const char *token, UT_Vector2i &val) const
{
    return mapLookup(myOwner->desc().aovSettings, token, val);
}

bool
HUSD_RenderVar::lookup(const char *token, UT_StringHolder &val) const
{
    return mapLookup(myOwner->desc().aovSettings, token, val);
}

void
HUSD_RenderVar::dump() const
{
    UT_AutoJSONWriter   w(std::cerr, false);
    dump(*w);
}

void
HUSD_RenderVar::dump(UT_JSONWriter &w) const
{
    myOwner->dump(w);
}

HUSD_RenderProduct::HUSD_RenderProduct()
    : myOwner(new husd_RenderProduct(this))
{
}

HUSD_RenderProduct::~HUSD_RenderProduct()
{
    auto *owner = UTverify_cast<husd_RenderProduct *>(myOwner);
    if (!owner->bound())        // Never passed to XUSD
        delete myOwner;
}

bool
HUSD_RenderProduct::lookup(const char *token, int64 &val) const
{
    return mapLookup(myOwner->settings(), token, val);
}

bool
HUSD_RenderProduct::lookup(const char *token, fpreal64 &val) const
{
    return mapLookup(myOwner->settings(), token, val);
}

bool
HUSD_RenderProduct::lookup(const char *token, UT_Vector2i &val) const
{
    return mapLookup(myOwner->settings(), token, val);
}

bool
HUSD_RenderProduct::lookup(const char *token, UT_StringHolder &val) const
{
    return mapLookup(myOwner->settings(), token, val);
}

HUSD_RenderSettingsContext::storeProperty
HUSD_RenderProduct::writer()
{
    auto *owner = UTverify_cast<husd_RenderProduct *>(myOwner);
    return HUSD_RenderSettingsContext::storeProperty(owner->renderSettingsPtr());
}

void
HUSD_RenderProduct::copySetting(const HUSD_RenderSettings &settings,
        const char *token)
{
    const auto &usd = settings.myOwner->renderSettings();
    TfToken     name(token);
    auto it = usd.find(name);
    if (it != usd.end())
    {
        UTverify_cast<husd_RenderProduct *>(myOwner)->storeSetting(name, it->second);
    }
}

UT_UniquePtr<HUSD_RenderVar>
HUSD_RenderProduct::newRenderVar() const
{
    return UTmakeUnique<HUSD_RenderVar>();
}

void
HUSD_RenderProduct::addRenderVars(const UT_Array<const HUSD_RenderVar *> &vars)
{
    UT_ASSERT(myOwner->vars().size() == 0);
    auto *owner = UTverify_cast<husd_RenderProduct *>(myOwner);
    owner->bumpCapacity(vars.size());
    for (const auto &v : vars)
        owner->addVar(v);
}

exint
HUSD_RenderProduct::size() const
{
    return myOwner->vars().size();
}

const HUSD_RenderVar *
HUSD_RenderProduct::renderVar(exint i) const
{
    const auto *var = UTverify_cast<const husd_RenderVar *>(myOwner->vars()[i].get());
    return var->myImpl;
}

UT_StringHolder
HUSD_RenderProduct::productType() const
{
    return UT_StringHolder(myOwner->productType().GetText());
}

UT_StringHolder
HUSD_RenderProduct::productName(int frame) const
{
    return UT_StringHolder(myOwner->productName(frame).GetText());
}

UT_StringHolder
HUSD_RenderProduct::outputName() const
{
    return myOwner->outputName();
}

bool
HUSD_RenderProduct::isRaster() const
{
    return myOwner->isRaster();
}

const UT_StringHolder &
HUSD_RenderProduct::filename() const
{
    return UTverify_cast<const husd_RenderProduct *>(myOwner)->filename();
}

const UT_StringHolder &
HUSD_RenderProduct::partname() const
{
    return UTverify_cast<const husd_RenderProduct *>(myOwner)->partname();
}

void
HUSD_RenderProduct::addMetaData(IMG_FileParms &fparms) const
{
    const HdAovSettingsMap      &settings = myOwner->settings();
    for (auto it : settings)
    {
        static constexpr UT_StringLit   theLeader("driver:parameters:");
        static constexpr UT_StringLit   theHuskLeader("driver:parameters:husk:");
        const UT_StringRef	        name(it.first.GetString());

	UT_WorkBuffer	 key, val;
	const char	*key_name = nullptr;
        if (name.startsWith(theHuskLeader.asRef()))
            key_name = name.c_str() + theHuskLeader.length();
        else if (name.startsWith(theLeader.asRef()))
            key_name = name.c_str() + theLeader.length();
        if (key_name)
        {
            bool         is_valid = false;
            for (auto md : theMetaDataTypes)
            {
                if (md.myFunc(key, val, md.myPrefix, key_name, it.second))
                {
                    is_valid = true;
                    break;
                }
            }
            if (is_valid)
                fparms.setOption(key.buffer(), val.buffer());
            else
            {
#if 0
                UTdebugFormat("{} unsupported type {} - storing string {}",
                        key_name, it.second.GetTypeName(), it.second);
#endif
                val.format("{}", it.second);
                if (val.length())
                    fparms.setOption(key_name, val.buffer());
            }
        }
    }
}

void
HUSD_RenderProduct::dump() const
{
    UT_AutoJSONWriter   w(std::cerr, false);
    dump(*w);
}

void
HUSD_RenderProduct::dump(UT_JSONWriter &w) const
{
    myOwner->dump(w);
}

#if 0
bool
HUSD_RenderProduct::expandProduct(
        const HUSD_RenderSettingsContext &ctx,
        int product_index,
        int frame)
{
    return myOwner->expandProduct(ctx.impl(), product_index, frame);
}
#endif

HUSD_RenderSettings::HUSD_RenderSettings(const UT_StringHolder &prim_path,
        const UT_StringHolder &filename,
        time_t file_timestamp)
    : myOwner(new husd_RenderSettings(this, prim_path, filename, file_timestamp))
{
}

HUSD_RenderSettings::~HUSD_RenderSettings()
{
    UT_ASSERT(myOwner && dynamic_cast<husd_RenderSettings *>(myOwner));
    delete myOwner;
}

bool
HUSD_RenderSettings::supportedDelegate(const UT_StringRef &name) const
{
    return true;
}

bool
HUSD_RenderSettings::init(const HUSD_HuskEngine &engine,
        const UT_StringHolder &settings_path,
        HUSD_RenderSettingsContext &ctx)
{
    return myOwner->init(engine.impl()->stage(),
            SdfPath(settings_path.c_str()),
            ctx.impl());
}

bool
HUSD_RenderSettings::makeFilePathDirs(const char *path)
{
    UT_String   dir, file;
    if (!UTisstring(path))
        return true;

    UT_String   path_str(path);
    path_str.splitPath(dir, file);
    if (!dir.isstring())
        return true;

    UT_FileStat sbuf;
    if (UTfileStat(dir, &sbuf) == 0)
    {
        if (sbuf.myFileType == UT_FileStat::DIRECTORY)
            return true;                // Directory exists
        UT_ErrorLog::warningOnce(
                "Invalid output path: {} is not a directory", dir);
        return false;
    }
    FS_Info     dir_info(dir);
    UT_ErrorLog::format(3, "Creating output directory: {}", dir);
    if (!FSmakeDirs(dir_info))
    {
        UT_ErrorLog::warningOnce(
                "Unable to create output directory path: {}", dir);
        return false;
    }
    return true;
}

bool
HUSD_RenderSettings::updateFrame(HUSD_RenderSettingsContext &ctx,
        int frame,
        int product_group,
        bool mkdirs,
        bool delegate_products,
        bool create_dummy_render_product)
{
    HUSD_HuskEngine     *engine = SYSconst_cast(ctx.huskEngine());
    UT_ASSERT(engine);
    if (!myOwner->updateFrame(engine->impl()->stage(),
                ctx.impl(), create_dummy_render_product))
    {
        return false;
    }

    engine->setDataWindow(dataWindow(product_group));
    engine->updateSettings(*this);
    expandProducts(ctx, frame, product_group);
    if (delegate_products)
        engine->delegateRenderProducts(*this, product_group);

    if (mkdirs)
    {
        UT_SmallArray<HUSD_RenderProduct *>     prods;
        productGroup(product_group, prods);
        for (const auto *prod : prods)
        {
            // Only make paths for raster products
            if (prod->isRaster()
                || prod->productType() == theKarmaCheckpoint.asRef()
                || prod->productType() == theKarmaDeep.asRef())
            {
                if (!makeFilePathDirs(prod->outputName()))
                    return false;
            }
        }
    }

    return true;
}

bool
HUSD_RenderSettings::expandProducts(const HUSD_RenderSettingsContext &ctx,
        int fnum, int product_group)
{
    return myOwner->expandProducts(ctx.impl(), fnum, product_group);
}

const char *
HUSD_RenderSettings::huskNullRasterName()
{
    return HusdHuskTokens->huskNullRaster.GetText();
}

bool
HUSD_RenderSettings::resolveProducts(const HUSD_HuskEngine &engine,
        HUSD_RenderSettingsContext &ctx,
        bool create_dummy)
{
    return myOwner->resolveProducts(engine.impl()->stage(),
            ctx.impl(),
            create_dummy);
}

UT_UniquePtr<HUSD_RenderProduct>
HUSD_RenderSettings::newRenderProduct() const
{
    return UTmakeUnique<HUSD_RenderProduct>();
}

bool
HUSD_RenderSettings::lookup(const char *token, int64 &val) const
{
    return mapLookup(myOwner->renderSettings(), token, val);
}

bool
HUSD_RenderSettings::lookup(const char *token, fpreal64 &val) const
{
    return mapLookup(myOwner->renderSettings(), token, val);
}

bool
HUSD_RenderSettings::lookup(const char *token, UT_Vector2i &val) const
{
    return mapLookup(myOwner->renderSettings(), token, val);
}

bool
HUSD_RenderSettings::lookup(const char *token, UT_StringHolder &val) const
{
    return mapLookup(myOwner->renderSettings(), token, val);
}

HUSD_RenderSettingsContext::storeProperty
HUSD_RenderSettings::writer()
{
    auto *owner = UTverify_cast<husd_RenderSettings *>(myOwner);
    return HUSD_RenderSettingsContext::storeProperty(owner->renderSettingsPtr());
}

UT_StringHolder
HUSD_RenderSettings::renderer() const
{
    return UTmakeUnsafeRef(myOwner->renderer().GetText());
}

UT_StringHolder
HUSD_RenderSettings::cameraPath(const HUSD_RenderProduct *p) const
{
    SdfPath     cpath = myOwner->cameraPath(xusdProduct(p));
    if (cpath.IsEmpty())
        return UT_StringHolder::theEmptyString;
    return HUSD_Path(cpath).pathStr();
}

double
HUSD_RenderSettings::shutterOpen(const HUSD_RenderProduct *p) const
{
    return myOwner->shutterOpen(xusdProduct(p));
}

double
HUSD_RenderSettings::shutterClose(const HUSD_RenderProduct *p) const
{
    return myOwner->shutterClose(xusdProduct(p));
}

int
HUSD_RenderSettings::xres(const HUSD_RenderProduct *p) const
{
    return myOwner->xres(xusdProduct(p));
}

int
HUSD_RenderSettings::yres(const HUSD_RenderProduct *p) const
{
    return myOwner->yres(xusdProduct(p));
}

UT_Vector2i
HUSD_RenderSettings::res(const HUSD_RenderProduct *p) const
{
    const GfVec2i       &v = myOwner->res(xusdProduct(p));
    return UT_Vector2i(v[0], v[1]);
}

fpreal
HUSD_RenderSettings::pixelAspect(const HUSD_RenderProduct *p) const
{
    return myOwner->pixelAspect(xusdProduct(p));
}

UT_Vector4
HUSD_RenderSettings::dataWindowF(const HUSD_RenderProduct *p) const
{
    const GfVec4f       &v = myOwner->dataWindowF(xusdProduct(p));
    return UT_Vector4(v[0], v[1], v[2], v[3]);
}

UT_DimRect
HUSD_RenderSettings::dataWindow(const HUSD_RenderProduct *p) const
{
    return myOwner->dataWindow(xusdProduct(p));
}

void
HUSD_RenderSettings::purpose(UT_StringArray &purposes) const
{
    const VtArray<TfToken>      &v = myOwner->purpose();
    purposes.setSize(0);
    purposes.bumpCapacity(v.size());
    for (exint i = 0, n = v.size(); i < n; ++i)
        purposes.append(UT_StringHolder(v[i].GetText()));
}

bool
HUSD_RenderSettings::disableMotionBlur(const HUSD_RenderProduct *p) const
{
    return myOwner->disableMotionBlur(xusdProduct(p));
}

UT_StringHolder
HUSD_RenderSettings::outputName(int product_group) const
{
    return myOwner->outputName(product_group);
}

const HUSD_RenderProduct *
HUSD_RenderSettings::productInGroup(int product_group) const
{
    const auto &pgroups = myOwner->productGroups();
    if (product_group < 0 || product_group >= pgroups.size())
    {
        UT_ASSERT(0 && "Product group out of range");
        return nullptr;
    }
    int idx = pgroups[product_group][0];
    return UTverify_cast<const husd_RenderProduct *>(myOwner->products()[idx].get())->myImpl;
}

exint
HUSD_RenderSettings::productGroupSize() const
{
    return myOwner->productGroups().size();
}

exint
HUSD_RenderSettings::productsInGroup(exint group) const
{
    return myOwner->productGroups()[group].size();
}

const HUSD_RenderProduct *
HUSD_RenderSettings::product(exint gidx, exint pidx) const
{
    const auto  &group = myOwner->productGroups()[gidx];
    const auto  &products = myOwner->products();
    pidx = group[pidx];
    const auto  *p = UTverify_cast<const husd_RenderProduct *>(products[pidx].get());
    return p->myImpl;
}

exint
HUSD_RenderSettings::totalProductCount() const
{
    return myOwner->products().size();
}

void
HUSD_RenderSettings::allProducts(ProductGroup &group) const
{
    group.clear();
    for (auto &&prod : myOwner->products())
    {
        const auto *p = UTverify_cast<const husd_RenderProduct *>(prod.get());
        group.append(p->myImpl);
    }
}

void
HUSD_RenderSettings::productGroup(int i, ProductGroup &group) const
{
    const auto &products = myOwner->products();
    group.clear();
    for (int idx : myOwner->productGroups()[i])
    {
        const auto *p = UTverify_cast<const husd_RenderProduct *>(products[idx].get());
        group.append(p->myImpl);
    }
}

bool
HUSD_RenderSettings::addProduct(UT_UniquePtr<HUSD_RenderProduct> hp, int pgroup)
{
    HUSD_RenderProduct  *tmp = hp.release();
    auto xp = UTverify_cast<husd_RenderProduct *>(tmp->myOwner);
    UTverify_cast<husd_RenderSettings *>(myOwner)->addProduct(xp->bind(), pgroup);
    return true;
}

void
HUSD_RenderSettings::removeProduct(exint i)
{
    UTverify_cast<husd_RenderSettings *>(myOwner)->removeProduct(i);
}

void
HUSD_RenderSettings::printSettings() const
{
    myOwner->printSettings();
}

void
HUSD_RenderSettings::dump() const
{
    UT_AutoJSONWriter   w(std::cerr, false);
    dump(*w);
}

void
HUSD_RenderSettings::dump(UT_JSONWriter &w) const
{
    myOwner->dump(w);
}
