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

#include "BRAY_HdPass.h"
#include "BRAY_HdParam.h"
#include "BRAY_HdAOVBuffer.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdCamera.h"
#include <SYS/SYS_Hash.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/HUSD_HydraPrim.h>
#include <BRAY/BRAY_Types.h>
#include <UT/UT_ArenaInfo.h>
#include <UT/UT_Debug.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_SysClone.h>
#include <UT/UT_ParallelUtil.h>
#include <iostream>
#include <UT/UT_Rect.h>

#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/glf/diagnostic.h>
#include <pxr/imaging/glf/glContext.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/work/loops.h>
#include <pxr/usd/usdRender/tokens.h>

#include <random>

//#define DEBUG_AOVS

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static constexpr UT_StringLit theDriverAovPrefix("driver:parameters:aov:");
    static const TfToken theDriverAovName("driver:parameters:aov:name");
    static const TfToken theDriverAovFormat("driver:parameters:aov:format");
    static const TfToken theDriverAovMultiSample("driver:parameters:aov:multiSample");

    static BRAY::AOVBufferPtr
    emptyAOV()
    {
	static BRAY::AOVBufferPtr	theEmptyAOVPtr;
	return theEmptyAOVPtr;
    }

    enum bray_PlaneType
    {
	PLANE_INVALID = -1,
	PLANE_COLOR,
	PLANE_DEPTH,
	PLANE_PRIMID,
	PLANE_INSTANCEID,
	PLANE_NORMAL,
	PLANE_PRIMVAR
    };

    static bray_PlaneType
    planeType(const HdParsedAovToken &aov)
    {
	if (aov.name == HdAovTokens->color)
	    return PLANE_COLOR;
	if (aov.name == HdAovTokens->cameraDepth
		|| aov.name == HdAovTokens->depth)
	    return PLANE_DEPTH;
	if (aov.name == HdAovTokens->primId
                || aov.name == HdAovTokens->elementId)
	    return PLANE_PRIMID;
	if (aov.name == HdAovTokens->instanceId)
	    return PLANE_INSTANCEID;
	if (aov.name == HdAovTokens->Neye || aov.name == HdAovTokens->normal)
	    return PLANE_NORMAL;
	if (aov.isPrimvar)
	    return PLANE_PRIMVAR;
	return PLANE_INVALID;
    }

    static HdFormat
    parseFormat(const TfToken &aovFormat)
    {
	static UT_Map<UT_StringRef, HdFormat>	theMap({
		{ "float",	HdFormatFloat32 },
		{ "color2f",	HdFormatFloat32Vec2 },
		{ "color3f",	HdFormatFloat32Vec3 },
		{ "color4f",	HdFormatFloat32Vec4 },
		{ "float2",	HdFormatFloat32Vec2 },
		{ "float3",	HdFormatFloat32Vec3 },
		{ "float4",	HdFormatFloat32Vec4 },
		{ "half",	HdFormatFloat16 },
		{ "float16",	HdFormatFloat16 },
		{ "color2h",	HdFormatFloat16Vec2 },
		{ "color3h",	HdFormatFloat16Vec3 },
		{ "color4h",	HdFormatFloat16Vec4 },
		{ "half2",	HdFormatFloat16Vec2 },
		{ "half3",	HdFormatFloat16Vec3 },
		{ "half4",	HdFormatFloat16Vec4 },
		{ "u8",		HdFormatUNorm8 },
		{ "uint8",	HdFormatUNorm8 },
		{ "color2u8",	HdFormatUNorm8Vec2 },
		{ "color3u8",	HdFormatUNorm8Vec3 },
		{ "color4u8",	HdFormatUNorm8Vec4 },
		{ "i8",		HdFormatSNorm8 },
		{ "int8",	HdFormatSNorm8 },
		{ "color2i8",	HdFormatSNorm8Vec2 },
		{ "color3i8",	HdFormatSNorm8Vec3 },
		{ "color4i8",	HdFormatSNorm8Vec4 },
		{ "int",	HdFormatInt32 },
		{ "int2",	HdFormatInt32Vec2 },
		{ "int3",	HdFormatInt32Vec3 },
		{ "int4",	HdFormatInt32Vec4 },
		{ "uint",	HdFormatInt32 },
		{ "uint2",	HdFormatInt32Vec2 },
		{ "uint3",	HdFormatInt32Vec3 },
		{ "uint4",	HdFormatInt32Vec4 },
	    });
	auto it = theMap.find(BRAY_HdUtil::toStr(aovFormat));
	return it == theMap.end() ? HdFormatInvalid : it->second;
    }
}

