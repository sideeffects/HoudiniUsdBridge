/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	renderParam.h (Karma USD Plugin)
 *
 * COMMENTS:
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
    , myDataWindow(0, 0, 1, 1)
    , myPixelAspect(1)
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
    fillTimes(times, nsegments, myShutter[0], myShutter[1]);
}

void
BRAY_HdParam::fillFrameTimes(float *times, int nsegments) const
{
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
