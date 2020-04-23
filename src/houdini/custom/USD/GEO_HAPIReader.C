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
#include <SYS/SYS_Math.h>
#include <UT/UT_FileUtil.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_UniquePtr.h>

//
// GEO_HAPITimeCacheInfo
//

bool
GEO_HAPITimeCacheInfo::operator==(const GEO_HAPITimeCacheInfo &rhs)
{
    if (myCacheMethod != GEO_HAPI_TIME_CACHING_RANGE)
    {
        return myCacheMethod == rhs.myCacheMethod;
    }
    return (myCacheMethod == rhs.myCacheMethod) &&
           (SYSisEqual(myStartTime, rhs.myStartTime)) &&
           (SYSisEqual(myEndTime, rhs.myEndTime)) &&
           (SYSisEqual(myInterval, rhs.myInterval));
}

bool
GEO_HAPITimeCacheInfo::operator!=(const GEO_HAPITimeCacheInfo &rhs)
{
    return !operator==(rhs);
}

//
// GEO_HAPIReader
//

GEO_HAPIReader::GEO_HAPIReader()
    : myAssetId(-1), mySessionId(-1), myReadSuccess(false)
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

// For sorting in a UT_Array
// The items will be sorted by time with tolerance
static int
timeComparator(const GEO_HAPITimeSample *lhs, const GEO_HAPITimeSample *rhs)
{
    fpreal32 l = lhs->first;
    fpreal32 r = rhs->first;
    if (SYSisEqual(l, r))
        return 0;
    return (l < r) ? -1 : 1;
}

// Returns the index of the found sample and -1 otherwise
static exint
findTimeSample(const UT_Array<GEO_HAPITimeSample> &samples, float time)
{
    // Look for the index first so 'time' has tolerance
    GEO_HAPITimeSample sampleToFind;
    sampleToFind.first = time;
    return samples.uniqueSortedFind(sampleToFind, timeComparator);
}

// Adds a time sample to the array, keeping it sorted
// Returns the index of the added element
static exint
addTimeSample(UT_Array<GEO_HAPITimeSample> &samples, float time)
{
    UT_Array<GEO_HAPITimeSample> tempArray;
    GEO_HAPITimeSample tempSample(time, GEO_HAPIGeoHandle());
    tempArray.append(tempSample);
    samples.sortedUnion(tempArray, timeComparator);

    return samples.uniqueSortedFind(tempSample, timeComparator);
}

bool
GEO_HAPIReader::hasPrimAtTime(float time) const
{
    return (findTimeSample(myGeos, time)) >= 0;
}

GEO_HAPIGeoHandle
GEO_HAPIReader::getGeo(float time)
{
    exint geoIndex = findTimeSample(myGeos, time);

    return (geoIndex >= 0) ? myGeos(geoIndex).second : GEO_HAPIGeoHandle();
}

bool
GEO_HAPIReader::init(const std::string &filePath, const std::string &assetName)
{
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
        myUsingDefaultAssetName = true;

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

        myUsingDefaultAssetName = (geoIndex == 0);
    }

    if (geoIndex < 0 || geoIndex >= geoCount)
    {
        TF_WARN("Asset \"%s\" not found", assetName.c_str());
        return false;
    }

    // Save the asset name used
    myAssetName = buf.buffer();

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

