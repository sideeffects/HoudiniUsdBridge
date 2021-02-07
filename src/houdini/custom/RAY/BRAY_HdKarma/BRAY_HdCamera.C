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
#include "BRAY_HdIO.h"
#include "BRAY_HdParam.h"
#include <UT/UT_SmallArray.h>
#include <UT/UT_WorkArgs.h>

#include <pxr/base/gf/range1f.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/sdf/assetPath.h>

#include <UT/UT_Debug.h>
#include <UT/UT_EnvControl.h>
#include <HUSD/HUSD_Constants.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_HydraUtils.h>
#include <HUSD/XUSD_Tokens.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

using namespace XUSD_HydraUtils;

namespace
{
    // The USD spec states that aperture and focal length are given in mm, but
    // the Hydra code converts these measurements to the "scene units",
    // assuming the scene units are centimetres.  That is, the focal and
    // aperture are divided by 10 to convert to cm.
    //
    // Since Karma expects the values to be in mm., we need to undo what Hydra
    // does by scaling the values back.
    static constexpr fpreal	theHydraCorrection = 10;

    class TokenMaker
    {
    public:
	TokenMaker(BRAY_CameraProperty prop)
	{
	    UT_WorkBuffer	tmp;
	    myString = BRAYproperty(tmp, BRAY_CAMERA_PROPERTY, prop,
						BRAY_HdUtil::parameterPrefix());
	    myToken = TfToken(myString.c_str(), TfToken::Immortal);
	}
	const TfToken	&token() const { return myToken; }
    private:
	UT_StringHolder	myString;
	TfToken		myToken;
    };

