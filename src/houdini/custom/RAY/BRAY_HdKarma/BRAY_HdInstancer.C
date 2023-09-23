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

#include "BRAY_HdInstancer.h"
#include "BRAY_HdFormat.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/usdGeom/tokens.h>

#include <UT/UT_Debug.h>
#include <UT/UT_Set.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_VarEncode.h>
#include <UT/UT_JSONWriter.h>
#include <SYS/SYS_TypeTraits.h>
#include "BRAY_HdUtil.h"
#include "BRAY_HdParam.h"

PXR_NAMESPACE_OPEN_SCOPE

#if 0
// Define local tokens for the names of the primvars the instancer
// consumes.
// XXX: These should be hydra tokens...
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (instanceTransform)
    (rotate)
    (scale)
    (translate)
);
#endif

namespace
{
    template <typename D, typename S>
    static void
    lerpVec(D *dest, const S *s0, const S *s1, float lerp, int n)
    {
	for (int i = 0; i < n; ++i)
	    dest[i] = SYSlerp(s0[i], s1[i], S(lerp));
    }

    static UT_Set<TfToken> &
    transformTokens()
    {
	static UT_Set<TfToken>	theTokens({
#if 0
                // We need to pick these up for computeTransform()
		HdInstancerTokens->translate,
		HdInstancerTokens->rotate,
		HdInstancerTokens->scale,
		HdInstancerTokens->instanceTransform,
#endif
                HdTokens->velocities,
                HdTokens->accelerations,
                //UsdGeomTokens->angularVelocities
	});
	return theTokens;
    }