BRAY_HdPass::BRAY_HdPass(HdRenderIndex *index,
	const HdRprimCollection &collection,
	BRAY_HdParam &rparm,
	BRAY::RendererPtr &renderer,
	HdRenderThread &renderThread,
	SYS_AtomicInt32 &sceneVersion,
	BRAY::ScenePtr &scene)
    : HdRenderPass(index, collection)
    , myRenderParam(rparm)
    , myRenderer(renderer)
    , myCameraMask(BRAY_RAY_NONE)
    , myShadowMask(BRAY_RAY_NONE)
    , myThread(renderThread)
    , mySceneVersion(sceneVersion)
    , myScene(scene)
    , myColorBuffer()
    , myWidth(0)
    , myHeight(0)
    , myView(1.0f)
    , myProj(1.0f)
    , myPixelAspect(1)
    , myLastVersion(-1)
    , myResolution(-1, -1)
    , myDataWindow(0, 0, 1, 1)
    , myValidAOVs(true)
{
}

BRAY_HdPass::~BRAY_HdPass()
{
    stopRendering();
}

bool
BRAY_HdPass::IsConverged() const
{
    // If there's an error, say we're converged so the render loop quits
    if (myRenderer.isError())
        return true;

    if (!myAOVBindings.size())
	return !myValidAOVs;
    for (auto &&b : myAOVBindings)
    {
	if (b.renderBuffer && !b.renderBuffer->IsConverged())
	    return false;
    }
    return true;
}

static void
setWindow(BRAY::ScenePtr &scn, BRAY_SceneOption opt, const UT_DimRect &r)
{
    int64	val[4] = { r.x(), r.y(), r.x2(), r.y2() };
    scn.setOption(opt, val, 4);
}

void
BRAY_HdPass::stopRendering()
{
    myRenderer.prepareForStop();
    myThread.StopRender();
    UT_ASSERT(!myRenderer.isRendering());
}

void
BRAY_HdPass::updateSceneResolution()
{
    int res[2];
    res[0] = myResolution[0] <= 0 ? int(myWidth) : myResolution[0];
    res[1] = myResolution[1] <= 0 ? int(myHeight) : myResolution[1];
    myScene.setOption(BRAY_OPT_RESOLUTION, res, 2);

    // Compute display window based on the cropWindow
    float	xmin = SYSceil(res[0] * myDataWindow[0]);
    float	ymin = SYSceil(res[1] * myDataWindow[1]);
    float	xmax = SYSceil(res[0] * myDataWindow[2] - 1);
    float	ymax = SYSceil(res[1] * myDataWindow[3] - 1);
    UT_DimRect	data_window = UT_InclusiveRect(int(xmin), int(ymin),
					int(xmax), int(ymax));

    setWindow(myScene, BRAY_OPT_DATAWINDOW, data_window);
}

static bool
isValid(const GfMatrix4d &m)
{
    const auto *d = m.data();
    for (int i = 0; i < 16; ++i)
        if (!SYSisFinite(d[i]))
            return false;
    return true;
}

