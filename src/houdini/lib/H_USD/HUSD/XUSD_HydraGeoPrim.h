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
 * NAME:	XUSD_HydraGeoPrim.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for a hydra geometry prim (HdRprim)
 */
#ifndef XUSD_HydraGeoPrim_h
#define XUSD_HydraGeoPrim_h

#include <pxr/pxr.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/basisCurves.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/points.h>
#include <pxr/imaging/hd/volume.h>
#include <GT/GT_AttributeList.h>
#include <GT/GT_DataArray.h>
#include <GT/GT_TransformArray.h>
#include <GT/GT_Transform.h>
#include <GT/GT_Types.h>
#include <GEO/GEO_PackedTypes.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_Pair.h>
#include <UT/UT_Tuple.h>
#include <UT/UT_Options.h>
#include <SYS/SYS_Types.h>
#include "HUSD_HydraGeoPrim.h"

class GT_DAIndexedString;
class GT_PrimPolygonMesh;

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_HydraGeoBase;

/// Container for a hydra geometry prim (HdRprim)
class XUSD_HydraGeoPrim : public HUSD_HydraGeoPrim
{
public:
	     XUSD_HydraGeoPrim(TfToken const& type_id,
			       SdfPath const& prim_id,
			       SdfPath const& instancer_id,
			       HUSD_Scene &scene);
            ~XUSD_HydraGeoPrim() override;

    bool               isValid() const override { return myHydraPrim != nullptr; }
    bool               updateGTSelection(bool *has_selection = nullptr) override;
    void               clearGTSelection() override;

    HdRprim	      *rprim() const { return myHydraPrim; }
    const TfToken     &primType() const { return myTypeID; }

    UT_StringHolder     getTopLevelPath(HdSceneDelegate *delegate,
                                        SdfPath const& prim_id,
                                        SdfPath const& instancer_id);
    
    const UT_StringArray &materials() const override;

private:
    HdRprim	       *myHydraPrim;
    XUSD_HydraGeoBase  *myPrimBase;
    TfToken             myTypeID;
};


/// Base tie-in class for common data
class XUSD_HydraGeoBase
{
public:
    XUSD_HydraGeoBase(GT_PrimitiveHandle &prim,
		      GT_PrimitiveHandle &instance,
		      int &dirty,
		      XUSD_HydraGeoPrim &hprim);

    bool	updateGTSelection(bool *has_selection);
    void	clearGTSelection();

    const UT_StringArray &materials() const { return myMaterials; }
    
protected:
    void	resetPrim();
    void	clearDirty(HdDirtyBits *dirty_bits) const;
    bool        isDeferred(const SdfPath &id,
                           HdSceneDelegate *scene_delegate,
                           HdRenderParam *,
			   HdDirtyBits &bits) const;
    
    GEO_ViewportLOD checkVisibility(HdSceneDelegate *sceneDelegate,
				    const SdfPath   &id,
				    HdDirtyBits     *dirty_bits);
    bool	addBBoxAttrib(HdSceneDelegate* scene_delegate,
			      const SdfPath	     &id,
			      GT_AttributeListHandle &detail,
			      const GT_Primitive     *gt_prim) const;
    
    // buildTransforms() should only be called in Sync() methods
    void	buildTransforms(HdSceneDelegate *scene_delegate,
				const SdfPath  &proto_id,
				const SdfPath  &instr_id,
				HdDirtyBits    *dirty_bits);
    
    bool	updateAttrib(const TfToken	      &usd_attrib,
			     const UT_StringRef       &gt_attrib,
			     HdSceneDelegate	      *scene_delegate,
			     const SdfPath	      &id,
			     HdDirtyBits	      *dirty_bits,
			     GT_Primitive	      *gt_prim,
			     GT_AttributeListHandle   (&attrib_list)[4],
                             GT_Type                   gt_type,
			     int		      *point_freq_size=nullptr,
			     bool		       set_point_freq = false,
			     bool		      *exists = nullptr,
                             GT_DataArrayHandle        vert_index = nullptr);
    