    template <typename V3, bool DO_INTERP>
    static void
    doApplyTranslate(UT_Array<GfMatrix4d> &transforms,
            const VtIntArray &instanceIndices,
	    const V3 *seg0,
            const V3 *seg1,
            float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
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
    doApplyRotate(UT_Array<GfMatrix4d> &transforms,
            const VtIntArray &instanceIndices,
	    const V4 *seg0,
            const V4 *seg1,
            float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
        UTparallelFor(UT_BlockedRange<exint>(0, transforms.size()),
            [&](const UT_BlockedRange<exint> &r)
            {
                GfMatrix4d	 mat(1);
                for (exint i = r.begin(), n = r.end(); i < n; ++i)
                {
                    const V4 &x0 = seg0[instanceIndices[i]];
                    GfQuatd   q = GfQuatd(x0[3], GfVec3d(x0[0], x0[1], x0[2]));
                    if (DO_INTERP)
                    {
                        const V4 &x1 = seg1[instanceIndices[i]];
                        GfQuatd	  q1(x1[3], GfVec3d(x1[0], x1[1], x1[2]));
                        q = GfSlerp(q, q1, lerp);
                    }
                    // Note: we want to use GfQuatd here to avoid the
                    // GfRotation overload, which would introduce a conversion
                    // to axis-angle and back. GfRotation is also incorrect if
                    // the input is not normalized (Bug 102229).
                    mat.SetRotate(q);
                    transforms[i] = mat * transforms[i];
                }
            }
        );
    }

    template <typename V3, bool DO_INTERP>
    static void
    doApplyScale(UT_Array<GfMatrix4d> &transforms,
            const VtIntArray &instanceIndices,
	    const V3 *seg0,
            const V3 *seg1,
            float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
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
    doApplyTransform(UT_Array<GfMatrix4d> &transforms,
            const VtIntArray &instanceIndices,
	    const M4 *seg0,
            const M4 *seg1,
            float lerp)
    {
	UT_ASSERT(transforms.size() == instanceIndices.size());
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
	METHOD(UT_Array<GfMatrix4d> &transforms, \
                const VtIntArray &instanceIndices, \
		const V *primvar0, const V *primvar1, float lerp) \
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

    // Split an attribute list into shader attributes and properties.  Property
    // names will be encoded and prefixed with "karma:object:"
    void
    splitAttributes(const GT_AttributeListHandle &source,
            GT_AttributeListHandle &attribs,
            GT_AttributeListHandle &properties)
    {
        if (!source)
            return;
        static constexpr UT_StringLit   thePrefix("karma:object:");
        UT_StringArray                  snames;
        GT_AttributeMapHandle           pmap;
        UT_SmallArray<int>              pidx;
        for (int i = 0, n = source->entries(); i < n; ++i)
        {
            const UT_StringHolder       &sname = source->getName(i);
            UT_StringHolder              dname = UT_VarEncode::decodeVar(sname);
            if (dname.startsWith(thePrefix))
            {
                snames.append(sname);
                if (!pmap)
                    pmap = UTmakeIntrusive<GT_AttributeMap>();

                // Strip off prefix
                UT_StringHolder stripped(dname.c_str() + thePrefix.length());
                pidx.append(pmap->add(stripped, false));
                UT_ASSERT(pidx.last() >= 0);
            }
        }
        if (!snames.size())
        {
            // Common case with no attributes
            attribs = source;
            return;
        }
        if (snames.size() != source->entries())
            attribs = source->removeAttributes(snames);

        // Currently, properties cannot be motion blurred
        properties = UTmakeIntrusive<GT_AttributeList>(pmap, 1);
        for (int i = 0, n = snames.size(); i < n; ++i)
        {
            properties->set(pidx[i], source->get(snames[i]));
        }
    }

    GfMatrix4d
    rotationMatrix(GfVec3f w)
    {
        static constexpr double EPS = 1e-12;
        double   theta = w.Normalize();
        if (theta <= EPS)
            return GfMatrix4d(1);
        double  st, ct;
        double  x = w[0];
        double  y = w[1];
        double  z = w[2];
        SYSsincos(theta, &st, &ct);
        double cr = 1-ct;
        return GfMatrix4d(cr*x*x + ct  , cr*x*y + st*z, cr*x*z - st*y, 0,
                          cr*y*x + st*z, cr*y*y + ct  , cr*y*z + st*x, 0,
                          cr*z*x + st*y, cr*z*y - st*x, cr*z*z + ct,   0,
                          0, 0, 0, 1);
    }

    void
    velocityBlur(const SdfPath &id,
            const VtIntArray &instanceindices,
            int nsegs,
            const VtArray<GfVec3f> *velocities,
            const VtArray<GfVec3f> *angularVelocities,
            const VtArray<GfVec3f> *accel,
            UT_Array<GfMatrix4d> *xformList, const float *shutter_times)
    {
        UT_ASSERT(velocities || angularVelocities);
        size_t  nitems = velocities ? velocities->size() : angularVelocities->size();
        if (accel && accel->size() != nitems)
            accel = nullptr;
        if (angularVelocities && angularVelocities->size() != nitems)
            angularVelocities = nullptr;
        for (int seg = 0; seg < nsegs; ++seg)
        {
            if (shutter_times[seg] == 0)
                continue;

            float       tm = shutter_times[seg];
            float       a = .5*tm*tm;
            for (size_t i = 0, m = instanceindices.size(); i < m; ++i)
            {
                size_t idx = instanceindices[i];
                if (idx >= nitems) // invalid idx?
                    continue;

                GfMatrix4d      xform(1.0);
                GfVec3d         vel(0.0);

                if (velocities)
                    vel = (*velocities)[idx] * tm;
                if (accel)
                {
                    const GfVec3f &acc = (*accel)[idx];
                    vel += GfVec3d(acc[0]*a, acc[1]*a, acc[2]*a);
                }
                if (angularVelocities)
                {
                    GfMatrix4d  xlate(1.0);
                    GfVec3d     p = xformList[seg][i].ExtractTranslation();
                    xform *= xlate.SetTranslateOnly(vel);
                    xform *= xlate.SetTranslateOnly(-p);
                    xform *= rotationMatrix((*angularVelocities)[idx] * tm);
                    xform *= xlate.SetTranslateOnly(p);
                }
                else
                {
                    xform.SetTranslateOnly(vel);
                }

                xformList[seg][i] = xformList[seg][i] * xform;
            }
        }
    }
}

#if 0
static void
dumpDesc(HdSceneDelegate *sd, HdInterpolation style, const SdfPath &id)
{
    const auto	&descs = sd->GetPrimvarDescriptors(id, style);
    if (!descs.size())
	return;
    UTdebugFormat("-- {} {} --", id, TfEnum::GetName(style));
    for (auto &d : descs)
	UTdebugFormat("  {}", d.name);
}

static void
dumpAllDesc(HdSceneDelegate *sd, const SdfPath &id)
{
    UTdebugFormat("-- {} --", id);
    for (auto &&style : {
	        HdInterpolationConstant,
		HdInterpolationUniform,
		HdInterpolationVarying,
		HdInterpolationVertex,
		HdInterpolationFaceVarying,
		HdInterpolationInstance,
	    })
	dumpDesc(sd, style, id);
}
#endif

BRAY_HdInstancer::BRAY_HdInstancer(HdSceneDelegate* delegate,
                                     SdfPath const& id)
    : HdInstancer(delegate, id)
    , myNewObject(false)
    , mySegments(2)
    , myMotionBlur(MotionBlurStyle::ACCEL)
{
}

BRAY_HdInstancer::~BRAY_HdInstancer()
{
}

void
BRAY_HdInstancer::applyNesting(BRAY_HdParam &rparm,
	BRAY::ScenePtr &scene)
{
    if (!myInstanceMap.size())
	return;

    // Make sure to build the scene graph if required
    BRAY::ObjectPtr	proto;

    if (myInstanceMap.size() > 1)
    {
	// In this case, we have multiple objects being instanced.  For this we
	// want to aggregate the edits into a scene graph.
	if (!mySceneGraph)
	{
	    myNewObject = true;
	    mySceneGraph = scene.createScene();
	    for (auto &&inst : myInstanceMap)
		mySceneGraph.addInstanceToScene(inst.second);
	}
	else
	{
	    myNewObject = false;
	    scene.updateObject(mySceneGraph, BRAY_EVENT_CONTENTS);
	}
	proto = mySceneGraph;	// This is the object we want to process
    }
    else
    {
	for (auto &&inst : myInstanceMap)
	{
	    UT_ASSERT(!proto);
	    proto = inst.second;
	    break;
	}
    }

    if (GetParentId().IsEmpty())
    {
	if (myNewObject)
	{
	    myNewObject = false;
	    scene.updateObject(proto, BRAY_EVENT_NEW);
	}
    }
    else
    {
	HdInstancer	*parentInstancer =
	    GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
	UT_ASSERT(parentInstancer);
	UT_SmallArray<GfMatrix4d>	px;
	px.emplace_back(1.0);

	UTverify_cast<BRAY_HdInstancer *>(parentInstancer)->
	    NestedInstances(rparm, scene, GetId(), proto, px,
                    proto.objectProperties(scene));

    }
}

static GT_AttributeListHandle
adjustSegments(const GT_AttributeListHandle &alist, int nsegs)
{
    // Ensure the attribute list has nsegs motion segments
    if (!alist || nsegs == alist->getSegments())
        return alist;
    UT_ASSERT(nsegs >= alist->getSegments());
    auto result = UTmakeIntrusive<GT_AttributeList>(alist->getMap(), nsegs);
    int  max = alist->getSegments()-1;
    for (int i = 0, n = alist->entries(); i < n; ++i)
    {
        for (int seg = 0; seg < nsegs; ++seg)
            result->set(i, alist->get(i, SYSmin(seg, max)), seg);
    }
    return result;
}

GT_AttributeListHandle
BRAY_HdInstancer::extractListForPrototype(const SdfPath &protoId,
        const GT_AttributeListHandle &attrs,
        const GT_AttributeListHandle &constantattrs) const
{
    // If there are no attributes, just return an empty array
    if ((!attrs || !attrs->entries()) &&
        (!constantattrs || !constantattrs->entries()))
        return GT_AttributeListHandle();

    // Figure out how many motion segments the result requires
    int nsegs = 1;
    if (attrs)
        nsegs = SYSmax(nsegs, attrs->getSegments());
    if (constantattrs)
        nsegs = SYSmax(nsegs, constantattrs->getSegments());

    VtIntArray indices = GetDelegate()->GetInstanceIndices(GetId(), protoId);

    GT_AttributeListHandle      newattrs;
    if (attrs && attrs->entries())
    {
        newattrs = adjustSegments(attrs, nsegs);
        if (indices.size() != attrs->get(0)->entries())
        {
            GT_DataArrayHandle gt_indices = BRAY_HdUtil::gtArray(indices);
            newattrs = newattrs->createIndirect(gt_indices);
        }
    }

    if (constantattrs && constantattrs->entries())
    {
        if (newattrs)
        {
            GT_AttributeListHandle      c = constantattrs->createConstant(0,
                                                        indices.size());
            newattrs = newattrs->mergeNewAttributes(adjustSegments(c, nsegs));
        }
        else
        {
            UT_ASSERT(constantattrs->getSegments() == nsegs);
            newattrs = constantattrs->createConstant(0, indices.size());
        }
    }

    return newattrs;
}

static inline int
getInt(const VtValue &val, int def)
{
    if (val.IsEmpty())
        return def;
    if (val.IsHolding<int32>())
        return val.UncheckedGet<int32>();
    if (val.IsHolding<int64>())
        return val.UncheckedGet<int64>();
    if (val.IsHolding<uint8>())
        return val.UncheckedGet<uint8>();
    UT_ASSERT(0 && "Unexpected integer value");
    return def;
}

static inline bool
getBool(const VtValue &val, bool def)
{
    if (val.IsEmpty())
        return def;
    if (val.IsHolding<bool>())
        return val.UncheckedGet<bool>();
    return 0 != getInt(val, def ? 1 : 0);
}

void
BRAY_HdInstancer::loadBlur(const BRAY_HdParam &rparm,
        HdSceneDelegate *sd,
        const SdfPath &id,
        BRAY::OptionSet &props)
{
    if (rparm.disableMotionBlur())
    {
        myMotionBlur = MotionBlurStyle::NONE;
        mySegments = 1;
        return;
    }

    bool        enable;
    if (!props.import(BRAY_OBJ_MOTION_BLUR, &enable, 1))
    {
        UT_ASSERT(0);
        enable = true;
    }
    if (!enable)
    {
        myMotionBlur = MotionBlurStyle::NONE;
        mySegments = 1;
        return;
    }

    int         vblur, isamp;
    if (!props.import(BRAY_OBJ_INSTANCE_VELBLUR, &vblur, 1))
        vblur = 0;
    if (!props.import(BRAY_OBJ_INSTANCE_SAMPLES, &isamp, 1))
        isamp = 2;
    if (vblur < 0 || vblur > 2)
    {
        UT_ErrorLog::error("Invalid instance velocity blur {} ({})",
                vblur, id);
        vblur = 0;
    }
    if (isamp < 1)
    {
        UT_ErrorLog::error("Invalid instance blur samples {} ({})",
                isamp, id);
        isamp = 1;
    }
    mySegments = SYSmax(1, isamp);
    if (mySegments < 2)
    {
        myMotionBlur = MotionBlurStyle::NONE;
        return;
    }
    switch (vblur)
    {
        case 0:
            myMotionBlur = MotionBlurStyle::DEFORM;
            break;
        case 1:
            myMotionBlur = MotionBlurStyle::VELOCITY;
            mySegments = 2;     // Clamp to 2 segmetns
            break;
        case 2:
            myMotionBlur = MotionBlurStyle::ACCEL;
            break;
        default:
            UT_ASSERT(0);
    }
}

void
BRAY_HdInstancer::Sync(HdSceneDelegate *sd,
                        HdRenderParam *rparm,
                        HdDirtyBits *dirtyBits)
{
    _UpdateInstancer(sd, dirtyBits);

    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, GetId()) ||
        HdChangeTracker::IsTransformDirty(*dirtyBits, GetId()))
    {
        syncPrimvars(sd, rparm, dirtyBits);
    }

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void
BRAY_HdInstancer::getSegment(int nsegs, float time,
        int &seg0, int &seg1, float &lerp) const
{
    nsegs = SYSmin(nsegs, mySegments);
    if (nsegs == 1)
    {
        seg0 = seg1 = 0;
        lerp = 0;
    }
    else if (nsegs == 2)
    {
        seg0 = 0;
        seg1 = 1;
        lerp = time;
    }
    else
    {
        time *= (nsegs-1);
        seg0 = SYSmin(nsegs-2, int(time));
        seg1 = seg0 + 1;
        lerp = time - seg0;
    }
}

void
BRAY_HdInstancer::syncPrimvars(HdSceneDelegate* delegate,
        HdRenderParam* renderParam,
        HdDirtyBits* dirtyBits)
{
    // When this is called from HUSD, we always pass in a 1.  However, the
    // method allows us to override the segments based on the instance's motion
    // blur.
    BRAY_HdParam        &rparm = *UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr      &scene = rparm.getSceneForEdit();

    const SdfPath       &id = GetId();

    UT_ASSERT(HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id) ||
              HdChangeTracker::IsTransformDirty(*dirtyBits, id));

    // Set up motion blur properties for the instance.  In this case, we
    // re-map the instance blur settings to the object blur settings for
    // BRAY_HdUtil.
    BRAY::OptionSet     propstmp = scene.objectProperties().duplicate();

    BRAY_HdUtil::updateObjectProperties(propstmp, *delegate, id);

    // Load motion blur settings
    loadBlur(rparm, delegate, id, propstmp);

    if (myMotionBlur == MotionBlurStyle::VELOCITY
            || myMotionBlur == MotionBlurStyle::ACCEL)
    {
        auto *sd = GetDelegate();
        myVelocities = BRAY_HdUtil::evalVt(sd, id, HdTokens->velocities);
        myAccelerations = BRAY_HdUtil::evalVt(sd, id, HdTokens->accelerations);
        myAngularVelocities = BRAY_HdUtil::evalVt(sd, id,
                                    UsdGeomTokens->angularVelocities);
        if (!myVelocities.IsHolding<VtArray<GfVec3f>>()
                && !myAngularVelocities.IsHolding<VtArray<GfVec3f>>())
        {
            myVelocities = VtValue();
            myAngularVelocities = VtValue();
            myAccelerations = VtValue();
            myMotionBlur = MotionBlurStyle::NONE;
            mySegments = 1;
        }
        else if (!myAccelerations.IsHolding<VtArray<GfVec3f>>())
        {
            myAccelerations = VtValue();
            myMotionBlur = MotionBlurStyle::VELOCITY;
            mySegments = 2;
        }
    }

    propstmp.set(BRAY_OBJ_MOTION_BLUR, myMotionBlur != MotionBlurStyle::NONE);
    propstmp.set(BRAY_OBJ_GEO_SAMPLES, mySegments);
    switch (myMotionBlur)
    {
        case MotionBlurStyle::NONE:
            propstmp.set(BRAY_OBJ_GEO_VELBLUR, 0);
            UT_ASSERT(mySegments == 1);
            break;
        case MotionBlurStyle::DEFORM:
            propstmp.set(BRAY_OBJ_GEO_VELBLUR, 0);
            UT_ASSERT(mySegments >= 2);
            break;
        case MotionBlurStyle::VELOCITY:
            UT_ASSERT(mySegments == 2);
            propstmp.set(BRAY_OBJ_GEO_VELBLUR, 1);
            break;
        case MotionBlurStyle::ACCEL:
            propstmp.set(BRAY_OBJ_GEO_VELBLUR, 2);
            UT_ASSERT(mySegments >= 2);
            break;
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id))
    {
        // Compute the number of transform motion segments.
        //
        // Since this instancer can be shared by many prototypes, it's more
        // efficient for us to cache the transforms rather than calling in
        // privComputeTransforms.  This is especially true when there's
        // motion blur and Hydra has to traverse the instancer hierarchy to
        // compute the proper motion segements for blur.
        myXforms.setSize(mySegments);
        if (mySegments == 1)
        {
            myXforms[0] = GetDelegate()->GetInstancerTransform(GetId());
        }
        else
        {
            UT_StackBuffer<float>       xtimes(mySegments);
            exint usegs = GetDelegate()->SampleInstancerTransform(GetId(),
                    mySegments, xtimes.array(), myXforms.data());
            if (usegs < myXforms.size())
            {
                // USD has fewer segments than we requested, so shrink our
                // arrays.
                myXforms.setSize(usegs);
            }
            else if (usegs > myXforms.size())
            {
                UT_StackBuffer<float>   big_xtimes(usegs);
                // USD has more samples, so we need to grow the arrays
                myXforms.setSize(usegs);
                usegs = GetDelegate()->SampleInstancerTransform(GetId(),
                    myXforms.size(), big_xtimes.array(), myXforms.data());
                UT_ASSERT(usegs == myXforms.size());
            }
        }
    }