void
BRAY_HdPass::_Execute(const HdRenderPassStateSharedPtr &renderPassState,
                             TfTokenVector const &renderTags)
{
    // Restart rendering if there are updates to instancing.  This process
    // might bump the scene version number, so it's important to do this prior
    // to loading the version number.
    myRenderParam.processQueuedInstancers();

    // Now, we can check to see if we need to restart
    bool	needStart = false;
    bool        needupdateaperture = false;
    if (myLastVersion != mySceneVersion.load())
    {
        stopRendering();
	needStart = true;
    }

    const HdCamera	*cam = renderPassState->GetCamera();
    if (cam && myRenderParam.differentCamera(cam->GetId()))
    {
        // When we detect a different camera, we need to stop the render
        // immediately before we set the render camera.
        stopRendering();
	needStart = true;
        myRenderParam.setCameraPath(cam->GetId());
        UT_ErrorLog::format(8, "Setting render camera: {}", cam->GetId());
    }
    else if (!cam)
    {
        UT_ErrorLog::error("No render camera defined in renderPassState");
    }

    BRAY_RayVisibility	camera = BRAY_RAY_NONE;
    BRAY_RayVisibility	shadow = BRAY_RAY_NONE;
    for (auto &&tag : renderTags)
    {
	switch (HUSD_HydraPrim::renderTag(tag))
	{
	    case HUSD_HydraPrim::TagGuide:
		camera = (camera | BRAY_GUIDE_CAMERA);
		shadow = (shadow | BRAY_GUIDE_SHADOW);
		break;

	    case HUSD_HydraPrim::TagProxy:
		camera = (camera | BRAY_PROXY_CAMERA);
		shadow = (shadow | BRAY_PROXY_SHADOW);
		break;

	    case HUSD_HydraPrim::TagRender:
		camera = (camera | BRAY_RAY_CAMERA);
		shadow = (shadow | BRAY_RAY_SHADOW);
		break;

	    case HUSD_HydraPrim::TagInvisible:
	    case HUSD_HydraPrim::TagDefault:
		break;

	    case HUSD_HydraPrim::NumRenderTags:
		camera = BRAY_ANY_CAMERA;
		shadow = BRAY_ANY_SHADOW;
		UT_ASSERT(0);
		break;
	}
    }
    if (camera == BRAY_RAY_NONE && renderTags.size())
	camera = BRAY_RAY_CAMERA;

    if (camera != myCameraMask || shadow != myShadowMask)
    {
	stopRendering();
        needStart = true;
	myScene.setCameraRayMask(camera);
	myScene.setShadowRayMask(shadow);
	myCameraMask = camera;
	myShadowMask = shadow;
    }

    // If the camera has changed, reset the sample buffer.
    GfVec4f	vp = renderPassState->GetViewport();

    // Handle camera framing
    const auto &displayWindow = renderPassState->GetFraming().displayWindow;
    const auto &dataWindow = renderPassState->GetFraming().dataWindow;
    if (!displayWindow.IsEmpty())
    {
        vp[2] = displayWindow.GetMax()[0] - displayWindow.GetMin()[0];
        vp[3] = displayWindow.GetMax()[1] - displayWindow.GetMin()[1];
    }
    if (dataWindow.IsValid())
    {
        fpreal  w = SYSsaferecip(vp[2] - 1);
        fpreal  h = SYSsaferecip(vp[3] - 1);
        GfVec4f v4(dataWindow.GetMinX() * w,
                    dataWindow.GetMinY() * h,
                    dataWindow.GetMaxX() * w,
                    dataWindow.GetMaxY() * h);
        myRenderParam.setDataWindow(v4);
    }
    myRenderParam.setRenderResolution(GfVec2i(vp[2], vp[3]));

    GfMatrix4d	view = renderPassState->GetWorldToViewMatrix();
    GfMatrix4d	proj = renderPassState->GetProjectionMatrix();
    if (isValid(proj) && isValid(view))
    {
        if (myView != view || myProj != proj)
        {
            stopRendering();
            needStart = true;
            needupdateaperture = true;
            myView = view;
            myProj = proj;
            UT_ErrorLog::format(8, "Update view/proj: {} {}", view, proj);
        }
    }

    // Determine whether we need to update the renderer attachments.
    //
    // It's possible for the passed in attachments to be empty, but that's
    // never a legal state for the renderer, so if that's the case we add
    // a color attachment that we can blit to the GL framebuffer. In order
    // to check whether we need to add this color attachment, we check both
    // the passed in attachments and also whether the renderer currently has
    // bound attachments.
    auto	attachments = renderPassState->GetAovBindings();
    if (attachments != myFullAOVBindings || !myRenderer.getAOVCount())
    {
	// In general, the render thread clears attachments, but make sure
	// they are cleared initially on this thread.
	stopRendering();
	needStart = true;
	myValidAOVs = true;
	myFullAOVBindings = attachments;
	myAOVBindings = attachments;

	// Filter out the bad AOVs
	if (!validateAOVs(myAOVBindings))
	{
	    // Prune out any invalid attachments
	    HdRenderPassAovBindingVector	tmpBindings;
	    for (auto &&aov : attachments)
	    {
		auto &&buf = UTverify_cast<BRAY_HdAOVBuffer *>(aov.renderBuffer);
		if (buf && buf->aovBuffer() != emptyAOV())
		    tmpBindings.push_back(aov);
#if defined(DEBUG_AOVS)
		else
		    UTdebugFormat("Delete AOV {}", aov.aovName);
#endif
	    }
	    myAOVBindings = tmpBindings;
	}

	if (myAOVBindings.empty())
	{
	    if (!myColorBuffer)
		myColorBuffer.reset(new BRAY_HdAOVBuffer(SdfPath::EmptyPath()));
	    // Create a default set of color/depth planes
	    HdRenderPassAovBinding	clr;
	    clr.aovName = HdAovTokens->color;
	    clr.renderBuffer = myColorBuffer.get();
	    clr.clearValue = VtValue(GfVec4f(0, 0, 0, 1));
	    myAOVBindings.push_back(clr);
	}
    }

    // If the viewport has changed, resize the sample buffer.  We need to do
    // this *after* we've updated any changes to AOVs
    bool	windowDirty = false;

    if (myResolution != myRenderParam.resolution())
    {
	myResolution = myRenderParam.resolution();
	windowDirty = true;
    }
    if (myDataWindow != myRenderParam.dataWindow())
    {
	myDataWindow = myRenderParam.dataWindow();
	windowDirty = true;
    }
    if (myPixelAspect != myRenderParam.pixelAspect())
    {
	myPixelAspect = myRenderParam.pixelAspect();
	stopRendering();
	needStart = true;
	myScene.setOption(BRAY_OPT_PIXELASPECT, myPixelAspect);
    }
    if (myWidth != vp[2] || myHeight != vp[3] || windowDirty)
    {
	stopRendering();
	needStart = true;
        needupdateaperture = true;
        myWidth = vp[2];
        myHeight = vp[3];
	updateSceneResolution();
    }

    if (needupdateaperture)
    {
	stopRendering();
        needStart = true;
	const BRAY_HdCamera	*hcam = dynamic_cast<const BRAY_HdCamera *>(cam);
	if (hcam)
	{
	    int	imgres[2];
	    myScene.sceneOptions().import(BRAY_OPT_RESOLUTION, imgres, 2);
	    SYSconst_cast(hcam)->updateAperture(&myRenderParam,
		    GfVec2i(imgres[0], imgres[1]));
	}
    }

    // Reset the sample buffer if it's been requested.
    if (needStart)
    {
        UT_ErrorLog::format(8, "Restart Hydra render ({} AOVs)",
                myAOVBindings.size());
	for (auto &&aov : myAOVBindings)
	    UTverify_cast<BRAY_HdAOVBuffer *>(aov.renderBuffer)->clearConverged();

        // When rendering for IPR, update the random seed on every iteration
        if (*myScene.sceneOptions().bval(BRAY_OPT_IPR_INC_RANDOM))
        {
            int seed = *myScene.sceneOptions().ival(BRAY_OPT_RANDOMSEED);
            seed = SYSwang_inthash(seed + 37);
            myScene.sceneOptions().set(BRAY_OPT_RANDOMSEED, seed);
        }

        // Set version stamp for when I render
	myLastVersion = mySceneVersion.load();
        if (myRenderer.prepareRender())
        {
            if (myScene.optionB(BRAY_OPT_HD_FOREGROUND))
                myRenderer.render();
            else
                myThread.StartRender();
        }
        else
        {
            UT_ASSERT(0
                    && "How did prepare fail?"
                    && "Was the aperture 0?");
            UT_ASSERT(myRenderer.isError());
        }
    }
    else if (myRenderer.isPaused())
    {
        if (myThread.IsStopRequested())
        {
            // If the renderer is paused, this will cause it to wake up to
            // stop properly.
            myRenderer.prepareForStop();
        }
    }
}

