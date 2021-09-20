/*
 * Copyright 2019 Side Effects Software Inc.
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

#ifndef __GEO_FILE_UTILS_H__
#define __GEO_FILE_UTILS_H__

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/path.h"
#include <UT/UT_Map.h>
#include <UT/UT_SharedPtr.h>

PXR_NAMESPACE_OPEN_SCOPE

class TfToken;
class VtValue;

// Controls the handling of topology attributes. They can be written to time
// samples to allow for animated topology. They can be written to the default
// attribute value for static topology authoring. Or they can be skipped
// entirely for explicit control when authoring overlay layers.
enum GEO_TopologyHandling {
    GEO_USD_TOPOLOGY_ANIMATED,
    GEO_USD_TOPOLOGY_STATIC,
    GEO_USD_TOPOLOGY_NONE
};

// Controls the handling of USD packed prims. They can be completely ignored,
// unpacked into standard Houdini geometry types, or we can just author
// xforms for them.
enum GEO_HandleUsdPackedPrims {
    GEO_USD_PACKED_IGNORE,
    GEO_USD_PACKED_XFORM
};

// Controls the handling of packed prims with instanced geometry. They can be
// imported as separate xform for each instance, or as point
// instancers.
enum GEO_HandlePackedPrims {
    GEO_PACKED_XFORMS,
    GEO_PACKED_POINTINSTANCER,
    GEO_PACKED_NATIVEINSTANCES,
    GEO_PACKED_UNPACK
};

// Controls the handling of agent prims. They can be imported with (optionally
// instanced) SkelRoot's (skinned geometry), (optionally instanced) skeletons,
// or just with animation (for efficiently overlaying time samples).
enum GEO_HandleAgents {
    GEO_AGENT_INSTANCED_SKELROOTS,
    GEO_AGENT_INSTANCED_SKELS,
    GEO_AGENT_SKELROOTS,
    GEO_AGENT_SKELS,
    GEO_AGENT_SKELANIMATIONS
};

/// Controls the handling of NURBS curves. They can be converted to BasisCurves
/// under certain restrictions, or converted to NurbsCurves prims (which have
/// limited Hydra support).
enum GEO_HandleNurbsCurves {
    GEO_NURBS_BASISCURVES,
    GEO_NURBS_NURBSCURVES
};

// Specifies how all prims other than USD packed prims should be processed.
// They are either unpacked as usual, or can author only Over prims with
// only Transforms on them.
enum GEO_HandleOtherPrims {
    GEO_OTHER_DEFINE,
    GEO_OTHER_OVERLAY,
    GEO_OTHER_XFORM
};

void
GEOconvertTokenToEnum(const TfToken &str_value, GEO_HandleOtherPrims &value);

#define GEO_HANDLE_OTHER_PRIMS_TOKENS  \
    ((define,   "define")) \
    ((overlay,  "overlay")) \
    ((xform,    "xform"))

TF_DECLARE_PUBLIC_TOKENS(GEO_HandleOtherPrimsTokens,
                         GEO_HANDLE_OTHER_PRIMS_TOKENS);

// Determines how the GEO_KindGuide value of each prim gets mapped to a
// specific KindToken. This lets the mapping of kind "guidance" to a specific
// kind token to all happen in a single function.
enum GEO_KindSchema {
    GEO_KINDSCHEMA_NONE,
    GEO_KINDSCHEMA_COMPONENT,
    GEO_KINDSCHEMA_NESTED_GROUP,
    GEO_KINDSCHEMA_NESTED_ASSEMBLY
};

// Guides the selection of a prim's Kind based on the GEO_KindSchema we have
// been asked to apply.
enum GEO_KindGuide {
    GEO_KINDGUIDE_TOP,
    GEO_KINDGUIDE_BRANCH,
    GEO_KINDGUIDE_LEAF
};

typedef UT_Map<TfToken, VtValue> GEO_FileMetadata;

using GEO_PathHandle = UT_SharedPtr<SdfPath>;

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_FILE_UTILS_H__
