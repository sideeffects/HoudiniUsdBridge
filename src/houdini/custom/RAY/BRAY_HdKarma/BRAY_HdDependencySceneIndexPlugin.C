// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "BRAY_HdDependencySceneIndexPlugin.h"

#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/dependenciesSchema.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/mapContainerDataSource.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/perfLog.h>

#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/volumeFieldBindingSchema.h>
#include <pxr/imaging/hd/volumeFieldSchema.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "BRAY_HdDependencySceneIndexPlugin"))

    // Dependencies
    (kmaVolumeFieldBindingToDependencies)
    (kmaAddedToMaterialBindings)
    (kmaRemovedToMaterialBindings)
    (kmaMaterialBindingsToAddedDependency)
    (kmaMaterialBindingsToRemovedDependency)

    // Other
    ((added, "__added__"))
    ((removed, "__removed__"))
);

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<BRAY_HdDependencySceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // This scene index should be added *before*
    // BRAY_HdDependencyForwardingSceneIndexPlugin (which currently uses 1000).
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase
        = 100;

    for (auto &&plugin : { "Karma XPU", "Karma CPU" })
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            plugin,
            _tokens->sceneIndexPluginName,
            nullptr,
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

namespace
{

void _AddIfNecessary(const TfToken &name, TfTokenVector * const names)
{
    if (std::find(names->begin(), names->end(), name) == names->end()) {
        names->push_back(name);
    }
}
    
// For each of the fields in the volume, we want a dependency on the field's
// .volumeField from .volumeFieldBinding on the volume itself
// (i.e., this dependency will be attached to the volume).
// This is to address update issues in Karma when changing between frames and
// not seeing animated volume data changing in the viewport (BUG #143421)
HdContainerDataSourceHandle
_ComputeVolumeFieldToVolumeBindingDependency(
    const HdContainerDataSourceHandle &primSource)
{
    return
        HdMapContainerDataSource::New(
            [](const HdDataSourceBaseHandle &pathDs)
            {
                static HdLocatorDataSourceHandle dependedOnLocatorDataSource =
                    HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                        HdVolumeFieldSchema::GetDefaultLocator());
                static HdLocatorDataSourceHandle affectedLocatorDataSource =
                    HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                        HdVolumeFieldBindingSchema::GetDefaultLocator());
                return 
                    HdDependencySchema::Builder()
                        .SetDependedOnPrimPath(HdPathDataSource::Cast(pathDs))
                        .SetDependedOnDataSourceLocator(dependedOnLocatorDataSource)
                        .SetAffectedDataSourceLocator(affectedLocatorDataSource)
                        .Build();
            },
            HdVolumeFieldBindingSchema::GetFromParent(primSource)
                .GetContainer());
}

// If the volume's .volumeFieldBinding changes, the above per-field dependencies
// are potentially now invalid, so we need to introduce a dependency for them
// as well.
HdContainerDataSourceHandle
_ComputeVolumeFieldBindingToDependenciesDependency()
{
    static HdLocatorDataSourceHandle dependedOnLocatorDataSource =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdVolumeFieldBindingSchema::GetDefaultLocator());
    static HdLocatorDataSourceHandle affectedLocatorDataSource =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdDependenciesSchema::GetDefaultLocator());

    return
        HdRetainedContainerDataSource::New(
            _tokens->kmaVolumeFieldBindingToDependencies,
            HdDependencySchema::Builder()
                .SetDependedOnDataSourceLocator(dependedOnLocatorDataSource)
                .SetAffectedDataSourceLocator(affectedLocatorDataSource)
                .Build());
}

