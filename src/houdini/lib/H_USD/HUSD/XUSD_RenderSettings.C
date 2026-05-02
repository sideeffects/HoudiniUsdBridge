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
 * NAME:	XUSD_RenderSettings.h (karma Library, C++)
 *
 * COMMENTS:
 */

#include "XUSD_RenderSettings.h"
#include "HUSD_FileExpanded.h"
#include "XUSD_Tokens.h"
#include "XUSD_Format.h"
#include <PXL/PXL_Common.h>
#include <UT/UT_Assert.h>
#include <UT/UT_Debug.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_Options.h>
#include <UT/UT_Rect.h>
#include <UT/UT_Set.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_PerfMonAutoEvent.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_VarScan.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_WorkBuffer.h>
#include <PY/PY_Python.h>
#include <PY/PY_EvaluationContext.h>
#include <PY/PY_InterpreterAutoLock.h>
#include <PY/PY_CPythonAPI.h>
#include <SYS/SYS_Floor.h>
#include <SYS/SYS_Hash.h>
#include <SYS/SYS_Math.h>
#include <tools/henv.h>
#include <pxr/base/tf/pyPtrHelpers.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/tokens.h>
#include <pxr/usd/usdRender/var.h>

#include <functional>
#include <iostream>
#include <tuple>
#include <utility>
#include <string.h>

#if defined(WIN32)
// Define hashing functions to store an HdAovSettingsMap in a VtValue.
namespace std
{
    template <> struct hash<PXR_NS::HdAovSettingsMap>
    {
        std::size_t operator()(const PXR_NS::HdAovSettingsMap &m) const
        {
            std::size_t h = 0;
            for (auto &&i : m)
            {
                SYShashCombine(h, i.first);
                SYShashCombine(h, i.second);
            }
            return h;
        }
    };
}