    void	createInstance(HdSceneDelegate          *scene_delegate,
			       const SdfPath		&proto_id,
			       const SdfPath		&inst_id,
			       HdDirtyBits		*dirty_bits,
			       GT_Primitive		*geo,
			       GEO_ViewportLOD		 lod,
			       int			 mat_id,
			       bool			 instance_change);
    void        buildShaderInstanceOverrides(
                                HdSceneDelegate         *sd,
                                const SdfPath           &inst_id,
                                const SdfPath           &proto_id,
                                HdDirtyBits             *dirty_bits);
    bool        processInstancerOverrides(
                                HdSceneDelegate         *sd,
                                const SdfPath           &inst_id,
                                const SdfPath           &proto_id,
                                HdDirtyBits             *dirty_bits,
                                int                      inst_level,
                                int                     &num_instances);
    void        processNestedOverrides(int level,
                                       GT_DAIndexedString *overrides,
                                       const UT_Options *input_opt,
                                       int &index) const;
    void        assignOverride(const UT_Options *options,
                               GT_DAIndexedString *overrides,
                               int index) const;

    void	removeFromDisplay();

    XUSD_HydraGeoPrim		&myHydraPrim;
    UT_Matrix4D 		 myPrimTransform;
    GT_TransformHandle           myGTPrimTransform;
    UT_StringMap<UT_Tuple<GT_Owner,int, bool, void *> >  myAttribMap;
    UT_StringMap<UT_StringHolder> myExtraAttribs;
    UT_StringMap<UT_StringHolder> myExtraUVAttribs;
    GT_PrimitiveHandle		&myGTPrim;
    GT_PrimitiveHandle		&myInstance;
    int				&myDirtyMask;
    int64			 myInstanceId;
    GT_TransformArrayHandle	 myInstanceTransforms;
    GT_DataArrayHandle		 mySelection;
    GT_DataArrayHandle		 myMatIDArray;
    GT_DataArrayHandle		 myMaterialsArray;
    int				 myMaterialID;
    GT_DataArrayHandle		 myPickIDArray;
    GT_DataArrayHandle           myInstanceMatID;
    UT_IntArray                  myInstanceLevels;
    UT_StringArray               myLightLink;
    UT_StringArray               myShadowLink;
    UT_StringArray               myMaterials;
    
    class InstStackEntry
    {
    public:
         InstStackEntry() : nInst(0), options(nullptr) {}
        ~InstStackEntry() { clear(); }
        
        void clear()
            {
                delete options;
                options = nullptr;
                attribs = nullptr;
            }
        int nInst;
        UT_Array<UT_Options> *options;
        GT_AttributeListHandle attribs;
    };
    
    UT_Array<InstStackEntry >    myInstanceAttribStack;
    GT_DataArrayHandle           myInstanceOverridesAttrib;
    GT_AttributeListHandle       myInstanceAttribList;
    SdfPath                      myInstancerPath;
};
    

/// Container for a hydra mesh primitive
class XUSD_HydraGeoMesh : public HdMesh, public XUSD_HydraGeoBase
{
public:
	     XUSD_HydraGeoMesh(TfToken const& type_id,
			       SdfPath const& prim_id,
			       SdfPath const& instancer_id,
			       GT_PrimitiveHandle &prim,
			       GT_PrimitiveHandle &instance,
			       int &dirty,
			       XUSD_HydraGeoPrim &hprim);
    ~XUSD_HydraGeoMesh() override;

    void Sync(HdSceneDelegate *delegate,
                      HdRenderParam *rparm,
                      HdDirtyBits *dirty_bits,
                      TfToken const &representation) override;
    
    void Finalize(HdRenderParam *rparm) override;
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void	_InitRepr(TfToken const &representation,
				  HdDirtyBits *dirty_bits) override;
    bool                generatePointNormals(GT_PrimitiveHandle &mesh);
    void                consolidateMesh(HdSceneDelegate    *scene_delegate,
                                        GT_PrimPolygonMesh *mesh,
                                        SdfPath const      &id,
                                        HdDirtyBits        *dirty_bits,
                                        bool                needs_normals);


