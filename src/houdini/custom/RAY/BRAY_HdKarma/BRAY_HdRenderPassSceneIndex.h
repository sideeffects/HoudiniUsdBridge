//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#pragma once

#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/collectionExpressionEvaluator.h>
#include <optional>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(BRAY_HdRenderPassSceneIndex);

class BRAY_HdRenderPassSceneIndex : 
    public HdSingleInputFilteringSceneIndexBase
{
public:
    static BRAY_HdRenderPassSceneIndexRefPtr New(
            const HdSceneIndexBaseRefPtr& inputSceneIndex);
    
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    BRAY_HdRenderPassSceneIndex(
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
    // State specified by a render pass.
    // If renderPassPath is the empty path, no render pass is active.
    // Collection evaluators are set sparsely, corresponding to
    // the presence of the collection in the render pass schema.
    struct _RenderPassState {
        SdfPath renderPassPath;

        // Retain the expressions so we can compare old vs. new state.
        SdfPathExpression matteExpr;
        SdfPathExpression renderVisExpr;
        SdfPathExpression cameraVisExpr;
        SdfPathExpression pruneExpr;

        // Evalulators for each pattern expression.
        std::optional<HdCollectionExpressionEvaluator> matteEval;
        std::optional<HdCollectionExpressionEvaluator> renderVisEval;
        std::optional<HdCollectionExpressionEvaluator> cameraVisEval;
        std::optional<HdCollectionExpressionEvaluator> pruneEval;

        bool DoesOverrideMatte(
                const SdfPath &primPath,
                HdSceneIndexPrim const& prim) const;
        bool DoesOverrideVis(
                const SdfPath &primPath,
                HdSceneIndexPrim const& prim) const;
        bool DoesOverrideCameraVis(
                const SdfPath &primPath,
                HdSceneIndexPrim const& prim) const;
        bool DoesPrune(
                const SdfPath &primPath) const;

        static VtBoolArray GetInstancerMask(
            HdSceneIndexPrim const& prim,
            std::optional<HdCollectionExpressionEvaluator> const& eval,
            bool invert = false);
    };

    // Pull on the scene globals schema for the active render pass,
    // computing and caching its state in _activeRenderPass.
    void _UpdateActiveRenderPassState(
            HdSceneIndexObserver::AddedPrimEntries *addedEntries,
            HdSceneIndexObserver::DirtiedPrimEntries *dirtyEntries,
            HdSceneIndexObserver::RemovedPrimEntries *removedEntries);

    // State for the active render pass.
    _RenderPassState _activeRenderPass;
};

PXR_NAMESPACE_CLOSE_SCOPE
