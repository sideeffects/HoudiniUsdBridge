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
 *      Calvin Gu
 *  Side Effects Software Inc
 *  123 Front Street West, Suite 1401
 *  Toronto, Ontario
 *  Canada   M5J 2M2
 *  416-504-9876
 *
 * NAME:    FS_ArResolver.C (USD Plugin, C++)
 *
 * COMMENTS:
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
#include <UT/UT_IStream.h>
#include <UT/UT_NTStreamUtil.h>
#include <UT/UT_OFStream.h>
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

#ifdef DEBUG_AR_RESOLVER
#define DEBUG_PRINT(...) \
    if (HoudiniGetenv("HOUDINI_DEBUG_RESOLVER")) { \
	UT_WorkBuffer buf; \
	buf.print(__VA_ARGS__); \
	UTdbgout(__FILE__, __LINE__, buf.buffer()); \
    }
#else
#define DEBUG_PRINT(...)
#endif

AR_DEFINE_RESOLVER(FS_ArResolver, ArResolver);

static bool
IsFileRelative(const std::string& path)
{
    return path.find("./") == 0 || path.find("../") == 0;
}

static std::string
JoinRelativePath(const std::string& anchorPath, const std::string& path)
{
    std::string resolvedPath = path;
    if(!anchorPath.empty())
    {
	// XXX - CLEANUP:
	// It's tempting to use AnchorRelativePath to combine the two
	// paths here, but that function's file-relative anchoring
	// causes consumers to break.
	//
	// Ultimately what we should do is specify whether anchorPath
	// in both Resolve and AnchorRelativePath can be files or directories
	// and fix up all the callers to accommodate this.
	resolvedPath = TfStringCatPaths(anchorPath, path);
    }
    // Since we delayed the fetching until FetchToLocalResolvedPath, the
    // resolvedPath might just be an expected path for fetching and not
    // a real file path. So we removed the TfPathExists check here.
    return resolvedPath;
}

static bool
IsSopReference(const char *path)
{
    // Before calling this function, make sure that the path string starts
    // with the OPREF_PREFIX string.
    UT_ASSERT(strstr(path, OPREF_PREFIX) == path);

    path += OPREF_PREFIX_LEN;

    const char *args = strstr(path, ":SDF_FORMAT_ARGS:");
    int pathlen = args ? int(intptr_t(args - path)) : strlen(path);

    return (pathlen > 4 && strncmp(path+pathlen-4, ".sop", 4) == 0);
}

// ============================================================================

FS_ArResolver::FS_ArResolver()
{
    // Initialize search paths by reading global environment.
    mySearchPath.push_back(ArchGetCwd());

    const std::string envPath = TfGetenv("PXR_AR_DEFAULT_SEARCH_PATH");

    if(!envPath.empty())
    {
	for(const auto& p : TfStringTokenize(envPath, ARCH_PATH_LIST_SEP))
	{
	    mySearchPath.push_back(TfAbsPath(p));
	}
    }

    std::vector<TfType> resolvers = ArGetAvailableResolvers();

#ifdef DEBUG_AR_RESOLVER
    DEBUG_PRINT("Possible fallback resolvers:");
    for (auto &&resolver : resolvers)
    {
	DEBUG_PRINT("        ", resolver.GetTypeName().c_str());
    }
#endif
    if (!resolvers.empty())
    {
	myFallbackResolver = ArCreateResolver(*resolvers.begin());
	if (myFallbackResolver)
	{
	    DEBUG_PRINT("Created fallback resolver: ",
		resolvers.begin()->GetTypeName().c_str());
	}
	else
	{
	    DEBUG_PRINT("No fallback resolver created.");
	}
    }
}

FS_ArResolver::~FS_ArResolver()
{
    // Clear fetched temp files.
    for(FetchMap::iterator i=myFetchMap.begin(); i!=myFetchMap.end(); ++i)
    {
	if(i->second->myHasFetched && i->second->myFetchedSuccessfully)
	{
	    UT_AutoLock lock(i->second->myLock);
	    UT_FileUtil::removeFile(i->second->myFetchPath.c_str());
	}
    }
}