PXR_NAMESPACE_OPEN_SCOPE
std::size_t
hash_value(const HdAovSettingsMap &m)
{
    return std::hash<HdAovSettingsMap>{}(m);
}
PXR_NAMESPACE_CLOSE_SCOPE
#endif

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static constexpr UT_StringLit	theDefaultImage("karma.exr");
    static constexpr UT_StringLit       theIPName("ip");
    static constexpr UT_StringLit       theMDName("md");
    static const std::string		theHuskDefault("husk_default");
    static const std::string		theHuskDummyRaster("husk_dummy_raster");
    static const std::string            theLPECf("C.*[LO]");

    using ProductList = XUSD_RenderSettings::ProductList;

    static bool
    importShutter(const UsdStageRefPtr &usd,
            const SdfPath &path,
            const UsdTimeCode &time,
            GfVec2d &shutter)
    {
        UsdPrim             prim = usd->GetPrimAtPath(path);
        UsdGeomCamera       cam(prim);
        if (!cam)
            return false;
        cam.GetShutterOpenAttr().Get(&shutter[0], time);
        cam.GetShutterCloseAttr().Get(&shutter[1], time);
        return true;
    }

    template <typename T, typename V>
    static bool
    tryImportValue(const VtValue &vv, T &val, const TfToken &name)
    {
        if (!(vv.IsHolding<V>()))
            return false;
        val = vv.UncheckedGet<V>();
        return true;
    }

    template <typename T, typename V, typename NEXT, typename... Types>
    static bool
    tryImportValue(const VtValue &vv, T &val, const TfToken &name)
    {
        if (tryImportValue<T, V>(vv, val, name))
            return true;
        return tryImportValue<T, NEXT, Types...>(vv, val, name);
    }

    template <typename T, typename V, typename... Types>
    static void
    tryImport(const HdAovSettingsMap &map,
            XUSD_RenderProduct::SettingOverride<T> &val,
            const TfToken &name)
    {
        auto it = map.find(name);
        if (it != map.end())
        {
            val.myAuthored = tryImportValue<T, V, Types...>(
                                        it->second, val.myValue, name);
            if (!val.myAuthored)
            {
                UTdebugFormat("Expected {} to be holding {} not {}",
                    name, typeid(val).name(),
                    it->second.GetType().GetTypeName());
            }
        }
    }

    static bool
    isFramebuffer(const UT_StringHolder &pname)
    {
	return pname == theIPName.asRef() || pname == theMDName.asRef();
    }

    static bool
    isHuskNullRaster(const UT_StringRef &pname)
    {
        return pname == HusdHuskTokens->huskNullRaster.GetText();
    }

    static UT_StringHolder
    makePartName(const UT_StringHolder &filename, const char *path,
                    const char *part = "_part")
    {
        if (isHuskNullRaster(filename))
            return filename;
#if 1
	const char	*ext = strrchr(filename, '.');
	const char	*file = strrchr(filename, '/');
	if (file && UTisstring(path))
	    file = file + 1; // step past the '/'
	else
	    file = filename.c_str();

	UT_WorkBuffer	 result;
	if (UTisstring(path))
	{
	    UT_String base(path);
	    base.normalizePath();
	    if (!base.endsWith("/"))
		base.append('/');
	    result.strcpy(base);
	}

	if (!ext)
	{
	    result.strcat(file);
	    result.append(part);
	}
	else
	{
	    result.strncat(file, ext - file);
	    result.append(part);
	    result.append(ext);
	}

	return UT_StringHolder(result);
#else
	static constexpr UT_StringLit	thePartExt(".part");
	UT_StringHolder	part = filename;
	part += thePartExt.asRef();
	return part;
#endif
    }

    static SdfPath
    resolveCamera(UsdRelationship cams, SdfPath id)
    {
        SdfPathVector   paths;
        if (cams)
            cams.GetTargets(&paths);
        if (!paths.size())
            return SdfPath();
        if (paths.size() > 1)
        {
            UT_ErrorLog::warning(
			"Multiple cameras in settings {}, choosing {}",
                        id, paths[0]);
        }
        return paths[0];
    }

    static bool
    isAuthored(const UsdAttribute &atr)
    {
        if (!atr.HasAuthoredValue())
            return false;
#if 1
        return true;
#else
        // For render products, we only want authored values to override the
        // render settings.  However, since the Product LOP authors all
        // attributes, we specifically check to see whether the attribute is in
        // a well known list of attributes we want to ignore.
        //
        // TODO: In the future, we want to let products override resolution,
        // disableMotionBlur, etc.
        //
        // The tokens commented out allow the product to override the render
        // settings
        static UT_Set<TfToken>  theProductSkipList({
                            UsdRenderTokens->aspectRatioConformPolicy,
                            UsdRenderTokens->dataWindowNDC,
                            UsdRenderTokens->disableMotionBlur,
                            UsdRenderTokens->disableDepthOfField,
                            UsdRenderTokens->pixelAspectRatio,
                            UsdRenderTokens->resolution,
                            //UsdRenderTokens->camera,
                            //UsdRenderTokens->orderedVars,
                            //UsdRenderTokens->productName,
                            //UsdRenderTokens->productType,
                        });
        return !theProductSkipList.contains(atr.GetName());
#endif
    }


    const UsdAttribute
    getAuthoredAttribute(const UsdPrim &prim,
                 const TfToken &name,
                 const TfToken &fallback = TfToken())
    {
        UsdAttribute atr = prim.GetAttribute(name);
        if (atr && isAuthored(atr))
            return atr;
        if (!fallback.IsEmpty())
        {
            atr = prim.GetAttribute(fallback);
            if (atr && isAuthored(atr))
                return atr;
        }
        return UsdAttribute();
    }

    template <typename T>
    static bool
    loadAttribute(const UsdPrim &prim,
            const UsdTimeCode &time,
            const TfToken &name,
            const TfToken &fallback,
            T &val)
    {
	const UsdAttribute attr = getAuthoredAttribute(prim, name, fallback);
	if (!attr)
	    return false;
	return attr.Get(&val, time);
    }

    template <typename T, typename V>
    static bool
    importOption(T &dest, const UsdAttribute &attr, const UsdTimeCode &time)
    {
	V	value;
	if (attr.Get(&value, time))
	{
	    dest = value;
	    return true;
	}
	return false;
    }

    template <typename T, typename V, typename NEXT, typename... Types>
    static bool
    importOption(T &dest, const UsdAttribute &attr, const UsdTimeCode &time)
    {
	if (importOption<T, V>(dest, attr, time))
	    return true;
	return importOption<T, NEXT, Types...>(dest, attr, time);
    }

    template <typename T, typename V, typename... Types>
    static bool
    importProperty(const UsdPrim &prim, const UsdTimeCode &time,
            T &val, const TfToken &name, const TfToken &fallback = TfToken())
    {
	UsdAttribute	attr = prim.GetAttribute(name);
        if (!attr && !fallback.IsEmpty())
            attr = prim.GetAttribute(fallback);
	if (!attr)
	    return false;
	return importOption<T, V, Types...>(val, attr, time);
    }

    static VtArray<TfToken>
    parsePurpose(const char *raw_purpose)
    {
	VtArray<TfToken>	list;
	UT_String		purpose(raw_purpose);
	UT_WorkArgs		args;
	purpose.tokenize(args, ',');
	for (auto &&arg : args)
	{
	    UT_String	a(arg);
	    a.trimSpace();
	    list.push_back(TfToken(a.c_str()));
	}
	if (!list.size())
	{
	    list.push_back(HdTokens->geometry);
	    list.push_back(UsdGeomTokens->render);
	}
	return list;
    }

    static void
    listCameras(UT_Array<SdfPath> &cams)
    {
	if (!cams.size())
	    UT_ErrorLog::error("There must be a camera in the USD file");
	else
	{
	    UT_ErrorLog::error("Found {} cameras in the USD file.  {}\n\t{}",
		    cams.size(),
		    "Please select the camera for rendering",
                    "using render settings or a command line option.");
	    cams.sort();
	    for (auto &&c : cams)
		UT_ErrorLog::format(0, "  - {}", c);
	}
    }

    template <typename T, typename V>
    static bool
    getValue(const VtValue &val, T &result)
    {
        if (!val.IsHolding<V>())
            return false;
        result = val.UncheckedGet<V>();
        return true;
    }

    template <typename T, typename V, typename W, typename... Types>
    static bool
    getValue(const VtValue &val, T &result)
    {
        if (getValue<T, V>(val, result))
            return true;
        return getValue<T, W, Types...>(val, result);
    }

    template <typename MapType>
    static void
    buildSettings(MapType &map,
            const UsdPrim &prim,
            const UsdTimeCode &time,
            bool include_default_values)
    {
        map.clear();
	for (auto &&prop : prim.GetProperties())
	{
            if (UsdAttribute attrib = prop.As<UsdAttribute>())
            {
                VtValue val;
                if ((include_default_values || isAuthored(attrib)) &&
                    attrib.Get(&val, time))
                {
                    map[attrib.GetName()] = val;
                }
            }
            else if (UsdRelationship rel = prop.As<UsdRelationship>())
            {
                if (include_default_values || rel.HasAuthoredTargets())
                {
                    SdfPathVector       targets;
                    rel.GetTargets(&targets);
                    map[rel.GetName()] = VtValue(targets);
                }
            }
	}
    }

    template <typename T, typename FUNC> inline static void
    overrideSetting(HdAovSettingsMap &settings, const TfToken &token,
	    FUNC func)
    {
	auto it = settings.find(token);
	if (it != settings.end())
	{
	    UT_ASSERT(it->second.IsHolding<T>());
	    const T &val = it->second.UncheckedGet<T>();
	    settings[token] = func(val);
	}
    }

    static void
    overrideSettings(HdAovSettingsMap &settings,
	    const XUSD_RenderSettingsContext &ctx)
    {
	overrideSetting<GfVec2i>(settings, UsdRenderTokens->resolution,
		[&](const GfVec2i &v) { return ctx.overrideResolution(v); });
	overrideSetting<float>(settings, UsdRenderTokens->pixelAspectRatio,
		[&](const float &v) { return ctx.overridePixelAspect(v); });
	overrideSetting<GfVec4f>(settings, UsdRenderTokens->dataWindowNDC,
		[&](const GfVec4f &v) { return ctx.overrideDataWindow(v); });
    }

    UT_StringHolder
    expandFile(const XUSD_RenderSettingsContext &ctx,
            const char *over,
            int i,
	    const TfToken &pname,
            bool &changed)
    {
	const char	*ofile = UTisstring(over) ? over : pname.GetText();

	changed = false;

	if (!UTisstring(ofile))
        {
            // User has explicitly set the product name to an empty file
            return UT_StringHolder::theEmptyString;
        }

	UT_StringHolder expanded = HUSD_FileExpanded::expand(ofile,
					ctx.startFrame(),
					ctx.frameInc(),
                                        ctx.frameList(),
					i,
					changed);
	return expanded;
    }

    template <typename T>
    static void
    dumpScalar(UT_JSONWriter &w, const T &val)
    {
	w.jsonValue(val);
    }
    template <> void
    dumpScalar<TfToken>(UT_JSONWriter &w, const TfToken &v)
    {
	w.jsonValue(v.GetText());
    }
    template <> void
    dumpScalar<SdfPath>(UT_JSONWriter &w, const SdfPath &v)
    {
	w.jsonValue(v.GetString());
    }
    template <> void
    dumpScalar<SdfAssetPath>(UT_JSONWriter &w, const SdfAssetPath &v)
    {
	const std::string &res = v.GetResolvedPath();
	if (res.length())
	    w.jsonValue(res);
	else
	    w.jsonValue(v.GetAssetPath());
    }

    template <typename T>
    static void
    dumpVector(UT_JSONWriter &w, const T *vec, int size)
    {
	w.jsonUniformArray(size, vec);
    }
    template <> void
    dumpVector<TfToken>(UT_JSONWriter &w, const TfToken *vec, int size)
    {
	w.jsonBeginArray();
	for (int i = 0; i < size; ++i)
	    dumpScalar(w, vec[i]);
	w.jsonEndArray();
    }
    template <> void
    dumpVector<std::string>(UT_JSONWriter &w, const std::string *vec, int size)
    {
	w.jsonBeginArray();
	for (int i = 0; i < size; ++i)
	    dumpScalar(w, vec[i]);
	w.jsonEndArray();
    }
    template <> void
    dumpVector<SdfPath>(UT_JSONWriter &w, const SdfPath *vec, int size)
    {
	w.jsonBeginArray();
	for (int i = 0; i < size; ++i)
	    dumpScalar(w, vec[i]);
	w.jsonEndArray();
    }
    template <> void
    dumpVector<GfHalf>(UT_JSONWriter &w, const GfHalf *vec, int size)
    {
	w.jsonUniformArray(size, (const fpreal16 *)vec);
    }

    static void
    dumpValue(UT_JSONWriter &w, const VtValue &val)
    {
#define SCALAR(TYPE) \
	else if (val.IsHolding<TYPE>()) \
	    dumpScalar<TYPE>(w, val.UncheckedGet<TYPE>()); \
	/* end macro */
#define ARRAY(TYPE) \
	else if (val.IsHolding<TYPE>()) \
	{ \
	    const TYPE &vec = val.UncheckedGet<TYPE>(); \
	    dumpVector(w, vec.data(), vec.size()); \
	} \
	/* end macro */
#define VECTOR(TYPE) \
	else if (val.IsHolding<TYPE>()) \
	{ \
	    const TYPE &vec = val.UncheckedGet<TYPE>(); \
	    dumpVector(w, vec.data(), TYPE::dimension); \
	} \
	/* end macro */
#define MATRIX(TYPE) \
	else if (val.IsHolding<TYPE>()) \
	{ \
	    const TYPE &vec = val.UncheckedGet<TYPE>(); \
	    dumpVector(w, vec.data(), TYPE::numRows*TYPE::numColumns); \
	} \
	/* end macro */

	if (0) { }	// Start off big cascading else statements
	ARRAY(TfTokenVector)
	ARRAY(VtArray<TfToken>)
	ARRAY(VtArray<std::string>)
	ARRAY(VtArray<SdfPath>)
	ARRAY(SdfPathVector)
	SCALAR(bool)
	SCALAR(int8)
	SCALAR(int16)
	SCALAR(int32)
	SCALAR(int64)
#if 0
	SCALAR(uint8)
	SCALAR(uint16)
	SCALAR(uint32)
	SCALAR(uint64)
#endif
	SCALAR(fpreal16)
	SCALAR(fpreal32)
	SCALAR(fpreal64)
	SCALAR(SdfAssetPath)
	SCALAR(TfToken)
	SCALAR(std::string)
	SCALAR(UT_StringHolder)
	SCALAR(SdfPath)
	VECTOR(GfVec2i)
	VECTOR(GfVec3i)
	VECTOR(GfVec4i)
	VECTOR(GfVec2h)
	VECTOR(GfVec3h)
	VECTOR(GfVec4h)
	VECTOR(GfVec2f)
	VECTOR(GfVec3f)
	VECTOR(GfVec4f)
	VECTOR(GfVec2d)
	VECTOR(GfVec3d)
	VECTOR(GfVec4d)
	MATRIX(GfMatrix2f)
	MATRIX(GfMatrix2d)
	MATRIX(GfMatrix3f)
	MATRIX(GfMatrix3d)
	MATRIX(GfMatrix4f)
	MATRIX(GfMatrix4d)
	else
	{
	    w.jsonNull();
	}
#undef SCALAR
#undef VECTOR
#undef ARRAY
#undef MATRIX
    }

    template <typename T>
    static void
    dumpSettings(UT_JSONWriter &w, const T &settings)
    {
	using item = std::pair<TfToken, VtValue>;
	UT_Array<item>	list;
	for (auto &&item : settings)
	    list.append({item.first, item.second});
	list.sort([](const item &a, const item &b)
		{
		    return a.first < b.first;
		});
	w.jsonBeginMap();
	for (auto &&s : list)
	{
	    w.jsonKeyToken(s.first.GetText());
	    dumpValue(w, s.second);
	}
	w.jsonEndMap();
    }

    struct FormatSpec
    {
	template <typename T>
	FormatSpec(HdFormat f, const T &v, PXL_DataFormat pf, PXL_Packing pp)
	    : hdFormat(f)
	    , vtZero(v)
	    , pxlFormat(pf)
	    , pxlPacking(pp)
	{
	}
	HdFormat	hdFormat;
	VtValue		vtZero;
	PXL_DataFormat	pxlFormat;
	PXL_Packing	pxlPacking;
    };

    template <typename T> using hdVec2 = std::tuple<T,T>;
    template <typename T> using hdVec3 = std::tuple<T,T,T>;
    template <typename T> using hdVec4 = std::tuple<T,T,T,T>;

    static TfToken      theColor2f("color2f", TfToken::Immortal);
    static TfToken      theColor3f("color3f", TfToken::Immortal);
    static TfToken      theColor4f("color4f", TfToken::Immortal);
    static TfToken      theColor2h("color2h", TfToken::Immortal);
    static TfToken      theColor3h("color3h", TfToken::Immortal);
    static TfToken      theColor4h("color4h", TfToken::Immortal);
    static TfToken      theColor2u8("color2u8", TfToken::Immortal);
    static TfToken      theColor3u8("color3u8", TfToken::Immortal);
    static TfToken      theColor4u8("color4u8", TfToken::Immortal);
    static TfToken      theColor2i8("color2i8", TfToken::Immortal);
    static TfToken      theColor3i8("color3i8", TfToken::Immortal);
    static TfToken      theColor4i8("color4i8", TfToken::Immortal);
    static TfToken      theColor2u16("color2u16", TfToken::Immortal);
    static TfToken      theColor3u16("color3u16", TfToken::Immortal);
    static TfToken      theColor4u16("color4u16", TfToken::Immortal);
    static TfToken      theColor2i16("color2i16", TfToken::Immortal);
    static TfToken      theColor3i16("color3i16", TfToken::Immortal);
    static TfToken      theColor4i16("color4i16", TfToken::Immortal);

    static UT_Set<TfToken>      theColorTokens({
                        theColor2f,
                        theColor3f,
                        theColor4f,
                        theColor2h,
                        theColor3h,
                        theColor4h,
                        theColor2u8,
                        theColor3u8,
                        theColor4u8,
                        theColor2i8,
                        theColor3i8,
                        theColor4i8,
                        theColor2u16,
                        theColor3u16,
                        theColor4u16,
                        theColor2i16,
                        theColor3i16,
                        theColor4i16,
    });


    #define TOK(NAME) TfToken(NAME, TfToken::Immortal)
    static UT_Map<TfToken, FormatSpec>	theFormatSpecs({
	{ TOK("float"), { HdFormatFloat32, float(0), PXL_FLOAT32, PACK_SINGLE }},
	{ theColor2f, { HdFormatFloat32Vec2, GfVec2f(0), PXL_FLOAT32, PACK_UV }},
	{ theColor3f, { HdFormatFloat32Vec3, GfVec3f(0), PXL_FLOAT32, PACK_RGB }},
	{ theColor4f, { HdFormatFloat32Vec4, GfVec4f(0), PXL_FLOAT32, PACK_RGBA }},
	{ TOK("point3f"), { HdFormatFloat32Vec3, GfVec3f(0), PXL_FLOAT32, PACK_RGB }},
	{ TOK("normal3f"), { HdFormatFloat32Vec3, GfVec3f(0), PXL_FLOAT32, PACK_RGB }},
	{ TOK("vector3f"), { HdFormatFloat32Vec3, GfVec3f(0), PXL_FLOAT32, PACK_RGB }},
	{ TOK("float2"), { HdFormatFloat32Vec2, GfVec2f(0), PXL_FLOAT32, PACK_UV }},
	{ TOK("float3"), { HdFormatFloat32Vec3, GfVec3f(0), PXL_FLOAT32, PACK_RGB }},
	{ TOK("float4"), { HdFormatFloat32Vec4, GfVec4f(0), PXL_FLOAT32, PACK_RGBA }},

	{ TOK("half"), { HdFormatFloat16, GfHalf(0), PXL_FLOAT16, PACK_SINGLE }},
	{ TOK("float16"), { HdFormatFloat16, GfHalf(0), PXL_FLOAT16, PACK_SINGLE }},
	{ theColor2h, { HdFormatFloat16Vec2, GfVec2h(0), PXL_FLOAT16, PACK_UV }},
	{ theColor3h, { HdFormatFloat16Vec3, GfVec3h(0), PXL_FLOAT16, PACK_RGB }},
	{ theColor4h, { HdFormatFloat16Vec4, GfVec4h(0), PXL_FLOAT16, PACK_RGBA }},
	{ TOK("point3h"), { HdFormatFloat16Vec3, GfVec3h(0), PXL_FLOAT16, PACK_RGB }},
	{ TOK("normal3h"), { HdFormatFloat16Vec3, GfVec3h(0), PXL_FLOAT16, PACK_RGB }},
	{ TOK("vector3h"), { HdFormatFloat16Vec3, GfVec3h(0), PXL_FLOAT16, PACK_RGB }},
	{ TOK("half2"), { HdFormatFloat16Vec2, GfVec2h(0), PXL_FLOAT16, PACK_UV }},
	{ TOK("half3"), { HdFormatFloat16Vec3, GfVec3h(0), PXL_FLOAT16, PACK_RGB }},
	{ TOK("half4"), { HdFormatFloat16Vec4, GfVec4h(0), PXL_FLOAT16, PACK_RGBA }},

	// Now, create some mappings for HdFormat
	{ TOK("u8"), { HdFormatUNorm8, uint8(0), PXL_INT8, PACK_SINGLE }},
	{ TOK("uint8"), { HdFormatUNorm8, uint8(0), PXL_INT8, PACK_SINGLE }},
	{ theColor2u8, { HdFormatUNorm8Vec2, hdVec2<uint8>(0,0), PXL_INT8, PACK_UV }},
	{ theColor3u8, { HdFormatUNorm8Vec3, hdVec3<uint8>(0,0,0), PXL_INT8, PACK_RGB }},
	{ theColor4u8, { HdFormatUNorm8Vec4, hdVec4<uint8>(0,0,0,0), PXL_INT8, PACK_RGBA }},

	{ TOK("i8"), { HdFormatSNorm8, int8(0), PXL_INT8, PACK_SINGLE }},
	{ TOK("int8"), { HdFormatSNorm8, int8(0), PXL_INT8, PACK_SINGLE }},
	{ theColor2i8, { HdFormatSNorm8Vec2, hdVec2<uint8>(0,0), PXL_INT8, PACK_UV }},
	{ theColor3i8, { HdFormatSNorm8Vec3, hdVec3<uint8>(0,0,0), PXL_INT8, PACK_RGB }},
	{ theColor4i8, { HdFormatSNorm8Vec4, hdVec4<uint8>(0,0,0,0), PXL_INT8, PACK_RGBA }},

	{ TOK("u16"), { HdFormatUInt16, uint16(0), PXL_INT16, PACK_SINGLE }},
	{ TOK("uint16"), { HdFormatUInt16, uint16(0), PXL_INT16, PACK_SINGLE }},
	{ theColor2u16, { HdFormatUInt16Vec2, hdVec2<uint16>(0,0), PXL_INT16, PACK_UV }},
	{ theColor3u16, { HdFormatUInt16Vec3, hdVec3<uint16>(0,0,0), PXL_INT16, PACK_RGB }},
	{ theColor4u16, { HdFormatUInt16Vec4, hdVec4<uint16>(0,0,0,0), PXL_INT16, PACK_RGBA }},

	{ TOK("i16"), { HdFormatInt16, int16(0), PXL_INT16, PACK_SINGLE }},
	{ TOK("int16"), { HdFormatInt16, int16(0), PXL_INT16, PACK_SINGLE }},
	{ theColor2i16, { HdFormatInt16Vec2, hdVec2<uint16>(0,0), PXL_INT16, PACK_UV }},
	{ theColor3i16, { HdFormatInt16Vec3, hdVec3<uint16>(0,0,0), PXL_INT16, PACK_RGB }},
	{ theColor4i16, { HdFormatInt16Vec4, hdVec4<uint16>(0,0,0,0), PXL_INT16, PACK_RGBA }},

	{ TOK("int"), { HdFormatInt32, int(0), PXL_INT32, PACK_SINGLE }},
	{ TOK("int2"), { HdFormatInt32Vec2, GfVec2i(0,0), PXL_INT32, PACK_UV }},
	{ TOK("int3"), { HdFormatInt32Vec3, GfVec3i(0,0,0), PXL_INT32, PACK_RGB }},
	{ TOK("int4"), { HdFormatInt32Vec4, GfVec4i(0,0,0,0), PXL_INT32, PACK_RGBA }},
	{ TOK("uint"), { HdFormatInt32, int(0), PXL_INT32, PACK_SINGLE }},
	{ TOK("uint2"), { HdFormatInt32Vec2, GfVec2i(0,0), PXL_INT32, PACK_UV }},
	{ TOK("uint3"), { HdFormatInt32Vec3, GfVec3i(0,0,0), PXL_INT32, PACK_RGB }},
	{ TOK("uint4"), { HdFormatInt32Vec4, GfVec4i(0,0,0,0), PXL_INT32, PACK_RGBA }},

        // 2025/01/01 texCoord2/texCoord3 are deprecated and included only for compatibilty.
	{ TOK("texCoord2f"), { HdFormatFloat32Vec2, GfVec2f(0), PXL_FLOAT32, PACK_UV }},
	{ TOK("texCoord3f"), { HdFormatFloat32Vec3, GfVec3f(0), PXL_FLOAT32, PACK_RGB }},
	{ TOK("texCoord2h"), { HdFormatFloat16Vec2, GfVec2h(0), PXL_FLOAT16, PACK_UV }},
	{ TOK("texCoord3h"), { HdFormatFloat16Vec3, GfVec3h(0), PXL_FLOAT16, PACK_RGB }},
    });
    #undef TOK

    static const char *
    PXRHdFormat(HdFormat f)
    {
#define CASE(F) case F: return #F;
	switch (f)
	{
	    CASE(HdFormatUNorm8)
	    CASE(HdFormatUNorm8Vec2)
	    CASE(HdFormatUNorm8Vec3)
	    CASE(HdFormatUNorm8Vec4)
	    CASE(HdFormatSNorm8)
	    CASE(HdFormatSNorm8Vec2)
	    CASE(HdFormatSNorm8Vec3)
	    CASE(HdFormatSNorm8Vec4)
	    CASE(HdFormatUInt16)
	    CASE(HdFormatUInt16Vec2)
	    CASE(HdFormatUInt16Vec3)
	    CASE(HdFormatUInt16Vec4)
	    CASE(HdFormatInt16)
	    CASE(HdFormatInt16Vec2)
	    CASE(HdFormatInt16Vec3)
	    CASE(HdFormatInt16Vec4)
	    CASE(HdFormatFloat16)
	    CASE(HdFormatFloat16Vec2)
	    CASE(HdFormatFloat16Vec3)
	    CASE(HdFormatFloat16Vec4)
	    CASE(HdFormatFloat32)
	    CASE(HdFormatFloat32Vec2)
	    CASE(HdFormatFloat32Vec3)
	    CASE(HdFormatFloat32Vec4)
	    CASE(HdFormatInt32)
	    CASE(HdFormatInt32Vec2)
	    CASE(HdFormatInt32Vec3)
	    CASE(HdFormatInt32Vec4)
	    default:
		return "unknown_format";
	}
#undef CASE
    }

    static void
    dumpSpecs()
    {
	UT_ErrorLog::format(2, "Possible aov:format specifications:");
	for (auto &&s : theFormatSpecs)
	{
	    UT_ErrorLog::format(1, "  {} : {} - {}[{}]",
		    s.first,
		    PXRHdFormat(s.second.hdFormat),
		    PXLdataFormat(s.second.pxlFormat),
		    PXLpackingComponents(s.second.pxlPacking));
	}
    }

    static bool
    parseFormat(const TfToken &token,
	    HdFormat &format,
	    VtValue &clearValue,
	    PXL_DataFormat &pxl_format,
	    PXL_Packing &packing)
    {
	auto it = theFormatSpecs.find(token);
	if (it == theFormatSpecs.end())
	    return false;
	format = it->second.hdFormat;
	clearValue = it->second.vtZero;
	pxl_format = it->second.pxlFormat;
	packing = it->second.pxlPacking;
	return true;
    }

    static SYS_HashType
    hashPrimAndProperties(const UsdPrim &prim, fpreal now)
    {
        SYS_HashType    h = TfHash()(prim);
        if (!prim.IsActive())
            SYShashCombine(h, 0xdeadbeef);
        for (const auto &a : prim.GetAttributes())
        {
            VtValue             val;
            SYS_HashType        ax = TfHash()(a);
            SYS_HashType        vx;
            SYShashCombine(h, ax);
            if (a.Get(&val, now))
            {
                vx = TfHash()(val);
                SYShashCombine(h, vx);
            }
        }
        return h;
    }
}	// End anonymous namespace

