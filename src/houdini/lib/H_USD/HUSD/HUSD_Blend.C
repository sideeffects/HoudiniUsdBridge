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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_Blend.h"
#include "HUSD_Constants.h"
#include "HUSD_TimeCode.h"
#include "HUSD_Xform.h"
#include "XUSD_Data.h"
#include "XUSD_Format.h"
#include "XUSD_Utils.h"
#include <gusd/UT_Gf.h>
#include <UT/UT_TransformUtil.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/interpolation.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include BOOST_HEADER(preprocessor/seq/for_each.hpp)
#include <functional>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

/// \class HUSD_UntypedInterpolator
///
/// Interpolator used for type-erased value access.
///
/// The type-erased value API does not provide information about the
/// expected value type, so this interpolator needs to do more costly
/// type lookups to dispatch to the appropriate interpolator.
///
class HUSD_UntypedInterpolator
{
public:
    HUSD_UntypedInterpolator(VtValue* result)
        : _result(result)
    {
    }

    bool Interpolate(const UsdAttribute &baseattr,
	    const UsdAttribute &newattr,
	    const UsdTimeCode &timecode,
	    fpreal blend);

private:
    VtValue* _result;
};

template <class T>
inline T
HUSDlerp(double alpha, const T &lower, const T &upper)
{
    return GfLerp(alpha, lower, upper);
}

inline GfQuath
HUSDlerp(double alpha, const GfQuath &lower, const GfQuath &upper)
{
    return GfSlerp(alpha, lower, upper);
}

inline GfQuatf
HUSDlerp(double alpha, const GfQuatf &lower, const GfQuatf &upper)
{
    return GfSlerp(alpha, lower, upper);
}

inline GfQuatd
HUSDlerp(double alpha, const GfQuatd &lower, const GfQuatd &upper)
{
    return GfSlerp(alpha, lower, upper);
}

/// \class HUSD_LinearInterpolator
///
/// Object implementing linear interpolation for attribute values.
///
/// With linear interpolation, the attribute value for a time with no samples
/// will be linearly interpolated from the previous and next time samples.
///
template <class T>
class HUSD_LinearInterpolator
{
public:
    HUSD_LinearInterpolator(T* result)
        : _result(result)
    {
    }

    bool Interpolate(const UsdAttribute &baseattr,
	    const UsdAttribute &newattr,
	    const UsdTimeCode &timecode,
	    fpreal blend)
    {
        T base_value, new_value;

	baseattr.Get<T>(&base_value, timecode);
	newattr.Get<T>(&new_value, timecode);

        *_result = HUSDlerp(blend, base_value, new_value);

        return true;
    }

private:
    T* _result;
};

// Specialization to linearly interpolate each element for
// array types.
template <class T>
class HUSD_LinearInterpolator<VtArray<T> >
{
public:
    HUSD_LinearInterpolator(VtArray<T>* result)
        : _result(result)
    {
    }

    bool Interpolate(const UsdAttribute &baseattr,
	    const UsdAttribute &newattr,
	    const UsdTimeCode &timecode,
	    fpreal blend)
    {
        VtArray<T> base_value, new_value;

	baseattr.Get<VtArray<T> >(&base_value, timecode);
	newattr.Get<VtArray<T> >(&new_value, timecode);

        _result->swap(base_value);

	// If sizes don't match, use the new value, even if the blend factor is
	// zero. If the old value was size 1, act as if this value has the same
	// length as the new value and do the interpolation. If the attributes
	// are primvars, we also want to record the change to the interpolation
	// type.
        if (_result->size() != new_value.size())
	{
	    if (_result->size() == 1 && new_value.size() > 1)
	    {
		// Note that "new_value" will now contain the "base" value.
		_result->swap(new_value);

		T *rptr = _result->data();

		for (size_t i = 0, j = _result->size(); i != j; ++i) {
		    rptr[i] = HUSDlerp(blend, new_value[0], rptr[i]);
		}
	    }
	    else
		_result->swap(new_value);

            return true;
        }

        if (blend == 0.0) {
	    // If the blend value is zero, just return the base value.
	    return true;
        }
	else if (blend == 1.0) {
            // If the blend value is one, just swap the new value in.
            _result->swap(new_value);
        }
        else {
	    // Calculate the interpolated values.
	    T *rptr = _result->data();
	    for (size_t i = 0, j = _result->size(); i != j; ++i) {
		rptr[i] = HUSDlerp(blend, rptr[i], new_value[i]);
	    }
        }

        return true;
    }        

private:
    VtArray<T>* _result;
};