// When a material is added or removed, Karma needs to update the prims that
// have a material binding to said material
// BUG #143440, #143827, #145006
// https://github.com/PixarAnimationStudios/OpenUSD/issues/3573
HdContainerDataSourceHandle
_ComputeMaterialBindingsDependencies(
    HdContainerDataSourceHandle const &inputDs)
{
    const HdMaterialBindingsSchema materialBindings =
        HdMaterialBindingsSchema::GetFromParent(inputDs);
    
    TfToken names[4];
    HdDataSourceBaseHandle dataSources[4];
    size_t count = 0;

    // We do something a bit "cheeky" here where we depend on a nonexistent
    // locator so that we'll never receive a dirty signal, except for if the
    // material prim is removed, in which case the DependencyForwardingSceneIndex
    // will trigger all the prim's dependants.

    // HdsiMaterialBindingResolvingSceneIndex already ran, so we can just
    // call GetMaterialBinding here (which uses the allPurpose binding).
    if (HdPathDataSourceHandle const pathDs =
            materialBindings.GetMaterialBinding().GetPath())
    {
        static HdLocatorDataSourceHandle const materialBindingsLocDs =
            HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                HdMaterialBindingsSchema::GetDefaultLocator());
        
        if (!pathDs->GetTypedValue(0.0f).IsEmpty())
        {
            // If a bound material's `__added__` is dirtied via one of:
            // * `SendPrimsDirtied` in `_PrimsAdded` (below in this file)
            // Then we need the geometry's material bindings to be dirtied
            static HdLocatorDataSourceHandle const addedLocDs =
                HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                    HdDataSourceLocator(_tokens->added));
            HdDataSourceBaseHandle const atmbDependencyDs =
                HdDependencySchema::Builder()
                     .SetDependedOnPrimPath(pathDs)
                     .SetDependedOnDataSourceLocator(addedLocDs)
                     .SetAffectedDataSourceLocator(materialBindingsLocDs)
                     .Build();
            names[count] = _tokens->kmaAddedToMaterialBindings;
            dataSources[count] = atmbDependencyDs;
            count++;
            
            // If a bound material's `__removed__` is dirtied via one of:
            // * `SendPrimsDirtied` in `_PrimsAdded` (below in this file)
            // * `HdDependencyForwardingSceneIndex` dirtying it as a result
            //   of the Hydra prim being removed
            // Then we need the geometry's material bindings to be dirtied
            static HdLocatorDataSourceHandle const removedLocDs =
                HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                    HdDataSourceLocator(_tokens->removed));
            HdDataSourceBaseHandle const rtmbDependencyDs =
                HdDependencySchema::Builder()
                     .SetDependedOnPrimPath(pathDs)
                     .SetDependedOnDataSourceLocator(removedLocDs)
                     .SetAffectedDataSourceLocator(materialBindingsLocDs)
                     .Build();
            names[count] = _tokens->kmaRemovedToMaterialBindings;
            dataSources[count] = rtmbDependencyDs;
            count++;
        }

        // If the material bindings change, the dependencies built just above
        // are potentially invalid and need to be reevaluated.
        
        static const HdLocatorDataSourceHandle atmbDependencyLocDs =
            HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                HdDependenciesSchema::GetDefaultLocator().Append(
                    _tokens->kmaAddedToMaterialBindings));
        static HdDataSourceBaseHandle const mbtaDependencyDs =
            HdDependencySchema::Builder()
                // Prim depends on itself.
                .SetDependedOnDataSourceLocator(materialBindingsLocDs)
                .SetAffectedDataSourceLocator(atmbDependencyLocDs)
                .Build();
        names[count] = _tokens->kmaMaterialBindingsToAddedDependency;
        dataSources[count] = mbtaDependencyDs;
        count++;
        
        static const HdLocatorDataSourceHandle rtmbDependencyLocDs =
            HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                HdDependenciesSchema::GetDefaultLocator().Append(
                    _tokens->kmaRemovedToMaterialBindings));
        static HdDataSourceBaseHandle const mbtrDependencyDs =
            HdDependencySchema::Builder()
                // Prim depends on itself.
                .SetDependedOnDataSourceLocator(materialBindingsLocDs)
                .SetAffectedDataSourceLocator(rtmbDependencyLocDs)
                .Build();
        names[count] = _tokens->kmaMaterialBindingsToRemovedDependency;
        dataSources[count] = mbtrDependencyDs;
        count++;
    }
    
    return HdRetainedContainerDataSource::New(
        count, names, dataSources);
}

    
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

