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
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraUtils.h (HUSD Library, C++)
 *
 * COMMENTS:	Utility functions for Hydra delegates, as Hydra classes derive
 *		from Pixar Hd* classes so adding common methods between
 *		different hydra prim types is tricky.
 */
#ifndef XUSD_HydraUtils_h
#define XUSD_HydraUtils_h

#include "HUSD_API.h"

#include <UT/UT_Array.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Options.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_Tuple.h>
#include <GT/GT_DANumeric.h>
#include <GT/GT_Types.h>
#include <GT/GT_TransformArray.h>
#include <GT/GT_Transform.h>
#include <GT/GT_PrimSubdivisionMesh.h>

#include <pxr/pxr.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/imaging/pxOsd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;
class SdfPath;
class TfToken;
class PxOsdSubdivTags;

namespace XUSD_HydraUtils
{
    HUSD_API void buildAttribMap(
	HdSceneDelegate *scene_del,
	SdfPath const   &path,
	UT_StringMap<UT_Tuple<GT_Owner,int,bool,void*> >&map,
	const UT_Map<GT_Owner,GT_Owner>*remap=nullptr);

    HUSD_API UT_Matrix4D fullTransform(HdSceneDelegate *scene_del,
				       const SdfPath   &prim_path);

    template<typename T>
    HUSD_API bool eval(VtValue &val, T &ret_val);


    template<typename T>
    HUSD_API bool evalAttrib(T &val,
			     HdSceneDelegate *scene_del,
			     const SdfPath   &prim_path,
			     const TfToken   &attrib_name);

    template<typename T>
    HUSD_API bool evalCameraAttrib(T &val,
			     HdSceneDelegate *scene_del,
			     const SdfPath   &prim_path,
			     const TfToken   &attrib_name);

    template<typename T>
    HUSD_API bool evalLightAttrib(T &val,
				  HdSceneDelegate *scene_del,
				  const SdfPath   &prim_path,
				  const TfToken   &attrib_name);

    HUSD_API GT_TransformArrayHandle createTransformArray(
				   const VtMatrix4dArray &insts);

    template <typename A_TYPE>
    GT_DataArrayHandle createGTArray(const A_TYPE &usd,
				     GT_Type tinfo=GT_TYPE_NONE,
				     int64 data_id = -1);
    HUSD_API GT_DataArrayHandle attribGT(const VtValue &value,
					 GT_Type tinfo=GT_TYPE_NONE,
					 int64 data_id=-1);
    HUSD_API bool addToOptions(UT_Options &options,
                               const VtValue &value,
                               const UT_StringRef &name);
    
    HUSD_API int64	newDataId();

    HUSD_API void processSubdivTags(const PxOsdSubdivTags &subdivTags,
                        UT_Array<GT_PrimSubdivisionMesh::Tag> &subd_tags);

    HUSD_API void processSubdivTags(const PxOsdSubdivTags &subdivTags,
                        const VtIntArray &hole_indices,
                        UT_Array<GT_PrimSubdivisionMesh::Tag> &subd_tags);

    HUSD_API void processSubdivTags(
                        UT_Array<GT_PrimSubdivisionMesh::Tag> &subd_tags,

                        const VtIntArray &crease_indices,
                        const VtIntArray &crease_lengths,
                        const VtFloatArray &crease_weights,

                        const VtIntArray &corner_indices,
                        const VtFloatArray &corner_weights,

                        const VtIntArray &hole_indices,

                        const TfToken &vtx_InterpolationRule,
                        const TfToken &fvar_InterpolationRule
                    );

};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
