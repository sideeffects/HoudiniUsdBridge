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

#include "BRAY_HdCamera.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdParam.h"
#include "BRAY_HdFormat.h"
#include "BRAY_HdTokens.h"
#include <UT/UT_SmallArray.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_WorkArgs.h>

#include <pxr/base/gf/range1f.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/sdf/assetPath.h>

#include <UT/UT_Debug.h>
#include <UT/UT_EnvControl.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static constexpr UT_StringLit       thePluginName("BRAY_HdKarma");

    static bool
    isOrtho(const VtValue &projection)
    {
        if (projection.IsHolding<HdCamera::Projection>())
        {
            return projection.UncheckedGet<HdCamera::Projection>()
                        == HdCamera::Orthographic;
        }
        if (projection.IsHolding<TfToken>())
        {
            return projection.UncheckedGet<TfToken>()
                        == UsdGeomTokens->orthographic;
        }
        return false;
    }
    // The USD spec states that aperture and focal length are given in mm, but
    // the Hydra code converts these measurements to the "scene units",
    // assuming the scene units are centimetres.  That is, the focal and
    // aperture are divided by 10 to convert to cm.
    //
    // Since Karma expects the values to be in mm., we need to undo what Hydra
    // does by scaling the values back.
    static constexpr fpreal	theHydraCorrection = 10;

    template <typename FLOAT_T=float>
    static FLOAT_T
    floatValue(const VtValue &v)
    {
	if (v.IsHolding<fpreal32>())
	    return v.UncheckedGet<fpreal32>();
	if (v.IsHolding<fpreal64>())
	    return v.UncheckedGet<fpreal64>();
	if (v.IsHolding<fpreal16>())
	    return v.UncheckedGet<fpreal16>();

        UTdebugFormat("Holding: {}", v.GetType().GetTypeName());
	UT_ASSERT(0 && "VtValue is not a float");
	if (v.IsHolding<int32>())
	    return v.UncheckedGet<int32>();
	if (v.IsHolding<int64>())
	    return v.UncheckedGet<int64>();
	UT_ASSERT(0);
	return 0;
    }

    template <typename FLOAT_T=float>
    static FLOAT_T
    floatValue(const UT_Array<VtValue> &arr, int idx)
    {
        UT_ASSERT(arr.size());
        idx = SYSmin(idx, arr.size()-1);
        return floatValue(arr[idx]);
    }

    static GfVec2f
    float2Value(const VtValue &v)
    {
	if (v.IsHolding<GfVec2f>())
	    return v.UncheckedGet<GfVec2f>();
	if (v.IsHolding<GfRange1f>())
	{
	    auto r = v.UncheckedGet<GfRange1f>();
	    return GfVec2f(r.GetMin(), r.GetMax());
	}
	if (v.IsHolding<GfVec2d>())
	{
	    auto r = v.UncheckedGet<GfVec2d>();
	    return GfVec2f(r[0], r[0]);
	}
	if (v.IsHolding<GfRange1d>())
	{
	    auto r = v.UncheckedGet<GfRange1d>();
	    return GfVec2f(r.GetMin(), r.GetMax());
	}
	UT_ASSERT(0);
	return GfVec2f(0, 1);
    }

    static void
    setAperture(UT_Array<BRAY::OptionSet> &cprops,
	    BRAY_HdParam::ConformPolicy policy,
	    const UT_Array<VtValue> &haperture,
	    const UT_Array<VtValue> &vaperture,
	    float imgaspect,
	    float pixel_aspect)
    {
	int		n = cprops.size();
	for (int i = 0; i < n; ++i)
	{
	    float	hap = floatValue(haperture, i)*theHydraCorrection;
	    float	vap = floatValue(vaperture, i)*theHydraCorrection;
	    float	par = pixel_aspect;
	    //UTdebugFormat("Input aperture[{}]: {} {} {}/{} {}", int(policy), hap, vap, hap/vap, imgaspect, pixel_aspect);

            UT_ErrorLog::format(8,
                    "Aspect ratio conform {} H/V: {}/{}, PAR: {}, IAR: {}",
                    int(policy), hap, vap, pixel_aspect, imgaspect);
	    BRAY_HdParam::aspectConform(policy,
		    vap, par, SYSsafediv(hap, vap), imgaspect);

	    cprops[i].set(BRAY_CAMERA_ORTHO_WIDTH, vap/theHydraCorrection);
	    cprops[i].set(BRAY_CAMERA_APERTURE, vap);
	    //UTdebugFormat("Set aperture: {} {} {}/{} {}", hap, vap, hap/vap, imgaspect, pixel_aspect);
	}
    }

    void
    updateScreenWindow(UT_Array<VtValue> &screenWindow,
            const UT_Array<VtValue> &hoff, const UT_Array<VtValue> &hap,
            const UT_Array<VtValue> &voff, const UT_Array<VtValue> &vap)
    {
        auto needAdjust = [](const UT_Array<VtValue> &off)
        {
            for (auto &&o : off)
            {
                if (floatValue(o) != 0)
                    return true;
            }
            return false;
        };
        if (!needAdjust(hoff) && !needAdjust(voff))
            return;
        if (!screenWindow.size())
        {
            screenWindow.emplace_back(GfVec4f(-1, 1, -1, 1));
        }
        int nseg = SYSmax(screenWindow.size(), hoff.size(), voff.size());
        // Extend the screenWindow array
        for (int i = screenWindow.size(); i < nseg; ++i)
            screenWindow.append(screenWindow.last());

        // Adjust the horizontal offset
        UT_ASSERT(hap.size());
        UT_ASSERT(vap.size());
        for (int i = 0; i < nseg; ++i)
        {
            // Compute ratio of offsets - screen window is (-1, 1).  If the
            // horizontal aperture is 10, an offset of 5 should change the
            // window to (0, 2).  That's because the entire width of the image
            // is 10, so we want to move half the image over.
            float h = 0, v = 0;
            if (hoff.size())
                h = 2*SYSsafediv(floatValue(hoff, i), floatValue(hap, i));
            if (voff.size())
                v = 2*SYSsafediv(floatValue(voff, i), floatValue(vap, i));
            UT_ASSERT(screenWindow[i].IsHolding<GfVec4f>());
            GfVec4f sw = screenWindow[i].UncheckedGet<GfVec4f>();
            //UTdebugFormat("SW: {} -> {}", sw, sw + GfVec4f(h, h, v, v));
            sw += GfVec4f(h, h, v, v);
            screenWindow[i] = VtValue(sw);
        }
    }

    static void
    setShader(HdSceneDelegate *sd, const SdfPath &id,
	    BRAY::CameraPtr &cam, const BRAY::ScenePtr &scene)
    {
	std::string str;
        BRAY_HdUtil::evalCamera<std::string>(str, sd, id,
                BRAY_HdUtil::cameraToken(BRAY_CAMERA_LENSSHADER));
	UT_String buffer(str);
	UT_WorkArgs work_args;
	buffer.parse(work_args);
	int size = work_args.getArgc();
	UT_StringArray args(size, size);
	for (int i = 0; i < size; ++i)
	    args[i] = work_args.getArg(i);
	cam.setShader(scene, args);
    }

}

