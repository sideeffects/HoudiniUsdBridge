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

#ifndef __BRAY_HdUtil__
#define __BRAY_HdUtil__

#include <pxr/pxr.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/renderDelegate.h>	// For HdRenderSettingsMap
#include <pxr/imaging/hd/rprim.h>
#include <BRAY/BRAY_Interface.h>
#include <HUSD/XUSD_Format.h>
#include <UT/UT_Set.h>
#include <gusd/USD_Utils.h>
#include <GT/GT_AttributeList.h>
#include <GT/GT_DataArray.h>
#include <GT/GT_Handles.h>

// NOTE: Please consider adding general functions to HUSD/XUSD_HydraUtils.h
//       so that the GL can use them as well.

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdParam;

class BRAY_HdUtil
{
public:
    /// Returns "karma:", the prefix for karma-specific parameters
    static const char	*parameterPrefix();

    static const std::string	&resolvePath(const SdfAssetPath &p);

    // When you want a UT_StringHolder, you should never call GetText() or
    // GetString() or GetToken() directly.  On SdfPath objects, this will cache
    // the full path forever.  For TfToken, @c toStr() can share data if the
    // token is immortal.

    // Convert an SdfPath to a UT_StringHolder without caching the value on the
    // SdfPath.
    static UT_StringHolder      toStr(const SdfPath &p);
    // Convert a VtValue to a UT_StringHolder (or return an empty string)
    static UT_StringHolder      toStr(const VtValue &t);

    // @{
    // Convert other types to UT_StringHolder
    static UT_StringHolder      toStr(const std::string &s)
                                    { return UT_StringHolder(s); }
    static UT_StringHolder      toStr(const SdfAssetPath &p)
                                    { return resolvePath(p); }
    static UT_StringHolder      toStr(const TfToken &t)
    {
        return GusdUSD_Utils::TokenToStringHolder(t);
    }
    // @}

    enum EvalStyle
    {
	EVAL_GENERIC,
	EVAL_CAMERA_PARM,
	EVAL_LIGHT_PARM,
    };
    // If there are multiple requests for material relative references, this
    // adds an unusual cost to garbage collection for Hydra.  We hypothesize
    // this is due to attempting to remove paths multiple times.  This class
    // manages the material resolution so that there's only one binding
    // request.
    class MaterialId
    {
    public:
	MaterialId(HdSceneDelegate &sd, const SdfPath &id)
	    : myDelegate(sd)
	    , myId(id)
	    , myResolved(false)
	{
	}
	bool		IsEmpty() const { return myMaterial.IsEmpty(); }
	SYS_SAFE_BOOL	operator bool() const { return !IsEmpty(); }

	// Return the path (possibly not unresolved)
	const char	*path() const { return myMaterial.GetText(); }

	// Resolve the path and return the material
	const SdfPath	&resolvePath()
	{
	    if (!myResolved)
	    {
		myMaterial = myDelegate.GetMaterialId(myId);
		myResolved = true;
	    }
	    return myMaterial;
	}
    private:
	HdSceneDelegate	&myDelegate;
	SdfPath		 myId;
	SdfPath		 myMaterial;
	bool		 myResolved;
    };
    /// Create a GT data array for the given source type
    template <typename A_TYPE> static
    GT_DataArrayHandle	gtArray(const A_TYPE &usd_array,
				GT_Type tinfo = GT_TYPE_NONE);

    // Print the VtValue as a VEX constant, returning the VEX type.  This may
    // return a @c nullptr if the VtValue cannot be converted.
    static const char	*valueToVex(UT_WorkBuffer &, const VtValue &value);

    // Print the VtValue as a VEX constant, returning the VEX type.  This may
    // return a @c nullptr if the VtValue cannot be converted.
    static bool		appendVexArg(UT_StringArray &args,
				const UT_StringHolder &name,
				const VtValue &value);

    // Add a Key/VtValue to a UT_Options (returns false if the value type isn't
    // handled)
    static bool addOption(UT_Options &opts,
                        const TfToken &name, const VtValue &value);

    /// Update visibility for an object
    static void	updateVisibility(HdSceneDelegate *sd,
			const SdfPath &id,
			BRAY::OptionSet &props,
			bool is_visible,
			const TfToken &render_tag);

    /// Sum the values in an integer array
    static GT_Size	sumCounts(const GT_DataArrayHandle &counts);

    /// @{
    /// Create a BRAY::SpacePtr for a given matrix.  This includes transforms
    /// for multiple segements of motion blur.  This is specialized for:
    /// - GfMatrix4f
    /// - GfMatrix4d
    template <typename M_TYPE> static
    BRAY::SpacePtr	makeSpace(const M_TYPE *m, int seg_count = 1);

    template <typename M_TYPE> static
    BRAY::SpacePtr	makeSpace(const M_TYPE *const*m, int seg_count = 1);

    template <typename M_TYPE> inline static
    BRAY::SpacePtr	makeSpace(const M_TYPE &m) { return makeSpace(&m, 1); }
    /// @}