// Assumes myParms and myAssetId have been updated
bool
GEO_HAPIReader::updateParms(const HAPI_Session &session,
                            const HAPI_NodeInfo &assetInfo,
                            UT_WorkBuffer &buf)
{
    UT_UniquePtr<HAPI_ParmInfo> parms(new HAPI_ParmInfo[assetInfo.parmCount]);
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

                // Setting parameters cooks the node again, so check if it can be
                // avoided
                bool setParms = false;
                UT_UniquePtr<int> currentParmVals(new int[outCount]);
                ENSURE_SUCCESS(HAPI_GetParmIntValues(
                                   &session, myAssetId, currentParmVals.get(),
                                   parm->intValuesIndex, outCount),
                               session);
                for (int i = 0; i < outCount; i++)
                {
                    if (!SYSisEqual(currentParmVals.get()[i], out.get()[i]))
                    {
                        setParms = true;
                        break;
                    }
                }

                if (setParms)
                {
                    ENSURE_SUCCESS(
                        HAPI_SetParmIntValues(&session, myAssetId, out.get(),
                                              parm->intValuesIndex, outCount),
                        session);
                }
            }
        }
        else if (HAPI_ParmInfo_IsFloat(parm))
        {
            keyBuf.sprintf("%s%s", GEO_HDA_PARM_NUMERIC_PREFIX, buf.buffer());
            std::string key = keyBuf.toStdString();

            // set floats
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

                // Setting parameters cooks the node again, so check if it can be
                // avoided
                bool setParms = false;
                UT_UniquePtr<float> currentParmVals(new float[outCount]);
                ENSURE_SUCCESS(HAPI_GetParmFloatValues(
                                   &session, myAssetId, currentParmVals.get(),
                                   parm->floatValuesIndex, outCount),
                               session);
                for (int i = 0; i < outCount; i++)
                {
                    if (!SYSisEqual(currentParmVals.get()[i], out.get()[i]))
                    {
                        setParms = true;
                        break;
                    }
                }

                if (setParms)
                {
                    ENSURE_SUCCESS(HAPI_SetParmFloatValues(
                                       &session, myAssetId, out.get(),
                                       parm->floatValuesIndex, outCount),
                                   session);
                }
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

                // Setting parameters cooks the node again, so check if it can
                // be avoided
                HAPI_StringHandle parmSH;
                ENSURE_SUCCESS(
                    HAPI_GetParmStringValue(
                        &session, myAssetId, buf.buffer(), 0, false, &parmSH),
                    session);

                // Fill buf with the parameter's current value
                CHECK_RETURN(GEOhapiExtractString(session, parmSH, buf));

                if (strcmp(out, buf.buffer()))
                {
                    ENSURE_SUCCESS(HAPI_SetParmStringValue(
                                       &session, myAssetId, out, parm->id, 0),
                                   session);
                }
            }
        }
    }

    return true;
}

static bool
cookAtTime(const HAPI_Session &session, HAPI_NodeId assetId, float time)
{
    // Set the session time
    ENSURE_SUCCESS(HAPI_SetTime(&session, time), session);

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

    ENSURE_COOK_SUCCESS(cookResult, session);
    return true;
}