    // Make an attribute list, but exclude all the tokens for
    // transforms We need to capture attributes before syncPrimvars()
    // clears the dirty bits when it caches the transform data.
    //
    // NOTE: There's a possible indeterminant order here.  The
    // prototypes can be processed in arbitrary order, but the
    // prototype's motion blur settings are used to determine the
    // motion segments for attributes on the instance attribs.  So, if
    // prototypes have different motion blur settings, the behaviour of
    // the instance evaluation might be different.
    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id))
    {
        myAttributes = BRAY_HdUtil::makeAttributes(GetDelegate(),
                        rparm,
                        GetId(),
                        HdInstancerTokens->instancer,
                        -1,
                        propstmp,
                        HdInterpolationInstance,
                        &transformTokens(),
                        false);
        myConstantAttributes = BRAY_HdUtil::makeAttributes(GetDelegate(),
                        rparm,
                        GetId(),
                        HdInstancerTokens->instancer,
                        -1,
                        propstmp,
                        HdInterpolationConstant,
                        &transformTokens(),
                        false);
    }
}

static void
dump(const BRAY::OptionSet &prop, const char *msg)
{
    UTdebugFormat("Props: {}", msg);
    UT_AutoJSONWriter   w(std::cerr, false);
    prop.dump(*w);
}

