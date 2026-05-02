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

#ifndef __XUSD_Skeleton_h__
#define __XUSD_Skeleton_h__

#include <GU/GU_DetailHandle.h>
#include <UT/UT_VectorTypes.h>

#include <pxr/pxr.h>
#include <pxr/base/vt/types.h>

class GU_Detail;
class UT_StringArray;
class UT_StringHolder;

template <typename T>
class UT_Array;

PXR_NAMESPACE_OPEN_SCOPE

struct GusdSkinImportParms;
class SdfPath;
class UsdSkelSkinningQuery;

/// Convert a skinned primitive to geometry in the KineFX format, including
/// skinning weights, blendshapes, etc.
GU_DetailHandle
XUSDimportSkinnedPrim(
        const GusdSkinImportParms &parms,
        const UsdSkelSkinningQuery &skinning_query,
        const VtTokenArray &joint_names,
        const VtMatrix4dArray &inv_bind_transforms,
        const SdfPath &root_path,
        const UT_StringHolder &shape_attrib);

/// Import blendshapes for USD Skin Import.
bool
XUSDimportBlendShapes(
        GU_Detail &detail,
        const UsdSkelSkinningQuery &skinning_query,
        const SdfPath &root_path,
        const UT_Matrix4D &geom_bind_xform);

/// Import the geometry for all blendshape inputs (including in-between
/// shapes), and record the necessary detail attributes on the base shape for
/// the agent blendshape deformer.
bool
XUSDimportAgentBlendShapes(
        GU_Detail &base_shape,
        UT_Array<GU_DetailHandle> &all_shape_details,
        UT_StringArray &all_shape_names,
        const UsdSkelSkinningQuery &skinning_query,
        const SdfPath &root_path,
        const UT_Matrix4D &geom_bind_xform);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