    GT_DataArrayHandle		 myCounts, myVertex;
    int64			 myTopHash;
    bool			 myIsSubD;
    bool			 myIsLeftHanded;
    int				 myRefineLevel;
};

/// Container for a hydra curves primitive
class XUSD_HydraGeoCurves : public HdBasisCurves, public XUSD_HydraGeoBase
{
public:
	     XUSD_HydraGeoCurves(TfToken const& type_id,
				 SdfPath const& prim_id,
				 SdfPath const& instancer_id,
				 GT_PrimitiveHandle &prim,
				 GT_PrimitiveHandle &instance,
				 int &dirty,
				 XUSD_HydraGeoPrim &hprim);
            ~XUSD_HydraGeoCurves() override;

    void Sync(HdSceneDelegate *delegate,
              HdRenderParam *rparm,
              HdDirtyBits *dirty_bits,
              TfToken const &representation) override;
    
    void Finalize(HdRenderParam *rparm) override;
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void	_InitRepr(TfToken const &representation,
                          HdDirtyBits *dirty_bits) override;

    GT_PrimitiveHandle   myBasisCurve;
    GT_DataArrayHandle   myCounts;
    GT_DataArrayHandle	 myIndices;
    GT_Basis		 myBasis;
    bool		 myWrap;
    
};

/// Container for a hydra volume primitive.
class XUSD_HydraGeoVolume : public HdVolume, public XUSD_HydraGeoBase
{
public:
	     XUSD_HydraGeoVolume(TfToken const& typeId,
			       SdfPath const& primId,
			       SdfPath const& instancerId,
			       GT_PrimitiveHandle &prim,
			       GT_PrimitiveHandle &instance,
			       int &dirty,
			       XUSD_HydraGeoPrim &hprim);
            ~XUSD_HydraGeoVolume() override;

    void Sync(HdSceneDelegate *delegate,
              HdRenderParam *rparm,
              HdDirtyBits *dirty_bits,
              TfToken const &representation) override;
    
    void Finalize(HdRenderParam *renderParam) override;
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void	_InitRepr(TfToken const &representation,
                          HdDirtyBits *dirty_bits) override;
};


/// Container for a hydra curves primitive
class XUSD_HydraGeoPoints : public HdPoints, public XUSD_HydraGeoBase
{
public:
	     XUSD_HydraGeoPoints(TfToken const& type_id,
				 SdfPath const& prim_id,
				 SdfPath const& instancer_id,
				 GT_PrimitiveHandle &prim,
				 GT_PrimitiveHandle &instance,
				 int &dirty,
				 XUSD_HydraGeoPrim &hprim);
            ~XUSD_HydraGeoPoints() override;

    void Sync(HdSceneDelegate *delegate,
              HdRenderParam *rparm,
              HdDirtyBits *dirty_bits,
              TfToken const &representation) override;
    
    void Finalize(HdRenderParam *rparm) override;
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void	_InitRepr(TfToken const &representation,
			  HdDirtyBits *dirty_bits) override;
   
};

/// Container for a hydra curves primitive
class XUSD_HydraGeoBounds : public HdBasisCurves, public XUSD_HydraGeoBase
{
public:
	     XUSD_HydraGeoBounds(TfToken const& type_id,
				 SdfPath const& prim_id,
				 SdfPath const& instancer_id,
				 GT_PrimitiveHandle &prim,
				 GT_PrimitiveHandle &instance,
				 int &dirty,
				 XUSD_HydraGeoPrim &hprim);
            ~XUSD_HydraGeoBounds() override;

    void Sync(HdSceneDelegate *delegate,
              HdRenderParam *rparm,
              HdDirtyBits *dirty_bits,
              TfToken const &representation) override;
    
    void Finalize(HdRenderParam *rparm) override;
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void	_InitRepr(TfToken const &representation,
			  HdDirtyBits *dirty_bits) override;

    GT_PrimitiveHandle   myBasisCurve;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
