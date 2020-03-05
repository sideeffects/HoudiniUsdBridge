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
GEO_HAPIReader::readHAPI(const std::string &filePath, const GEO_HAPIParameterMap &parmMap)
{
    myAssetPath = filePath;
    myModTime = UT_FileUtil::getFileModTime(filePath.c_str());
    myParms = parmMap;

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

    // Set up cooking options
    HAPI_CookOptions cookOptions = HAPI_CookOptions_Create();
    cookOptions.handleSpherePartTypes = true;
    cookOptions.packedPrimInstancingMode =
        HAPI_PACKEDPRIM_INSTANCING_MODE_HIERARCHY;

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
    int namesCount = geoCount;
    ENSURE_SUCCESS(HAPI_GetAvailableAssets(
        &session, libraryId, assetNames.get(), namesCount),
	session);

    // TODO: Add argument to specify which asset to create
    //	     and hold geos based on frame time instead
    // Load the first asset for now
    const int geoIndex = 0;

    if (geoIndex < 0 || geoIndex >= geoCount)
    {
	// TODO: Add an error message for and asset not being found
        CLEANUP(session);
	return false;
    }

    HAPI_NodeId assetId;

    CHECK_RETURN(
        GEOhapiExtractString(session, assetNames.get()[geoIndex], buf));

    ENSURE_SUCCESS(HAPI_CreateNode(&session, -1, buf.buffer(), "Asset", false,
                                   &assetId),
                   session);

    // Apply parameter changes to asset node
    HAPI_NodeInfo assetInfo;
    ENSURE_SUCCESS(HAPI_GetNodeInfo(&session, assetId, &assetInfo), session);
    
    UT_UniquePtr<HAPI_ParmInfo> parms(new HAPI_ParmInfo[assetInfo.parmCount]);
    ENSURE_SUCCESS(HAPI_GetParameters(
                       &session, assetId, parms.get(), 0, assetInfo.parmCount),
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
            keyBuf.sprintf("%s%s", GEO_HDA_PARM_NUMERIC_PREFIX, buf.buffer());
            std::string key = keyBuf.toStdString();

	    // set ints
            if (myParms.find(key) != myParms.end())
            {
		std::vector<std::string> valStrings = TfStringSplit(
                    myParms.at(key), GEO_HDA_PARM_SEPARATOR);

                // Ignore extra values if they are given
                const int outCount = SYSmin((int)valStrings.size(), parm->size);
                UT_ASSERT(outCount > 0);

                UT_UniquePtr<int> out(new int[outCount]);
                for (int i = 0; i < outCount; i++)
                {
                    const char *valString = valStrings.at(i).c_str();
                    out.get()[i] = SYSfastFloor(SYSatof64(valString));
                }

		ENSURE_SUCCESS(
                    HAPI_SetParmIntValues(&session, assetId, out.get(),
                                          parm->intValuesIndex, outCount),
                    session);
            }
        }
        else if (HAPI_ParmInfo_IsFloat(parm))
        {
            
            keyBuf.sprintf("%s%s", GEO_HDA_PARM_NUMERIC_PREFIX, buf.buffer());
            std::string key = keyBuf.toStdString();

            // set ints
            if (myParms.find(key) != myParms.end())
            {
                std::vector<std::string> valStrings = TfStringSplit(
                    myParms.at(key), GEO_HDA_PARM_SEPARATOR);

                // Ignore extra values if they are given
                const int outCount = SYSmin((int)valStrings.size(), parm->size);
                UT_ASSERT(outCount > 0);

                UT_UniquePtr<float> out(new float[outCount]);
                for (int i = 0; i < outCount; i++)
                {
                    const char *valString = valStrings.at(i).c_str();
                    out.get()[i] = SYSatof(valString);
                }

                ENSURE_SUCCESS(
                    HAPI_SetParmFloatValues(&session, assetId, out.get(),
                                            parm->floatValuesIndex, outCount),
                    session);
            }
        }
        else if (HAPI_ParmInfo_IsString(parm))
        {
            // set a string
            keyBuf.sprintf("%s%s", GEO_HDA_PARM_STRING_PREFIX, buf.buffer());
            std::string key = keyBuf.toStdString();

            if (myParms.find(key) != myParms.end())
            {
                const char *out = myParms.at(key).c_str();

                ENSURE_SUCCESS(HAPI_SetParmStringValue(
                                   &session, assetId, out, parm->id, 0),
                               session);
            }
        }
    }

    // Cook the Node
    ENSURE_SUCCESS(HAPI_CookNode(&session, assetId, nullptr), session);
    int cookStatus;
    HAPI_Result cookResult;

    do
    {
        cookResult = HAPI_GetStatus(
            &session, HAPI_STATUS_COOK_STATE, &cookStatus);
    } while (cookStatus > HAPI_STATE_MAX_READY_STATE &&
             cookResult == HAPI_RESULT_SUCCESS);

    ENSURE_SUCCESS(cookResult, session);

    // TODO: Have a HAPIGeo saved for each cook frame 
    // Store the geos in a different data structure so frames with identical
    // geometry can reference the same geo object
    myGeos.setSize(1);
    
    HAPI_GeoInfo geo;
    if (HAPI_RESULT_SUCCESS ==
        HAPI_GetDisplayGeoInfo(&session, assetId, &geo))
    {
        CHECK_RETURN(myGeos[0].loadGeoData(session, geo, buf));

        myHasPrim = true;
    }

    CLEANUP(session);
    return true;
}

bool
GEO_HAPIReader::checkReusable(const std::string &filePath,
                              const GEO_HAPIParameterMap &parmMap)
{
    exint modTime = UT_FileUtil::getFileModTime(filePath.c_str());
    return ((myAssetPath == filePath) && (myModTime == modTime) &&
            (myParms == parmMap));
}


