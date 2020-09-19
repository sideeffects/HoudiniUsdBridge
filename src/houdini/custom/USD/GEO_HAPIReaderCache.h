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

#ifndef __GEO_HAPI_READER_CACHE_H__
#define __GEO_HAPI_READER_CACHE_H__

#include "GEO_HAPIReader.h"
#include <UT/UT_CappedCache.h>
#include <UT/UT_IntrusivePtr.h>

// Keys for searching for cached HAPIReaders
class GEO_HAPIReaderKey : public UT_CappedKey
{
public:
    GEO_HAPIReaderKey();

    // Useful constructor. Gets the mod time from filePath
    GEO_HAPIReaderKey(
            const UT_StringRef& filePath,
            const UT_StringRef& assetName);

    ~GEO_HAPIReaderKey() override = default;

    // Inherited functions
    UT_CappedKey* duplicate() const override;
    unsigned int getHash() const override;
    bool isEqual(const UT_CappedKey& key) const override;

    // identifying members
    UT_StringHolder myFilePath;
    UT_StringHolder myAssetName;
    exint myFileModTime;
};
using GEO_HAPIReaderKeyHandle = UT_IntrusivePtr<GEO_HAPIReaderKey>;

// Class to access the global Reader cache
class GEO_HAPIReaderCache
{
public:
    // Removes the matching HAPIReader from the cache and returns it so it can
    // be edited
    // Returns an empty handle if no matching Reader was found
    static GEO_HAPIReaderHandle pop(const GEO_HAPIReaderKey& key);

    // Add the Reader to the cache
    static void push(
            const GEO_HAPIReaderKey& key,
            const GEO_HAPIReaderHandle& reader);

    static void initExitCallback();
};

#endif // __GEO_HAPI_READER_CACHE_H__