void
FS_ArResolver::_EvalHoudiniNoCache(const UT_String& source, UT_String& realPath)
{
    // If FS can handle the identifier, do it here
    if(IsHoudiniPath(source.toStdString()))
    {
	if (source.startsWith(OPREF_PREFIX))
	{
            // We will sometimes get asset paths with arguments still attached
            // to a layer path name. This happens when we reference from a SOP
            // with volumes. The volume field "file paths" will be the full
            // SOP layer path, including the arguments (which we need during
            // the save process to pull the right GU_Detail out of the
            // XUSD_LockedGeoRegistry). In this case we don't want to resolve
            // the path at all. Just return an empty string. The unresolved
            // asset path is more informative than the path resolved to the
            // related .sop file on disk.
            if (!source.fcontain(":SDF_FORMAT_ARGS:") && IsSopReference(source))
            {
                const char *ext = source.fileExtension();
                UT_String safeext;

                if (ext)
                {
                    safeext = ".";
                    safeext.append(UT_VarEncode::encodeVar(ext+1));
                }
                realPath.sprintf("%s/usdtemp-%d-%d%s", UTgetTmpDir(),
                    getpid(), source.hash(), safeext.c_str());

                FetchMap::accessor accessor;
                if(!myFetchMap.find(accessor, realPath))
                {
                    myFetchMap.insert(accessor, realPath);
                    accessor->second = new FetchItem(source, realPath);
                }
            }
            else
                realPath = UT_String::getEmptyString();
	}
        else if (source.startsWith(UT_HDA_DEFINITION_PREFIX) ||
                 source.startsWith(UT_OTL_LIBRARY_PREFIX) ||
                 source.startsWith(UT_OP_DATA_BLOCK_PREFIX))
	{
            const char *ext = source.fileExtension();
            UT_String safeext;
            bool dofetch = false;
	    bool isshader = false;

            // Mark the identifier need to fetch later by adding an
            // FetchItem, then immediately fetch the item. opdef or oplib
            // files are likely to be texture maps or other non-layer assets
            // which will not get explicitly fetched by the USD library.
            if (source.endsWith("VexCode"))
            {
                safeext = ".vex";
		isshader = true;
            }
	    else if (source.endsWith("VflCode"))
            {
                safeext = ".vfl";
		isshader = true;
            }
            else if (ext)
            {
                safeext = ".";
                safeext.append(UT_VarEncode::encodeVar(ext+1));
            }
            realPath.sprintf("%s/usdtemp-%d-%d%s", UTgetTmpDir(),
                getpid(), source.hash(), safeext.c_str());

            // Create a block around the accessor object because the
            // accessor may hold a lock on the map.
            {
                FetchMap::accessor accessor;
                if(!myFetchMap.find(accessor, realPath))
                {
                    myFetchMap.insert(accessor, realPath);
                    accessor->second = new FetchItem(source, realPath);

		    // HDA sections that hold VEX shader code can be loaded
		    // directly (by VEX library), so no need to save temp file.
                    dofetch = !isshader;
                }
            }

            // Fetch the file to the resolved location if we just added it
            // to our map.
            if (dofetch)
                FetchToLocalResolvedPath(source.toStdString(),
                    realPath.toStdString());
	}
    }
    else
    {
	UT_ASSERT(!"We should only be resolving Houdini paths");
	realPath = source;
    }
}

void
FS_ArResolver::_EvalHoudini(const UT_String& source, UT_String& realPath)
{
    // Extract the smart pointer from the TLS pointer array
    CacheScopeDataArray& localCacheScopeDataArray =
	myTLSCacheScopeDataArray.get();

    // If we have a cache, we want to check there first. Otherwise calculate
    // the resolved path.
    if(!localCacheScopeDataArray.isEmpty())
    {
	CacheScopeData	 &localStackData = localCacheScopeDataArray.last();
	SharedPathMapsPtr pathmaps = localStackData.myPathMapsPtr;
	PathMap::accessor accessor;

        // Return the cached resolved path if we have it, otherwise
        // calculate it and add it to the cache.
	if(pathmaps->myExpandToDiskMap.find(accessor, source))
	{
	    realPath = accessor->second;
	}
	else
	{
	    _EvalHoudiniNoCache(source, realPath);
	    pathmaps->myExpandToDiskMap.insert(accessor, source);
	    accessor->second = realPath;
	}
    }
    else
	_EvalHoudiniNoCache(source, realPath);
}

