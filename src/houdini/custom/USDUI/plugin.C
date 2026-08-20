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

#include "XUSD_ImagingEngineGL.h"
#include <UT/UT_DSOVersion.h>
#include <UT/UT_String.h>
#include <SYS/SYS_Version.h>
#include <SYS/SYS_Visibility.h>

extern "C"
{
    SYS_VISIBILITY_EXPORT extern
    PXR_NS::XUSD_ImagingEngine *
    newImagingEngine(const UT_StringRef &renderer,
            bool force_null_hgi, bool use_scene_indices)
    {
        return new PXR_NS::XUSD_ImagingEngineGL(PXR_NS::TfToken(renderer),
            force_null_hgi, use_scene_indices);
    }
}

