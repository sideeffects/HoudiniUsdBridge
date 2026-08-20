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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_ImagingEngine.C (HUSD Library, C++)
 */

#include "XUSD_ImagingEngine.h"

#include "HUSD_ErrorScope.h"
#include <FS/UT_DSO.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_String.h>
#include <UT/UT_UniquePtr.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

PXR_NAMESPACE_OPEN_SCOPE

typedef XUSD_ImagingEngine *(*XUSD_ImagingEngineCreator)
        (const UT_StringRef&, bool, bool);

UT_UniquePtr<XUSD_ImagingEngine>
XUSD_ImagingEngine::createImagingEngine(const UT_StringRef& renderer,
        bool force_null_hgi, bool use_scene_indices)
{
    static XUSD_ImagingEngineCreator theCreator;

    if (!theCreator)
    {
        const UT_PathSearch *searchpath;
        UT_String            dsopath;

        searchpath = UT_PathSearch::getInstance(UT_HOUDINI_DSO_PATH);
        searchpath->findFile(dsopath, "usdui/USD_UI" FS_DSO_EXTENSION);
        if (dsopath.isstring())
        {
            UT_DSO           dso;
            UT_StringHolder  fullpath;
            void            *funcptr;

            funcptr = dso.findProcedure(dsopath, "newImagingEngine", fullpath);
            theCreator = (XUSD_ImagingEngineCreator)funcptr;
            if (!funcptr)
            {
                UT_WorkBuffer   msg;
                msg.format("Unable to load DSO {}\n", dsopath);
                msg.appendFormat("System configuration error.  {}\n",
                        "Try running with HOUDINI_DSO_ERROR=1");
                HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
                UTformat("{}\n", msg);
                return UT_UniquePtr<XUSD_ImagingEngine>();
            }
        }
    }

    UT_ASSERT(theCreator);
    return UT_UniquePtr<XUSD_ImagingEngine>(
        theCreator(renderer, force_null_hgi, use_scene_indices));
}

XUSD_ImagingEngine::XUSD_ImagingEngine()
{
}

XUSD_ImagingEngine::~XUSD_ImagingEngine()
{
}

/* static */
TfTokenVector
XUSD_ImagingEngine::GetRendererPlugins()
{
    HfPluginDescVector pluginDescriptors;
    HdRendererPluginRegistry::GetInstance().GetPluginDescs(&pluginDescriptors);

    TfTokenVector plugins;
    for(size_t i = 0; i < pluginDescriptors.size(); ++i) {
        plugins.push_back(pluginDescriptors[i].id);
    }
    return plugins;
}

/* static */
std::string
XUSD_ImagingEngine::GetRendererDisplayName(TfToken const &id)
{
    HfPluginDesc pluginDescriptor;
    if (!TF_VERIFY(HdRendererPluginRegistry::GetInstance().
                   GetPluginDesc(id, &pluginDescriptor))) {
        return std::string();
    }

    return pluginDescriptor.displayName;
}

PXR_NAMESPACE_CLOSE_SCOPE
