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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraInstancer.C (HUSD Library, C++)
 *
 * COMMENTS:	Basic instancer for creating instance transforms.
 *
 */

#include "XUSD_HydraInstancer.h"
#include "XUSD_Format.h"
#include "XUSD_Tokens.h"
#include "HUSD_Scene.h"

#include <UT/UT_Debug.h>

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/quaternion.h>
#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace 
{
    template <typename QT, typename VT>
    static VtValue
    quatToVec4(const QT &qarr)
    {
	using VELEM = typename VT::value_type;
	VT	rarr;
	rarr.reserve(qarr.size());
	for (auto &&q : qarr)
	{
	    rarr.push_back(VELEM(q.GetReal(),
		    q.GetImaginary()[0],
		    q.GetImaginary()[1],
		    q.GetImaginary()[2]));
	}
	return VtValue(rarr);
    }

    template <typename D, typename S>
    static void
    lerpVec(D *dest, const S *s0, const S *s1, float lerp, int n)
    {
	for (int i = 0; i < n; ++i)
	    dest[i] = SYSlerp(s0[i], s1[i], S(lerp));
    }

    static VtValue
    patchQuaternion(const VtValue &v)
    {
	if (v.IsHolding<VtQuathArray>())
	{
	    return quatToVec4<VtQuathArray, VtVec4hArray>(v.UncheckedGet<VtQuathArray>());
	}
	if (v.IsHolding<VtQuatfArray>())
	{
	    return quatToVec4<VtQuatfArray, VtVec4fArray>(v.UncheckedGet<VtQuatfArray>());
	}
	if (v.IsHolding<VtQuatdArray>())
	{
	    return quatToVec4<VtQuatdArray, VtVec4dArray>(v.UncheckedGet<VtQuatdArray>());
	}
	return v;
    }

    template <typename V3, bool DO_INTERP>
    static void
    doApplyTranslate(VtMatrix4dArray &transforms, const VtIntArray &instanceIndices,
	    const void *primvar0, const void *primvar1, float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
	const V3	*seg0 = reinterpret_cast<const V3 *>(primvar0);
	const V3	*seg1 = reinterpret_cast<const V3 *>(primvar1);
	GfMatrix4d	 mat(1);
	GfVec3d		 xd;
	for (exint i = 0, n = transforms.size(); i < n; ++i)
	{
	    const V3	&x0 = seg0[instanceIndices[i]];

	    if (DO_INTERP)
	    {
		const V3	&x1 = seg1[instanceIndices[i]];
		lerpVec(xd.data(), x0.data(), x1.data(), lerp, 3);
	    }
	    else
	    {
		xd = GfVec3d(x0);
	    }

	    mat.SetTranslate(xd);
	    transforms[i] = mat * transforms[i];
	}
    }

    template <typename V4, bool DO_INTERP>
    static void
    doApplyRotate(VtMatrix4dArray &transforms, const VtIntArray &instanceIndices,
	    const void *primvar0, const void *primvar1, float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
	const V4	*seg0 = reinterpret_cast<const V4 *>(primvar0);
	const V4	*seg1 = reinterpret_cast<const V4 *>(primvar1);
	GfMatrix4d	 mat(1);
	GfQuaternion	 q;
	for (exint i = 0, n = transforms.size(); i < n; ++i)
	{
	    const V4	&x0 = seg0[instanceIndices[i]];
	    q = GfQuaternion(x0[0], GfVec3f(x0[1], x0[2], x0[3]));
	    if (DO_INTERP)
	    {
		const V4	&x1 = seg1[instanceIndices[i]];
		GfQuaternion	 q1(x1[0], GfVec3f(x1[1], x1[2], x1[3]));
		q = GfSlerp(q, q1, lerp);
	    }
	    mat.SetRotate(GfRotation(q));
	    transforms[i] = mat * transforms[i];
	}
    }

    template <typename V3, bool DO_INTERP>
    static void
    doApplyScale(VtMatrix4dArray &transforms, const VtIntArray &instanceIndices,
	    const void *primvar0, const void *primvar1, float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
	const V3	*seg0 = reinterpret_cast<const V3 *>(primvar0);
	const V3	*seg1 = reinterpret_cast<const V3 *>(primvar1);
	GfMatrix4d	 mat(1);
	GfVec3d		 xd;
	for (exint i = 0, n = transforms.size(); i < n; ++i)
	{
	    const V3	&x0 = seg0[instanceIndices[i]];
	    if (DO_INTERP)
	    {
		const V3 &x1 = seg1[instanceIndices[i]];
		lerpVec(xd.data(), x0.data(), x1.data(), lerp, 3);
	    }
	    else
	    {
		xd = GfVec3d(x0);
	    }
	    mat.SetScale(xd);
	    transforms[i] = mat * transforms[i];
	}
    }

    template <typename M4, bool DO_INTERP>
    static void
    doApplyTransform(VtMatrix4dArray &transforms, const VtIntArray &instanceIndices,
	    const void *primvar0, const void *primvar1, float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
	const M4	*seg0 = reinterpret_cast<const M4 *>(primvar0);
	const M4	*seg1 = reinterpret_cast<const M4 *>(primvar1);
	GfMatrix4d	xd;
	for (exint i = 0, n = transforms.size(); i < n; ++i)
	{
	    const M4	&x0 = seg0[instanceIndices[i]];
	    if (DO_INTERP)
	    {
		// TODO: Better interpolation
		const M4 &x1 = seg1[instanceIndices[i]];
		lerpVec(xd.data(), x0.data(), x1.data(), lerp, 16);
	    }
	    else
	    {
		xd = GfMatrix4d(x0);
	    }
	    transforms[i] = xd * transforms[i];
	}
    }

    // Macro to call transform functions with specializations for motion blur
    // or non-motion blurred interpolation.
    #define APPLY_FUNC(METHOD, IMPL) \
	template <typename V> static void \
	METHOD(VtMatrix4dArray &transforms, const VtIntArray &instanceIndices, \
		const void *primvar0, const void *primvar1, float lerp) \
	{ \
	    if (primvar0 != primvar1 && lerp != 0) { \
		IMPL<V, true>(transforms, instanceIndices, \
			primvar0, primvar1, lerp); \
	    } else { \
		IMPL<V, false>(transforms, instanceIndices, \
			primvar0, primvar1, 0); \
	    } \
	} \
	/* end of macro */

    APPLY_FUNC(applyTranslate, doApplyTranslate)
    APPLY_FUNC(applyRotate, doApplyRotate)
    APPLY_FUNC(applyScale, doApplyScale)
    APPLY_FUNC(applyTransform, doApplyTransform)
    #undef APPLY_FUNC

} // Namespace