    static TokenMaker	theCameraWindow(BRAY_CAMERA_WINDOW);
    static TokenMaker	theCameraLensShader(BRAY_CAMERA_LENSSHADER);
    static TfToken	theUseLensShaderToken("karma:camera:use_lensshader",
					TfToken::Immortal);

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
	    XUSD_RenderSettings::HUSD_AspectConformPolicy policy,
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
	    XUSD_RenderSettings::aspectConform(policy,
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
	evalCameraAttrib<std::string>(str, sd, id, theCameraLensShader.token());
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
    , myResolution(0, 0)
    , myAspectConformPolicy(XUSD_RenderSettings::HUSD_AspectConformPolicy::
                            EXPAND_APERTURE)
    , myNeedConforming(false)
    , myAperturesHash(0)
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
}

static void
setFloatProperty(UT_Array<BRAY::OptionSet> &cprops, BRAY_CameraProperty brayprop,
	const UT_Array<VtValue> &values, fpreal scale=1)
{
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
    // If we're set by the viewport camera, or we haven't been created, or the
    // resolution hasn't changed, then just return.
    BRAY_HdParam &rparm = *UTverify_cast<BRAY_HdParam *>(renderParam);

    // Hash current aperture values and compare against the previous value to
    // determine if it needs to be updated (instead of calling
    // cameraProperties() and comparing directly since that's more costly)
    SYS_HashType apertureshash = 0;
    for (int i = 0, n = myHAperture.size(); i < n; ++i)
    {
        SYShashCombine(apertureshash, floatValue(myHAperture, i));
        SYShashCombine(apertureshash, floatValue(myVAperture, i));
    }

    if (!myNeedConforming || !myCamera ||
        (res == myResolution &&
         myAspectConformPolicy == rparm.conformPolicy() &&
         myAperturesHash == apertureshash) )
    {
	return;
    }

    myAperturesHash = apertureshash;

    UT_Array<BRAY::OptionSet> cprops = myCamera.cameraProperties();

    myResolution = res;
    myAspectConformPolicy = rparm.conformPolicy();

    fpreal64	pixel_aspect = rparm.pixelAspect();
    setAperture(cprops, rparm.conformPolicy(), myHAperture, myVAperture,
	    SYSsafediv(pixel_aspect*res[0], fpreal64(res[1])), pixel_aspect);
    if (lock_camera)
    {
	BRAY::ScenePtr	&scene = rparm.getSceneForEdit();
	myCamera.commitOptions(scene);
    }
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
    if (strstr(name, HUSD_Constants::getKarmaRendererPluginName()))
    {
	// Default viewport camera
	UT_Array<BRAY::OptionSet>	 cprops;

	bool viewdirty = *dirtyBits & DirtyViewMatrix;
	bool projdirty = *dirtyBits & DirtyProjMatrix;

	HdCamera::Sync(sd, renderParam, dirtyBits);

	// Following must be done after HdCamera::Sync()
	if (viewdirty)
	{
	    // Set transform
	    myCamera.setTransform(
		    BRAY_HdUtil::makeSpace(&_worldToViewMatrix, 1));
	    event = event | BRAY_EVENT_XFORM;
	}
	if (projdirty)
	{
	    myCamera.resizeCameraProperties(1);
	    cprops = myCamera.cameraProperties();

	    bool ortho = _projectionMatrix[2][3] == 0.0;
	    bool cvex = false;
	    evalCameraAttrib<bool>(cvex, sd, id, theUseLensShaderToken);

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
		fpreal a = _projectionMatrix[2][2];
		fpreal b = _projectionMatrix[3][2];
		//fpreal f = SYSsafediv(b, a+1);
		//fpreal n = -f * SYSsafediv(1 + a, 1 - a);
		fpreal	nf[2];
		nf[1] = SYSsafediv(b, a+1);
		nf[0] = -nf[1] * SYSsafediv(1 + a, 1 - a);
		cprops[0].set(BRAY_CAMERA_CLIP, nf, 2);
	    }
	    if (cvex)
	    {
		setShader(sd, id, myCamera, scene);
		GfVec3d x(1, 0, 0);
		x = _projectionMatrix.GetInverse().Transform(x);
		float cam_aspect = SYSsafediv(_projectionMatrix[0][0], _projectionMatrix[1][1]);
		cprops[0].set(BRAY_CAMERA_PROJECTION, (int)BRAY_PROJ_CVEX_SHADER);
		cprops[0].set(BRAY_CAMERA_ORTHO_WIDTH, x[0] * 2 * cam_aspect);
	    }
	    else
	    {
		if (ortho)
		{
		    GfVec3d x(1, 0, 0);
		    x = _projectionMatrix.GetInverse().Transform(x);
		    float cam_aspect = SYSsafediv(_projectionMatrix[0][0], _projectionMatrix[1][1]);
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
		_projectionMatrix[2][0]-1,
		_projectionMatrix[2][0]+1,
		_projectionMatrix[2][1]-1,
		_projectionMatrix[2][1]+1
	    };
	    cprops[0].set(BRAY_CAMERA_WINDOW, window, 4);
#endif

	    // Set focal, aperture, ortho, and clip range
	    // Just use default aperture for now
	    float aperture = *cprops[0].fval(BRAY_CAMERA_APERTURE);
	    float focal = _projectionMatrix[0][0] * aperture * 0.5f;
	    float cam_aspect = SYSsafediv(_projectionMatrix[1][1], _projectionMatrix[0][0]);
	    cprops[0].set(BRAY_CAMERA_FOCAL, focal * cam_aspect);
	}

	// When we don't have a camera aspect ratio from a camera schema, we
	// don't need to worry about conforming.
	myNeedConforming = false;
    }
    else
    {
	// Non-default cameras (tied to a UsdGeomCamera)
	VtValue vshutteropen = sd->Get(id, UsdGeomTokens->shutterOpen);
	VtValue vshutterclose = sd->Get(id, UsdGeomTokens->shutterClose);
	fpreal shutter[2] = { floatValue<fpreal>(vshutteropen),
			     floatValue<fpreal>(vshutterclose) };
	rparm.updateShutter(id, shutter[0], shutter[1]);

	// Since we have a camera aspect ratio defined, we need to worry about
	// the conforming policy.
	myNeedConforming = true;

	// Transform
	BRAY::OptionSet	oprops = myCamera.objectProperties();

	if (*dirtyBits & HdChangeTracker::DirtyParams)
	{
	    BRAY_HdUtil::updateObjectProperties(oprops, *sd, id);
	}

	int nsegs = BRAY_HdUtil::xformSamples(rparm, oprops);

	VtValue				projection;
	VtValue				unitscale_value;
	UT_SmallArray<GfMatrix4d>	mats;
	UT_SmallArray<VtValue>		focal;
	UT_SmallArray<VtValue>		focusDistance;
	UT_SmallArray<VtValue>		fStop;
	UT_SmallArray<VtValue>		screenWindow;
	UT_SmallArray<VtValue>		hOffset;
	UT_SmallArray<VtValue>		vOffset;
	UT_StackBuffer<float>		times(nsegs);
	bool				is_ortho = false;

	rparm.fillShutterTimes(times, nsegs);
	BRAY_HdUtil::xformBlur(sd, mats, id, times, nsegs);

	projection = sd->Get(id, UsdGeomTokens->projection);

	// Now, we need to invert the matrices
	for (int i = 0, n = mats.size(); i < n; ++i)
        {
	    mats[i] = mats[i].GetInverse();
            UT_ErrorLog::format(8, "{} xform[{}] - {}\n", id, i, mats[i]);
        }
	BRAY_HdUtil::dformCamera(sd, myHAperture, id,
		UsdGeomTokens->horizontalAperture, times, nsegs);
	BRAY_HdUtil::dformCamera(sd, myVAperture, id,
		UsdGeomTokens->verticalAperture, times, nsegs);
	BRAY_HdUtil::dformCamera(sd, hOffset, id,
		UsdGeomTokens->horizontalApertureOffset, times, nsegs);
	BRAY_HdUtil::dformCamera(sd, vOffset, id,
		UsdGeomTokens->verticalApertureOffset, times, nsegs);
	BRAY_HdUtil::dformCamera(sd, focal, id,
		UsdGeomTokens->focalLength, times, nsegs);
	BRAY_HdUtil::dformCamera(sd, focusDistance, id,
		UsdGeomTokens->focusDistance, times, nsegs);
	BRAY_HdUtil::dformCamera(sd, fStop, id,
		UsdGeomTokens->fStop, times, nsegs);
	BRAY_HdUtil::dformCamera(sd, screenWindow, id,
		theCameraWindow.token(), times, nsegs);

	if (projection.IsHolding<TfToken>()
		&& projection.UncheckedGet<TfToken>() == UsdGeomTokens->orthographic)
	{
	    is_ortho = true;
	}

	// Check to see if any of the camera properties have multiple segments
	exint	psize = 1;
	psize = SYSmax(psize, mats.size());
	psize = SYSmax(psize, myHAperture.size());
	psize = SYSmax(psize, myVAperture.size());
	psize = SYSmax(psize, hOffset.size());
	psize = SYSmax(psize, vOffset.size());
	psize = SYSmax(psize, focal.size());
	psize = SYSmax(psize, focusDistance.size());
	psize = SYSmax(psize, fStop.size());
	psize = SYSmax(psize, screenWindow.size());

        UT_ErrorLog::format(8, "{} view motion segments for {}", psize, id);
	myCamera.setTransform(BRAY_HdUtil::makeSpace(mats.data(), mats.size()));
	event = event | BRAY_EVENT_XFORM;

	myCamera.resizeCameraProperties(psize);
	UT_Array<BRAY::OptionSet> cprops = myCamera.cameraProperties();
	updateAperture(renderParam, rparm.resolution(), false);
	setFloatProperty(cprops, BRAY_CAMERA_FOCAL, focal, theHydraCorrection);
	setFloatProperty(cprops, BRAY_CAMERA_FOCUS_DISTANCE, focusDistance);
	setFloatProperty(cprops, BRAY_CAMERA_FSTOP, fStop);

        updateScreenWindow(screenWindow,
                hOffset, myHAperture, vOffset, myVAperture);
	if (screenWindow.size())
	    setVecProperty<GfVec4f>(cprops, BRAY_CAMERA_WINDOW, screenWindow);

	bool cvex = 0;
	evalCameraAttrib<bool>(cvex, sd, id, theUseLensShaderToken);
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


	// Cliprange and shutter should not be animated
	// (TODO: move them to scene/renderer option)
	VtValue vrange = sd->Get(id, UsdGeomTokens->clippingRange);
	for (auto &&cprop : cprops)
	{
	    cprop.set(BRAY_CAMERA_CLIP, float2Value(vrange).data(), 2);
	}

	// Shutter cannot be animated
	for (auto &&cprop : cprops)
	    cprop.set(BRAY_CAMERA_SHUTTER, shutter, 2);

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

    if ((*dirtyBits) & (~DirtyViewMatrix & AllDirty))
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

PXR_NAMESPACE_CLOSE_SCOPE