bool
FS_ArResolver::IsHoudiniPath(const std::string& path)
{
    // Check for an "op:", "opdef:", or "oplib:" prefix.
    if (path.compare(0, OPREF_PREFIX_LEN, OPREF_PREFIX) == 0 ||
        path.compare(0, UT_HDA_DEFINITION_PREFIX_LEN,
            UT_HDA_DEFINITION_PREFIX) == 0 ||
        path.compare(0, UT_OTL_LIBRARY_PREFIX_LEN,
            UT_OTL_LIBRARY_PREFIX) == 0 ||
        path.compare(0, UT_OP_DATA_BLOCK_PREFIX_LEN,
            UT_OP_DATA_BLOCK_PREFIX) == 0)
    {
	DEBUG_PRINT("Is Houdini Path: ", path.c_str());
	return true;
    }

    // No section means it's just a regular path.
    DEBUG_PRINT("NOT Houdini Path: ", path.c_str());
    return false;
}

std::string
FS_ArResolver::ExpandPath(const std::string& path)
{
    if (!CH_Manager::getContextExists())
	return path;

    UT_String	 utPath(path);
    UT_String	 utExpandPath;

    // Expand the raw string every single time
    CHgetManager()->expandString(utPath.c_str(), utExpandPath, CHgetEvalTime());

    // Extract the smart pointer from the TLS pointer array
    CacheScopeDataArray& localCacheScopeDataArray =
	myTLSCacheScopeDataArray.get();

    // Access the pervious expanding strings from the detected pointer
    if(!localCacheScopeDataArray.isEmpty())
    {
	CacheScopeData	 &localStackData = localCacheScopeDataArray.last();
	SharedPathMapsPtr pathmaps = localStackData.myPathMapsPtr;
	PathMap::accessor accessor;

	// Overwrite the existing pair
	if(pathmaps->myIdToExpandMap.find(accessor, utPath))
	{
	    if(accessor->second != utExpandPath)
	    {
		accessor->second = utExpandPath;
		/*
		    TODO: Refresh the file cache of USD here
		*/
	    }
	}

	// Append new pair
	else
	{
	    pathmaps->myIdToExpandMap.insert(accessor, utPath);
	    accessor->second = utExpandPath;
	}
    }

    return utExpandPath.toStdString();
}

std::string
FS_ArResolver::ComputeDiskPath(const std::string& path)
{
    UT_String	 utPath(ExpandPath(path));
    UT_String	 utRealPath;

    _EvalHoudini(utPath, utRealPath);

    return utRealPath.toStdString();
}

void
FS_ArResolver::ConfigureResolverForAsset(const std::string& path)
{
    // We don't want to do anything here. Just pass the call along to the
    // fallback resolver.
    if (myFallbackResolver)
	myFallbackResolver->ConfigureResolverForAsset(path);
}

std::string
FS_ArResolver::AnchorRelativePath(const std::string& anchorPath,
    const std::string& path)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
	return path;

    return myFallbackResolver->AnchorRelativePath(anchorPath, path);
}

bool
FS_ArResolver::IsRelativePath(const std::string& path)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
	return false;

    return myFallbackResolver->IsRelativePath(path);
}

bool
FS_ArResolver::IsRepositoryPath(const std::string& path)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
	return false;

    return myFallbackResolver->IsRepositoryPath(path);
}

bool
FS_ArResolver::IsSearchPath(const std::string& path)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
	return false;

    return myFallbackResolver->IsSearchPath(path);
}

std::string
FS_ArResolver::GetExtension(const std::string& path)
{
    if (IsHoudiniPath(path))
    {
	if (path.compare(0, OPREF_PREFIX_LEN, OPREF_PREFIX) == 0)
	{
            if (IsSopReference(path.c_str()))
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
    }

    if (myFallbackResolver)
	return myFallbackResolver->GetExtension(path);

    return TfGetExtension(path);
}

std::string
FS_ArResolver::ComputeNormalizedPath(const std::string& path)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
	return ExpandPath(path);

    std::string	     exppath = ExpandPath(path);

    // Even if we have a fallback, we want to expand environment variables and
    // Houdini global variables, then pass this expanded path to the fallback.
    return myFallbackResolver->ComputeNormalizedPath(exppath);
}

std::string
FS_ArResolver::ComputeRepositoryPath(const std::string& path)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
	return std::string();

    return myFallbackResolver->ComputeRepositoryPath(path);
}

std::string
FS_ArResolver::ComputeLocalPath(const std::string& path)
{
    if(path.empty())
	return path;

    if (!myFallbackResolver || IsHoudiniPath(path))
	return ExpandPath(path);

    return myFallbackResolver->ComputeLocalPath(path);
}

