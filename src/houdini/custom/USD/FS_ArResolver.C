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
 *  Side Effects Software Inc
 *  123 Front Street West, Suite 1401
 *  Toronto, Ontario
 *  Canada   M5J 2M2
 *  416-504-9876
 *
 * NAME:    FS_ArResolver.C (USD Plugin, C++)
 *
 */

#include "FS_ArResolver.h"

#include <OP/OP_Node.h>
#include <CH/CH_Manager.h>
#include <FS/FS_Reader.h>
#include <UT/UT_Assert.h>
#include <UT/UT_Access.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Defines.h>
#include <UT/UT_FileUtil.h>
#include <UT/UT_String.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_VarEncode.h>
#include <tools/henv.h>

#include <pxr/usd/ar/defineResolver.h>

#include "pxr/usd/ar/filesystemAsset.h"
#include <pxr/usd/ar/assetInfo.h>
#include <pxr/usd/ar/resolverContext.h>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/arch/systemInfo.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/value.h>

PXR_NAMESPACE_OPEN_SCOPE

AR_DEFINE_RESOLVER(FS_ArResolver, ArResolver);

namespace {
    const char *theFormatArgsSeparator = ":SDF_FORMAT_ARGS:";

    class MemoryAsset : public ArAsset
    {
    public:
        explicit MemoryAsset(std::shared_ptr<const char> data, size_t dataSize)
            : myData(data)
            , myDataSize(dataSize)
        {
        }

        size_t GetSize() const override
        { return myDataSize; }

        std::shared_ptr<const char> GetBuffer() const override
        { return myData; }

        size_t Read(void* buffer, size_t count, size_t offset) const override
        {
            if (ARCH_UNLIKELY(offset + count > myDataSize)) {
                return 0;
            }
            memcpy(buffer, myData.get() + offset, count);
            return count;
        }
        
        std::pair<FILE*, size_t> GetFileUnsafe() const override
        { return std::pair<FILE*, size_t>(nullptr, 0); }

    private:
        std::shared_ptr<const char>  myData;
        size_t                       myDataSize;
    };

    // Simple class to represent a URI.
    class _URI
    {
    public:
        _URI(const std::string& uri)
        {
            const size_t index = uri.find(":");
            if (index == std::string::npos) {
                _path.push_back(uri);
            }
            else {
                _scheme = uri.substr(0, index);

                std::vector<std::string> splitargs =
                    TfStringSplit(uri.substr(index+1), theFormatArgsSeparator);

                if (!splitargs.empty())
                {
                    std::vector<std::string> path =
                        TfStringSplit(splitargs[0], "/");

                    if (!path.empty())
                    {
                        _assetName = std::move(path.front());
                        _path.resize(path.size() - 1);
                        std::move(path.begin() + 1, path.end(), _path.begin());
                    }
                }

                _hasArgs = (splitargs.size() > 1);
                if (_hasArgs)
                    _args = splitargs[1];
            }
        }

        _URI(const ArResolvedPath& resolvedPath)
            : _URI(resolvedPath.GetPathString())
        { }

        const std::string& GetScheme() const
        { return _scheme; }
        const std::string& GetAssetName() const
        { return _assetName; }

        std::string GetNormalized() const
        {
            if (_scheme.empty()) {
                return TfNormPath(_path.back());
            }

            if (_hasArgs)
                return _scheme + ":" +
                    TfNormPath(_assetName + "/" + TfStringJoin(_path, "/")) +
                    theFormatArgsSeparator + _args;
            else
                return _scheme + ":" +
                    TfNormPath(_assetName + "/" + TfStringJoin(_path, "/"));
        }

        _URI& Anchor(const std::string& relativePath)
        {
            std::vector<std::string> relativeParts =
                TfStringSplit(relativePath, "/");

            if (!_path.empty()) {
                _path.pop_back();
            }

            _path.insert(_path.end(),
                relativeParts.begin(),
                relativeParts.end());

            return *this;
        }

    private:
        std::string _scheme;
        std::string _assetName;
        std::string _args;
        std::vector<std::string> _path;
        bool _hasArgs;
    };

