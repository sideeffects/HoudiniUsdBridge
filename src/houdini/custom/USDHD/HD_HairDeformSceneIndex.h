/*
 * Copyright 2025 Side Effects Software Inc.
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
 */

#pragma once

#include "HD_HairDeformUtils.h"

#include <UT/UT_Array.h>
#include <UT/UT_ConcurrentHashMap.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_Map.h>
#include <UT/UT_Set.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringHolder.h>

#include <functional>
#include <type_traits>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/collectionExpressionEvaluator.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

class HairDeformSchema;

TF_DECLARE_REF_PTRS(HD_HairDeformSceneIndex);

class HD_HairDeformSceneIndex : public HdSingleInputFilteringSceneIndexBase
{
public:
    static HD_HairDeformSceneIndexRefPtr New(
            const HdSceneIndexBaseRefPtr &inputSceneIndex);

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    HD_HairDeformSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex);

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
    HdContainerDataSourceHandle _BuildOverlayDataSource(
            const SdfPath &prim_path,
            const HdSceneIndexPrim &prim) const;

    // Cache clearing helpers

    void _ClearDeformerCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR deformer {}", primpath.GetText());
        _deformercachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearSkinMeshCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR skinmesh {}",
                primpath.GetText());
        _skinmeshcachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearRestPointsCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR restpoints {}",
                primpath.GetText());
        _restpointscachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearSurfaceTopoCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR surfacetopo {}",
                primpath.GetText());
        _surfacetopocachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearCurveSkinCaptureCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR curveskincapture {}",
                primpath.GetText());
        _maincurveskincapturecachemap->erase(UT_StringHolder(primpath.GetText()));
        _deformercurveskincapturecachemap->erase(
                UT_StringHolder(primpath.GetText()));
    }

    void _ClearGuideInterpCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR guideinterp {}",
                primpath.GetText());
        _guideinterpcachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearGIMSurfaceTopoCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR gimsurfacetopo {}",
                primpath.GetText());
        _gimsurfacetopocachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearPointDeformCaptureCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR pointdeformcapture {}",
                primpath.GetText());
        _pointdeformcapturecachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearSkinSubdEvalCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR skinsubdeval {}",
                primpath.GetText());
        _skinsubdcachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearClumpTopoCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR clumptopo {}",
                primpath.GetText());
        _clumptopocachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearOrientAttribsCache(const SdfPath &primpath)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE CLEAR orientattribs {}",
                primpath.GetText());
        _orientattribscachemap->erase(UT_StringHolder(primpath.GetText()));
    }

    void _ClearPrimVarCache(const SdfPath &primpath);

    void _LogCacheState(const char *event)
    {
        auto &&printkeys = [&](const char *title, auto &cachemap)
        {
            UT_WorkBuffer buf(title);
            buf.appendFormat(" after {}", event);
            buf.append("\n");
            for (auto it = cachemap->begin(); it != cachemap->end(); it++)
            {
                buf.appendFormat("    {}", it->first);
                buf.append("\n");
            }

            UT_ErrorLog::warning(buf.c_str());
        };
        printkeys("Cached Deformers", _deformercachemap);
        printkeys("Cached Rest Points", _restpointscachemap);
        printkeys("Cached Primvars", _cachemap);
    }

    // Relationship map helpers

    using ForwardMapType = UT_Map<SdfPath, UT_Set<SdfPath>, SdfPath::Hash>;
    using ReverseMapType = UT_Map<SdfPath, SdfPath, SdfPath::Hash>;

    // True if primPath is tracked as a groom in any reverse map.
    bool _IsKnownGroom(const SdfPath &primPath) const
    {
        return _groomtoskinmap.count(primPath)
            || _groomtoguideinterpmeshmap.count(primPath)
            || _groomtopointdeformmap.count(primPath);
    }

    // Remove groomPath from the forward map entry for targetPath.
    // Returns true if the entry became empty and was erased.
    template <typename MapT>
    bool _RemoveFromForwardMap(
            MapT &forwardMap,
            const SdfPath &targetPath,
            const SdfPath &groomPath)
    {
        auto it = forwardMap.find(targetPath);
        if (it != forwardMap.end())
        {
            it->second.erase(groomPath);
            if (it->second.empty())
            {
                forwardMap.erase(it);
                return true;
            }
        }
        return false;
    }

    // Push a DirtiedPrimEntry for each dependant groom, optionally
    // calling perDependant on each one (e.g. to clear caches).
    void _DirtyDependants(
            const UT_Set<SdfPath> &dependants,
            HdSceneIndexObserver::DirtiedPrimEntries &entries,
            std::function<void(const SdfPath &)> perDependant = {});

    // Update a single groom→target relationship. Removes the old
    // mapping (calling onOldRemoved if provided) and inserts the new.
    void _UpdateRelationship(
            const SdfPath &groomPath,
            const VtArray<SdfPath> &paths,
            ForwardMapType &forwardMap,
            ReverseMapType &reverseMap,
            const char *label,
            std::function<void()> onOldRemoved = {});

    // Detach a groom from all relationship maps and clear its caches.
    void _DetachGroom(const SdfPath &groomPath);

    // Register a groom prim's relationships from its HairDeformSchema.
    void _AttachGroom(
            const SdfPath &groomPath,
            HairDeformSchema &schema);

    // Handle removal of a target prim (skin, GIM, or point deform
    // deformer) — dirty all dependent grooms and clean up maps.
    void _HandleTargetRemoved(const SdfPath &targetPath);

    UT_Set<SdfPath, SdfPath::Hash> _parents;
    ForwardMapType _skintogroommap;
    ReverseMapType _groomtoskinmap;
    ForwardMapType _guideinterpmeshtogroommap;
    ReverseMapType _groomtoguideinterpmeshmap;
    ForwardMapType _pointdeformtogroommap;
    ReverseMapType _groomtopointdeformmap;
    PrimVarCacheMapPtr _cachemap;
    DeformerCacheMapPtr _deformercachemap;
    SkinMeshCacheMapPtr _skinmeshcachemap;
    RestPointsCacheMapPtr _restpointscachemap;
    SurfaceTopoCacheMapPtr _surfacetopocachemap;
    CurveSkinCaptureCacheMapPtr _maincurveskincapturecachemap;
    CurveSkinCaptureCacheMapPtr _deformercurveskincapturecachemap;
    GuideInterpCacheMapPtr _guideinterpcachemap;
    GIMSurfaceTopoCacheMapPtr _gimsurfacetopocachemap;
    PointDeformCaptureCacheMapPtr _pointdeformcapturecachemap;
    SkinSubdEvalCacheMapPtr _skinsubdcachemap;
    ClumpTopoCacheMapPtr _clumptopocachemap;
    OrientAttribsCacheMapPtr _orientattribscachemap;
};

PXR_NAMESPACE_CLOSE_SCOPE