XUSD_HydraInstancer::XUSD_HydraInstancer(HdSceneDelegate* delegate,
					 SdfPath const& id,
					 SdfPath const &parentId)
    : HdInstancer(delegate, id, parentId)
    , myIsPointInstancer(false)
    , myXTimes()
    , myPTimes()
    , myNSegments(0)
    , myXSegments(0)
    , myPSegments(0)
{
}

XUSD_HydraInstancer::~XUSD_HydraInstancer()
{
}

int
XUSD_HydraInstancer::syncPrimvars(bool recurse, int nsegs)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdChangeTracker &changeTracker =
        GetDelegate()->GetRenderIndex().GetChangeTracker();
    SdfPath const& id = GetId();

    // Use the double-checked locking pattern to check if this instancer's
    // primvars are dirty.
    int dirtyBits = changeTracker.GetInstancerDirtyBits(id);
    // Double lock
    if (HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, id)
	    || HdChangeTracker::IsTransformDirty(dirtyBits, id))
    {
	UT_Lock::Scope	lock(myLock);

	nsegs = SYSmax(nsegs, 1);
	if (myNSegments != nsegs)
	{
	    // In theory, USD can return segments outside my segment
	    // bounds, so we need o allocate two extra time samples.
	    UT_UniquePtr<float[]> ptimes = UTmakeUnique<float[]>(nsegs+2);
	    UT_UniquePtr<float[]> xtimes = UTmakeUnique<float[]>(nsegs+2);
	    UT_UniquePtr<GfMatrix4d[]> xforms = UTmakeUnique<GfMatrix4d[]>(nsegs+2);
	    std::fill(ptimes.get(), ptimes.get()+nsegs+2, 0.0f);
	    std::fill(xtimes.get(), xtimes.get()+nsegs+2, 0.0f);
	    std::fill(xforms.get(), xforms.get()+nsegs+2, GfMatrix4d(1.0));
	    if (myNSegments)
	    {
		// We already have arrays allocated
		int copysz = SYSmin(nsegs, myNSegments);
		std::copy(myPTimes.get(), myPTimes.get()+copysz, ptimes.get());
		std::copy(myXTimes.get(), myXTimes.get()+copysz, xtimes.get());
		std::copy(myXforms.get(), myXforms.get()+copysz, xforms.get());
	    }
	    else
	    {
		UT_ASSERT(!myPTimes);
	    }
	    myPTimes = std::move(ptimes);
	    myXTimes = std::move(xtimes);
	    myXforms = std::move(xforms);
	    myNSegments = nsegs;
	    // If we've shrunk our segment arrays, we need to make sure the
	    // ptimes and xtimes reflect this (so we don't access bad memory).
	    // This is for the case where only one of primvars or xforms are
	    // dirty.
	    myPSegments = SYSmin(myPSegments, myNSegments);
	    myXSegments = SYSmin(myXSegments, myXSegments);
	}

        dirtyBits = changeTracker.GetInstancerDirtyBits(id);

	if (HdChangeTracker::IsTransformDirty(dirtyBits, id))
	{
	    // Compute the number of transform motion segments.
	    //
	    // Since this instancer can be shared by many prototypes, it's more
	    // efficient for us to cache the transforms rather than calling in
	    // privComputeTransforms.  This is especially true when there's
	    // motion blur and Hydra has to traverse the instancer hierarchy to
	    // compute the proper motion segements for blur.
	    if (myNSegments == 1)
	    {
		myXSegments = 1;
		myXforms[0] = GetDelegate()->GetInstancerTransform(GetId());
	    }
	    else
	    {
		myXSegments = GetDelegate()->SampleInstancerTransform(GetId(),
			myNSegments, myXTimes.get(), myXforms.get());
	    }
	}

        if (HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, id))
	{
            // If this instancer has dirty primvars, get the list of
            // primvar names and then cache each one.
            HdPrimvarDescriptorVector primvarDescriptors;
            primvarDescriptors = GetDelegate()->
		GetPrimvarDescriptors(id, HdInterpolationInstance);

	    UT_SmallArray<VtValue>	uvalues;
	    UT_SmallArray<float>	utimes;
	    uvalues.bumpSize(nsegs);
	    utimes.bumpSize(nsegs);

            for (auto &&descriptor : primvarDescriptors)
	    {
		const auto	&name = descriptor.name;
                if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, name))
		{
		    exint	usegs;
		    if (nsegs == 1)
		    {
			uvalues[0] = GetDelegate()->Get(id, name);
			usegs = uvalues[0].IsEmpty() ? 0 : 1;
		    }
		    else
		    {
			usegs = GetDelegate()->SamplePrimvar(id, name, nsegs,
					utimes.data(), uvalues.data());
			if (usegs > nsegs)
			{
			    utimes.bumpSize(usegs);
			    uvalues.bumpSize(usegs);
			    usegs = GetDelegate()->SamplePrimvar(id, name, usegs,
					    utimes.data(), uvalues.data());
			}
			// We assume all primvars are either constant (one
			// segment) or have a consistent number of segments.
			// @c usegs should be either 1 or the number of USD
			// motion segments (or we haven't set the number of
			// segments yet).
			UT_ASSERT(usegs == 1 || usegs == myPSegments || myPSegments == 0);

			// NOTE:  The Get() function magically translates
			// GfQuath to GfVec4f, which also changes the layout of
			// the code.  Currently, this is required since
			// HdVtBufferSource can't hold a quaternion.
			// See: pointInstancerAdapter.cpp:779 or so...
			for (exint i = 0; i < usegs; ++i)
			{
			    UT_ASSERT(!uvalues[i].IsEmpty());
			    uvalues[i] = patchQuaternion(uvalues[i]);
			}
			if (usegs > 1 && usegs > myPSegments)
			{
			    UT_ASSERT(usegs < myNSegments+2);
			    myPSegments = usegs;
			    std::copy(utimes.data(), utimes.data()+usegs,
				    myPTimes.get());
			}
			else if (myPSegments > 0)
			{
			    UT_ASSERT_P(std::equal(utimes.data(),
					utimes.data()+usegs,
					myPTimes.get()));
			}
		    }
                    if (usegs > 0)
		    {
			PrimvarMapItem	vals(usegs);
			for (exint i = 0; i < usegs; ++i)
			{
			    vals.setBuffer(i,
				    UTmakeUnique<HdVtBufferSource>(name, uvalues[i]));
			}
			myPrimvarMap.erase(name);
                        myPrimvarMap.emplace(name, std::move(vals));
                    }
                }
            }

            // Mark the instancer as clean
            changeTracker.MarkInstancerClean(id);
        }
    }

    if(recurse)
    {
        auto pid = GetParentId();
        if(!pid.IsEmpty())
        {
            auto xinst = GetDelegate()->GetRenderIndex().GetInstancer(pid);
            if(xinst)
                UTverify_cast<XUSD_HydraInstancer *>(xinst)->syncPrimvars(true);
        }
    }
    UT_ASSERT(motionSegments() > 0);
    return motionSegments();
}