BRAY_HdCamera::BRAY_HdCamera(const SdfPath &id)
    : HdCamera(id)
    , myNeedConforming(false)
{
#if 0
    if (!id.IsEmpty())
	UTdebugFormat("New Camera : {} {} ", this, id);
    else
	UTdebugFormat("Empty camera path");
#endif
}

BRAY_HdCamera::~BRAY_HdCamera()
{
    //UTdebugFormat("Delete camera {}", this);
}

void
BRAY_HdCamera::Finalize(HdRenderParam *renderParam)
{
    if (myCamera)
    {
	BRAY::ScenePtr	&scene =
	    UTverify_cast<BRAY_HdParam *>(renderParam)->getSceneForEdit();
	scene.updateCamera(myCamera, BRAY_EVENT_DEL);
    }
    myCamera = BRAY::CameraPtr();
}

static void
setFloatProperty(UT_Array<BRAY::OptionSet> &cprops, BRAY_CameraProperty brayprop,
	const UT_Array<VtValue> &values, fpreal scale=1)
{
    if (!values.size())
        return;

    int				n = cprops.size();

    UT_ASSERT(values.size() == n || values.size() == 1);
    if (values.size() == n)
    {
	for (int i = 0; i < n; ++i)
	    cprops[i].set(brayprop, floatValue(values[i])*scale);
    }
    else
    {
	float	f = floatValue(values[0])*scale;
	for (int i = 0; i < n; ++i)
	    cprops[i].set(brayprop, f);
    }
}

