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
 *      Side Effects Software Inc
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 * NAME:    FS_ArResolver.h (USD Plugin, C++)
 *
 * COMMENTS:
 *     This plugin grant USD the power to use Houdini file protocol.
 *   The input path will be expanded then passed into FS_Reader. If
 *   an index file is detected, the stream of the called section will
 *   be fetched as a disk path in tmp folder. Otherwise this resolver
 *   will return the file path directly.
 *     The native resolver from Pixar uses PXR_AR_DEFAULT_SEARCH_PATH
 *   to search files. This feature is inherited to this plugin, but
 *   only works when FS_Reader return an invalid path.
 *
 */

#ifndef __FS_ARRESOLVER_H__
#define __FS_ARRESOLVER_H__

#include <UT/UT_SharedPtr.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_Array.h>
#include <UT/UT_String.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Lock.h>
#include <UT/UT_ConcurrentHashMap.h>
#include <UT/UT_ThreadSpecificValue.h>

#include <pxr/pxr.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/base/vt/value.h>

#include <string>
#include <vector>
#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

class FS_ArResolver : public ArResolver
{
public:
			 FS_ArResolver();
                        ~FS_ArResolver() override;

    // FS_ArResolver exclusive public

    // Return true if the path needs to be handled by Houdini.
    // If not, we'll pass it off to our fallback resolver.
    bool		 IsHoudiniPath(const std::string& path);

    // Expanding the given path by Houdini local config.
    // Since paths from Houdini GUI are supposed to be expanded before
    // this plugin start. Then this method only used to expand embedded
    // paths in USD file.
    // e.g. $JOB/my.usd -> /home/usr/show/my.usd.
    std::string		 ExpandPath(const std::string& path);

    // Compute the disk path from the input path. If the fetching is needed,
    // this function will return the expected path of temp file.
    std::string		 ComputeDiskPath(const std::string& path);

    // ArResolver overrides
    void                 ConfigureResolverForAsset(
				const std::string& path) override;
    std::string          AnchorRelativePath(const std::string& anchorPath, 
				const std::string& path) override;
    bool                 IsRelativePath(const std::string& path) override;
    bool                 IsRepositoryPath(const std::string& path) override;
    bool                 IsSearchPath(const std::string& path) override;
    std::string          GetExtension(const std::string& path) override;
    std::string          ComputeNormalizedPath(
				const std::string& path) override;
    std::string          ComputeRepositoryPath(
				const std::string& path) override;
    std::string          ComputeLocalPath(const std::string& path) override;
    std::string          Resolve(const std::string& path) override;
    std::string          ResolveWithAssetInfo(const std::string& path,
				ArAssetInfo* assetInfo) override;
    void                 UpdateAssetInfo(const std::string& identifier,
				const std::string& filePath,
				const std::string& fileVersion,
				ArAssetInfo* assetInfo) override;
    VtValue              GetModificationTimestamp(const std::string& path,
				const std::string& resolvedPath) override;
    bool                 FetchToLocalResolvedPath(const std::string& path,
				const std::string& resolvedPath) override;
    std::shared_ptr<ArAsset>         OpenAsset(
				const std::string &resolvedPath) override;
    bool                 CreatePathForLayer(const std::string& path) override;
    bool                 CanWriteLayerToPath(const std::string& path,
				std::string* whyNot) override;
    bool                 CanCreateNewLayerWithIdentifier(
				const std::string& identifier,
				std::string* whyNot) override;
    ArResolverContext    CreateDefaultContext() override;
    ArResolverContext    CreateDefaultContextForAsset(
				const std::string& filePath) override;
    ArResolverContext    GetCurrentContext() override;
    void                 RefreshContext(
				const ArResolverContext& context) override;

    void                 BeginCacheScope(VtValue* cacheScopeData) override;
    void                 EndCacheScope(VtValue* cacheScopeData) override;
    void                 BindContext(const ArResolverContext& context,
				VtValue* bindingData) override;
    void                 UnbindContext(const ArResolverContext& context,
				VtValue* bindingData) override;

private:
    // Do the actual conversion of Houdini paths to real paths on disk.
    void		 _EvalHoudiniNoCache(const UT_String&,
				UT_String& realPath);
    void		 _EvalHoudini(const UT_String& source,
				UT_String& realPath);

    // Types for the scoped identifier-to-resolvedPath map cache
    typedef UT_ConcurrentHashMap<UT_StringHolder, UT_StringHolder> PathMap;
    struct SharedPathMaps
    {
	PathMap			 myIdToExpandMap;
	PathMap			 myExpandToDiskMap;
    };
    typedef UT_SharedPtr<SharedPathMaps> SharedPathMapsPtr;
    struct CacheScopeData
    {
				 CacheScopeData()
				     : myPathMapsPtr(
					   UTmakeShared<SharedPathMaps>())
				 { }
	bool			 operator==(const CacheScopeData &other) const
				 {
				     return
					 myPathMapsPtr == other.myPathMapsPtr &&
					 myFallbackData == other.myFallbackData;
				 }

	SharedPathMapsPtr	 myPathMapsPtr;
	VtValue			 myFallbackData;
    };
    typedef UT_Array<CacheScopeData>			 CacheScopeDataArray;
    typedef UT_ThreadSpecificValue<CacheScopeDataArray>	 TLSCacheScopeDataArray;

    // Types for thread-safe fetching
    struct FetchItem : public UT_IntrusiveRefCounter<FetchItem>
    {
	FetchItem(UT_String ide, UT_String path) : 
	    myIdentifier(ide), myFetchPath(path),
	    myHasFetched(false), myFetchedSuccessfully(false) {} 

	UT_Lock		 myLock;
	UT_StringHolder	 myIdentifier;
	UT_StringHolder	 myFetchPath;
	bool		 myHasFetched;
	bool		 myFetchedSuccessfully;
    };
    typedef UT_IntrusivePtr<FetchItem> FetchPtr;
    typedef UT_ConcurrentHashMap<UT_StringHolder, FetchPtr> FetchMap;

    // Private members
    TLSCacheScopeDataArray	 myTLSCacheScopeDataArray;
    FetchMap			 myFetchMap;
    std::vector<std::string>	 mySearchPath;
    std::unique_ptr<ArResolver>	 myFallbackResolver;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __FS_ARRESOLVER_H__
