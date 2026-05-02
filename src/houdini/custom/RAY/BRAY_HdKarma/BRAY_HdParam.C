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
#include "BRAY_HdDelegate.h"
#include "BRAY_HdInstancer.h"
#include "BRAY_HdLight.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdFormat.h"
#include "BRAY_HdTokens.h"
#include "BRAY_HdEncodeJSON.h"
#include <UT/UT_JSONWriter.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_Debug.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_ErrorLog.h>
#include <iostream>

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/usdRender/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

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
	    SYS_FALLTHROUGH;
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

    static const char *
    stringValue(const VtValue &val, const char *defval = "")
    {
        if (val.IsHolding<std::string>())
            return val.UncheckedGet<std::string>().c_str();
        if (val.IsHolding<TfToken>())
            return val.UncheckedGet<TfToken>().GetText();
        if (val.IsHolding<UT_StringHolder>())
            return val.UncheckedGet<UT_StringHolder>().c_str();
        return defval;
    }
}

#define MAX_XPU_GEO_MOTIONSEGS  8       // This may change in the future

BRAY_HdParam::BRAY_HdParam(BRAY::ScenePtr &scene,
	BRAY::RendererPtr &renderer,
	HdRenderThread &thread,
        BRAY_HdDelegate &delegate,
	SYS_AtomicInt32 &version)
    : myScene(scene)
    , myRenderer(renderer)
    , myThread(thread)
    , mySceneVersion(version)
    , myShutter {SHUTTER_OPEN_DEFAULT, SHUTTER_CLOSE_DEFAULT}
    , myResolution(-1, -1)
    , myRenderRes(-1, -1)
    , myDataWindow(0, 0, 1, 1)
    , myPixelAspect(1)
    , myConformPolicy(ConformPolicy::EXPAND_APERTURE)
    , myDisableMotionBlur(false)
    , myDisableDepthOfField(false)
    , myHoudiniViewport(false)
    , myStatsUpdateTime(120)
    , myMaxDeformSegments(scene.isKarmaXPU() ? MAX_XPU_GEO_MOTIONSEGS : SYS_INT32_MAX)
    , myNeedCryptoName(false)
    , myNeedCryptoMaterial(false)
    , myDelegate(delegate)
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
BRAY_HdParam::stopRendering()
{
    myRenderer.prepareForStop();
    myThread.StopRender();
    UT_ASSERT(!myRenderer.isRendering());
    clearRenderStats();
}


BRAY::ScenePtr &
BRAY_HdParam::getSceneForEdit()
{
    stopRendering();
    mySceneVersion.add(1);
    myScene.startEdits(myRenderer);
    return myScene;
}

void
BRAY_HdParam::queueInstancer(HdSceneDelegate *sd, BRAY_HdInstancer *instancer)
{
    UT_Lock::Scope	lock(myQueueLock);
    int level = instancer->getNestLevel();
    myQueuedInstancers.setSizeIfNeeded(level+1);
    myQueuedInstancers[level].insert(instancer);
}

void
BRAY_HdParam::addLightFilter(BRAY_HdLight *lp, const SdfPath &filter)
{
    myLightFilterMap[filter].insert(lp);
}

void
BRAY_HdParam::eraseLightFilter(BRAY_HdLight *lp)
{
    for (auto &&it : myLightFilterMap)
        it.second.erase(lp);
}

void
BRAY_HdParam::updateLightFilter(HdSceneDelegate *sd, const SdfPath &filter)
{
    auto it = myLightFilterMap.find(filter);
    if (it == myLightFilterMap.end())
    {
        // There's a light filter that isn't referenced by any light
        return;
    }
    for (auto lp : it->second)
        lp->updateLightFilter(sd, this, filter);
}

void
BRAY_HdParam::finalizeLightFilter(const SdfPath &filter)
{
    UT_ASSERT(9);
    auto it = myLightFilterMap.find(filter);
    if (it == myLightFilterMap.end())
    {
        // There's a light filter that isn't referenced by any light
        return;
    }
    for (auto lp : it->second)
        lp->finalizeLightFilter(this, filter);
}