bool 
HUSD_UntypedInterpolator::Interpolate(
	const UsdAttribute &baseattr,
	const UsdAttribute &newattr,
	const UsdTimeCode &timecode,
	fpreal blend)
{
    // Since we're working with type-erased objects, we have no
    // choice but to do a series of runtime type checks to determine 
    // what kind of interpolation is supported for the attribute's
    // value.

    const TfType attrValueType = baseattr.GetTypeName().GetType();
    if (!attrValueType) {
        TF_RUNTIME_ERROR(
            "Unknown value type '%s' for attribute '%s'",
            baseattr.GetTypeName().GetAsToken().GetText(),
            baseattr.GetPath().GetString().c_str());
        return false;
    }

#define _MAKE_CLAUSE(r, unused, type)                                   \
    {                                                                   \
        static const TfType valueType = TfType::Find<type>();           \
        if (attrValueType == valueType) {                               \
            type result;                                                \
            if (HUSD_LinearInterpolator<type>(&result).Interpolate(     \
                    baseattr, newattr, timecode, blend)) {              \
                *_result = result;                                      \
                return true;                                            \
            }                                                           \
            return false;                                               \
        }                                                               \
    }

    HBOOST_PP_SEQ_FOR_EACH(_MAKE_CLAUSE, ~, USD_LINEAR_INTERPOLATION_TYPES)
#undef _MAKE_CLAUSE

    return false;
}

class HUSD_Blend::husd_BlendPrivate {
public:
    SdfLayerRefPtr		 myLayer;
    XUSD_LockedGeoArray		 myLockedGeoArray;
};

class husd_BlendData {
public:
    UsdStageRefPtr			 myBaseStage;
    UsdStageRefPtr			 myCombinedStage;
    SdfLayerRefPtr			 myLayer;
    UsdTimeCode				 myTimeCode;
    fpreal				 myBlendFactor;
    std::map<SdfPath, UT_Matrix4D>	 myBlendXforms;
    std::map<SdfPath, VtValue>		 myBlendValues;
    std::map<SdfPath, TfToken>		 myPrimvarInterps;
    bool				 myUsedTimeVaryingData;
};

HUSD_Blend::HUSD_Blend()
    : myPrivate(new HUSD_Blend::husd_BlendPrivate()),
      myTimeVarying(false)
{
}

HUSD_Blend::~HUSD_Blend()
{
}

bool
HUSD_Blend::setBlendHandle(const HUSD_DataHandle &src)
{
    HUSD_AutoReadLock	 inlock(src);
    auto		 indata = inlock.data();
    bool		 success = false;

    if (indata && indata->isStageValid())
    {
	// Flatten the information we want to blend into a single layer.
	// This also strips out any layers marked as "do not save", meaning
	// they should be ignored.
	myPrivate->myLayer = indata->createFlattenedLayer(
	    HUSD_IGNORE_STRIPPED_LAYERS);

	// Hold onto lockedgeos to keep in memory any cooked OP data referenced
	// by the layers being merged.
	myPrivate->myLockedGeoArray.concat(indata->lockedGeos());
	success = true;
    }

    return success;
}