    /// Create a primvar -> material input binding
    static bool	addInput(const UT_StringHolder &primvarName,
			const VtValue &fallbackValue,
			const TfToken &vexName,
			UT_Array<BRAY::MaterialInput> &inputMap,
			UT_StringArray &args);

    /// Create an array of transforms from a source array of matrices.  This is
    /// specialized for:
    /// - VtMatrix4fArray
    /// - VtMatrix4dArray
    template <typename L_TYPE> static
    void		makeSpaceList(UT_Array<BRAY::SpacePtr> &xforms,
				    const L_TYPE &list);

    template <typename L_TYPE> static
    void		makeSpaceList(UT_Array<BRAY::SpacePtr> &xforms,
				    const L_TYPE *list,
				    int seg_count);

    /// Code that is used throughout the BRAY_Hd library for a variety of purposes
    /// We have moved it here so that we don't end up duplicating it more than 
    /// necessary. Consider moving any local namespace static functions
    /// which might be used somewhere else to here.

#if 0
    static
    GT_AttributeListHandle	makeProperties(HdSceneDelegate &sd,
					const BRAY_HdParam &rparm,
					const SdfPath &id,
					const HdInterpolation *interp,
					int ninterp);
#endif
    /// Check to see the primitive primvars match what we previously had
    static bool			matchAttributes(HdSceneDelegate *sd,
					const SdfPath &id,
					const TfToken &primType,
					const HdInterpolation *interp,
					int ninterp,
					const GT_AttributeListHandle &gt,
					const UT_Set<TfToken> *skip = nullptr,
					bool skip_namespace = true);
    static bool			matchAttributes(HdSceneDelegate *sd,
					const SdfPath &id,
					const TfToken &primType,
					const HdInterpolation interp,
					const GT_AttributeListHandle &gt,
					const UT_Set<TfToken> *skip = nullptr,
					bool skip_namespace = true)
    {
	return matchAttributes(sd, id, primType, &interp, 1,
		gt, skip, skip_namespace);
    }

    static
    GT_AttributeListHandle	makeAttributes(HdSceneDelegate *sd,
					const BRAY_HdParam &rparm,
					const SdfPath &id,
					const TfToken &typeId,
					GT_Size expected_size,
					const BRAY::OptionSet &props,
					const HdInterpolation *interp,
					int ninterp,
					const UT_Set<TfToken> *skip = nullptr,
					bool skip_namespace = true);
    static
    GT_AttributeListHandle	makeAttributes(HdSceneDelegate *sd,
					const BRAY_HdParam &rparm,
					const SdfPath& id,
					const TfToken& typeId,
					GT_Size expected_size,
					const BRAY::OptionSet &props,
					HdInterpolation interp,
					const UT_Set<TfToken> *skip = nullptr,
					bool skip_namespace = true)
    {
	return makeAttributes(sd, rparm, id, typeId, expected_size, props,
		&interp, 1, skip, skip_namespace);
    }
    static bool			updateAttributes(HdSceneDelegate *sd,
					const BRAY_HdParam &rparm,
					HdDirtyBits *dirtyBits,
					const SdfPath &id,
					const GT_AttributeListHandle &src,
					GT_AttributeListHandle &dest,
					BRAY_EventType &event,
					const BRAY::OptionSet &props,
					const HdInterpolation *interp,
					int ninterp);
    static bool			updateAttributes(HdSceneDelegate *sd,
					const BRAY_HdParam &rparm,
					HdDirtyBits *dirtyBits,
					const SdfPath &id,
					const GT_AttributeListHandle &src,
					GT_AttributeListHandle &dest,
					BRAY_EventType &event,
					const BRAY::OptionSet &props,
					HdInterpolation interp)
    {
	return updateAttributes(sd, rparm, dirtyBits, id,
		src, dest, event, props, &interp, 1);
    }


    static
    void		    dumpvalue(const TfToken& token, const VtValue& val,
				const GT_DataArrayHandle& d);

    // Dump some common representations of a value
    static void		dumpValue(const VtValue &val, const char *msg="");
    static void		dumpValue(const VtValue &val, const TfToken &tok)
			    { dumpValue(val, tok.GetText()); }
    static void		dumpValue(const VtValue &val, const std::string &tok)
			    { dumpValue(val, tok.c_str()); }

    static
    GT_AttributeListHandle  velocityBlur(const GT_AttributeListHandle& src,
				int style,
				int nseg,
				const BRAY_HdParam &param);

    static int          velocityBlur(const BRAY_HdParam &rparm,
                                const BRAY::OptionSet &props);

    static int		xformSamples(const BRAY_HdParam &rparm,
				const BRAY::OptionSet &props);

    /// Queries the scene delegate to check if we have animated transforms
    /// Note that this function does not query for instancer transforms yet!
    static void		xformBlur(HdSceneDelegate* sceneDelegate,
				const BRAY_HdParam &rparm,
				const SdfPath &id,
				UT_Array<GfMatrix4d>& xforms,
				const BRAY::OptionSet &props);

