/*
 * Copyright 2020 Side Effects Software Inc.
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

#include "GEO_HAPIReader.h"
#include "GEO_HAPIUtils.h"
#include <UT/UT_FileUtil.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_UniquePtr.h>


GEO_HAPIReader::GEO_HAPIReader() : myHasPrim(false) {}

GEO_HAPIReader::~GEO_HAPIReader() {}

bool
GEO_HAPIReader::readHAPI(const std::string &filePath)
{
    myAssetPath = filePath;
    myModTime = UT_FileUtil::getFileModTime(filePath.c_str());

    // start an out of process Houdini Engine session
    HAPI_Session session;

    HAPI_ThriftServerOptions serverOptions{0, 0.f};
    serverOptions.autoClose = true;
    serverOptions.timeoutMs = 3000.0f;

    if (HAPI_RESULT_SUCCESS !=
        HAPI_StartThriftNamedPipeServer(&serverOptions, "hapi", nullptr))
    {
        return false;
    }

    if (HAPI_CreateThriftNamedPipeSession(&session, "hapi") !=
        HAPI_RESULT_SUCCESS)
    {
        return false;
    }

    HAPI_CookOptions cookOptions = HAPI_CookOptions_Create();
    cookOptions.handleSpherePartTypes = true;

    if (HAPI_RESULT_SUCCESS != HAPI_Initialize(&session, &cookOptions, true, -1,
                                               nullptr, nullptr, nullptr,
                                               nullptr, nullptr))
    {
        return false;
    }

    // Buffer for string values
    UT_WorkBuffer buf;

    // Load the asset from the given path
    HAPI_AssetLibraryId libraryId;

    ENSURE_SUCCESS(HAPI_LoadAssetLibraryFromFile(
        &session, filePath.c_str(), false, &libraryId),
	session);

    int geoCount;

    // Query Assets
    ENSURE_SUCCESS(
        HAPI_GetAvailableAssetCount(&session, libraryId, &geoCount),
	session);

    UT_UniquePtr<HAPI_StringHandle> assetNames(
        new HAPI_StringHandle[geoCount]);
    UT_UniquePtr<HAPI_NodeId> assetIds(new HAPI_NodeId[geoCount]);
    int namesCount = geoCount;
    ENSURE_SUCCESS(HAPI_GetAvailableAssets(
        &session, libraryId, assetNames.get(), namesCount),
	session);

    // Make a node for every available asset
    // TODO: Add argument to specify which asset to create
    //	     and hold geos based on frame time instead
    for (int i = 0; i < geoCount; i++)
    {
        CHECK_RETURN(GEOhapiExtractString(session, assetNames.get()[i], buf));

        // Nodes will begin to cook as they are created
        ENSURE_SUCCESS(HAPI_CreateNode(
            &session, -1, buf.buffer(), "Asset", true, assetIds.get() + i),
	    session);
    }

    // Wait for the nodes to finish cooking
    int cookStatus;
    HAPI_Result cookResult;

    do
    {
        cookResult = HAPI_GetStatus(
            &session, HAPI_STATUS_COOK_STATE, &cookStatus);
    } while (cookStatus > HAPI_STATE_MAX_READY_STATE &&
             cookResult == HAPI_RESULT_SUCCESS);

    ENSURE_SUCCESS(cookResult, session);

    myGeos.setSize(geoCount);

    // Search the assets for all displaying Geos (SOPs)
    for (int i = 0; i < geoCount; i++)
    {
        HAPI_GeoInfo geo;
        if (HAPI_RESULT_SUCCESS ==
            HAPI_GetDisplayGeoInfo(&session, assetIds.get()[i], &geo))
        {
            CHECK_RETURN(myGeos[i].loadGeoData(session, geo, buf));

            myHasPrim = true;
        }
    }

    CLEANUP(session);
    return true;
}

bool
GEO_HAPIReader::checkReusable(const std::string &filePath)
{
    exint modTime = UT_FileUtil::getFileModTime(filePath.c_str());
    return ((myAssetPath == filePath) && (myModTime == modTime));
}