class _PrimDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_PrimDataSource)

    TfTokenVector GetNames() override
    {
        TfTokenVector result = _inputPrim.dataSource->GetNames();

        // We want to be as selective as possible as to when we add any
        // __dependencies data.
        if (_inputPrim.primType == HdPrimTypeTokens->volume ||
            HdMaterialBindingsSchema::GetFromParent(_inputPrim.dataSource)
        ) {
            _AddIfNecessary(HdDependenciesSchema::GetSchemaToken(), &result);
        }
        
        return result;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdDependenciesSchema::GetSchemaToken()) {
            return _GetDependencies();
        }
        return _inputPrim.dataSource->Get(name);
    }

private:
    _PrimDataSource(
        const HdSceneIndexPrim &inputPrim)
    : _inputPrim(inputPrim)
    {
    }

    HdContainerDataSourceHandle _GetDependencies() const {
        // NOTE: The capacity for this array should be updated to always store
        //       at least (but not necessarily more than) the number of
        //       dependencies we're potentially adding, plus one
        //       (for dependencies already existing on the input Scene Index)
        HdContainerDataSourceHandle dataSources[4];
        size_t count = 0;

        if (HdContainerDataSourceHandle const ds =
                HdDependenciesSchema::GetFromParent(_inputPrim.dataSource)
                    .GetContainer()) {
            dataSources[count++] = ds;
        }

        //
        // The list of conditions below should reflect the checks in GetNames()
        //

        if (_inputPrim.primType == HdPrimTypeTokens->volume) {
            dataSources[count++] =
                _ComputeVolumeFieldToVolumeBindingDependency(
                    _inputPrim.dataSource);
            dataSources[count++] =
                _ComputeVolumeFieldBindingToDependenciesDependency();
        }

        if (auto matbindings = HdMaterialBindingsSchema::GetFromParent(
            _inputPrim.dataSource))
        {
            dataSources[count++] =
                _ComputeMaterialBindingsDependencies(
                    _inputPrim.dataSource);
        }

        // And finally return everything we've amassed
        switch(count) {
            case 0:  return nullptr;
            case 1:  return dataSources[0];
            default: return HdOverlayContainerDataSource::New(
                count, dataSources);
        }
    }
    
    const HdSceneIndexPrim _inputPrim;
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TF_DECLARE_REF_PTRS(_SceneIndex);

class _SceneIndex : public HdSingleInputFilteringSceneIndexBase
{
public:
    static _SceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr &inputSceneIndex)
    {
        return TfCreateRefPtr(new _SceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
        if (prim.dataSource) {
            prim.dataSource = _PrimDataSource::New(prim);
        }
        return prim;
    }

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _SceneIndex(
        const HdSceneIndexBaseRefPtr &inputSceneIndex)
      : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
        SetDisplayName("Declare Karma dependencies");
    }

    void _PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        _SendPrimsAdded(entries);

        HdSceneIndexObserver::DirtiedPrimEntries dirtied;
        for (auto &&entry : entries)
        {
            // Sometimes an upstream Scene Index will modify a Hydra prim to
            // be typeless. This is effectively the same as removing it (i.e.,
            // the renderer won't see it), so we'll explicitly dirty our custom
            // `__removed__` locator on the prim. 
            if (entry.primType.IsEmpty())
            {
                static const HdDataSourceLocator removedLocDs(_tokens->removed);
                dirtied.emplace_back(entry.primPath, removedLocDs);
            }
            // In case a material has been (re)added to the scene *after* a
            // geometry looking for it has been added, we need the geometry's
            // `materialBinding` to be dirtied, so we explicitly dirty our
            // custom `__added__` locator on the material prim.
            else if (entry.primType == HdPrimTypeTokens->material)
            {
                static const HdDataSourceLocator addedLocDs(_tokens->added);
                dirtied.emplace_back(entry.primPath, addedLocDs);
            }
        }
        if (!dirtied.empty())
            _SendPrimsDirtied(dirtied);
    }

    void _PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries) override
    {
        _SendPrimsRemoved(entries);
    }

    void _PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries) override
    {
        _SendPrimsDirtied(entries);
    }
};

} // end of anonymous namespace

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Implementation of BRAY_HdDependencySceneIndexPlugin

HdSceneIndexBaseRefPtr
BRAY_HdDependencySceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return _SceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
