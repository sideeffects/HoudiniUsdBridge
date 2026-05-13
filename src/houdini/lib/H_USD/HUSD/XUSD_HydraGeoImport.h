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

#ifndef XUSD_HydraGeoImport_h
#define XUSD_HydraGeoImport_h

#include "HUSD_API.h"

#include <GU/GU_DetailHandle.h>

#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Options for how Hydra prims are translated to geometry.
struct HUSD_API XUSD_HydraGeoImportOptions
{
    /// Whether to transform the prim's geometry to world space.
    bool myApplyPrimXform = true;
};

/// Converts a Hydra geometry prim (e.g. of type `mesh`) to a GU_Detail.
HUSD_API GU_DetailHandle
XUSDimportGeoFromHydraPrim(
        const HdSceneIndexPrim &prim,
        const SdfPath &prim_path,
        const XUSD_HydraGeoImportOptions &options);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
