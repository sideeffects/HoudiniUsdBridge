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
#include "XUSD_Format.h"
#include <UT/UT_Debug.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_VarScan.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_Options.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_WorkBuffer.h>
#include <tools/henv.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/tokens.h>
#include <UT/UT_JSONWriter.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static constexpr UT_StringLit	theDefaultImage("karma.exr");
    static const std::string		theHuskDefault("husk_default");

#define DECL_TOKEN(VAR, TXT) \
    static const TfToken VAR(TXT, TfToken::Immortal); \
    /* end of macro */
    DECL_TOKEN(theSourcePrim, "sourcePrim");
    DECL_TOKEN(theAovName, "driver:parameters:aov:name");
    DECL_TOKEN(theAovFormat, "driver:parameters:aov:format");
    DECL_TOKEN(theMultiSampledName, "driver:parameters:aov:multiSampled");
    DECL_TOKEN(theClearValueName, "driver:parameters:aov:clearValue");
    DECL_TOKEN(thePurposesName, "includedPurposes");
    DECL_TOKEN(theIPName, "ip");
    DECL_TOKEN(theMDName, "md");
    DECL_TOKEN(theInvalidPolicy, "invalidConformPolicy");
#undef DECL_TOKEN

    static UT_StringHolder
    makePartName(const UT_StringHolder &filename)
    {
#if 1
	static constexpr UT_StringLit	thePartName("_part");
	const char	*ext = strrchr(filename, '.');
	if (!ext)
	{
	    UT_StringHolder	part = filename;
	    part += thePartName.asRef();
	    return part;
	}

	UT_WorkBuffer	 result;
	result.strncpy(filename, ext - filename);
	result.append(thePartName.asRef());
	result.append(ext);
	return UT_StringHolder(result);
#else
	static constexpr UT_StringLit	thePartExt(".part");
	UT_StringHolder	part = filename;
	part += thePartExt.asRef();
	return part;
#endif
    }

    template <typename T>
    static bool
    loadAttribute(const UsdPrim &prim, const UsdTimeCode &time,
            const TfToken &name, T &val)
    {
	const UsdAttribute attr = prim.GetAttribute(name);
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
            T &val, const TfToken &name)
    {
	UsdAttribute	attr = prim.GetAttribute(name);
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
	    UT_ErrorLog::error("Found {} cameras in the USD file.  {}",
		    cams.size(),
		    "Please use the -c option to specify the render camera:");
	    cams.stdsort([](const SdfPath &a, const SdfPath &b)
		    {
			return a < b;
		    }
	    );
	    for (auto &&c : cams)
		UT_ErrorLog::format(0, "  - {}", c);
	}
    }

    template <typename MapType>
    static void
    buildSettings(MapType &map, const UsdPrim &prim, const UsdTimeCode &time)
    {
	for (auto &&attrib : prim.GetAttributes())
	{
	    VtValue val;
	    if (attrib.HasValue() && attrib.Get(&val, time))
		map[attrib.GetName()] = val;
	}
    }

    UT_StringHolder
    expandFile(const XUSD_RenderSettingsContext &ctx, int i,
	    const TfToken &pname, bool &changed)
    {
	const char	*ofile = pname.GetText();

	changed = false;
	if (ctx.overrideProductName())
	    ofile = ctx.overrideProductName();

	if (!UTisstring(ofile))
	    return theDefaultImage.asHolder();

	UT_StringHolder expanded = HUSD_FileExpanded::expand(ofile,
					ctx.startFrame(),
					ctx.frameInc(),
					i,
					changed);
	return expanded;
    }

    static bool
    isFramebuffer(const TfToken &pname)
    {
	return pname == theIPName || pname == theMDName;
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
	list.stdsort([](const item &a, const item &b)
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

    #define TOK(NAME) TfToken(NAME, TfToken::Immortal)
    static UT_Map<TfToken, FormatSpec>	theFormatSpecs({
	{ TOK("float"), { HdFormatFloat32, float(0), PXL_FLOAT32, PACK_SINGLE }},
	{ TOK("color2f"), { HdFormatFloat32Vec2, GfVec2f(0), PXL_FLOAT32, PACK_DUAL }},
	{ TOK("color3f"), { HdFormatFloat32Vec3, GfVec3f(0), PXL_FLOAT32, PACK_RGB }},
	{ TOK("color4f"), { HdFormatFloat32Vec4, GfVec4f(0), PXL_FLOAT32, PACK_RGBA }},
	{ TOK("float2"), { HdFormatFloat32Vec2, GfVec2f(0), PXL_FLOAT32, PACK_DUAL }},
	{ TOK("float3"), { HdFormatFloat32Vec3, GfVec3f(0), PXL_FLOAT32, PACK_RGB }},
	{ TOK("float4"), { HdFormatFloat32Vec4, GfVec4f(0), PXL_FLOAT32, PACK_RGBA }},

	{ TOK("half"), { HdFormatFloat16, GfHalf(0), PXL_FLOAT16, PACK_SINGLE }},
	{ TOK("float16"), { HdFormatFloat16, GfHalf(0), PXL_FLOAT16, PACK_SINGLE }},
	{ TOK("color2h"), { HdFormatFloat16Vec2, GfVec2h(0), PXL_FLOAT16, PACK_DUAL }},
	{ TOK("color3h"), { HdFormatFloat16Vec3, GfVec3h(0), PXL_FLOAT16, PACK_RGB }},
	{ TOK("color4h"), { HdFormatFloat16Vec4, GfVec4h(0), PXL_FLOAT16, PACK_RGBA }},
	{ TOK("half2"), { HdFormatFloat16Vec2, GfVec2h(0), PXL_FLOAT16, PACK_DUAL }},
	{ TOK("half3"), { HdFormatFloat16Vec3, GfVec3h(0), PXL_FLOAT16, PACK_RGB }},
	{ TOK("half4"), { HdFormatFloat16Vec4, GfVec4h(0), PXL_FLOAT16, PACK_RGBA }},

	// Now, create some mappings for HdFormat
	{ TOK("u8"), { HdFormatUNorm8, uint8(0), PXL_INT8, PACK_SINGLE }},
	{ TOK("uint8"), { HdFormatUNorm8, uint8(0), PXL_INT8, PACK_SINGLE }},
	{ TOK("color2u8"), { HdFormatUNorm8Vec2, hdVec2<uint8>(0,0), PXL_INT8, PACK_DUAL }},
	{ TOK("color3u8"), { HdFormatUNorm8Vec3, hdVec3<uint8>(0,0,0), PXL_INT8, PACK_RGB }},
	{ TOK("color4u8"), { HdFormatUNorm8Vec4, hdVec4<uint8>(0,0,0,0), PXL_INT8, PACK_RGBA }},

	{ TOK("i8"), { HdFormatSNorm8, int8(0), PXL_INT8, PACK_SINGLE }},
	{ TOK("int8"), { HdFormatSNorm8, int8(0), PXL_INT8, PACK_SINGLE }},
	{ TOK("color2i8"), { HdFormatSNorm8Vec2, hdVec2<uint8>(0,0), PXL_INT8, PACK_DUAL }},
	{ TOK("color3i8"), { HdFormatSNorm8Vec3, hdVec3<uint8>(0,0,0), PXL_INT8, PACK_RGB }},
	{ TOK("color4i8"), { HdFormatSNorm8Vec4, hdVec4<uint8>(0,0,0,0), PXL_INT8, PACK_RGBA }},

	{ TOK("int"), { HdFormatInt32, int(0), PXL_INT32, PACK_SINGLE }},
	{ TOK("int2"), { HdFormatInt32Vec2, GfVec2i(0,0), PXL_INT32, PACK_DUAL }},
	{ TOK("int3"), { HdFormatInt32Vec3, GfVec3i(0,0,0), PXL_INT32, PACK_RGB }},
	{ TOK("int4"), { HdFormatInt32Vec4, GfVec4i(0,0,0,0), PXL_INT32, PACK_RGBA }},
	{ TOK("uint"), { HdFormatInt32, int(0), PXL_INT32, PACK_SINGLE }},
	{ TOK("uint2"), { HdFormatInt32Vec2, GfVec2i(0,0), PXL_INT32, PACK_DUAL }},
	{ TOK("uint3"), { HdFormatInt32Vec3, GfVec3i(0,0,0), PXL_INT32, PACK_RGB }},
	{ TOK("uint4"), { HdFormatInt32Vec4, GfVec4i(0,0,0,0), PXL_INT32, PACK_RGBA }},
    });
    #undef TOK

    static const char *
    PXLdataFormat(PXL_DataFormat f)
    {
	switch (f)
	{
	    case PXL_INT8:
		return "int8";
	    case PXL_INT16:
		return "int16";
	    case PXL_INT32:
		return "int32";
	    case PXL_FLOAT16:
		return "float16";
	    case PXL_FLOAT32:
		return "float32";
	    default:
		return "unknown_format";
	}
    }
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
	UT_ErrorLog::format(1, "Possible aov:format specifications:");
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
    if (!loadAttribute(prim.GetPrim(), ctx.evalTime(), theAovName, myAovName))
    {
	UT_ErrorLog::error("Missing {} token in RenderVar {}",
		theAovName, prim.GetPath());
	return false;
    }
    myAovToken = TfToken(myAovName);
    return true;
}

bool
XUSD_RenderVar::resolveFrom(const UsdRenderVar &rvar,
        const XUSD_RenderSettingsContext &ctx)
{
    UsdPrim	prim = rvar.GetPrim();
    UT_ASSERT(prim);
    myHdDesc = ctx.defaultAovDescriptor(myAovToken);
    myHdDesc.aovSettings[theSourcePrim] = prim.GetPath();
    buildSettings(myHdDesc.aovSettings, prim.GetPrim(), ctx.evalTime());
    importProperty<bool, bool, int32, int64>(prim,
            ctx.evalTime(),
	    myHdDesc.multiSampled,
	    theMultiSampledName);

    if (!parseFormat(dataType(),
	    myHdDesc.format,
	    myHdDesc.clearValue,
	    myDataFormat,
	    myPacking))
    {
	UTdebugFormat("Unsupported data format '{}' in RenderVar {}",
		dataType(), prim.GetPath());
	dumpSpecs();
	return false;
    }
    {
	UsdAttribute cv = prim.GetAttribute(theClearValueName);
	if (cv)
	    cv.Get(&myHdDesc.clearValue, ctx.evalTime());
    }

    TfToken	aovformat;
    if (loadAttribute(prim, ctx.evalTime(), theAovFormat, aovformat))
    {
	HdFormat	tmpformat;
	VtValue		tmpclear;
	if (!parseFormat(aovformat,
		    tmpformat,
		    tmpclear,
		    myDataFormat,
		    myPacking))
	{
	    UTdebugFormat("Unsupported image data format '{}' in RenderVar {}",
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
    static TfToken	theCName("C", TfToken::Immortal);
    static TfToken	theColor4f("color4f", TfToken::Immortal);
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
    myHdDesc.aovSettings[UsdRenderTokens->dataType] = theColor4f;
    myHdDesc.aovSettings[UsdRenderTokens->sourceType] = UsdRenderTokens->lpe;
    myHdDesc.aovSettings[UsdRenderTokens->sourceName] = std::string("C.*");
    myHdDesc.aovSettings[theSourcePrim] = theHuskDefault;

    // TODO: build up the quantization settings

    return false;
}

const TfToken &
XUSD_RenderVar::dataType() const
{
    auto it = myHdDesc.aovSettings.find(UsdRenderTokens->dataType);
    UT_ASSERT(it != myHdDesc.aovSettings.end());
    UT_ASSERT(it->second.IsHolding<TfToken>());
    return it->second.UncheckedGet<TfToken>();
}

const std::string &
XUSD_RenderVar::sourceName() const
{
    auto it = myHdDesc.aovSettings.find(UsdRenderTokens->sourceName);
    UT_ASSERT(it != myHdDesc.aovSettings.end());
    UT_ASSERT(it->second.IsHolding<std::string>());
    return it->second.UncheckedGet<std::string>();
}

const TfToken &
XUSD_RenderVar::sourceType() const
{
    auto it = myHdDesc.aovSettings.find(UsdRenderTokens->sourceType);
    UT_ASSERT(it != myHdDesc.aovSettings.end());
    UT_ASSERT(it->second.IsHolding<TfToken>());
    return it->second.UncheckedGet<TfToken>();
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
{
}

XUSD_RenderProduct::~XUSD_RenderProduct()
{
}

bool
XUSD_RenderProduct::loadFrom(const UsdStageRefPtr &usd,
	const UsdRenderProduct &prod,
	const XUSD_RenderSettingsContext &ctx)
{
    UsdPrim prim = prod.GetPrim();
    auto vars = prod.GetOrderedVarsRel();
    if (!vars)
    {
	UT_ErrorLog::error("No orderedVars to specify channels for {}",
		prim.GetPath());
	return false;
    }
    SdfPathVector	paths;
    vars.GetTargets(&paths);
    if (!paths.size())
    {
	UT_ErrorLog::error("No orderedVars to specify channels for {}",
		prim.GetPath());
	return false;
    }
    myVars.setCapacityIfNeeded(paths.size());
    for (auto &&p : paths)
    {
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

    buildSettings(mySettings, prim, ctx.evalTime());
    mySettings[theSourcePrim] = prim.GetPath();
    return true;
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
    return true;
}


bool
XUSD_RenderProduct::buildDefault(const XUSD_RenderSettingsContext &ctx)
{
    static TfToken	thePicFormat("file", TfToken::Immortal);
    const char	*ofile = ctx.defaultProductName();
    if (!ofile)
	ofile = theDefaultImage.c_str();

    // Build settings
    mySettings[UsdRenderTokens->productType] = UsdRenderTokens->raster;
    mySettings[UsdRenderTokens->productName] = TfToken(ofile);
    mySettings[theSourcePrim] = theHuskDefault;

    myVars.emplace_back(newRenderVar());
    myVars.last()->buildDefault(ctx);
    return true;
}

const TfToken &
XUSD_RenderProduct::productType() const
{
    auto it = mySettings.find(UsdRenderTokens->productType);
    UT_ASSERT(it != mySettings.end());
    UT_ASSERT(it->second.IsHolding<TfToken>());
    return it->second.Get<TfToken>();
}

const TfToken &
XUSD_RenderProduct::productName() const
{
    auto it = mySettings.find(UsdRenderTokens->productName);
    UT_ASSERT(it != mySettings.end());
    UT_ASSERT(it->second.IsHolding<TfToken>());
    return it->second.Get<TfToken>();
}

bool
XUSD_RenderProduct::expandProduct(const XUSD_RenderSettingsContext &ctx,
        int frame)
{
    const TfToken	&pname = productName();
    bool		 expanded;
    myFilename = expandFile(ctx, frame, pname, expanded);
    if (ctx.frameCount() > 1
	    && !expanded
	    && !isFramebuffer(pname))
    {
	UT_ErrorLog::error("Error: Output file '{}' should have variables",
		pname);
	return false;
    }
    myPartname = makePartName(myFilename);
    return myVars.size() > 0;
}

void
XUSD_RenderProduct::dump(UT_JSONWriter &w) const
{
    w.jsonBeginMap();
    w.jsonKeyToken("settings");
    dumpSettings(w, mySettings);
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
	    aovs.push_back(v->aovToken());
	    descs.push_back(v->desc());
	}
    }
    return true;
}


//-----------------------------------------------------------------

XUSD_RenderSettings::XUSD_RenderSettings()
{
}

XUSD_RenderSettings::~XUSD_RenderSettings()
{
}

bool
XUSD_RenderSettings::init(const UsdStageRefPtr &usd,
	const SdfPath &settings_path,
	XUSD_RenderSettingsContext &ctx)
{
    myProducts.clear();

    if (!settings_path.IsEmpty())
    {
	myUsdSettings = UsdRenderSettings::Get(usd, settings_path);
	if (!myUsdSettings)
	{
	    UT_WorkBuffer	path;
	    // Test to see if it's a relative path under settings.
	    path.strcpy("/Render/");
	    path.append(settings_path.GetString());
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

    return true;
}

bool
XUSD_RenderSettings::resolveProducts(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx)
{
    if (!myProducts.size())
    {
	myProducts.emplace_back(newRenderProduct());
	return myProducts.last()->buildDefault(ctx);
    }
    auto products = myUsdSettings.GetProductsRel();
    UT_ASSERT(products);
    if (!products)
    {
	UT_ErrorLog::error("Programming error - missing render products");
	return false;
    }
    SdfPathVector	paths;
    products.GetTargets(&paths);
    if (paths.size() != myProducts.size())
    {
	UT_ErrorLog::error("Programming error - product size mismatch");
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
    UT_ErrorLog::format(1, "{}", tmp);
    UTdebugFormat("{}", tmp);
}

void
XUSD_RenderSettings::dump(UT_JSONWriter &w) const
{
    w.jsonBeginMap();
    w.jsonKeyValue("RenderDelegate", myRenderer.GetText());
    w.jsonKeyValue("Camera", myCameraPath.GetString());
    w.jsonKeyToken("RenderSettings");
    dumpSettings(w, mySettings);

    w.jsonKeyToken("RenderProducts");
    w.jsonBeginArray();
    for (auto &&p : myProducts)
	p->dump(w);
    w.jsonEndArray();

    w.jsonEndMap();
}

bool
XUSD_RenderSettings::expandProducts(const XUSD_RenderSettingsContext &ctx,
	int frame)
{
    for (auto &&p : myProducts)
    {
	if (!p->expandProduct(ctx, frame))
	    return false;
    }
    return true;
}

void
XUSD_RenderSettings::setDefaults(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx)
{
    myRenderer = ctx.renderer();

    myProducts.clear();
    myShutter[0] = 0;
    myShutter[1] = 0.5;
    myRes = ctx.defaultResolution();
    myPixelAspect = 1;
    myDataWindowF = GfVec4f(0, 0, 1, 1);
    // Get default (or option)
    myPurpose = parsePurpose(ctx.defaultPurpose());	// Default

    computeImageWindows(usd, ctx);
}

void
XUSD_RenderSettings::computeImageWindows(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx)
{
    float	xmin = SYSceil(myRes[0] * myDataWindowF[0]);
    float	ymin = SYSceil(myRes[1] * myDataWindowF[1]);
    float	xmax = SYSceil(myRes[0] * myDataWindowF[2] - 1);
    float	ymax = SYSceil(myRes[1] * myDataWindowF[3] - 1);

    myDataWindow = UT_InclusiveRect(int(xmin), int(ymin), int(xmax), int(ymax));

    UsdPrim		prim = usd->GetPrimAtPath(myCameraPath);
    UsdGeomCamera	cam(prim);
    if (cam)
    {
	cam.GetShutterOpenAttr().Get(&myShutter[0], ctx.evalTime());
	cam.GetShutterCloseAttr().Get(&myShutter[1], ctx.evalTime());
    }
    else
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

    auto cams = myUsdSettings.GetCameraRel();
    if (cams)
    {
	SdfPathVector	paths;
	cams.GetTargets(&paths);
	switch (paths.size())
	{
	    case 0:
		UT_ErrorLog::warning("No camera specified in render settings {}",
			myUsdSettings.GetPrim().GetPath());
		break;
	    case 1:
		myCameraPath = paths[0];
		break;
	    default:
		UT_ErrorLog::warning(
			"Multiple cameras in render settings {}, choosing {}",
			myUsdSettings.GetPrim().GetPath(), paths[0]);
		myCameraPath = paths[0];
		break;
	}
    }
    auto products = myUsdSettings.GetProductsRel();
    if (products)
    {
	SdfPathVector	paths;
	products.GetTargets(&paths);
	myProducts.setCapacityIfNeeded(paths.size());
	for (const auto &p : paths)
	{
	    UsdRenderProduct product = UsdRenderProduct::Get(usd, p);
	    if (!product)
	    {
		UT_ErrorLog::error("Unable to find render product: {}", p);
		return false;
	    }
	    myProducts.emplace_back(newRenderProduct());
	    if (!myProducts.last()->loadFrom(usd, product, ctx))
		return false;
	}
    }

    myUsdSettings.GetResolutionAttr().Get(&myRes, ctx.evalTime());
    myUsdSettings.GetPixelAspectRatioAttr().Get(&myPixelAspect, ctx.evalTime());
    myUsdSettings.GetDataWindowNDCAttr().Get(&myDataWindowF, ctx.evalTime());
    myUsdSettings.GetIncludedPurposesAttr().Get(&myPurpose, ctx.evalTime());

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

    if (UTisstring(ctx.overridePurpose()))
	myPurpose = parsePurpose(ctx.overridePurpose());

    if (conformPolicy(ctx) == HUSD_AspectConformPolicy::ADJUST_PIXEL_ASPECT)
    {
	// To adjust pixel aspect ratio, we need the camera's apertures as well
	// as the image aspect ratio.
	float		imgaspect = SYSsafediv(fpreal(xres()), fpreal(yres()));
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

    return true;
}

void
XUSD_RenderSettings::buildRenderSettings(const UsdStageRefPtr &usd,
	const XUSD_RenderSettingsContext &ctx)
{
    computeImageWindows(usd, ctx);

    ctx.setDefaultSettings(*this, mySettings);

    // Copy settings from primitive
    if (myUsdSettings.GetPrim())
	buildSettings(mySettings, myUsdSettings.GetPrim(), ctx.evalTime());

    ctx.overrideSettings(*this, mySettings);

    // Now, copy settings from my member data
    static TfToken theRendererName("houdini:renderer", TfToken::Immortal);
    static TfToken theHuskName("husk", TfToken::Immortal);
    mySettings[theRendererName] = theHuskName;
    mySettings[UsdGeomTokens->shutterOpen] = myShutter[0];
    mySettings[UsdGeomTokens->shutterClose] = myShutter[1];
    mySettings[UsdRenderTokens->resolution] = myRes;
    mySettings[UsdRenderTokens->pixelAspectRatio] = myPixelAspect;
    mySettings[UsdRenderTokens->dataWindowNDC] = myDataWindowF;
    mySettings[thePurposesName] = myPurpose;
}

bool
XUSD_RenderSettings::collectAovs(TfTokenVector &aovs, HdAovDescriptorList &descs) const
{
    for (auto &&p : myProducts)
    {
	if (!p->collectAovs(aovs, descs))
	    return false;
    }
    return true;
}

UT_StringHolder
XUSD_RenderSettings::outputName() const
{
    if (myProducts.size() == 0)
	return UT_StringHolder::theEmptyString;
    if (myProducts.size() == 1)
	return myProducts[0]->outputName();
    UT_WorkBuffer	tmp;
    tmp.strcpy(myProducts[0]->outputName());
    for (int i = 1, n = myProducts.size(); i < n; ++i)
	tmp.appendFormat(", {}", myProducts[i]->outputName());
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

TfToken
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
	    return theInvalidPolicy;
    }
    return theInvalidPolicy;
}

XUSD_RenderSettings::HUSD_AspectConformPolicy
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

XUSD_RenderSettings::HUSD_AspectConformPolicy
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

