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

#include "BRAY_HdUtil.h"
#include <gusd/GT_VtArray.h>
#include <gusd/GT_VtStringArray.h>
#include <gusd/UT_Gf.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/base/gf/size2.h>
#include <pxr/base/gf/size3.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/camera.h>
#include <SYS/SYS_Math.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_FSATable.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_TagManager.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_WorkBuffer.h>
#include <UT/UT_VarEncode.h>
#include <GT/GT_DAConstantValue.h>
#include <GT/GT_DAIndexedString.h>
#include <HUSD/HUSD_HydraPrim.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_HydraUtils.h>

#include "BRAY_HdParam.h"

// When this define is set, if the SdfAssetPath fails to resolve as a VEX
// variable, we still output the original asset path.  This lets Houdini
// attempt to resolve the path itself (for example, using HOUDINI_TEXTURE_PATH
// or HOUDINI_GEOMETRY_PATH).
//
// This may also be required if there are UDIM textures being used since the
// preview shader expects to be able to expand UDIM textures.
#define USE_HOUDINI_PATH

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static constexpr UT_StringLit	thePrefix("karma:");
    static constexpr UT_StringLit	thePrimvarPrefix("primvars:karma:");

    enum BRAY_USD_TYPE
    {
	BRAY_USD_INVALID,

	BRAY_USD_BOOL,

	BRAY_USD_INT8,
	BRAY_USD_INT16,
	BRAY_USD_INT32,
	BRAY_USD_INT64,

	BRAY_USD_UINT8,
	BRAY_USD_UINT16,
	BRAY_USD_UINT32,
	BRAY_USD_UINT64,

	BRAY_USD_VEC2I,
	BRAY_USD_VEC3I,
	BRAY_USD_VEC4I,

	BRAY_USD_REALH,
	BRAY_USD_VEC2H,
	BRAY_USD_VEC3H,
	BRAY_USD_VEC4H,
	BRAY_USD_QUATH,

	BRAY_USD_REALF,
	BRAY_USD_VEC2F,
	BRAY_USD_VEC3F,
	BRAY_USD_VEC4F,
	BRAY_USD_QUATF,
	BRAY_USD_MAT2F,
	BRAY_USD_MAT3F,
	BRAY_USD_MAT4F,
	BRAY_USD_RANGE1F,

	BRAY_USD_REALD,
	BRAY_USD_VEC2D,
	BRAY_USD_VEC3D,
	BRAY_USD_VEC4D,
	BRAY_USD_QUATD,
	BRAY_USD_MAT2D,
	BRAY_USD_MAT3D,
	BRAY_USD_MAT4D,
	BRAY_USD_RANGE1D,

	BRAY_USD_TFTOKEN,
	BRAY_USD_SDFPATH,
	BRAY_USD_SDFASSETPATH,
	BRAY_USD_STRING,	// std::string
	BRAY_USD_HOLDER,	// UT_StringHolder

	BRAY_USD_MAX_TYPES,
    };

    template <typename T> struct BRAY_UsdResolver;	// Get enum from type
    template <BRAY_USD_TYPE BT> struct BRAY_UsdTypeResolver;	// Get type from enum

    #define BRAY_USD_RESOLVER(CPPTYPE, BTYPE)	\
	template <> struct BRAY_UsdResolver<CPPTYPE> { \
	    static constexpr BRAY_USD_TYPE type = BTYPE; \
	}; \
	template <> struct BRAY_UsdTypeResolver<BTYPE> { \
	    using T = CPPTYPE; \
	    using this_type = CPPTYPE; \
	}; \

    BRAY_USD_RESOLVER(bool, BRAY_USD_BOOL)
    BRAY_USD_RESOLVER(int8, BRAY_USD_INT8)
    BRAY_USD_RESOLVER(int16, BRAY_USD_INT16)
    BRAY_USD_RESOLVER(int32, BRAY_USD_INT32)
    BRAY_USD_RESOLVER(int64, BRAY_USD_INT64)
    BRAY_USD_RESOLVER(uint8, BRAY_USD_UINT8)
    BRAY_USD_RESOLVER(uint16, BRAY_USD_UINT16)
    BRAY_USD_RESOLVER(uint32, BRAY_USD_UINT32)
    BRAY_USD_RESOLVER(uint64, BRAY_USD_UINT64)
    BRAY_USD_RESOLVER(fpreal16, BRAY_USD_REALH)
    BRAY_USD_RESOLVER(GfVec2i, BRAY_USD_VEC2I)
    BRAY_USD_RESOLVER(GfVec3i, BRAY_USD_VEC3I)
    BRAY_USD_RESOLVER(GfVec4i, BRAY_USD_VEC4I)
    BRAY_USD_RESOLVER(GfVec2h, BRAY_USD_VEC2H)
    BRAY_USD_RESOLVER(GfVec3h, BRAY_USD_VEC3H)
    BRAY_USD_RESOLVER(GfVec4h, BRAY_USD_VEC4H)
    BRAY_USD_RESOLVER(GfQuath, BRAY_USD_QUATH)
    BRAY_USD_RESOLVER(fpreal32, BRAY_USD_REALF)
    BRAY_USD_RESOLVER(GfVec2f, BRAY_USD_VEC2F)
    BRAY_USD_RESOLVER(GfVec3f, BRAY_USD_VEC3F)
    BRAY_USD_RESOLVER(GfVec4f, BRAY_USD_VEC4F)
    BRAY_USD_RESOLVER(GfQuatf, BRAY_USD_QUATF)
    BRAY_USD_RESOLVER(GfMatrix2f, BRAY_USD_MAT2F)
    BRAY_USD_RESOLVER(GfMatrix3f, BRAY_USD_MAT3F)
    BRAY_USD_RESOLVER(GfMatrix4f, BRAY_USD_MAT4F)
    BRAY_USD_RESOLVER(GfRange1f, BRAY_USD_RANGE1F)
    BRAY_USD_RESOLVER(fpreal64, BRAY_USD_REALD)
    BRAY_USD_RESOLVER(GfVec2d, BRAY_USD_VEC2D)
    BRAY_USD_RESOLVER(GfVec3d, BRAY_USD_VEC3D)
    BRAY_USD_RESOLVER(GfVec4d, BRAY_USD_VEC4D)
    BRAY_USD_RESOLVER(GfQuatd, BRAY_USD_QUATD)
    BRAY_USD_RESOLVER(GfRange1d, BRAY_USD_RANGE1D)
    BRAY_USD_RESOLVER(GfMatrix2d, BRAY_USD_MAT2D)
    BRAY_USD_RESOLVER(GfMatrix3d, BRAY_USD_MAT3D)
    BRAY_USD_RESOLVER(GfMatrix4d, BRAY_USD_MAT4D)
    BRAY_USD_RESOLVER(TfToken, BRAY_USD_TFTOKEN)
    BRAY_USD_RESOLVER(SdfPath, BRAY_USD_SDFPATH)
    BRAY_USD_RESOLVER(SdfAssetPath, BRAY_USD_SDFASSETPATH)
    BRAY_USD_RESOLVER(std::string, BRAY_USD_STRING)
    BRAY_USD_RESOLVER(UT_StringHolder, BRAY_USD_HOLDER)

    #undef BRAY_USD_RESOLVER

    static BRAY_USD_TYPE
    mapType(const std::type_index &tidx)
    {
	#define MAP_TYPE(CPPTYPE, USDTYPE)	\
	    { std::type_index(typeid(CPPTYPE)), USDTYPE },
	static UT_Map<std::type_index, BRAY_USD_TYPE>	theMap({
		MAP_TYPE(bool, BRAY_USD_BOOL)
		MAP_TYPE(int8, BRAY_USD_INT8)
		MAP_TYPE(int16, BRAY_USD_INT16)
		MAP_TYPE(int32, BRAY_USD_INT32)
		MAP_TYPE(int64, BRAY_USD_INT64)
		MAP_TYPE(uint8, BRAY_USD_UINT8)
		MAP_TYPE(uint16, BRAY_USD_UINT16)
		MAP_TYPE(uint32, BRAY_USD_UINT32)
		MAP_TYPE(uint64, BRAY_USD_UINT64)
		MAP_TYPE(fpreal16, BRAY_USD_REALH)
		MAP_TYPE(GfVec2i, BRAY_USD_VEC2I)
		MAP_TYPE(GfVec3i, BRAY_USD_VEC3I)
		MAP_TYPE(GfVec4i, BRAY_USD_VEC4I)
		MAP_TYPE(GfVec2h, BRAY_USD_VEC2H)
		MAP_TYPE(GfVec3h, BRAY_USD_VEC3H)
		MAP_TYPE(GfVec4h, BRAY_USD_VEC4H)
		MAP_TYPE(GfQuath, BRAY_USD_QUATH)
		MAP_TYPE(fpreal32, BRAY_USD_REALF)
		MAP_TYPE(GfVec2f, BRAY_USD_VEC2F)
		MAP_TYPE(GfVec3f, BRAY_USD_VEC3F)
		MAP_TYPE(GfVec4f, BRAY_USD_VEC4F)
		MAP_TYPE(GfQuatf, BRAY_USD_QUATF)
		MAP_TYPE(GfMatrix2f, BRAY_USD_MAT2F)
		MAP_TYPE(GfMatrix3f, BRAY_USD_MAT3F)
		MAP_TYPE(GfMatrix4f, BRAY_USD_MAT4F)
		MAP_TYPE(GfRange1f, BRAY_USD_RANGE1F)
		MAP_TYPE(fpreal64, BRAY_USD_REALD)
		MAP_TYPE(GfVec2d, BRAY_USD_VEC2D)
		MAP_TYPE(GfVec3d, BRAY_USD_VEC3D)
		MAP_TYPE(GfVec4d, BRAY_USD_VEC4D)
		MAP_TYPE(GfQuatd, BRAY_USD_QUATD)
		MAP_TYPE(GfRange1d, BRAY_USD_RANGE1D)
		MAP_TYPE(GfMatrix2d, BRAY_USD_MAT2D)
		MAP_TYPE(GfMatrix3d, BRAY_USD_MAT3D)
		MAP_TYPE(GfMatrix4d, BRAY_USD_MAT4D)
		MAP_TYPE(TfToken, BRAY_USD_TFTOKEN)
		MAP_TYPE(SdfPath, BRAY_USD_SDFPATH)
		MAP_TYPE(SdfAssetPath, BRAY_USD_SDFASSETPATH)
		MAP_TYPE(std::string, BRAY_USD_STRING)
		MAP_TYPE(UT_StringHolder, BRAY_USD_HOLDER)
	});
	#undef MAP_TYPE
	auto it = theMap.find(std::type_index(tidx));
	if (it != theMap.end())
	    return it->second;
	UTdebugFormat("Invalid type {}", tidx.name());
	return BRAY_USD_INVALID;
    }

    static BRAY_USD_TYPE
    valueType(const VtValue &val)
    {
	if (val.IsArrayValued())
	    return BRAY_USD_INVALID;	// It's an array
	return mapType(std::type_index(val.GetTypeid()));
    }

    static BRAY_USD_TYPE
    arrayType(const VtValue &val)
    {
	if (!val.IsArrayValued())
	    return BRAY_USD_INVALID;	// Not an array
	return mapType(std::type_index(val.GetElementTypeid()));
    }

    // Returns the tuple size (or 0 for an error)
    constexpr static int
    materialTypeSize(BRAY_USD_TYPE type, BRAY::MaterialInput::Storage &store)
    {
	store = BRAY::MaterialInput::Storage::FLOAT;
	switch (type)
	{
	    case BRAY_USD_BOOL:
	    case BRAY_USD_INT8:
	    case BRAY_USD_INT16:
	    case BRAY_USD_INT32:
	    case BRAY_USD_INT64:
	    case BRAY_USD_UINT8:
	    case BRAY_USD_UINT16:
	    case BRAY_USD_UINT32:
	    case BRAY_USD_UINT64:
		store = BRAY::MaterialInput::Storage::INTEGER;
		return 1;

	    case BRAY_USD_VEC2I:
		// VEX has no integer vectors, we intrpret as float
		return 2;
	    case BRAY_USD_VEC3I:
		// VEX has no integer vectors, we intrpret as float
		return 3;
	    case BRAY_USD_VEC4I:
		// VEX has no integer vectors, we intrpret as float
		return 4;

	    case BRAY_USD_REALH:
	    case BRAY_USD_REALF:
	    case BRAY_USD_REALD:
		return 1;

	    case BRAY_USD_VEC2H:
	    case BRAY_USD_VEC2F:
	    case BRAY_USD_VEC2D:
	    case BRAY_USD_RANGE1F:
	    case BRAY_USD_RANGE1D:
		return 2;

	    case BRAY_USD_VEC3H:
	    case BRAY_USD_VEC3F:
	    case BRAY_USD_VEC3D:
		return 3;

	    case BRAY_USD_VEC4H:
	    case BRAY_USD_VEC4F:
	    case BRAY_USD_VEC4D:
	    case BRAY_USD_QUATH:
	    case BRAY_USD_QUATF:
	    case BRAY_USD_QUATD:
		return 4;

	    case BRAY_USD_MAT2F:
	    case BRAY_USD_MAT2D:
		return 4;

	    case BRAY_USD_MAT3F:
	    case BRAY_USD_MAT3D:
		return 9;

	    case BRAY_USD_MAT4F:
	    case BRAY_USD_MAT4D:
		return 16;

	    case BRAY_USD_TFTOKEN:
	    case BRAY_USD_SDFPATH:
	    case BRAY_USD_SDFASSETPATH:
	    case BRAY_USD_STRING:
	    case BRAY_USD_HOLDER:
		store = BRAY::MaterialInput::Storage::STRING;
		return 1;

	    case BRAY_USD_INVALID:
	    case BRAY_USD_MAX_TYPES:
		break;
	}
	return 0;
    };

    static inline const char *
    stripPrefix(const char *name)
    {
	if (!strncmp(name, thePrefix.c_str(), thePrefix.length()))
	    return name + thePrefix.length();
	if (!strncmp(name, thePrimvarPrefix.c_str(), thePrimvarPrefix.length()))
	    return name + thePrimvarPrefix.length();
	return name;
    }

    static inline UT_StringHolder
    tokenToString(const TfToken &token)
    {
	if (token.IsImmortal())
	    return UTmakeUnsafeRef(token.GetText());
	return UT_StringHolder(token.GetText());
    }
    static inline UT_StringHolder
    tokenToString(const std::string &token)
    {
	return UT_StringHolder(token);
    }
    static inline UT_StringHolder
    tokenToString(const SdfAssetPath &path)
    {
	return BRAY_HdUtil::resolvePath(path);
    }

    static inline VtValue
    getValue(const BRAY::OptionSet &opt, const char *name,
	    const HdRenderSettingsMap &settings)
    {
	auto it = settings.find(TfToken(name));
	return it == settings.end() ? VtValue() : it->second;
    }

    static inline VtValue
    getValue(const BRAY::OptionSet &opt, int token,
	    const HdRenderSettingsMap &settings)
    {
	UT_StringHolder	name = opt.fullName(token);
	auto it = settings.find(TfToken(name.c_str()));
	if (it == settings.end())
	    it = settings.find(TfToken(opt.name(token).c_str()));
	return it == settings.end() ? VtValue() : it->second;
    }

    static inline VtValue
    getValue(const BRAY::OptionSet &opt, int token,
	    HdSceneDelegate &sd, const SdfPath &path)
    {
	UT_StringHolder	name = opt.fullName(token);
	VtValue v = sd.Get(path, TfToken(name.c_str()));
	if (v.IsEmpty())
	    v = sd.Get(path, TfToken(opt.name(token).c_str()));
	return v;
    }

    template <typename T>
    static inline bool
    setScalar(BRAY::OptionSet &opt, int token, const VtValue &val)
    {
	if (val.IsHolding<T>())
	{
	    opt.set(token, val.UncheckedGet<T>());
	    return true;
	}
	if (val.IsArrayValued() && val.GetArraySize() == 1
		&& val.IsHolding<VtArray<T>>())
	{
	    opt.set(token, val.UncheckedGet<VtArray<T>>()[0]);
	    return true;
	}
	return false;
    }

    static inline bool
    setString(BRAY::OptionSet &opt, int token, const VtValue &val)
    {
	UT_ASSERT(!val.IsEmpty());
	if (val.IsHolding<TfToken>())
	{
	    //UTdebugFormat("Set {} to {}", myToken, val.UncheckedGet<TfToken>());
	    return opt.set(token, tokenToString(val.UncheckedGet<TfToken>()));
	}
	if (val.IsHolding<std::string>())
	{
	    //UTdebugFormat("Set {} to {}", myToken, val.Get<std::string>());
	    return opt.set(token, tokenToString(val.UncheckedGet<std::string>()));
	}
	if (val.IsHolding<SdfAssetPath>())
	{
	    //UTdebugFormat("Set {} to {}", myToken, val.Get<SdfAssetPath>());
	    return opt.set(token, tokenToString(val.UncheckedGet<SdfAssetPath>()));
	}
	if (val.IsHolding<UT_StringHolder>())
	{
	    //UTdebugFormat("Set {} to {}", myToken, val.Get<std::string>());
	    return opt.set(token, val.UncheckedGet<UT_StringHolder>());
	}
	UTdebugFormat("Type[{}/{}]: {}", token, opt.name(token), val.GetType().GetTypeName());
	UT_ASSERT(0 && "Value not holding string option");
	return false;
    }

    template <typename T, typename S, typename... Types>
    static inline bool
    setScalar(BRAY::OptionSet &opt, int token, const VtValue &val)
    {
	UT_ASSERT(!val.IsEmpty());
	if (setScalar<T>(opt, token, val))
	    return true;
	// Check the rest of the types
	if (setScalar<S, Types...>(opt, token, val))
	    return true;
	// Some integer properties can be set by their menu options.
	return setString(opt, token, val);
    }

    template <typename T>
    static inline bool
    setVector(BRAY::OptionSet &options, int token, const T &val)
    {
	return options.set(token, val.data(), T::dimension);
    }
    template <typename T>
    static inline bool
    setRange(BRAY::OptionSet &options, int token, const T &val)
    {
	fpreal64    data[2] = { val.GetMin(), val.GetMax() };
	return options.set(token, data, 2);
    }

    template <typename T>
    static inline bool
    setVector(BRAY::OptionSet &opt, int token, const VtValue &val)
    {
	if (val.IsHolding<T>())
	{
	    opt.set(token, val.UncheckedGet<T>().data(), T::dimension);
	    return true;
	}
	if (val.IsArrayValued() && val.GetArraySize() == 1
		&& val.IsHolding<VtArray<T>>())
	{
	    opt.set(token,
		    val.UncheckedGet<VtArray<T>>()[0].data(), T::dimension);
	    return true;
	}
	return false;
    }

    template <typename T, typename S>
    static inline bool
    setVector(BRAY::OptionSet &opt, int token, const VtValue &val)
    {
	if (setVector<T>(opt, token, val))
	    return true;
	if (setVector<S>(opt, token, val))
	    return true;

	UTdebugFormat("Type[{}]: {}", token, val.GetType().GetTypeName());
	UT_ASSERT(val.IsEmpty() && "Value holding wrong type for option");
	return false;
    }

    static inline bool
    bray_setOption(BRAY::OptionSet &options, int token, const VtValue &val)
    {
	switch (options.storage(token))
	{
	    case GT_STORE_UINT8:	// Bool option
		UT_ASSERT(options.size(token) == 1);
		return setScalar<bool>(options, token, val);
	    case GT_STORE_STRING:	// Bool option
		UT_ASSERT(options.size(token) == 1
			|| options.size(token) == -1);
		return setString(options, token, val);
	    case GT_STORE_INT64:
		switch (options.size(token))
		{
		    case 1:
			return setScalar<int64, int32, bool>(options, token, val);
		    case 2:
			return setVector<GfVec2i>(options, token, val);
		    case 3:
			return setVector<GfVec3i>(options, token, val);
		    case 4:
			return setVector<GfVec4i>(options, token, val);
		    default:
			UT_ASSERT(0 && "Unhandled int vector size");
		}
		break;
	    case GT_STORE_REAL64:
		switch (options.size(token))
		{
		    case 1:
			return setScalar<fpreal64, fpreal32, int64, int32, bool>(
				options, token, val);
		    case 2:
			return setVector<GfVec2d, GfVec2f>(options, token, val);
		    case 3:
			return setVector<GfVec3d, GfVec3f>(options, token, val);
		    case 4:
			return setVector<GfVec4d, GfVec4f>(options, token, val);
		    default:
			UT_ASSERT(0 && "Unhandled int vector size");
		}
		break;
	    default:
		UT_ASSERT(0);
	}
	return false;
    }

    template <typename ENUM_TYPE>
    static inline bool
    updateGenericOptions(BRAY::ScenePtr &scene,
	    const HdRenderSettingsMap &settings)
    {
	bool				changed = false;
	constexpr size_t		nopts = BRAYmaxOptions<ENUM_TYPE>();
	constexpr BRAY_PropertyType	ptype = BRAYpropertyType<ENUM_TYPE>();
	BRAY::OptionSet			options = scene.defaultProperties(ptype);
	UT_WorkBuffer			storage;
	for (size_t i = 0; i < nopts; ++i)
	{
	    VtValue	value = getValue(options, i, settings);
	    if (value.IsEmpty())
	    {
		const char *name = BRAYproperty(storage, ptype, i,
					    thePrefix.c_str());
		if (UTisstring(name))
		    value = getValue(options, name, settings);
	    }
	    if (!value.IsEmpty())
		changed |= bray_setOption(options, i, value);
	}
	return changed;
    }

    static inline bool
    bray_updateSceneOptions(BRAY::ScenePtr &scene,
				    const HdRenderSettingsMap &settings)
    {
	bool		changed = false;
	changed |= updateGenericOptions<BRAY_SceneOption>(scene, settings);
	changed |= updateGenericOptions<BRAY_ObjectProperty>(scene, settings);
	changed |= updateGenericOptions<BRAY_LightProperty>(scene, settings);
	changed |= updateGenericOptions<BRAY_CameraProperty>(scene, settings);
	changed |= updateGenericOptions<BRAY_PlaneProperty>(scene, settings);
	return changed;
    }

    static inline bool
    bray_updateObjectProperties(BRAY::OptionSet &props,
				    HdSceneDelegate &sd,
				    const SdfPath &path)
    {
	// Iterate over all the scene options checking if they exist in the
	// settings.
	bool changed = false;
	for (int i = 0; i < BRAY_OBJ_MAX_PROPERTIES; ++i)
	{
	    VtValue	value = getValue(props, i, sd, path);
	    if (!value.IsEmpty())
		changed |= bray_setOption(props, i, value);
	}
	return changed;
    }

    template <typename T>
    static inline bool
    vectorEqual(BRAY::OptionSet &options, int token, const T &val)
    {
	return options.isEqual(token, val.data(), T::dimension);
    }

    template <typename T>
    static inline bool
    rangeEqual(BRAY::OptionSet &options, int token, const T &val)
    {
	fpreal64  data[2] = { val.GetMin(), val.GetMax() };
	return options.isEqual(token, data, 2);
    }

    static inline bool
    bray_optionNeedsUpdate(const BRAY::ScenePtr &scene,
	    const TfToken &name,
	    const VtValue &val)
    {
	std::pair<BRAY_PropertyType, int>	prop = BRAYproperty(
						    stripPrefix(name.GetText()),
						    BRAY_SCENE_PROPERTY
						);
	if (!BRAYisValid(prop))
	    return false;

	BRAY::OptionSet	options = scene.defaultProperties(prop.first);
	int		token = prop.second;

	#define IS_EQUAL(CTYPE) \
	    case BRAY_UsdResolver<CTYPE>::type: \
		UT_ASSERT_P(val.IsHolding<CTYPE>()); \
		return !options.isEqual(token, val.UncheckedGet<CTYPE>()); \
	    /* end macro */
	#define IS_EQUAL_VECTOR(CTYPE) \
	    case BRAY_UsdResolver<CTYPE>::type: \
		UT_ASSERT_P(val.IsHolding<CTYPE>()); \
		return !vectorEqual(options, token, val.UncheckedGet<CTYPE>()); \
	    /* end macro */
	#define IS_EQUAL_RANGE(CTYPE) \
	    case BRAY_UsdResolver<CTYPE>::type: \
		UT_ASSERT_P(val.IsHolding<CTYPE>()); \
		return !rangeEqual(options, token, val.UncheckedGet<CTYPE>()); \
	    /* end macro */
	#define IS_EQUAL_STRING(CTYPE) \
	    case BRAY_UsdResolver<CTYPE>::type: \
		UT_ASSERT_P(val.IsHolding<CTYPE>()); \
		return !options.isEqual(token, tokenToString(val.UncheckedGet<CTYPE>())); \
	    /* end macro */
	switch (valueType(val))
	{
	    IS_EQUAL(bool)
	    IS_EQUAL(int32)
	    IS_EQUAL(int64)
	    IS_EQUAL(fpreal32)
	    IS_EQUAL(fpreal64)
	    IS_EQUAL_VECTOR(GfVec2i)
	    IS_EQUAL_VECTOR(GfVec3i)
	    IS_EQUAL_VECTOR(GfVec4i)
	    IS_EQUAL_VECTOR(GfVec2f)
	    IS_EQUAL_VECTOR(GfVec3f)
	    IS_EQUAL_VECTOR(GfVec4f)
	    IS_EQUAL_VECTOR(GfVec2d)
	    IS_EQUAL_VECTOR(GfVec3d)
	    IS_EQUAL_VECTOR(GfVec4d)
	    IS_EQUAL_RANGE(GfRange1f)
	    IS_EQUAL_RANGE(GfRange1d)
	    IS_EQUAL_STRING(TfToken)
	    IS_EQUAL_STRING(std::string)
	    IS_EQUAL_STRING(SdfAssetPath)
	    IS_EQUAL(UT_StringHolder)
	    default:
		break;
	}
	#undef IS_EQUAL
	UTdebugFormat("Unhandled type: {}", val.GetTypeName());
	return false;
    }

    /// This class will unlock an object property, restoring it's locked status
    /// on exit.  This allows the scene to forcibly set object property values
    /// even if they are locked.
    struct ObjectPropertyOverride
    {
	ObjectPropertyOverride(BRAY::ScenePtr &scene,
		BRAY_PropertyType ptype,
		int id)
	    : myScene(scene)
	    , myPType(ptype)
	    , myId(id)
	{
	    if (myPType == BRAY_OBJECT_PROPERTY)
	    {
		// Unlock the property so it can be modified
		myState = myScene.lockProperty(BRAY_ObjectProperty(myId), false);
	    }
	}
	~ObjectPropertyOverride()
	{
	    // If we had a locked object property, then re-lock on destruct
	    if (myPType == BRAY_OBJECT_PROPERTY && myState)
		myScene.lockProperty(BRAY_ObjectProperty(myId), true);
	}
	BRAY::ScenePtr		&myScene;
	BRAY_PropertyType	 myPType;
	int			 myId;
	bool			 myState;
    };

    static inline bool
    bray_updateSceneOption(BRAY::ScenePtr &scene,
	    const TfToken &name,
	    const VtValue &val)
    {
	std::pair<BRAY_PropertyType, int>	prop = BRAYproperty(
						    stripPrefix(name.GetText()),
						    BRAY_SCENE_PROPERTY
						);
	if (prop.first == BRAY_INVALID_PROPERTY || prop.second < 0)
	    return false;

	BRAY::OptionSet options = scene.defaultProperties(prop.first);
	int		token = prop.second;

	ObjectPropertyOverride	override(scene, prop.first, prop.second);

	#define DO_SET(CTYPE) \
	    case BRAY_UsdResolver<CTYPE>::type: \
		UT_ASSERT_P(val.IsHolding<CTYPE>()); \
		return options.set(token, val.UncheckedGet<CTYPE>()); \
	    /* end macro */
	#define DO_SET_VECTOR(CTYPE) \
	    case BRAY_UsdResolver<CTYPE>::type: \
		UT_ASSERT_P(val.IsHolding<CTYPE>()); \
		return setVector(options, token, val.UncheckedGet<CTYPE>()); \
	    /* end macro */
	#define DO_SET_RANGE(CTYPE) \
	    case BRAY_UsdResolver<CTYPE>::type: \
		UT_ASSERT_P(val.IsHolding<CTYPE>()); \
		return setRange(options, token, val.UncheckedGet<CTYPE>()); \
	    /* end macro */
	#define DO_SET_STRING(CTYPE) \
	    case BRAY_UsdResolver<CTYPE>::type: \
		UT_ASSERT_P(val.IsHolding<CTYPE>()); \
		return options.set(token, tokenToString(val.UncheckedGet<CTYPE>())); \
	    /* end macro */

	switch (valueType(val))
	{
	    DO_SET(bool)
	    DO_SET(int32)
	    DO_SET(int64)
	    DO_SET(fpreal32)
	    DO_SET(fpreal64)
	    DO_SET_VECTOR(GfVec2i)
	    DO_SET_VECTOR(GfVec3i)
	    DO_SET_VECTOR(GfVec4i)
	    DO_SET_VECTOR(GfVec2f)
	    DO_SET_VECTOR(GfVec3f)
	    DO_SET_VECTOR(GfVec4f)
	    DO_SET_VECTOR(GfVec2d)
	    DO_SET_VECTOR(GfVec3d)
	    DO_SET_VECTOR(GfVec4d)
	    DO_SET_RANGE(GfRange1f)
	    DO_SET_RANGE(GfRange1d)
	    DO_SET_STRING(TfToken)
	    DO_SET_STRING(std::string)
	    DO_SET_STRING(SdfAssetPath)
	    DO_SET(UT_StringHolder)
	    default:
		break;
	}
	UTdebugFormat("Unhandled type: {}", val.GetTypeName());
	return false;
    }
    using EvalStyle = BRAY_HdUtil::EvalStyle;

    static constexpr UT_StringLit	theOpenParen("(");
    static constexpr UT_StringLit	theCloseParen(")");
    static constexpr UT_StringLit	theP("P");
    static constexpr UT_StringLit	theN("N");
    static constexpr UT_StringLit	thePScale("pscale");
    static constexpr UT_StringLit	theWidth("width");

    static const char *
    getPrimvarProperty(const char *name)
    {
	static constexpr UT_StringLit	theRenderPrefix("karma:");
	if (!strncmp(name, theRenderPrefix.c_str(), theRenderPrefix.length()))
	    return name + theRenderPrefix.length();
	return nullptr;
    }

    static GT_Type
    typeHint(const TfToken &token)
    {
	if (token == HdTokens->points)
	    return GT_TYPE_POINT;
	if (token == HdTokens->normals)
	    return GT_TYPE_NORMAL;
	if (token == HdTokens->displayColor)
	    return GT_TYPE_COLOR;
	return GT_TYPE_NONE;
    }

    static bool
    hasNamespace(const TfToken &name)
    {
	return UT_StringWrap(name.GetText()).startsWith(thePrefix.c_str(),
		true, thePrefix.length());
    }

    static bool
    isVector3(const GT_DataArrayHandle &a)
    {
	return a && a->getTupleSize() == 3 && GTisFloat(a->getStorage());
    }

    static bool
    hasNull(const UT_Array<GT_DataArrayHandle> &array)
    {
	for (auto &&d : array)
	    if (!d)
		return true;
	return false;
    };

    template <typename T>
    static void
    vexPrintScalar(UT_WorkBuffer &buf, const T &v)
    {
	buf.appendFormat("{}", v);
    }

    template <typename T>
    static void
    vexPrintQuoted(UT_WorkBuffer &buf, const T &v)
    {
	// TODO: If the string has an embedded quote, we need to protect the
	// contents
	buf.appendFormat("'{}'", v);
    }

    template <typename T>
    static void
    vexPrintVector(UT_WorkBuffer &buf, const T *v, int size)
    {
	buf.appendFormat("{{{}", v[0]);
	for (int i = 1; i < size; i++)
	    buf.appendFormat(",{}", v[i]);
	buf.append("}");
    }

    template <typename T>
    static void
    vexPrintQuat(UT_WorkBuffer &buf, const T &q)
    {
	buf.appendFormat("{{{}", q.GetReal());
	const auto &im = q.GetImaginary();
	for (int i = 0; i < 3; ++i)
	    buf.appendFormat(",{}", im[i]);
	buf.append("}");
    }

    template <typename T>
    static void
    vexPrintMatrix(UT_WorkBuffer &buf, const T *v, int dim)
    {
	buf.append("{");
	vexPrintVector(buf, v, dim);
	for (int i = 1; i < dim; ++i)
	{
	    v += dim;
	    buf.append(",");
	    vexPrintVector(buf, v, dim);
	}
	buf.append("}");
    }

    template <typename T>
    static void
    vexVectorArg(UT_StringArray &args, const T *v, int size)
    {
	UT_WorkBuffer	tmp;
	for (int i = 0; i < size; ++i)
	{
	    tmp.format("{}", v[i]);
	    args.append(tmp);
	}
    }

    template <typename T>
    static void
    vexRangeArg(UT_StringArray &args, const T &v)
    {
	UT_WorkBuffer	tmp;
	tmp.format("{}", v.GetMin());
	args.append(tmp);
	tmp.format("{}", v.GetMax());
	args.append(tmp);
    }

    static GfMatrix4d
    doLerp(const GfMatrix4d &a, const GfMatrix4d &b, double t)
    {
	// TODO: Better blending of transform
	GfMatrix4d		 m;
	double		*d = m.data();
	const double	*ad = a.data();
	const double	*bd = b.data();
	for (int i = 0; i < 16; ++i)
	    d[i] = SYSlerp(ad[i], bd[i], t);
	return m;
    }

    static GT_DataArrayHandle
    doLerp(const GT_DataArrayHandle &a, const GT_DataArrayHandle &b, double t)
    {
	UT_ASSERT(a->entries() == b->entries());
	if (!GTisFloat(a->getStorage()))
	{
	    // Conditional interpolation
	    return t < .5 ? a : b;
	}
	UT_UniquePtr<GT_Real32Array> r(new GT_Real32Array(a->entries(),
						a->getTupleSize(),
						a->getTypeInfo()));
	GT_DataArrayHandle	 astore, bstore;
	const fpreal32		*av = a->getF32Array(astore);
	const fpreal32		*bv = b->getF32Array(bstore);
	fpreal32		*rv = r->data();
	for (exint i = 0, n = a->getTupleSize() * a->entries(); i < n; ++i)
	    rv[i] = SYSlerp(av[i], bv[i], t);

	return GT_DataArrayHandle(r.release());
    }

    static VtValue
    doLerp(const VtValue &a, const VtValue &b, double t)
    {
	#define INTERP(CTYPE) \
	case BRAY_UsdResolver<CTYPE>::type: \
	    UT_ASSERT_P(a.IsHolding<CTYPE>()); \
	    UT_ASSERT_P(b.IsHolding<CTYPE>()); \
	    return VtValue(SYSlerp(a.UncheckedGet<CTYPE>(), \
				   b.UncheckedGet<CTYPE>(), t)); \
	/* end of macro */

	// Conditional interpolation
	#define CINTERP(CTYPE)	\
	case BRAY_UsdResolver<CTYPE>::type: \
	    UT_ASSERT_P(a.IsHolding<CTYPE>()); \
	    UT_ASSERT_P(b.IsHolding<CTYPE>()); \
	    return VtValue(t < .5 ? a.UncheckedGet<CTYPE>() \
				  : b.UncheckedGet<CTYPE>()); \
	/* end macro */

	switch (valueType(a))
	{
	    INTERP(fpreal64)
	    INTERP(fpreal32)
	    INTERP(fpreal16)
	    CINTERP(bool)
	    CINTERP(int8)
	    CINTERP(int16)
	    CINTERP(int32)
	    CINTERP(int64)
	    CINTERP(uint8)
	    CINTERP(uint16)
	    CINTERP(uint32)
	    CINTERP(uint64)
	    CINTERP(std::string)
	    CINTERP(TfToken)
	    CINTERP(UT_StringHolder)
	    default:
		UT_ASSERT(0 && "Unhandled interpolation type");
	}
	return a;
    }
    #undef INTERP
    #undef CINTERP

    template <typename T>
    static void
    lerp(T &result, const T *src, float t, float t0, float t1)
    {
	if (t == t0)
	    result = src[0];
	else if (t == t1)
	    result = src[1];
	else
	{
	    double tt = (t - t0)/(t1 - t0);
	    result = doLerp(src[0], src[1], tt);
	}
    }

    template <typename T>
    static void
    interpolateValues(UT_Array<T> &result, const T *samples,
	    const float *times, int ntimes,
	    const float *utimes, int nutimes)
    {
	switch (nutimes)
	{
	    case 1:
		result.append(samples[0]);
		break;

	    case 2:
		// Linear blur.
		result.setSize(2);
		// It's possible the times don't match though, so we have to
		// make sure to fix the times.
		lerp(result[0], samples, times[0], utimes[0], utimes[1]);
		lerp(result[1], samples, times[ntimes-1], utimes[0], utimes[1]);
		break;

	    default:
		UT_ASSERT(utimes[0] <= times[0]
			&& utimes[nutimes-1] >= times[ntimes-1]
			&& "USD times should bracket requested times");
		result.setSize(ntimes);
		int	base = 0;
		for (int i = 0; i < ntimes; ++i)
		{
		    // Move to the next interpolation region.
		    // (i.e. times[base] < times[i], times[base+1] >= times[i])
		    while (base < nutimes-2 && utimes[base+1] < times[i])
			base++;
		    lerp(result[i], samples+base,
			    times[i], utimes[base], utimes[base+1]);
		}
		break;
	}
    }

    class primvarSamples
    {
    public:
	primvarSamples(int nsegs)
	{
	    bumpSize(nsegs);
	}
	void	bumpSize(int nsegs)
	{
	    myTimes.bumpSize(nsegs);
	    myValues.bumpSize(nsegs);
	}
	int	 size() const { return myTimes.size(); }
	float	*times() { return myTimes.data(); }
	VtValue	*values() { return myValues.data(); }

	// Some camera values are specified in mm, but are automatically
	// converted to cm in Hydra.  However, when sampling motion, there's
	// no interface to sample blurred camera values, so raw primvars are
	// sampled.  When this happens, we need to manually convert the values
	// from mm to cm.
	//
	// This conversion happens in: UsdImagingCameraAdapter::UpdateForTime()
	void	convertMMtoCM(int nsegs)
	{
	    for (int i = 0; i < nsegs; ++i)
	    {
		UT_ASSERT(myValues[i].IsHolding<float>());
		myValues[i] = VtValue(0.1f * myValues[i].UncheckedGet<float>());
	    }
	}
    private:
	UT_SmallArray<float>	myTimes;
	UT_SmallArray<VtValue>	myValues;
    };

    template <EvalStyle STYLE=BRAY_HdUtil::EVAL_GENERIC>
    static size_t
    samplePrimvar(HdSceneDelegate *sd,
	    const SdfPath &id,
	    const TfToken &name,
	    primvarSamples &samples)
    {
	// There seems to be an issue with the Apple test scenes and the
	// Kitchen where SamplePrimvar() doesn't return the same array as Get()
	// for single motion segments.
	//
	// This seems to be mostly fixed, except for $RTK/inst_attrib1, where
	// SamplePrimvar() doesn't properly expand the duplicated values.
	//
	// There doesn't seem to be a way to evaluate motion samples for camera
	// or light parameters.
	if (samples.size() == 1)
	{
	    samples.times()[0] = 0;
	    switch (STYLE)
	    {
		case BRAY_HdUtil::EVAL_GENERIC:
		    samples.values()[0] = sd->Get(id, name);
		    break;
		case BRAY_HdUtil::EVAL_CAMERA_PARM:
		    samples.values()[0] = sd->GetCameraParamValue(id, name);
		    break;
		case BRAY_HdUtil::EVAL_LIGHT_PARM:
		    samples.values()[0] = sd->GetLightParamValue(id, name);
		    break;
	    }
	    return samples.values()[0].IsEmpty() ? 0 : 1;
	}
	int usegs = sd->SamplePrimvar(id, name, samples.size(),
				samples.times(), samples.values());
	if (usegs > samples.size())
	{
	    samples.bumpSize(usegs);
	    usegs = sd->SamplePrimvar(id, name, samples.size(),
				samples.times(), samples.values());
	}
	if (STYLE == BRAY_HdUtil::EVAL_CAMERA_PARM)
	{
	    for (const auto &tok : {
		    HdCameraTokens->horizontalAperture,
		    HdCameraTokens->verticalAperture,
		    HdCameraTokens->horizontalApertureOffset,
		    HdCameraTokens->verticalApertureOffset,
		    HdCameraTokens->focalLength })
	    {
		if (name == tok)
		{
		    samples.convertMMtoCM(usegs);
		    break;
		}
	    }
	}
	sd->Get(id, name);	// Flush from the value cache
	return usegs;
    }

    static UT_FSATableT<BRAY_RayVisibility, BRAY_RAY_NONE>  theRayType(
	BRAY_RAY_CAMERA,	"primary",
	BRAY_RAY_DIFFUSE,	"diffuse",
	BRAY_RAY_REFLECT,	"reflect",
	BRAY_RAY_REFRACT,	"refract",
	BRAY_RAY_SHADOW,	"shadow",
	-1, nullptr);

    static void
    setRenderVisibility(BRAY::OptionSet &props, const VtValue &value)
    {
	UT_StringHolder visibility;
	if (value.IsHolding<VtArray<std::string> >())
	    visibility = value.UncheckedGet<VtArray<std::string> >()[0];
	else if (value.IsHolding<VtArray<UT_StringHolder> >())
	    visibility = value.UncheckedGet<VtArray<UT_StringHolder> >()[0];
	else
	    UT_ASSERT(0 && "Expected string array");

	BRAY_RayVisibility mask = BRAY_RAY_NONE;
	// Lifted from mantra
	if (visibility != "*")
	{
	    UT_Array<const char *>	matching;
	    UT_Array<const char *>	failing;
	    bool			outmatch;

	    UT_TagManager		mgr;
	    UT_String		errors;
	    UT_TagExpressionPtr	tag = mgr.createExpression(visibility, errors);

	    tag->matchAllNames(matching, failing, outmatch);

	    BRAY_RayVisibility fail = BRAY_RAY_NONE;
	    for (int i = 0; i < matching.entries(); i++)
		mask = mask | theRayType.findSymbol(matching(i));
	    for (int i = 0; i < failing.entries(); i++)
		fail = fail | theRayType.findSymbol(failing(i));

	    if (outmatch)
		mask = mask | ~(fail|mask);
	}
	else
	{
	    mask = BRAY_RAY_RENDER_MASK;
	}

	props.set(BRAY_OBJ_RENDER_MASK, int64(mask));
	BRAY_RayVisibility combinedmask = 
	    BRAY_RayVisibility(*props.ival(BRAY_OBJ_VISIBILITY_MASK));
	// Preserve renderTag masks (purposes) from visibility updates
	combinedmask = (combinedmask & ~BRAY_RAY_RENDER_MASK) | mask;
	props.set(BRAY_OBJ_VISIBILITY_MASK, int64(combinedmask));
    }

    static void
    lockObjectProperties(BRAY::ScenePtr &scene)
    {
	scene.lockAllObjectProperties(false);
	scene.lockProperties(
		*scene.sceneOptions().sval(BRAY_OPT_OVERRIDE_OBJECT), true);
    }

}

