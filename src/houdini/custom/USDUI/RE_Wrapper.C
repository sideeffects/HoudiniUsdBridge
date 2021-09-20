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

RE_Wrapper::RE_Wrapper(bool createcontext)
    : mySetContext(false)
{
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
