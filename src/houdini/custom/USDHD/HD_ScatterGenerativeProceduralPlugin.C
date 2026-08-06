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

//#include "HD_HdGpApexScatterEngine.h"
#include "HD_HdGpHapiScatterEngine.h"
#include "HD_HdGpScatterEngine.h"
// #include "HD_HdGpSopScatterEngine.h"

#include <UT/UT_CameraParms.h>
#include <UT/UT_DoubleLock.h>
#include <UT/UT_Lock.h>
#include <UT/UT_Map.h>
#include <UT/UT_UniquePtr.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quath.h>
#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/instanceSchema.h>
#include <pxr/imaging/hd/instancerTopologySchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneGlobalsSchema.h>
#include <pxr/imaging/hd/sceneIndexPrimView.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/imaging/hdGp/generativeProceduralPlugin.h>
#include <pxr/imaging/hdGp/generativeProceduralPluginRegistry.h>

PXR_NAMESPACE_USING_DIRECTIVE

/// _ScatterProcedural /////////////////////////////////////////////////////////

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((procEnableCamera,         "proc:enableCamera"))
    ((procCameraPath,           "proc:cameraPath"))
    ((procSourceMeshPath,       "proc:sourceMeshPath"))
    ((procSourceMaskPath,       "proc:maskObjectPath"))
    ((procSourceMaskPointsPath, "proc:maskPointsPath"))
    ((procSourcePIPath,         "proc:sourcePointInstancerPath"))
    ((procGraphMode,            "proc:graph:mode"))
    ((procGraphPath,            "proc:graph:path"))
    ((instancer,                "pointinstancer"))
    (apex)
    (sop)
    (hapi)
);

namespace
{

///////////////////////////////////////////////////////////////////////////////
// Procedural declaration which populates an existing child Hydra `instancer`
// prim by scattering points on the `primvars:sourceMeshPath` mesh(es).

class _ScatterProcedural : public HdGpGenerativeProcedural
{
public:
    _ScatterProcedural(const SdfPath &proceduralPrimPath);

    // Declares current state of dependencies
    DependencyMap UpdateDependencies(
            const HdSceneIndexBaseRefPtr &inputScene) override;

    // Handles dirty dependencies and returns the current state (name & type)
    // of child prims
    ChildPrimTypeMap Update(
            const HdSceneIndexBaseRefPtr &inputScene,
            const ChildPrimTypeMap &previousResult,
            const DependencyMap &dirtiedDependencies,
            HdSceneIndexObserver::DirtiedPrimEntries *outputDirtiedPrims)
            override;

    // Returns dataSource of a child prim
    HdSceneIndexPrim GetChildPrim(
            const HdSceneIndexBaseRefPtr &inputScene,
            const SdfPath &childPrimPath) override;

private:
    /// private member variables //////////////////////////////////////////////

    UT_UniquePtr<HD_HdGpScatterEngine> myEngine;
    UT_Lock myLock;
    bool myUpToDate;
    // Retained container with the canonical three per-instance
    // primvars (instanceTranslations/Rotations/Scales) and the
    // instancerTopology (instanceIndices). Overlaid onto the
    // instancer prim's data source as the highest-opinion layer.
    HdContainerDataSourceHandle myInstancerOverlayDs;
    // Optional sibling overlay holding `{primvars: extraPrimvars}` —
    // additional per-instance primvars the engine surfaced beyond the
    // canonical three. Null when the engine doesn't populate it.
    HdContainerDataSourceHandle myExtraPrimvarsDs;

    /// private member functions //////////////////////////////////////////////

