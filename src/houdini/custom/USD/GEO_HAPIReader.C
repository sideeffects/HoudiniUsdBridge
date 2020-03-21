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

GEO_HAPIReader::GEO_HAPIReader()
    : myHasPrim(false), myAssetId(-1), myTime(0.f), mySessionId(-1)
{
}

GEO_HAPIReader::~GEO_HAPIReader()
{
    if (mySessionId >= 0)
    {
        // Delete the node we created
        if (myAssetId >= 0)
        {
            GEO_HAPISessionManager::SessionScopeLock lock(mySessionId);
            HAPI_Session &session = lock.getSession();
            if (HAPI_IsSessionValid(&session) == HAPI_RESULT_SUCCESS)
            {
                HAPI_DeleteNode(&session, myAssetId);
            }
        }

        GEO_HAPISessionManager::unregister(mySessionId);
    }
}

bool
GEO_HAPIReader::init(const std::string &filePath, const std::string &assetName)
{
    myAssetName = assetName;
    myAssetPath = filePath;
    myModTime = UT_FileUtil::getFileModTime(filePath.c_str());

    if (mySessionId < 0)
    {
        mySessionId = GEO_HAPISessionManager::registerAsUser();
        if (mySessionId < 0)
            return false;
    }

    // Take control of the session
    GEO_HAPISessionManager::SessionScopeLock scopeLock(mySessionId);
    HAPI_Session &session = scopeLock.getSession();

    // Load the asset from the given path
    HAPI_AssetLibraryId libraryId;

    ENSURE_SUCCESS(HAPI_LoadAssetLibraryFromFile(
                       &session, filePath.c_str(), true, &libraryId),
                   session);

    int geoCount;

    // Query Assets
    ENSURE_SUCCESS(
        HAPI_GetAvailableAssetCount(&session, libraryId, &geoCount), session);

    UT_UniquePtr<HAPI_StringHandle> assetNames(new HAPI_StringHandle[geoCount]);
    ENSURE_SUCCESS(HAPI_GetAvailableAssets(
                       &session, libraryId, assetNames.get(), geoCount),
                   session);

    int geoIndex;
    UT_WorkBuffer buf;

    if (assetName.empty())
    {
        // Load the first asset if none was specified
        geoIndex = 0;

        CHECK_RETURN(
            GEOhapiExtractString(session, assetNames.get()[geoIndex], buf));
    }
    else
    {
        geoIndex = -1;
        exint i = 0;

        while (geoIndex < 0 && i < geoCount)
        {
            CHECK_RETURN(
                GEOhapiExtractString(session, assetNames.get()[i], buf));

            if (SYSstrcasecmp(buf.buffer(), assetName.c_str()) == 0)
                geoIndex = i;

            ++i;
        }
    }

    if (geoIndex < 0 || geoIndex >= geoCount)
    {
        // TODO: Add an error message for an asset not being found
        return false;
    }

    // If a node was created before, delete it
    if (myAssetId >= 0)
    {
        HAPI_DeleteNode(&session, myAssetId);
        myAssetId = -1;
    }

    ENSURE_SUCCESS(
        HAPI_CreateNode(&session, -1, buf.buffer(), nullptr, false, &myAssetId),
        session);

    return true;
}

