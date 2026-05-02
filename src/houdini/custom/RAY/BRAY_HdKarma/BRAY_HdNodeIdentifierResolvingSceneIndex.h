//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#pragma once

#include <pxr/pxr.h>
#include <pxr/imaging/hd/materialFilteringSceneIndexBase.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(BRAY_HdNodeIdentifierResolvingSceneIndex);

/// Scene index that converts the sourceAsset/sourceCode info into a nodeType
/// (nodeIdentifier).
class BRAY_HdNodeIdentifierResolvingSceneIndex
    : public HdMaterialFilteringSceneIndexBase
{
public:
    static
    BRAY_HdNodeIdentifierResolvingSceneIndexRefPtr
    New(HdSceneIndexBaseRefPtr const &inputSceneIndex);

    ~BRAY_HdNodeIdentifierResolvingSceneIndex() override = default;

protected: // HdMaterialFilteringSceneIndexBase overrides
    FilteringFnc _GetFilteringFunction() const override;

private:
    BRAY_HdNodeIdentifierResolvingSceneIndex(
        HdSceneIndexBaseRefPtr const &inputSceneIndex);
};

PXR_NAMESPACE_CLOSE_SCOPE