template <typename T>
static void
setVecProperty(UT_Array<BRAY::OptionSet> &cprops, BRAY_CameraProperty brayprop,
	const UT_Array<VtValue> &values)
{
    if (!values.size())
        return;

    int				n = cprops.size();

    UT_ASSERT(values.size() == n || values.size() == 1);
    if (values.size() == n)
    {
	for (int i = 0; i < n; ++i)
	{
	    if (values[i].IsHolding<T>())
	    {
		T	f = values[0].UncheckedGet<T>();
		cprops[i].set(brayprop, f.data(), T::dimension);
	    }
	}
    }
    else if (values[0].IsHolding<T>())
    {
	T	f = values[0].UncheckedGet<T>();
	for (int i = 0; i < n; ++i)
	    cprops[i].set(brayprop, f.data(), T::dimension);
    }
    else
    {
	UT_ASSERT(0 && "Unexpected value type");
    }
}

void
BRAY_HdCamera::updateAperture(HdRenderParam *renderParam,
	const GfVec2i &res,
	bool lock_camera)
{
    // If we're set by the viewport camera, or we haven't been created
    // then just return.
    if (!myNeedConforming || !myCamera)
	return;

    BRAY_HdParam &rparm = *UTverify_cast<BRAY_HdParam *>(renderParam);
    UT_Array<BRAY::OptionSet> cprops = myCamera.cameraProperties();

    fpreal64	pixel_aspect = rparm.pixelAspect();
    setAperture(cprops, rparm.conformPolicy(), myHAperture, myVAperture,
	    SYSsafediv(pixel_aspect*res[0], fpreal64(res[1])), pixel_aspect);
    if (lock_camera)
    {
	BRAY::ScenePtr	&scene = rparm.getSceneForEdit();
	myCamera.commitOptions(scene);
    }
}

template <BRAY_HdUtil::EvalStyle STYLE>
void
BRAY_HdCameraProps::init(HdSceneDelegate *sd,
                    BRAY_HdParam &rparm,
                    const SdfPath &id,
                    const BRAY::OptionSet &oprops)
{
    bool    autoseg = BRAY_HdUtil::autoSegment(rparm, oprops);
    int     nsegs = BRAY_HdUtil::xformSamples(rparm, oprops, autoseg);

    UT_StackBuffer<float>       times(nsegs);

    rparm.fillShutterTimes(times, nsegs);
    BRAY_HdUtil::xformBlur(sd, myXform, id, times, nsegs, autoseg);

    myProjection = BRAY_HdUtil::evalVt(sd, id, UsdGeomTokens->projection);

    // TODO: Hydra does some strange translation of units when evaluating
    // camera parameters on some parameters.  This really only affects DOF, but
    // since this codepath is also used for HdCoordSys, we may need
    // consistency.
    BRAY_HdUtil::dformBlur<STYLE>(sd, myHAperture, id,
            UsdGeomTokens->horizontalAperture, times, nsegs, autoseg);
    BRAY_HdUtil::dformBlur<STYLE>(sd, myVAperture, id,
            UsdGeomTokens->verticalAperture, times, nsegs, autoseg);
    BRAY_HdUtil::dformBlur<STYLE>(sd, myHOffset, id,
            UsdGeomTokens->horizontalApertureOffset, times, nsegs, autoseg);
    BRAY_HdUtil::dformBlur<STYLE>(sd, myVOffset, id,
            UsdGeomTokens->verticalApertureOffset, times, nsegs, autoseg);
    BRAY_HdUtil::dformBlur<STYLE>(sd, myFocal, id,
            UsdGeomTokens->focalLength, times, nsegs, autoseg);
    BRAY_HdUtil::dformBlur<STYLE>(sd, myFocusDistance, id,
            UsdGeomTokens->focusDistance, times, nsegs, autoseg);
    BRAY_HdUtil::dformBlur<STYLE>(sd, myFStop, id,
            UsdGeomTokens->fStop, times, nsegs, autoseg);
    BRAY_HdUtil::dformBlur<STYLE>(sd, myScreenWindow, id,
            BRAY_HdUtil::cameraToken(BRAY_CAMERA_WINDOW),
            times, nsegs, autoseg);
    BRAY_HdUtil::dformBlur<STYLE>(sd, myExposure, id,
            UsdGeomTokens->exposure, times, nsegs, autoseg);
    BRAY_HdUtil::dformBlur<STYLE>(sd, myTint, id,
            BRAY_HdUtil::cameraToken(BRAY_CAMERA_TINT), times, nsegs, autoseg);


    // When evaluating HdCoordSys, it seems that all parameters aren't always
    // available.  We need the apertures when setting up stuff below
    if (!myHAperture.size())
        myHAperture.append(VtValue(1.0));
    if (!myVAperture.size())
        myVAperture.append(VtValue(1.0));

    updateScreenWindow(myScreenWindow,
            myHOffset, myHAperture, myVOffset, myVAperture);

    if (!myScreenWindow.size())
        myScreenWindow.append(VtValue(GfVec4f(-1, 1, -1, 1)));

    // Cliprange and shutter should not be animated
    myClippingRange = BRAY_HdUtil::evalVt(sd, id, UsdGeomTokens->clippingRange);
}