bool
GEO_HAPIReader::readHAPI(const GEO_HAPIParameterMap &parmMap, float time)
{
    // Check that init was successfully called
    UT_ASSERT(mySessionId >= 0 && myAssetId >= 0);

    if (myHasPrim && myTime == time && myParms == parmMap)
    {
        // Use our old data if the parms and time haven't changed
        return true;
    }

    myTime = time;
    myParms = parmMap;
    
    // TODO: This is unnecessary if geos are saved by frame
    myGeos.clear();
    myHasPrim = false;

    // Take control of the session
    GEO_HAPISessionManager::SessionScopeLock scopeLock(mySessionId);
    HAPI_Session &session = scopeLock.getSession();

    // Buffer for string values
    UT_WorkBuffer buf;

    // Get the node created in init()
    HAPI_NodeInfo assetInfo;
    ENSURE_SUCCESS(HAPI_GetNodeInfo(&session, myAssetId, &assetInfo), session);
    
    // Apply parameter changes to asset node if the node has parameters
    if (assetInfo.parmCount > 0)
    {
        UT_UniquePtr<HAPI_ParmInfo> parms(
            new HAPI_ParmInfo[assetInfo.parmCount]);
        ENSURE_SUCCESS(HAPI_GetParameters(&session, myAssetId, parms.get(), 0,
                                          assetInfo.parmCount),
                       session);

        UT_WorkBuffer keyBuf;
        for (int i = 0; i < assetInfo.parmCount; i++)
        {
            HAPI_ParmInfo *parm = parms.get() + i;

            // Fill buf with the parameter name
            CHECK_RETURN(GEOhapiExtractString(session, parm->nameSH, buf));

            // Check what type Houdini Engine expects for this parameter
            if (HAPI_ParmInfo_IsInt(parm))
            {
                keyBuf.sprintf(
                    "%s%s", GEO_HDA_PARM_NUMERIC_PREFIX, buf.buffer());
                std::string key = keyBuf.toStdString();

                // set ints
                if (myParms.find(key) != myParms.end())
                {
                    std::vector<std::string> valStrings = TfStringSplit(
                        myParms.at(key), GEO_HDA_PARM_SEPARATOR);

                    // Ignore extra values if they are given
                    const int outCount = SYSmin(
                        (int)valStrings.size(), parm->size);
                    UT_ASSERT(outCount > 0);

                    UT_UniquePtr<int> out(new int[outCount]);
                    for (int i = 0; i < outCount; i++)
                    {
                        const char *valString = valStrings.at(i).c_str();
                        out.get()[i] = SYSfastFloor(SYSatof64(valString));
                    }

                    ENSURE_SUCCESS(
                        HAPI_SetParmIntValues(&session, myAssetId, out.get(),
                                              parm->intValuesIndex, outCount),
                        session);
                }
            }
            else if (HAPI_ParmInfo_IsFloat(parm))
            {
                keyBuf.sprintf(
                    "%s%s", GEO_HDA_PARM_NUMERIC_PREFIX, buf.buffer());
                std::string key = keyBuf.toStdString();

                // set ints
                if (myParms.find(key) != myParms.end())
                {
                    std::vector<std::string> valStrings = TfStringSplit(
                        myParms.at(key), GEO_HDA_PARM_SEPARATOR);

                    // Ignore extra values if they are given
                    const int outCount = SYSmin(
                        (int)valStrings.size(), parm->size);
                    UT_ASSERT(outCount > 0);

                    UT_UniquePtr<float> out(new float[outCount]);
                    for (int i = 0; i < outCount; i++)
                    {
                        const char *valString = valStrings.at(i).c_str();
                        out.get()[i] = SYSatof(valString);
                    }

                    ENSURE_SUCCESS(HAPI_SetParmFloatValues(
                                       &session, myAssetId, out.get(),
                                       parm->floatValuesIndex, outCount),
                                   session);
                }
            }
            else if (HAPI_ParmInfo_IsString(parm))
            {
                // set a string
                keyBuf.sprintf(
                    "%s%s", GEO_HDA_PARM_STRING_PREFIX, buf.buffer());
                std::string key = keyBuf.toStdString();

                if (myParms.find(key) != myParms.end())
                {
                    const char *out = myParms.at(key).c_str();

                    ENSURE_SUCCESS(HAPI_SetParmStringValue(
                                       &session, myAssetId, out, parm->id, 0),
                                   session);
                }
            }
        }
    }

    // Set the session time
    ENSURE_SUCCESS(HAPI_SetTime(&session, myTime), session);

    // Cook the Node
    ENSURE_SUCCESS(HAPI_CookNode(&session, myAssetId, nullptr), session);
    int cookStatus;
    HAPI_Result cookResult;

    do
    {
        cookResult = HAPI_GetStatus(
            &session, HAPI_STATUS_COOK_STATE, &cookStatus);
    } while (cookStatus > HAPI_STATE_MAX_READY_STATE &&
             cookResult == HAPI_RESULT_SUCCESS);

    ENSURE_COOK_SUCCESS(cookResult, session);

    // TODO: Organize geos by time
    myGeos.setSize(1);

    HAPI_GeoInfo geo;
    if (HAPI_RESULT_SUCCESS ==
        HAPI_GetDisplayGeoInfo(&session, myAssetId, &geo))
    {
        CHECK_RETURN(myGeos[0].loadGeoData(session, geo, buf));

        myHasPrim = true;
    }
    return true;
}

bool
GEO_HAPIReader::checkReusable(const std::string &filePath,
                              const std::string &assetName)
{
    exint modTime = UT_FileUtil::getFileModTime(filePath.c_str());
    return ((myAssetPath == filePath) && (myModTime == modTime) && (myAssetName == assetName));
}