void
BRAY_HdInstancer::NestedInstances(BRAY_HdParam &rparm,
	BRAY::ScenePtr &scene,
	SdfPath const &prototypeId,
	const BRAY::ObjectPtr &protoObj,
	const UT_Array<GfMatrix4d> &protoXform,
        const BRAY::OptionSet &protoProps)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    const SdfPath                      &id = GetId();
    UT_Array<BRAY::SpacePtr>            xforms;

    UT_StackBuffer<UT_Array<GfMatrix4d>>        xformList(mySegments);
    UT_StackBuffer<float>                       shutter_times(mySegments);

    rparm.fillShutterTimes(shutter_times, mySegments);
    for (int i = 0; i < mySegments; ++i)
    {
        if (myMotionBlur != MotionBlurStyle::DEFORM
                && i > 0
                && protoXform.size() == 1)
        {
            // When we have velocity/acceleration blur, we just pull out the
            // first transform
            xformList[i] = xformList[0];     // Copy xforms from prev segment
        }
        else
        {
            int	pidx = SYSmin(i, int(protoXform.size()-1));
            float shutter = SYSsafediv(float(i), float(mySegments-1));
            computeTransforms(xformList[i], prototypeId,
                                protoXform[pidx], shutter);
        }
    }
    if (myMotionBlur == MotionBlurStyle::VELOCITY
            || myMotionBlur == MotionBlurStyle::ACCEL)
    {
        UT_ASSERT(mySegments > 1 &&
                (myVelocities.IsHolding<VtArray<GfVec3f>>()
                    || myAngularVelocities.IsHolding<VtArray<GfVec3f>>()));
        UT_StackBuffer<float>    frameTimes(mySegments);
        VtArray<GfVec3f>         astore, vstore, avstore;
        const VtArray<GfVec3f>  *velocities = nullptr;
        const VtArray<GfVec3f>  *angularVelocities = nullptr;
        const VtArray<GfVec3f>  *accelerations = nullptr;
        rparm.shutterToFrameTime(frameTimes.array(),
                shutter_times.array(), mySegments);
        if (myVelocities.IsHolding<VtArray<GfVec3f>>())
        {
            vstore = myVelocities.UncheckedGet<VtArray<GfVec3f>>();
            velocities = &vstore;
        }
        if (myAngularVelocities.IsHolding<VtArray<GfVec3f>>())
        {
            avstore = myAngularVelocities.UncheckedGet<VtArray<GfVec3f>>();
            angularVelocities = &avstore;
        }
        UT_ASSERT(velocities || angularVelocities);
        if (myMotionBlur == MotionBlurStyle::ACCEL)
        {
            UT_ASSERT(myAccelerations.IsHolding<VtArray<GfVec3f>>());
            astore = myAccelerations.UncheckedGet<VtArray<GfVec3f>>();
            accelerations = &astore;
        }

        VtIntArray instanceindices =
            GetDelegate()->GetInstanceIndices(id, prototypeId);
        velocityBlur(id, instanceindices, mySegments,
                    velocities,
                    angularVelocities,
                    accelerations,
                    xformList.array(),
                    frameTimes.array());
    }
    BRAY_HdUtil::makeSpaceList(xforms, xformList.array(), mySegments);
    BRAY::ObjectPtr     *inst = nullptr;

    bool		 new_instance = false;
    {
        UT_Lock::Scope	lock(myLock);
        // Find existing or create a new instance.
        inst = &myInstanceMap[prototypeId];

        // If this is a new instance, we need to create one
        if (!*inst)
        {
            new_instance = true;
            myNewObject = true;	// There's a new object in me

            // use prototype ID for leaf instances (which will have the instance
            // ID baked in anyway).  This allows for unique naming, and matches
            // the non-nested instance naming convention as well.
            UT_StringHolder name =  protoObj.isLeaf()  ?
                                    BRAY_HdUtil::toStr(prototypeId) :
                                    BRAY_HdUtil::toStr(GetId());

            *inst = scene.createInstance(protoObj, name);
        }
    }

    // Update information
    inst->setInstanceTransforms(scene, xforms);
    GT_AttributeListHandle      attribs, properties;
    splitAttributes(attributesForPrototype(prototypeId), attribs, properties);
    inst->setInstanceAttributes(scene, attribs);

    // Update per-xform light linking
    GT_DataArrayHandle categories;
    {
        UT_Lock::Scope lock(myLock);
        UT_Map<SdfPath, GT_DataArrayHandle>::iterator it =
            myCategories.find(prototypeId);
        if(it != myCategories.end())
            categories = it->second;
    }

    if (categories)
    {
        static constexpr UT_StringLit theLightCategoryAttr("lightcategories");
        UT_ASSERT(categories->entries() == xforms.size());
        if (properties)
        {
            properties = properties->addAttribute(
                theLightCategoryAttr.asHolder(), categories, false);
        }
        else
        {
            properties = GT_AttributeList::createAttributeList(
                theLightCategoryAttr.asHolder(), categories);
        }
    }

    inst->setInstanceProperties(scene, properties);
    inst->setInstanceIds(UT_Array<exint>());
    inst->validateInstance();

    if (!new_instance)
	scene.updateObject(*inst, BRAY_EVENT_XFORM);

    // Make sure to process myself after all my children have been processed.
    rparm.queueInstancer(GetDelegate(), this);
}