void
BRAY_HdParam::removeQueuedInstancer(const BRAY_HdInstancer *instancer)
{
    UT_Lock::Scope	lock(myQueueLock);

    // BRAY_HdInstancer::getNestLevel() can cause problems here since parent
    // instancer may be synced first and deleted, leaving invalid parent path
    // and unknown nesting level.
    // So don't bother with counting level and simply attempt to erase from
    // every queue - no need to worry about performance since there won't be
    // many queues and they're almost always empty.
    //
    // Alternatively we could store queue index in the instancer but we want to
    // avoid unnecessary bookkeeping.
    for (exint i = 0, n = myQueuedInstancers.size(); i < n; ++i)
    {
        if (myQueuedInstancers[i].erase(SYSconst_cast(instancer)))
            break;
    }
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
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    exint	nq = getQueueCount();
    if (!nq)
	return;

    // Make sure to stop the render before processing
    stopRendering();

    // Make sure to bump version numbers
    auto &&scene = getSceneForEdit();

    // safe to clear shared data cache now that there's no more Sync()
    myDataSharingCache.clear();

    HdSceneDelegate *sd = nullptr;

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
                {
		    instances[idx++] = k;
                    UT_ASSERT(!sd || sd == k->GetDelegate());
                    if (!sd)
                        sd = k->GetDelegate();
                }
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

    // Hydra runs garbage collection on primvar value cache immediately after
    // all Sync() calls are done, and applyNesting() is called afterwards. So
    // when NestedInstances() is called for a parent instancer, its primvars
    // are extracted and put on the garbage collection queue but never get
    // cleaned up... UNTIL the next IPR update, which causes the legit
    // new/dirty primvars to be evicted from cache after Sync(), before we even
    // had a chance to extract them in applyNesting().
    //
    // Manually invoking PostSyncCleanup() here clears garbage collection queue
    // so that we don't lose data on the next update.
    //
    // (alternative and more canonical solution is to recursively extract
    // primvars for instancers upon Sync())
    if (sd)
        sd->PostSyncCleanup();

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
BRAY_HdParam::setDataWindow(const GfVec4f &v4)
{
    if (v4 == myDataWindow)
        return false;
    myDataWindow = v4;
    return true;
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
	auto policy = conformPolicy(token);
	changed = (policy != myConformPolicy);
	myConformPolicy = policy;
    }
    return changed;
}

bool
BRAY_HdParam::setDisableMotionBlur(const VtValue &val)
{
    bool	is = boolValue(val, myDisableMotionBlur);
    bool	changed = (is != myDisableMotionBlur);
    myDisableMotionBlur = is;
    return changed;
}

bool
BRAY_HdParam::setDisableDepthOfField(const VtValue &val)
{
    bool	disable = boolValue(val, myDisableDepthOfField);
    bool	changed = (disable != myDisableDepthOfField);
    myDisableDepthOfField = disable;
    return changed;
}

bool
BRAY_HdParam::differentCamera(const SdfPath &path) const
{
    return BRAY_HdUtil::toStr(path) != myCameraPath;
}

bool
BRAY_HdParam::setCameraPath(const UT_StringHolder &path)
{
    if (myCameraPath != path)
    {
	myCameraPath = path;
	myScene.sceneOptions().set(BRAY_OPT_RENDER_CAMERA, myCameraPath);
	return true;
    }
    return false;
}

bool
BRAY_HdParam::setCameraPath(const SdfPath &path)
{
    return setCameraPath(BRAY_HdUtil::toStr(path));
}

bool
BRAY_HdParam::setCameraPath(const VtValue &value)
{
    if (value.IsHolding<SdfPath>())
	return setCameraPath(value.UncheckedGet<SdfPath>());
    if (value.IsHolding<std::string>())
	return setCameraPath(value.UncheckedGet<std::string>());

    UT_ASSERT(0 && "The camera path should be an SdfPath");
    return false;
}