//-----------------------------------------------------------------

XUSD_RenderSettingsContext::~XUSD_RenderSettingsContext()
{
}

//-----------------------------------------------------------------

XUSD_RenderVar::XUSD_RenderVar()
    : myDataFormat(PXL_FLOAT16)
    , myPacking(PACK_RGB)
{
}

XUSD_RenderVar::~XUSD_RenderVar()
{
}

bool
XUSD_RenderVar::loadFrom(const UsdRenderVar &prim,
        const XUSD_RenderSettingsContext &ctx)
{
    if (!loadAttribute(prim.GetPrim(),
                ctx.evalTime(),
                HusdHuskTokens->driver_parameters_aov_husk_name,
                HusdHuskTokens->driver_parameters_aov_name,
                myAovName))
    {
        UT_ErrorLog::error("Missing {} token in RenderVar {}",
                HusdHuskTokens->driver_parameters_aov_husk_name, prim.GetPath());
        return false;
    }
    myAovToken = TfToken(myAovName);
    return true;
}

UT_UniquePtr<XUSD_RenderVar>
XUSD_RenderVar::clone() const
{
    UT_UniquePtr<XUSD_RenderVar> v = UTmakeUnique<XUSD_RenderVar>();
    v->myHdDesc = myHdDesc;
    v->myAovName = myAovName;
    v->myAovToken = myAovToken;
    v->myDataFormat = myDataFormat;
    v->myPacking = myPacking;
    return v;
}

bool
XUSD_RenderVar::resolveFrom(const UsdRenderVar &rvar,
        const XUSD_RenderSettingsContext &ctx)
{
    UsdPrim	prim = rvar.GetPrim();
    UT_ASSERT(prim);
    myHdDesc = ctx.defaultAovDescriptor(myAovToken);
    buildSettings(myHdDesc.aovSettings, prim.GetPrim(), ctx.evalTime(), false);
    myHdDesc.aovSettings[HusdHuskTokens->sourcePrim] = prim.GetPath();
    importProperty<bool, bool, int32, int64>(prim,
            ctx.evalTime(),
	    myHdDesc.multiSampled,
	    HusdHuskTokens->driver_parameters_aov_husk_multiSampled,
	    HusdHuskTokens->driver_parameters_aov_multiSampled);

    TfToken dt = dataType();
    if (dt.IsEmpty())
    {
        if (!loadAttribute(prim,
                    ctx.evalTime(),
                    HusdHuskTokens->driver_parameters_aov_husk_format,
                    HusdHuskTokens->driver_parameters_aov_format,
                    dt))
        {
            UT_ErrorLog::error("aov:format required for 'auto' data type");
            dumpSpecs();
            return false;
        }
        UT_ErrorLog::format(4, "Choosing data format {} for RenderVar {}",
                dt, prim.GetPath());
    }

    if (!parseFormat(dt,
	    myHdDesc.format,
	    myHdDesc.clearValue,
	    myDataFormat,
	    myPacking))
    {
        UT_ErrorLog::error("Unsupported data format '{}' in RenderVar {}",
		dt, prim.GetPath());
	dumpSpecs();
	return false;
    }
    {
	const UsdAttribute cv = getAuthoredAttribute(prim,
                HusdHuskTokens->driver_parameters_aov_husk_clearValue,
                HusdHuskTokens->driver_parameters_aov_clearValue);
	if (cv)
	    cv.Get(&myHdDesc.clearValue, ctx.evalTime());
    }

    TfToken	aovformat;
    if (loadAttribute(prim,
                ctx.evalTime(),
                HusdHuskTokens->driver_parameters_aov_husk_format,
                HusdHuskTokens->driver_parameters_aov_format,
                aovformat))
    {
	HdFormat	tmpformat;
	VtValue		tmpclear;
	if (!parseFormat(aovformat,
		    tmpformat,
		    tmpclear,
		    myDataFormat,
		    myPacking))
	{
            UT_ErrorLog::error("Unsupported image data format '{}' in RenderVar {}",
		    aovformat, prim.GetPath());
	    dumpSpecs();
	    return false;
	}
    }
    return true;
}