static inline void
splitSegment(int nsegs, const float *sample_times,
	float time, int &seg0, int &seg1, float &lerp)
{
    switch (nsegs)
    {
	case 0:
	case 1:
	    // No motion blur
	    seg0 = seg1 = 0;
	    lerp = 0;
	    break;
	case 2:
	    // Linear blur between two segments
	    seg0 = 0;
	    seg1 = 1;
	    lerp = SYSefit(time, sample_times[0], sample_times[1], 0.0f, 1.0f);
	    break;
	default:
	{
	    auto &&seg = std::upper_bound(sample_times+1,
		    sample_times+nsegs, lerp);
	    seg1 = seg - sample_times;
	    if (seg1 == nsegs)
	    {
		seg0 = seg1 = nsegs - 1;
		lerp = 0;
	    }
	    else
	    {
		seg0 = seg1-1;	// Previous segment
		lerp = SYSefit(time,
			sample_times[seg0], sample_times[seg1], 0.0f, 1.0f);
	    }
	    break;
	}
    }
}

void
XUSD_HydraInstancer::getSegment(float time,
	int &seg0, int &seg1, float &lerp, bool for_xform) const
{
    if (for_xform)
	splitSegment(xsegments(), xtimes(), time, seg0, seg1, lerp);
    else
	splitSegment(psegments(), ptimes(), time, seg0, seg1, lerp);
}

