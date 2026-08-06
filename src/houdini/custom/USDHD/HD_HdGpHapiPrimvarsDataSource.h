/*
 * Copyright 2026 Side Effects Software Inc.
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

#ifndef __HD_HdGpHapiPrimvarsDataSource__
#define __HD_HdGpHapiPrimvarsDataSource__

#include <HAPI/HAPI.h>

#include <pxr/pxr.h>
#include <pxr/base/tf/denseHashSet.h>
#include <pxr/base/tf/hash.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/primvarSchema.h>

#include <memory>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

/// An HdContainerDataSource exposing a HAPI part's attributes as Hydra
/// primvars. The container's children are the HAPI attribute names
/// (post-HD_HdGpHydraPrimvarName), and each child is a per-primvar
/// container with `primvarValue` / `interpolation` / `role` children
/// populated lazily on access.
///
/// Lifetime contract: the HAPI_Session pointer, HAPI_NodeId, and
/// HAPI_PartId passed to New() must remain valid for the lifetime of
/// the returned datasource. The datasource caches HAPI_PartInfo at
/// construction and resolves attribute info lazily on Get(name);
/// callers must not destroy/recook the underlying part out from under
/// the datasource.
///
/// Thread safety: HAPI is not safe under concurrent reads against a
/// single session. This class owns a std::mutex (held via shared_ptr
/// so file-local child datasources can share it) and serializes all
/// HAPI calls behind it. Hydra's thread-safety expectations for
/// datasources are met by that serialization. Parallelism across
/// different parts requires separate datasource instances.
class HD_HdGpHapiPrimvarsDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(HD_HdGpHapiPrimvarsDataSource);

    using TfTokenSet = TfDenseHashSet<TfToken, TfHash>;

    TfTokenVector GetNames() override;
    HdDataSourceBaseHandle Get(const TfToken &name) override;

private:
    HD_HdGpHapiPrimvarsDataSource(
        const HAPI_Session *session,
        HAPI_NodeId         node_id,
        HAPI_PartId         part_id,
        const TfToken      &point_interpolation_type =
                                HdPrimvarSchemaTokens->vertex,
        const TfTokenSet   &excluded_names = TfTokenSet());

    bool                isExcluded(const TfToken &name) const;

    const HAPI_Session *mySession;
    HAPI_NodeId         myNodeId;
    HAPI_PartId         myPartId;
    TfToken             myPointInterpolationType;
    TfTokenSet          myExcludedNames;

    // Cached at construction; never mutated afterwards.
    HAPI_PartInfo       myPartInfo;
    bool                myPartInfoValid;

    // Shared with child value datasources so HAPI access stays
    // serialized regardless of which side outlives the other.
    std::shared_ptr<std::mutex> myMutex;

    // Lazy cache populated by GetNames() under myMutex.
    bool                myNamesCached;
    TfTokenVector       myNames;
};

HD_DECLARE_DATASOURCE_HANDLES(HD_HdGpHapiPrimvarsDataSource);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