void
BRAY_HdInstancer::eraseFromScenegraph(BRAY::ScenePtr &scene)
{
    // post delete for all instances
    for (auto &&inst : myInstanceMap)
    {
	UT_ASSERT(inst.second);
	scene.updateObject(inst.second, BRAY_EVENT_DEL);
    }

    // also post delete for the scenegraph (if we have one)
    if (mySceneGraph)
	scene.updateObject(mySceneGraph, BRAY_EVENT_DEL);

    myInstanceMap.clear();
    mySceneGraph = BRAY::ObjectPtr();
}

namespace
{
    static int
    findIndex(const GT_AttributeListHandle &attribs,
            const TfToken &name,
            int tuple_size)
    {
        int     idx = attribs->getIndex(name.GetText());
        if (idx > 0 && attribs->get(idx)->getTupleSize() != tuple_size)
            idx = -1;
        return idx;
    }

    template <typename T>
    static const T *
    primvarData(const GT_DataArrayHandle &data, GT_DataArrayHandle &store)
    {
        using SCALAR_T = typename T::ScalarType;
        if (data->getStorage() == GTstorage<SCALAR_T>())
        {
            const void  *raw = data->getBackingData();
            if (raw)
                return (const T *)raw;
        }
        return (const T *)data->getArray<SCALAR_T>(store);
    }

