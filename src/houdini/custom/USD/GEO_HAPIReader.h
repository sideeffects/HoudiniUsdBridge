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

#ifndef __GEO_HAPI_READER_H__
#define __GEO_HAPI_READER_H__

#include "GEO_HAPIGeo.h"
#include "GEO_HAPISessionManager.h"
#include <HAPI/HAPI.h>
#include <UT/UT_Array.h>
#include <UT/UT_CappedCache.h>
#include <UT/UT_IntrusivePtr.h>

typedef std::pair<fpreal32, GEO_HAPIGeoHandle> GEO_HAPITimeSample;
typedef std::map<std::string, std::string> GEO_HAPIParameterMap;

// Specifies how to cache different time samples
enum GEO_HAPITimeCaching
{
    // No caching
    GEO_HAPI_TIME_CACHING_NONE = 0,

    // Cache time samples as they are requested
    GEO_HAPI_TIME_CACHING_CONTINUOUS,

    // Immediately cache all time samples within a specified range and interval
    GEO_HAPI_TIME_CACHING_RANGE
};

struct GEO_HAPITimeCacheInfo
{
    GEO_HAPITimeCaching myCacheMethod = GEO_HAPI_TIME_CACHING_CONTINUOUS;
    fpreal32 myStartTime = 0.0f;
    fpreal32 myEndTime = 1.0f;
    fpreal32 myInterval = 1.0f / 24.0f;

    bool operator==(const GEO_HAPITimeCacheInfo &rhs);
    bool operator!=(const GEO_HAPITimeCacheInfo &rhs);
};

struct GEO_HAPIMetadataInfo
{
    GEO_HAPITimeCacheInfo timeCacheInfo;

    bool keepEngineOpen = false;
};

/// \class GEO_HAPIReader
///
/// Class to read data from a HAPI session.
/// Stores geometry data and attributes.
///
class GEO_HAPIReader : public UT_CappedItem
{
public:
    GEO_HAPIReader();
    ~GEO_HAPIReader() override;

    //
    // Creates a node in the shared HAPI_Session containing an asset. The asset
    // is specified by the library filePath points to. If assetName is passed,
    // the node will contain the asset with the matching name. Otherwise it will
    // contain the first asset in the library. Returns true iff the node was
    // successfully created
    //
    bool init(const std::string &filePath,
              const std::string &assetName = std::string());

    // Loads data from the asset specified by the last init() call
    bool readHAPI(
            const std::string &filePath,
            const GEO_HAPIParameterMap &parmMap,
            fpreal32 time = 0.f,
            const std::string &assetName = std::string(),
            const GEO_HAPIMetadataInfo &metaInfo = GEO_HAPIMetadataInfo());

    // Accessors
    bool hasPrim() const { return !myGeos.isEmpty(); }
    bool hasPrimAtTime(float time) const;
    GEO_HAPIGeoHandle getGeo(float time = 0.0f);

    int64 getMemoryUsage() const override;
    int64 getMemoryUsage(bool inclusive) const;

private:

    bool updateParms(const HAPI_Session &session,
                     const HAPI_NodeInfo &assetInfo,
                     UT_WorkBuffer &buf);

    bool loadGeometry(
            const std::string &filePath,
            const std::string &assetName,
            const GEO_HAPIParameterMap &parmMap,
            fpreal32 time,
            const GEO_HAPITimeCacheInfo &cacheInfo);

    void exitEngine();

    UT_StringHolder myAssetPath;

    GEO_HAPIParameterMap myParms;

    GEO_HAPISessionID mySessionId;
    HAPI_NodeId myAssetId;
    GEO_HAPISessionStatusHandle myOldSessionStatus;

    UT_Array<GEO_HAPITimeSample> myGeos;
    GEO_HAPITimeCacheInfo myTimeCacheInfo;
    bool myReadSuccess;

    bool myMaintainHAPISession;
};
using GEO_HAPIReaderHandle = UT_IntrusivePtr<GEO_HAPIReader>;

#endif // __GEO_HAPI_READER_H__