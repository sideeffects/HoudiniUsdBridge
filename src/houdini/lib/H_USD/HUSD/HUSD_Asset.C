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
 *    Assets use the form: path/to/usdz[filename.ext]
 */
#include "HUSD_Asset.h"
#include <UT/UT_IStream.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>

class husd_AssetPrivate
{
public:
    std::shared_ptr<PXR_NS::ArAsset> myAsset;
};

HUSD_Asset::HUSD_Asset(const UT_StringRef &path)
    : myData(new husd_AssetPrivate),
      myValid(false)
{
    auto asset = PXR_NS::ArGetResolver().OpenAsset( path.toStdString() );
    if(asset)
    {
	myData->myAsset = asset;
	myValid =  true;
    }
}

HUSD_Asset::~HUSD_Asset()
{
    delete myData;
}

size_t
HUSD_Asset::size() const
{
    UT_ASSERT(myValid);
    return myValid ? myData->myAsset->GetSize() : 0;
}
	    

std::shared_ptr<const char> 
HUSD_Asset::buffer() const
{
    UT_ASSERT(myValid);
    return myValid ? myData->myAsset->GetBuffer()
		   : std::shared_ptr<const char>(nullptr);
}
	    
UT_IStream *
HUSD_Asset::newStream() const
{
    UT_ASSERT(myValid);
    if(myValid)
    {
	auto buffer = myData->myAsset->GetBuffer();
	return new UT_IStream((const char *)buffer.get(),
			      myData->myAsset->GetSize(),
			      UT_ISTREAM_BINARY);
    }
    return nullptr;
}
