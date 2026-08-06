// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "HD_DependencySceneIndexPlugin.h"

#include "pxr/imaging/hd/containerDataSourceEditor.h"
#include "pxr/imaging/hd/dependenciesSchema.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/mapContainerDataSource.h"
#include "pxr/imaging/hd/materialBindingsSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/volumeFieldBindingSchema.h"
#include "pxr/imaging/hd/volumeFieldSchema.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HD_DependencySceneIndexPlugin"))

    (hglVolumeFieldBindingToDependencies)
    (hglMaterialToMaterialBindings)
    (hglMaterialToPrimvars)
    (hglMaterialBindingsToDependencies)
    (hglPrimvarsToDependencies)
);

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HD_DependencySceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // This scene index should be added *before*
    // HD_DependencyForwardingSceneIndexPlugin (which currently uses 1000).
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase
        = 100;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Houdini GL",
        _tokens->sceneIndexPluginName,
        nullptr,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

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
            _tokens->hglVolumeFieldBindingToDependencies,
            HdDependencySchema::Builder()
                .SetDependedOnDataSourceLocator(dependedOnLocatorDataSource)
                .SetAffectedDataSourceLocator(affectedLocatorDataSource)
                .Build());
}

// When a material is changed (for example to connect a texture map to an input)
// HoudiniGL/VK needs to update the prims that have a material binding to said
// material (for example to reevaluate whether UV data is needed) (BUG #143040)
HdContainerDataSourceHandle
_ComputeMaterialToMaterialBindingsDependency(
    HdContainerDataSourceHandle const &inputDs)
{
    const HdMaterialBindingsSchema materialBindings =
        HdMaterialBindingsSchema::GetFromParent(inputDs);
    
    TfToken names[4];
    HdDataSourceBaseHandle dataSources[4];
    size_t count = 0;

    static HdLocatorDataSourceHandle const materialLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdMaterialSchema::GetDefaultLocator());
    static HdLocatorDataSourceHandle const materialBindingsLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdMaterialBindingsSchema::GetDefaultLocator());
    static HdLocatorDataSourceHandle const primvarsLocDs =
        HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
            HdPrimvarsSchema::GetDefaultLocator());

    // HdsiMaterialBindingResolvingSceneIndex already ran, so we can just
    // call GetMaterialBinding here (which uses the allPurpose binding).
    if (HdPathDataSourceHandle const pathDs =
            materialBindings.GetMaterialBinding().GetPath()) {
        if (!pathDs->GetTypedValue(0.0f).IsEmpty()) {
            names[count] = _tokens->hglMaterialToMaterialBindings;
            dataSources[count] = HdDependencySchema::Builder()
                     .SetDependedOnPrimPath(pathDs)
                     .SetDependedOnDataSourceLocator(materialLocDs)
                     .SetAffectedDataSourceLocator(materialBindingsLocDs)
                     .Build();
            count++;
            
            names[count] = _tokens->hglMaterialToPrimvars;
            dataSources[count] = HdDependencySchema::Builder()
                     .SetDependedOnPrimPath(pathDs)
                     .SetDependedOnDataSourceLocator(materialLocDs)
                     .SetAffectedDataSourceLocator(primvarsLocDs)
                     .Build();
            count++;
        }
    }

    // If the material bindings change, the dependencies built just above
    // are potentially invalid and need to be reevaluated.
    {
        {
            static const HdLocatorDataSourceHandle dependencyLocDs =
                HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                    HdDependenciesSchema::GetDefaultLocator().Append(
                        _tokens->hglMaterialToMaterialBindings));
            static HdDataSourceBaseHandle const dependencyDs =
                HdDependencySchema::Builder()
                    // Prim depends on itself.
                    .SetDependedOnDataSourceLocator(materialBindingsLocDs)
                    .SetAffectedDataSourceLocator(dependencyLocDs)
                    .Build();
            names[count] = _tokens->hglMaterialBindingsToDependencies;
            dataSources[count] = dependencyDs;
            count++;
        }
        {
            static const HdLocatorDataSourceHandle dependencyLocDs =
                HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                    HdDependenciesSchema::GetDefaultLocator().Append(
                        _tokens->hglMaterialToPrimvars));
            static HdDataSourceBaseHandle const dependencyDs =
                HdDependencySchema::Builder()
                    // Prim depends on itself.
                    .SetDependedOnDataSourceLocator(primvarsLocDs)
                    .SetAffectedDataSourceLocator(dependencyLocDs)
                    .Build();
            names[count] = _tokens->hglMaterialBindingsToDependencies;
            dataSources[count] = dependencyDs;
            count++;
        }
    }
    
    return HdRetainedContainerDataSource::New(count, names, dataSources);
}
    
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
            HdMaterialBindingsSchema::GetFromParent(_inputPrim.dataSource)) {
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
            dataSources[count] =
                ds;
            count++;
        }

        //
        // The list of conditions below should reflect the checks in GetNames()
        //

        if (_inputPrim.primType == HdPrimTypeTokens->volume) {
            dataSources[count++] =
                _ComputeVolumeFieldToVolumeBindingDependency(
                    _inputPrim.dataSource);
            dataSources[count] =
                _ComputeVolumeFieldBindingToDependenciesDependency();
            count++;
        }
        
        if (HdMaterialBindingsSchema::GetFromParent(_inputPrim.dataSource)) {
            dataSources[count] =
                _ComputeMaterialToMaterialBindingsDependency(
                    _inputPrim.dataSource);
            count++;
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

/// \class _SceneIndex
///
/// The scene index that adds dependencies for volume prims.
///
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
        SetDisplayName("Declare HoudiniGL/VK dependencies");
    }

    void _PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        _SendPrimsAdded(entries);
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

}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Implementation of HD_DependencySceneIndexPlugin

HdSceneIndexBaseRefPtr
HD_DependencySceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return _SceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