const char *
BRAY_HdUtil::valueToVex(UT_WorkBuffer &buf, const VtValue &val)
{
    if (val.IsArrayValued())
    {
	UT_ASSERT(0 && "Array types not handled");
	return nullptr;
    }

#define HANDLE_SCALAR1(NAME, TYPE) \
    case BRAY_UsdResolver<TYPE>::type: \
	UT_ASSERT_P(val.IsHolding<TYPE>()); \
	vexPrintScalar(buf, val.UncheckedGet<TYPE>()); \
	return NAME; \
    /* end of macro */

#define HANDLE_SCALAR2(NAME, FTYPE, DTYPE) \
    HANDLE_SCALAR1(NAME, FTYPE) \
    HANDLE_SCALAR1(NAME, DTYPE) \
    /* end of macro */
#define HANDLE_SCALAR3(NAME, HTYPE, FTYPE, DTYPE) \
    HANDLE_SCALAR1(NAME, HTYPE) \
    HANDLE_SCALAR1(NAME, FTYPE) \
    HANDLE_SCALAR1(NAME, DTYPE) \
    /* end of macro */
#define HANDLE_STRING(TYPE) \
    case BRAY_UsdResolver<TYPE>::type: \
	UT_ASSERT_P(val.IsHolding<TYPE>()); \
	vexPrintQuoted(buf, val.UncheckedGet<TYPE>()); \
	return "string"; \
    /* end of macro */
#define HANDLE_VECTOR1(NAME, TYPE, SIZE) \
    case BRAY_UsdResolver<TYPE>::type: \
	UT_ASSERT_P(val.IsHolding<TYPE>()); \
	vexPrintVector(buf, val.UncheckedGet<TYPE>().GetArray(), SIZE); \
	return NAME; \
    /* end macro */
#define HANDLE_VECTORF(NAME, TYPE, SIZE) \
    case BRAY_UsdResolver<TYPE##h>::type: break; \
    HANDLE_VECTOR1(NAME, TYPE##f, SIZE) \
    HANDLE_VECTOR1(NAME, TYPE##d, SIZE) \
    /* end of macro */
#define HANDLE_VECTOR(NAME, TYPE, SIZE) \
    HANDLE_VECTOR1(NAME, TYPE##i, SIZE) \
    HANDLE_VECTORF(NAME, TYPE, SIZE) \
    /* end of macro */
#define HANDLE_MATRIX1(NAME, TYPE, SIZE) \
    case BRAY_UsdResolver<TYPE>::type: \
	UT_ASSERT_P(val.IsHolding<TYPE>()); \
	vexPrintMatrix(buf, val.UncheckedGet<TYPE>().GetArray(), SIZE); \
	return NAME; \
    /* end of macro */
#define HANDLE_MATRIX(NAME, TYPE, SIZE) \
    HANDLE_MATRIX1(NAME, TYPE##f, SIZE) \
    HANDLE_MATRIX1(NAME, TYPE##d, SIZE) \
    /* end of macro */

    switch (valueType(val))
    {
	HANDLE_SCALAR3("float", fpreal16, fpreal32, fpreal64);
	HANDLE_SCALAR2("int8", int8, uint8);
	HANDLE_SCALAR2("int16", int16, uint16);
	HANDLE_SCALAR2("int", int32, uint32);
	HANDLE_SCALAR2("int64", int64, uint64);
	HANDLE_SCALAR1("bool", bool);
	HANDLE_VECTOR("vector2", GfVec2, 2);
	HANDLE_VECTOR("vector",  GfVec3, 3);
	HANDLE_VECTOR("vector4", GfVec4, 4);
	HANDLE_MATRIX("matrix2", GfMatrix2, 2);
	HANDLE_MATRIX("matrix3", GfMatrix3, 3);
	HANDLE_MATRIX("matrix",  GfMatrix4, 4);
	case BRAY_USD_RANGE1F:
	{
	    UT_ASSERT_P(val.IsHolding<GfRange1f>());
	    GfRange1f r = val.UncheckedGet<GfRange1f>();
	    buf.appendFormat("{{{},{}}}", r.GetMin(), r.GetMax());
	    return "vector2";
	}
	case BRAY_USD_RANGE1D:
	{
	    UT_ASSERT_P(val.IsHolding<GfRange1f>());
	    GfRange1f r = val.UncheckedGet<GfRange1f>();
	    buf.appendFormat("{{{},{}}}", r.GetMin(), r.GetMax());
	    return "vector2";
	}
	HANDLE_STRING(std::string);
	HANDLE_STRING(TfToken);
	HANDLE_STRING(SdfPath);
	HANDLE_STRING(UT_StringHolder)
	case BRAY_USD_QUATH: break;	// Half not handled
	case BRAY_USD_QUATF:
	    UT_ASSERT_P(val.IsHolding<GfQuatf>());
	    vexPrintQuat(buf, val.UncheckedGet<GfQuatf>());
	    return "vector4";
	case BRAY_USD_QUATD:
	    UT_ASSERT_P(val.IsHolding<GfQuatd>());
	    vexPrintQuat(buf, val.UncheckedGet<GfQuatd>());
	    return "vector4";
	case BRAY_USD_SDFASSETPATH:
	{
	    UT_ASSERT_P(val.IsHolding<SdfAssetPath>());
	    SdfAssetPath p = val.UncheckedGet<SdfAssetPath>();
	    vexPrintQuoted(buf, resolvePath(p));
	    return "string";
	}
	case BRAY_USD_INVALID:
	case BRAY_USD_MAX_TYPES:
	    break;
    }
    if (!val.IsEmpty())
    {
	//UTdebugFormat("Unhandled Type: {}", val.GetTypeName());
	UT_ASSERT(0 && "Unhandled data type");
    }
    return nullptr;
#undef HANDLE_MATRIX
#undef HANDLE_STRING
#undef HANDLE_SCALAR
#undef HANDLE_VECTOR
}

bool
BRAY_HdUtil::appendVexArg(UT_StringArray &args,
	const UT_StringHolder &name,
	const VtValue &val)
{
    UT_WorkBuffer	wbuf;
    bool		is_array = false;
    BRAY_USD_TYPE	t = valueType(val);
    if (t == BRAY_USD_INVALID)
    {
	t = arrayType(val);
	is_array = true;
    }
#define SCALAR_ARG1(TYPE) \
    case BRAY_UsdResolver<TYPE>::type: \
	args.append(name); \
	if (!is_array) { \
	    UT_ASSERT_P(val.IsHolding<TYPE>()); \
	    vexPrintScalar(wbuf, val.UncheckedGet<TYPE>()); \
	    args.append(wbuf); \
	} else { \
	    UT_ASSERT_P(val.IsHolding<VtArray<TYPE>>()); \
	    const VtArray<TYPE> &arr = val.UncheckedGet< VtArray<TYPE> >(); \
	    args.append(theOpenParen.asHolder()); \
	    for (VtArray<TYPE>::const_iterator it = arr.cbegin(), \
		end = arr.cend(); it != end; ++it) \
	    { \
		wbuf.clear(); \
		vexPrintScalar(wbuf, *it); \
		args.append(wbuf); \
	    } \
	    args.append(theCloseParen.asHolder()); \
	} \
	return true; \
	/* end of macro */

#define SCALAR_ARG2(T1, T2) \
    SCALAR_ARG1(T1) \
    SCALAR_ARG1(T2) \
    /* end of macro */
#define SCALAR_ARG3(T1, T2, T3) \
    SCALAR_ARG1(T1) \
    SCALAR_ARG1(T2) \
    SCALAR_ARG1(T3) \
    /* end of macro */

#define RANGE_ARG(TYPE) \
    case BRAY_UsdResolver<TYPE>::type:  \
	args.append(name); \
	args.append(theOpenParen.asHolder()); \
	if (!is_array) { \
	    UT_ASSERT_P(val.IsHolding<TYPE>()); \
	    vexRangeArg(args, val.UncheckedGet<TYPE>()); \
	} else { \
	    UT_ASSERT_P(val.IsHolding<VtArray<TYPE>>()); \
	    const VtArray<TYPE> &arr = val.UncheckedGet< VtArray<TYPE> >(); \
	    for (VtArray<TYPE>::const_iterator it = arr.cbegin(), \
		end = arr.cend(); it != end; ++it) \
		    vexRangeArg(args, *it); \
	} \
	args.append(theCloseParen.asHolder()); \
	return true; \
	/* end macro */

#define VECTOR_ARG1(TYPE, METHOD, SIZE) \
    case BRAY_UsdResolver<TYPE>::type: \
	args.append(name); \
	args.append(theOpenParen.asHolder()); \
	if (!is_array) { \
	    UT_ASSERT_P(val.IsHolding<TYPE>()); \
	    vexVectorArg(args, val.UncheckedGet<TYPE>().METHOD(), SIZE); \
	} else { \
	    UT_ASSERT_P(val.IsHolding<VtArray<TYPE>>()); \
	    const VtArray<TYPE> &arr = val.UncheckedGet< VtArray<TYPE> >(); \
	    for (VtArray<TYPE>::const_iterator it = arr.cbegin(), \
		end = arr.cend(); it != end; ++it) \
		    vexVectorArg(args, it->METHOD(), SIZE); \
	} \
	args.append(theCloseParen.asHolder()); \
	return true; \
	/* end of macro */

#define VECTOR_ARG2(T1, T2, METHOD, SIZE) \
    VECTOR_ARG1(T1, METHOD, SIZE) \
    VECTOR_ARG1(T2, METHOD, SIZE) \
    /* end of macro */
#define STRING_ARG(TYPE, METHOD) \
    case BRAY_UsdResolver<TYPE>::type: \
	args.append(name); \
	if (!is_array) { \
	    UT_ASSERT_P(val.IsHolding<TYPE>()); \
	    args.append(val.UncheckedGet<TYPE>()METHOD); \
	} else { \
	    UT_ASSERT_P(val.IsHolding<VtArray<TYPE>>()); \
	    const VtArray<TYPE> &arr = val.UncheckedGet< VtArray<TYPE> >(); \
	    args.append(theOpenParen.asHolder()); \
	    for (VtArray<TYPE>::const_iterator it = arr.cbegin(), \
		end = arr.cend(); it != end; ++it) \
		    args.append((*it)METHOD); \
	    args.append(theCloseParen.asHolder()); \
	} \
	return true; \
    /* end of macro */

    switch (t)
    {
	SCALAR_ARG2(fpreal32, fpreal64);
	SCALAR_ARG2(int32, int64);
	SCALAR_ARG1(bool)
	VECTOR_ARG2(GfVec2f, GfVec2d, data, 2);
	VECTOR_ARG2(GfVec3f, GfVec3d, data, 3);
	VECTOR_ARG2(GfVec4f, GfVec4d, data, 4);
	VECTOR_ARG2(GfMatrix2f, GfMatrix2d, GetArray, 2);
	VECTOR_ARG2(GfMatrix3f, GfMatrix3d, GetArray, 3);
	VECTOR_ARG2(GfMatrix4f, GfMatrix4d, GetArray, 4);
	RANGE_ARG(GfRange1f)
	RANGE_ARG(GfRange1d)
	STRING_ARG(std::string,);
	STRING_ARG(TfToken, .GetText());
	STRING_ARG(UT_StringHolder,);

	case BRAY_USD_SDFASSETPATH:
	    args.append(name);
	    if (!is_array)
	    {
		UT_ASSERT_P(val.IsHolding<SdfAssetPath>());
		SdfAssetPath p = val.UncheckedGet<SdfAssetPath>();
		args.append(UT_StringHolder(resolvePath(p)));
	    }
	    else
	    {
		UT_ASSERT_P(val.IsHolding<VtArray<SdfAssetPath>>());
		args.append(theOpenParen.asHolder());
		const VtArray<SdfAssetPath> &arr =
		    val.UncheckedGet<VtArray<SdfAssetPath>>();
		for (auto it = arr.cbegin(), end = arr.cend(); it != end; ++it)
		    args.append(resolvePath(*it));
		args.append(theCloseParen.asHolder());
	    }
	    return true;
	default:
	    break;
    }
#undef STRING_ARG
#undef SCALAR_ARG1
#undef VECTOR_ARG_T
#undef VECTOR_ARG2
    if (!val.IsEmpty())
    {
	//UTdebugFormat("Unhandled Type: {}", val.GetTypeName());
	UT_ASSERT(0 && "Unhandled data type");
    }
    return false;
}

namespace
{
    class sumTask
    {
    public:
	sumTask(const GT_DataArray &array)
	    : myArray(array)
	    , mySize(0)
	{
	}
	sumTask(const sumTask &task, UT_Split)
	    : myArray(task.myArray)
	    , mySize(0)
	{
	}
	GT_Size	size() const { return mySize; }
	void	join(const sumTask &task)
	{
	    mySize += task.mySize;
	}
	void	operator()(const UT_BlockedRange<GT_Size> &r)
	{
	    for (GT_Size i = r.begin(), n = r.end(); i < n; ++i)
		mySize += myArray.getI32(i);	// TODO: Possibly I64?
	}
    private:
	const GT_DataArray	&myArray;
	GT_Size			 mySize;
    };
}

GT_Size
BRAY_HdUtil::sumCounts(const GT_DataArrayHandle &counts)
{
    GT_Size	n = counts->entries();
    if (!n)
	return 0;

    if (dynamic_cast<const GT_IntConstant *>(counts.get()))
    {
	// This is easy
	return n * counts->getI64(0);
    }

    sumTask	task(*counts);
    UTparallelReduceLightItems(UT_BlockedRange<GT_Size>(0, n), task);
    return task.size();
}

template <typename A_TYPE> GT_DataArrayHandle
BRAY_HdUtil::gtArray(const A_TYPE &usd, GT_Type tinfo)
{
    return GT_DataArrayHandle(new GusdGT_VtArray<typename A_TYPE::value_type>(
		usd, tinfo));
}

template <typename A_TYPE> GT_DataArrayHandle
BRAY_HdUtil::gtArrayFromScalar(const A_TYPE &usd, GT_Type tinfo)
{
    return GT_DataArrayHandle(new GT_DAConstantValue<A_TYPE>(
                1, usd, 1, tinfo));
}

template <typename A_TYPE> GT_DataArrayHandle
BRAY_HdUtil::gtArrayFromScalarClass(const A_TYPE &usd, GT_Type tinfo)
{
    using UT_TYPE = typename GusdUT_Gf::TypeEquivalence<A_TYPE>::UtType;
    UT_TYPE utvalue;

    GusdUT_Gf::Convert(usd, utvalue);

    return GT_DataArrayHandle(
        new GT_DAConstantValue<typename UT_TYPE::value_type>(
        1, utvalue.data(), UT_TYPE::tuple_size, tinfo));
}

GT_DataArrayHandle
BRAY_HdUtil::convertAttribute(const VtValue &val, const TfToken &token)
{
    if (val.IsEmpty())
	return GT_DataArrayHandle();

    // TODO: Surely there must be a better way to do this!
    BRAY_USD_TYPE	t = valueType(val);
    bool		is_array = false;
    if (t == BRAY_USD_INVALID)
    {
	is_array = true;
	t = arrayType(val);
    }
    if (t == BRAY_USD_INVALID)
    {
	UTdebugFormat("Unhandled type {} for {}", val.GetTypeName(), token);
	return GT_DataArrayHandle();
    }
#define HANDLE_TYPE(TYPE) \
    case BRAY_UsdResolver<TYPE>::type: \
	if (is_array) { \
	    UT_ASSERT_P(val.IsHolding<VtArray<TYPE>>()); \
	    return gtArray(val.UncheckedGet<VtArray<TYPE>>(), typeHint(token)); \
	} \
	UT_ASSERT_P(val.IsHolding<TYPE>()); \
        return gtArrayFromScalar(val.UncheckedGet<TYPE>(), typeHint(token)); \
    /* end macro */

#define HANDLE_CLASS_TYPE(TYPE, tuple_size) \
    case BRAY_UsdResolver<TYPE>::type: \
	if (is_array) { \
	    UT_ASSERT_P(val.IsHolding<VtArray<TYPE>>()); \
	    return gtArray(val.UncheckedGet<VtArray<TYPE>>(), typeHint(token)); \
	} \
	UT_ASSERT_P(val.IsHolding<TYPE>()); \
        return gtArrayFromScalarClass(val.UncheckedGet<TYPE>(),typeHint(token)); \
    /* end macro */

    switch (t)
    {
	HANDLE_TYPE(int32)
	HANDLE_TYPE(int64)
	HANDLE_TYPE(fpreal32)
	HANDLE_TYPE(fpreal64)
	HANDLE_TYPE(fpreal16)

	HANDLE_CLASS_TYPE(GfVec3f, 3)
	HANDLE_CLASS_TYPE(GfVec4f, 4)
	HANDLE_CLASS_TYPE(GfVec2f, 2)
	HANDLE_CLASS_TYPE(GfQuatf, 4)
	HANDLE_CLASS_TYPE(GfMatrix3f, 9)
	HANDLE_CLASS_TYPE(GfMatrix4f, 16)

	HANDLE_CLASS_TYPE(GfVec3d, 3)
	HANDLE_CLASS_TYPE(GfVec4d, 4)
	HANDLE_CLASS_TYPE(GfVec2d, 2)
	HANDLE_CLASS_TYPE(GfQuatd, 4)
	HANDLE_CLASS_TYPE(GfMatrix3d, 9)
	HANDLE_CLASS_TYPE(GfMatrix4d, 16)

	HANDLE_CLASS_TYPE(GfVec3h, 3)
	HANDLE_CLASS_TYPE(GfVec4h, 4)
	HANDLE_CLASS_TYPE(GfVec2h, 2)
	HANDLE_CLASS_TYPE(GfQuath, 4)

	case BRAY_USD_STRING:
	    if (!is_array)
	    {
		UT_ASSERT_P(val.IsHolding<std::string>());
		GT_DAIndexedString	*arr = new GT_DAIndexedString(1);
		arr->setString(0, 0, UT_StringHolder(val.Get<std::string>()));
		return GT_DataArrayHandle(arr);
	    }
		UT_ASSERT_P(val.IsHolding<VtArray<std::string>>());
	    return GT_DataArrayHandle(new GusdGT_VtStringArray<std::string>(
		    val.Get<VtArray<std::string>>()));
	    break;
	default:
	    UTdebugFormat("Unhandled type: {}", val.GetTypeName());
	    break;
    }
#undef HANDLE_CLASS_TYPE
#undef HANDLE_TYPE

    return GT_DataArrayHandle();
}

template <typename M_TYPE>
BRAY::SpacePtr
BRAY_HdUtil::makeSpace(const M_TYPE *m, int seg_count)
{
    UT_StackBuffer<UT_Matrix4D>	x(seg_count);
    for (int i = 0; i < seg_count; ++i)
    {
	auto data = m[i].GetArray();
	x[i] = UT_Matrix4D(
		data[ 0], data[ 1], data[ 2], data[ 3],
		data[ 4], data[ 5], data[ 6], data[ 7],
		data[ 8], data[ 9], data[10], data[11],
		data[12], data[13], data[14], data[15]
	    );
    }
    return BRAY::SpacePtr(x, seg_count);
}

template <typename M_TYPE>
BRAY::SpacePtr
BRAY_HdUtil::makeSpace(const M_TYPE *const*m, int seg_count)
{
    UT_StackBuffer<UT_Matrix4D>	x(seg_count);
    for (int i = 0; i < seg_count; ++i)
    {
	auto data = m[i]->GetArray();
	x[i] = UT_Matrix4D(
		data[ 0], data[ 1], data[ 2], data[ 3],
		data[ 4], data[ 5], data[ 6], data[ 7],
		data[ 8], data[ 9], data[10], data[11],
		data[12], data[13], data[14], data[15]
	    );
    }
    return BRAY::SpacePtr(x, seg_count);
}

template <typename L_TYPE> void
BRAY_HdUtil::makeSpaceList(UT_Array<BRAY::SpacePtr> &xforms, const L_TYPE &list)
{
    xforms.setSize(0);
    xforms.setCapacityIfNeeded(list.size());
    for (exint i = 0, n = list.size(); i < n; ++i)
	xforms.append(makeSpace(list[i]));
}

template <typename L_TYPE> void
BRAY_HdUtil::makeSpaceList(UT_Array<BRAY::SpacePtr> &xforms,
	const L_TYPE *list, int nsegs)
{
    using M_TYPE = typename L_TYPE::value_type;
    UT_StackBuffer<const M_TYPE *>	mptr(nsegs);

    xforms.setSize(0);
    xforms.setCapacityIfNeeded(list[0].size());
    for (exint i = 0, n = list[0].size(); i < n; ++i)
    {
	for (int seg = 0; seg < nsegs; ++seg)
	    mptr[seg] = &(list[seg][i]);
	xforms.append(makeSpace(mptr.array(), nsegs));
    }
}


UT_StringHolder
BRAY_HdUtil::usdNameToGT(const TfToken& token, const TfToken& typeId)
{
    if (token == HdTokens->points)
	return theP.asHolder();
    if (token == HdTokens->normals)
	return theN.asHolder();
    if (token == HdTokens->widths)
    {
	if (typeId == HdPrimTypeTokens->points)
	    return thePScale.asHolder();
	else if (typeId == HdPrimTypeTokens->basisCurves)
	    return theWidth.asHolder();
    }
    return UT_VarEncode::encodeVar(token.GetString());
}

const TfToken
BRAY_HdUtil::gtNameToUSD(const UT_StringHolder& name)
{
    if (name == theP.asRef())
	return HdTokens->points;
    if (name == theN.asRef())
	return HdTokens->normals;
    if (name == theWidth.asRef() || name == thePScale.asRef())
	return HdTokens->widths;
    return TfToken(name.c_str());
}

const UT_StringHolder&
BRAY_HdUtil::velocityName()
{
    static const UT_StringLit theName("velocities");
    return theName.asHolder();
}

const UT_StringHolder&
BRAY_HdUtil::accelName()
{
    static const UT_StringLit theAccelName("accelerations");
    return theAccelName.asHolder();
}

#if 0
GT_AttributeListHandle
BRAY_HdUtil::makeProperties(HdSceneDelegate &sd,
	const BRAY_HdParam &rparm,
	const SdfPath &id,
	const HdInterpolation *interp,
	int ninterp)
{
    int			nattribs = 0;
    for (int ii = 0; ii < ninterp; ++ii)
    {
	const auto &descs = sd.GetPrimvarDescriptors(id, interp[ii]);
	nattribs += descs.size();
    }
    if (!nattribs)
	return GT_AttributeListHandle();

    UT_Array<GT_DataArrayHandle>	attribs(nattribs);
    GT_AttributeMapHandle		map(new GT_AttributeMap());
    float				tm;
    rparm.fillShutterTimes(&tm, 1);

    for (int ii = 0; ii < ninterp; ++ii)
    {
	const auto	&descs = sd.GetPrimvarDescriptors(id, interp[ii]);
	for (exint i = 0, n = descs.size(); i < n; ++i)
	{
	    const char	*name = getPrimvarProperty(descs[i].name.GetText());
	    if (!name)
		continue;
	    auto prop = BRAYproperty(name, BRAY_OBJECT_PROPERTY);
	    if (prop.first != BRAY_OBJECT_PROPERTY)
		continue;
	    UT_SmallArray<GT_DataArrayHandle>	data;
	    if (!dformBlur(&sd, data, id, descs[i].name, &tm, 1))
		continue;

	    map->add(descs[i].name.GetText(), true);
	    attribs.append(data[0]);
	}
    }
    GT_AttributeListHandle	alist;
    if (map->entries())
    {
	alist.reset(new GT_AttributeList(map, 1));
	for (int i = 0, n = map->entries(); i < n; ++i)
	    alist->set(i, attribs[i]);
    }
    return alist;
}
#endif

namespace
{
    static bool
    matchMotionSamples(const SdfPath &id,
	    UT_Array<GT_DataArrayHandle> &data,
	    GT_Size expected_size)
    {
	// Check that all the arrays have the correct size.  If they don't we
	// copy over the "closest" array that does have the correct size.
	int	correct = data.size();
	bool	prev_ok = false;

	// First, we do a pass through the data, copying arrays that have the
	// correct size to subsequent entries.
	for (int ts = 0, n = data.size(); ts < n; ++ts)
	{
	    if (data[ts]->entries() == expected_size)
	    {
		correct = SYSmin(ts, correct);
		prev_ok = true;
	    }
	    else
	    {
		UT_ErrorLog::warningOnce(
			"{}: bad motion sample size - is topology changing?",
			id);
		if (prev_ok)
		{
		    data[ts] = data[ts-1];
		    // Leave prev_ok set to true
		}
	    }
	}
	// We need to have at least one array with correct samples
	// But we only have to worry about items at the beginning of the array,
	// since the correct size is copied to the items after it's found.
	UT_ASSERT(correct >= 0 && correct < data.size());
	if (correct == data.size())
	    return false;
	if (correct > 0 && correct < data.size())
	{
	    for (int ts = 0, n = data.size(); ts < n; ++ts)
	    {
		// We've got to a place where the rest of the entries
		// will be ok.
		if (data[ts]->entries() == expected_size)
		    break;
		data[ts] = data[correct];
	    }
	}
	return true;
    }
}

template <typename T>
static int
matchAttribDict(const T &desc,
	const TfToken &primType,
	const GT_AttributeListHandle &gt,
	const UT_Set<TfToken> *skip,
	bool skip_namespace,
	bool &new_primvar)
{
    int		nfound = 0;
    for (auto &&d : desc)
    {
	if (skip && skip->contains(d.name))
	    continue;
	if (skip_namespace && hasNamespace(d.name))
	    continue;
	if (gt && gt->getIndex(BRAY_HdUtil::usdNameToGT(d.name, primType)) >= 0)
	    nfound++;
	else
	{
	    //UTdebugFormat("New primvar: {}", d.name);
	    new_primvar = true;
	    break;
	}
    }
    return nfound;
}

bool
BRAY_HdUtil::matchAttributes(HdSceneDelegate *sd,
	const SdfPath &id,
	const TfToken &primType,
	const HdInterpolation *interp,
	int ninterp,
	const GT_AttributeListHandle &gt,
	const UT_Set<TfToken> *skip,
	bool skip_namespace)
{
    int		nfound = 0;
    int		ngt = gt ? gt->entries() : 0;
    bool	new_primvar = false;
    for (int i = 0; i < ninterp; ++i)
    {
	nfound += matchAttribDict(sd->GetPrimvarDescriptors(id, interp[i]),
		primType, gt, skip, skip_namespace, new_primvar);
	nfound += matchAttribDict(sd->GetExtComputationPrimvarDescriptors(id, interp[i]),
		primType, gt, skip, skip_namespace, new_primvar);
    }
    if (gt && skip)
    {
	for (auto &&name : *skip)
	{
	    if (gt->getIndex(usdNameToGT(name, primType)) >= 0)
		nfound++;
	}
    }
    return !new_primvar && nfound == ngt;
}

GT_AttributeListHandle
BRAY_HdUtil::makeAttributes(HdSceneDelegate *sd,
	const BRAY_HdParam &rparm,
	const SdfPath& id,
	const TfToken& typeId,
	GT_Size expected_size,
	const BRAY::OptionSet &props,
	const HdInterpolation *interp,
	int ninterp,
	const UT_Set<TfToken> *skip,
	bool skip_namespace)
{
    UT_ASSERT(props);
    int	nattribs = 0;
    for (int ii = 0; ii < ninterp; ++ii)
    {
	const auto	&descs = sd->GetPrimvarDescriptors(id, interp[ii]);
	const auto	&cdescs = sd->GetExtComputationPrimvarDescriptors(id, interp[ii]);
	nattribs += descs.size() + cdescs.size();
    }
    if (!nattribs)
	return GT_AttributeListHandle();

    int						nsegs = 1;
    UT_Array<UT_Array<GT_DataArrayHandle>>	attribs(nattribs);
    GT_AttributeMapHandle			map(new GT_AttributeMap());

    // compute the number of maximum deformation blur segments that we can compute
    bool mblur = rparm.instantShutter() ? false : *props.bval(BRAY_OBJ_MOTION_BLUR);
    int	 vblur = *props.ival(BRAY_OBJ_GEO_VELBLUR);

    // if velocity blur is enabled, we disable deformation blur
    if (mblur && !vblur)
	nsegs = *props.ival(BRAY_OBJ_GEO_SAMPLES);

    int				maxsegs = 1;
    UT_StackBuffer<float>	tm(nsegs);
    rparm.fillShutterTimes(tm, nsegs);	// Desired times

    for (int ii = 0; ii < ninterp; ++ii)
    {
	const auto	&descs = sd->GetPrimvarDescriptors(id, interp[ii]);
	const auto	&cdescs = sd->GetExtComputationPrimvarDescriptors(id, interp[ii]);

	// try to convert all available primvars to attributes
	for (exint i = 0, n = descs.size(); i < n; ++i)
	{
	    if (skip && skip->contains(descs[i].name))
		continue;
	    if (skip_namespace && hasNamespace(descs[i].name))
		continue;
	    UT_SmallArray<GT_DataArrayHandle>	data;
	    if (!dformBlur(sd, data, id, descs[i].name, tm.array(), nsegs))
		continue;
	    if (data.size() > 1 && expected_size >= 0)
	    {
		// Make sure all arrays have the proper counts
		if (!matchMotionSamples(id, data, expected_size))
		    continue;
	    }
	    else
	    {
		UT_ASSERT(expected_size < 0 
			|| expected_size == data[0]->entries());
		if (expected_size >= 0 &&
			expected_size != data[0]->entries())
		{
		    UT_ErrorLog::warningOnce(
			"{}: bad primvar sample size for {}",
			id, descs[i].name);
		    continue;
		}
	    }

	    map->add(usdNameToGT(descs[i].name, typeId), true);
	    maxsegs = SYSmax(maxsegs, int(data.size()));
	    attribs.append(data);
	}
	// Try to convert the computed primvars to attributes
	for (auto &&v : HdExtComputationUtils::GetComputedPrimvarValues(cdescs, sd))
	{
	    const auto		&name = v.first;
	    if (skip && skip->contains(name))
		continue;
	    if (skip_namespace && hasNamespace(name))
		continue;
	    GT_DataArrayHandle	 gv = convertAttribute(v.second, name);
	    if (!gv)
		continue;

	    // TODO: Motion blur
	    UT_SmallArray<GT_DataArrayHandle>	data;
	    data.append(gv);
	    map->add(usdNameToGT(name, typeId), false);
	    attribs.append(data);
	}
    }

    // construct an attribute map with all our converted attributes
    GT_AttributeListHandle	alist;
    if (map->entries())
    {
	alist.reset(new GT_AttributeList(map, maxsegs));
	for (int i = 0, n = map->entries(); i < n; ++i)
	{
	    int currsegs = attribs[i].size();
	    if (currsegs == 1)
		alist->setAllSegments(i, attribs[i][0]);
	    else
	    {
		UT_ASSERT(currsegs == maxsegs);
		for (int seg = 0; seg < currsegs; seg++)
		{
		    alist->set(i, attribs[i][seg], seg);
		}
	    }
	}
    }

    return alist;
}

void
BRAY_HdUtil::updateVisibility(HdSceneDelegate *sd,
	const SdfPath &id,
	BRAY::OptionSet &props,
	bool is_visible,
	const TfToken &render_tag)
{
    BRAY_RayVisibility mask = BRAY_RAY_ALL;
    if (!is_visible)
	mask = BRAY_RAY_NONE;
    else
    {
	// The properties should be updated with the current object's
	// properties.  However, we need to turn off bits of the mask based on
	// the render tag.
	switch (HUSD_HydraPrim::renderTag(render_tag))
	{
	    case HUSD_HydraPrim::TagGuide:
		mask = BRAY_RAY_GUIDE_MASK;
		break;
	    case HUSD_HydraPrim::TagProxy:
		mask = BRAY_RAY_PROXY_MASK;
		break;
	    case HUSD_HydraPrim::TagRender:
		mask = BRAY_RayVisibility(*props.ival(BRAY_OBJ_RENDER_MASK));
		break;
	    case HUSD_HydraPrim::TagDefault:
		mask = BRAY_RAY_PROXY_MASK | BRAY_RAY_GUIDE_MASK |
		    BRAY_RayVisibility(*props.ival(BRAY_OBJ_RENDER_MASK));
		break;
	    case HUSD_HydraPrim::TagInvisible:
		mask = BRAY_RAY_NONE;
		break;
	    case HUSD_HydraPrim::NumRenderTags:
		UT_ASSERT(0);
	}
    }
    props.set(BRAY_OBJ_VISIBILITY_MASK, int64(mask));
}

void
BRAY_HdUtil::dumpValue(const VtValue &val, const char *msg)
{
    #define SCALAR_DUMP(TYPE)	\
	case TYPE: UTdebugFormat("Value: {} {}", msg, \
		       val.UncheckedGet<BRAY_UsdTypeResolver<TYPE>::T>()); \
	break; \
	/* end macro */
    #define ARRAY_DUMP(TYPE) \
	case TYPE: UTdebugFormat("Value: {} {}", msg, \
		       val.UncheckedGet<VtArray<BRAY_UsdTypeResolver<TYPE>::T>>()); \
	break; \
	/* end macro */

    BRAY_USD_TYPE	t = valueType(val);
    switch (t)
    {
	SCALAR_DUMP(BRAY_USD_BOOL)
	SCALAR_DUMP(BRAY_USD_INT8)
	SCALAR_DUMP(BRAY_USD_INT16)
	SCALAR_DUMP(BRAY_USD_INT32)
	SCALAR_DUMP(BRAY_USD_INT64)
	SCALAR_DUMP(BRAY_USD_UINT8)
	SCALAR_DUMP(BRAY_USD_UINT16)
	SCALAR_DUMP(BRAY_USD_UINT32)
	SCALAR_DUMP(BRAY_USD_UINT64)
	SCALAR_DUMP(BRAY_USD_VEC2I)
	SCALAR_DUMP(BRAY_USD_VEC3I)
	SCALAR_DUMP(BRAY_USD_VEC4I)
	SCALAR_DUMP(BRAY_USD_REALH)
	SCALAR_DUMP(BRAY_USD_VEC2H)
	SCALAR_DUMP(BRAY_USD_VEC3H)
	SCALAR_DUMP(BRAY_USD_VEC4H)
	SCALAR_DUMP(BRAY_USD_QUATH)
	SCALAR_DUMP(BRAY_USD_REALF)
	SCALAR_DUMP(BRAY_USD_VEC2F)
	SCALAR_DUMP(BRAY_USD_VEC3F)
	SCALAR_DUMP(BRAY_USD_VEC4F)
	SCALAR_DUMP(BRAY_USD_QUATF)
	SCALAR_DUMP(BRAY_USD_MAT2F)
	SCALAR_DUMP(BRAY_USD_MAT3F)
	SCALAR_DUMP(BRAY_USD_MAT4F)
	SCALAR_DUMP(BRAY_USD_REALD)
	SCALAR_DUMP(BRAY_USD_VEC2D)
	SCALAR_DUMP(BRAY_USD_VEC3D)
	SCALAR_DUMP(BRAY_USD_VEC4D)
	SCALAR_DUMP(BRAY_USD_QUATD)
	SCALAR_DUMP(BRAY_USD_MAT2D)
	SCALAR_DUMP(BRAY_USD_MAT3D)
	SCALAR_DUMP(BRAY_USD_MAT4D)
	SCALAR_DUMP(BRAY_USD_TFTOKEN)
	SCALAR_DUMP(BRAY_USD_SDFPATH)
	SCALAR_DUMP(BRAY_USD_SDFASSETPATH)
	SCALAR_DUMP(BRAY_USD_STRING)
	SCALAR_DUMP(BRAY_USD_HOLDER)
	SCALAR_DUMP(BRAY_USD_RANGE1F)
	SCALAR_DUMP(BRAY_USD_RANGE1D)

	// Unhandled types
	case BRAY_USD_MAX_TYPES:
	    UTdebugFormat("{}: Unhandled type {}", msg, val.GetTypeName());
	    break;
	// Possibly an array
	case BRAY_USD_INVALID:
	    switch (arrayType(val))
	    {
		ARRAY_DUMP(BRAY_USD_BOOL)
		ARRAY_DUMP(BRAY_USD_INT32)
		ARRAY_DUMP(BRAY_USD_INT64)
		ARRAY_DUMP(BRAY_USD_REALF)
		ARRAY_DUMP(BRAY_USD_REALD)
		ARRAY_DUMP(BRAY_USD_TFTOKEN)
		ARRAY_DUMP(BRAY_USD_SDFPATH)
		ARRAY_DUMP(BRAY_USD_SDFASSETPATH)
		ARRAY_DUMP(BRAY_USD_STRING)
		ARRAY_DUMP(BRAY_USD_HOLDER)
		default:
		    UTdebugFormat("{}: Unhandled type {}", msg, val.GetTypeName());
		    break;
	    }
    }
    #undef SCALAR_DUMP
    #undef ARRAY_DUMP
}

void
BRAY_HdUtil::dumpvalue(const TfToken &token,
	const VtValue &val,
	const GT_DataArrayHandle &d)
{
    UTdebugFormat("Attribute: {}", token.GetString());
    UTdebugFormat("  IsArrayValued: {}", val.IsArrayValued());
    UTdebugFormat("  GetArraySize: {}", val.GetArraySize());
    UTdebugFormat("  GetTypeName: {}", val.GetTypeName());
    if (d && d->entries() == 1)
	d->dumpValues(token.GetText());
}

GT_DataArrayHandle
BRAY_HdUtil::computeBlur(const GT_DataArrayHandle &Parr,
	const fpreal32 *P,
	const fpreal32 *v,
	const fpreal32 *a,
	fpreal32 amount)
{
    if (amount == 0)
	return Parr;

    exint	size = Parr->entries();
    auto	result = new GT_Real32Array(size, 3, GT_TYPE_POINT);
    fpreal32	accelFactor = 0.5f * amount * amount;
    // TODO: Use VM?
    for (exint i = 0, n = size * 3; i < n; ++i)
    {
	fpreal32 val = P[i] + v[i] * amount; // velocity blur
	if (a)
	{
	    val += a[i] * accelFactor;	// accel blur
	}
	result->data()[i] = val;
    }
    return GT_DataArrayHandle(result);
}

bool
BRAY_HdUtil::velocityBlur(UT_Array<GT_DataArrayHandle> &p,
	const GT_DataArrayHandle &Parr,	// Source positions
	const GT_DataArrayHandle &varr,	// Source velocity
	const GT_DataArrayHandle &Aarr,	// Source acceleration
	int style,
	int nseg,
	const BRAY_HdParam &rparm)
{
    UT_ASSERT(isVector3(Parr));

    if (nseg == 1 || !rparm.validShutter() || !isVector3(varr))
	return false;

    bool bAccel = (nseg > 2 && style > 1 && isVector3(Aarr));
    if (!bAccel)
	nseg = 2;	// Force segment count to 2

    p.setSize(nseg);
    GT_DataArrayHandle		 pstore, vstore, astore;
    const fpreal32		*P = Parr->getF32Array(pstore);
    const fpreal32		*v = varr->getF32Array(vstore);
    const fpreal32		*a = bAccel ? Aarr->getF32Array(astore) : nullptr;
    UT_StackBuffer<float>	 times(nseg);

    // Fills out frame times (not shutter times)
    rparm.fillFrameTimes(times, nseg);

    for (int seg = 0; seg < nseg; seg++)
    {
	p[seg] = computeBlur(Parr, P, v, a, times[seg]);
    }
    return true;
}

GT_AttributeListHandle
BRAY_HdUtil::velocityBlur(const GT_AttributeListHandle& src,
	int style,
	int nseg,
	const BRAY_HdParam &rparm)
{
    if (!src || src->getSegments() != 1 || nseg == 1 || style == 0
	    || rparm.instantShutter())
    {
	return src;
    }

    int				 pidx = src->getIndex(theP.asRef());
    const GT_DataArrayHandle	&P = src->get(pidx);
    const GT_DataArrayHandle	&v = src->get(velocityName());
    const GT_DataArrayHandle	&a = src->get(accelName());
    if (!isVector3(P) || !isVector3(v))
	return src;

    UT_SmallArray<GT_DataArrayHandle>	p;
    if (!velocityBlur(p, P, v, a, style, nseg, rparm))
	return src;
    GT_AttributeList	*alist = new GT_AttributeList(src->getMap(), p.size());
    for (int i = 0, n = alist->entries(); i < n; ++i)
    {
	if (i == pidx)
	{
	    for (int seg = 0; seg < p.size(); seg++)
		alist->set(i, p[seg], seg);
	}
	else
	{
	    alist->setAllSegments(i, src->get(i));
	}
    }
    return GT_AttributeListHandle(alist);
}

bool
BRAY_HdUtil::updateAttributes(HdSceneDelegate* sd,
	const BRAY_HdParam &rparm,
	HdDirtyBits* dirtyBits,
	const SdfPath& id,
	const GT_AttributeListHandle& src,
	GT_AttributeListHandle& dest,
	BRAY_EventType &event,
	const BRAY::OptionSet &props,
	const HdInterpolation *interp,
	int ninterp)
{
    // preliminary sanity check
    UT_ASSERT(!dest);
    UT_ASSERT(props);
    if (!src)
	return false;

    const UT_StringArray			&names = src->getNames();
    UT_Array<UT_Array<GT_DataArrayHandle>>	 values(names.size(),
							names.size());
    bool	 dirty = false;
    bool	 mblur = *props.bval(BRAY_OBJ_MOTION_BLUR);
    int		 vblur = *props.ival(BRAY_OBJ_GEO_VELBLUR);

    // get all the primvars that are dirty.
    // NOTE: output will have the 'same' number of segments if
    // a dirty attribute is found
    int				nsegs = src->getSegments();
    UT_StackBuffer<float>	tm(nsegs);

    rparm.fillShutterTimes(tm, nsegs);

    int		 pidx = -1, vidx = -1, aidx = -1;
    for (int ii = 0; ii < ninterp; ++ii)
    {
	const auto	&cdescs = sd->GetExtComputationPrimvarDescriptors(id, interp[ii]);
	auto	vstore = HdExtComputationUtils::GetComputedPrimvarValues(cdescs, sd);
	bool	is_point = interp[ii] == HdInterpolationVarying
			|| interp[ii] == HdInterpolationVertex;
	for (int i = 0, n = names.size(); i < n; ++i)
	{
	    if (values[i].size())
		continue;

	    if (is_point)
	    {
		if (names[i] == theP.asRef())
		    pidx = i;
		else if (names[i] == BRAY_HdUtil::velocityName())
		    vidx = i;
		else if (names[i] == BRAY_HdUtil::accelName())
		    aidx = i;
	    }

	    TfToken	token = gtNameToUSD(names[i].c_str());

	    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, token))
	    {
		UT_SmallArray<GT_DataArrayHandle>	  data;
		auto	&&cit = vstore.find(token);
		if (cit != vstore.end())
		{
		    // TODO: Motion blur for compute
		    data.append(convertAttribute(cit->second, token));
		    UT_ASSERT(data[0]);
		}
		else
		{
		    // Sample the primvar
		    dformBlur(sd, data, id, token, tm.array(), nsegs);
		}
		UT_ASSERT(data.size());

		values[i] = data;
		dirty = true;
		if (is_point && (i == pidx || i == vidx || i == aidx))
		    event = (event | BRAY_EVENT_ATTRIB_P);
		else
		    event = (event | BRAY_EVENT_ATTRIB);
	    }
	}
    }

    // if anything is dirty, construct the new attribute list
    if (dirty)
    {
	// Handle velocity blur explicitly
	UT_Array<GT_DataArrayHandle> p;
	if (vidx >= 0 && mblur && vblur)
	{
	    BRAY_HdUtil::velocityBlur(p,
		values[pidx][0],
		values[vidx][0],
		aidx >= 0 ? values[aidx][0] : nullptr,
		vblur,
		nsegs,
		rparm);
	}

	// compose the new attribute list
	dest.reset(new GT_AttributeList(src->getMap(), nsegs));
	for (int i = 0, n = names.size(); i < n; ++i)
	{
	    // check if we are position and we had our segments
	    // modified by velocity blur
	    if ((i == pidx) && mblur && vblur)
	    {
		// Make sure velocityBlur succeeded!?
		if (p.size() == nsegs)
		{
		    for (int seg = 0; seg < nsegs; seg++)
		    {
			dest->set(i, p[seg], seg);
		    }
		}
		else
		{
		    // velocity blur handling failed?
		    // just copy positions to all segments
		    dest->setAllSegments(i, values[pidx][0]);
		}
	    }
	    else if (values[i].size() == 0)
	    {
		// If we had a segment in the source that was not dirty
		// just copy it back into dest.
		for (int seg = 0; seg < nsegs; seg++)
		{
		    dest->set(i, src->get(names[i], seg), seg);
		}
		continue;
	    }
	    else
	    {
		// store the converted updated primvars
		if (values[i].size() == nsegs)
		{
		    for (int seg = 0; seg < nsegs; seg++)
		    {
			dest->set(i, values[i][seg], seg);
		    }
		}
		else
		{
		    // We sampled a primvar that was dirty, and
		    // has different segments than what was originally
		    // authored.
		    dest->setAllSegments(i, values[i][0]);
		}
	    }
	}
    }
    return dirty;
}

int
BRAY_HdUtil::xformSamples(const BRAY_HdParam &rparm, const BRAY::OptionSet &o)
{
    return !rparm.instantShutter() && *o.bval(BRAY_OBJ_MOTION_BLUR)
		? *o.ival(BRAY_OBJ_XFORM_SAMPLES)
		: 1;
}

void
BRAY_HdUtil::xformBlur(HdSceneDelegate *sd,
    const BRAY_HdParam &rparm,
    const SdfPath& id,
    UT_Array<GfMatrix4d> &xforms,
    const BRAY::OptionSet &props)
{
    UT_ASSERT(props);
    // compute number of transform segments to compute
    int nsegs = xformSamples(rparm, props);

    UT_StackBuffer<float>	tm(nsegs);
    rparm.fillShutterTimes(tm, nsegs);
    xformBlur(sd, xforms, id, tm.array(), nsegs);
}

void
BRAY_HdUtil::xformBlur(HdSceneDelegate *sd,
	UT_Array<GfMatrix4d> &xforms,
	const SdfPath &id,
	const float *times, int nsegs)
{
    xforms.clear();

    UT_SmallArray<GfMatrix4d>	temp;
    UT_SmallArray<float>	utm;
    temp.bumpSize(nsegs);
    utm.bumpSize(nsegs);

    int usegs = sd->SampleTransform(id, nsegs, utm.data(), temp.data());
    if (usegs > nsegs)
    {
	temp.bumpSize(usegs);
	utm.bumpSize(usegs);
	usegs = sd->SampleTransform(id, usegs, utm.data(), temp.data());
    }
    for (int i = 1; i < usegs; ++i)
    {
	if (temp[i] != temp[0])
	{
	    interpolateValues(xforms, temp.array(),
			times, nsegs, utm.array(), usegs);
	    return;
	}
    }
    // All transforms are equal
    xforms.append(temp[0]);
}

template <EvalStyle STYLE>
bool
BRAY_HdUtil::dformBlur(HdSceneDelegate *sd,
	UT_Array<GT_DataArrayHandle> &values,
	const SdfPath &id,
	const TfToken &name,
	const float *times, int nsegs)
{
    values.clear();

    primvarSamples	samples(nsegs);
    int usdsegs = samplePrimvar<STYLE>(sd, id, name, samples);
    if (!usdsegs)
	return false;
    UT_ASSERT(usdsegs <= samples.size());
    UT_StackBuffer<GT_DataArrayHandle>	gvalues(usdsegs);
    for (int i = 0; i < usdsegs; ++i)
    {
	gvalues[i] = convertAttribute(samples.values()[i], name);
	if (!gvalues[i])
	    return false;
    }
    interpolateValues(values, gvalues.array(),
	    times, nsegs, samples.times(), usdsegs);
    return values.size() > 0;
}

template <EvalStyle STYLE>
bool
BRAY_HdUtil::dformBlur(HdSceneDelegate *sd,
	UT_Array<VtValue> &values,
	const SdfPath &id,
	const TfToken &name,
	const float *times, int nsegs)
{
    values.clear();

    primvarSamples	samples(nsegs);
    int usdsegs = samplePrimvar<STYLE>(sd, id, name, samples);
    if (!usdsegs)
	return false;
    UT_ASSERT(usdsegs <= samples.size());
    interpolateValues(values, samples.values(),
	    times, nsegs, samples.times(), usdsegs);
    return values.size() > 0;
}

bool
BRAY_HdUtil::updateObjectProperties(BRAY::OptionSet &props,
	HdSceneDelegate &sd,
        const SdfPath &id)
{
    return bray_updateObjectProperties(props, sd, id);
}

bool
BRAY_HdUtil::updateObjectPrimvarProperties(BRAY::OptionSet &props,
	HdSceneDelegate &sd,
	HdDirtyBits* dirtyBits,
        const SdfPath &id)
{
    // Update object properties by iterating over primvars and looking for
    // karma properties.  This is more efficient than iterating over all the
    // karma properties looking for a primvar of that name.
    bool	 changed = false;
    const auto	&descs = sd.GetPrimvarDescriptors(id, HdInterpolationConstant);
    for (auto &&d : descs)
    {
        if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, d.name))
        {
            const char	*name = getPrimvarProperty(d.name.GetText());
            if (!name)
                continue;

            if (!strcmp(name, "object:rendervisibility"))
            {
                VtValue	value = sd.Get(id, d.name);
                setRenderVisibility(props, value);
                continue;
            }
            auto prop = BRAYproperty(name, BRAY_OBJECT_PROPERTY);
            if (prop.first != BRAY_OBJECT_PROPERTY)
            {
                UTdebugFormat("Invalid object property: {}", d.name);
                continue;
            }
            if (prop.second == BRAY_OBJ_VISIBILITY_MASK)
            {
                // Visibility mask should be computed based on
                // 'rendervisibility' primvar and not set directly.
                continue;
            }
            VtValue	value = sd.Get(id, d.name);
            changed |= bray_setOption(props, prop.second, value);
        }
    }
    return changed;
}

/// Update scene settings
bool
BRAY_HdUtil::updateSceneOptions(BRAY::ScenePtr &scene,
	const HdRenderSettingsMap &settings)
{
    bool status = bray_updateSceneOptions(scene, settings);
    lockObjectProperties(scene);
    return status;
}

bool
BRAY_HdUtil::sceneOptionNeedUpdate(BRAY::ScenePtr &scene,
	const TfToken &token, const VtValue &value)
{
    return bray_optionNeedsUpdate(scene, token, value);
}

bool
BRAY_HdUtil::updateSceneOption(BRAY::ScenePtr &scene,
	const TfToken &token, const VtValue &value)
{
    bool	status =  bray_updateSceneOption(scene, token, value);
    if (token == "karma:global:overrideobject")
	lockObjectProperties(scene);
    return status;
}

void
BRAY_HdUtil::updatePropCategories(BRAY_HdParam &rparm,
	HdSceneDelegate *delegate, HdRprim *rprim, BRAY::OptionSet &props)
{
    BRAY::ScenePtr &scene = rparm.getSceneForEdit();
    const SdfPath &id = rprim->GetId();

    VtArray<TfToken> categories;
    if (rprim->GetInstancerId().IsEmpty())
	categories = delegate->GetCategories(id);
    else
	// TODO: what is the proper way to get traceset for prototype in
	// instancers?
	categories = delegate->GetCategories(rprim->GetInstancerId());

    UT_StringHolder lightlink;
    UT_StringHolder tracesets;
    for (TfToken const& category: categories) 
    {
	// Ignore categories not found in global list of trace sets
	if (scene.isTraceset(category.GetText()))
	{
	    if (tracesets.isstring())
		tracesets += " ";
	    tracesets += category.GetText();
	}

	if (rparm.isValidLightCategory(category.GetText()))
	{
	    if (lightlink.isstring())
		lightlink += " ";
	    lightlink += category.GetText();
	}
    }

    props.set(BRAY_OBJ_TRACESETS, tracesets);
    props.set(BRAY_OBJ_LIGHT_CATEGORIES, lightlink);
}

bool
BRAY_HdUtil::setOption(BRAY::OptionSet &options, int token, const VtValue &val)
{
    return bray_setOption(options, token, val);
}

bool
BRAY_HdUtil::updateRprimId(BRAY::OptionSet &props, HdRprim *rprim)
{
    int prev_rprimid;
    props.import(BRAY_OBJ_HD_RPRIM_ID, &prev_rprimid, 1);
    int rprimid = rprim->GetPrimId();
    if (prev_rprimid != rprimid)
    {
	props.set(BRAY_OBJ_HD_RPRIM_ID, rprimid);
	return true;
    }
    return false;
}

const char *
BRAY_HdUtil::parameterPrefix()
{
    return thePrefix.c_str();
}

const std::string &
BRAY_HdUtil::resolvePath(const SdfAssetPath &p)
{
    const std::string &resolved = p.GetResolvedPath();
    if (resolved.empty())
	return p.GetAssetPath();
    return resolved;
}

bool
BRAY_HdUtil::addInput(const UT_StringHolder &primvarName,
	const VtValue &fallbackValue,
	const TfToken &vexName,
	UT_Array<BRAY::MaterialInput> &inputMap,
	UT_StringArray &args)
{
    BRAY_USD_TYPE	utype = valueType(fallbackValue);

    // TODO: VEX array types
    if (utype == BRAY_USD_INVALID)
	return false;

    BRAY::MaterialInput::Storage store;
    int tsize = materialTypeSize(utype, store);
    if (tsize < 1)
	return false;

    UT_StringHolder	vname = tokenToString(vexName);

    inputMap.emplace_back(primvarName,
	    vname,
	    store,
	    tsize,
	    false);

    appendVexArg(args, vname, fallbackValue);
    return true;
}

#define INSTANTIATE_ARRAY(TYPE) \
    template GT_DataArrayHandle BRAY_HdUtil::gtArray(const VtArray<TYPE> &, \
	    GT_Type); \
    /* end of macro */

#define INSTANTIATE_SPACE(TYPE) \
    template BRAY::SpacePtr BRAY_HdUtil::makeSpace(const TYPE *, int); \
    template BRAY::SpacePtr BRAY_HdUtil::makeSpace(const TYPE *const*, int); \
    /* end of macro */

#define INSTANTIATE_SPACE_LIST(TYPE) \
    template void BRAY_HdUtil::makeSpaceList(UT_Array<BRAY::SpacePtr> &, \
	    const TYPE &); \
    template void BRAY_HdUtil::makeSpaceList(UT_Array<BRAY::SpacePtr> &, \
	    const TYPE *, int); \
    /* end of macro */

#define INSTANTIATE_EVAL_STYLE(STYLE) \
    template bool BRAY_HdUtil::dformBlur<STYLE>(HdSceneDelegate *, \
	UT_Array<GT_DataArrayHandle> &, const SdfPath &, const TfToken &, \
	const float *, int ); \
    template bool BRAY_HdUtil::dformBlur<STYLE>(HdSceneDelegate *, \
	UT_Array<VtValue> &, const SdfPath &, const TfToken &, \
	const float *, int); \
    /* end of macro */

INSTANTIATE_ARRAY(GfVec3f)
INSTANTIATE_ARRAY(GfVec4f)
INSTANTIATE_ARRAY(GfVec2f)
INSTANTIATE_ARRAY(GfQuatf)
INSTANTIATE_ARRAY(GfMatrix3f)
INSTANTIATE_ARRAY(GfMatrix4f)
INSTANTIATE_ARRAY(fpreal32)

INSTANTIATE_ARRAY(GfVec3d)
INSTANTIATE_ARRAY(GfVec4d)
INSTANTIATE_ARRAY(GfVec2d)
INSTANTIATE_ARRAY(GfQuatd)
INSTANTIATE_ARRAY(GfMatrix3d)
INSTANTIATE_ARRAY(GfMatrix4d)
INSTANTIATE_ARRAY(fpreal64)

INSTANTIATE_ARRAY(GfVec3h)
INSTANTIATE_ARRAY(GfVec4h)
INSTANTIATE_ARRAY(GfVec2h)
INSTANTIATE_ARRAY(GfQuath)

INSTANTIATE_ARRAY(int32)
INSTANTIATE_ARRAY(int64)

INSTANTIATE_SPACE(GfMatrix4f)
INSTANTIATE_SPACE(GfMatrix4d)

INSTANTIATE_SPACE_LIST(VtMatrix4fArray)
INSTANTIATE_SPACE_LIST(VtMatrix4dArray)

INSTANTIATE_EVAL_STYLE(BRAY_HdUtil::EVAL_GENERIC)
INSTANTIATE_EVAL_STYLE(BRAY_HdUtil::EVAL_CAMERA_PARM)
INSTANTIATE_EVAL_STYLE(BRAY_HdUtil::EVAL_LIGHT_PARM)

PXR_NAMESPACE_CLOSE_SCOPE
