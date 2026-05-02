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
 * NAME:	HUSD_Scene.h (HUSD Library, C++)
 *
 * COMMENTS:	Scene info for the native Houdini viewport renderer
 */
#ifndef HUSD_Scene_h
#define HUSD_Scene_h

#include "HUSD_API.h"
#include <UT/UT_Lock.h>
#include <UT/UT_LinkList.h>
#include <UT/UT_Map.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_StringSet.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_Vector2.h>
#include <SYS/SYS_Types.h>
#include <GT/GT_Primitive.h>
#include "HUSD_HydraPrim.h"
#include "HUSD_Overrides.h"
#include "HUSD_PathSet.h"
#include "HUSD_PostLayers.h"
#include "HUSD_PrimHandle.h"
#include "HUSD_Utils.h"
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE
class XUSD_ViewerDelegate;
class XUSD_HydraInstancer;
class HdRenderIndex;
class HdRenderParam;
PXR_NAMESPACE_CLOSE_SCOPE

class HUSD_HydraCamera;
class HUSD_HydraField;
class HUSD_HydraGeoPrim;
class HUSD_HydraLight;
class HUSD_HydraPrim;
class HUSD_HydraMaterial;
class HUSD_DataHandle;
class husd_ConsolidatedPrims;

typedef UT_IntrusivePtr<HUSD_HydraGeoPrim>  HUSD_HydraGeoPrimPtr;
typedef UT_IntrusivePtr<HUSD_HydraCamera>   HUSD_HydraCameraPtr;
typedef UT_IntrusivePtr<HUSD_HydraLight>    HUSD_HydraLightPtr;
typedef UT_IntrusivePtr<HUSD_HydraField>    HUSD_HydraFieldPtr;
typedef UT_IntrusivePtr<HUSD_HydraMaterial> HUSD_HydraMaterialPtr;

/// Scene information for the native viewport renderer
class HUSD_API HUSD_Scene : public UT_NonCopyable
{
public:
    class husd_ImageFilterList;

	     HUSD_Scene(bool use_vulkan);
    virtual ~HUSD_Scene();

    virtual void reset();

    const UT_Map<HUSD_Path, HUSD_HydraGeoPrimPtr> &displayGeometry() const
                            { return myDisplayGeometry; }
    const UT_Map<HUSD_Path, HUSD_HydraGeoPrimPtr> &geometry() const
                            { return myGeometry; }
    const UT_Map<HUSD_Path, HUSD_HydraCameraPtr> &cameras() const
                            { return myCameras; }
    const UT_Map<HUSD_Path, HUSD_HydraLightPtr> &lights() const
                            { return myLights; }
    const UT_Map<HUSD_Path, HUSD_HydraMaterialPtr> &materials() const
                            { return myMaterials; }
    const UT_Map<HUSD_Path, HUSD_HydraFieldPtr> &fields() const
                            { return myFields; }

    // all of these return true if the list was modified, false if the serial
    // matched;
    bool        fillGeometry(UT_Array<HUSD_HydraGeoPrimPtr> &array,
                             int64 &list_serial);
    bool        fillLights(UT_Array<HUSD_HydraLightPtr> &array,
                             int64 &list_serial);
    bool        fillCameras(UT_Array<HUSD_HydraCameraPtr> &array,
                             int64 &list_serial);

    static PXR_NS::XUSD_ViewerDelegate *newDelegate();
    static void freeDelegate(PXR_NS::XUSD_ViewerDelegate *del);

    static void pushScene(HUSD_Scene *scene); 
    static void popScene(HUSD_Scene *scene);
    static bool hasScene();

    void addGeometry(HUSD_HydraGeoPrim *geo, bool new_geo);
    void removeGeometry(HUSD_HydraGeoPrim *geo);

    void addDisplayGeometry(HUSD_HydraGeoPrim *geo);
    void removeDisplayGeometry(HUSD_HydraGeoPrim *geo);

    void addField(HUSD_HydraField *field);
    void removeField(HUSD_HydraField *field);

    virtual void addCamera(HUSD_HydraCamera *cam, bool new_cam);
    virtual void removeCamera(HUSD_HydraCamera *cam);
    bool isCamera(const UT_StringRef &path) const;

    virtual void addLight(HUSD_HydraLight *light, bool new_light);
    virtual void removeLight(HUSD_HydraLight *light);
    bool isLight(const UT_StringRef &path) const;

    virtual void addMaterial(HUSD_HydraMaterial *mat);
    virtual void removeMaterial(HUSD_HydraMaterial *mat);
    HUSD_Path lookupMaterial(int id) const;

