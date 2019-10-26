/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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
    HUSD_API bool evalMaterialAttrib(T &val,
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

    HUSD_API void getMaterialParms(UT_StringArray  &parms,
				   HdSceneDelegate *scene_del,
				   const SdfPath   &prim_path);
    
    HUSD_API GT_TransformArrayHandle createTransformArray(
				   const VtMatrix4dArray &insts);
    
    template <typename A_TYPE>
    GT_DataArrayHandle createGTArray(const A_TYPE &usd,
				     GT_Type tinfo=GT_TYPE_NONE,
				     int64 data_id = -1);
    HUSD_API GT_DataArrayHandle attribGT(const VtValue &value,
					 GT_Type tinfo=GT_TYPE_NONE,
					 int64 data_id=-1);

    HUSD_API int64	newDataId();

    HUSD_API void processSubdivTags(const PxOsdSubdivTags &subdivTags,
			   UT_Array<GT_PrimSubdivisionMesh::Tag> &subd_tags);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
