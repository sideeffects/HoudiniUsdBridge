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

#include "BRAY_HdParam.h"
#include "BRAY_HdInstancer.h"
#include "BRAY_HdLight.h"
#include <UT/UT_JSONWriter.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_Debug.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_ErrorLog.h>
#include <HUSD/XUSD_Format.h>
#include <iostream>

#include <pxr/imaging/hd/sceneDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

#if 0
namespace
{
    template <typename T>
    void
    jsonValue(UT_JSONWriter &w, const char *token, const T &val)
    {
	w.jsonKeyToken(token);
	w.jsonValue(val);
    }
}
#endif

namespace
{
    static void
    fillTimes(float *times, int nsegments, float t0, float t1)
    {
	switch (nsegments)
	{
	case 1:
	    times[0] = (t0 + t1) * 0.5f;
	    break;
	default:
	    if (nsegments > 2)
	    {
		float	scale = (t1 - t0) / (nsegments - 1);
		for (int i = 1; i < nsegments-1; ++i)
		    times[i] = t0 + i*scale;
	    }
	// fall thru
	case 2:
	    times[0] = t0;
	    times[nsegments-1] = t1;
	}
    }

    static double
    floatValue(const VtValue &val, double defval)
    {
	if (val.IsHolding<double>())
	    return val.UncheckedGet<double>();
	if (val.IsHolding<float>())
	    return val.UncheckedGet<float>();
	return defval;
    }

    static bool
    boolValue(const VtValue &val, bool defval)
    {
	if (val.IsHolding<bool>())
	    return val.UncheckedGet<bool>();
	if (val.IsHolding<int32>())
	    return val.UncheckedGet<int32>();
	if (val.IsHolding<uint32>())
	    return val.UncheckedGet<uint32>();
	if (val.IsHolding<int64>())
	    return val.UncheckedGet<int64>();
	if (val.IsHolding<uint64>())
	    return val.UncheckedGet<uint64>();
	if (val.IsHolding<int8>())
	    return val.UncheckedGet<int8>();
	if (val.IsHolding<uint8>())
	    return val.UncheckedGet<uint8>();
	UT_ASSERT(0);
	return defval;
    }
}

BRAY_HdParam::BRAY_HdParam(BRAY::ScenePtr &scene,
	BRAY::RendererPtr &renderer,
	HdRenderThread &thread,
	SYS_AtomicInt32 &version)
    : myScene(scene)
    , myRenderer(renderer)
    , myThread(thread)
    , mySceneVersion(version)
    , myShutter {-0.25f, 0.25f}
    , myResolution(-1, -1)
    , myRenderRes(-1, -1)
    , myDataWindow(0, 0, 1, 1)
    , myPixelAspect(1)
    , myConformPolicy(ConformPolicy::EXPAND_APERTURE)
    , myInstantShutter(false)
{
    setFPS(24);
}

void
BRAY_HdParam::dump() const
{
    UT_AutoJSONWriter	w(std::cerr, false);
    dump(w);
}

void
BRAY_HdParam::dump(UT_JSONWriter &w) const
{
    w.jsonBeginMap();
    w.jsonEndMap();
}

void
BRAY_HdParam::queueInstancer(HdSceneDelegate *sd, BRAY_HdInstancer *instancer)
{
    UT_Lock::Scope	lock(myQueueLock);
    int level = instancer->getNestLevel();
    myQueuedInstancers.setSizeIfNeeded(level+1);
    myQueuedInstancers[level].insert(instancer);
}

exint
BRAY_HdParam::getQueueCount() const
{
    UT_Lock::Scope	lock(myQueueLock);
    exint result = 0;
    for (int i = 0, n = myQueuedInstancers.size(); i < n; ++i)
	result += myQueuedInstancers[i].size();
    return result;
}

void
BRAY_HdParam::processQueuedInstancers()
{
    exint	nq = getQueueCount();
    if (!nq)
	return;

    // Make sure to stop the render before processing
    stopRendering();

    // Make sure to bump version numbers
    auto &&scene = getSceneForEdit();

    // Process instancer that need nesting.  Processing leaf instancers may
    // queue up additional nesting levels.
    while (getQueueCount())
    {
	// Process bottom-up (leaf first)
	for (int i = myQueuedInstancers.size()-1; i >= 0; --i)
	{
	    QueuedInstances currqueue;
	    UTswap(myQueuedInstancers[i], currqueue);
	    if (currqueue.size())
	    {
		UT_StackBuffer<BRAY_HdInstancer *> instances(currqueue.size());
		int		idx = 0;
		for (auto &&k : currqueue)
		    instances[idx++] = k;
		UT_ASSERT(idx == currqueue.size());

		UTparallelForEachNumber(exint(currqueue.size()),
		    [&](const UT_BlockedRange<exint> &r) {
			for (auto i = r.begin(), n = r.end(); i < n; ++i)
			{
			    instances[i]->applyNesting(*this, scene);
			}
		    });

		// Need to break out of this for-loop and start over because
		// myQueuedInstancers may have been modified by applyNesting().
		break;
	    }
	}
    }
    return;
}

