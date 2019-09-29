/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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
    UT_IStream *newStream() const;

    // Size in bytes.
    size_t	size() const;

    // entire buffer of the asset.
    std::shared_ptr<const char> buffer() const;
    
private:
    bool	myValid;
    husd_AssetPrivate *myData;
};

#endif
