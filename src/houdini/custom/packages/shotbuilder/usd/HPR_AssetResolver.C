/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	20 Maud St.
 *	Toronto, Ontario,  M5V 2M5
 *	Canada
 *	416-504-9876
 *
 * NAME:	UT_HPRURI (C++)
 *
 * COMMENTS:	Class to handle parsing HPR URIs
 */

#include "HPR_AssetResolver.h"
#include "HPR_AssetResolverContext.h"
#include <FS/UT_HprUri.h>

#include "pxr/usd/ar/defineResolver.h"
#include "pxr/usd/ar/filesystemAsset.h"
#include "pxr/usd/ar/filesystemWritableAsset.h"
#include "pxr/base/tf/fileUtils.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include <UT/UT_Array.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_WorkBuffer.h>

PXR_NAMESPACE_OPEN_SCOPE

AR_DEFINE_RESOLVER(HPRResolver, ArResolver);


std::string
_DoCreateIdenifier(const std::string& assetPath,
                   const ArResolvedPath& anchorAssetPath)
{
    // 1. if assetPath is absolute,
    //       pass it along as is
    // 2. if assetPath is relative, but doesn't start with '.'
    //       pass to default asset resolver to be looked up in search paths
    // 3. if assetPath is relative, then anchorAssetPath must be hpr,
    //       return a hpr path, by appending assetPath to anchorAssetPath
    if (UTisAbsolutePath(assetPath.c_str()) ||
        !UTstringStartsWith(assetPath, "."))
    {
        // TODO: Check for existence of hpr path at this relative location,
        //       if it exists, use that, otherwise pass along assetPath as is.
        return assetPath;
    }

    // need to remove the /layer.ext?v=# portion of the anchor path.
    UT_String anchorPath(anchorAssetPath);
    UT_String dirPath, fileName;
    anchorPath.splitPath(dirPath, fileName);

    UT_StringHolder path(assetPath);
    UTmakeAbsoluteFilePath(path,dirPath);
    return path.toStdString();
}

std::string
HPRResolver::_CreateIdentifier(
        const std::string& assetPath,
        const ArResolvedPath& anchorAssetPath) const
{
    return _DoCreateIdenifier(assetPath, anchorAssetPath);
}

std::string
HPRResolver::_CreateIdentifierForNewAsset(
    const std::string& assetPath,
    const ArResolvedPath& anchorAssetPath) const
{
    return _DoCreateIdenifier(assetPath, anchorAssetPath);
}

std::string
HPRResolver::_GetExtension(
        const std::string& path) const
{
    UT_HprUri uri(path);
    return uri.extension().toStdString();
}

std::string
HPRResolver::_DoResolve(
    const std::string& assetPath,
    bool               newAsset) const
{
    UT_HprUri assetUri(assetPath);
    if (!assetUri.isValid())
        return ArResolvedPath(assetPath);

    if (newAsset)
    {
        // New asset, so we want the "next" version (unless specified in uri?)
        assetUri.setVersion("next");
        return assetUri.uri().toStdString();
    }

     if (!assetUri.isVersionSet())
    {
        // No version supplied, check the current context.
        if (const HPRResolverContext* ctx =
                _GetCurrentContextObject<HPRResolverContext>())
        {
            UT_String versionKey(assetUri.path());
            versionKey += "/";
            versionKey += assetUri.name();
            UTnormalizeFilePath(versionKey);
            std::string versionStr = ctx->GetVersion(versionKey);
            if (versionStr != UT_HprUri::getPlaceHolderVersion().c_str())
                assetUri.setVersion(versionStr.c_str());
        }
    }
    return assetUri.uri().toStdString();
}

ArResolvedPath
HPRResolver::_Resolve(
    const std::string& assetPath) const
{
    const std::string resolvedAssetPath = _DoResolve(assetPath, false);
    return ArResolvedPath(resolvedAssetPath);
}


ArResolvedPath
HPRResolver::_ResolveForNewAsset(
    const std::string& assetPath) const
{
    const std::string resolvedAssetPath = _DoResolve(assetPath, true);
    return ArResolvedPath(resolvedAssetPath);
}

ArResolverContext
HPRResolver::_CreateDefaultContext() const
{
    return ArResolverContext();
}

ArResolverContext
HPRResolver::_CreateDefaultContextForAsset(
    const std::string& assetPath) const
{
    return ArResolverContext();
}

ArResolverContext
HPRResolver::_CreateContextFromString(
    const std::string& contextStr) const
{
    return ArResolverContext(
        HPRResolverContext(contextStr)
    );
}

bool
HPRResolver::_IsContextDependentPath(
    const std::string& assetPath) const
{
    return true;
}

void
HPRResolver::_RefreshContext(
    const ArResolverContext& context)
{

}

ArTimestamp
HPRResolver::_GetModificationTimestamp(
    const std::string& path,
    const ArResolvedPath& resolvedPath) const
{
    return ArFilesystemAsset::GetModificationTimestamp(
        ArResolvedPath(_DoResolve(resolvedPath, false))
    );
}

std::shared_ptr<ArAsset>
HPRResolver::_OpenAsset(
    const ArResolvedPath& resolvedPath) const
{
    UT_HprUri uri(resolvedPath.GetPathString());
    // TODO: uri.getFilepath should probably return a bool, to say whether
    //       or not the filepath makes sense, since the uri could have bad
    //       version info....
    std::string filepath = uri.resolvedPath().toStdString();
    return ArFilesystemAsset::Open(ArResolvedPath(std::move(filepath)));
}

std::shared_ptr<ArWritableAsset>
HPRResolver::_OpenAssetForWrite(
    const ArResolvedPath& resolvedPath,
    WriteMode writeMode) const
{
    UT_HprUri uri(resolvedPath.GetPathString());
    std::string filepath = uri.resolvedPath(true).toStdString();
    return ArFilesystemWritableAsset::Create(ArResolvedPath(std::move(filepath)), writeMode);
}

ArAssetInfo
HPRResolver::_GetAssetInfo(
    const std::string& assetPath,
    const ArResolvedPath& resolvedPath) const
{
    // TODO:
    return ArAssetInfo();
}

PXR_NAMESPACE_CLOSE_SCOPE