bool
BRAY_HdParam::setResolution(const VtValue &val)
{
    bool	changed = false;
    if (val.IsHolding<GfVec2i>())
    {
	changed = (myResolution != val.UncheckedGet<GfVec2i>());
	myResolution = val.UncheckedGet<GfVec2i>();
    }
    else
	UT_ErrorLog::error("Expected resolution to be 2-ints");
    return changed;
}

bool
BRAY_HdParam::setDataWindow(const VtValue &val)
{
    bool	changed = false;
    if (val.IsHolding<GfVec4f>())
    {
	changed = (myDataWindow != val.UncheckedGet<GfVec4f>());
	myDataWindow = val.UncheckedGet<GfVec4f>();
    }
    else
	UT_ErrorLog::error("Expected data window to be 4-floats");
    return changed;
}

bool
BRAY_HdParam::setPixelAspect(const VtValue &val)
{
    double	pa = floatValue(val, myPixelAspect);
    bool	changed = (pa != myPixelAspect);
    myPixelAspect = pa;
    return changed;
}

bool
BRAY_HdParam::setConformPolicy(const VtValue &val)
{
    bool	changed = false;
    if (val.IsHolding<TfToken>())
    {
	TfToken	token = val.UncheckedGet<TfToken>();
	auto policy = XUSD_RenderSettings::conformPolicy(token);
	changed = (policy != myConformPolicy);
	myConformPolicy = policy;
    }
    return changed;
}

bool
BRAY_HdParam::setInstantShutter(const VtValue &val)
{
    bool	is = boolValue(val, myInstantShutter);
    bool	changed = (is != myInstantShutter);
    myInstantShutter = is;
    return changed;
}

bool
BRAY_HdParam::setCameraPath(const SdfPath &path)
{
    if (myCameraPath != path)
    {
	myCameraPath = path;
	myScene.sceneOptions().set(BRAY_OPT_RENDER_CAMERA, path.GetText());
	return true;
    }
    return false;
}

bool
BRAY_HdParam::setCameraPath(const VtValue &value)
{
    if (value.IsHolding<SdfPath>())
	return setCameraPath(value.UncheckedGet<SdfPath>());

    UT_ASSERT(0 && "The camera path should be an SdfPath");
    return false;
}

void
BRAY_HdParam::updateShutter(const SdfPath &id, fpreal open, fpreal close)
{
    if (id == myCameraPath)
    {
	myShutter[0] = open;
	myShutter[1] = close;
    }
}

template <int INDEX>
bool
BRAY_HdParam::setShutter(const VtValue &open)
{
    SYS_STATIC_ASSERT(INDEX == 0 || INDEX == 1);
    float	shutter = myShutter[INDEX];
    myShutter[INDEX] = floatValue(open, myShutter[INDEX]);
    UT_ASSERT(myShutter[INDEX] >= -1 && myShutter[INDEX] <= 1);
    return shutter != myShutter[INDEX];
}

void
BRAY_HdParam::fillShutterTimes(float *times, int nsegments) const
{
    if (myInstantShutter)
	std::fill(times, times+nsegments, myShutter[0]);
    else
	fillTimes(times, nsegments, myShutter[0], myShutter[1]);
}

void
BRAY_HdParam::fillFrameTimes(float *times, int nsegments) const
{
    if (myInstantShutter)
	std::fill(times, times+nsegments, myShutter[0]*myIFPS);
    else
	fillTimes(times, nsegments, myShutter[0]*myIFPS, myShutter[1]*myIFPS);
}

void
BRAY_HdParam::addLightCategory(const UT_StringHolder &name)
{
    UT_Lock::Scope	lock(myQueueLock);
    myLightCategories.insert(name);
}

bool
BRAY_HdParam::eraseLightCategory(const UT_StringHolder &name)
{
    UT_Lock::Scope	lock(myQueueLock);
    bool result = myLightCategories.erase(name);
    return result;
}

bool
BRAY_HdParam::isValidLightCategory(const UT_StringHolder &name)
{
    UT_Lock::Scope	lock(myQueueLock);
    bool result = myLightCategories.count(name);
    return result;
}

// Instantiate setShutter with open/close
template bool BRAY_HdParam::setShutter<0>(const VtValue &);
template bool BRAY_HdParam::setShutter<1>(const VtValue &);

PXR_NAMESPACE_CLOSE_SCOPE