bool
XUSD_RenderVar::buildDefault(const XUSD_RenderSettingsContext &ctx)
{
    myAovToken = HdAovTokens->color;
    myAovName = std::string(myAovToken.GetText());
    myDataFormat = PXL_FLOAT16;
    myPacking = PACK_RGBA;
    // Renderer AOV should be 32 bit float
    myHdDesc = ctx.defaultAovDescriptor(myAovToken);
    if (myHdDesc.format == HdFormatInvalid)
    {
	myHdDesc = HdAovDescriptor(HdFormatFloat32Vec4,
			true, VtValue(GfVec4f(0.0)));
    }
    myHdDesc.aovSettings[UsdRenderTokens->dataType] = UsdRenderTokens->color3f;
    myHdDesc.aovSettings[UsdRenderTokens->sourceType] = UsdRenderTokens->lpe;
    myHdDesc.aovSettings[UsdRenderTokens->sourceName] = theLPECf;
    myHdDesc.aovSettings[HusdHuskTokens->sourcePrim] = theHuskDefault;

    // TODO: build up the quantization settings

    return false;
}

const TfToken &
XUSD_RenderVar::dataType() const
{
    auto it = myHdDesc.aovSettings.find(UsdRenderTokens->dataType);
    if (it == myHdDesc.aovSettings.end())
        return UsdRenderTokens->color3f; // Fallback value
    if (it == myHdDesc.aovSettings.end() || !it->second.IsHolding<TfToken>())
    {
#if UT_ASSERT_LEVEL > 0
        UTdebugFormat("Bad data type: {} {}", it == myHdDesc.aovSettings.end(),
                        myHdDesc.aovSettings.size());
        for (auto &&it : myHdDesc.aovSettings)
            UTdebugFormat("  '{}' = '{}'", it.first, it.second);
#endif
        UT_ASSERT(0);
        return HusdHuskTokens->color4f;
    }
    return it->second.UncheckedGet<TfToken>();
}

const std::string &
XUSD_RenderVar::sourceName() const
{
    auto it = myHdDesc.aovSettings.find(UsdRenderTokens->sourceName);
    if (it == myHdDesc.aovSettings.end() || !it->second.IsHolding<std::string>())
    {
#if UT_ASSERT_LEVEL > 0
        UTdebugFormat("Bad source name: {}", myHdDesc.aovSettings.size());
        for (auto &&it : myHdDesc.aovSettings)
            UTdebugFormat("  '{}' = '{}'", it.first, it.second);
#endif
        UT_ASSERT(0);
        static std::string      theEmpty("");
        return theEmpty;
    }
    return it->second.UncheckedGet<std::string>();
}

const TfToken &
XUSD_RenderVar::sourceType() const
{
    auto it = myHdDesc.aovSettings.find(UsdRenderTokens->sourceType);
    if (it == myHdDesc.aovSettings.end())
    {
        // No sourceType specified - in theory, this should have come from the
        // schema, but it doesn't.
        return UsdRenderTokens->raw;
    }
    UT_ASSERT(it->second.IsHolding<TfToken>());
    if (it->second.IsHolding<TfToken>())
	return it->second.Get<TfToken>();

    // Fall back if it's a string
    if (it->second.IsHolding<std::string>())
    {
        UT_ErrorLog::error("RenderVar {} has string value for sourceType",
                aovName());
        // Leak this since we need to return a reference and the user made a
        // mistake.
        TfToken *tmp = new TfToken(it->second.Get<std::string>());
        return *tmp;
    }

    UT_ErrorLog::error("RenderVar {} has bad sourceType - assuming raw",
            aovName());
    return UsdRenderTokens->raw;
}

void
XUSD_RenderVar::dump(UT_JSONWriter &w) const
{
    w.jsonBeginMap();
    w.jsonKeyValue("AOVName", myAovName);
    w.jsonKeyValue("AOVPixelFormat", PXLdataFormat(myDataFormat));
    w.jsonKeyValue("AOVChannelSize", PXLpackingComponents(myPacking));
    w.jsonKeyValue("HdFormat", PXRHdFormat(myHdDesc.format));
    w.jsonKeyValue("HdMultiSampled", myHdDesc.multiSampled);
    w.jsonKeyToken("HdClearValue");
    dumpValue(w, myHdDesc.clearValue);
    w.jsonKeyToken("settings");
    dumpSettings(w, myHdDesc.aovSettings);
    w.jsonEndMap();
}

//-----------------------------------------------------------------

XUSD_RenderProduct::XUSD_RenderProduct()
    : myIsDefault(false)
{
}

XUSD_RenderProduct::~XUSD_RenderProduct()
{
}

namespace
{
    bool
    isRasterProduct(const UsdPrim &prim)
    {
        VtValue ptype;
        prim.GetAttribute(UsdRenderTokens->productType).Get(&ptype);
        UT_ASSERT(ptype.IsHolding<TfToken>());
        // If we have a non-raster product, we may not need ordered vars
        return ptype.IsHolding<TfToken>()
                && ptype.UncheckedGet<TfToken>() == UsdRenderTokens->raster;
    }
}

bool
XUSD_RenderProduct::loadFrom(const UsdStageRefPtr &usd,
	const UsdRenderProduct &prod,
	const XUSD_RenderSettingsContext &ctx)
{
    UsdPrim             prim = prod.GetPrim();
    auto                vars = prod.GetOrderedVarsRel();
    SdfPathVector       paths;
    if (!vars)
    {
        // If we have a non-raster product, we may not need ordered vars
        if (isRasterProduct(prim))
        {
            UT_ErrorLog::error("No orderedVars to specify channels for {}",
                    prim.GetPath());
            return false;
        }
    }
    else
    {
        vars.GetTargets(&paths);
        if (!paths.size() && isRasterProduct(prim))
        {
            UT_ErrorLog::error("No orderedVars to specify channels for {}",
                    prim.GetPath());
            return false;
        }
    }
    myVars.setCapacityIfNeeded(paths.size());
    for (auto &&p : paths)
    {
        UT_ErrorLog::format(8, "{}: Loading render var: {}", prim.GetPath(), p);
	UsdRenderVar v = UsdRenderVar::Get(usd, p);
	if (!v)
	{
	    UT_ErrorLog::error("Bad orderedVar path {} for product {}",
		    p, prim.GetPath());
	    return false;
	}
	myVars.emplace_back(newRenderVar());
	if (!myVars.last()->loadFrom(v, ctx))
	    return false;
    }

    UT_ErrorLog::format(8, "{} contains {} render vars",
            prim.GetPath(), myVars.size());
    return true;
}

void
XUSD_RenderProduct::updateSettings(const UsdStageRefPtr &usd,
	const UsdRenderProduct &prod,
	const XUSD_RenderSettingsContext &ctx)
{
    UsdPrim             prim = prod.GetPrim();

    // Build settings map, but only include authored values.  These authored
    // values will override values on the settings.
    buildSettings(mySettings, prim, ctx.evalTime(), false);

    if (UsdAttribute productName = prim.GetAttribute(UsdRenderTokens->productName))
    {
	int numFrames = ctx.frameCount();
	if (numFrames > 1 && productName.ValueMightBeTimeVarying())
	{
            UT_ErrorLog::format(8,
                    "Time varying product name ({} frames)", numFrames);
	    fpreal timeInc = ctx.frameInc();
	    fpreal time = ctx.startFrame();
            const std::vector<fpreal>   *flist = ctx.frameList();
	    VtValue val;
	    VtArray<TfToken> names(numFrames);
	    for (int i = 0; i < numFrames; ++i)
	    {
                double  ff = time + i*timeInc;
                if (flist && i < flist->size())
                    ff = (*flist)[i];
		productName.Get(&val, UsdTimeCode(ff));
		UT_ASSERT(val.IsHolding<TfToken>());
		names[i] = val.Get<TfToken>();
	    }
	    mySettings[UsdRenderTokens->productName] = VtValue(names);
	    UT_ASSERT(mySettings[UsdRenderTokens->productName].IsArrayValued());
	}
    }
    myCameraPath = ctx.overrideCamera();
    if (myCameraPath.IsEmpty())
        myCameraPath = resolveCamera(prod.GetCameraRel(), prim.GetPath());
    mySettings[HusdHuskTokens->sourcePrim] = prim.GetPath();
    overrideSettings(mySettings, ctx);

    // Now, set up overrides based on settings
    myShutter.myAuthored = importShutter(usd, myCameraPath,
                                ctx.evalTime(), myShutter.myValue);
    tryImport<GfVec2i, GfVec2i>(mySettings, myRes, UsdRenderTokens->resolution);
    tryImport<float, fpreal32, fpreal64>(mySettings, myPixelAspect,
            UsdRenderTokens->pixelAspectRatio);
    tryImport<GfVec4f, GfVec4f>(mySettings, myDataWindowF,
            UsdRenderTokens->dataWindowNDC);
    tryImport<bool, bool>(mySettings, myDisableMotionBlur,
            UsdRenderTokens->disableMotionBlur);
    tryImport<bool, bool>(mySettings, myInstantaneousShutter,
            UsdRenderTokens->instantaneousShutter);
    tryImport<bool, bool>(mySettings, myDisableDepthOfField,
            UsdRenderTokens->disableDepthOfField);
}

bool
XUSD_RenderProduct::resolveFrom(const UsdStageRefPtr &usd,
	const UsdRenderProduct &prod,
	const XUSD_RenderSettingsContext &ctx)
{
    auto vars = prod.GetOrderedVarsRel();
    UT_ASSERT(vars && "Should have failed in loadFrom()");
    if (!vars)
	return false;

    SdfPathVector	paths;
    vars.GetTargets(&paths);
    if (paths.size() != myVars.size())
    {
	UT_ASSERT(0 && "Paths should match myVars size");
	UT_ErrorLog::error("Programming error - path/var size mismatch");
	return false;
    }
    for (int i = 0, n = myVars.size(); i < n; ++i)
    {
	UsdRenderVar v = UsdRenderVar::Get(usd, paths[i]);
	UT_ASSERT(v && "should have been detected in loadFrom()");
	if (!myVars[i]->resolveFrom(v, ctx))
	    return false;
    }

    updateSettings(usd, prod, ctx);

    return true;
}


bool
XUSD_RenderProduct::buildDefault(const XUSD_RenderSettingsContext &ctx)
{
    const char	*ofile = ctx.defaultProductName();
    if (!ofile)
	ofile = theDefaultImage.c_str();

    // Build settings
    mySettings[UsdRenderTokens->productType] = UsdRenderTokens->raster;
    mySettings[UsdRenderTokens->productName] = TfToken(ofile);
    mySettings[HusdHuskTokens->sourcePrim] = theHuskDefault;

    myVars.emplace_back(newRenderVar());
    myVars.last()->buildDefault(ctx);
    return true;
}

bool
XUSD_RenderProduct::buildDummyRaster(const XUSD_RenderSettingsContext &ctx,
        const XUSD_RenderProduct &src)
{
    UT_ASSERT(src.productType() != UsdRenderTokens->raster);
    UT_ASSERT(src.vars().size());

    mySettings[UsdRenderTokens->productType] = UsdRenderTokens->raster;
    mySettings[UsdRenderTokens->productName] = HusdHuskTokens->huskNullRaster;
    mySettings[HusdHuskTokens->sourcePrim] = theHuskDummyRaster;
    myFilename = UTmakeUnsafeRef(HusdHuskTokens->huskNullRaster.GetText());

    for (const auto &var : src.vars())
        myVars.emplace_back(var->clone());
    return true;
}

const TfToken &
XUSD_RenderProduct::productType() const
{
    auto it = mySettings.find(UsdRenderTokens->productType);
    if (it == mySettings.end())
        return UsdRenderTokens->raster;
    UT_ASSERT(it->second.IsHolding<TfToken>());
    return it->second.Get<TfToken>();
}

bool
XUSD_RenderProduct::isRaster() const
{
    return productType() == UsdRenderTokens->raster;
}

