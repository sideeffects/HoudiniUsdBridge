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
#include "XUSD_Utils.h"
#include <UT/UT_DirUtil.h>
#include <UT/UT_ErrorManager.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_StdUtil.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringSet.h>
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
    SdfPath              campath = HUSDgetHoudiniFreeCameraSdfPath();
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
            UT_StringSet             filesmap;

            // Put the files into a string vector, eliminating duplicates as
            // we go (in case the same path is in the HOUDINI_PATH twice).
            for (auto &&file : files)
            {
                UT_String    fullpath(file);

                UTmakeAbsoluteFilePath(fullpath);
                if (!filesmap.contains(fullpath))
                {
                    sublayerpaths.push_back(fullpath.toStdString());
                    filesmap.insert(fullpath);
                }
            }
            stage->GetRootLayer()->SetSubLayerPaths(sublayerpaths);
            myCameraLayer = UsdUtilsFlattenLayerStack(stage);
            if (errman.getNumErrors())
            {
                UT_String    errmsgs;

                errman.getErrorMessages(errmsgs);
                std::cerr << "Problem loading FreeCamera.usda files:";
                std::cerr << std::endl;
                std::cerr << errmsgs;
                std::cerr << std::endl;
            }
        }
    }

    if (!myCameraLayer)
    {
        std::cerr << "Unable to compose FreeCamera.usda files." << std::endl;
    }
    if (!myCameraLayer->GetPrimAtPath(campath))
    {
        std::cerr << "No camera defined in FreeCamera.usda files." << std::endl;
        myCameraLayer.Reset();
    }
    if (!myCameraLayer && files.size() > 0)
    {
        for (auto &&file : files)
            std::cerr << "    " << file << std::endl;
    }

    initializeLayerData();
}

XUSD_MirrorRootLayerData::~XUSD_MirrorRootLayerData()
{
}
void
XUSD_MirrorRootLayerData::initializeLayerData()
{
    SdfPath              campath = HUSDgetHoudiniFreeCameraSdfPath();

    myLayer = SdfLayer::CreateAnonymous();
    SdfCreatePrimInLayer(myLayer, campath);
}

PXR_NAMESPACE_CLOSE_SCOPE

