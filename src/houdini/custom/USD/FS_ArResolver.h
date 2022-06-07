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

protected:
    std::string _CreateIdentifier(
        const std::string& assetPath,
        const ArResolvedPath& anchorAssetPath) const override;
    // We don't support writing to any Houdini-specific paths.
    std::string _CreateIdentifierForNewAsset(
        const std::string& assetPath,
        const ArResolvedPath& anchorAssetPath) const override
    { return std::string(); }

    ArResolvedPath _Resolve(
        const std::string& assetPath) const override;
    // We don't support writing to any Houdini-specific paths.
    ArResolvedPath _ResolveForNewAsset(
        const std::string& assetPath) const override
    { return ArResolvedPath(); }

    std::string _GetExtension(
        const std::string& path) const override;

    std::shared_ptr<ArAsset> _OpenAsset(
        const ArResolvedPath &resolvedPath) const override;
    // We don't support writing to any Houdini-specific paths.
    bool _CanWriteAssetToPath(
        const ArResolvedPath& resolvedPath,
        std::string* whyNot) const override
    { return false; }
    std::shared_ptr<ArWritableAsset> _OpenAssetForWrite(
        const ArResolvedPath& resolvedPath,
        WriteMode writeMode) const override
    { return std::shared_ptr<ArWritableAsset>(); }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __FS_ARRESOLVER_H__