TfToken
XUSD_RenderProduct::productName(int frame) const
{
    auto it = mySettings.find(UsdRenderTokens->productName);
    if (it == mySettings.end())
        return TfToken();
    UT_ASSERT(it != mySettings.end());
    if (it->second.IsHolding<TfToken>())
	return it->second.Get<TfToken>();

    if (it->second.IsHolding<std::string>())
        return TfToken(it->second.Get<std::string>());

    if (it->second.IsHolding<VtArray<TfToken>>())
    {
        const VtArray<TfToken> &names = it->second.Get<VtArray<TfToken>>();
        if (names.size())
        {
            frame = SYSclamp(frame, 0, int(names.size())-1);
            return names[frame];
        }
    }
    if (it->second.IsHolding<VtArray<std::string>>())
    {
        const VtArray<std::string> &names = it->second.Get<VtArray<std::string>>();
        if (names.size())
        {
            frame = SYSclamp(frame, 0, int(names.size())-1);
            return TfToken(names[frame]);
        }
    }
    UT_ErrorLog::error("RenderProduct productName is not holding a TfToken");
    return TfToken();
}

#define SPECIFIC_PRODUCT(MEMBER, MESSAGE) \
    int nauth = 0; \
    auto prevval = val; \
    for (exint i = 0, np = products.size(); i < np; ++i) { \
        if (products[i]->isRaster() && products[i]->MEMBER.myAuthored) { \
            if (i && prevval != products[i]->MEMBER.myValue) { \
                UT_ErrorLog::warning("Not all products have matching {}", \
                        MESSAGE); \
                break; \
            } \
            prevval = products[i]->MEMBER.myValue; \
            nauth++; \
        } \
    } \
    if (nauth == products.size()) val = prevval; \
    return nauth > 0;
    /* end macro */

bool
XUSD_RenderProduct::specificRes(GfVec2i &val, const ProductList &products)
{
    SPECIFIC_PRODUCT(myRes, "resolution");
}

bool
XUSD_RenderProduct::specificPixelAspect(float &val, const ProductList &products)
{
    SPECIFIC_PRODUCT(myPixelAspect, "pixel aspect ratio");
}

bool
XUSD_RenderProduct::specificDataWindow(GfVec4f &val, const ProductList &products)
{
    SPECIFIC_PRODUCT(myDataWindowF, "data window");
}

bool
XUSD_RenderProduct::disableMotionBlur(bool &val) const
{
    // If myDisableMotionBlur is authored, use that
    if (myDisableMotionBlur.import(val))
        return true;
    // Otherwise, fall back to instantaneousShutter
    if (myInstantaneousShutter.import(val))
        return true;
    return false;
}

bool
XUSD_RenderProduct::specificDisableMotionBlur(bool &val, const ProductList &products)
{
    {
        // First check if myDisabledMotionBlur is authored
        SPECIFIC_PRODUCT(myDisableMotionBlur, "disable motion blur");
        // Check whether we authored the disableMotionBlur setting
        if (nauth)
            return true;
    }

    // If we didn't author the disableMotionBlur setting, fall back to
    // instantaneousShutter.
    SPECIFIC_PRODUCT(myInstantaneousShutter, "instantaneous shutter");
}

bool
XUSD_RenderProduct::disableDepthOfField(bool &val) const
{
    // If myDisableDepthOfField is authored, use that
    if (myDisableDepthOfField.import(val))
        return true;
    return false;
}

bool
XUSD_RenderProduct::specificDisableDepthOfField(bool &val, const ProductList &products)
{
    // First check if myDisableDepthOfField is authored
    SPECIFIC_PRODUCT(myDisableDepthOfField, "disable depth of field");
    // Check whether we authored the disableDepthOfField setting
    if (nauth)
        return true;
}
#undef SPECIFIC_PRODUCT

UT_StringHolder
XUSD_RenderProduct::insertExtraBeforeExtension(const UT_StringHolder &filename,
                                        const char *extra,
                                        const char *path)
{
    return makePartName(filename, path, extra);
}

bool
XUSD_RenderProduct::expandProduct(const XUSD_RenderSettingsContext &ctx,
        int pidx, int frame, UT_Set<UT_StringHolder> &other_products)
{
    UT_ASSERT(frame < ctx.frameCount());
    const TfToken	&pname = productName(frame);
    const char          *override = nullptr;
    if (!isHuskNullRaster(myFilename))
        override = ctx.overrideProductName(*this, pidx);
    if (!mySettings[UsdRenderTokens->productName].IsArrayValued()
	|| UTisstring(override))
    {
	bool		 expanded;
	myFilename = expandFile(ctx, override, frame, pname, expanded);
	if (ctx.frameCount() > 1
                && myFilename
		&& !expanded
		&& !isFramebuffer(myFilename)
                && !isHuskNullRaster(myFilename))
	{
	    UT_ErrorLog::error(
                    "Error: Rendering {} frames. The output '{}' {}",
		    ctx.frameCount(), pname,
                    "should have variables to prevent overwriting.");
	    return false;
	}
    }
    else
    {
	myFilename = pname.GetText();
    }

    myFullFilename = myFilename;
    if (!isHuskNullRaster(myFilename))
        myFilename = ctx.addTileSuffix(myFilename);

    if (!other_products.insert(myFilename).second)
    {
        UT_ErrorLog::warning("Multiple products with the same filename: {}",
                myFilename);
        if (isRaster())
        {
            // Don't save out this raster
            myFilename = HusdHuskTokens->huskNullRaster.GetText();
        }
    }

    myPartname = makePartName(myFilename,
                        ctx.overrideSnapshotPath(*this, pidx),
                        ctx.overrideSnapshotSuffix(*this, pidx));
    return myVars.size() > 0 || productType() != UsdRenderTokens->raster;
}

void
XUSD_RenderProduct::dump(UT_JSONWriter &w) const
{
    w.jsonBeginMap();
    w.jsonKeyToken("settings");
    dumpSettings(w, mySettings);
    w.jsonKeyValue("expandedProductName", outputName());
    w.jsonKeyToken("RenderVariables");
    w.jsonBeginArray();
	for (auto &&var : myVars)
	    var->dump(w);
    w.jsonEndArray();
    w.jsonEndMap();
}

bool
XUSD_RenderProduct::collectAovs(TfTokenVector &aovs,
	HdAovDescriptorList &descs) const
{
    TfToken::Set	dups;
    for (auto &&v : aovs)
	dups.insert(v);
    for (auto &v : myVars)
    {
	// Avoid duplicates
	if (dups.insert(v->aovToken()).second)
	{
            UT_ErrorLog::format(8, "Adding AOV for {} {} ({})",
                    v->dataType(), v->aovName(), v->aovToken());
	    aovs.push_back(v->aovToken());
	    descs.push_back(v->desc());
	}
        else
        {
            UT_ErrorLog::format(8, "Skipping duplicate AOV for {}", v->aovToken());
        }
    }
    return true;
}

template <typename T>
static bool
collectImageFilterLists_T(const T &settings,
        const UsdStageRefPtr &usd,
        UT_StringArray  &paths,
        UT_Array<UsdPrim> &prims)
{
    const auto &ifilters = settings.find(HusdHuskTokens->husk_orderedImageFilters);
    if (ifilters == settings.end())
        return false;

    const VtValue       &value = ifilters->second;
    if (!value.IsHolding<SdfPathVector>())
        return false;
    const SdfPathVector &targets = value.UncheckedGet<SdfPathVector>();
    if (!targets.size())
        return false;

    for (const auto &p : targets)
    {
        // TODO: Verify it's a HoudiniImageFilterList primitive
        UsdPrim prim = usd->GetPrimAtPath(p);
        if (prim.IsValid())
        {
            paths.append(p.GetAsString());
            prims.append(prim);
        }
    }
    return true;
}

bool
XUSD_RenderProduct::collectImageFilterLists(const UsdStageRefPtr &usd,
        UT_StringArray &paths,
        UT_Array<UsdPrim> &prims) const
{
    return collectImageFilterLists_T(mySettings, usd, paths, prims);
}


//-----------------------------------------------------------------

XUSD_RenderSettings::XUSD_RenderSettings()
    : myShutter(0.0, 0.0)
    , myRes(0, 0)
    , myPixelAspect(1.0)
    , myDataWindow(0, 0, 0, 0)
    , myDataWindowF(0.0, 0.0, 0.0, 0.0)
    , myDisableMotionBlur(false)
    , myDisableDepthOfField(false)
    , myProductDataWindow(false)
    , myFirstFrame(true)
{
}

XUSD_RenderSettings::~XUSD_RenderSettings()
{
}

bool
XUSD_RenderSettings::init(const UsdStageRefPtr &usd,
			const UT_StringHolder &settings_path,
			XUSD_RenderSettingsContext &ctx)
{
    return init(usd, SdfPath(settings_path.toStdString()), ctx);
}

bool
XUSD_RenderSettings::init(const UsdStageRefPtr &usd,
	const SdfPath &settings_path,
	XUSD_RenderSettingsContext &ctx)
{
    myFirstFrame = true;
    myProducts.clear();
    myProductGroups.clear();

    if (!settings_path.IsEmpty())
    {
	myUsdSettings = UsdRenderSettings::Get(usd, settings_path);
	if (!myUsdSettings)
	{
	    UT_WorkBuffer	path;
	    // Test to see if it's a relative path under settings.
	    path.strcpy("/Render/");
	    path.append(HUSD_Path(settings_path).pathStr());
	    UT_String		strpath(path.buffer());
	    strpath.collapseAbsolutePath();
	    myUsdSettings = UsdRenderSettings::Get(usd, SdfPath(strpath.c_str()));
	}
	if (!myUsdSettings)
	{
	    UT_ErrorLog::warning("Unable to find settings prim: {}",
		    settings_path);
	}
    }
    if (!myUsdSettings)
    {
	myUsdSettings = UsdRenderSettings::GetStageRenderSettings(usd);
	if (myUsdSettings)
	{
	    UT_ErrorLog::warning("Using default settings: {}",
		    myUsdSettings.GetPath());
	}
    }
    ctx.initFromUSD(myUsdSettings);

    // Set default settings
    setDefaults(usd, ctx);

    // Load settings from RenderSettings primitive
    if (!loadFromPrim(usd, ctx))
	return false;

    if (!loadFromOptions(usd, ctx) && !ctx.allowCameraless())
	return false;

    // Now all the settings have been initialized, we can build the render
    // settings map.
    buildRenderSettings(usd, ctx);

    bool        no_camera = myCameraPath.IsEmpty();
    if (no_camera)
    {
        // If there's no camera on the render settings, maybe it's defined on
        // every product.
        no_camera = false;
        for (auto &&p : myProducts)
        {
            if (p->cameraPath().IsEmpty())
            {
                no_camera = true;
                break;
            }
        }
    }

    if (no_camera)
    {
	// If no camera was specified, see if there's a single camera in the
	// scene.
	UT_Array<SdfPath>	cams;
	findCameras(cams, usd->GetPseudoRoot());
	if (cams.size() != 1)
	{
	    listCameras(cams);
	    return false;
	}
	myCameraPath = cams[0];
	UT_ErrorLog::warning("No camera specified, using '{}'", myCameraPath);
    }

    if (myRes[0] < 1 || myRes[1] < 1)
    {
        UT_ErrorLog::error("{} Invalid resolution ({} x {})",
                settings_path, myRes[0], myRes[1]);
        return false;
    }

    return true;
}

bool
XUSD_RenderSettings::updateFrame(const UsdStageRefPtr &usd,
	XUSD_RenderSettingsContext &ctx,
        HUSD_CustomProductAction custom_product_action)
{
    // Indicate we're updating for a subsequent frame in the sequence
    myFirstFrame = false;

    setDefaults(usd, ctx);

    if (!loadFromPrim(usd, ctx))
        return false;

    if (!loadFromOptions(usd, ctx) && !ctx.allowCameraless())
        return false;

    buildRenderSettings(usd, ctx);

    resolveProducts(usd, ctx, custom_product_action);

    return true;
}