    bool
    _IsSopReference(const char *path)
    {
        // Before calling this function, make sure that the path string starts
        // with the OPREF_PREFIX string.
        UT_ASSERT(strstr(path, OPREF_PREFIX) == path);

        path += OPREF_PREFIX_LEN;

        const char *args = strstr(path, theFormatArgsSeparator);
        int pathlen = args ? int(intptr_t(args - path)) : strlen(path);

        return (pathlen > 4 && strncmp(path+pathlen-4, ".sop", 4) == 0);
    }
};

// ============================================================================

FS_ArResolver::FS_ArResolver()
{
}

FS_ArResolver::~FS_ArResolver()
{
}

std::string
FS_ArResolver::_CreateIdentifier(
    const std::string& assetPath,
    const ArResolvedPath& anchorAssetPath) const
{
    // Ar will call this function if either assetPath or anchorAssetPath
    // have a URI scheme that is associated with this resolver.

    // If assetPath has a URI scheme it must be an absolute URI so we
    // just return the normalized URI as the asset's identifier.
    const _URI assetURI(assetPath);
    if (!assetURI.GetScheme().empty()) {
        return assetURI.GetNormalized();
    }

    // Otherwise anchor assetPath to anchorAssetPath and return the
    // normalized URI.
    return _URI(anchorAssetPath).Anchor(assetPath).GetNormalized();
}

ArResolvedPath
FS_ArResolver::_Resolve(
    const std::string& assetPath) const
{
    return ArResolvedPath(_CreateIdentifier(assetPath, ArResolvedPath()));
}

std::string
FS_ArResolver::_GetExtension(const std::string& path) const
{
    if (path.compare(0, OPREF_PREFIX_LEN, OPREF_PREFIX) == 0)
    {
        if (_IsSopReference(path.c_str()))
            return "sop";

        return "";
    }
    else if (path.compare(0, UT_HDA_DEFINITION_PREFIX_LEN,
                UT_HDA_DEFINITION_PREFIX) == 0 ||
             path.compare(0, UT_OTL_LIBRARY_PREFIX_LEN,
                UT_OTL_LIBRARY_PREFIX) == 0 ||
             path.compare(0, UT_OP_DATA_BLOCK_PREFIX_LEN,
                UT_OP_DATA_BLOCK_PREFIX) == 0)
    {
        UT_String pathstr(path);
        const char *ext = nullptr;

        // opdef paths that end with "VexCode" are really .vex files.
        if (pathstr.endsWith("VexCode"))
            ext = ".vex";
        else if (pathstr.endsWith("VflCode"))
            ext = ".vfl";
        else
            ext = pathstr.fileExtension();

        if (UTisstring(ext) && *ext == '.')
            ext++;

        return UT_String(ext).toStdString();
    }

    // Fall back to the default implementation.
    return ArResolver::_GetExtension(path);
}

std::shared_ptr<ArAsset>
FS_ArResolver::_OpenAsset(
    const ArResolvedPath &resolvedPath) const
{
    const std::string &resolvedPathStr = resolvedPath.GetPathString();

    if (resolvedPathStr.compare(0, OPREF_PREFIX_LEN, OPREF_PREFIX) == 0)
    {
        std::shared_ptr<const char> data;

        return std::make_shared<MemoryAsset>(data, 0);
    }
    else if (resolvedPathStr.compare(0, UT_HDA_DEFINITION_PREFIX_LEN,
                UT_HDA_DEFINITION_PREFIX) == 0 ||
             resolvedPathStr.compare(0, UT_OTL_LIBRARY_PREFIX_LEN,
                UT_OTL_LIBRARY_PREFIX) == 0 ||
             resolvedPathStr.compare(0, UT_OP_DATA_BLOCK_PREFIX_LEN,
                UT_OP_DATA_BLOCK_PREFIX) == 0)
    {
        FS_Reader reader(resolvedPathStr.c_str());

        if (reader.isGood())
        {
            size_t dataSize = reader.getLength();
            char *buf = new char[dataSize];
            reader.getStream()->bread(buf, dataSize);
            std::shared_ptr<const char> data(buf);

            return std::make_shared<MemoryAsset>(data, dataSize);
        }
    }

    return std::shared_ptr<ArAsset>();
}

PXR_NAMESPACE_CLOSE_SCOPE
