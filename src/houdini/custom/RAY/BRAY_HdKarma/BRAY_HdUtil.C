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
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <SYS/SYS_Math.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_FSATable.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_TagManager.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_WorkBuffer.h>
#include <GT/GT_DAConstantValue.h>
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
	if (val.IsHolding<GfVec2d>())
	    return !vectorEqual(options, token, val.UncheckedGet<GfVec2d>());
	if (val.IsHolding<GfVec3d>())
	    return !vectorEqual(options, token, val.UncheckedGet<GfVec3d>());
	if (val.IsHolding<GfVec4d>())
	    return !vectorEqual(options, token, val.UncheckedGet<GfVec4d>());
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
	if (val.IsHolding<GfVec2d>())
	    return setVector(options, token, val.UncheckedGet<GfVec2d>());
	if (val.IsHolding<GfVec3d>())
	    return setVector(options, token, val.UncheckedGet<GfVec3d>());
	if (val.IsHolding<GfVec4d>())
	    return setVector(options, token, val.UncheckedGet<GfVec4d>());
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
	return strchr(name.GetText(), ':') != nullptr;
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
	#define INTERP(TYPE) \
	    if (a.IsHolding<TYPE>()) \
		return VtValue(SYSlerp(a.UncheckedGet<TYPE>(), \
					b.UncheckedGet<TYPE>(), t)); \
	    /* end of macro */
	#define CINTERP(TYPE)	\
	    if (a.IsHolding<TYPE>()) \
		return VtValue(t < .5 ? a.UncheckedGet<TYPE>() \
				: b.UncheckedGet<TYPE>()); \
	    /* end macro */
	// Blended interpolation
	INTERP(fpreal64)
	INTERP(fpreal32)
	INTERP(fpreal16)
	// Conditional interpolation
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
	#undef INTERP
	#undef CINTERP
	UT_ASSERT(0 && "Unhandled interpolation type");
	return a;
    }

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
		case BRAY_HdUtil::EVAL_MATERIAL_PARM:
		    samples.values()[0] = sd->GetMaterialParamValue(id, name);
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

#define HANDLE_SCALAR(NAME, FTYPE, DTYPE) \
    if (val.IsHolding<FTYPE>()) { \
	vexPrintScalar(buf, val.UncheckedGet<FTYPE>()); \
	return NAME; \
    } \
    if (val.IsHolding<DTYPE>()) { \
	vexPrintScalar(buf, val.UncheckedGet<DTYPE>()); \
	return NAME; \
    } \
    /* end of macro */
#define HANDLE_STRING(TYPE) \
    if (val.IsHolding<TYPE>()) { \
	vexPrintQuoted(buf, val.UncheckedGet<TYPE>()); \
	return "string"; \
    } \
    /* end of macro */