bool
GEO_HAPIReader::readHAPI(const GEO_HAPIParameterMap &parmMap,
                         fpreal32 time,
                         const GEO_HAPITimeCacheInfo &cacheInfo)
{
    // Check that init was successfully called
    UT_ASSERT(mySessionId >= 0 && myAssetId >= 0);

    bool resetParms = (myParms != parmMap);

    // If cached geos were cooked with different parameters, there is no
    // reason to store them anymore
    if (resetParms)
    {
        myGeos.clear();
    }

    if (myReadSuccess && hasPrim())
    {
        exint timeIndex = findTimeSample(myGeos, time);

        if (timeIndex >= 0)
        {
            // Clear the cache if we are told not to cache data from other
            // time samples
            if (cacheInfo.myCacheMethod == GEO_HAPI_TIME_CACHING_NONE)
            {
                // Keep the time sample we are about to use so we don't need
                // to reload it right away
                GEO_HAPIGeoHandle g = myGeos(timeIndex).second;
                myGeos.clear();
                myGeos.append(GEO_HAPITimeSample(time, g));
                myTimeCacheInfo = cacheInfo;
            }

            // We have already cached data for this time and parmMap
            return true;
        }
    }

    // Get rid of any stored data if the last time we loaded data caused an
    // error
    if (!myReadSuccess)
    {
        myGeos.clear();
    }
    myReadSuccess = false;

    // Take control of the session
    GEO_HAPISessionManager::SessionScopeLock scopeLock(mySessionId);
    HAPI_Session &session = scopeLock.getSession();

    // Buffer for reading string values from Houdini Engine
    UT_WorkBuffer buf;

    // Get the node created in init()
    HAPI_NodeInfo assetInfo;
    ENSURE_SUCCESS(HAPI_GetNodeInfo(&session, myAssetId, &assetInfo), session);

    // Ensure the passed asset is geometry
    if (!(assetInfo.type & (HAPI_NODETYPE_OBJ | HAPI_NODETYPE_SOP)))
    {
        TF_WARN("Unable to find geometry in asset: %s",
                        myAssetPath.buffer());
        // return true and just throw a warning to prevent this node from
        // attempting to load multiple times
        return true;
    }

    // Apply parameter changes to asset node
    if (resetParms && assetInfo.parmCount > 0)
    {
        myParms = parmMap;
        updateParms(session, assetInfo, buf);
    }

    // Check one adjacent cached time to reuse their data if possible
    // Sets timeIndex to the index of the newly added sample
    auto addNewTime = [&](fpreal32 timeToAdd, exint &timeIndex) -> bool 
    {
        // Ensure myProcessedTimes remains unique and sorted
        UT_ASSERT(findTimeSample(myGeos, timeToAdd) < 0);

        timeIndex = addTimeSample(myGeos, timeToAdd);

        UT_ASSERT(timeIndex >= 0);

        HAPI_GeoInfo geo;
        bool reusingGeo = false;

        // Check the previous available time to see if data can be reused
        if (timeIndex > 0)
        {
            const GEO_HAPITimeSample &prev = myGeos(timeIndex - 1);
            CHECK_RETURN(cookAtTime(session, myAssetId, prev.first));
            CHECK_RETURN(cookAtTime(session, myAssetId, timeToAdd));

            if (HAPI_RESULT_SUCCESS ==
                HAPI_GetDisplayGeoInfo(&session, myAssetId, &geo))
            {
                if (!geo.hasGeoChanged)
                {
                    reusingGeo = true;
                    myGeos(timeIndex).second = prev.second;
                }
            }
        }
        // Check the next available time to see if data can be reused
        else if (timeIndex + 1 < myGeos.entries())
        {
            const GEO_HAPITimeSample &next = myGeos(timeIndex + 1);
            CHECK_RETURN(cookAtTime(session, myAssetId, next.first));
            CHECK_RETURN(cookAtTime(session, myAssetId, timeToAdd));

            if (HAPI_RESULT_SUCCESS ==
                HAPI_GetDisplayGeoInfo(&session, myAssetId, &geo))
            {
                if (!geo.hasGeoChanged)
                {
                    reusingGeo = true;
                    myGeos(timeIndex).second = next.second;
                }
            }
        }

        // Found nothing that matches this time sample, so load the data from
        // Houdini Engine
        if (!reusingGeo)
        {
            CHECK_RETURN(cookAtTime(session, myAssetId, timeToAdd))

            if (HAPI_RESULT_SUCCESS ==
                HAPI_GetDisplayGeoInfo(&session, myAssetId, &geo))
            {
                myGeos(timeIndex).second.reset(new GEO_HAPIGeo);
                CHECK_RETURN(myGeos(timeIndex).second->loadGeoData(session, geo, buf));
            }
            else
            {
                TF_WARN("Unable to find geometry in asset: %s",
                        myAssetPath.buffer());
            }
        }

        return true;
    };

    // Nothing is cached, so just cook the node and load the data
    if (cacheInfo.myCacheMethod == GEO_HAPI_TIME_CACHING_NONE)
    {
        exint index;
        CHECK_RETURN(addNewTime(time, index));

        // Do not cache any other time samples
        GEO_HAPIGeoHandle g = myGeos(index).second;
        myGeos.clear();
        myGeos.append(GEO_HAPITimeSample(time, g));

        if (!g)
            return false;
    }
    else if (cacheInfo.myCacheMethod == GEO_HAPI_TIME_CACHING_CONTINUOUS)
    {
        exint i;
        CHECK_RETURN(addNewTime(time, i));
        // Check if the geo failed to add
        if (!myGeos(i).second)
            return false;
    }
    else if (cacheInfo.myCacheMethod == GEO_HAPI_TIME_CACHING_RANGE)
    {
        bool loadedNewTime = false;

        // Check validity
        if (SYSisGreater(cacheInfo.myEndTime, cacheInfo.myStartTime) &&
            SYSisGreater(cacheInfo.myInterval, 0.f))
        {
            // Load all the geos in the range
            if (myTimeCacheInfo != cacheInfo)
            {
                // This check is to avoid clearing the cache when a geometry is
                // loaded with default time caching settings and set to
                // GEO_HAPI_TIME_CACHING_RANGE later
                if (myTimeCacheInfo.myCacheMethod !=
                    GEO_HAPI_TIME_CACHING_CONTINUOUS)
                    myGeos.clear();

                fpreal32 t = cacheInfo.myStartTime;
                exint lastCookedIndex;

                // Cook the first time sample
                CHECK_RETURN(addNewTime(t, lastCookedIndex));
                loadedNewTime |= SYSisEqual(t, time);
                t = cacheInfo.myStartTime + cacheInfo.myInterval;

                // Cook the remaining time samples
                exint i = 1;
                HAPI_GeoInfo geo;
                while (SYSisLessOrEqual(t, cacheInfo.myEndTime))
                {
                    loadedNewTime |= SYSisEqual(t, time);

                    if (findTimeSample(myGeos, t) < 0)
                    {
                        // The last cooked time sample was a previous time
                        // sample in the range
                        // Cook this time sample and check for changes

                        exint timeIndex = addTimeSample(myGeos, t);

                        CHECK_RETURN(cookAtTime(session, myAssetId, t));

                        if (HAPI_RESULT_SUCCESS ==
                            HAPI_GetDisplayGeoInfo(&session, myAssetId, &geo))
                        {
                            // Check if the last time sample can be reused
                            if (geo.hasGeoChanged)
                            {
                                myGeos(timeIndex).second.reset(
                                    new GEO_HAPIGeo);
                                CHECK_RETURN(
                                    myGeos(timeIndex).second->loadGeoData(
                                        session, geo, buf));
                            }
                            else
                            {
                                myGeos(timeIndex).second =
                                    myGeos(lastCookedIndex).second;
                            }
                        }
                        else
                        {
                            TF_WARN("Unable to find geometry in asset: %s",
                                    myAssetPath.buffer());
                        }

                        // Check if the geo failed to add
                        if (!myGeos(timeIndex).second)
                            return false;

                        lastCookedIndex = timeIndex;
                    }

                    i++;
                    t = cacheInfo.myStartTime + (i * cacheInfo.myInterval);
                }
            }
        }
        else
        {
            TF_WARN("Invalid time caching settings.");
        }

        // Warn the user if the currently requested time is not within the
        // specified range
        if (!loadedNewTime)
        {
            UT_ASSERT(findTimeSample(myGeos, time) < 0);
            
            TF_WARN("Requested time sample is not within the specified time "
                    "cache range and interval");

            // Set this so the cache is not cleared. If we got this far, the
            // geometry and HDA are valid and their data can be reused.
            myReadSuccess = true;

            return false;
        }
    }
    else
    {
        UT_ASSERT(false && "Unexpected Time Cache Method");
    }

    myTimeCacheInfo = cacheInfo;
    myReadSuccess = true;
    return true;
}

bool
GEO_HAPIReader::checkReusable(const std::string &filePath,
                              const std::string &assetName)
{
    exint modTime = UT_FileUtil::getFileModTime(filePath.c_str());
    bool namesMatch = (myAssetName == assetName) ||
                      (myUsingDefaultAssetName && assetName.empty());
    return (namesMatch && (myAssetPath == filePath) && (myModTime == modTime));
}
