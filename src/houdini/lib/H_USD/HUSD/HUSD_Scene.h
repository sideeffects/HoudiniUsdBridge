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

#include <pxr/pxr.h>

#include "HUSD_API.h"
#include <UT/UT_Lock.h>
#include <UT/UT_LinkList.h>
#include <UT/UT_Map.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_Pair.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_StringSet.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_Vector2.h>
#include <SYS/SYS_Types.h>
#include "HUSD_PrimHandle.h"
#include "HUSD_Overrides.h"

PXR_NAMESPACE_OPEN_SCOPE
class XUSD_ViewerDelegate;
class HdRenderIndex;
class HdRenderParam;
PXR_NAMESPACE_CLOSE_SCOPE

class HUSD_HydraCamera;
class HUSD_HydraGeoPrim;
class HUSD_HydraLight;
class HUSD_HydraPrim;
class HUSD_HydraMaterial;
class HUSD_DataHandle;

typedef UT_IntrusivePtr<HUSD_HydraGeoPrim>  HUSD_HydraGeoPrimPtr;
typedef UT_IntrusivePtr<HUSD_HydraCamera>   HUSD_HydraCameraPtr;
typedef UT_IntrusivePtr<HUSD_HydraLight>    HUSD_HydraLightPtr;
typedef UT_IntrusivePtr<HUSD_HydraMaterial> HUSD_HydraMaterialPtr;

/// Scene information for the native viewport renderer
class HUSD_API HUSD_Scene : public UT_NonCopyable
{
public:
	     HUSD_Scene();
    virtual ~HUSD_Scene();

    UT_StringMap<HUSD_HydraGeoPrimPtr>  &geometry() { return myDisplayGeometry; }
    UT_StringMap<HUSD_HydraCameraPtr>   &cameras()  { return myCameras; }
    UT_StringMap<HUSD_HydraLightPtr>    &lights()   { return myLights; }
    UT_StringMap<HUSD_HydraMaterialPtr> &materials(){ return myMaterials; }

    // all of these return true if the list was modified, false if the serial
    // matched;
    bool        fillGeometry(UT_Array<HUSD_HydraGeoPrimPtr> &array,
                             int64 &list_serial);
    bool        fillLights(UT_Array<HUSD_HydraLightPtr> &array,
                             int64 &list_serial);
    bool        fillCameras(UT_Array<HUSD_HydraCameraPtr> &array,
                             int64 &list_serial);

    const UT_StringRef &lookupPath(int id) const;
    int                 lookupGeomId(const UT_StringRef &path);

    static PXR_NS::XUSD_ViewerDelegate *newDelegate();
    static void freeDelegate(PXR_NS::XUSD_ViewerDelegate *del);

    static void pushScene(HUSD_Scene *scene); 
    static void popScene(HUSD_Scene *scene);
    static bool hasScene();

    void addGeometry(HUSD_HydraGeoPrim *geo, bool new_geo);
    void removeGeometry(HUSD_HydraGeoPrim *geo);

    void addDisplayGeometry(HUSD_HydraGeoPrim *geo);
    void removeDisplayGeometry(HUSD_HydraGeoPrim *geo);

    virtual void addCamera(HUSD_HydraCamera *cam, bool new_cam);
    virtual void removeCamera(HUSD_HydraCamera *cam);

    virtual void addLight(HUSD_HydraLight *light, bool new_light);
    virtual void removeLight(HUSD_HydraLight *light);

    virtual void addMaterial(HUSD_HydraMaterial *mat);
    virtual void removeMaterial(HUSD_HydraMaterial *mat);

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
    const UT_StringRef &renderPrimCamera() const { return myRenderPrimCamera; }
    void         setRenderPrimCamera(const UT_StringRef &camera);

    UT_Vector2I  renderPrimResolution() const { return myRenderPrimRes; }
    void         setRenderPrimResolution(UT_Vector2I res) {myRenderPrimRes=res;}

    void	 deferUpdates(bool defer) { myDeferUpdate = defer; }
    bool	 isDeferredUpdate() const { return myDeferUpdate; }
    
    // Volumes
    const UT_StringSet &volumesUsingField(const UT_StringRef &field) const;
    void addVolumeUsingField(const UT_StringHolder &volume,
			     const UT_StringHolder &field);
    void removeVolumeUsingFields(const UT_StringRef &volume);

    // Selections. A highlight is a temporary selection which can be turned into
    // a selection in various ways.
    void	addToHighlight(int id);
    void	addPathToHighlight(const UT_StringHolder &path);
    void	addInstanceToHighlight(int id);
    void	clearHighlight();

    void	setHighlightAsSelection();
    void	addHighlightToSelection();
    void	removeHighlightFromSelection();
    void	toggleHighlightInSelection();
    void	intersectHighlightWithSelection();
    bool	clearSelection();

    bool        selectParents();
    bool        selectChildren(bool all_children); // false = first child only
    bool        selectSiblings(bool next_sibling); // false = prev sibling
    bool        recallPrevSelection();
    bool        recallNextSelection();
    void        clearStashedSelections();
    
    void	setSelection(const UT_StringArray &paths,
                             bool stash_selection = true);
    const UT_StringArray &getSelectionList();
    void        redoSelectionList();

    // Convert a pattern to a selection.
    void	convertSelection(const char *selection_pattern,
				 UT_StringArray &paths);
    