#define HANDLE_VECTOR(NAME, TYPE, SIZE) \
    if (val.IsHolding<TYPE##f>()) { \
	vexPrintVector(buf, val.UncheckedGet<TYPE##f>().data(), SIZE); \
	return NAME; \
    } \
    if (val.IsHolding<TYPE##d>()) { \
	vexPrintVector(buf, val.UncheckedGet<TYPE##d>().data(), SIZE); \
	return NAME; \
    } \
    /* end of macro */
#define HANDLE_MATRIX(NAME, TYPE, SIZE) \
    if (val.IsHolding<TYPE##f>()) { \
	vexPrintMatrix(buf, val.UncheckedGet<TYPE##f>().GetArray(), SIZE); \
	return NAME; \
    } \
    if (val.IsHolding<TYPE##d>()) { \
	vexPrintMatrix(buf, val.UncheckedGet<TYPE##d>().GetArray(), SIZE); \
	return NAME; \
    } \
    /* end of macro */

    HANDLE_SCALAR("float", fpreal32, fpreal64);
    HANDLE_SCALAR("int", int32, uint32);
    HANDLE_SCALAR("int64", int64, uint64);
    HANDLE_SCALAR("int8", int8, uint8);
    HANDLE_SCALAR("int16", int16, uint16);
    HANDLE_SCALAR("bool", bool, bool);
    HANDLE_STRING(std::string);
    HANDLE_STRING(TfToken);
    HANDLE_STRING(UT_StringHolder)

    HANDLE_VECTOR("vector2", GfVec2, 2);
    HANDLE_VECTOR("vector",  GfVec3, 3);
    HANDLE_VECTOR("vector4", GfVec4, 4);
    HANDLE_MATRIX("matrix2", GfMatrix2, 2);
    HANDLE_MATRIX("matrix3", GfMatrix3, 3);
    HANDLE_MATRIX("matrix",  GfMatrix4, 4);
#undef HANDLE_MATRIX
#undef HANDLE_STRING
#undef HANDLE_SCALAR
#undef HANDLE_VECTOR

    if (val.IsHolding<SdfAssetPath>())
    {
	SdfAssetPath p = val.UncheckedGet<SdfAssetPath>();
	const std::string &resolved = p.GetResolvedPath();
#if defined(USE_HOUDINI_PATH)
	if (!resolved.length())
	{
	    // If the asset path isn't resolved by USD, let Houdini try
	    vexPrintQuoted(buf, p.GetAssetPath());
	    return "string";
	}
#endif
	vexPrintQuoted(buf, resolved);
	return "string";
    }
    if (!val.IsEmpty())
    {
	//UTdebugFormat("Unhandled Type: {}", val.GetTypeName());
	UT_ASSERT(0 && "Unhandled data type");
    }
    return nullptr;
}

bool
BRAY_HdUtil::appendVexArg(UT_StringArray &args,
	const UT_StringHolder &name,
	const VtValue &val)
{
    UT_WorkBuffer	wbuf;
#define SCALAR_ARG(FTYPE) \
    if (val.IsHolding<FTYPE>()) { \
	args.append(name); \
	vexPrintScalar(wbuf, val.UncheckedGet<FTYPE>()); \
	args.append(wbuf); \
	return true; \
    } \
    if (val.IsHolding< VtArray<FTYPE> >()) { \
	args.append(name); \
	const VtArray<FTYPE> &arr = val.UncheckedGet< VtArray<FTYPE> >(); \
	args.append(theOpenParen.asHolder()); \
	for (VtArray<FTYPE>::const_iterator it = arr.cbegin(), \
	    end = arr.cend(); it != end; ++it) \
	{ \
	    wbuf.clear(); \
	    vexPrintScalar(wbuf, *it); \
	    args.append(wbuf); \
	} \
	args.append(theCloseParen.asHolder()); \
	return true; \
    }
    /* end of macro */
#define VECTOR_ARG_T(TYPE, METHOD, SIZE) \
    if (val.IsHolding<TYPE>()) { \
	args.append(name); \
	args.append(theOpenParen.asHolder()); \
	vexVectorArg(args, val.UncheckedGet<TYPE>().METHOD(), SIZE); \
	args.append(theCloseParen.asHolder()); \
	return true; \
    } \
    if (val.IsHolding< VtArray<TYPE> >()) { \
	args.append(name); \
	const VtArray<TYPE> &arr = val.UncheckedGet< VtArray<TYPE> >(); \
	args.append(theOpenParen.asHolder()); \
	for (VtArray<TYPE>::const_iterator it = arr.cbegin(), \
	    end = arr.cend(); it != end; ++it) \
		vexVectorArg(args, it->METHOD(), SIZE); \
	args.append(theCloseParen.asHolder()); \
	return true; \
    }
    /* end of macro */
#define VECTOR_ARG(TYPE, METHOD, SIZE) \
    VECTOR_ARG_T(TYPE##f, METHOD, SIZE) \
    VECTOR_ARG_T(TYPE##d, METHOD, SIZE)
    /* end of macro */

    SCALAR_ARG(fpreal32);
    SCALAR_ARG(fpreal64);
    SCALAR_ARG(int32);
    SCALAR_ARG(int64);
    SCALAR_ARG(bool);
    VECTOR_ARG(GfVec2, data, 2);
    VECTOR_ARG(GfVec3, data, 3);
    VECTOR_ARG(GfVec4, data, 4);
    VECTOR_ARG(GfMatrix2, GetArray, 2);
    VECTOR_ARG(GfMatrix3, GetArray, 3);
    VECTOR_ARG(GfMatrix4, GetArray, 4);
#undef SCALAR_ARG
#undef VECTOR_ARG_T
#undef VECTOR_ARG

#define STRING_ARG(TYPE, METHOD) \
    if (val.IsHolding<TYPE>()) { \
	args.append(name); \
	args.append(val.UncheckedGet<TYPE>()METHOD); \
	return true; \
    } \
    if (val.IsHolding< VtArray<TYPE> >()) { \
	args.append(name); \
	const VtArray<TYPE> &arr = val.UncheckedGet< VtArray<TYPE> >(); \
	args.append(theOpenParen.asHolder()); \
	for (VtArray<TYPE>::const_iterator it = arr.cbegin(), \
	    end = arr.cend(); it != end; ++it) \
		args.append((*it)METHOD); \
	args.append(theCloseParen.asHolder()); \
	return true; \
    }
    /* end of macro */

    STRING_ARG(std::string,);
    STRING_ARG(TfToken, .GetText());
    STRING_ARG(UT_StringHolder,);
#undef STRING_ARG

    if (val.IsHolding<SdfAssetPath>())
    {
	SdfAssetPath p = val.UncheckedGet<SdfAssetPath>();
	const std::string &resolved = p.GetResolvedPath();
	args.append(name);
	if (resolved.length())
	    args.append(UT_StringHolder(resolved));
#if defined(USE_HOUDINI_PATH)
	else
	    args.append(UT_StringHolder(p.GetAssetPath()));
#endif
	return true;
    }
    else if (val.IsHolding< VtArray<SdfAssetPath> >())
    {
	args.append(name);
	args.append(theOpenParen.asHolder());
	const VtArray<SdfAssetPath> &arr = 
	    val.UncheckedGet< VtArray<SdfAssetPath> >();
	for (VtArray<SdfAssetPath>::const_iterator it = arr.cbegin(),
	    end = arr.cend(); it != end; ++it)
	{
	    SdfAssetPath p = *it;
	    const std::string &resolved = p.GetResolvedPath();
	    if (resolved.length())
		args.append(UT_StringHolder(resolved));
#if defined(USE_HOUDINI_PATH)
	    else
		args.append(UT_StringHolder(p.GetAssetPath()));
#endif
	}
	args.append(theCloseParen.asHolder());
	return true;
    }
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
	    , mySize(task.mySize)
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
    // TODO: Surely there must be a better way to do this!
#define HANDLE_TYPE(TYPE) \
    if (val.IsHolding<VtArray<TYPE>>()) \
	return gtArray(val.UncheckedGet<VtArray<TYPE>>(), typeHint(token)); \
    if (val.IsHolding<TYPE>()) \
        return gtArrayFromScalar(val.UncheckedGet<TYPE>(), typeHint(token));
#define HANDLE_CLASS_TYPE(TYPE, tuple_size) \
    if (val.IsHolding<VtArray<TYPE>>()) \
	return gtArray(val.UncheckedGet<VtArray<TYPE>>(), typeHint(token)); \
    if (val.IsHolding<TYPE>()) \
        return gtArrayFromScalarClass(val.UncheckedGet<TYPE>(),typeHint(token));

    HANDLE_CLASS_TYPE(GfVec3f, 3)
    HANDLE_CLASS_TYPE(GfVec4f, 4)
    HANDLE_CLASS_TYPE(GfVec2f, 2)
    HANDLE_CLASS_TYPE(GfQuatf, 4)
    HANDLE_CLASS_TYPE(GfMatrix3f, 9)
    HANDLE_CLASS_TYPE(GfMatrix4f, 16)
    HANDLE_TYPE(fpreal32)

    HANDLE_CLASS_TYPE(GfVec3d, 3)
    HANDLE_CLASS_TYPE(GfVec4d, 4)
    HANDLE_CLASS_TYPE(GfVec2d, 2)
    HANDLE_CLASS_TYPE(GfQuatd, 4)
    HANDLE_CLASS_TYPE(GfMatrix3d, 9)
    HANDLE_CLASS_TYPE(GfMatrix4d, 16)
    HANDLE_TYPE(fpreal64)

    HANDLE_CLASS_TYPE(GfVec3h, 3)
    HANDLE_CLASS_TYPE(GfVec4h, 4)
    HANDLE_CLASS_TYPE(GfVec2h, 2)
    HANDLE_CLASS_TYPE(GfQuath, 4)
    HANDLE_TYPE(fpreal16)

    HANDLE_TYPE(int32)
    HANDLE_TYPE(int64)

    if (val.IsHolding<VtArray<std::string>>())
    {
	return GT_DataArrayHandle(new GusdGT_VtStringArray<std::string>(
            val.Get<VtArray<std::string>>()));
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


const UT_StringHolder
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
    return UT_StringHolder(token.GetString());
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
    static void
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
    }
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
    bool	mblur = *props.bval(BRAY_OBJ_MOTION_BLUR);
    int		vblur = *props.ival(BRAY_OBJ_GEO_VELBLUR);

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
		matchMotionSamples(id, data, expected_size);
	    }
	    else
	    {
		UT_ASSERT(expected_size < 0 
			|| expected_size == data[0]->entries());
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

namespace
{
    template <typename T> static bool
    dumpValueT(const VtValue &val, const char *msg)
    {
	if (!val.IsHolding<T>())
	    return false;
	UTdebugFormat("Value: {} {}", msg, val.UncheckedGet<T>());
	return true;
    }
}

void
BRAY_HdUtil::dumpValue(const VtValue &val, const char *msg)
{
#define DUMP_TYPE(T) if (dumpValueT<T>(val, msg)) return;
    DUMP_TYPE(fpreal64)
    DUMP_TYPE(fpreal32)
    DUMP_TYPE(fpreal16)
    DUMP_TYPE(bool)
    DUMP_TYPE(int8)
    DUMP_TYPE(int16)
    DUMP_TYPE(int32)
    DUMP_TYPE(int64)
    DUMP_TYPE(uint8)
    DUMP_TYPE(uint16)
    DUMP_TYPE(uint32)
    DUMP_TYPE(uint64)
    DUMP_TYPE(std::string)
    DUMP_TYPE(TfToken)
    DUMP_TYPE(SdfPath)
    DUMP_TYPE(UT_StringHolder)
    DUMP_TYPE(GfVec2i)
    DUMP_TYPE(GfVec3i)
    DUMP_TYPE(GfVec4i)
    DUMP_TYPE(GfVec2f)
    DUMP_TYPE(GfVec3f)
    DUMP_TYPE(GfVec4f)
    DUMP_TYPE(GfSize2)
    DUMP_TYPE(GfSize3)
    DUMP_TYPE(GfQuatf)
    DUMP_TYPE(GfMatrix3f)
    DUMP_TYPE(GfMatrix4f)
    DUMP_TYPE(GfVec2d)
    DUMP_TYPE(GfVec3d)
    DUMP_TYPE(GfVec4d)
    DUMP_TYPE(GfQuatd)
    DUMP_TYPE(GfMatrix3d)
    DUMP_TYPE(GfMatrix4d)
    DUMP_TYPE(GfVec2h)
    DUMP_TYPE(GfVec3h)
    DUMP_TYPE(GfVec4h)
    DUMP_TYPE(GfQuath)
    DUMP_TYPE(GfMatrix4f)
    DUMP_TYPE(GfMatrix4d)
    DUMP_TYPE(VtArray<bool>)
    DUMP_TYPE(VtArray<int32>)
    DUMP_TYPE(VtArray<int64>)
    DUMP_TYPE(VtArray<fpreal32>)
    DUMP_TYPE(VtArray<fpreal64>)
    DUMP_TYPE(VtArray<std::string>)
    DUMP_TYPE(VtArray<TfToken>)
    DUMP_TYPE(VtArray<SdfPath>)
#undef DUMP_TYPE
    UTdebugFormat("{}: Unhandled type {}", msg, val.GetTypeName());
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
    if (!src || src->getSegments() != 1 || nseg == 1 || style == 0)
	return src;

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
BRAY_HdUtil::xformSamples(const BRAY::OptionSet &o)
{
    return *o.bval(BRAY_OBJ_MOTION_BLUR) ? *o.ival(BRAY_OBJ_XFORM_SAMPLES) : 1;
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
    int nsegs = xformSamples(props);

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
INSTANTIATE_EVAL_STYLE(BRAY_HdUtil::EVAL_MATERIAL_PARM)

PXR_NAMESPACE_CLOSE_SCOPE