void
BRAY_HdParam::updateShutter(const SdfPath &id, fpreal open, fpreal close)
{
    if (myCameraPath == BRAY_HdUtil::toStr(id))
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
    return shutter != myShutter[INDEX];
}

void
BRAY_HdParam::fillShutterTimes(float *times, int nsegments) const
{
    if (myDisableMotionBlur)
	std::fill(times, times+nsegments, shutterMid());
    else
	fillTimes(times, nsegments, myShutter[0], myShutter[1]);
}

void
BRAY_HdParam::fillFrameTimes(float *times, int nsegments) const
{
    if (myDisableMotionBlur)
	std::fill(times, times+nsegments, shutterMid()*myIFPS);
    else
	fillTimes(times, nsegments, myShutter[0]*myIFPS, myShutter[1]*myIFPS);
}

void
BRAY_HdParam::shutterToFrameTime(float *frame,
        const float *shutter, int nsegs) const
{
    if (myDisableMotionBlur)
    {
	std::fill(frame, frame+nsegs, shutterMid()*myIFPS);
    }
    else
    {
        for (int i = 0; i < nsegs; ++i)
            frame[i] = shutter[i] * myIFPS;
    }
}

float
BRAY_HdParam::frameShutterTime() const
{
    // Compute the shutter time which corresponds to the actual frame.
    // This is used to compute frame-specific values (like the world to camera
    // transform stored in OpenEXR metadata).
    static constexpr float SHUTTER_MIN    = 1 - SYS_FP32_EPSILON;

    fpreal                      dshutter = myShutter[1] - myShutter[0];
    if (myDisableMotionBlur || SYSequalZero(dshutter))
        return shutterMid();

    // If the shutter brackets the frame
    if (myShutter[0] <= 0 && myShutter[1] >= 0)
        return SYSmin(-myShutter[0] / dshutter, SHUTTER_MIN);

    // If both shutters are to the left of the frame, 1 is closer to the frame
    if (myShutter[1] < 0)
        return SHUTTER_MIN;
    // If both shutters are to the right of the frame, 0 is closer to the frame
    if (myShutter[0] > 0)
        return 0;

    // Very weird case (likely shutter close is less than shutter open)
    return shutterMid();
}

namespace
{
    static bool
    isRenderActive(BRAY_RenderStage stage)
    {
        return stage != BRAY_STAGE_INACTIVE && stage != BRAY_STAGE_CONVERGED;
    }
    static bool
    needsUpdate(const BRAY::RendererPtr &ren,
                const VtDictionary &stats,
                const UT_StopWatch &timer,
                fpreal timeout,
                fpreal progress,
                BRAY_RenderStage curr_stage,
                BRAY_HdParam::StatsState stats_state)
    {
        using StatsState = BRAY_HdParam::StatsState;
        if (stats.size() == 0 || stats_state == StatsState::STATS_BEGIN)
        {
            return true;
        }
        if (stats.size() != 0 && stats_state == StatsState::STATS_END)
        {
            return false;
        }

        if (ren.isError())
        {
            return true;
        }

        const BRAY::Stats       &s = ren.stats();
        // If the renderer thinks it's done, but we think we're active, then we
        // need to update
        BRAY_RenderStage        stage = s.renderStage();
        if (stage != curr_stage)        // Update if there's a change in state
            return true;
        if (!isRenderActive(s.renderStage())
                    && stats_state == StatsState::STATS_ACTIVE)
        {
            return true;
        }

        // If the stats timer is up, or the percent complete has been bumped up
        // by over half a point.
        if (timer.lap() > timeout || s.percentComplete() > progress + 0.5)
            return true;

        return false;
    }
}