bool
BRAY_HdPass::validateRenderSettings(const HdRenderPassAovBinding &aov,
	HdRenderBuffer *abuf) const
{
    auto findKey = [](const HdAovSettingsMap &aovSettings,
			    const TfToken &token, VtValue &val)
    {
	auto it = aovSettings.find(token);
	if (it == aovSettings.end())
	    return false;
	val = it->second;
	return true;
    };

#define EXTRACT_DATA(TYPE, NAME, KEY) \
    TYPE NAME; \
    if (!findKey(aov.aovSettings, KEY, val)) return false; \
    if (val.IsHolding<TYPE>()) { NAME = val.UncheckedGet<TYPE>(); } \
    else { \
	UTdebugFormat("Expected {} to be {}", #NAME, #TYPE); \
	return false; \
    } \
    /* end macro */

    VtValue	val;
    EXTRACT_DATA(TfToken, dataType, UsdRenderTokens->dataType);
    EXTRACT_DATA(TfToken, sourceType, UsdRenderTokens->sourceType);
    EXTRACT_DATA(std::string, sourceName, UsdRenderTokens->sourceName);
    EXTRACT_DATA(std::string, aovName, theDriverAovName);
    EXTRACT_DATA(TfToken, aovFormat, theDriverAovFormat);

    bool	multiSample = true;
    if (findKey(aov.aovSettings, theDriverAovMultiSample, val))
    {
	if (val.IsHolding<bool>())
	    multiSample = val.UncheckedGet<bool>();
    }

    // Check to see the format for the plane is correct
    HdFormat		format = parseFormat(aovFormat);
    if (format == HdFormatInvalid)
    {
	UTdebugFormat("Invalid Format: {}", aovFormat);
	return false;
    }

    // sourceType := { raw, primvar, lpe, intrinsic }
    if (sourceType == UsdRenderTokens->lpe)
    {
	static constexpr UT_StringLit	theLPEPrefix("lpe:");
	if (!UT_StringWrap(sourceName.c_str()).startsWith(theLPEPrefix))
	{
	    UT_WorkBuffer	tmp;
	    tmp.strcpy("lpe:");
	    tmp.append(sourceName);
	    sourceName = tmp.toStdString();
	}
    }
    else if (sourceType == UsdRenderTokens->raw)
    {
	// Unnamespaced source is assumed to be vex export
	if (UT_StringWrap(sourceName.c_str()).findChar(':') == nullptr)
	{
	    UT_WorkBuffer	tmp;
	    tmp.strcpy("vex:");
	    tmp.append(sourceName);
	    sourceName = tmp.toStdString();
	}
    }
    else if (sourceType == UsdRenderTokens->primvar)
    {
	static constexpr UT_StringLit	thePrimvarPrefix("primvar:");
	if (!UT_StringWrap(sourceName.c_str()).startsWith(thePrimvarPrefix))
	{
	    UT_WorkBuffer	tmp;
	    tmp.strcpy("primvar:");
	    tmp.append(sourceName);
	    sourceName = tmp.toStdString();
	}
    }

    int			tuplesize = HdGetComponentCount(format);
    PXL_DataFormat	dataformat;
    switch (HdGetComponentFormat(format))
    {
	case HdFormatUNorm8:
	case HdFormatSNorm8:
	    dataformat = PXL_INT8;
	    break;
	case HdFormatFloat16:
	    dataformat = PXL_FLOAT16;
	    break;
	case HdFormatFloat32:
	    dataformat = PXL_FLOAT32;
	    break;
	case HdFormatInt32:
	    dataformat = PXL_INT32;
	    break;
	default:
	    UT_ASSERT(0 && "Scalar type not handled");
	    dataformat = PXL_FLOAT32;
	    break;
    }

    BRAY::OptionSet	opts = myScene.planeProperties();
    opts.set(BRAY_PLANE_SAMPLING, int(multiSample ? 0 : 1));
    for (auto &&v : aov.aovSettings)
    {
	const TfToken	&key = v.first;
	if (UT_StringWrap(key.GetText()).startsWith(theDriverAovPrefix))
	{
	    const char 	*name = key.GetText() + theDriverAovPrefix.length();
	    BRAY_PlaneProperty prop = BRAYplaneProperty(name);
	    if (prop != BRAY_PLANE_INVALID_PROPERTY)
	    {
		//BRAY_HdUtil::dumpValue(v.second, name);
		BRAY_HdUtil::setOption(opts, prop, v.second);
	    }
	}
    }

    // Add AOV to renderer
    BRAY::RendererPtr::ImagePlane plane = {
	    aovName, sourceName, tuplesize, dataformat, opts
	};
    BRAY::AOVBufferPtr aovbufferptr = myRenderer.addOutputPlane(plane);

    auto *buf = UTverify_cast<BRAY_HdAOVBuffer *>(abuf);
    buf->setAOVBuffer(aovbufferptr);
    return true;
}

bool
BRAY_HdPass::validateAOVs(HdRenderPassAovBindingVector &bindings) const
{
    myRenderer.clearOutputPlanes();

    int	nvalid = 0;
    UT_Set<UT_StringHolder>     added_names;
    for (int i = 0, n = bindings.size(); i < n; ++i)
    {
	auto	&&b = bindings[i];
	auto	&&abuf = b.renderBuffer;

	if (!abuf)
	{
	    UTdebugFormat("AOV {} has no renderbuffer", b.aovName);
	    break;
	}

	if (validateRenderSettings(b, abuf))
	{
	    nvalid++;		// Valid from render settings
	    continue;
	}

	bool	isvalid = true;
	auto makeInvalid = [&](const char *msg)
	{
	    UTdebugFormat("{}", msg);
	    isvalid = false;
	};


	HdParsedAovToken	aov(b.aovName);
	bray_PlaneType		ptype = planeType(aov);

	if (ptype == PLANE_INVALID)
	{
	    UT_ErrorLog::error("Unsupported AOV settings for: {}", aov.name);
	    UTverify_cast<BRAY_HdAOVBuffer *>(abuf)->setConverged();
	    isvalid = false;
	    continue;
	}

	if (isvalid)
	{
	    // Check to see the format for the plane is correct
	    HdFormat	format = abuf->GetFormat();
	    PXL_DataFormat	dataformat;
	    int		tuplesize = HdGetComponentCount(format);
	    UT_StringHolder aovname;
	    UT_StringHolder aovvar;
	    float defaultval = 0.0f;
	    switch (HdGetComponentFormat(format))
	    {
		case HdFormatUNorm8:
		case HdFormatSNorm8:
		    dataformat = PXL_INT8;
		    break;
		case HdFormatFloat16:
		    dataformat = PXL_FLOAT16;
		    break;
		case HdFormatFloat32:
		    dataformat = PXL_FLOAT32;
		    break;
		case HdFormatInt32:
		    dataformat = PXL_INT32;
		    break;
		default:
		    UT_ASSERT(0 && "Scalar type not handled");
		    dataformat = PXL_FLOAT32;
		    break;
	    }
	    switch (ptype)
	    {
		case PLANE_INVALID:
		    isvalid = false;
		    break;
		case PLANE_COLOR:
		{
		    aovname = "Cf";
		    aovvar = "lpe:C.*";
		    if (format == HdFormatFloat16Vec3 ||
			format == HdFormatFloat16Vec4)
		    {
			dataformat = PXL_FLOAT16;
		    }
		    else if (format == HdFormatFloat32Vec3 ||
			format == HdFormatFloat32Vec4)
		    {
			dataformat = PXL_FLOAT32;
		    }
		    else if (format == HdFormatUNorm8Vec3 ||
			    format == HdFormatUNorm8Vec4 ||
			    format == HdFormatSNorm8Vec3 ||
			    format == HdFormatSNorm8Vec4)
		    {
			dataformat = PXL_INT8;
		    }
		    else
		    {
			makeInvalid("Invalid format for color plane");
		    }
		    break;
		}
		case PLANE_DEPTH:
		{
		    aovname = "Pz";
		    aovvar = BRAYrayImport(BRAY_RAYIMPORT_HIT_Pz);
		    dataformat = PXL_FLOAT32;
		    if (format != HdFormatFloat32)
			makeInvalid("Invalid depth format");
		    break;
		}
		case PLANE_PRIMID:
		{
		    aovname = "PrimId";
		    aovvar = BRAYrayImport(BRAY_RAYIMPORT_HD_PRIM);
		    dataformat = PXL_INT32;
		    if (format != HdFormatInt32)
			makeInvalid("Invalid primId format");
		    defaultval = -1.0f;
		    break;
		}
		case PLANE_INSTANCEID:
		{
		    aovname = "InstanceId";
		    aovvar = BRAYrayImport(BRAY_RAYIMPORT_HD_INST);
		    dataformat = PXL_INT32;
		    if (format != HdFormatInt32)
			makeInvalid("Invalid instanceId format");
		    defaultval = -1.0f;
		    break;
		}
		case PLANE_NORMAL:
		{
		    aovname = "N";
		    aovvar = BRAYrayImport(BRAY_RAYIMPORT_HIT_N);
		    dataformat = PXL_FLOAT32;
		    if (format != HdFormatFloat32Vec3
			&& format != HdFormatFloat16Vec3)
		    {
			makeInvalid("Invalid normal format");
		    }
		    break;
		}
		case PLANE_PRIMVAR:
		    break;
	    }
            auto added = added_names.insert(aovname);
            if (!added.second)
            {
                // Duplicate AOV
                isvalid = false;
            }
	    if (isvalid)
	    {
		BRAY::OptionSet	opts = myScene.planeProperties();
		opts.set(BRAY_PLANE_SAMPLING,
			int(abuf->IsMultiSampled() ? 0 : 1));
		opts.set(BRAY_PLANE_DEFAULT_VALUE, defaultval);
		// Add AOV to renderer
		BRAY::RendererPtr::ImagePlane plane = {
			aovname, aovvar, tuplesize, dataformat, opts
		    };
		BRAY::AOVBufferPtr aovbufferptr =
		    myRenderer.addOutputPlane(plane);

		auto *buf = UTverify_cast<BRAY_HdAOVBuffer *>(abuf);
		buf->setAOVBuffer(aovbufferptr);
		nvalid++;
	    }
	}
	if (!isvalid)
	{
	    // Clear existing assignment
	    auto *buf = UTverify_cast<BRAY_HdAOVBuffer *>(abuf);
	    buf->setAOVBuffer(emptyAOV());
#if defined(DEBUG_AOVS)
	    UTdebugFormat("Invalid: {}", b.aovName);
#endif
	}
    }
    return nvalid == bindings.size() && nvalid != 0;
}

PXR_NAMESPACE_CLOSE_SCOPE