int
BRAY_HdCameraProps::propSegments() const
{
    exint	psize = 0;
    psize = SYSmax(psize, myHAperture.size());
    psize = SYSmax(psize, myVAperture.size());
    psize = SYSmax(psize, myHOffset.size());
    psize = SYSmax(psize, myVOffset.size());
    psize = SYSmax(psize, myFocal.size());
    psize = SYSmax(psize, myFocusDistance.size());
    psize = SYSmax(psize, myExposure.size());
    psize = SYSmax(psize, myFStop.size());
    psize = SYSmax(psize, myTint.size());
    psize = SYSmax(psize, myScreenWindow.size());
    return psize;
}

template <typename T> UT_Array<BRAY::OptionSet>
BRAY_HdCameraProps::setProperties(BRAY::ScenePtr &scene, T &obj) const
{
    obj.setTransform(scene,
                BRAY_HdUtil::makeSpace(myXform.data(), myXform.size()));

    obj.resizeCameraProperties(propSegments());
    UT_Array<BRAY::OptionSet>       cprops = obj.cameraProperties();

    setFloatProperty(cprops, BRAY_CAMERA_FOCAL, myFocal, theHydraCorrection);
    setFloatProperty(cprops, BRAY_CAMERA_FOCUS_DISTANCE, myFocusDistance);
    setFloatProperty(cprops, BRAY_CAMERA_FSTOP, myFStop);
    setFloatProperty(cprops, BRAY_CAMERA_EXPOSURE, myExposure);
    setVecProperty<GfVec3f>(cprops, BRAY_CAMERA_TINT, myTint);
    setVecProperty<GfVec4f>(cprops, BRAY_CAMERA_WINDOW, myScreenWindow);

    // Now call setAperture to set the ortho width and karma aperture.  This is
    // done primarily for HdCoordSys.
    float imgaspect = 1;

    if (myHAperture.size() > 0 && myVAperture.size() > 0)
    {
        imgaspect = SYSsafediv(floatValue(myHAperture, 0),
                               floatValue(myVAperture, 0));
    }
    setAperture(cprops, BRAY_HdParam::ConformPolicy::DEFAULT,
            myHAperture, myVAperture, imgaspect, 1.0f);

    if (!myClippingRange.IsEmpty())
    {
        GfVec2f clip = float2Value(myClippingRange);
        for (auto &&cprop : cprops)
            cprop.set(BRAY_CAMERA_CLIP, clip.data(), 2);
    }

    return cprops;
}

