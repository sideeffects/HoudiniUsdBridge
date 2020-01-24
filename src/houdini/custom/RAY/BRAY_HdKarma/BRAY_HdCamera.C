/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdCamera.h (RAY Library, C++)
 *
 * COMMENTS:
 */

#include "BRAY_HdCamera.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdIO.h"
#include "BRAY_HdParam.h"
#include <UT/UT_SmallArray.h>

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
#include <HUSD/XUSD_BRAYUtil.h>
#include <HUSD/XUSD_Tokens.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

using namespace XUSD_HydraUtils;

namespace
{
    class TokenMaker
    {
    public:
	TokenMaker(BRAY_CameraProperty prop)
	{
	    UT_WorkBuffer	tmp;
	    myString = BRAYproperty(tmp, BRAY_CAMERA_PROPERTY, prop,
						HUSD_BRAY_NS::parameterPrefix());
	    myToken = TfToken(myString.c_str(), TfToken::Immortal);
	}
	const TfToken	&token() const { return myToken; }
    private:
	UT_StringHolder	myString;
	TfToken		myToken;
    };

    static TokenMaker	theCameraWindow(BRAY_CAMERA_WINDOW);

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
}

BRAY_HdCamera::BRAY_HdCamera(const SdfPath &id)
    : HdCamera(id)
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
	const UT_Array<VtValue> &values)
{
    int				n = cprops.size();

    UT_ASSERT(values.size() == n || values.size() == 1);
    if (values.size() == n)
    {
	for (int i = 0; i < n; ++i)
	    cprops[i].set(brayprop, floatValue(values[i]));
    }
    else
    {
	float	f = floatValue(values[0]);
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
BRAY_HdCamera::Sync(HdSceneDelegate *sd,
		    HdRenderParam *renderParam,
		    HdDirtyBits *dirtyBits)
{
    const SdfPath	&id = GetId();
    if (id.IsEmpty())	// Not a real camera?
	return;

    //UTdebugFormat("Sync Camera: {} {}", this, id);
    BRAY_HdParam	&rparm = *UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr	&scene = rparm.getSceneForEdit();

    if (strstr(id.GetText(),
	HUSD_Constants::getKarmaRendererPluginName().c_str()))
    {
	// Default camera
	if (!myCamera)
	    myCamera = scene.createCamera(id.GetString());

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
	}
	if (projdirty)
	{
	    myCamera.resizeCameraProperties(1);
	    cprops = myCamera.cameraProperties();
	    bool ortho = _projectionMatrix[2][3] == 0.0;

	    // The projection matrix is typically defined as
	    //  [ S 0  0 0
	    //    0 S  0 0
	    //    tx ty  -f/(f-n)  -1
	    //    0 0  -f*n/(f-n)  0 ]
	    // Where:
	    //   S = "zoom" ( 1/tan(FOV/2))
	    //   f = far clipping
	    //   n = near clipping
	    //   tx = 2d pan in x (NDC space)
	    //   ty = 2d pan in y (NDC space)
	    if (ortho)
	    {
		GfVec3d x(1,0,0);
		x = _projectionMatrix.GetInverse().Transform(x);
		float aspect = SYSsafediv(_projectionMatrix[0][0], _projectionMatrix[1][1]);
		cprops[0].set(BRAY_CAMERA_ORTHO, true);
		cprops[0].set(BRAY_CAMERA_ORTHO_WIDTH, x[0]*2*aspect);
	    }
	    else
	    {
		cprops[0].set(BRAY_CAMERA_ORTHO, false);
	    }

	    // Handle the projection offset
	    fpreal64	window[4] = {
		_projectionMatrix[2][0]-1,
		_projectionMatrix[2][0]+1,
		_projectionMatrix[2][1]-1,
		_projectionMatrix[2][1]+1
	    };
	    cprops[0].set(BRAY_CAMERA_WINDOW, window, 4);

	    // Set focal, aperture, ortho, and clip range
	    // Just use default aperture for now
	    float aperture = *cprops[0].fval(BRAY_CAMERA_APERTURE);
	    float focal = _projectionMatrix[0][0] * aperture * 0.5f;
	    float aspect = SYSsafediv(_projectionMatrix[1][1], _projectionMatrix[0][0]);
	    cprops[0].set(BRAY_CAMERA_FOCAL, focal * aspect);
	    cprops[0].set(BRAY_CAMERA_ASPECT, aspect);
	}
    }
    else
    {
	// Non-default cameras
	if (!myCamera)
	    myCamera = scene.createCamera(id.GetString());

	// Transform
	BRAY::OptionSet	oprops = myCamera.objectProperties();

	if (*dirtyBits & HdChangeTracker::DirtyParams)
	{
	    BRAY_HdUtil::updateObjectProperties(oprops, *sd, id);
	}

	int nsegs = BRAY_HdUtil::xformSamples(oprops);

	VtValue				projection;
	UT_SmallArray<GfMatrix4d>	mats;
	UT_SmallArray<VtValue>		haperture;
	UT_SmallArray<VtValue>		vaperture;
	UT_SmallArray<VtValue>		focal;
	UT_SmallArray<VtValue>		focusDistance;
	UT_SmallArray<VtValue>		fStop;
	UT_SmallArray<VtValue>		screenWindow;
	UT_StackBuffer<float>		times(nsegs);
	bool				is_ortho = false;

	sd->SamplePrimvar(id, UsdGeomTokens->projection, 1, times, &projection);

	rparm.fillShutterTimes(times, nsegs);
	BRAY_HdUtil::xformBlur(sd, mats, id, times, nsegs);

	// Now, we need to invert the matrices
	for (int i = 0, n = mats.size(); i < n; ++i)
	    mats[i] = mats[i].GetInverse();
	BRAY_HdUtil::dformCamera(sd, haperture, id,
		UsdGeomTokens->horizontalAperture, times, nsegs);
	BRAY_HdUtil::dformCamera(sd, vaperture, id,
		UsdGeomTokens->verticalAperture, times, nsegs);
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
	psize = SYSmax(psize, haperture.size());
	psize = SYSmax(psize, vaperture.size());
	psize = SYSmax(psize, focal.size());
	psize = SYSmax(psize, focusDistance.size());
	psize = SYSmax(psize, fStop.size());
	psize = SYSmax(psize, screenWindow.size());

	myCamera.setTransform(BRAY_HdUtil::makeSpace(mats.data(), mats.size()));
	myCamera.resizeCameraProperties(psize);
	UT_Array<BRAY::OptionSet> cprops = myCamera.cameraProperties();
	setFloatProperty(cprops, BRAY_CAMERA_APERTURE, vaperture);
	for (int i = 0, n = cprops.size(); i < n; ++i)
	{
	    float ha = floatValue(haperture[SYSmin(i, haperture.size()-1)]);
	    float va = floatValue(vaperture[SYSmin(i, vaperture.size()-1)]);
	    cprops[i].set(BRAY_CAMERA_ASPECT, SYSsafediv(ha, va));
	}
	setFloatProperty(cprops, BRAY_CAMERA_FOCAL, focal);
	setFloatProperty(cprops, BRAY_CAMERA_FOCUS_DISTANCE, focusDistance);
	setFloatProperty(cprops, BRAY_CAMERA_FSTOP, fStop);
	if (screenWindow.size())
	    setVecProperty<GfVec4f>(cprops, BRAY_CAMERA_WINDOW, screenWindow);
	if (is_ortho)
	{
	    for (auto &&cprop : cprops)
		cprop.set(BRAY_CAMERA_ORTHO, true);
	    setFloatProperty(cprops, BRAY_CAMERA_ORTHO_WIDTH, vaperture);
	}
	else
	{
	    for (auto &&cprop : cprops)
		cprop.set(BRAY_CAMERA_ORTHO, false);
	}

	// Cliprange and shutter should not be animated
	// (TODO: move them to scene/renderer option)
	VtValue vrange = sd->Get(id, UsdGeomTokens->clippingRange);
	for (auto &&cprop : cprops)
	{
	    cprop.set(BRAY_CAMERA_CLIP, float2Value(vrange).data(), 2);
	}

	// Shutter cannot be animated
	VtValue vshutteropen = sd->Get(id, UsdGeomTokens->shutterOpen);
	VtValue vshutterclose = sd->Get(id, UsdGeomTokens->shutterClose);
	fpreal shutter[2] = { floatValue<fpreal>(vshutteropen),
			     floatValue<fpreal>(vshutterclose) };
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

    myCamera.lockOptions(scene);

    *dirtyBits &= ~AllDirty;
}

HdDirtyBits
BRAY_HdCamera::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