std::string
FS_ArResolver::Resolve(const std::string& path)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
	return ResolveWithAssetInfo(path, /* assetInfo = */ nullptr);

    DEBUG_PRINT("Calling fallback Resolve method: ", path.c_str());

    return myFallbackResolver->Resolve(path);
}

ArResolverContext
FS_ArResolver::CreateDefaultContext()
{
    if (!myFallbackResolver)
	return ArResolverContext();

    return myFallbackResolver->CreateDefaultContext();
}

ArResolverContext
FS_ArResolver::CreateDefaultContextForAsset(const std::string& filePath)
{
    if (!myFallbackResolver)
	return ArResolverContext();

    return myFallbackResolver->CreateDefaultContextForAsset(filePath);
}

void
FS_ArResolver::RefreshContext(const ArResolverContext& context)
{
    if (myFallbackResolver)
	myFallbackResolver->RefreshContext(context);
}

ArResolverContext
FS_ArResolver::GetCurrentContext()
{
    if (!myFallbackResolver)
	return ArResolverContext();

    return myFallbackResolver->GetCurrentContext();
}

std::string
FS_ArResolver::ResolveWithAssetInfo(const std::string& path,
    ArAssetInfo* assetInfo)
{
    if(path.empty())
	return path;

    if (!myFallbackResolver || IsHoudiniPath(path))
    {
	std::string realPath = ComputeDiskPath(path);

	if(IsRelativePath(realPath))
	{
	    // First try to resolve relative paths against the current
	    // working directory.
	    std::string resolvedPath = JoinRelativePath(ArchGetCwd(), realPath);
	    if(!resolvedPath.empty())
		return resolvedPath;

	    // If that fails and the path is a search path, try to resolve
	    // against each directory in the specified search paths.
	    if(IsSearchPath(realPath))
	    {
		for(const auto& eachSearchPath : mySearchPath)
		{
		    resolvedPath = JoinRelativePath(eachSearchPath, realPath);
		    if(!resolvedPath.empty())
			return resolvedPath;
		}
	    }

	    return std::string();
	}

	return JoinRelativePath(std::string(), realPath);
    }

    DEBUG_PRINT("Calling fallback ResolveWithAssetInfo method: ", path.c_str());
    return myFallbackResolver->ResolveWithAssetInfo(path, assetInfo);
}

void
FS_ArResolver::UpdateAssetInfo(const std::string& identifier,
    const std::string& filePath, const std::string& fileVersion,
    ArAssetInfo* resolveInfo)
{
    if (!myFallbackResolver)
    {
	if(resolveInfo)
	{
	    if(!fileVersion.empty())
	    {
		resolveInfo->version = fileVersion;
	    }
	}
    }

    return myFallbackResolver->
	UpdateAssetInfo(identifier, filePath, fileVersion, resolveInfo);
}

VtValue
FS_ArResolver::GetModificationTimestamp(const std::string& path,
    const std::string& resolvedPath)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
    {
	double time;

	// The resolved path will be a file on disk.
	if(ArchGetModificationTime(resolvedPath.c_str(), &time))
	    return VtValue(time);

	return VtValue();
    }

    return myFallbackResolver->GetModificationTimestamp(path, resolvedPath);
}

bool
FS_ArResolver::FetchToLocalResolvedPath(const std::string& path,
    const std::string& resolvedPath)
{  
    FetchMap::accessor accessor;

    // Use the fallback resolver if we cannot find the FetchItem.
    if(!myFetchMap.find(accessor, UT_String(resolvedPath)))
    {
	if (myFallbackResolver)
	    return myFallbackResolver->
		FetchToLocalResolvedPath(path, resolvedPath);

	return true;
    }

    UT_AutoLock lock(accessor->second->myLock);

    // Skip the fetching if the temp file has been created.
    if(accessor->second->myHasFetched)
	return accessor->second->myFetchedSuccessfully;

    const UT_StringHolder &identifier = accessor->second->myIdentifier;

    if (identifier.startsWith(OPREF_PREFIX))
    {
        if (IsSopReference(path.c_str()))
        {
            UT_OFStream ostream(accessor->second->myFetchPath.c_str());
            ostream << identifier.c_str();
            accessor->second->myHasFetched = true;
            accessor->second->myFetchedSuccessfully = true;
            return true;
        }
    }
    else if (identifier.startsWith(UT_HDA_DEFINITION_PREFIX) ||
             identifier.startsWith(UT_OTL_LIBRARY_PREFIX) ||
             identifier.startsWith(UT_OP_DATA_BLOCK_PREFIX))
    {
	// Read the stream from the identifier. This is the original,
        // unmodified path. Copy the stream into the resolved location
        // on disk as a normal addressable file.
	FS_Reader reader(identifier.c_str());
	accessor->second->myHasFetched = true;

	if(reader.isGood())
	{
	    UT_OFStream ostream(accessor->second->myFetchPath.c_str());
	    UTcopyStreamToStream(*reader.getStream(), ostream);
	    accessor->second->myFetchedSuccessfully = true;
	    return true;
	}
    }

    // Throw the error and exit.
    TF_WARN("Cannot fetch stream from '%s' to '%s'.\n",
	path.c_str(), resolvedPath.c_str());

    return false;
}