void
BRAY_HdCamera::Sync(HdSceneDelegate *sd,
		    HdRenderParam *renderParam,
		    HdDirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    const SdfPath	&id = GetId();
    if (id.IsEmpty())	// Not a real camera?
	return;

    //UTdebugFormat("Sync Camera: {} {} {}", this, id, (int)*dirtyBits);
    BRAY_HdParam	&rparm = *UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr	&scene = rparm.getSceneForEdit();
    BRAY_EventType	event = BRAY_NO_EVENT;
    UT_StringHolder     name = BRAY_HdUtil::toStr(id);

    if (!myCamera)
	myCamera = scene.createCamera(name);

    UT_ErrorLog::format(8, "Sync camera {}", id);
    if (name.contains(thePluginName))
    {
	// Default viewport camera
	UT_Array<BRAY::OptionSet>	 cprops;

	bool viewdirty = *dirtyBits & (DirtyTransform | DirtyParams);
	bool projdirty = *dirtyBits & DirtyParams;

	HdCamera::Sync(sd, renderParam, dirtyBits);

        GfMatrix4d projmat = ComputeProjectionMatrix();

	// Following must be done after HdCamera::Sync()
	if (viewdirty)
	{
	    // Set transform
	    myCamera.setTransform(scene, BRAY_HdUtil::makeSpace(GetTransform()));
	    event = event | BRAY_EVENT_XFORM;
	}
	if (projdirty)
	{
	    myCamera.resizeCameraProperties(1);
	    cprops = myCamera.cameraProperties();

	    bool ortho = projmat[2][3] == 0.0;
	    bool cvex = false;
            BRAY_HdUtil::evalCamera<bool>(cvex, sd, id,
                    BRAYHdTokens->karma_camera_use_lensshader);

	    // The projection matrix is typically defined as
	    //  [ S   0    0          0
	    //    0   S    0          0
	    //    tx ty -(f+n)/(f-n) -1
	    //    0   0  -f*n/(f-n)   0 ]
	    // Where:
	    //   S = "zoom" ( 1/tan(FOV/2))
	    //   f = far clipping
	    //   n = near clipping
	    //   tx = 2d pan in x (NDC space)
	    //   ty = 2d pan in y (NDC space)
	    {
		fpreal a = projmat[2][2];
		fpreal b = projmat[3][2];
		fpreal	nf[2];
                if (ortho)
                {
                    nf[0] = SYSsafediv(b + 1, a);
                    nf[1] = SYSsafediv(b - 1, a);
                }
                else
                {
                    nf[1] = SYSsafediv(b, a+1);
                    nf[0] = -nf[1] * SYSsafediv(1 + a, 1 - a);
                }
		cprops[0].set(BRAY_CAMERA_CLIP, nf, 2);
	    }
	    if (cvex)
	    {
		setShader(sd, id, myCamera, scene);
		GfVec3d x(1, 0, 0);
		x = projmat.GetInverse().Transform(x);
		float cam_aspect = SYSsafediv(projmat[0][0], projmat[1][1]);
		cprops[0].set(BRAY_CAMERA_PROJECTION, (int)BRAY_PROJ_CVEX_SHADER);
		cprops[0].set(BRAY_CAMERA_ORTHO_WIDTH, x[0] * 2 * cam_aspect);
	    }
	    else
	    {
		if (ortho)
		{
		    GfVec3d x(1, 0, 0);
		    x = projmat.GetInverse().Transform(x);
		    float cam_aspect = SYSsafediv(projmat[0][0], projmat[1][1]);
		    cprops[0].set(BRAY_CAMERA_PROJECTION, (int)BRAY_PROJ_ORTHOGRAPHIC);
		    cprops[0].set(BRAY_CAMERA_ORTHO_WIDTH, x[0] * 2 * cam_aspect);
		}
		else
		{
		    cprops[0].set(BRAY_CAMERA_PROJECTION, (int)BRAY_PROJ_PERSPECTIVE);
		}
	    }

#if 0
	    // Handle the projection offset
	    fpreal64	window[4] = {
		projmat[2][0]-1,
		projmat[2][0]+1,
		projmat[2][1]-1,
		projmat[2][1]+1
	    };
	    cprops[0].set(BRAY_CAMERA_WINDOW, window, 4);
#endif

	    // Set focal, aperture, ortho, and clip range
	    // Just use default aperture for now
	    float aperture = *cprops[0].fval(BRAY_CAMERA_APERTURE);
	    float focal = projmat[0][0] * aperture * 0.5f;
	    float cam_aspect = SYSsafediv(projmat[1][1], projmat[0][0]);
	    cprops[0].set(BRAY_CAMERA_FOCAL, focal * cam_aspect);
	}

	// When we don't have a camera aspect ratio from a camera schema, we
	// don't need to worry about conforming.
	myNeedConforming = false;
    }
    else
    {
	// Non-default cameras (tied to a UsdGeomCamera)
	VtValue vshutteropen = BRAY_HdUtil::evalVt(sd, id, UsdGeomTokens->shutterOpen);
	VtValue vshutterclose = BRAY_HdUtil::evalVt(sd, id, UsdGeomTokens->shutterClose);
	fpreal shutter[2] = { floatValue<fpreal>(vshutteropen),
			     floatValue<fpreal>(vshutterclose) };
	rparm.updateShutter(id, shutter[0], shutter[1]);

	// Since we have a camera aspect ratio defined, we need to worry about
	// the conforming policy.
	myNeedConforming = true;

	// Transform
	BRAY::OptionSet	oprops = myCamera.objectProperties();

	if (*dirtyBits & DirtyParams)
	    BRAY_HdUtil::updateObjectProperties(oprops, *sd, id);


        BRAY_HdCameraProps              cpropset;

        cpropset.init<BRAY_HdUtil::EVAL_CAMERA_PARM>(sd, rparm,
                                    id, oprops);

	bool    is_ortho = isOrtho(cpropset.myProjection);

        UT_Array<BRAY::OptionSet> cprops = cpropset.setProperties(scene, myCamera);
        UT_ASSERT(cprops.size() > 0);

        myHAperture = cpropset.myHAperture;
        myVAperture = cpropset.myVAperture;

        UT_ErrorLog::format(8, "{} motion segments for {}",
                        cpropset.propSegments(), id);
	event = event | BRAY_EVENT_XFORM;

	bool cvex = false;
        BRAY_HdUtil::evalCamera<bool>(cvex, sd, id,
                    BRAYHdTokens->karma_camera_use_lensshader);
	if (cvex)
	{
	    setShader(sd, id, myCamera, scene);
	    for (auto &&cprop : cprops)
		cprop.set(BRAY_CAMERA_PROJECTION, (int)BRAY_PROJ_CVEX_SHADER);
	}
	else
	{
	    if (is_ortho)
	    {
		for (auto &&cprop : cprops)
		    cprop.set(BRAY_CAMERA_PROJECTION, (int)BRAY_PROJ_ORTHOGRAPHIC);
	    }
	    else
	    {
		for (auto &&cprop : cprops)
		    cprop.set(BRAY_CAMERA_PROJECTION, (int)BRAY_PROJ_PERSPECTIVE);
	    }
	}


	// Shutter cannot be animated
	for (auto &&cprop : cprops)
	    cprop.set(BRAY_CAMERA_SHUTTER, shutter, 2);

        // Update the aperture
        updateAperture(renderParam, rparm.resolution(), false);

	// Call base class to make sure all base class members hare dealt with
	// We've set _projectionMatrix, but possibly not exactly the way the
	// base class wants.  If we call this *before* the xformBlur, the
	// motion samples are incorrect.
	HdCamera::Sync(sd, renderParam, dirtyBits);
    }

    // TODO: USD assumes camera focal/aperture to be in mm and world in cm
    // (see gfcamera's aperture and focal length unit). We might want to
    // do the conversion here, or add extra options for world scale units.
    // (relevant only for DOF/lens shader)
    myCamera.commitOptions(scene);

    if ((*dirtyBits) & (~DirtyTransform & AllDirty))
	event = event | BRAY_EVENT_PROPERTIES;
    if (event != BRAY_NO_EVENT)
	scene.updateCamera(myCamera, event);

    *dirtyBits &= ~AllDirty;
}

HdDirtyBits
BRAY_HdCamera::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

// Instantiate setProperties for CameraPtr, CoordSysPtr
template void
BRAY_HdCameraProps::init<BRAY_HdUtil::EVAL_CAMERA_PARM>(
                    HdSceneDelegate *,
                    BRAY_HdParam &,
                    const SdfPath &,
                    const BRAY::OptionSet &);
template void
BRAY_HdCameraProps::init<BRAY_HdUtil::EVAL_GENERIC>(
                    HdSceneDelegate *,
                    BRAY_HdParam &,
                    const SdfPath &,
                    const BRAY::OptionSet &);

template UT_Array<BRAY::OptionSet>
BRAY_HdCameraProps::setProperties<BRAY::CameraPtr>(
                    BRAY::ScenePtr &, BRAY::CameraPtr &) const;

template UT_Array<BRAY::OptionSet>
BRAY_HdCameraProps::setProperties<BRAY::CoordSysPtr>(
                    BRAY::ScenePtr &, BRAY::CoordSysPtr &) const;


PXR_NAMESPACE_CLOSE_SCOPE