const VtDictionary &
BRAY_HdParam::getRenderStats() const
{
    if (myRenderer)
    {
        if (needsUpdate(myRenderer, myStats, myStatsTimer, myStatsUpdateTime,
                    myStatsProgress, myStatsStage, myStatsState))
        {
            UT_Lock::Scope      lock(myStatsLock);
            // Double check lock
            if (needsUpdate(myRenderer, myStats, myStatsTimer, myStatsUpdateTime,
                        myStatsProgress, myStatsStage, myStatsState))
            {
                SYSconst_cast(this)->updateStats();
            }
        }
    }
    return myStats;
}

void
BRAY_HdParam::setHoudiniViewport()
{
    // Everything we need to do for interactive husk
    setHuskInteractive();

    // Plus, tell BRAY we're rendering to the viewport
    myScene.setOption(BRAY_OPT_IPR_ENABLE, true);

    // Record the fact that we're being rendered from the Houdini viewport
    myHoudiniViewport = true;
}

void
BRAY_HdParam::setHuskInteractive()
{
    myStatsUpdateTime = 1;
}

void
BRAY_HdParam::clearRenderStats()
{
    // Don't actually clear the dictionary in case another thread is
    // referencing it while the renderer is being stopped.
    myStatsTimer.start();
    myStatsProgress = 0;
    myStatsStage = BRAY_STAGE_INACTIVE;
    myStatsState = StatsState::STATS_BEGIN;
}

void
BRAY_HdParam::updateStats()
{
    UT_ASSERT(myRenderer);
    const auto &stats = myRenderer.stats();

    // Update Stats state
    if (!isRenderActive(stats.renderStage()))
        myStatsState = StatsState::STATS_END;
    else
        myStatsState = StatsState::STATS_ACTIVE;
    myStatsProgress = stats.percentComplete();
    myStatsStage = stats.renderStage();
    myStatsTimer.start();       // Restart timer

    // Update VtDictionary
    for (int i =0; i < BRAY::Stats::MAX_KEYS; ++i)
    {
        BRAY::Stats::Key key = (BRAY::Stats::Key)i;
        BRAY::Stats::Value      value = stats.get(key);
        if (value)
        {
            UT_VERIFY(BRAY_HdEncodeJSON::insert(myStats, value.key(), value.get()));
        }
    }
    int ndev = stats.get(BRAY::Stats::XPU_DEVICE_COUNT).get().getI();
    if (ndev)
    {
        VtArray<VtDictionary>   xpu;
        xpu.resize(ndev);
        for (int dev = 0; dev < ndev; ++dev)
        {
            for (int i = 0; i < BRAY::Stats::DEVICE_MAX_KEYS; ++i)
            {
                BRAY::Stats::DeviceKey  key = (BRAY::Stats::DeviceKey)i;
                BRAY::Stats::Value      value = stats.get(key, dev);
                if (value)
                {
                    UT_VERIFY(BRAY_HdEncodeJSON::insert(xpu[dev], value.key(), value.get()));
                }
            }
        }
        myStats[BRAYHdTokens->xpu_devices] = VtValue::Take(xpu);
    }
    if (myRenderer.isError())
    {
        static const std::string theMessage =
            "Stopped rendering - fatal error (see Log Viewer Pane)";
        myStats[BRAYHdTokens->renderProgressAnnotation] = theMessage;
        myStats[BRAYHdTokens->huskErrorStatus] = 1;
    }
}