void
XUSD_RenderSettings::partitionProducts()
{
    myProductGroups.clear();
    for (exint i = 0, n = myProducts.size(); i < n; ++i)
    {
        const XUSD_RenderProduct        *p = myProducts[i].get();
        SdfPath                          campath = cameraPath(p);
        bool                             found = false;
        for (int g = 0, ng = myProductGroups.size(); g < ng; ++g)
        {
            int first = myProductGroups[g][0];
            if (campath == cameraPath(myProducts[first].get()))
            {
                myProductGroups[g].append(i);
                found = true;
                break;
            }
        }
        if (!found)
        {
            // New product group
            myProductGroups.append(ProductGroup());
            myProductGroups.last().append(i);
        }
    }
}

bool
XUSD_RenderSettings::accountForExtraProducts(const SdfPathVector &paths) const
{
    UT_ASSERT(paths.size() != myProducts.size());

    // We only add products, we don't remove them
    if (paths.size() > myProducts.size())
        return false;

    // There are 3 products we can add, but we should only add them once
    int prod_mplay = 0;
    int prod_default = 0;
    int prod_dummy = 0;
    for (const auto &prod : myProducts)
    {
        if (prod->productType() == UsdRenderTokens->raster)
        {
            if (prod->isDefault())
                prod_default++;
            else if (prod->productName() == HusdHuskTokens->ip)
                prod_mplay++;
            else if (prod->productName() == HusdHuskTokens->huskNullRaster)
                prod_dummy++;
        }
    }
    if (prod_default > 1)
    {
        UT_ErrorLog::error("Programming error - multiple default products");
        return false;
    }
    if (prod_dummy > 1)
    {
        UT_ErrorLog::error("Programming error - multiple dummy raster products");
        return false;
    }
    return paths.size() + prod_mplay+prod_default+prod_dummy == myProducts.size();
}

bool
XUSD_RenderSettings::resolveProducts(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx,
        HUSD_CustomProductAction custom_product_action)
{
    if (!myProducts.size())
    {
	myProducts.emplace_back(newRenderProduct());
        myProducts[0]->setIsDefault();
	if (!myProducts.last()->buildDefault(ctx))
            return false;
    }
    else
    {
        auto products = myUsdSettings.GetProductsRel();
        UT_ASSERT(products || isDefaultProduct());
        if (!products && !isDefaultProduct())
        {
            UT_ErrorLog::error("Programming error - missing render products");
            return false;
        }
        SdfPathVector	paths;
        if (products)
            products.GetTargets(&paths);
        if (paths.size() != myProducts.size()
                && !accountForExtraProducts(paths))
        {
            UT_ErrorLog::error("Programming error - product size mismatch {} != {}",
                    paths.size(), myProducts.size());
            return false;
        }
        for (int i = 0, n = paths.size(); i < n; ++i)
        {
            UsdRenderProduct product = UsdRenderProduct::Get(usd, paths[i]);
            if (!product)
            {
                UT_ErrorLog::error("Invalid UsdRenderProduct: {}", paths[i]);
                return false;
            }
            if (!myProducts[i]->resolveFrom(usd, product, ctx))
                return false;
        }
    }
    if (custom_product_action == CUSTOM_PRODUCT_CREATE_DUMMY_RASTER)
    {
        int     src_prod = -1;
        bool    has_raster = false;
        for (int i = 0, n = myProducts.size(); i < n; ++i)
        {
            const XUSD_RenderProduct    &prod = *myProducts[i];
            if (prod.productType() == UsdRenderTokens->raster)
            {
                has_raster = true;
                break;
            }
            if (src_prod < 0 && prod.vars().size())
                src_prod = i;
        }
        if (!has_raster && src_prod >= 0)
        {
            // Create dummy render product
            UT_ErrorLog::format(1, "Adding dummy raster product");
            myProducts.emplace_back(newRenderProduct());
            if (!myProducts.last()->buildDummyRaster(ctx, *myProducts[src_prod]))
                return false;

            // TODO: This doesn't handle the case where we need to partition
            // the delegate render products into separate groups.  For example,
            // if there are two delegate render products which reference
            // different cameras.
        }
    }
    partitionProducts();
    return true;
}

void
XUSD_RenderSettings::printSettings() const
{
    UT_WorkBuffer	tmp;
    {
	static const UT_Options	printOpts(
		"int json:indentstep", int(4),
		"int json:textwidth", int(1024),
		nullptr);
	UT_AutoJSONWriter	w(tmp);
	w->setOptions(printOpts);
	dump(*w);
    }
    UT_ErrorLog::format(0, "{}", tmp);
}

void
XUSD_RenderSettings::dump() const
{
    UT_AutoJSONWriter	w(std::cerr, false);
    dump(*w);
}

void
XUSD_RenderSettings::dump(UT_JSONWriter &w) const
{
    w.jsonBeginMap();
    if (!myCameraPath.IsEmpty())
        w.jsonKeyValue(UsdRenderTokens->camera.GetText(), myCameraPath.GetString());
    w.jsonKeyToken("RenderSettings");
    dumpSettings(w, mySettings);

    if (myProducts.size())
    {
        w.jsonKeyToken("RenderProducts");
        w.jsonBeginArray();
        for (auto &&p : myProducts)
            p->dump(w);
        w.jsonEndArray();
    }

    w.jsonEndMap();
}

SdfPath
XUSD_RenderSettings::cameraPath(const XUSD_RenderProduct *p) const
{
    SdfPath     path;
    if (p && p->cameraPath(path))
        return path;
    return myCameraPath;
}

bool
XUSD_RenderSettings::expandProducts(const XUSD_RenderSettingsContext &ctx,
	int frame, int product_group, bool delegate_products)
{
    UT_StackBuffer<int> raster_indices(myProducts.size());
    int nrasters = 0;
    bool all_overrides_exist = true;
    if (myProducts.size() > 0 && ctx.overrideProductName(*myProducts[0], 0))
    {
        for (int i = 1, n = myProducts.size(); i < n; ++i)
        {
            if (isHuskNullRaster(myProducts[i]->outputName()))
                continue;
            if (!ctx.overrideProductName(*myProducts[i], i))
            {
                // Not all products have overrides, so fall back and only
                // provide overrides for raster products.
                all_overrides_exist = false;
                break;
            }
        }
    }

    if (all_overrides_exist)
    {
        for (int i = 0, n = myProducts.size(); i < n; ++i)
            raster_indices[i] = i;
    }
    else
    {
        for (int i = 0, n = myProducts.size(); i < n; ++i)
        {
            if (myProducts[i]->isRaster())
                raster_indices[i] = nrasters++;
            else
                raster_indices[i] = -1;
        }
    }
    // We process delegate render products first.  This way, if there are
    // duplicate filenames, the delegate render product gets priority.
    UT_Set<UT_StringHolder>     filenames;
    for (int pidx : myProductGroups[product_group])
    {
        XUSD_RenderProduct      *p = myProducts[pidx].get();
        if (!p->isRaster())
        {
            if (!p->expandProduct(ctx, raster_indices[pidx], frame, filenames))
                return false;
        }
    }
    if (!delegate_products)
        filenames.clear();      // We don't care about duplicate filenames
    for (int pidx : myProductGroups[product_group])
    {
        XUSD_RenderProduct      *p = myProducts[pidx].get();
        if (p->isRaster())
        {
            if (!p->expandProduct(ctx, raster_indices[pidx], frame, filenames))
                return false;
        }
    }
    return true;
}

void
XUSD_RenderSettings::setDefaults(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx)
{
    if (myFirstFrame)
        myProducts.clear();

    myShutter[0] = 0;
    myShutter[1] = 0.5;
    myRes = ctx.defaultResolution();
    myPixelAspect = 1;
    myDataWindowF = GfVec4f(0, 0, 1, 1);
    myProductDataWindow = false;
    myDisableMotionBlur = false;
    myDisableDepthOfField = false;
    // Get default (or option)
    myPurpose = parsePurpose(ctx.defaultPurpose());	// Default

    computeImageWindows(usd, ctx);
}

void
XUSD_RenderSettings::computeImageWindows(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx)
{
    myDataWindow = computeDataWindow(myRes, myDataWindowF);

    if (!importShutter(usd, myCameraPath, ctx.evalTime(), myShutter))
    {
        myShutter[0] = 0;
        myShutter[1] = 0.5;
    }
}

bool
XUSD_RenderSettings::loadFromPrim(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx)
{
    if (!myUsdSettings || !myUsdSettings.GetPrim())
	return true;

    UT_ErrorLog::format(8, "Loading render settings: {}",
            myUsdSettings.GetPrim().GetPath());

    myCameraPath = resolveCamera(myUsdSettings.GetCameraRel(),
			myUsdSettings.GetPrim().GetPath());

    auto products = myUsdSettings.GetProductsRel();
    if (myFirstFrame && products)
    {
	SdfPathVector	paths;
	products.GetTargets(&paths);
	myProducts.setCapacityIfNeeded(paths.size());
	for (const auto &p : paths)
	{
            UT_ErrorLog::format(8, "{}: Loading product: {}",
                    myUsdSettings.GetPrim().GetPath(), p);
	    UsdRenderProduct product = UsdRenderProduct::Get(usd, p);
	    if (!product)
	    {
		UT_ErrorLog::error("Unable to find render product: {}", p);
		return false;
	    }
	    myProducts.emplace_back(newRenderProduct());
	    if (!myProducts.last()->loadFrom(usd, product, ctx))
		return false;
            // Try to update settings from the product
            myProducts.last()->updateSettings(usd, product, ctx);
	}
    }

    myUsdSettings.GetResolutionAttr().Get(&myRes, ctx.evalTime());
    myUsdSettings.GetPixelAspectRatioAttr().Get(&myPixelAspect, ctx.evalTime());
    myUsdSettings.GetDataWindowNDCAttr().Get(&myDataWindowF, ctx.evalTime());
    myUsdSettings.GetIncludedPurposesAttr().Get(&myPurpose, ctx.evalTime());
    if (!myUsdSettings.GetDisableMotionBlurAttr().HasAuthoredValue()
            && myUsdSettings.GetInstantaneousShutterAttr().HasAuthoredValue())
    {
        // If we disableMotionBlur isn't authored, but instantaneousShutter is,
        // then use instantaneousShutter.
        myUsdSettings.GetInstantaneousShutterAttr().Get(&myDisableMotionBlur,
                ctx.evalTime());
    }
    else
    {
        // Use the more modern disableMotionBlur (instantaneousShutter was
        // deprectated)
        myUsdSettings.GetDisableMotionBlurAttr().Get(&myDisableMotionBlur,
                ctx.evalTime());
    }
    myUsdSettings.GetDisableDepthOfFieldAttr().Get(&myDisableDepthOfField, ctx.evalTime());

    // If all products define the author the same value, then we want to
    // override the value defined on the settings.
    XUSD_RenderProduct::specificRes(myRes, myProducts);
    XUSD_RenderProduct::specificPixelAspect(myPixelAspect, myProducts);
    myProductDataWindow = XUSD_RenderProduct::specificDataWindow(myDataWindowF, myProducts);
    XUSD_RenderProduct::specificDisableMotionBlur(myDisableMotionBlur, myProducts);
    XUSD_RenderProduct::specificDisableDepthOfField(myDisableDepthOfField, myProducts);

    UT_ErrorLog::format(8, "{} contains {} render products",
                    myUsdSettings.GetPrim().GetPath(), myProducts.size());

    return true;
}