    // Remove any non-prim (instance) selections.
    bool        removeInstanceSelections();
    // Remove any non-instance (prim) selections.
    bool        removePrimSelections();

    bool	hasSelection() const;
    bool	hasHighlight() const;
    bool	isSelected(int id) const;
    bool	isSelected(const HUSD_HydraPrim *prim) const;
    bool	isHighlighted(int id) const;
    bool	isHighlighted(const HUSD_HydraPrim *prim) const;

    int64	highlightID() const { return myHighlightID; }
    int64	selectionID() const { return mySelectionID; }

    int64	getMaterialID(const UT_StringRef &path);

    static int  getMaxGeoIndex();
    
    // bumped when a geo prim is added or removed.
    int64	getGeoSerial() const    { return myGeoSerial; }
    int64	getCameraSerial() const { return myCamSerial; }
    int64	getLightSerial() const  { return myLightSerial; }
    
    // bumped when any prim has Sync() called.
    int64       getModSerial() const { return myModSerial; }
    void        bumpModSerial() { myModSerial++; }

    enum PrimType
    {
	INVALID_TYPE = 0,
	
	GEOMETRY,
	LIGHT,
	CAMERA,
	MATERIAL,
	PATH,
	INSTANCE
    };
    PrimType	getPrimType(int id) const;

    int		getOrCreateID(const UT_StringRef &path,
                              PrimType type = GEOMETRY);
    
    void	setStage(const HUSD_DataHandle &data,
			 const HUSD_ConstOverridesPtr &overrides);

    PXR_NS::HdRenderIndex *renderIndex() { return myRenderIndex; }
    void setRenderIndex(PXR_NS::HdRenderIndex *ri) { myRenderIndex = ri; }
    
    PXR_NS::HdRenderParam *renderParam() { return myRenderParam; }
    void setRenderParam(PXR_NS::HdRenderParam *rp) { myRenderParam = rp; }
    
    // Debugging only... Do not use in production code.
    HUSD_PrimHandle getPrim(const UT_StringHolder &path) const;

    enum LightCategory
    {
        CATEGORY_LIGHT,
        CATEGORY_SHADOW
    };
    void         addCategory(const UT_StringRef &name,   LightCategory cat);
    void         removeCategory(const UT_StringRef &name,LightCategory cat);
    bool         isCategory(const UT_StringRef &name,    LightCategory cat);

    void         pendingRemovalGeom(const UT_StringRef &path,
                                    HUSD_HydraGeoPrimPtr prim);
    HUSD_HydraGeoPrimPtr fetchPendingRemovalGeom(const UT_StringRef &path);
    void         pendingRemovalCamera(const UT_StringRef &path,
                                    HUSD_HydraCameraPtr prim);
    HUSD_HydraCameraPtr fetchPendingRemovalCamera(const UT_StringRef &path);
    void         pendingRemovalLight(const UT_StringRef &path,
                                    HUSD_HydraLightPtr prim);
    HUSD_HydraLightPtr fetchPendingRemovalLight(const UT_StringRef &path);
    
    void         clearPendingRemovalPrims();
protected:
    virtual void geometryDisplayed(HUSD_HydraGeoPrim *, bool) {}
    void	 selectionModified(int id);

    void         stashSelection();
    bool         makeSelection(const UT_Map<int,int> &selection,
                               bool validate);

    int          getIDForPrim(const UT_StringRef &path,
                              PrimType &return_prim_type,
                              bool create_path_id = false);
  
    UT_Map<int, UT_Pair<UT_StringHolder, PrimType> >	myNameIDLookup;
    UT_StringMap<int>			myPathIDs;
    UT_StringMap<UT_StringSet>		myFieldsInVolumes;
    UT_StringMap<HUSD_HydraGeoPrimPtr>	myGeometry;
    UT_StringMap<HUSD_HydraGeoPrimPtr>	myDisplayGeometry;
    UT_StringMap<HUSD_HydraCameraPtr>	myCameras;
    UT_StringMap<HUSD_HydraLightPtr>	myLights;
    UT_StringMap<HUSD_HydraMaterialPtr>	myMaterials;
    UT_StringMap<HUSD_HydraGeoPrimPtr>  myPendingRemovalGeom;
    UT_StringMap<HUSD_HydraCameraPtr>   myPendingRemovalCamera;
    UT_StringMap<HUSD_HydraLightPtr>   myPendingRemovalLight;
    UT_StringArray                      myRenderPrimNames;
    UT_StringHolder                     myRenderPrimCamera;
    UT_StringHolder                     myCurrentRenderPrim;
    UT_StringHolder                     myDefaultRenderPrim;

    UT_Map<int,int>			myHighlight;
    UT_Map<int,int>			mySelection;
    UT_StringMap<int64>			myMatIDs;
    UT_StringArray			mySelectionArray;
    int64				mySelectionArrayID;
    bool                                mySelectionArrayNeedsUpdate;
    int64				myHighlightID;
    int64				mySelectionID;
    int64				myGeoSerial;
    int64                               myModSerial;
    int64                               myCamSerial;
    int64                               myLightSerial;
    int64                               mySelectionResolveSerial;
    bool				myDeferUpdate;
    UT_Vector2I                         myRenderPrimRes;

    UT_Lock				myDisplayLock;
    UT_Lock				myLightCamLock;
    UT_Lock				myMaterialLock;
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
};

#endif
