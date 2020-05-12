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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "XUSD_MirrorRootLayerData.h"
#include "HUSD_ErrorScope.h"
#include <UT/UT_ErrorManager.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_StdUtil.h>
#include <UT/UT_StringArray.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

#define XUSD_FREE_CAMERA_FILE "FreeCamera.usda"

XUSD_MirrorRootLayerData::XUSD_MirrorRootLayerData()
{
    const UT_PathSearch *search = UT_PathSearch::getInstance(UT_HOUDINI_PATH);
    UT_StringArray       files;

    if (search)
    {
        search->findAllFiles(XUSD_FREE_CAMERA_FILE, files);
        if (files.size() > 0)
        {
            UT_ErrorManager          errman;
            HUSD_ErrorScope          errorscope(&errman, false);
            UsdStageRefPtr           stage = UsdStage::CreateInMemory();
            std::vector<std::string> sublayerpaths;

            UTarrayToStdVectorOfStrings(files, sublayerpaths);
            stage->GetRootLayer()->SetSubLayerPaths(sublayerpaths);
            myLayer = UsdUtilsFlattenLayerStack(stage);
            if (errman.getNumErrors())
            {
                UT_String            errmsgs;

                errman.getErrorMessages(errmsgs);
                std::cerr << "Problem loading FreeCamera.usda files:";
                std::cerr << std::endl;
                std::cerr << errmsgs;
                std::cerr << std::endl;
            }
        }
    }

    if (!myLayer)
    {
        std::cerr << "Unable to compose FreeCamera.usda files." << std::endl;
        if (files.size() > 0)
        {
            for (auto &&file : files)
                std::cerr << "    " << file << std::endl;
        }
        myLayer = SdfLayer::CreateAnonymous();
    }
}

XUSD_MirrorRootLayerData::~XUSD_MirrorRootLayerData()
{
}

PXR_NAMESPACE_CLOSE_SCOPE