std::shared_ptr<ArAsset>
FS_ArResolver::OpenAsset(const std::string &resolvedPath)
{
    if (!myFallbackResolver)
    {
	FILE* f = ArchOpenFile(resolvedPath.c_str(), "rb");
	if (!f) {
	    return nullptr;
	}

	return std::shared_ptr<ArAsset>(new ArFilesystemAsset(f));
    }

    return myFallbackResolver->OpenAsset(resolvedPath);
}

bool
FS_ArResolver::CreatePathForLayer(const std::string& path)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
        return false;

    return myFallbackResolver->CreatePathForLayer(path);
}

bool
FS_ArResolver::CanWriteLayerToPath(const std::string& path, std::string* whyNot)
{
    if (!myFallbackResolver || IsHoudiniPath(path))
        return false;

    return myFallbackResolver->CanWriteLayerToPath(path, whyNot);
}

bool
FS_ArResolver::CanCreateNewLayerWithIdentifier(const std::string& identifier,
    std::string* whyNot)
{
    return true;
}

void
FS_ArResolver::BeginCacheScope(VtValue* cacheScopeData)
{
    // cacheScopeData is held by ArResolverScopedCache instances
    // but is only populated by this function, so we know it must
    // be empty (when constructing a regular ArResolverScopedCache)
    // or holding on to a _CachePtr (when constructing an
    // ArResolverScopedCache that shares data with another one).

    TF_VERIFY(cacheScopeData && (cacheScopeData->IsEmpty() ||
	cacheScopeData->IsHolding<CacheScopeData>()));

    DEBUG_PRINT("Beginning Cache Scope");
    // Get local value from thread-local storage
    CacheScopeDataArray& localCacheScopeDataArray =
	myTLSCacheScopeDataArray.get();

    // Expand the pointer array with the data from cacheScopeData
    if(cacheScopeData->IsHolding<CacheScopeData>())
    {
	localCacheScopeDataArray.append(
	    cacheScopeData->UncheckedGet<CacheScopeData>());
    }
    else
    {
	if(localCacheScopeDataArray.isEmpty())
	{
	    // When the cache doesn't exist, create a new one
	    localCacheScopeDataArray.append(CacheScopeData());
	}
	else
	{
	    // Clone the latest pointer in the array
	    localCacheScopeDataArray.append(localCacheScopeDataArray.last());
	}
    }

    myFallbackResolver->BeginCacheScope(
	&localCacheScopeDataArray.last().myFallbackData);

    // Store the data from our stack in cacheScopeData.
    *cacheScopeData = localCacheScopeDataArray.last();
}

void
FS_ArResolver::EndCacheScope(VtValue* cacheScopeData)
{
    TF_VERIFY(cacheScopeData && cacheScopeData->IsHolding<CacheScopeData>());

    DEBUG_PRINT("Ending Cache Scope");
    // Get local value from thread-local storage
    CacheScopeDataArray& localCacheScopeDataArray =
	myTLSCacheScopeDataArray.get();

    // Simply pop the last pointer in the array
    if(!localCacheScopeDataArray.isEmpty())
    {
	myFallbackResolver->EndCacheScope(
	    &localCacheScopeDataArray.last().myFallbackData);
	localCacheScopeDataArray.removeLast();
    }
}

void
FS_ArResolver::BindContext(const ArResolverContext& context,
    VtValue* bindingData)
{
    DEBUG_PRINT("Binding Context");
    myFallbackResolver->BindContext(context, bindingData);
}

void
FS_ArResolver::UnbindContext(const ArResolverContext& context,
    VtValue* bindingData)
{
    DEBUG_PRINT("Unbinding Context");
    myFallbackResolver->UnbindContext(context, bindingData);
}

PXR_NAMESPACE_CLOSE_SCOPE
