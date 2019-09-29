/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *  Side Effects Software Inc
 *  123 Front Street West, Suite 1401
 *  Toronto, Ontario
 *  Canada   M5J 2M2
 *  416-504-9876
 *
 * NAME:    FS_UsdzReaderHelper.C
 *
 * COMMENTS:
 *
 */

#include "FS_UsdzReaderHelper.h"
#include <UT/UT_DSOVersion.h>
#include <UT/UT_FileUtil.h>
#include <UT/UT_StringView.h>
#include <UT/UT_Debug.h>
#include <UT/UT_IStream.h>

namespace
{
    static constexpr UT_StringLit   theUsdzPattern(".usdz[");

    // Valid usdz asset files are of the type
    // filename.usdz[assetname.type]
    // @param len is an optional parameter than can be used to
    //        store the length of the actual filename alone
    //        i.e filename.usdz without asset paths.
    static bool
    isValidUsdzAssetFile(const char* source, int* len = nullptr)
    {
	auto loc = SYSstrcasestr(source, theUsdzPattern.c_str());
	if (loc == nullptr)
	    return false;

	if (len)
	{
	    *len = loc - source + 5; // add length for .usdz also
	    UT_ASSERT(*len > 5);	 // atleast one letter in name
	}
	return true;
    }

    static UT_StringHolder
    extractAssetName(const char *name, int len)
    {
	UT_StringView	view(name+len);	// Asset wrapped in []
	UT_ASSERT(view.length() > 2);
	UT_ASSERT(view[0] == '[' && view[view.length()-1] == ']');
	view = view.substr(1, view.length()-2);
	return UT_StringHolder(view);
    }

    class fs_UsdAssetStream
	: public FS_ReaderStream
    {
    public:
	fs_UsdAssetStream(const char *source, int len)
	    : FS_ReaderStream()
	    , myAsset(source)
	{
	    UT_ASSERT(myAsset.isValid());
	    if (myAsset.isValid())
	    {
		UT_ASSERT(len > 2);
		myFile = extractAssetName(source, len);
		myModTime = 0;
		myDataSize = myAsset.size();
		myStream = UTmakeUnique<UT_IStream>(myAsset.buffer().get(),
			myAsset.size(), UT_ISTREAM_BINARY);
	    }
	}
	bool	isValid() const { return myAsset.isValid(); }
	virtual int64	getMemoryUsage(bool inclusive) const override final
	{
	    int64	mem = inclusive ? sizeof(this) : 0;
	    if (myAsset.isValid())
		mem += myAsset.size();
	    return mem;
	}
    protected:
	HUSD_Asset	myAsset;
    };
}

// We need to make sure that houdini is able to register
// the FS helpers when loading this dso.
UT_DSOVERSION_EXPORT
void
installFSHelpers()
{
    new FS_UsdzReaderHelper();
    new FS_UsdzInfoHelper();
}

FS_ReaderStream *
FS_UsdzReaderHelper::createStream(const char *source, const UT_Options *options)
{
    int		len;

    if (isValidUsdzAssetFile(source, &len))
    {
	auto is = new fs_UsdAssetStream(source, len);
	if (is->isValid())
	    return is;
	delete is;
    }
    return nullptr;
}

bool
FS_UsdzInfoHelper::canHandle(const char* source)
{
    return isValidUsdzAssetFile(source);
}

bool
FS_UsdzInfoHelper::hasAccess(const char* source, int mode)
{
    if(isValidUsdzAssetFile(source))
    {
	HUSD_Asset asset(source);
	if (asset.isValid())
	    return true;
    }
    return false;
}

bool
FS_UsdzInfoHelper::getIsDirectory(const char* source)
{
    // usdz files are not directories
    return false;
}

int
FS_UsdzInfoHelper::getModTime(const char* source)
{
    // Each usdz file, being a store of many files
    // the paths are of the type filename.usdz[assetname.type]
    // the actual file is filename.usdz. So we just
    // extract out the usdz part and check for modification times.
    int len = -1;
    if (isValidUsdzAssetFile(source, &len))
    {
	UT_StringHolder usdzfile(source, len);
	return UT_FileUtil::getFileModTime(usdzfile.buffer());
    }
    return 0;
}

int64
FS_UsdzInfoHelper::getSize(const char* source)
{
    // Each usdz file can contain many files inside it
    // we need to make sure that we return the correct
    // size.
    if(isValidUsdzAssetFile(source))
    {
	HUSD_Asset asset(source);
	if (asset.isValid())
	    return asset.size();
    }
    return 0;
}

bool
FS_UsdzInfoHelper::getContents(const char* source, UT_StringArray& contents,
    UT_StringArray* dirs)
{
    // usdz files are not directories
    return false;
}
