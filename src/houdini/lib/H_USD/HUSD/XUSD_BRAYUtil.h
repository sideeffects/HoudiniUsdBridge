/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	XUSD_BRAYUtil.h (HUSD Library, C++)
 *
 * COMMENTS:
 */

#ifndef __XUSD_BRAYUtil__
#define __XUSD_BRAYUtil__

/// @file This file provides utilities for the BRAY karma hydra plugin.
/// The file isn't compiled or used by any files in the HUSD library and must
/// be a header-only include.
#include <BRAY/BRAY_Interface.h>
#include <UT/UT_Debug.h>
#include <UT/UT_WorkBuffer.h>

#include <pxr/pxr.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/attribute.h>
#include "XUSD_Format.h"

PXR_NAMESPACE_OPEN_SCOPE
namespace HUSD_BRAY_NS
{
    static constexpr UT_StringLit	thePrimvarPrefix("primvars:karma:");
    static constexpr UT_StringLit	thePrefix("karma:");

    static inline const char *
    parameterPrefix()
    {
	return thePrefix.c_str();
    }

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
	return UT_StringHolder(token.GetText());
    }
    static inline UT_StringHolder
    tokenToString(const std::string &token)
    {
	return UT_StringHolder(token);
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
	UTdebugFormat("Type[{}]: {}", token, val.GetType().GetTypeName());
	UT_ASSERT(0 && "Value holding wrong type for option");
	return false;
    }

    static inline bool
    setString(BRAY::OptionSet &opt, int token, const VtValue &val)
    {
	UT_ASSERT(!val.IsEmpty());
	if (val.IsHolding<TfToken>())
	{
	    opt.set(token, tokenToString(val.UncheckedGet<TfToken>()));
	    //UTdebugFormat("Set {} to {}", myToken, val.UncheckedGet<TfToken>());
	    return true;
	}
	if (val.IsHolding<std::string>())
	{
	    opt.set(token, tokenToString(val.UncheckedGet<std::string>()));
	    //UTdebugFormat("Set {} to {}", myToken, val.Get<std::string>());
	    return true;
	}
	if (val.IsHolding<UT_StringHolder>())
	{
	    opt.set(token, val.UncheckedGet<UT_StringHolder>());
	    //UTdebugFormat("Set {} to {}", myToken, val.Get<std::string>());
	    return true;
	}
	UTdebugFormat("Type[{}]: {}", token, val.GetType().GetTypeName());
	UT_ASSERT(0 && "Value not holding string option");
	return false;
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
    setOption(BRAY::OptionSet &options, int token, const VtValue &val)
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
					    parameterPrefix());
		if (UTisstring(name))
		    value = getValue(options, name, settings);
	    }
	    if (!value.IsEmpty())
		changed |= setOption(options, i, value);
	}
	return changed;
    }

    static inline bool
    updateSceneOptions(BRAY::ScenePtr &scene,
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
    updateObjectProperties(BRAY::OptionSet &props,
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
		changed |= setOption(props, i, value);
	}
	return changed;
    }

    template <typename T>
    static inline bool
    vectorEqual(BRAY::OptionSet &options, int token, const T &val)
    {
	return options.isEqual(token, val.data(), T::dimension);
    }

    static inline bool
    optionNeedsUpdate(const BRAY::ScenePtr &scene,
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

	if (val.IsHolding<bool>())
	    return !options.isEqual(token, val.UncheckedGet<bool>());
	if (val.IsHolding<int32>())
	    return !options.isEqual(token, val.UncheckedGet<int32>());
	if (val.IsHolding<int64>())
	    return !options.isEqual(token, val.UncheckedGet<int64>());
	if (val.IsHolding<fpreal32>())
	    return !options.isEqual(token, val.UncheckedGet<fpreal32>());
	if (val.IsHolding<fpreal64>())
	    return !options.isEqual(token, val.UncheckedGet<fpreal64>());
	if (val.IsHolding<GfVec2i>())
	    return !vectorEqual(options, token, val.UncheckedGet<GfVec2i>());
	if (val.IsHolding<GfVec3i>())
	    return !vectorEqual(options, token, val.UncheckedGet<GfVec3i>());
	if (val.IsHolding<GfVec4i>())
	    return !vectorEqual(options, token, val.UncheckedGet<GfVec4i>());
	if (val.IsHolding<GfVec2f>())
	    return !vectorEqual(options, token, val.UncheckedGet<GfVec2f>());
	if (val.IsHolding<GfVec3f>())
	    return !vectorEqual(options, token, val.UncheckedGet<GfVec3f>());
	if (val.IsHolding<GfVec4f>())
	    return !vectorEqual(options, token, val.UncheckedGet<GfVec4f>());
	if (val.IsHolding<TfToken>())
	{
	    return !options.isEqual(token,
		    tokenToString(val.UncheckedGet<TfToken>()));
	}
	if (val.IsHolding<std::string>())
	{
	    return !options.isEqual(token,
		    tokenToString(val.UncheckedGet<std::string>()));
	}
	if (val.IsHolding<UT_StringHolder>())
	{
	    return !options.isEqual(token, val.UncheckedGet<UT_StringHolder>());
	}

	UTdebugFormat("Unhandled type: {}", val.GetTypeName());
	return false;
    }

    template <typename T>
    static inline bool
    setVector(BRAY::OptionSet &options, int token, const T &val)
    {
	return options.set(token, val.data(), T::dimension);
    }

    static inline bool
    updateSceneOption(BRAY::ScenePtr &scene,
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

	if (val.IsHolding<bool>())
	    return options.set(token, val.UncheckedGet<bool>());
	if (val.IsHolding<int32>())
	    return options.set(token, val.UncheckedGet<int32>());
	if (val.IsHolding<int64>())
	    return options.set(token, val.UncheckedGet<int64>());
	if (val.IsHolding<fpreal32>())
	    return options.set(token, val.UncheckedGet<fpreal32>());
	if (val.IsHolding<fpreal64>())
	    return options.set(token, val.UncheckedGet<fpreal64>());
	if (val.IsHolding<GfVec2i>())
	    return setVector(options, token, val.UncheckedGet<GfVec2i>());
	if (val.IsHolding<GfVec3i>())
	    return setVector(options, token, val.UncheckedGet<GfVec3i>());
	if (val.IsHolding<GfVec4i>())
	    return setVector(options, token, val.UncheckedGet<GfVec4i>());
	if (val.IsHolding<GfVec2f>())
	    return setVector(options, token, val.UncheckedGet<GfVec2f>());
	if (val.IsHolding<GfVec3f>())
	    return setVector(options, token, val.UncheckedGet<GfVec3f>());
	if (val.IsHolding<GfVec4f>())
	    return setVector(options, token, val.UncheckedGet<GfVec4f>());
	if (val.IsHolding<TfToken>())
	{
	    return options.set(token,
		    tokenToString(val.UncheckedGet<TfToken>()));
	}
	if (val.IsHolding<std::string>())
	{
	    return options.set(token,
		    tokenToString(val.UncheckedGet<std::string>()));
	}
	if (val.IsHolding<UT_StringHolder>())
	{
	    return options.set(token, val.UncheckedGet<UT_StringHolder>());
	}

	UTdebugFormat("Unhandled type: {}", val.GetTypeName());
	return false;
    }
} // End namespace HUSD_BRAY_NS
PXR_NAMESPACE_CLOSE_SCOPE

#endif