    /// Compute transformation blur by interpolating samples (if required).
    /// The @c times and @c nsegs are the sample times requested by the user
    /// while the @c stimes and @c snsegs are the sample times provided by
    /// Hydra.  The resulting array of @c xforms will have the transform
    /// samples distributed evenly over the 0-1 shutter space.
    static void		xformBlur(HdSceneDelegate *sceneDelegate,
				UT_Array<GfMatrix4d> &xforms,
				const SdfPath &id,
				const float *times, int nsegs);


    /// Compute an array of primvar values for motion blur
    /// The @c times and @c nsegs are the sample times requested by the user
    /// while the @c stimes and @c snsegs are the sample times provided by
    /// Hydra.  The resulting array of @c values will have the samples
    /// distributed evenly over the 0-1 shutter space.
    template <EvalStyle ESTYLE=EVAL_GENERIC>
    static bool		dformBlur(HdSceneDelegate *sceneDelegate,
				UT_Array<GT_DataArrayHandle> &values,
				const SdfPath &id,
				const TfToken &name,
				const float *times, int nsegs);

    static inline bool	dformCamera(HdSceneDelegate *sd,
				UT_Array<GT_DataArrayHandle> &values,
				const SdfPath &id,
				const TfToken &name,
				const float *times, int nsegs)
    {
	return dformBlur<EVAL_CAMERA_PARM>(sd, values, id, name, times, nsegs);
    }

    /// Compute an array of VtValue scalar values for motion blur.
    /// The @c times and @c nsegs are the sample times requested by the user
    /// while the @c stimes and @c snsegs are the sample times provided by
    /// Hydra.  The resulting array of @c values will have the samples
    /// distributed evenly over the 0-1 shutter space.
    /// This is only valid if the primvar refers to a scalar value.
    template <EvalStyle ESTYLE=EVAL_GENERIC>
    static bool		dformBlur(HdSceneDelegate *sceneDelegate,
				UT_Array<VtValue> &values,
				const SdfPath &id,
				const TfToken &name,
				const float *times, int nsegs);
    static inline bool	dformCamera(HdSceneDelegate *sd,
				UT_Array<VtValue> &values,
				const SdfPath &id,
				const TfToken &name,
				const float *times, int nsegs)
    {
	return dformBlur<EVAL_CAMERA_PARM>(sd, values, id, name, times, nsegs);
    }

    /// Evaluate parameters and update the object properties
    static bool		updateObjectProperties(BRAY::OptionSet &props,
				HdSceneDelegate &delegate,
				const SdfPath &id);
    static bool		updateObjectPrimvarProperties(BRAY::OptionSet &props,
				HdSceneDelegate &delegate,
                                HdDirtyBits* dirtyBits,
				const SdfPath &id);

    /// Update scene settings
    static bool		updateSceneOptions(BRAY::ScenePtr &scene,
				const HdRenderSettingsMap &settings);

    static bool		sceneOptionNeedUpdate(BRAY::ScenePtr &scene,
				const TfToken &token,
				const VtValue &value);
    static bool		updateSceneOption(BRAY::ScenePtr &scene,
				const TfToken &token,
				const VtValue &value);

    /// Update tracesets property
    static void		updatePropCategories(BRAY_HdParam &rparm,
				HdSceneDelegate *delegate,
				HdRprim *rprim,
				BRAY::OptionSet &props);

    /// Set the option set's value
    static bool		setOption(BRAY::OptionSet &options,
				int token,
				const VtValue &value);

    /// Convert an attribute of the given name
    static
    GT_DataArrayHandle	    convertAttribute(const VtValue &val,
				const TfToken &token);

    /// Update hdrprimid. returns true if id was changed
    static bool		updateRprimId(BRAY::OptionSet &props,
				HdRprim *rprim);

    static UT_StringHolder usdNameToGT(const TfToken& token,
				const TfToken& typeId);

private:
    template <EvalStyle ESTYLE=EVAL_GENERIC>
    static bool		dformBlurArray(HdSceneDelegate *sceneDelegate,
				UT_Array<GT_DataArrayHandle> &values,
				const SdfPath &id,
				const TfToken &lengths,
				const float *times, int nsegs);

    /// Create a GT data array for the given scalar source type
    template <typename A_TYPE> static
    GT_DataArrayHandle	gtArrayFromScalar(const A_TYPE &usd,
				GT_Type tinfo = GT_TYPE_NONE);
    /// Create a GT data array for the given scalar class source type
    template <typename A_TYPE> static
    GT_DataArrayHandle	gtArrayFromScalarClass(const A_TYPE &usd,
                                GT_Type tinfo = GT_TYPE_NONE);

    static
    const TfToken	    gtNameToUSD(const UT_StringHolder& name);

    static
    const UT_StringHolder&  velocityName();

    static
    const UT_StringHolder&  accelName();

    static
    bool		    velocityBlur(UT_Array<GT_DataArrayHandle>& p,
				const GT_DataArrayHandle& Parr,
				const GT_DataArrayHandle& varr,
				const GT_DataArrayHandle& Aarr,
				int style,
				int nseg,
				const BRAY_HdParam &rparm);

    static
    GT_DataArrayHandle	    computeBlur(const GT_DataArrayHandle& Parr,
				const fpreal32* P, const fpreal32* v,
				const fpreal32* a, fpreal32 amount);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