    void addInstancer(const HUSD_Path &path,
                      PXR_NS::XUSD_HydraInstancer *instancer);
    void removeInstancer(const HUSD_Path &path);
    PXR_NS::XUSD_HydraInstancer *getInstancer(const HUSD_Path &path);

    static const UT_StringHolder &viewportRenderPrimToken();

    // Render Setting Prims don't exist in Hydra. The view places these
    // here for easier interchange between high level objects. 
    const UT_StringArray &renderPrimNames() const { return myRenderPrimNames; }
    bool         setRenderPrimNames(const UT_StringArray &names);
    const UT_StringRef &defaultRenderPrim() const { return myDefaultRenderPrim;}
    void         setDefaultRenderPrim(const UT_StringRef &path)
                                           { myDefaultRenderPrim = path;}
    const UT_StringRef &currentRenderPrim() const { return myCurrentRenderPrim;}
    void         setCurrentRenderPrim(const UT_StringRef &path)
                                           { myCurrentRenderPrim = path;}

    const UT_StringArray &renderPassNames() const { return myRenderPassNames; }
    bool         setRenderPassNames(const UT_StringArray &names);

    const UT_StringRef &renderPrimCamera() const { return myRenderPrimCamera; }
    void         setRenderPrimCamera(const UT_StringRef &camera);

    UT_Vector2I  renderPrimResolution() const { return myRenderPrimRes; }
    void         setRenderPrimResolution(UT_Vector2I res) {myRenderPrimRes=res;}

    void         setRenderPrimConform(HUSD_AspectConformPolicy p)
                 { myConformPolicy = p; }
    HUSD_AspectConformPolicy getRenderPrimConform() const
                 { return myConformPolicy; }
    void         adjustAperture(fpreal &apv, fpreal caspect, fpreal iaspect);

    const UT_StringArray &renderPrimImageFilters() const;
    const UT_Array<int>  &renderPrimCopFilterNodeIds() const;
    bool         setRenderPrimImageFilters(const UT_StringArray &filters,
                                    HUSD_TimeCode tc,
                                    const UT_StringSet &color_aovs,
                                    const UT_StringSet &non_color_aovs);

    void	 deferUpdates(bool defer) { myDeferUpdate = defer; }
    bool	 isDeferredUpdate() const { return myDeferUpdate; }

    void         consolidateMesh(const GT_PrimitiveHandle &mesh,
                                 const UT_BoundingBoxF &bbox,
                                 int prim_id,
                                 int mat_id,
                                 int dirty_bits,
                                 HUSD_HydraPrim::RenderTag tag,
                                 bool left_handed,
                                 bool auto_gen_nml,
                                 UT_Array<UT_BoundingBox> &instance_bbox,
                                 int instancer_id);
    void         removeConsolidatedPrim(int id);
    void         dirtyConsolidatedPrim(int id, int dirty_bits);

    void         setPrimCount(int64 pcount) { myPrimCount = pcount; }
    int64        getPrimCount() const       { return myPrimCount; }

    // Volumes
    const HUSD_PathSet &volumesUsingField(const HUSD_Path &field) const;
    void addVolumeUsingField(const HUSD_Path &volume,
			     const HUSD_Path &field);
    void removeVolumeUsingFields(const HUSD_Path &volume);

    bool        selectParents();
    bool        selectChildren(bool all_children); // false = first child only
    bool        selectSiblings(bool next_sibling); // false = prev sibling
    bool        recallPrevSelection();
    bool        recallNextSelection();

    bool	setSelectionPaths(const HUSD_PathSet &paths,
                        const UT_StringSet &pathswithinstanceids,
                        bool stash_selection = true);
    bool	setSelectionPaths(const UT_StringArray &paths,
                        bool stash_selection = true);
    bool	setSelectionPaths(const UT_StringSet &paths,
                        bool stash_selection = true);
    bool	clearSelection();
    bool	hasSelection() const;
    bool        isSelected(const HUSD_Path &path) const;
    bool        isSelected(const UT_StringRef &path) const;
    const UT_StringSet &getSelectionPaths() { return mySelection; }
    int64	selectionID() const { return mySelectionID; }

    // Remove any non-prim (instance) selections.
    bool        removeInstanceSelections();
    // Remove any non-instance (prim) selections.
    bool        removePrimSelections();

    static int  getMaxGeoIndex();
    
