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
#include "HUSD_HydraGeoPrim.h"
#include "HUSD_Path.h"
#include "HUSD_Scene.h"

#include <UT/UT_Debug.h>
#include <UT/UT_StopWatch.h>

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
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d      mat(1);
                GfVec3d         xd;
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
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
        );
    }

    template <typename V4, bool DO_INTERP>
    static void
    doApplyRotate(VtMatrix4dArray &transforms, const VtIntArray &instanceIndices,
	    const void *primvar0, const void *primvar1, float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
	const V4	*seg0 = reinterpret_cast<const V4 *>(primvar0);
	const V4	*seg1 = reinterpret_cast<const V4 *>(primvar1);
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d	 mat(1);
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
                {
                    const V4	&x0 = seg0[instanceIndices[i]];
                    GfQuatd     q = GfQuatd(x0[0], GfVec3d(x0[1], x0[2], x0[3]));
                    if (DO_INTERP)
                    {
                        const V4	&x1 = seg1[instanceIndices[i]];
                        GfQuatd	         q1(x1[0], GfVec3d(x1[1], x1[2], x1[3]));
                        q = GfSlerp(q, q1, lerp);
                    }
                    // Note: we want to use GfQuatd here to avoid the GfRotation
                    // overload, which would introduce a conversion to axis-angle and
                    // back. GfRotation is also incorrect if the input is not
                    // normalized (Bug 102229).
                    mat.SetRotate(q);
                    transforms[i] = mat * transforms[i];
                }
            }
        );
    }

    template <typename V3, bool DO_INTERP>
    static void
    doApplyScale(VtMatrix4dArray &transforms, const VtIntArray &instanceIndices,
	    const void *primvar0, const void *primvar1, float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
	const V3	*seg0 = reinterpret_cast<const V3 *>(primvar0);
	const V3	*seg1 = reinterpret_cast<const V3 *>(primvar1);
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d	 mat(1);
                GfVec3d		 xd;
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
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
        );
    }

    template <typename M4, bool DO_INTERP>
    static void
    doApplyTransform(VtMatrix4dArray &transforms, const VtIntArray &instanceIndices,
	    const void *primvar0, const void *primvar1, float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
	const M4	*seg0 = reinterpret_cast<const M4 *>(primvar0);
	const M4	*seg1 = reinterpret_cast<const M4 *>(primvar1);
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d	xd;
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
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
        );
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
    , myIsResolved(false)
    , myIsPointInstancer(false)
    , myXTimes()
    , myPTimes()
    , myXforms()
    , myID(HUSD_HydraPrim::newUniqueId())
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
            myXTimes.setSize(nsegs);
            myXforms.setSize(nsegs);
	    if (nsegs == 1)
	    {
                myXTimes[0] = 0;
		myXforms[0] = GetDelegate()->GetInstancerTransform(GetId());
	    }
	    else
	    {
		exint nx = GetDelegate()->SampleInstancerTransform(GetId(),
			myXTimes.size(), myXTimes.data(), myXforms.data());
                if (nx < myXforms.size())
                {
                    // USD has fewer segments than we requested, so shrink our
                    // arrays.
                    myXTimes.setSize(nx);
                    myXforms.setSize(nx);
                }
                else if (nx > myXforms.size())
                {
                    // USD has more samples, so we need to grow the arrays
                    myXTimes.setSize(nx);
                    myXforms.setSize(nx);
                    nx = GetDelegate()->SampleInstancerTransform(GetId(),
                        myXTimes.size(), myXTimes.data(), myXforms.data());
                    UT_ASSERT(nx == myXforms.size());
                }
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
                        // segments yet).  This has failed is when:
                        // a) there's a string primvar, which had the same
                        //    value over all segments (see below)
                        // b) a transform primvar which had a single segment at
                        //    a non-integer time sample (bug 109654)
			UT_ASSERT(usegs == 1
                                || usegs == 2   // Linear interpolation
                                || usegs == psegments()
                                || psegments() == 0);

                        if (usegs > 1 && usegs < psegments())
                        {
                            // The only time I've seen this is with string
                            // values that are the same for every segment
                            for (int i = 1; i < usegs; ++i)
                                UT_ASSERT(uvalues[i] == uvalues[0]);
                            // Extend the last value to the end
                            uvalues.bumpSize(psegments());
                            std::fill(uvalues.data()+usegs,
                                    uvalues.data()+psegments(),
                                    uvalues.data()[usegs-1]);
                            std::copy(myPTimes.begin(), myPTimes.end(),
                                    utimes.begin());
                            usegs = psegments();
                        }

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
			if (usegs > 1 && usegs > myPTimes.size())
			{
                            myPTimes.setSize(usegs);
			    std::copy(utimes.begin(), utimes.end(),
				    myPTimes.data());
			}
			else if (psegments() > 0)
			{
			    UT_ASSERT_P(std::equal(utimes.data(),
					utimes.data()+usegs,
					myPTimes.data()));
			}
                        // Currently, SamplePrimvar() doesn't flush the value
                        // from the cache, so we need to do this explicitly
                        // with a call to Get().
			GetDelegate()->Get(id, name);
		    }
                    if (usegs > 0)
		    {
			PrimvarMapItem	vals(usegs);

			for (exint i = 0; i < usegs; ++i)
			{
			    vals.setValueAndBuffer(i, uvalues[i],
                                UTmakeUnique<HdVtBufferSource>(
                                    name, uvalues[i]));
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
		    sample_times+nsegs, time);
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
					   float	     shutter_time,
                                           int               hou_proto_id)
{
    // The transforms for this level of instancer are computed by:
    // foreach(index : indices) {
    //     instancerTransform * translate(index) * rotate(index) *
    //     scale(index) * instanceTransform(index)
    // }
    // If any transform isn't provided, it's assumed to be the identity.
    HUSD_Path ppath(prototypeId);
    UT_StringHolder proto_path = ppath.pathStr();
    HUSD_Path ipath(GetId());
    UT_StringHolder inst_path = ipath.pathStr();

    /// BEGIN LOCKED SECTION
    myLock.lock();
    myResolvedInstances.clear();
    myIsResolved = false;

    myPrototypeID[hou_proto_id] = proto_path;
    myLock.unlock();
    /// END LOCKED SECTION

    VtIntArray instanceIndices =
		    GetDelegate()->GetInstanceIndices(GetId(), prototypeId);
    const int num_inst = instanceIndices.size();

    //UTdebugPrint("Recompute transforms", GetId().GetText(), "#inst", num_inst);
    UT_StringArray inames;

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

    {
        // Lock while accessing myPrototypes
        UT_Lock::Scope  lock(myLock);
        auto &proto_indices = myPrototypes[inst_path];
        if(num_inst > 0)
        {
            UT_AutoLock lock_scope(myLock);
            UT_WorkBuffer buf;
            for(int i=0; i<num_inst; i++)
            {
                const int idx = instanceIndices[i];
                proto_indices[idx] = 1;
                
                buf.sprintf("%d", myIsPointInstancer ? idx : i);
                inames.append(buf.buffer());
                if(instances && !ids)
                    instances->append(inames.last());
            }
        }
        else
            proto_indices.clear();
    }

    // Get motion blur interpolants
    int seg0, seg1;
    float shutter;

    VtMatrix4dArray	transforms(num_inst);
    GfMatrix4d		ixform;
    switch (xsegments())
    {
        case 0:
            ixform = GfMatrix4d(1.0);
            break;
        case 1:
            ixform = myXforms[0];
            break;
        default:
            getSegment(shutter_time, seg0, seg1, shutter, true);
            int s0 = SYSmin(seg0, xsegments()-1);
            int s1 = SYSmin(seg1, xsegments()-1);
            lerpVec(ixform.data(),
                    myXforms[s0].data(), myXforms[s1].data(), shutter, 16);
            break;
    }
    std::fill(transforms.begin(), transforms.end(), ixform);

    // Note that we do not need to lock myLock here to access myPrimvarMap.
    // The syncPrimvars method should be called before this method to build
    // myPrimvarMap, but it guarantees that only one thread (the first one to
    // make it through that method) will change myPrimvarMap. So by the time
    // any thread reaches this point, it is guaranteed that no other threads
    // will be modifying myPrimvarMap.

    getSegment(shutter_time, seg0, seg1, shutter, false);

    UTisolate([&]()
    {
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
    });

    if (protoXform)
    {
	for (size_t i = 0; i < num_inst; ++i)
	    transforms[i] = (*protoXform) * transforms[i];
    }

    if (!parent_instancer)
    {
        if(ids && ids->entries() != transforms.size())
        {
            UT_StringHolder prefix;
            prefix.sprintf("?%d %d ", id(), hou_proto_id);
            
            const int nids = transforms.size();
            ids->entries(nids);

            for (size_t i = 0; i < nids; ++i)
            {
                UT_WorkBuffer nameb;
                UT_StringRef path;
                
                nameb.sprintf("%s%s", prefix.c_str(), inames(i).c_str());
                path = nameb.buffer();
                
                if(instances)
                    instances->append(path);
                (*ids)[i] = scene->getOrCreateInstanceID(path, inst_path,
                                                         proto_path);
            }

            return transforms;
        }

        // Top level transforms
        return transforms;
    }

    VtMatrix4dArray final(parent_transforms.size() * transforms.size());
    const int stride = transforms.size();
    if(ids)
    {
        UT_StringHolder prefix;
        prefix.sprintf("?%d %d", id(), hou_proto_id);
        
        ids->entries(parent_transforms.size() * stride);
        for (size_t i = 0; i < parent_transforms.size(); ++i)
            for (size_t j = 0; j < stride; ++j)
            {
                final[i * stride + j] = transforms[j] * parent_transforms[i];

                UT_WorkBuffer path;
                path.sprintf("%s %s %s", prefix.c_str(),
                             parent_names[i].c_str(),
                             inames[j].c_str());

                UT_StringRef spath(path.buffer());
                (*ids)[i*stride + j] =
                    scene->getOrCreateInstanceID(spath, inst_path, proto_path);
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
                path.sprintf("%s %s",
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
                                 0, nullptr, nullptr, nullptr, shutter, -1);
}

VtMatrix4dArray
XUSD_HydraInstancer::computeTransformsAndIDs(const SdfPath    &protoId,
                                             bool              recurse,
                                             const GfMatrix4d *protoXform,
                                             int               level,
                                             UT_IntArray      &ids,
                                             HUSD_Scene       *scene,
					     float	       shutter,
                                             int               hou_proto_id)
{
    return privComputeTransforms(protoId, recurse, protoXform, level, nullptr,
                                 &ids, scene, shutter, hou_proto_id);
}

const UT_StringRef &
XUSD_HydraInstancer::getCachedResolvedInstance(const UT_StringRef &id_key)
{
    static UT_StringRef theEmptyRef;
    
    auto entry = myResolvedInstances.find(id_key);
    if(entry != myResolvedInstances.end())
        return entry->second;

    return theEmptyRef;
}

void
XUSD_HydraInstancer::cacheResolvedInstance(const UT_StringRef &id_key,
                                           const UT_StringRef &resolved)
{
    myResolvedInstances[id_key] = resolved;
}

UT_StringArray
XUSD_HydraInstancer::resolveInstance(int proto_id,
                                     const UT_IntArray &indices,
                                     int index_level)
{
    UT_StringArray instances;

    if(myIsPointInstancer)
    {
        // Point instancer.
        HUSD_Path hpath(GetId());
        UT_StringHolder ipath(hpath.pathStr());
        UT_WorkBuffer inst;
        inst.sprintf("[%d]", indices(index_level));
        
        auto *pinst=GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());

        if(pinst)
        {
            index_level++;
            if(indices.isValidIndex(index_level))
            {
                instances = UTverify_cast<XUSD_HydraInstancer *>(pinst)->
                    resolveInstance(id(), indices, index_level);
            }
            else
                instances.append(UTverify_cast<XUSD_HydraInstancer *>(pinst)->
                                 findParentInstancer());

        }
        else
            instances.append(ipath);

        for(auto &i : instances)
            i += inst.buffer();
    }
    else
    {
        auto p = myPrototypeID.find(proto_id);
        if(p != myPrototypeID.end())
        {
            SdfPath prototype_id(p->second.toStdString());
            SdfPath primpath;
            primpath = GetDelegate()->GetScenePrimPath(prototype_id,
                                                       indices(index_level));
            HUSD_Path hpath(primpath);
            instances.append(hpath.pathStr());
        }
    }
    
    return instances;
}

UT_StringHolder
XUSD_HydraInstancer::findParentInstancer() const
{
    if(GetParentId().IsEmpty())
    {
        HUSD_Path hpath(GetId());
        return hpath.pathStr();
    }
    
    auto *pinst=GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
    return UTverify_cast<XUSD_HydraInstancer *>(pinst)->findParentInstancer();
}


UT_StringArray
XUSD_HydraInstancer::resolveInstanceID(HUSD_Scene &scene,
                                       const UT_StringRef &houdini_inst_path,
                                       int instance_idx,
                                       UT_StringHolder &child_indices,
                                       UT_StringArray *proto_id) const
{
    UT_StringArray result;
    int index = -1;
    int end_instance = houdini_inst_path.findCharIndex(']', instance_idx);
    if(end_instance != -1 && instance_idx != -1)
    {
        UT_StringHolder digit(houdini_inst_path.c_str() + instance_idx+1,
                              end_instance-instance_idx-1);
        index = SYSatoi(digit.c_str());
    }

    for(auto &prototype : myPrototypes)
    {
        // UTdebugPrint(index, "Proto", prototype.first);
        UT_StringArray proto;
        UT_StringHolder indices;
        
        auto child_instr = scene.getInstancer(prototype.first);
        if(child_instr && child_instr != this)
        {
            //UTdebugPrint("Resolve child instancer");
            const int next_instance=
                houdini_inst_path.findCharIndex('[',end_instance);
            child_instr->resolveInstanceID(scene, houdini_inst_path,
                                           next_instance, indices, &proto);
        }
        else
        {
            int pid = -1;
            auto entry = scene.geometry().find(prototype.first);
            if(entry != scene.geometry().end())
                pid = entry->second->id();

            HUSD_Path hpath(GetId());
            HUSD_Path ppath(prototype.first);
            UT_WorkBuffer buf;
            buf.sprintf("?%d %d ", id(), pid);
            proto.append(buf.buffer());
        }
            
        UT_WorkBuffer key;
        if(proto_id)
        {
            if(index != -1)
            {
                key.sprintf(" %d%s", index, indices.c_str());
                child_indices = key.buffer();
            }
            for(auto &p : proto)
                proto_id->append(p);
        }
        else
        {
            UT_ASSERT(index != -1);
            for(auto &p : proto)
            {
                key.sprintf("%s %d%s",
                            p.c_str(),
                            index,
                            indices.c_str());
                result.append(key.buffer());
            }
        }
    }

    return result;
}


void
XUSD_HydraInstancer::removePrototype(const UT_StringRef &proto_path,
                                     int id)
{
    UT_StringHolder path(proto_path);
    UT_AutoLock locker(myLock);
    myPrototypes.erase(path);
    myPrototypeID.erase(id);
}

void
XUSD_HydraInstancer::addInstanceRef(int id)
{
    myInstanceRefs[id] = 1;
}

bool
XUSD_HydraInstancer::invalidateInstanceRefs()
{
    for(auto &itr : myInstanceRefs)
        itr.second = 0;

    return myInstanceRefs.size() > 0;
}

const UT_Map<int,int> &
XUSD_HydraInstancer::instanceRefs() const
{
    return myInstanceRefs;
}

void
XUSD_HydraInstancer::removeInstanceRef(int id)
{
    myInstanceRefs.erase(id);
}

void
XUSD_HydraInstancer::clearInstanceRefs()
{
    myInstanceRefs.clear();
}

const VtValue &
XUSD_HydraInstancer::primvarValue(const TfToken &name) const
{
    auto it = myPrimvarMap.find(name);

    if (it == myPrimvarMap.end())
    {
        static VtValue theEmptyValue;
        return theEmptyValue;
    }

    return it->second.value(0);
}

PXR_NAMESPACE_CLOSE_SCOPE