#define IS_TYPE(BUF, TYPE) (BUF->GetTupleType() == HdTupleType{TYPE,1})
VtMatrix4dArray
XUSD_HydraInstancer::privComputeTransforms(const SdfPath    &prototypeId,
                                           bool              recurse,
                                           const GfMatrix4d *protoXform,
                                           int               level,
                                           UT_StringArray   *instances,
                                           UT_IntArray      *ids,
                                           HUSD_Scene       *scene,
					   float	     shutter_time)
{
    // The transforms for this level of instancer are computed by:
    // foreach(index : indices) {
    //     instancerTransform * translate(index) * rotate(index) *
    //     scale(index) * instanceTransform(index)
    // }
    // If any transform isn't provided, it's assumed to be the identity.

    VtIntArray instanceIndices =
		    GetDelegate()->GetInstanceIndices(GetId(), prototypeId);
    const int num_inst = instanceIndices.size();

    UT_StringArray inames;
    const bool write_inst = instances && (level == 0);

    HdInstancer *parent_instancer = nullptr;
    VtMatrix4dArray parent_transforms;
    UT_StringArray parent_names;

    if (recurse && !GetParentId().IsEmpty())
        parent_instancer =
            GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());

    if (parent_instancer)
    {
        parent_transforms =
            UTverify_cast<XUSD_HydraInstancer *>(parent_instancer)->
                privComputeTransforms(GetId(), true, nullptr, level-1,
                                      &parent_names, nullptr,
				      scene, shutter_time);
        // If we have a parent, but that parent has no transforms (i.e. all
        // its instances are hidden) then this instancer is also hidden, so
        // we should immediately return with no transforms.
        //
        // This fixes a crash caused by calling GetPathForInstanceIndex on
        // our (indirectly invisible) instances.
        if (parent_transforms.size() == 0)
            return parent_transforms;
    }

    if(num_inst > 0)
    {
        int absi = -1;

        // On Windows release builds, calling this method appears not to be
        // thread safe (crashes have not been observed on other platforms,
        // but Windows release builds crash consistently when manipulating
        // (even translating) point instancers with multiple prototypes.
        {
            UT_Lock::Scope	lock(myLock);
            GetDelegate()->GetPathForInstanceIndex(prototypeId,
                                                   instanceIndices[0],
                                                   &absi);
        }

        if(absi == -1)
        {
            // not a point intstancer.
            myIsPointInstancer = false;
            for(int i=0; i<num_inst; i++)
            {
                const int idx = instanceIndices[i];
                SdfPath path = GetDelegate()->
                    GetPathForInstanceIndex(prototypeId, idx, 0,0,0);

                inames.append(path.GetText());
                if(write_inst)
                    instances->append(inames.last());
            }
        }
        else // point instancer
        {
            myIsPointInstancer = true;
            const char *base = GetId().GetText();
            UT_WorkBuffer buf;
            for(int i=0; i<num_inst; i++)
            {
                const int idx = instanceIndices[i];

                if(level == 0)
                    buf.sprintf("%s[%d]", base, idx);
                else
                    buf.sprintf("[%d]", idx);

                inames.append(buf.buffer());
                if(write_inst)
                    instances->append(inames.last());
            }
        }
    }

    // Get motion blur interpolants
    int seg0, seg1;
    float shutter;

    VtMatrix4dArray	transforms(num_inst);
    GfMatrix4d		ixform;
    if (xsegments() <= 1)
	ixform = myXforms[0];
    else
    {
	getSegment(shutter_time, seg0, seg1, shutter, true);
	int s0 = SYSmin(seg0, xsegments()-1);
	int s1 = SYSmin(seg1, xsegments()-1);
	lerpVec(ixform.data(),
		myXforms[s0].data(), myXforms[s1].data(), shutter, 16);
    }
    std::fill(transforms.begin(), transforms.end(), ixform);

    // Note that we do not need to lock myLock here to access myPrimvarMap.
    // The syncPrimvars method should be called before this method to build
    // myPrimvarMap, but it guarantees that only one thread (the first one to
    // make it through that method) will change myPrimvarMap. So by the time
    // any thread reaches this point, it is guaranteed that no other threads
    // will be modifying myPrimvarMap.

    getSegment(shutter_time, seg0, seg1, shutter, false);

    // "translate" holds a translation vector for each index.
    auto &&vitt = myPrimvarMap.find(HusdHdPrimvarTokens()->translate);
    if (vitt != myPrimvarMap.end())
    {
	auto &vart = vitt->second;
	int  s0 = SYSmin(seg0, vart.size()-1);
	int  s1 = SYSmin(seg1, vart.size()-1);
	if(IS_TYPE(vart[s0], HdTypeFloatVec3))
	{
	    applyTranslate<GfVec3f>(transforms, instanceIndices,
		    vart[s0]->GetData(), vart[s1]->GetData(), shutter);
        }
	else if(IS_TYPE(vart[s0], HdTypeDoubleVec3))
	{
	    applyTranslate<GfVec3d>(transforms, instanceIndices,
		    vart[s0]->GetData(), vart[s1]->GetData(), shutter);
        }
	else if(IS_TYPE(vart[s0], HdTypeHalfFloatVec3))
	{
	    applyTranslate<GfVec3h>(transforms, instanceIndices,
		    vart[s0]->GetData(), vart[s1]->GetData(), shutter);
        }
	else
	{
	    UT_ASSERT(0 && "Unknown translate buffer type");
	}
    }

    // "rotate" holds a quaternion in <real, i, j, k> format for each index.
    auto &&vitr = myPrimvarMap.find(HusdHdPrimvarTokens()->rotate);
    if (vitr != myPrimvarMap.end())
    {
	auto &varr = vitr->second;
	int  s0 = SYSmin(seg0, varr.size()-1);
	int  s1 = SYSmin(seg1, varr.size()-1);
	if(IS_TYPE(varr[s0], HdTypeFloatVec4))
	{
	    applyRotate<GfVec4f>(transforms, instanceIndices,
		    varr[s0]->GetData(), varr[s1]->GetData(), shutter);
	}
	else if(IS_TYPE(varr[s0], HdTypeHalfFloatVec4))
	{
	    applyRotate<GfVec4h>(transforms, instanceIndices,
		    varr[s0]->GetData(), varr[s1]->GetData(), shutter);
	}
	else if(IS_TYPE(varr[s0], HdTypeDoubleVec4))
	{
	    applyRotate<GfVec4d>(transforms, instanceIndices,
		    varr[s0]->GetData(), varr[s1]->GetData(), shutter);
	}
	else
	{
	    UT_ASSERT(0 && "Unknown rotate buffer type");
	}
    }

    // "scale" holds an axis-aligned scale vector for each index.
    auto &&vits = myPrimvarMap.find(HusdHdPrimvarTokens()->scale);
    if (vits != myPrimvarMap.end())
    {
	auto &vars = vits->second;
	int  s0 = SYSmin(seg0, vars.size()-1);
	int  s1 = SYSmin(seg1, vars.size()-1);
	if(IS_TYPE(vars[s0], HdTypeFloatVec3))
	{
	    applyScale<GfVec3f>(transforms, instanceIndices,
		    vars[s0]->GetData(), vars[s1]->GetData(), shutter);
	}
	else if(IS_TYPE(vars[s0], HdTypeDoubleVec3))
	{
	    applyScale<GfVec3d>(transforms, instanceIndices,
		    vars[s0]->GetData(), vars[s1]->GetData(), shutter);
	}
	else if(IS_TYPE(vars[s0], HdTypeHalfFloatVec3))
	{
	    applyScale<GfVec3h>(transforms, instanceIndices,
		    vars[s0]->GetData(), vars[s1]->GetData(), shutter);
	}
	else
	{
	    UT_ASSERT(0 && "Unknown scale buffer type");
	}
    }

    // "instanceTransform" holds a 4x4 transform matrix for each index.
    auto &&viti = myPrimvarMap.find(HusdHdPrimvarTokens()->instanceTransform);
    if (viti != myPrimvarMap.end())
    {
	auto &vari = viti->second;
	int  s0 = SYSmin(seg0, vari.size()-1);
	int  s1 = SYSmin(seg1, vari.size()-1);
	if(IS_TYPE(vari[s0], HdTypeFloatMat4))
	{
	    applyTransform<GfMatrix4f>(transforms, instanceIndices,
		    vari[s0]->GetData(), vari[s1]->GetData(), shutter);
	}
	else if(IS_TYPE(vari[s0], HdTypeDoubleMat4))
	{
	    applyTransform<GfMatrix4d>(transforms, instanceIndices,
		    vari[s0]->GetData(), vari[s1]->GetData(), shutter);
        }
	else
	{
	    UT_ASSERT(0 && "Unknown transform type");
	}
    }

    if (protoXform)
    {
	for (size_t i = 0; i < num_inst; ++i)
	    transforms[i] = (*protoXform) * transforms[i];
    }

    if (!parent_instancer)
    {
        if(ids && ids->entries() == 0)
        {
            const int nids = transforms.size();
            ids->entries(nids);

            for (size_t i = 0; i < nids; ++i)
                (*ids)[i] = scene->getOrCreateID(inames[i]);
        }
        return transforms;
    }

    VtMatrix4dArray final(parent_transforms.size() * transforms.size());
    const int stride = transforms.size();
    if(ids)
    {
        ids->entries(parent_transforms.size() * stride);
        for (size_t i = 0; i < parent_transforms.size(); ++i)
            for (size_t j = 0; j < stride; ++j)
            {
                final[i * stride + j] = transforms[j] * parent_transforms[i];

                UT_WorkBuffer path;
                path.sprintf("%s%s",
                             parent_names[i].c_str(),
                             inames[j].c_str());
                UT_StringRef spath(path.buffer());
                (*ids)[i*stride + j] = scene->getOrCreateID(spath);
                if(instances)
                    instances->append(spath);
            }
    }
    else if(instances)
    {
        for (size_t i = 0; i < parent_transforms.size(); ++i)
            for (size_t j = 0; j < stride; ++j)
            {
                final[i * stride + j] =  transforms[j] * parent_transforms[i];

                UT_WorkBuffer path;
                path.sprintf("%s%s",
                             parent_names[i].c_str(),
                             inames[j].c_str());
                instances->append(path.buffer());
            }
    }
    else
    {
        for (size_t i = 0; i < parent_transforms.size(); ++i)
            for (size_t j = 0; j < stride; ++j)
                final[i * stride + j] =  transforms[j] * parent_transforms[i];
    }

    return final;
}
VtMatrix4dArray
XUSD_HydraInstancer::computeTransforms(const SdfPath    &protoId,
                                       bool              recurse,
                                       const GfMatrix4d *protoXform,
				       float		 shutter)
{
    return privComputeTransforms(protoId, recurse, protoXform,
	    0, nullptr, nullptr, nullptr, shutter);
}

VtMatrix4dArray
XUSD_HydraInstancer::computeTransformsAndIDs(const SdfPath    &protoId,
                                             bool              recurse,
                                             const GfMatrix4d *protoXform,
                                             int               level,
                                             UT_IntArray      &ids,
                                             HUSD_Scene       *scene,
					     float	       shutter)
{
    return privComputeTransforms(protoId, recurse, protoXform, level, nullptr,
                                 &ids, scene, shutter);
}

PXR_NAMESPACE_CLOSE_SCOPE