    // bumped when a geo prim is added or removed.
    int64	getGeoSerial() const    { return myGeoSerial; }
    int64	getCameraSerial() const { return myCamSerial; }
    int64	getLightSerial() const  { return myLightSerial; }
    virtual void dirtyCameraNames() {}
    virtual void dirtyLightNames() {}

    enum PrimType
    {
	INVALID_TYPE = 0,
	
	GEOMETRY,
	LIGHT,
	CAMERA,
	MATERIAL,
        FIELD,
	PATH,
	INSTANCE
    };
    PrimType	getPrimType(int id) const;
    class RenderKeyLookup
    {
    public:
        RenderKeyLookup(HUSD_Scene &scene)
            : myScene(scene),
              myInstanceIdMapLock(scene.myInstanceIdMapLock),
              myIdMapLock(scene.myIdMapLock)
        { }
        HUSD_RenderKey getRenderKey(int id, HUSD_Path &light_cam_path)
        { return myScene.getRenderKey(id, light_cam_path); }
        HUSD_HydraPrim *getPrimByID(int id)
        { return myScene.getPrimByID(id); }
    private:
        HUSD_Scene          &myScene;
        UT_AutoLock<UT_Lock> myInstanceIdMapLock;
        UT_AutoLock<UT_Lock> myIdMapLock;
    };
    UT_IntArray	getOrCreateInstanceIds(int primid, int numinst);

    void	setStage(const HUSD_DataHandle &data,
			 const HUSD_ConstOverridesPtr &overrides,
			 const HUSD_ConstPostLayersPtr &postlayers);

    PXR_NS::HdRenderIndex *renderIndex() { return myRenderIndex; }
    void setRenderIndex(PXR_NS::HdRenderIndex *ri) { myRenderIndex = ri; }
    
    PXR_NS::HdRenderParam *renderParam() { return myRenderParam; }
    void setRenderParam(PXR_NS::HdRenderParam *rp) { myRenderParam = rp; }
    
    enum LightCategory
    {
        CATEGORY_LIGHT,
        CATEGORY_SHADOW
    };
    void         addCategory(const UT_StringRef &name,   LightCategory cat);
    void         removeCategory(const UT_StringRef &name,LightCategory cat);
    bool         isCategory(const UT_StringRef &name,    LightCategory cat);

    void         pendingRemovalGeom(const HUSD_Path &path,
                                    HUSD_HydraGeoPrimPtr prim);
    HUSD_HydraGeoPrimPtr fetchPendingRemovalGeom(const HUSD_Path &path,
                                                 const UT_StringRef &prim_type);
    void         pendingRemovalCamera(const HUSD_Path &path,
                                    HUSD_HydraCameraPtr prim);
    HUSD_HydraCameraPtr fetchPendingRemovalCamera(const HUSD_Path &path);
    void         pendingRemovalLight(const HUSD_Path &path,
                                    HUSD_HydraLightPtr prim);
    HUSD_HydraLightPtr fetchPendingRemovalLight(const HUSD_Path &path);
    
    void         pendingRemovalMaterial(const HUSD_Path &path,
                                        HUSD_HydraMaterialPtr prim);
    HUSD_HydraMaterialPtr fetchPendingRemovalMaterial(const HUSD_Path &path);

    void         pendingRemovalInstancer(const HUSD_Path &path,
                                         PXR_NS::XUSD_HydraInstancer *inst);
    PXR_NS::XUSD_HydraInstancer *
                 fetchPendingRemovalInstancer(const HUSD_Path &path);

    void         postUpdate();
    void         processConsolidatedMeshes(bool finalize);

    void         setConsolidatedLimits(bool enable,
                                       int max_verts_single,
                                       int max_verts_instance,
                                       int max_instances)
                 {
                     myConsolidateMeshes = enable;
                     myConsolidatedMaxVerts = max_verts_single;
                     myConsolidatedMaxVertsInstances = max_verts_instance;
                     myConsolidatedMaxInstances = max_instances;
                 }
                     
    bool         isConsolidatingMeshes() const
                        { return myConsolidateMeshes; }
    int          getMeshMaxVertices() const
                        { return myConsolidatedMaxVerts; }
    int          getInstanceMaxVertices() const
                        { return myConsolidatedMaxVertsInstances; }
    int          getInstanceMaxCount() const
                        { return myConsolidatedMaxInstances; }

    bool         isVulkan() const { return myIsVulkan; }

    int          getDefaultMaterialID() const { return myIsVulkan ? 0 : -1; }
    
protected:
    void         addHydraPrim(HUSD_HydraPrim *prim);
    void         removeHydraPrim(HUSD_HydraPrim *prim);
    void         clearPendingRemovalPrims();