    void initializeEngine(const HdSceneIndexPrim &procPrim);
    bool setEngineGraphPath(const HdSceneIndexPrim &procPrim);
    bool updateEngineGraphParms(const HdSceneIndexPrim &procPrim);

};

///////////////////////////////////////////////////////////////////////////////
// Helper functions

VtArray<SdfPath> getProcSourcePaths(const HdSceneIndexPrim &prim, const TfToken pv_name)
{
    HdPrimvarsSchema primvars
            = HdPrimvarsSchema::GetFromParent(prim.dataSource);
    if (HdSampledDataSourceHandle sourceMeshDs
        = primvars.GetPrimvar(pv_name).GetPrimvarValue())
    {
        VtValue v = sourceMeshDs->GetValue(0.0f);
        if (v.IsHolding<VtArray<SdfPath>>())
            return v.UncheckedGet<VtArray<SdfPath>>();
    }
    return VtArray<SdfPath>();
}

using PathInstances = UT_Map<SdfPath, UT_Array<GfMatrix4d>>;

enum class LeafKind { Mesh, Instancer };

void buildDependencies(
    const HdSceneIndexBaseRefPtr &scene,
    const SdfPath &root,
    const GfMatrix4d &parent_mtx,
    HdGpGenerativeProcedural::DependencyMap &dependencies,
    PathInstances &path_instances,
    LeafKind leaf_kind)
{
    HdSceneIndexPrimView view(scene, root);
    for (auto iter = view.begin(); iter != view.end(); ++iter)
    {
        HdSceneIndexPrim prim = scene->GetPrim(*iter);
        if (!prim)
            continue;
        bool is_leaf = (leaf_kind == LeafKind::Mesh)
            ? (bool)HdMeshSchema::GetFromParent(prim.dataSource)
            : (bool)HdInstancerTopologySchema::GetFromParent(prim.dataSource);
        // If this is the kind of prim we're looking for, register it
        if (is_leaf)
        {
            path_instances[*iter].emplace_back(parent_mtx);
            dependencies[*iter].insert({
                HdPrimvarsSchema::GetDefaultLocator(),
                HdXformSchema::GetDefaultLocator()
            });
            iter.SkipDescendants();
        }
        // Non-leaf instancers are dead-ends in this traversal (their
        // contents are reached via the instance/prototype branch below).
        else if (HdInstancerTopologySchema::GetFromParent(prim.dataSource))
        {
            iter.SkipDescendants();
        }
        // Or if we've found something instance-prototype-like
        else if (auto instance = HdInstanceSchema::GetFromParent(prim.dataSource))
        {
            SdfPath instancer_path =
                instance.GetInstancer()->GetTypedValue(0.0f);
            HdSceneIndexPrim instancer_prim = scene->GetPrim(instancer_path);
            if (auto instancer = HdInstancerTopologySchema::GetFromParent(
                instancer_prim.dataSource))
            {
                dependencies[instancer_path].insert({
                    HdPrimvarsSchema::GetDefaultLocator()
                });
                
                GfMatrix4d mtx = parent_mtx;
                if (auto primvars =
                    HdPrimvarsSchema::GetFromParent(instancer_prim.dataSource))
                {
                    if (auto primvar =
                        primvars.GetPrimvar(HdInstancerTokens->instanceTransforms))
                    {
                        VtValue xform_vt = primvar.GetPrimvarValue()->GetValue(0.0f);
                        if (xform_vt.IsHolding<VtArray<GfMatrix4d>>())
                        {
                            int instance_idx =
                                instance.GetInstanceIndex()->GetTypedValue(0.0f);
                            auto instance_mtxs =
                                xform_vt.UncheckedGet<VtArray<GfMatrix4d>>(); 
                            mtx = instance_mtxs[instance_idx] * mtx;
                        }
                    }
                }
                
                int prototype_idx =
                    instance.GetPrototypeIndex()->GetTypedValue(0.0f);
                VtArray<SdfPath> prototype_paths =
                    instancer.GetPrototypes()->GetTypedValue(0.0f);
                SdfPath prototype_path = prototype_paths[prototype_idx];
                
                // Recurse to expand the prototype
                buildDependencies(scene, prototype_path, mtx,
                    dependencies, path_instances, leaf_kind);

                iter.SkipDescendants();
            }
            else
            {
                TF_WARN(
                    "Prim %s marked as being instanced but %s is not an instancer",
                    (*iter).GetText(), instancer_path.GetText());
            }
        }
    }
}

void getProcCameraDetails(
    const HdSceneIndexBaseRefPtr &scene, const HdSceneIndexPrim &prim,
    bool &enabled, SdfPath &cam_path, bool &using_scene_globals)
{
    enabled = false;
    using_scene_globals = false;
    
    HdPrimvarsSchema primvars
            = HdPrimvarsSchema::GetFromParent(prim.dataSource);
    if (HdSampledDataSourceHandle enableDs =
        primvars.GetPrimvar(_tokens->procEnableCamera).GetPrimvarValue())
    {
        if (VtValue enableVt = enableDs->GetValue(0.0f);
            (enableVt.IsHolding<bool>() && enableVt.UncheckedGet<bool>()) ||
            (enableVt.IsHolding<int>() && enableVt.UncheckedGet<int>()))
        {
            enabled = true;
            if (HdSampledDataSourceHandle pathDs =
                primvars.GetPrimvar(_tokens->procCameraPath).GetPrimvarValue())
            {
                VtValue pathVt = pathDs->GetValue(0.0f);
                if (pathVt.IsHolding<SdfPath>())
                    cam_path = pathVt.UncheckedGet<SdfPath>();
                else if (pathVt.IsHolding<VtArray<SdfPath>>() &&
                    pathVt.GetArraySize() > 0)
                    cam_path = pathVt.UncheckedGet<VtArray<SdfPath>>()[0];
            }
            // If no explicit camera path provided, use the sceneGlobals
            if (cam_path.IsEmpty())
            {
                if (auto globals = HdSceneGlobalsSchema::GetFromSceneIndex(scene))
                {
                    using_scene_globals = true;
                    if (auto camDs = globals.GetPrimaryCameraPrim())
                        cam_path = camDs->GetTypedValue(0.f);
                }
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// Procedural definition

_ScatterProcedural::_ScatterProcedural(
    const SdfPath &proceduralPrimPath)
    : HdGpGenerativeProcedural(proceduralPrimPath)
    , myUpToDate(false)
{
}

// Looks at arguments declares current state of dependencies
_ScatterProcedural::DependencyMap
_ScatterProcedural::UpdateDependencies(
        const HdSceneIndexBaseRefPtr &inputScene)
{
    HdSceneIndexPrim proc = inputScene->GetPrim(_GetProceduralPrimPath());
    DependencyMap dependencies;
    PathInstances meshInstances;
    for (auto &&path : getProcSourcePaths(proc, _tokens->procSourceMeshPath))
    {
        buildDependencies(inputScene, path, GfMatrix4d(1.0),
            dependencies, meshInstances, LeafKind::Mesh);
    }
    PathInstances maskInstances;
    for (auto &&path : getProcSourcePaths(proc, _tokens->procSourceMaskPath))
    {
        buildDependencies(inputScene, path, GfMatrix4d(1.0),
            dependencies, maskInstances, LeafKind::Mesh);
    }
    PathInstances pointsInstances;
    for (auto &&path : getProcSourcePaths(proc, _tokens->procSourceMaskPointsPath))
    {
        buildDependencies(inputScene, path, GfMatrix4d(1.0),
            dependencies, pointsInstances, LeafKind::Instancer);
    }

    // Camera
    bool cam_enabled;
    SdfPath cam_path;
    bool cam_scene_globals;
    getProcCameraDetails(inputScene, proc, cam_enabled, cam_path, cam_scene_globals);
    if (cam_enabled)
    {
        if (cam_scene_globals)
            dependencies[SdfPath::AbsoluteRootPath()] = {
                HdSceneGlobalsSchema::GetPrimaryCameraPrimLocator()
            };
        if (!cam_path.IsEmpty())
            dependencies[cam_path] = {
                HdCameraSchema::GetDefaultLocator(),
                HdXformSchema::GetDefaultLocator()
            };
    }
        
    // The instancer being populated
    dependencies[_GetProceduralPrimPath().AppendChild(_tokens->instancer)] =
        { HdXformSchema::GetDefaultLocator() };
    
    return dependencies;
}

// Cooks/Recooks and returns the current state of child paths and their
// types
_ScatterProcedural::ChildPrimTypeMap
_ScatterProcedural::Update(
        const HdSceneIndexBaseRefPtr &inputScene,
        const ChildPrimTypeMap &previousResult,
        const DependencyMap &dirtiedDependencies,
        HdSceneIndexObserver::DirtiedPrimEntries *outputDirtiedPrims)
{
    // Let's pre-fetch some of the procedural parms
    HdSceneIndexPrim proc = inputScene->GetPrim(_GetProceduralPrimPath());

    DependencyMap dependencies;
    PathInstances meshInstances;
    for (auto &&path : getProcSourcePaths(proc, _tokens->procSourceMeshPath))
    {
        buildDependencies(inputScene, path, GfMatrix4d(1.0),
            dependencies, meshInstances, LeafKind::Mesh);
    }
    PathInstances maskInstances;
    for (auto &&path : getProcSourcePaths(proc, _tokens->procSourceMaskPath))
    {
        buildDependencies(inputScene, path, GfMatrix4d(1.0),
            dependencies, maskInstances, LeafKind::Mesh);
    }
    PathInstances pointsInstances;
    for (auto &&path : getProcSourcePaths(proc, _tokens->procSourceMaskPointsPath))
    {
        buildDependencies(inputScene, path, GfMatrix4d(1.0),
            dependencies, pointsInstances, LeafKind::Instancer);
    }

    bool cam_enabled;
    SdfPath cam_path;
    bool cam_scene_globals = false;
    getProcCameraDetails(inputScene, proc, cam_enabled, cam_path, cam_scene_globals);
    
    // Various flags we'll set as we traverse the dirtied dependencies
    bool needToInitialize = !myEngine;
    bool needToSetGraph = false;
    bool needToSetScatterSurfaces = false;
    bool needToSetMaskSurfaces = false;
    bool needToSetMaskPoints = false;
    bool needToSetCamera = false;
    bool needToSetParms = false;
    VtArray<SdfPath> dirtyScatterSurfaces;
    VtArray<SdfPath> dirtyMaskSurfaces;
    VtArray<SdfPath> dirtyMaskPoints;

    for (auto &&dep : dirtiedDependencies)
    {
        // Handle changes on the procedural
        if (dep.first == _GetProceduralPrimPath())
        {
            for (auto &&loc : dep.second)
            {
                // Switching from APEX -> HAPI (for example) means a fresh start
                if (loc.GetLastElement() == _tokens->procGraphMode)
                {
                    needToInitialize = true;
                }
                else if (loc.GetLastElement() == _tokens->procGraphPath)
                {
                    needToSetGraph = true;
                }
                else if (loc.GetLastElement() == _tokens->procSourceMeshPath)
                {
                    needToSetScatterSurfaces = true;
                }
                else if (loc.GetLastElement() == _tokens->procSourceMaskPath)
                {
                    needToSetMaskSurfaces = true;
                }
                else if (loc.GetLastElement() == _tokens->procSourceMaskPointsPath)
                {
                    needToSetMaskPoints = true;
                }
                else if (loc.GetLastElement() == _tokens->procEnableCamera ||
                         loc.GetLastElement() == _tokens->procCameraPath)
                {
                    needToSetCamera = true;
                }
                else if (loc.Intersects(HdPrimvarsSchema::GetDefaultLocator()))
                {
                    needToSetParms = true;
                }
            }
        }
        else if (dep.first == SdfPath::AbsoluteRootPath())
        {
            if (cam_enabled && cam_scene_globals && dep.second.Intersects(
                HdSceneGlobalsSchema::GetPrimaryCameraPrimLocator()))
            {
                needToSetCamera = true;
            }
        }
        else
        {
            HdSceneIndexPrim prim = inputScene->GetPrim(dep.first);
            
            // If the dependency matches our registered list of surface meshes
            // then either the prim is either dirty or has been removed.
            // Regardless the case, we'll trigger an update with the engine.
            if (meshInstances.contains(dep.first))
            {
                dirtyScatterSurfaces.emplace_back(dep.first);
            }
            else if (maskInstances.contains(dep.first))
            {
                dirtyMaskSurfaces.emplace_back(dep.first);
            }
            else if (pointsInstances.contains(dep.first))
            {
                dirtyMaskPoints.emplace_back(dep.first);
            }

            else if (HdCameraSchema::GetFromParent(prim.dataSource))
            {
                // Same for cameras
                if (cam_path == dep.first)
                    needToSetCamera = true;
            }
            
            else if (HdInstancerTopologySchema::GetFromParent(prim.dataSource))
            {
                TF_WARN("Instancer %s triggered an update we're not handling",
                    dep.first.GetText());
            }
        }
    }
    
    // And now we're ready to do the work.

    bool dirty = false;

    if (needToInitialize)
    {
        initializeEngine(proc);
        dirty = true;
        needToSetGraph = true;
        needToSetParms = true;
        needToSetScatterSurfaces = true;
        needToSetMaskSurfaces = true;
        needToSetMaskPoints = true;
        needToSetCamera = true;
    }
    if (needToSetGraph)
    {
        dirty |= setEngineGraphPath(proc);
        needToSetParms = true;    
    }
    if (needToSetParms)
    {
        dirty |= updateEngineGraphParms(proc);
    }
    if (needToSetScatterSurfaces)
    {
        // Reset all surfaces, and prepare to re-send all the current meshes
        myEngine->setScatterSurface(std::string(), HdSceneIndexPrim(),
            UT_Array<GfMatrix4d>());
        dirtyScatterSurfaces.clear();
        for (auto &&key : meshInstances.key_range())
            dirtyScatterSurfaces.emplace_back(key);
    }
    for (auto &&path : dirtyScatterSurfaces)
    {
        HdSceneIndexPrim prim = inputScene->GetPrim(path);

        // Ensure we only pass prim data for prims with the `mesh` schema
        if (auto schema = HdMeshSchema::GetFromParent(prim.dataSource); !schema)
            prim.dataSource.reset();
        // Warnings/errors will come from the engine if appropriate
        dirty |= myEngine->setScatterSurface(path.GetAsString(), prim,
            meshInstances[path]);
    }
    if (needToSetMaskSurfaces)
    {
        // Reset all masks, and prepare to re-send all the current masks
        myEngine->setMaskSurface(std::string(), HdSceneIndexPrim(),
            UT_Array<GfMatrix4d>());
        dirtyMaskSurfaces.clear();
        for (auto &&key : maskInstances.key_range())
            dirtyMaskSurfaces.emplace_back(key);
    }
    for (auto &&path : dirtyMaskSurfaces)
    {
        HdSceneIndexPrim prim = inputScene->GetPrim(path);

        // Ensure we only pass prim data for prims with the `mesh` schema
        if (auto schema = HdMeshSchema::GetFromParent(prim.dataSource); !schema)
            prim.dataSource.reset();
        // Warnings/errors will come from the engine if appropriate
        dirty |= myEngine->setMaskSurface(path.GetAsString(), prim,
            maskInstances[path]);
    }
    if (needToSetMaskPoints)
    {
        // Reset all point masks, and prepare to re-send all current ones
        myEngine->setMaskPoints(std::string(), HdSceneIndexPrim(),
            UT_Array<GfMatrix4d>());
        dirtyMaskPoints.clear();
        for (auto &&key : pointsInstances.key_range())
            dirtyMaskPoints.emplace_back(key);
    }
    for (auto &&path : dirtyMaskPoints)
    {
        HdSceneIndexPrim prim = inputScene->GetPrim(path);

        // Ensure we only pass prim data for prims with the instancer schema
        if (auto schema = HdInstancerTopologySchema::GetFromParent(prim.dataSource);
            !schema)
            prim.dataSource.reset();
        // Warnings/errors will come from the engine if appropriate
        dirty |= myEngine->setMaskPoints(path.GetAsString(), prim,
            pointsInstances[path]);
    }
    if (needToSetCamera)
    {
        // Warnings/errors will come from the engine if appropriate
        if (cam_enabled && !cam_path.IsEmpty())
            dirty |= myEngine->setCamera(
                cam_path.GetAsString(), inputScene->GetPrim(cam_path));
        else
            dirty |= myEngine->setCamera(std::string(), HdSceneIndexPrim());
    }

    SdfPath instancerPath = 
        _GetProceduralPrimPath().AppendChild(_tokens->instancer);
    
    if (dirty)
    {
        outputDirtiedPrims->emplace_back(
            HdSceneIndexObserver::DirtiedPrimEntry(
                instancerPath, {
                    HdInstancerTopologySchema::GetDefaultLocator(),
                    HdPrimvarsSchema::GetDefaultLocator()
                }));
        myUpToDate = false;
    }
    
    // Regardless whether we've needed to cook or not,
    // return that we have an instancer child prim.
    return {
        { instancerPath, HdPrimTypeTokens->instancer }
    };
}

// Returns dataSource of a child prim
HdSceneIndexPrim
_ScatterProcedural::GetChildPrim(
        const HdSceneIndexBaseRefPtr &inputScene,
        const SdfPath &childPrimPath)
{
    SdfPath procpath = _GetProceduralPrimPath();
    HdSceneIndexPrim procPrim = inputScene->GetPrim(procpath);
    SdfPath instancerpath = procpath.AppendChild(_tokens->instancer);
    HdSceneIndexPrim instancerPrim = inputScene->GetPrim(instancerpath);

    if (!instancerPrim.dataSource)
    {
        TF_RUNTIME_ERROR("Couldn't find upstream instancer at %s",
            instancerpath.GetAsString().c_str());
        return HdSceneIndexPrim();
    }

    // Cook if we're not up to date
    UT_DoubleLock<bool> lock(myLock, myUpToDate);
    if (!lock.getValue())
    {
        myInstancerOverlayDs.reset();
        myExtraPrimvarsDs.reset();

        GfMatrix4d instancerXform(1.0);
        if (auto schema = HdXformSchema::GetFromParent(instancerPrim.dataSource);
            schema.IsDefined())
        {
            instancerXform = schema.GetMatrix()->GetTypedValue(0.0f).GetInverse();
        }

        HD_HdGpScatterEngine::Result cookResult = myEngine->cook();

        for (auto &translation : cookResult.translations)
            translation = GfVec3f(instancerXform.Transform(translation));
        GfQuath quat(instancerXform.ExtractRotationQuat().GetNormalized());
        for (auto &rotation : cookResult.rotations)
            rotation = quat * rotation;

        if (cookResult.translations.size() != cookResult.protoindexes.size())
        {
            TF_RUNTIME_ERROR(
                "Size mismatch between translation (%zu) and protoindex (%zu) arrays",
                cookResult.translations.size(), cookResult.protoindexes.size());
            return HdSceneIndexPrim();
        }

        size_t npts = cookResult.translations.size();
        
        HdInstancerTopologySchema instancerTopo =
            HdInstancerTopologySchema::GetFromParent(instancerPrim.dataSource);
        HdPathArrayDataSourceHandle prototypesDs = instancerTopo.GetPrototypes();
        if (!prototypesDs)
        {
            TF_RUNTIME_ERROR("Instancer has no prototypes defined");
            return HdSceneIndexPrim();
        }
        size_t nprotos = prototypesDs->GetTypedValue(0.f).size();
        if (nprotos == 0)
        {
            TF_RUNTIME_ERROR("Instancer has empty prototypes array");
            return HdSceneIndexPrim();
        }
        
        UT_Array<VtIntArray> instanceIndices(nprotos, nprotos);
        for (size_t i = 0; i < npts; i++)
        {
            int protoindex = cookResult.protoindexes[i];
            if (protoindex < 0 || static_cast<size_t>(protoindex) >= nprotos)
            {
                TF_WARN("Invalid protoindex %d at point %zu (nprotos=%zu), "
                        "skipping", protoindex, i, nprotos);
                continue;
            }
            instanceIndices[protoindex].emplace_back(i);
        }
        UT_Array<HdDataSourceBaseHandle> instanceIndicesDs(nprotos);
        for (size_t i = 0; i < nprotos; i++)
            instanceIndicesDs.emplace_back(
                HdRetainedTypedSampledDataSource<VtIntArray>::New(instanceIndices[i]));

        myInstancerOverlayDs = HdRetainedContainerDataSource::New(
            HdPrimvarsSchemaTokens->primvars,
            HdRetainedContainerDataSource::New(
                HdInstancerTokens->instanceTranslations,
                HdPrimvarSchema::Builder()
                    .SetInterpolation(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            HdPrimvarSchemaTokens->instance))
                    .SetPrimvarValue(
                        HdRetainedTypedSampledDataSource<VtVec3fArray>::New(
                            cookResult.translations))
                    .Build(),
                HdInstancerTokens->instanceRotations,
                HdPrimvarSchema::Builder()
                    .SetInterpolation(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            HdPrimvarSchemaTokens->instance))
                    .SetPrimvarValue(
                        HdRetainedTypedSampledDataSource<VtQuathArray>::New(
                            cookResult.rotations))
                    .Build(),
                HdInstancerTokens->instanceScales,
                HdPrimvarSchema::Builder()
                    .SetInterpolation(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            HdPrimvarSchemaTokens->instance))
                    .SetPrimvarValue(
                        HdRetainedTypedSampledDataSource<VtVec3fArray>::New(
                            cookResult.scales))
                    .Build()
                ),
            HdInstancerTopologySchemaTokens->instancerTopology,
            HdRetainedContainerDataSource::New(
                HdInstancerTopologySchemaTokens->instanceIndices,
                HdIntArrayVectorSchema::BuildRetained(nprotos, instanceIndicesDs.data())
            )
        );

        // If the engine emitted extra per-instance primvars, wrap
        // them in a top-level prim container with a `primvars` child
        // so HdOverlayContainerDataSource can merge them with our
        // retained-three at the same locator. HdOverlayContainerDataSource
        // recursively merges container-typed overlapping children
        // (pxr/imaging/hd/overlayContainerDataSource.h:18), so the
        // result is the union of all per-instance primvars.
        if (cookResult.extraPrimvars)
        {
            myExtraPrimvarsDs = HdRetainedContainerDataSource::New(
                HdPrimvarsSchemaTokens->primvars,
                cookResult.extraPrimvars);
        }

        lock.setValue(true);
    }

    HdSceneIndexPrim result;
    result.primType = HdPrimTypeTokens->instancer;
    if (myExtraPrimvarsDs)
    {
        result.dataSource = HdOverlayContainerDataSource::New(
            myInstancerOverlayDs, myExtraPrimvarsDs, instancerPrim.dataSource);
    }
    else
    {
        result.dataSource = HdOverlayContainerDataSource::New(
            myInstancerOverlayDs, instancerPrim.dataSource);
    }

    return result;
}

void
_ScatterProcedural::initializeEngine(const HdSceneIndexPrim &procPrim)
{
    HdPrimvarsSchema primvars
            = HdPrimvarsSchema::GetFromParent(procPrim.dataSource);

    TfToken graphMode;
    if (HdSampledDataSourceHandle graphModeDs
        = primvars.GetPrimvar(_tokens->procGraphMode).GetPrimvarValue())
    {
        VtValue v = graphModeDs->GetValue(0.0f);
        if (v.IsHolding<TfToken>())
        {
            graphMode = v.UncheckedGet<TfToken>();
        }
    }

    myEngine.reset();
    if (graphMode == _tokens->hapi)
        myEngine = UTmakeUnique<HD_HdGpHapiScatterEngine>();
    /*
    else if (graphMode == _tokens->apex)
        myEngine = UTmakeUnique<HD_HdGpApexScatterEngine>();
    else if (graphMode == _tokens->sop)
        myEngine = UTmakeUnique<HD_HdGpSopScatterEngine>();
    */
    else
        TF_WARN("Unsupported engine mode %s", graphMode.GetText());
}

bool
_ScatterProcedural::setEngineGraphPath(const HdSceneIndexPrim &procPrim)
{
    HdPrimvarsSchema primvars
            = HdPrimvarsSchema::GetFromParent(procPrim.dataSource);
    
    std::string graphPath;
    if (HdSampledDataSourceHandle graphPathDs
            = primvars.GetPrimvar(_tokens->procGraphPath).GetPrimvarValue())
    {
        VtValue v = graphPathDs->GetValue(0.0f);
        if (v.IsHolding<SdfAssetPath>())
        {
            graphPath = v.UncheckedGet<SdfAssetPath>().GetResolvedPath();
            if (graphPath.empty())
                graphPath = v.UncheckedGet<SdfAssetPath>().GetAssetPath();
        }
        else if (v.IsHolding<std::string>())
            graphPath = v.UncheckedGet<std::string>();
    }
    
    return myEngine->loadGraph(graphPath);
}

bool
_ScatterProcedural::updateEngineGraphParms(const HdSceneIndexPrim &procPrim)
{
    HdPrimvarsSchema primvars
            = HdPrimvarsSchema::GetFromParent(procPrim.dataSource);

    UT_Options globalParms;
    
    // Iterate over all primvars to find proc:inputs:global:* parameters
    const std::string globalPrefix = "proc:inputs:global:";
    for (const TfToken &name : primvars.GetPrimvarNames())
    {
        const std::string &nameStr = name.GetString();
        if (nameStr.rfind(globalPrefix, 0) != 0)
            continue;
        std::string parmName = nameStr.substr(globalPrefix.length());
        if (HdSampledDataSourceHandle ds =
            primvars.GetPrimvar(name).GetPrimvarValue())
        {
            VtValue v = ds->GetValue(0.0f);
            if (v.IsHolding<float>())
                globalParms.setOptionF(parmName.c_str(), v.UncheckedGet<float>());
            else if (v.IsHolding<double>())
                globalParms.setOptionF(parmName.c_str(), v.UncheckedGet<double>());
            else if (v.IsHolding<int>())
                globalParms.setOptionI(parmName.c_str(), v.UncheckedGet<int>());
            else if (v.IsHolding<std::string>())
                globalParms.setOptionS(parmName.c_str(), v.UncheckedGet<std::string>().c_str());
        }
    }

    return myEngine->setParms(globalParms);
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// Plugin declaration and definition

class HD_ScatterGenerativeProceduralPlugin : public HdGpGenerativeProceduralPlugin
{
public:
    HD_ScatterGenerativeProceduralPlugin() = default;

    HdGpGenerativeProcedural *Construct(
        const SdfPath &proceduralPrimPath) override
    {
        return new _ScatterProcedural(proceduralPrimPath);
    }
};

///////////////////////////////////////////////////////////////////////////////
// Registration hook

TF_REGISTRY_FUNCTION(TfType)
{
    HdGpGenerativeProceduralPluginRegistry::Define<
        HD_ScatterGenerativeProceduralPlugin,
        HdGpGenerativeProceduralPlugin>();
}
