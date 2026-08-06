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
 * NAME:	UT_SprURI.h ( UT Library, C++)
 *
 * COMMENTS:	Simple URI class, to represent Solaris Project Resolver (spr) paths.
 *
 * Defines how spr paths are interpreted, anchored, and resolved to system filepaths
 */

#ifndef HPR_ASSETRESOLVER_H__
#define HPR_ASSETRESOLVER_H__

#include "pxr/pxr.h"

#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/assetInfo.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class HPRResolver
    : public ArResolver
{
public:
    HPRResolver() = default;

    ~HPRResolver() override = default;

protected:
    std::string _CreateIdentifier(
        const std::string& assetPath,
        const ArResolvedPath& anchorAssetPath) const override;

    std::string _CreateIdentifierForNewAsset(
        const std::string& assetPath,
        const ArResolvedPath& anchorAssetPath) const override;

    std::string _GetExtension(
        const std::string& path) const override;

    ArResolvedPath _Resolve(
        const std::string& assetPath) const override;

    ArResolvedPath _ResolveForNewAsset(
        const std::string& assetPath) const override;

    ArResolverContext _CreateDefaultContext() const override;

    ArResolverContext _CreateDefaultContextForAsset(
        const std::string& assetPath) const override;

    ArResolverContext _CreateContextFromString(
        const std::string& contextStr) const override;

    bool _IsContextDependentPath(
        const std::string& assetPath) const override;

    void _RefreshContext(
        const ArResolverContext& context) override;

    ArTimestamp _GetModificationTimestamp(
        const std::string& path,
        const ArResolvedPath& resolvedPath) const override;

    std::shared_ptr<ArAsset> _OpenAsset(
        const ArResolvedPath& resolvedPath) const override;

    std::shared_ptr<ArWritableAsset> _OpenAssetForWrite(
        const ArResolvedPath& resolvedPath,
        WriteMode writeMode) const override;

    ArAssetInfo _GetAssetInfo(
        const std::string& assetPath,
        const ArResolvedPath& resolvedPath) const override;

private:
    std::string _DoResolve(
        const std::string& assetPath,
        bool newasset=false) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif //HPR_ASSETRESOLVER_H__