    friend class RenderKeyLookup;
    HUSD_RenderKey getRenderKey(int id, HUSD_Path &light_cam_path) const;
    HUSD_HydraPrim *getPrimByID(int id) const;

    virtual void geometryDisplayed(HUSD_HydraGeoPrim *, bool) {}
    void         stashSelection();

    UT_Map<HUSD_Path, HUSD_PathSet>		     myFieldsInVolumes;
    UT_Map<HUSD_Path, HUSD_HydraGeoPrimPtr>	     myGeometry;
    UT_Map<HUSD_Path, HUSD_HydraGeoPrimPtr>	     myDisplayGeometry;
    UT_Map<HUSD_Path, HUSD_HydraCameraPtr>	     myCameras;
    UT_Map<HUSD_Path, HUSD_HydraLightPtr>	     myLights;
    UT_Map<HUSD_Path, HUSD_HydraFieldPtr>	     myFields;
    UT_Map<HUSD_Path, PXR_NS::XUSD_HydraInstancer *> myInstancers;
    UT_Map<HUSD_Path, HUSD_HydraMaterialPtr>	     myMaterials;
    UT_Map<int, HUSD_Path>                           myMaterialIDs;
    UT_Map<HUSD_Path, HUSD_HydraGeoPrimPtr>          myPendingRemovalGeom;
    UT_Map<HUSD_Path, HUSD_HydraCameraPtr>           myPendingRemovalCamera;
    UT_Map<HUSD_Path, HUSD_HydraLightPtr>            myPendingRemovalLight;
    UT_Map<HUSD_Path, HUSD_HydraMaterialPtr>         myPendingRemovalMaterial;
    UT_Map<HUSD_Path, PXR_NS::XUSD_HydraInstancer *> myPendingRemovalInstancer;
    UT_Array<HUSD_HydraGeoPrimPtr>                   myDuplicateGeo;
    UT_Array<HUSD_HydraCameraPtr>                    myDuplicateCam;
    UT_Array<HUSD_HydraLightPtr>                     myDuplicateLight;

    UT_StringArray                      myRenderPrimNames;
    UT_StringHolder                     myRenderPrimCamera;
    UT_StringHolder                     myCurrentRenderPrim;
    UT_StringHolder                     myDefaultRenderPrim;
    UT_StringArray                      myRenderPassNames;

    UT_StringSet			mySelection;
    int64				mySelectionID;
    int64				myGeoSerial;
    int64                               myCamSerial;
    int64                               myLightSerial;
    bool				myDeferUpdate;
    UT_Vector2I                         myRenderPrimRes;
    HUSD_AspectConformPolicy            myConformPolicy;

    mutable UT_Lock			myDisplayLock;
    mutable UT_Lock                     myInstanceIdMapLock;
    mutable UT_Lock                     myIdMapLock;
    UT_Lock				myLightCamLock;
    UT_Lock				myMaterialLock;
    UT_Lock				myFieldLock;
    UT_Lock                             myCategoryLock;

    UT_StringMap<int>                   myLightLinkCategories;
    UT_StringMap<int>                   myShadowLinkCategories;

    UT_LinkList                         myStashedSelection;
    int64                               myStashedSelectionSizeB;
    UT_LinkNode                        *myCurrentRecalledSelection;
    int64                               myCurrentSelectionStashed;

    PXR_NS::HdRenderIndex	       *myRenderIndex; // TMP, hopefuly
    PXR_NS::HdRenderParam	       *myRenderParam; // TMP, hopefuly

    HUSD_DataHandle			myStage;
    HUSD_ConstOverridesPtr		myStageOverrides;
    HUSD_ConstPostLayersPtr		myStagePostLayers;

    UT_Map<HUSD_RenderKey, UT_IntArray> myRenderKeyToInstanceIdsMap;
    UT_Array<HUSD_RenderKey>            myInstanceIdToRenderKeyMap;
    UT_Map<int, HUSD_HydraPrim *>       myIdToPrimMap;

    husd_ConsolidatedPrims             *myPrimConsolidator;
    UT_UniquePtr<husd_ImageFilterList>  myImageFilterList;

    int64                               myPrimCount;
    bool                                myIsVulkan = false;
    bool myConsolidateMeshes = true;
    int myConsolidatedMaxVerts;
    int myConsolidatedMaxVertsInstances;
    int myConsolidatedMaxInstances;
};

#endif