static void
generateBlendXform(husd_BlendData &data,
	const SdfPath &primpath)
{
    UT_Matrix4D	 blendxform(1.0);

    // If the blend factor is zero, we still want to set a blend xform, so that
    // we end up with a consistent xformOpOrder over all time. But we don't
    // actually need to do any calculation. Just use the identity matrix.
    if (data.myBlendFactor != 0.0)
    {
	// Get the local xform of the base stage prim.
	UsdPrim	 baseprim(data.myBaseStage->GetPrimAtPath(primpath));
	UsdPrim	 newprim(data.myCombinedStage->GetPrimAtPath(primpath));

	if (baseprim && newprim)
	{
	    UsdGeomXformable	 basexformable(baseprim);
	    UsdGeomXformable	 newxformable(newprim);

	    if (basexformable && newxformable)
	    {
		GfMatrix4d	 basexform;
		GfMatrix4d	 newxform;
		bool		 dummy = false;

		// If either the base or combined xform is time varying,
		// then the blend operation is time varying.
		if (HUSDlocalTransformMightBeTimeVarying(baseprim) ||
		    HUSDlocalTransformMightBeTimeVarying(newprim))
		    data.myUsedTimeVaryingData = true;

		// Get the base and nex transforms so we can figure out the
		// transform needed to blend from one to the other.
		basexformable.GetLocalTransformation(
		    &basexform, &dummy, data.myTimeCode);
		newxformable.GetLocalTransformation(
		    &newxform, &dummy, data.myTimeCode);

		// If the transforms are equal, the default identity matrix
		// will do.
		if (basexform != newxform)
		{
                    UT_Array<UT_Matrix4D>    xforms;
                    UT_Array<fpreal64>       weights;
                    UT_Matrix4D              basexforminv;
                    UT_Matrix4D              slerpxform;

		    xforms.append(GusdUT_Gf::Cast(basexform));
                    weights.append(1.0 - data.myBlendFactor);
		    xforms.append(GusdUT_Gf::Cast(newxform));
                    weights.append(data.myBlendFactor);
                    slerpxform = UTslerp(xforms, weights);

		    xforms(0).invert(basexforminv);
		    blendxform = slerpxform * basexforminv;
		}
	    }
	}
    }
    data.myBlendXforms.emplace(primpath, blendxform);
}

static void
generateBlendAttribute(husd_BlendData &data,
	const UsdAttribute &baseattr,
	const UsdAttribute &newattr)
{
    VtValue			 result;
    HUSD_UntypedInterpolator	 interp(&result);

    if (interp.Interpolate(baseattr, newattr,
	    data.myTimeCode, data.myBlendFactor))
    {
	data.myBlendValues.emplace(baseattr.GetPath(), result);

	UsdGeomPrimvar		 newprimvar(newattr);

	if (newprimvar)
	{
	    UsdGeomPrimvar	 baseprimvar(baseattr);
	    TfToken		 newinterp = newprimvar.GetInterpolation();

	    if (!baseprimvar || newinterp != baseprimvar.GetInterpolation())
		data.myPrimvarInterps.emplace(baseattr.GetPath(), newinterp);
	}
    }
}

static void
primTraversal(husd_BlendData &data,
	const SdfPath &path)
{
    // Only interested in properties, and never interested in the
    // HoudiniLayerInfo primitive.
    if (path.IsPrimPropertyPath() &&
	path.GetPrimPath() != HUSDgetHoudiniLayerInfoSdfPath())
    {
	auto	 stage = data.myBaseStage;
	auto	 layer = data.myLayer;
	SdfPath	 primpath = path.GetPrimPath();
	UsdPrim	 baseprim(data.myBaseStage->GetPrimAtPath(primpath));
	UsdPrim	 newprim(data.myCombinedStage->GetPrimAtPath(primpath));

	if (baseprim && newprim)
	{
	    TfToken	 attrname = path.GetNameToken();
	    UsdAttribute baseattr = baseprim.GetAttribute(attrname);
	    UsdAttribute newattr = newprim.GetAttribute(attrname);

	    if (baseattr && newattr)
	    {
		if (UsdGeomXformable::
		    IsTransformationAffectedByAttrNamed(attrname))
		{
		    // This is a transform-related attribute. To do this
		    // accurately, we have to compose a combined stage.
		    if (data.myBlendXforms.find(primpath) ==
			    data.myBlendXforms.end())
		    {
			// We haven't yet calculated the blend xform for this
			// primitive.
			generateBlendXform(data, primpath);
		    }
		}
		else
		{
		    generateBlendAttribute(data, baseattr, newattr);
		}
	    }
	}
    }
}