bool
XUSD_RenderSettings::loadFromOptions(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx)
{
    myRes = ctx.overrideResolution(myRes);

    // Command line option for camera overrides data from prim
    SdfPath cpath = ctx.overrideCamera();
    if (!cpath.IsEmpty())
    {
	myCameraPath = cpath;
	UsdPrim		prim = usd->GetPrimAtPath(myCameraPath);
	UsdGeomCamera	cam(prim);
	if (!cam)
	{
	    UT_ErrorLog::error("Unable to find camera '{}'", cpath);
	    myCameraPath = SdfPath();
	    return false;
	}
	// Pick up things like motion blur settings from the camera.  If
	// there's no settings primitive, these should be the default.
	importProperty<fpreal64, fpreal32, fpreal64>(prim, ctx.evalTime(),
                myShutter[0], UsdGeomTokens->shutterOpen);
	importProperty<fpreal64, fpreal32, fpreal64>(prim, ctx.evalTime(),
                myShutter[1], UsdGeomTokens->shutterClose);
    }
    if (myCameraPath.IsEmpty())
    {
        // Pull the camera from our product list
        for (auto &&prod : myProducts)
        {
            myCameraPath = prod->cameraPath();
            if (!myCameraPath.IsEmpty())
                break;
        }
    }
    if (myCameraPath.IsEmpty())
    {
        // Try to pick up a single camera
        UT_Array<SdfPath>       cams;
        findCameras(cams, usd->GetPseudoRoot());
        if (cams.size() == 0)
        {
            UT_ErrorLog::error("No cameras found in the USD file");
            return false;
        }
        myCameraPath = cams[0];
        UT_ErrorLog::warning(
                "No camera in render settings, defaulting to {}", myCameraPath);
    }

    if (UTisstring(ctx.overridePurpose()))
	myPurpose = parsePurpose(ctx.overridePurpose());

    if (conformPolicy(ctx) == HUSD_AspectConformPolicy::ADJUST_PIXEL_ASPECT)
    {
	// To adjust pixel aspect ratio, we need the camera's apertures as well
	// as the image aspect ratio.
	float		imgaspect = SYSsafediv(fpreal(myRes[0]), fpreal(myRes[1]));
	float		hap, vap;
	UsdPrim		prim = usd->GetPrimAtPath(myCameraPath);
	UsdGeomCamera	cam(prim);
	if (cam)
	{
	    cam.GetHorizontalApertureAttr().Get(&hap, ctx.evalTime());
	    cam.GetVerticalApertureAttr().Get(&vap, ctx.evalTime());
	}
	else
	{
	    vap = 1;
	    hap = vap * imgaspect;
	}

	aspectConform(HUSD_AspectConformPolicy::ADJUST_PIXEL_ASPECT,
		vap, myPixelAspect, SYSsafediv(hap, vap), imgaspect);
    }

    myPixelAspect = ctx.overridePixelAspect(myPixelAspect);
    if (!myProductDataWindow)
    {
        // Only let the context modify the data window if the window is defined
        // on the settings (not the product).  If it's defined on the product,
        // the window has already been adjusted.
        myDataWindowF = ctx.overrideDataWindow(myDataWindowF);
    }
    myDisableMotionBlur = ctx.overrideDisableMotionBlur(myDisableMotionBlur);

    return true;
}

void
XUSD_RenderSettings::buildRenderSettings(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx)
{
    computeImageWindows(usd, ctx);

    // Copy settings from primitive
    if (myUsdSettings.GetPrim())
    {
	buildSettings(mySettings, myUsdSettings.GetPrim(),
            ctx.evalTime(), true);
    }

    ctx.setDefaultSettings(*this, mySettings);
    ctx.overrideSettings(*this, mySettings);

    // Now, copy settings from my member data
    mySettings[HusdHuskTokens->houdini_renderer] = HusdHuskTokens->husk;
    mySettings[UsdGeomTokens->shutterOpen] = myShutter[0];
    mySettings[UsdGeomTokens->shutterClose] = myShutter[1];
    mySettings[UsdRenderTokens->resolution] = myRes;
    mySettings[UsdRenderTokens->pixelAspectRatio] = myPixelAspect;
    mySettings[UsdRenderTokens->dataWindowNDC] = myDataWindowF;
    mySettings[UsdRenderTokens->disableMotionBlur] = myDisableMotionBlur;
    mySettings[UsdRenderTokens->instantaneousShutter] = myDisableMotionBlur;
    mySettings[HusdHuskTokens->includedPurposes] = myPurpose;
    mySettings[HusdHuskTokens->houdini_frame] = fpreal(ctx.evalTime().GetValue());
    mySettings[HusdHuskTokens->houdini_fps] = ctx.fps();
    mySettings[UsdRenderTokens->disableDepthOfField] = myDisableDepthOfField;
}

bool
XUSD_RenderSettings::collectAovs(TfTokenVector &aovs,
        HUSD_CustomProductAction custom_product_action,
        HdAovDescriptorList &descs) const
{
    static UT_StringArray theHideProductTypes = []()
    {
        UT_String product_types_str =
            UT_EnvControl::getString(ENV_HOUDINI_HIDE_PRODUCT_TYPES);
        UT_StringArray product_types;
        product_types_str.tokenize(product_types, " \t\n;");
        return product_types;
    }();
    int         num_raster = 0;

    for (auto &&p : myProducts)
    {
        // If the product isn't a raster product, we will likely skip the AOVs
        if (custom_product_action != CUSTOM_PRODUCT_ADD_AOVS &&
            p->productType() != UsdRenderTokens->raster)
        {
            UT_ErrorLog::format(4, "Non-raster product ({})", p->productType());
            auto it = p->settings().find(HusdHuskTokens->includeAovs);
            // If there's no "requireAovs" setting, we skip the non-raster vars
            // If the value of "requireAovs" is false, then we also skip aovs
            if (it == p->settings().end())
                continue;
            bool        requireAovs;
            if (!getValue<bool, bool, int32, int64>(it->second, requireAovs))
                continue;
            if (!requireAovs)
                continue;
        }
        else if (custom_product_action == CUSTOM_PRODUCT_ADD_AOVS &&
                 theHideProductTypes.find(p->productType().GetText()) >= 0)
        {
            // If the product type matches the env var listing product types
            // that renderers want to hide, then don't return the associated
            // AOVs.
            continue;
        }
        else
        {
            ++num_raster;
        }
	if (!p->collectAovs(aovs, descs))
	    return false;
    }
    if (!num_raster)
        UT_ErrorLog::error("No 'raster' products found in render settings");
    return true;
}

bool
XUSD_RenderSettings::collectImageFilterLists(const UsdStageRefPtr &usd,
        UT_StringArray &paths,
        UT_Array<UsdPrim> &prims,
        bool collect_products) const
{
    paths.clear();
    prims.clear();
    if (collect_products)
    {
        for (auto &&p : myProducts)
            p->collectImageFilterLists(usd, paths, prims);
    }
    // Collect any filters on the render settings prim
    collectImageFilterLists_T(mySettings, usd, paths, prims);
    return prims.size() > 0;
}


UT_StringHolder
XUSD_RenderSettings::outputName(int product_group) const
{
    if (myProducts.size() == 0)
	return UT_StringHolder::theEmptyString;
    const ProductGroup  &pg = myProductGroups[product_group];
    if (pg.size() == 1)
	return myProducts[pg[0]]->outputName();
    UT_WorkBuffer	tmp;
    tmp.strcpy(myProducts[pg[0]]->outputName());
    for (int i = 1, n = pg.size(); i < n; ++i)
	tmp.appendFormat(", {}", myProducts[pg[i]]->outputName());
    return UT_StringHolder(tmp);
}

void
XUSD_RenderSettings::findCameras(UT_Array<SdfPath> &names, UsdPrim prim)
{
    // Called from hdRender as well
    UsdGeomCamera	cam(prim);
    if (cam)
	names.append(prim.GetPath());
    for (auto &&kid : prim.GetAllChildren())
	findCameras(names, kid);
}

template <typename T> bool
XUSD_RenderSettings::aspectConform(HUSD_AspectConformPolicy conform,
		T &vaperture, T &pixel_aspect,
		T camaspect, T imgaspect)
{
    // Coming in:
    //	haperture = pixel_aspect * vaperture * camaspect
    // The goal is to make camaspect == imgaspect
    switch (conform)
    {
	case HUSD_AspectConformPolicy::INVALID:
	case HUSD_AspectConformPolicy::EXPAND_APERTURE:
	{
	    // So, vap = hap/imgaspect = vaperture*camaspect/imageaspect
	    T	vap = SYSsafediv(vaperture * camaspect, imgaspect);
	    if (vap <= vaperture)
		return false;
	    vaperture = vap;	// Increase aperture
	    return true;
	}
	case HUSD_AspectConformPolicy::CROP_APERTURE:
	{
	    // So, vap = hap/imgaspect = vaperture*camaspect/imageaspect
	    T	vap = SYSsafediv(vaperture * camaspect, imgaspect);
	    if (vap >= vaperture)
		return false;
	    vaperture = vap;	// Shrink aperture
	    return true;
	}
	case HUSD_AspectConformPolicy::ADJUST_HAPERTURE:
	    // Karma/HoudiniGL uses vertical aperture, so no need to change it
	    // here.
	    break;
	case HUSD_AspectConformPolicy::ADJUST_VAPERTURE:
	{
	    T	hap = vaperture * camaspect;	// Get horizontal aperture
	    // We want to make ha/va = imgaspect
	    vaperture = hap / imgaspect;
	}
	return true;
	case HUSD_AspectConformPolicy::ADJUST_PIXEL_ASPECT:
	{
	    // We can change the width of a pixel so that hap*aspect/va = img
	    pixel_aspect = SYSsafediv(camaspect, imgaspect);
	}
	return true;
    }
    return false;
}

const TfToken &
XUSD_RenderSettings::conformPolicy(HUSD_AspectConformPolicy p)
{
    switch (p)
    {
	case HUSD_AspectConformPolicy::EXPAND_APERTURE:
	    return UsdRenderTokens->expandAperture;
	case HUSD_AspectConformPolicy::CROP_APERTURE:
	    return UsdRenderTokens->cropAperture;
	case HUSD_AspectConformPolicy::ADJUST_HAPERTURE:
	    return UsdRenderTokens->adjustApertureWidth;
	case HUSD_AspectConformPolicy::ADJUST_VAPERTURE:
	    return UsdRenderTokens->adjustApertureHeight;
	case HUSD_AspectConformPolicy::ADJUST_PIXEL_ASPECT:
	    return UsdRenderTokens->adjustPixelAspectRatio;
	case HUSD_AspectConformPolicy::INVALID:
	    return HusdHuskTokens->invalidConformPolicy;
    }
    return HusdHuskTokens->invalidConformPolicy;
}

HUSD_AspectConformPolicy
XUSD_RenderSettings::conformPolicy(const TfToken &policy)
{
    static UT_Map<TfToken, HUSD_AspectConformPolicy>	theMap = {
	{ UsdRenderTokens->expandAperture,
	    HUSD_AspectConformPolicy::EXPAND_APERTURE},
	{ UsdRenderTokens->cropAperture,
	    HUSD_AspectConformPolicy::CROP_APERTURE},
	{ UsdRenderTokens->adjustApertureWidth,
	    HUSD_AspectConformPolicy::ADJUST_HAPERTURE},
	{ UsdRenderTokens->adjustApertureHeight,
	    HUSD_AspectConformPolicy::ADJUST_VAPERTURE},
	{ UsdRenderTokens->adjustPixelAspectRatio,
	    HUSD_AspectConformPolicy::ADJUST_PIXEL_ASPECT},
    };
    auto &&it = theMap.find(policy);
    if (it == theMap.end())
	return HUSD_AspectConformPolicy::DEFAULT;
    return it->second;
}

