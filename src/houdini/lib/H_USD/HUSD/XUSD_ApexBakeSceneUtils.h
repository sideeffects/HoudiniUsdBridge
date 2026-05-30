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

#include <GA/GA_Handle.h>
#include <UT/UT_VectorTypes.h>

#include <pxr/base/gf/range3f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/span.h>
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>

class GU_Detail;
class UT_StringRef;

PXR_NAMESPACE_OPEN_SCOPE

class SdfPath;

/// Utility functions for baking APEX scene outputs back to USD or Hydra prims.
namespace XUSD_ApexBakeSceneUtils
{
/// Reads the world-space transform of a joint from skeleton geometry.
/// Returns false if e.g. the joint name was not found in the skeleton.
/// Note: the prim path parameter is only used for adding additional context to
/// any warnings that are emitted.
bool getSkelJointXform(
        const SdfPath &prim_path,
        const GU_Detail &skel_geo,
        const UT_StringRef &joint_name,
        UT_Matrix4D &joint_xform);

/// Compute updated extents from the deformed points.
/// @{
GfRange3f computeExtentFromPoints(TfSpan<const GfVec3f> positions);
GfRange3f computeExtentFromPoints(TfSpan<const GfVec3h> positions);
/// @}

/// Convert attribute values into the equivalent Gf* type.
/// The values can also optionally be transformed into the destination prim's
/// space.
template <typename UtT, typename GfT>
void convertAttribute(
        const GU_Detail &detail,
        const GA_ROHandleT<UtT> &attrib,
        const UT_Matrix4D *world_to_prim_xform,
        VtArray<GfT> &result_array);

} // namespace XUSD_ApexBakeSceneUtils

PXR_NAMESPACE_CLOSE_SCOPE
