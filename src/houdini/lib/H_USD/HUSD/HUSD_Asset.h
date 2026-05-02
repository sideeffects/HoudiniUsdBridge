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
 * NAME:	HUSD_Asset.h (HUSD Library, C++)
 *
 * COMMENTS:
 *    Wrapper around the ArResolver and ArAsset classes.
 *    Assets use the form path/to/zip[filename.ext]
 */
#ifndef HUSD_Asset_h
#define HUSD_Asset_h

#include "HUSD_API.h"
#include <UT/UT_UniquePtr.h>
#include <SYS/SYS_Types.h>
#include <utility>

class UT_StringRef;
class UT_IStream;
class husd_AssetPrivate;

class HUSD_API HUSD_Asset
{
public:
		HUSD_Asset(const UT_StringRef &asset_path);
	       ~HUSD_Asset();


    bool	isValid() const { return myValid; }

    // Return a new stream for this asset. 
    UT_IStream *newStream();

    // Size in bytes.
    size_t	size() const;

private:
    bool                         myValid;
    husd_AssetPrivate           *myData;
    std::shared_ptr<const char>  myStreamBuffer;
};

#endif
