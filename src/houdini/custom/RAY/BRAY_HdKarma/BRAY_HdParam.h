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

#ifndef __renderParam__
#define __renderParam__

#include <pxr/pxr.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>
#include <SYS/SYS_AtomicInt.h>
#include <UT/UT_Set.h>
#include <UT/UT_Lock.h>
#include <UT/UT_Map.h>
#include <UT/UT_UniquePtr.h>
#include <BRAY/BRAY_Interface.h>

class UT_JSONWriter;

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdInstancer;
class BRAY_HdLight;

class BRAY_HdParam : public HdRenderParam
{
public:
    BRAY_HdParam(BRAY::ScenePtr &scene,
	    BRAY::RendererPtr &renderer,
	    HdRenderThread &thread,
	    SYS_AtomicInt32 &version);
    virtual ~BRAY_HdParam() = default;

    void		stopRendering()
    {
	myRenderer.prepareForStop();
	myThread.StopRender();
	UT_ASSERT(!myRenderer.isRendering());
    }

    BRAY::ScenePtr	&getSceneForEdit()
    {
	stopRendering();
	mySceneVersion.add(1);
	return myScene;
    }

    void	queueInstancer(HdSceneDelegate *sd, BRAY_HdInstancer *inst);

    /// Return true if the render has been stopped for processing
    void	processQueuedInstancers();

    /// Global list of light categories
    void	addLightCategory(const UT_StringHolder &name);
    bool	eraseLightCategory(const UT_StringHolder &name);
    bool	isValidLightCategory(const UT_StringHolder &name);

    void	dump() const;
    void	dump(UT_JSONWriter &w) const;

    /// Check if there's any shutter
    bool	validShutter() const { return myShutter[1] > myShutter[0]; }
    /// Fill out times in the range of shutterOpen() to shutterClose()
    void	fillShutterTimes(float *times, int nsegments) const;
    /// Fill out times in time offsets (scaled by fps)
    void	fillFrameTimes(float *times, int nsegments) const;
    /// @{
    /// Return the raw shutter open/close times
    float	shutterOpen() const { return myShutter[0]; }
    float	shutterClose() const { return myShutter[1]; }
    /// @}

    const GfVec2i	&resolution() const { return myResolution; }
    const GfVec4f	&dataWindow() const { return myDataWindow; }
    float		 pixelAspect() const { return myPixelAspect; }

    bool	setResolution(const VtValue &val);
    bool	setDataWindow(const VtValue &val);
    bool	setPixelAspect(const VtValue &val);

    // Returns true if the shutter changed.  INDEX:0 == open, INDEX:1 == close
    template <int INDEX> bool	setShutter(const VtValue &open);

    // Return true if either open or close changed
    bool	setShutter(const VtValue &open, const VtValue &close)
		{
		    bool	change = false;
		    change  = setShutter<0>(open);
		    change |= setShutter<1>(close);
		    return change;
		}

    void	setFPS(fpreal v)
		{
		    myFPS = v;
		    myIFPS = 1/v;
		}
    float	fps() const { return myFPS; }

private:
    exint	getQueueCount() const;

    using QueuedInstances = UT_Set<BRAY_HdInstancer *>;
    UT_Array<QueuedInstances>		 myQueuedInstancers;
    mutable UT_Lock			 myQueueLock;
    BRAY::ScenePtr			 myScene;
    BRAY::RendererPtr			&myRenderer;
    HdRenderThread			&myThread;
    SYS_AtomicInt32			&mySceneVersion;
    GfVec2i				 myResolution;
    GfVec4f				 myDataWindow;
    double				 myPixelAspect;
    float				 myShutter[2];
    float				 myFPS;
    float				 myIFPS;

    UT_Set<UT_StringHolder>		myLightCategories;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