void
BRAY_HdParam::processRasterProducts(const VtValue &value)
{
    myGlobalPrimvars.clear();
    myNeedCryptoName = false;
    myNeedCryptoMaterial = false;
    if (value.IsEmpty())
        return;

    using delegateProduct = HdAovSettingsMap;
    using delegateVar = HdAovSettingsMap;
    using delegateProductList = VtArray<delegateProduct>;
    using delegateVarList = VtArray<delegateVar>;

    auto findString = [](const HdAovSettingsMap &map, const TfToken &token)
    {
        auto it = map.find(token);
        return it == map.end() ? UT_StringRef() : UT_StringRef(stringValue(it->second));
    };

    UT_ASSERT(value.IsHolding<delegateProductList>());
    delegateProductList plist = value.Get<delegateProductList>();
    for (const auto &prod : plist)
    {
        // We should have a raster product here
        UT_ASSERT(!strcmp(findString(prod, UsdRenderTokens->productType), "raster")
               || prod.find(UsdRenderTokens->productType) == prod.end());
        auto var_it = prod.find(UsdRenderTokens->orderedVars);
        if (var_it == prod.end())
            continue;           // No render vars
        UT_ASSERT(var_it->second.IsHolding<delegateVarList>());
        delegateVarList vlist = var_it->second.Get<delegateVarList>();
        for (const auto &var : vlist)
        {
            static constexpr UT_StringLit       theObjectName("__name");
            static constexpr UT_StringLit       theMaterialName("__materialname");

            UT_StringRef sourceType = findString(var, UsdRenderTokens->sourceType);
            UT_StringRef sourceName = findString(var, UsdRenderTokens->sourceName);
            //UT_StringRef dataType = findString(var, UsdRenderTokens->dataType);
            if (!strcmp(sourceType, "primvar"))
            {
                // skip prefix if any
                exint semi = sourceName.lastCharIndex(';');
                myGlobalPrimvars.insert(sourceName.c_str() + semi + 1);

                if (!myNeedCryptoName &&
                    sourceName.endsWith(theObjectName))
                {
                    // Need nice names for cryptomatte object layer
                    myNeedCryptoName = true;
                }
                if (!myNeedCryptoMaterial &&
                    sourceName.endsWith(theMaterialName))
                {
                    // Need nice names for cryptomatte material layer
                    myNeedCryptoMaterial = true;
                }
            }
        }
    }
}

void
BRAY_HdParam::addLightCategory(const UT_StringHolder &name)
{
    UT_Lock::Scope	lock(myQueueLock);
    auto                it = myLightCategories.find(name);

    if (it != myLightCategories.end())
        it->second++;
    else
        myLightCategories.emplace(name, 1);
}

bool
BRAY_HdParam::eraseLightCategory(const UT_StringHolder &name)
{
    UT_Lock::Scope lock(myQueueLock);
    auto it = myLightCategories.find(name);

    if (it != myLightCategories.end())
    {
        UT_ASSERT(it->second >= 1);
        if (it->second <= 1)
            myLightCategories.erase(it);
        else
            it->second--;

        return true;
    }

    return false;
}

bool
BRAY_HdParam::isValidLightCategory(const UT_StringHolder &name)
{
    UT_Lock::Scope	lock(myQueueLock);
    auto                it = myLightCategories.find(name);

    return (it != myLightCategories.end() && it->second > 0);
}

const TfToken &
BRAY_HdParam::conformPolicy(ConformPolicy p)
{
    switch (p)
    {
	case ConformPolicy::EXPAND_APERTURE:
	    return UsdRenderTokens->expandAperture;
	case ConformPolicy::CROP_APERTURE:
	    return UsdRenderTokens->cropAperture;
	case ConformPolicy::ADJUST_HAPERTURE:
	    return UsdRenderTokens->adjustApertureWidth;
	case ConformPolicy::ADJUST_VAPERTURE:
	    return UsdRenderTokens->adjustApertureHeight;
	case ConformPolicy::ADJUST_PIXEL_ASPECT:
	    return UsdRenderTokens->adjustPixelAspectRatio;
	case ConformPolicy::INVALID:
	    return BRAYHdTokens->invalidConformPolicy;
    }
    return BRAYHdTokens->invalidConformPolicy;
}

BRAY_HdParam::ConformPolicy
BRAY_HdParam::conformPolicy(const TfToken &policy)
{
    static UT_Map<TfToken, ConformPolicy>	theMap = {
	{ UsdRenderTokens->expandAperture, ConformPolicy::EXPAND_APERTURE},
	{ UsdRenderTokens->cropAperture, ConformPolicy::CROP_APERTURE},
	{ UsdRenderTokens->adjustApertureWidth, ConformPolicy::ADJUST_HAPERTURE},
	{ UsdRenderTokens->adjustApertureHeight, ConformPolicy::ADJUST_VAPERTURE},
	{ UsdRenderTokens->adjustPixelAspectRatio, ConformPolicy::ADJUST_PIXEL_ASPECT},
    };
    auto &&it = theMap.find(policy);
    if (it == theMap.end())
	return ConformPolicy::DEFAULT;
    return it->second;
}

