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
*/

#include "RE_Wrapper.h"
#include <RE/RE_Render.h>

#include <RE/RE_OGLComputeGPU.h>
#include <RE/RE_OGLRender.h>
#include <RE/RE_Window.h>

static void
createOfflineRender()
{
    RE_OGLRender::setPerformBadDriverCheck(false);
    RE_OGLComputeGPU::init();
    RE_OGLComputeGPU::initStandalone(false);
#if 0
    UT_WorkBuffer           info;
    RE_Render           *r = RE_OGLComputeGPU::getRender();
    if (r)
    {
        RE_RenderAutoLock       lock(r);
        r->fetchDriverInfo(info, false);
        UTdebugFormat("OpenGL:\n{}", info);
    }
#endif
    RE_OGLRender::setPerformBadDriverCheck(true);
    if (!RE_OGLRender::hasGL3(3))
    {
        UTdebugFormat("No GL");
    }
    static UT_UniquePtr<RE_Window>      cwindow;
    RE_Render *ctx = REgetMainRender()->createNewOffscreenContext(cwindow);
    ctx->lockContextForRender();
}

RE_Wrapper::RE_Wrapper(bool createcontext)
    : mySetContext(false)
{
    if (createcontext && !REgetRender())
        createOfflineRender(); // For non-graphic apps

    if (createcontext && !RE_OGLRender::getCurrentRender() && REgetRender())
    {
        REgetRender()->makeCurrent();
        mySetContext = true;
    }
}

RE_Wrapper::~RE_Wrapper()
{
    if (mySetContext)
        RE_OGLRender::resetCurrent();
}

bool
RE_Wrapper::isOpenGLAvailable()
{
    return (RE_OGLRender::getCurrentRender() != nullptr);
}