bool
HUSD_Blend::execute(HUSD_AutoWriteLock &lock,
	fpreal blend,
	const HUSD_TimeCode &timecode,
	UT_StringArray &modified_prims) const
{
    bool			 success = false;
    auto			 outdata = lock.data();

    if (outdata && outdata->isStageValid())
    {
	husd_BlendData		 data;
	std::vector<std::string> sublayers;

	data.myBaseStage = outdata->stage();
	data.myLayer = myPrivate->myLayer;
	data.myTimeCode = HUSDgetNonDefaultUsdTimeCode(timecode);
	data.myUsedTimeVaryingData = false;
	data.myBlendFactor = blend;
	// Create a stage that applies the blend layer over the base layer.
	sublayers.push_back(data.myLayer->GetIdentifier());
	sublayers.push_back(data.myBaseStage->GetRootLayer()->GetIdentifier());
	data.myCombinedStage = HUSDcreateStageInMemory(
	    outdata->loadMasks().get(), outdata->stage());
	data.myCombinedStage->GetRootLayer()->SetSubLayerPaths(sublayers);

	// Traverse the blend layer. Any authored value should be blended
	// with the corresponding USD primitive on the current stage.
	myPrivate->myLayer->
	    Traverse(SdfPath::AbsoluteRootPath(),
		std::bind(primTraversal,std::ref(data),std::placeholders::_1));
	// Delete the combined stage before applying any edits so that we
	// don't waste any time on detecting/propagating change notifications.
	data.myCombinedStage.Reset();
	// Record if the blend used any time varying attributes.
	myTimeVarying = data.myUsedTimeVaryingData;

	if (!data.myBlendXforms.empty())
	{
	    HUSD_Xform		 xformer(lock);
	    HUSD_XformEntryMap	 xform_map;

	    for (auto &&it : data.myBlendXforms)
	    {
		xform_map.emplace(it.first.GetString(), HUSD_XformEntryArray(
		    { HUSD_XformEntry({it.second, timecode}) }));
	    }
	    xformer.applyXforms(xform_map, "blend", HUSD_XFORM_APPEND);
	}
	if (!data.myBlendValues.empty())
	{
	    for (auto &&it : data.myBlendValues)
	    {
		SdfPath	 primpath = it.first.GetPrimPath();
		UsdPrim	 prim = data.myBaseStage->GetPrimAtPath(primpath);

		if (prim)
		{
		    TfToken	 attrname = it.first.GetNameToken();
		    UsdAttribute attr = prim.GetAttribute(attrname);

		    if (attr)
		    {
			attr.Set(it.second, data.myTimeCode);
			HUSDclearDataId(attr);
		    }
		}
	    }
	}
	if (!data.myPrimvarInterps.empty())
	{
	    for (auto &&it : data.myPrimvarInterps)
	    {
		SdfPath	 primpath = it.first.GetPrimPath();
		UsdPrim	 prim = data.myBaseStage->GetPrimAtPath(primpath);

		if (prim)
		{
		    TfToken	 attrname = it.first.GetNameToken();
		    UsdAttribute attr = prim.GetAttribute(attrname);

		    if (attr)
		    {
			UsdGeomPrimvar	 primvar(attr);

			if (primvar)
			    primvar.SetInterpolation(it.second);
		    }
		}
	    }
	}

	success = true;
    }

    return success;
}