HUSD_AspectConformPolicy
XUSD_RenderSettings::conformPolicy(const XUSD_RenderSettingsContext &ctx) const
{
    TfToken	token;
    if (!myUsdSettings)
	return HUSD_AspectConformPolicy::DEFAULT;
    if (!importOption<TfToken, TfToken>(token,
		myUsdSettings.GetAspectRatioConformPolicyAttr(),
		ctx.evalTime()))
    {
	UT_ASSERT(0);
	return HUSD_AspectConformPolicy::DEFAULT;
    }
    return conformPolicy(token);
}

bool
XUSD_RenderSettings::getKarmaRandomSeed(int64 &seed,
        const XUSD_RenderSettingsContext &ctx) const
{
    if (!myUsdSettings)
        return false;
    UsdPrim     prim = myUsdSettings.GetPrim();
    if (!prim)
        return false;

    // 32 bit seed is the common case
    int seed32;
    if (loadAttribute(prim, ctx.evalTime(),
            HusdHuskTokens->karma_global_randomseed,
            TfToken(),
            seed32))
    {
        seed = seed32;
        return true;
    }

    // Try 64 bit seed
    return loadAttribute(prim, ctx.evalTime(),
            HusdHuskTokens->karma_global_randomseed,
            TfToken(),
            seed);
}

static bool
isColorAOV(const HdAovDescriptor &aov)
{
    auto it = aov.aovSettings.find(HusdHuskTokens->driver_parameters_aov_husk_format);
    if (it == aov.aovSettings.end())
        return false;
    if (!it->second.IsHolding<TfToken>())
        return false;
    const TfToken       &format = it->second.UncheckedGet<TfToken>();
    return theColorTokens.contains(format);
}

#if 0
static void
splitAOVs(const UT_StringMap<HdAovDescriptor> &aovs,
        UT_StringHolder &all,
        UT_StringHolder &color)
{
    UT_WorkBuffer       abuf;
    UT_WorkBuffer       cbuf;
    for (const auto &item : aovs)
    {
        if (abuf.length())
            abuf.append(' ');
        abuf.append(item.first);
        if (isColorAOV(item.second))
        {
            if (cbuf.length())
                cbuf.append(' ');
            cbuf.append(item.first);
        }
    }
    all = std::move(abuf);
    color = std::move(cbuf);
}
#endif

void
XUSD_RenderSettings::splitAOVs(const UT_StringMap<HdAovDescriptor> &aovs,
        UT_StringSet &color_aovs,
        UT_StringSet &non_color_aovs)
{
    color_aovs.clear();
    non_color_aovs.clear();
    for (const auto &aov : aovs)
    {
        if (isColorAOV(aov.second))
            color_aovs.insert(aov.first);
        else
            non_color_aovs.insert(aov.first);
    }
}

static void
buildAOVString(UT_WorkBuffer &buf, const UT_StringSet &aovs)
{
    for (const auto &a : aovs)
    {
        if (buf.length())
            buf.append(' ');
        buf.append(a);
    }
}

bool
XUSD_RenderSettings::updateHoudiniImageFilterList(
        const UsdStageRefPtr &stage,
        const SdfPath &usdprim,
        const UT_StringHolder &coppath,
        fpreal frame,
        const UT_StringSet &color_aov_set,
        const UT_StringSet &non_color_aov_set)
{
    UT_PerfMonAutoViewportDrawEvent     perf("LOP Viewer", "Update Slap Comp",
                                                UT_PERFMON_3D_VIEWPORT);
    PY_InterpreterAutoLock      py_lock;
    PY_PyObject                 *py_stage = nullptr;

    if (stage)
    {
        py_stage = (PY_PyObject *)TfMakePyPtr<UsdStageWeakPtr>::Execute(
                            SYSconst_cast(stage)).first;
    }

    PY_EvaluationContext        pctx;
    PY_PyObject *dict = (PY_PyObject *)pctx.getGlobalsDict();
    if (py_stage)
        UT_VERIFY(PY_PyDict_SetItemString(dict, "stage", py_stage) == 0);

    UT_WorkBuffer       script_path;
    UT_WorkBuffer       frame_str;
    UT_String           script;

    script_path.format("{}/husd/slapcomputils.py", PYgetPythonLibsSubdir());
    if (!HoudiniFindFile(script_path.buffer(), script))
    {
        UTdebugFormat("Missing slapcomputils.py?");
        return false;
    }
    frame_str.sprintf("%f", frame);

    UT_WorkBuffer       all_aovs;
    UT_WorkBuffer       color_aovs;
    buildAOVString(color_aovs, color_aov_set);
    all_aovs.strcpy(color_aovs);
    buildAOVString(all_aovs, non_color_aov_set);        // Append the non-color AOVs

    // Since the string returned by GetAsString may not be persistent, make a
    // copy here.
    std::string         usdprim_str = usdprim.GetAsString();

    UT_Array<const char *>      args;
    args.append(script.c_str());
    args.append("--prim");      args.append(usdprim_str.c_str());
    args.append("--name");      args.append(coppath.c_str());
    args.append("--frame");     args.append(frame_str.c_str());
    args.append("--aovs");      args.append(all_aovs.c_str());
    args.append("--caovs");     args.append(color_aovs.c_str());

    PY_Result   result = PYrunPythonStatementsFromFile(args.size(),
                                (char **)args.data(),
                                &pctx);
    if (result.myResultType == PY_Result::ERR)
    {
        UTdebugFormat("Error generating image filters: {}",
                result.myErrValue);
        UTdebugFormat("Stack:\n{}", result.myDetailedErrValue);
        return false;
    }

    return true;
}

SYS_HashType
XUSD_RenderSettings::hashHoudiniImageFilterList(const UsdPrim &prim, fpreal frame)
{
    SYS_HashType        h = hashPrimAndProperties(prim, frame);

    UsdRelationship     rel = prim.GetRelationship(HusdHuskTokens->orderedFilters);
    if (rel.IsValid())
    {
        SdfPathVector   targets;
        rel.GetTargets(&targets);
        for (const auto &path : targets)
        {
            const UsdPrim       &kid = prim.GetPrimAtPath(path);
            if (kid.IsValid())
                SYShashCombine(h, hashPrimAndProperties(kid, frame));
        }
    }
    return h;
}

bool
XUSD_RenderSettings::supportedDelegate(const TfToken &name) const
{
    return true;
}

VtValue
XUSD_RenderSettings::renderProducts(int product_group, bool raster) const
{
    using delegateProduct = HdAovSettingsMap;
    using delegateVar = HdAovSettingsMap;

    VtArray<delegateProduct>	drp;
    for (int pidx : myProductGroups[product_group])
    {
        const XUSD_RenderProduct        *p = myProducts[pidx].get();
	if ((p->productType() == UsdRenderTokens->raster) != raster)
	    continue;

	drp.push_back(delegateProduct());
	delegateProduct &dp = drp[drp.size()-1];
	dp = p->settings();	// The settings dictionary has all we need
        // Override the product name with the frame expanded value
        dp[UsdRenderTokens->productName] = TfToken(p->outputName());
	VtArray<delegateVar>	drv;
	for (auto &&v : p->vars())
	{
	    drv.push_back(delegateVar());
	    delegateVar &dv = drv[drv.size()-1];
	    dv[HusdHuskTokens->dataType] = v->dataType();
	    dv[HusdHuskTokens->sourceName] = v->sourceName();
	    dv[HusdHuskTokens->sourceType] = v->sourceType();

	    const HdAovDescriptor	&desc = v->desc();
	    dv[HusdHuskTokens->aovDescriptor_format] = desc.format;
	    dv[HusdHuskTokens->aovDescriptor_multiSampled] = desc.multiSampled;
	    dv[HusdHuskTokens->aovDescriptor_clearValue] = desc.clearValue;
	    dv[HusdHuskTokens->aovDescriptor_aovSettings] = desc.aovSettings;
	}
	dp[HusdHuskTokens->orderedVars] = VtValue(drv);
    }
    if (drp.size())
	return VtValue(drp);
    return VtValue();
}

double
XUSD_RenderSettings::shutterOpen(const XUSD_RenderProduct *p) const
{
    GfVec2d     val;
    if (p && p->shutter(val))
        return val[0];
    return myShutter[0];
}

double
XUSD_RenderSettings::shutterClose(const XUSD_RenderProduct *p) const
{
    GfVec2d     val;
    if (p && p->shutter(val))
        return val[1];
    return myShutter[1];
}

int
XUSD_RenderSettings::xres(const XUSD_RenderProduct *p) const
{
    GfVec2i     val;
    if (p && p->res(val))
        return val[0];
    return myRes[0];
}

int
XUSD_RenderSettings::yres(const XUSD_RenderProduct *p) const
{
    GfVec2i     val;
    if (p && p->res(val))
        return val[1];
    return myRes[1];
}

GfVec2i
XUSD_RenderSettings::res(const XUSD_RenderProduct *p) const
{
    GfVec2i     val;
    if (p && p->res(val))
        return val;
    return myRes;
}

float
XUSD_RenderSettings::pixelAspect(const XUSD_RenderProduct *p) const
{
    float       val;
    if (p && p->pixelAspect(val))
        return val;
    return myPixelAspect;
}

GfVec4f
XUSD_RenderSettings::dataWindowF(const XUSD_RenderProduct *p) const
{
    GfVec4f     val;
    if (p && p->dataWindow(val))
        return val;
    return myDataWindowF;
}

UT_InclusiveRect
XUSD_RenderSettings::computeDataWindow(const GfVec2i &res, const GfVec4f &win)
{
    float   xmin = SYSceil(res[0] * win[0]);
    float   ymin = SYSceil(res[1] * win[1]);
    float   xmax = SYSceil(res[0] * win[2] - 1);
    float   ymax = SYSceil(res[1] * win[3] - 1);

    return UT_InclusiveRect(int(xmin), int(ymin), int(xmax), int(ymax));
}

UT_DimRect
XUSD_RenderSettings::dataWindow(const XUSD_RenderProduct *p) const
{
    GfVec4f     win;
    GfVec2i     res;
    bool        haswin = p && p->dataWindow(win);
    bool        hasres = p && p->res(res);
    if (haswin || hasres)
    {
        // If the product overrides the data window or resolution, then my data
        // window won't match.  However, if they aren't defined, we copy the
        // value from the settings.
        if (!haswin)
            win = myDataWindowF;
        if (!hasres)
            res = myRes;
        return computeDataWindow(res, win);
    }
    return myDataWindow;
}

bool
XUSD_RenderSettings::disableMotionBlur(const XUSD_RenderProduct *p) const
{
    bool     val;
    if (p && p->disableMotionBlur(val))
        return val;
    return myDisableMotionBlur;
}

bool
XUSD_RenderSettings::disableDepthOfField(const XUSD_RenderProduct *p) const
{
    bool     val;
    if (p && p->disableDepthOfField(val))
        return val;
    return myDisableDepthOfField;
}

template <typename T> bool
XUSD_RenderSettings::aspectConform(const XUSD_RenderSettingsContext &ctx,
	T &vaperture, T &pixel_aspect,
	T cam_aspect, T img_aspect) const
{
    HUSD_AspectConformPolicy	policy = conformPolicy(ctx);
    return aspectConform(policy, vaperture, pixel_aspect,
	    cam_aspect, img_aspect);
}

#define INSTANTIATE_CONFORM(TYPE) \
    template HUSD_API bool XUSD_RenderSettings::aspectConform( \
	    HUSD_AspectConformPolicy c, TYPE &vaperture, TYPE &pixel_aspect, \
	    TYPE cam_aspect, TYPE img_aspect); \
    template HUSD_API bool XUSD_RenderSettings::aspectConform( \
	    const XUSD_RenderSettingsContext &ctx, \
	    TYPE &vaperture, TYPE &pixel_aspect, \
	    TYPE cam_aspect, TYPE img_aspect) const; \
    /* end macro */

INSTANTIATE_CONFORM(fpreal32)
INSTANTIATE_CONFORM(fpreal64)

PXR_NAMESPACE_CLOSE_SCOPE

