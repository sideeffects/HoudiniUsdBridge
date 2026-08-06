//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#pragma once

#include <pxr/imaging/hd/filteringSceneIndex.h>
#include "pxr/imaging/hd/collectionExpressionEvaluator.h"
#include <optional>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(HD_PortalLightSceneIndex);

class HD_PortalLightSceneIndex :
    public HdSingleInputFilteringSceneIndexBase
{
public:
    static HD_PortalLightSceneIndexRefPtr New(
            const HdSceneIndexBaseRefPtr& inputSceneIndex);

    //HD_PortalLightSceneIndex::~HD_PortalLightSceneIndex() = default;

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    HD_PortalLightSceneIndex(
            const HdSceneIndexBaseRefPtr& inputSceneIndex);

    void _PrimsAdded(
            const HdSceneIndexBase &sender,
            const HdSceneIndexObserver::AddedPrimEntries &entries) override;
    void _PrimsRemoved(
            const HdSceneIndexBase &sender,
            const HdSceneIndexObserver::RemovedPrimEntries &entries) override;
    void _PrimsDirtied(
            const HdSceneIndexBase &sender,
            const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;

private:
    SdfPathVector _AddMappingsForDome(const SdfPath& domePrimPath);
    SdfPathVector _RemoveMappingsForDome(const SdfPath& domePrimPath);

    bool isDomelight(const HdSceneIndexPrim &prim, const SdfPath &primPath) const;
    bool isDomelight(const SdfPath &primPath, const TfToken &primType) const;

private:
    // Map dome light paths to flag indicating presence of associated portals.
    std::unordered_map<SdfPath, bool, SdfPath::Hash> _domesWithPortals;

    // Map of portal mesh proxies
    std::unordered_map<SdfPath, bool, SdfPath::Hash> _portalMesh;

    // Map portal path to dome path. A previous name for this map was
    // "_portalToDome", but that conflicts with a material param name.
    std::unordered_map<SdfPath, SdfPath, SdfPath::Hash> _portalsToDomes;
};

PXR_NAMESPACE_CLOSE_SCOPE