    // Since Pixar's half type doesn't match fpreal16, we need to specialize
    // the 16 bit float versions.
#define HALF_PRIMVAR(TYPE) \
    template <> const TYPE * \
    primvarData<TYPE>(const GT_DataArrayHandle &data, GT_DataArrayHandle &store) \
    { \
        using SCALAR_T = fpreal16; \
        if (data->getStorage() == GTstorage<SCALAR_T>()) \
        { \
            const void  *raw = data->getBackingData(); \
            if (raw) \
                return (const TYPE *)raw; \
        } \
        return (const TYPE *)data->getArray<SCALAR_T>(store); \
    } \
    /* end macro */
HALF_PRIMVAR(GfVec3h)
HALF_PRIMVAR(GfVec4h)
#undef HALF_PRIMVAR
}

void
BRAY_HdInstancer::computeTransforms(UT_Array<GfMatrix4d> &transforms,
        const SdfPath &prototypeId,
        const GfMatrix4d &protoXform,
        float shutter_time)
{
    // The transforms for this level of instancer are computed by:
    // foreach(index : indices) {
    //     instancerTransform * translate(index) * rotate(index) *
    //     scale(index) * instanceTransform(index)
    // }
    // If any transform isn't provided, it's assumed to be the identity.
    VtIntArray instanceIndices =
        GetDelegate()->GetInstanceIndices(GetId(), prototypeId);
    int num_inst = instanceIndices.size();

    //UTdebugFormat("Compute {} transforms {}", num_inst, GetId());

    // Get motion blur interpolants
    transforms.setSize(num_inst);
    int                 seg0, seg1;
    float               shutter;
    GfMatrix4d          ixform;
    if (!myXforms.size())
        ixform = GfMatrix4d(1.0);
    else
    {
        getSegment(myXforms.size(), shutter_time, seg0, seg1, shutter);
        if (shutter == 0 || myXforms.size() == 1)
            ixform = myXforms[seg0];
        else
        {
            lerpVec(ixform.data(),
                    myXforms[seg0].data(), myXforms[seg1].data(), shutter, 16);
        }
    }
    std::fill(transforms.begin(), transforms.end(), ixform);

    // Note that we do not need to lock myLock here to access myPrimvarMap.
    // The syncPrimvars method should be called before this method to build
    // myPrimvarMap, but it guarantees that only one thread (the first one to
    // make it through that method) will change myPrimvarMap. So by the time
    // any thread reaches this point, it is guaranteed that no other threads
    // will be modifying myPrimvarMap.

    if (myAttributes)
    {
        getSegment(myAttributes->getSegments(), shutter_time,
                seg0, seg1, shutter);
        UTisolate([&]()
        {
            // "translate" holds a translation vector for each index.
            int                 idx;
            GT_DataArrayHandle  store0, store1;

            idx = findIndex(myAttributes, HdInstancerTokens->translate, 3);
            if (idx >= 0)
            {
                const auto &data0 = myAttributes->get(idx, seg0);
                const auto &data1 = myAttributes->get(idx, seg1);
                UT_ASSERT(data0->entries() == data1->entries());
                switch (data0->getStorage())
                {
                    case GT_STORE_REAL32:
                        applyTranslate<GfVec3f>(transforms,
                                instanceIndices,
                                primvarData<GfVec3f>(data0, store0),
                                primvarData<GfVec3f>(data1, store1),
                                shutter);
                        break;
                    case GT_STORE_REAL64:
                        applyTranslate<GfVec3d>(transforms,
                                instanceIndices,
                                primvarData<GfVec3d>(data0, store0),
                                primvarData<GfVec3d>(data1, store1),
                                shutter);
                        break;
                    case GT_STORE_REAL16:
                        applyTranslate<GfVec3h>(transforms,
                                instanceIndices,
                                primvarData<GfVec3h>(data0, store0),
                                primvarData<GfVec3h>(data1, store1),
                                shutter);
                        break;
                    default:
                        UT_ASSERT(0 && "Unknown buffer type");
                        break;
                }
            }

            // "rotate" holds a quaternion in <real, i, j, k> format for each index.
            idx = findIndex(myAttributes, HdInstancerTokens->rotate, 4);
            if (idx >= 0)
            {
                const auto &data0 = myAttributes->get(idx, seg0);
                const auto &data1 = myAttributes->get(idx, seg1);
                UT_ASSERT(data0->entries() == data1->entries());
                switch (data0->getStorage())
                {
                    case GT_STORE_REAL32:
                        applyRotate<GfVec4f>(transforms,
                                instanceIndices,
                                primvarData<GfVec4f>(data0, store0),
                                primvarData<GfVec4f>(data1, store1),
                                shutter);
                        break;
                    case GT_STORE_REAL64:
                        applyRotate<GfVec4d>(transforms,
                                instanceIndices,
                                primvarData<GfVec4d>(data0, store0),
                                primvarData<GfVec4d>(data1, store1),
                                shutter);
                        break;
                    case GT_STORE_REAL16:
                        applyRotate<GfVec4h>(transforms,
                                instanceIndices,
                                primvarData<GfVec4h>(data0, store0),
                                primvarData<GfVec4h>(data1, store1),
                                shutter);
                        break;
                    default:
                        UT_ASSERT(0 && "Unknown buffer type");
                        break;
                }
            }

            // "scale" holds an axis-aligned scale vector for each index.
            idx = findIndex(myAttributes, HdInstancerTokens->scale, 3);
            if (idx >= 0)
            {
                const auto &data0 = myAttributes->get(idx, seg0);
                const auto &data1 = myAttributes->get(idx, seg1);
                UT_ASSERT(data0->entries() == data1->entries());
                switch (data0->getStorage())
                {
                    case GT_STORE_REAL32:
                        applyScale<GfVec3f>(transforms,
                                instanceIndices,
                                primvarData<GfVec3f>(data0, store0),
                                primvarData<GfVec3f>(data1, store1),
                                shutter);
                        break;
                    case GT_STORE_REAL64:
                        applyScale<GfVec3d>(transforms,
                                instanceIndices,
                                primvarData<GfVec3d>(data0, store0),
                                primvarData<GfVec3d>(data1, store1),
                                shutter);
                        break;
                    case GT_STORE_REAL16:
                        applyScale<GfVec3h>(transforms,
                                instanceIndices,
                                primvarData<GfVec3h>(data0, store0),
                                primvarData<GfVec3h>(data1, store1),
                                shutter);
                    default:
                        UT_ASSERT(0 && "Unknown buffer type");
                        break;
                }
            }

            // "instanceTransform" holds a 4x4 transform matrix for each index.
            idx = findIndex(myAttributes, HdInstancerTokens->instanceTransform, 16);
            if (idx >= 0)
            {
                const auto &data0 = myAttributes->get(idx, seg0);
                const auto &data1 = myAttributes->get(idx, seg1);
                UT_ASSERT(data0->entries() == data1->entries());
                switch (data0->getStorage())
                {
                    case GT_STORE_REAL32:
                        applyTransform<GfMatrix4f>(transforms,
                                instanceIndices,
                                primvarData<GfMatrix4f>(data0, store0),
                                primvarData<GfMatrix4f>(data1, store1),
                                shutter);
                        break;
                    case GT_STORE_REAL64:
                        applyTransform<GfMatrix4d>(transforms,
                                instanceIndices,
                                primvarData<GfMatrix4d>(data0, store0),
                                primvarData<GfMatrix4d>(data1, store1),
                                shutter);
                        break;
                    default:
                        UT_ASSERT(0 && "Unknown buffer type");
                        break;
                }
            }
        });
    }
    GT_DataArrayHandle  store0, store1;
    if (protoXform != GfMatrix4d(1.0))
    {
        for (size_t i = 0; i < num_inst; ++i)
            transforms[i] = protoXform * transforms[i];
    }
}

int
BRAY_HdInstancer::getNestLevel() const
{
    int nestlevel = 0;
    const HdInstancer *instancer = this;
    while (!instancer->GetParentId().IsEmpty())
    {
        nestlevel++;
        instancer = GetDelegate()->GetRenderIndex().GetInstancer(
            instancer->GetParentId());
    }
    return nestlevel;
}

PXR_NAMESPACE_CLOSE_SCOPE

