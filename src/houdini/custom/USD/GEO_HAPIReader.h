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
#include <HAPI/HAPI.h>
#include <UT/UT_Array.h>

typedef UT_Array<GEO_HAPIGeo> GEO_HAPIGeoArray;
typedef std::map<std::string, std::string> GEO_HAPIParameterMap;

/// \class GEO_HAPIReader
///
/// Class to read data from a HAPI session.
/// Stores geometry data and attributes.
///
class GEO_HAPIReader
{
public:
    GEO_HAPIReader();
    ~GEO_HAPIReader();

    // Starts a Houdini Engine Process
    // Loads data from the asset pointed to by filePath
    bool readHAPI(const std::string &filePath,
                  const GEO_HAPIParameterMap &parmMap);

    bool checkReusable(const std::string &filePath,
                       const GEO_HAPIParameterMap &parmMap);

    // Accessors
    bool hasPrim() const { return myHasPrim; }
    GEO_HAPIGeoArray &getGeos() { return myGeos; }

private:

    bool myHasPrim;
    UT_String myAssetPath;
    exint myModTime;
    GEO_HAPIParameterMap myParms;

    // TODO: Save Geos based on cook frame
    GEO_HAPIGeoArray myGeos;
};

#endif // __GEO_HAPI_READER_H__