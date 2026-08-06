//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#pragma once

#include "pxr/imaging/hd/filteringSceneIndex.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(HD_CoordSysCameraDataSceneIndex);

class HD_CoordSysCameraDataSceneIndex : public HdSingleInputFilteringSceneIndexBase
{
public:
    static HD_CoordSysCameraDataSceneIndexRefPtr
    New(const HdSceneIndexBaseRefPtr &inputSceneIndex);

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    HD_CoordSysCameraDataSceneIndex(
        const HdSceneIndexBaseRefPtr &inputSceneIndex);

    void _PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries) override;

    void _PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries) override;

    void _PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
