//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "HD_CoordSysCameraDataSceneIndex.h"

#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/imaging/hd/dependenciesSchema.h>

PXR_NAMESPACE_OPEN_SCOPE

HD_CoordSysCameraDataSceneIndexRefPtr
HD_CoordSysCameraDataSceneIndex::New(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    return TfCreateRefPtr(
        new HD_CoordSysCameraDataSceneIndex(inputSceneIndex));
}

HD_CoordSysCameraDataSceneIndex::HD_CoordSysCameraDataSceneIndex(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
  : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
}

HdSceneIndexPrim
HD_CoordSysCameraDataSceneIndex::GetPrim(const SdfPath &primPath) const
{
    // Pass-through for all prims that aren't coordSys prims
    HdSceneIndexPrim prim =
        _GetInputSceneIndex()->GetPrim(primPath);
    if (prim.primType != HdPrimTypeTokens->coordSys)
        return prim;

    // And for coordSys prims who aren't children of a camera prim
    HdSceneIndexPrim parentPrim =
        _GetInputSceneIndex()->GetPrim(primPath.GetParentPath());
    if (parentPrim.primType != HdPrimTypeTokens->camera)
        return prim;

    // Fetch existing dependencies
    TfSmallVector<TfToken, 2> names;
    TfSmallVector<HdDataSourceBaseHandle, 2> values;
    HdDependenciesSchema::EntryVector dependenciesEntries;
    if (auto dependencies = HdDependenciesSchema::GetFromParent(prim.dataSource))
    {
        for (auto dependency : dependencies.GetEntries())
        {
            names.emplace_back(dependency.first);
            values.emplace_back(dependency.second.GetContainer());
        }
    }

    // Add a camera-data dependency
    static HdLocatorDataSourceHandle const cameraLocatorDs =
    HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
        HdCameraSchema::GetDefaultLocator());
    static HdLocatorDataSourceHandle const xformLocatorDs =
    HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
        HdXformSchema::GetDefaultLocator());
    auto cameraDependency = HdDependencySchema::Builder()
        .SetDependedOnPrimPath(
            HdRetainedTypedSampledDataSource<SdfPath>::New(primPath.GetParentPath()))
        .SetDependedOnDataSourceLocator(cameraLocatorDs)
        .SetAffectedDataSourceLocator(xformLocatorDs)
        .Build();
    names.emplace_back("cameraDependency");
    values.emplace_back(cameraDependency);
    auto dependencies = HdDependenciesSchema::BuildRetained(
        names.size(), &names[0], &values[0]);
    auto dependenciesDataSource =
        HdRetainedContainerDataSource::New(HdDependenciesSchema::GetSchemaToken(),
                                           dependencies);

    // Overlay new dependencies over the coordSys over the camera (`parentPrim`)
    // We really only need the `camera` bit of the camera's dataSource, but it's
    // easier to just include all the camera's data.
    // That said, it's important to note that, just above, we're explicitly
    // adding a dependency on the `camera` data, so only changes to things like
    // focal-length (and not some arbitrary primvar on the camera) will trigger
    // an update to the coordSys.
    auto dataSource = HdOverlayContainerDataSource::New(
        dependenciesDataSource, prim.dataSource, parentPrim.dataSource);
    return { HdPrimTypeTokens->coordSys, dataSource };
}

SdfPathVector
HD_CoordSysCameraDataSceneIndex::GetChildPrimPaths(
    const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HD_CoordSysCameraDataSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    if (_IsObserved())
        _SendPrimsAdded(entries);
}

void
HD_CoordSysCameraDataSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    if (_IsObserved())
        _SendPrimsRemoved(entries);
}

void
HD_CoordSysCameraDataSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    if (_IsObserved())
        _SendPrimsDirtied(entries);
}

PXR_NAMESPACE_CLOSE_SCOPE