template <typename T> bool
BRAY_HdParam::aspectConform(ConformPolicy conform,
		T &vaperture, T &pixel_aspect,
		T camaspect, T imgaspect)
{
    // Coming in:
    //	haperture = pixel_aspect * vaperture * camaspect
    // The goal is to make camaspect == imgaspect
    switch (conform)
    {
	case ConformPolicy::INVALID:
	case ConformPolicy::EXPAND_APERTURE:
	{
	    // So, vap = hap/imgaspect = vaperture*camaspect/imageaspect
	    T	vap = SYSsafediv(vaperture * camaspect, imgaspect);
	    if (vap <= vaperture)
		return false;
	    vaperture = vap;	// Increase aperture
	    return true;
	}
	case ConformPolicy::CROP_APERTURE:
	{
	    // So, vap = hap/imgaspect = vaperture*camaspect/imageaspect
	    T	vap = SYSsafediv(vaperture * camaspect, imgaspect);
	    if (vap >= vaperture)
		return false;
	    vaperture = vap;	// Shrink aperture
	    return true;
	}
	case ConformPolicy::ADJUST_HAPERTURE:
	    // Karma/HoudiniGL uses vertical aperture, so no need to change it
	    // here.
	    break;
	case ConformPolicy::ADJUST_VAPERTURE:
	{
	    T	hap = vaperture * camaspect;	// Get horizontal aperture
	    // We want to make ha/va = imgaspect
	    vaperture = hap / imgaspect;
	}
	return true;
	case ConformPolicy::ADJUST_PIXEL_ASPECT:
	{
	    // We can change the width of a pixel so that hap*aspect/va = img
	    pixel_aspect = SYSsafediv(camaspect, imgaspect);
	}
	return true;
    }
    return false;
}

bool
BRAY_HdParam::insertDataShareItem(DataSharingCache::accessor &found,
        const SdfPath &datasharingid)
{
    return myDataSharingCache.insert(found, BRAY_HdUtil::toStr(datasharingid));
}

bool
BRAY_HdParam::registerFieldToVolume(const UT_StringHolder &fieldname,
        const UT_StringHolder &volumename)
{
    UT_AutoWriteLock lock(myFieldToVolumesLock);
    UT_StringSet &volumes = myFieldToVolumes[fieldname];
    std::pair<UT_StringSet::iterator, bool> res = volumes.insert(volumename);
    return res.second;
}

UT_StringSet
BRAY_HdParam::getVolumes(const UT_StringHolder &fieldname) const
{
    UT_AutoReadLock lock(myFieldToVolumesLock);
    UT_StringSet result;
    auto it = myFieldToVolumes.find(fieldname);
    if (it != myFieldToVolumes.end())
        result = it->second;
    return result;
}

HdInstancer*
BRAY_HdParam::getInstancer(const SdfPath &instancerid) const
{
    const HdRenderIndex *renderIndex = myDelegate.getRenderIndex();
    return renderIndex ? renderIndex->GetInstancer(instancerid) : nullptr;
}

// Instantiate setShutter with open/close
template bool BRAY_HdParam::setShutter<0>(const VtValue &);
template bool BRAY_HdParam::setShutter<1>(const VtValue &);

#define INSTANTIATE_CONFORM(TYPE) \
    template bool BRAY_HdParam::aspectConform(BRAY_HdParam::ConformPolicy c, \
            TYPE &vaperture, TYPE &pixel_aspect, \
            TYPE cam_aspect, TYPE img_aspect); \
    /* end macro */

INSTANTIATE_CONFORM(fpreal32)
INSTANTIATE_CONFORM(fpreal64)

PXR_NAMESPACE_CLOSE_SCOPE
