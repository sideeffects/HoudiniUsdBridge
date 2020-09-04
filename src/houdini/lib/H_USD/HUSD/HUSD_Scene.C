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
 * NAME:	HUSD_SceneGraphDelegate.C (HUSD Library, C++)
 *
 * COMMENTS:	Scene info for the native Houdini viewport renderer
 */
#include "HUSD_Scene.h"

#include "HUSD_Info.h"
#include "HUSD_HydraGeoPrim.h"
#include "HUSD_HydraCamera.h"
#include "HUSD_HydraLight.h"
#include "HUSD_HydraMaterial.h"
#include "HUSD_Path.h"
#include "XUSD_HydraCamera.h"
#include "XUSD_HydraGeoPrim.h"
#include "XUSD_HydraInstancer.h"
#include "XUSD_HydraMaterial.h"
#include "XUSD_RenderSettings.h"
#include "XUSD_ViewerDelegate.h"

#include "HUSD_DataHandle.h"
#include "HUSD_PrimHandle.h"

#include <GT/GT_AttributeList.h>
#include <GT/GT_CatPolygonMesh.h>
#include <GT/GT_DAConstantValue.h>
#include <GT/GT_Names.h>
#include <GT/GT_Primitive.h>
#include <GT/GT_PrimInstance.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_Transform.h>
#include <GT/GT_TransformArray.h>
#include <GT/GT_Util.h>

#include <pxr/usd/sdf/path.h>
#include <pxr/imaging/hd/camera.h>

#include <UT/UT_Assert.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Lock.h>
#include <UT/UT_String.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_WorkBuffer.h>
#include <iostream>
#include <UT/UT_StackTrace.h>

using namespace UT::Literal;
PXR_NAMESPACE_USING_DIRECTIVE

// 10MB
#define STASHED_SELECTION_MEM_LIMIT exint(10*1024*1024)

static HUSD_Scene *theCurrentScene = nullptr;
static int theGeoIndex = 0;
static UT_IntArray theFreeGeoIndex;

static constexpr UT_StringLit theViewportPrimTokenL("__viewport_settings__");
static constexpr UT_StringLit theQuestionMark("?");
static UT_StringHolder theViewportPrimToken(theViewportPrimTokenL.asHolder());

const UT_StringHolder &
HUSD_Scene::viewportRenderPrimToken()
{
    return theViewportPrimToken;
}

// -------------------------------------------------------------------------
// Helper class to combine many small meshes.

static SYS_AtomicInt32 theUniqueConPrimIndex(0);

class husd_ConsolidatedGeoPrim : public HUSD_HydraGeoPrim
{
public:
    husd_ConsolidatedGeoPrim(HUSD_Scene &scene,
                             GT_PrimitiveHandle mesh,
                             int mat_id,
                             const char *name,
                             const GT_DataArrayHandle &sel,
                             const UT_BoundingBox &bbox)
        : HUSD_HydraGeoPrim(scene, name, true), // consolidated
          mySelection(sel),
          myBBoxList(nullptr)
        {
            myTransform = new GT_TransformArray();
            mySelectionDataId = 0;
            myMinPrimID = 0;
            myMaxPrimID = 0;
            myInstancerPrimID = -1;
            
            auto mat = new GT_DAConstantValue<int>(1, mat_id, 1);
            auto glcon = new GT_DAConstantValue<int>(1, 1, 1);
            
            myInstDetail = GT_AttributeList::createAttributeList(
                GT_Names::consolidated_mesh, glcon,
                "MatID", mat);

            setMesh(mesh, bbox);
        }

    void setMesh(GT_PrimitiveHandle mesh, const UT_BoundingBox &bbox)
        {
            myGTPrim = mesh;
            myBBox = bbox; 
            GT_Util::addBBoxAttrib(bbox, myInstDetail);
            myInstance = new GT_PrimInstance(mesh, myTransform,
                                             GT_GEOOffsetList(),
                                             GT_AttributeListHandle(),
                                             myInstDetail);
            myGTPrim->setPrimitiveTransform(
                new GT_Transform(&UT_Matrix4F::getIdentityMatrix(),1));
        }
    void setValid(bool valid) { myValidFlag = valid; }
    void setMaterial(const UT_StringRef &path)
        {
            myMaterial.forcedRef(0)= path;
        }

    // For consolidated instances; the point instancer
    void setInstancerPrimID(int id) { myInstancerPrimID = id; }
    
    void setPrimIDs(UT_IntArray &ids)
        {
            myPrimIDs = std::move(ids);
            if(myPrimIDs.entries() == 0)
            {
                myMinPrimID = 0;
                myMaxPrimID = 0;
            }
            else
            {
                myMinPrimID = myPrimIDs(0);
                myMaxPrimID = myPrimIDs(0);
                for(int i=1; i<myPrimIDs.entries(); i++)
                {
                    myMinPrimID = SYSmin(myPrimIDs(i), myMinPrimID);
                    myMaxPrimID = SYSmax(myPrimIDs(i), myMaxPrimID);
                }
            }
        }
    void setBBoxList(const UT_Array<UT_BoundingBoxF> *list)
        {
            myBBoxList = list;
        }
    
    const UT_StringArray &materials() const override { return myMaterial; }
    bool isValid() const override { return myValidFlag; }
    bool selectionDirty() const override
        {
            //UTdebugPrint("check selection dirty");
            if(myInstancerPrimID == -1)
            {
                for(int id : myPrimIDs)
                {
                    auto prim = scene().findConsolidatedPrim(id);
                    if(prim && prim->selectionDirty())
                    {
                        //UTdebugPrint("Consolidated Selection dirty", id);
                        return true;
                    }
                }
            }
            else
            {
                auto prim = scene().findConsolidatedPrim(myInstancerPrimID);
                if(prim && prim->selectionDirty())
                {
                    // UTdebugPrint("Consolidated Selection dirty INSTR",
                    //               myInstancerPrimID);
                    return true;
                }
            }
            return false;
        }

    bool updateGTSelection(bool *has_selection) override
        {
            if(!myPrimIDs.entries())
            {
                if(has_selection)
                    *has_selection = false;
                return false;
            }

            //UTdebugPrint("Update Con", myPrimIDs.entries());
            bool has_sel = false;
            bool changed = false;

            if(myInstancerPrimID == -1)
            {
                for(int id : myPrimIDs)
                {
                    auto prim = scene().findConsolidatedPrim(id);
                    if(prim)
                    {
                        bool sel = false;
                        changed |= prim->updateGTSelection(&sel);
                        //UTdebugPrint("ID", id, " sel", sel);
                        if(sel)
                        {
                            //UTdebugPrint("  --- ID selected", id);
                            has_sel = true;
                            changed = true;
                        }
                    }
                }
            }
            else // Consolidated Instancer
            {
                auto prim = scene().findConsolidatedPrim(myInstancerPrimID);
                if(prim)
                {
                    bool sel = false;
                    changed |= prim->updateGTSelection(&sel);
                    if(sel)
                    {
                        has_sel = true;
                        changed = true;
                    }
                }
            }

            //UTdebugPrint("changed cons", changed, has_sel);
            if(changed)
                mySelectionDataId++;

            auto selda =
                UTverify_cast<GT_DAConstantValue<int64> *>(mySelection.get());

            int64 seldata[] = { int64(changed?1:0), mySelectionDataId };
            selda->set(seldata, 2);
            
            if(changed)
            {
                if(myInstancerPrimID != -1)
                {
                    // Flag the instancer as selected.
                    scene().selectConsolidatedPrim(myInstancerPrimID);
                }
                else
                {
                    // only need to do this on one of the prims to update the
                    // whole mesh.
                    scene().selectConsolidatedPrim(myPrimIDs(0));
                }
            }
            if(has_selection)
                *has_selection = has_sel;

            // UTdebugPrint("Update selection: changed", changed,
            //              "selections", has_sel, mySelectionDataId);
            return changed;
        }

    bool getSelectedBBox(UT_BoundingBox &bbox) const override
        {
            bool found = false;
            bbox.makeInvalid();

            if(myInstancerPrimID != -1)
            {
                for(int i=0; i<myPrimIDs.entries(); i++)
                    if(scene().isSelected(myPrimIDs(i)))
                    {
                        bbox.enlargeBounds((*myBBoxList)(i));
                        found = true;
                    }
            }
            else
            {
                for(int i=0; i<myPrimIDs.entries(); i++)
                    if(scene().isSelected(myPrimIDs(i)))
                    {
                        bbox.enlargeBounds((*myBBoxList)(i));
                        found = true;
                    }
            }
            return found;
        }
    
    void getPrimIDRange(int &mn, int &mx) const override
        {
            mn = myMinPrimID;
            mx = myMaxPrimID;
        }
    bool        getBounds(UT_BoundingBox &box) const override
        { box = myBBox; return true; }
  
private:
    GT_TransformArrayHandle myTransform;
    GT_AttributeListHandle  myInstDetail;
    GT_DataArrayHandle      mySelection;
    UT_StringArray          myMaterial;
    UT_BoundingBox          myBBox;
    int64                   mySelectionDataId;
    int                     myInstancerPrimID;
    int                     myMinPrimID;
    int                     myMaxPrimID;
    bool                    myValidFlag = false;
    const UT_Array<UT_BoundingBoxF> *myBBoxList;
};

#define MAX_GROUP_FACES    50000
#define MIN_COMPLETE_THRESHOLD 49000

class husd_ConsolidatedPrims
{
public:
     husd_ConsolidatedPrims(HUSD_Scene &scene) : myScene(scene) {}
    ~husd_ConsolidatedPrims() {}

    void add(const GT_PrimitiveHandle &mesh,
             const UT_BoundingBoxF &bbox,
             int prim_id,
             int mat_id,
             int dirty_bits,
             HUSD_HydraPrim::RenderTag tag,
             bool lefthand,
             bool auto_nml,
             UT_Array<UT_BoundingBox> &instance_bbox,
             int instancer_id);
    void remove(int prim_id);
    void selectChange(HUSD_Scene &scene, int prim_id);

    void processBuckets(bool finalize);

    // Holds prims of similar material and render tag.
    class RenderTagBucket
    {
    public:
        // Holding bucket until batch processing once all Syncs are done.
        class NewPrim
        {
        public:
            NewPrim(const GT_PrimitiveHandle &prim,
                    int prim_id,
                    const UT_BoundingBoxF &bbox,
                    UT_Array<UT_BoundingBoxF> &ibbox)
                : myPrim(prim), myPrimID(prim_id), myBBox(bbox),
                  myInstBBox(std::move(ibbox))
                {}

            GT_PrimitiveHandle myPrim;
            int                myPrimID;
            UT_BoundingBoxF    myBBox;
            UT_Array<UT_BoundingBoxF> myInstBBox;
        };

        class PrimGroup
        {
        public:
            PrimGroup() : myPolyMerger(true, MAX_GROUP_FACES) {}

            void  selectChange(int prim_id);
            void  process(HUSD_Scene &scene,
                          int mat_id,
                          HUSD_HydraPrim::RenderTag tag,
                          bool lefthanded, bool auto_nml);
            void invalidate();

            UT_Array<UT_BoundingBoxF>    myBBox;
            UT_Array<UT_Array<UT_BoundingBoxF>>    myInstanceBBox;
            UT_Array<UT_BoundingBoxF>    myIBBoxList;
            UT_Map<int,int>              myPrimIDs;
            UT_IntArray                  myEmptySlots;
            HUSD_HydraGeoPrimPtr         myPrimGroup;
            GT_CatPolygonMesh            myPolyMerger;
            GT_DataArrayHandle           mySelectionInfo;
            int64                        myTopology = 1;
            int                          myDirtyBits = 0xFFFFFFFF;
            bool                         myDirtyFlag = true;
            bool                         myActiveFlag = false;
            bool                         myComplete = false;
        };

        void addPrim(const GT_PrimitiveHandle &mesh, int prim_id,
                     const UT_BoundingBoxF &bbox, int dirty_bits,
                     UT_Array<UT_BoundingBoxF> &instance_bbox)
            {
                //UTdebugPrint("Add prim", prim_id);
                auto entry = myIDGroupMap.find(prim_id);
                if(entry == myIDGroupMap.end())
                {
                    myNewPrims.append( { mesh, prim_id, bbox, instance_bbox } );
                    // Dirty bits are ignored; adding a prim to a group
                    // completely invalidates it.
                }
                else
                {
                    auto &&grp = myPrimGroups(entry->second);
                    auto idx = grp.myPrimIDs.find(prim_id);
                    if(idx != grp.myPrimIDs.end())
                    {
                        const int index = idx->second;
                        if(grp.myPolyMerger.replace(index, mesh))
                        {
                            grp.myBBox(index) = bbox;
                            grp.myInstanceBBox(index)=std::move(instance_bbox);
                            grp.myDirtyBits |= dirty_bits;
                        }
                        else
                        {
                            // no longer matches.
                            grp.myPolyMerger.clearMesh(idx->second);
                            grp.myDirtyBits = 0xFFFFFFFF;
                            myNewPrims.append({mesh,prim_id,bbox,instance_bbox});
                        }
                        grp.invalidate();
                    }
                    else
                    {
                        myNewPrims.append( {mesh, prim_id, bbox,instance_bbox} );
                    }
                }
                myDirtyFlag = true;
            }

        bool removePrim(int prim_id)
            {
                //UTdebugPrint("Remove prim", prim_id);
                auto entry = myIDGroupMap.find(prim_id);
                if(entry != myIDGroupMap.end())
                {
                    auto &&grp = myPrimGroups(entry->second);
                    auto idx = grp.myPrimIDs.find(prim_id);
                    if(idx != grp.myPrimIDs.end())
                    {
                        const int index = idx->second;
                        grp.myPrimIDs.erase(prim_id);
                        grp.myEmptySlots.append(index);
                        grp.myPolyMerger.clearMesh(index);
                        grp.invalidate();
                        grp.myDirtyBits = 0xFFFFFFFF;
                        //UTdebugPrint("Remove");
                        grp.myDirtyFlag = true;
                        myDirtyFlag = true;
                        myIDGroupMap.erase(prim_id);
                        return true;
                    }
                }
                return false;
           }
        bool selectChange(HUSD_Scene &scene, int prim_id)
            {
                auto entry = myIDGroupMap.find(prim_id);
                if(entry != myIDGroupMap.end())
                {
                    auto &&grp = myPrimGroups(entry->second);
                    auto idx = grp.myPrimIDs.find(prim_id);
                    if(idx != grp.myPrimIDs.end())
                    {
                        //UTdebugPrint("  ---- Dirty", prim_id);
                        grp.selectChange(prim_id);
                        myDirtyFlag = true;
                        return true;
                    }
                }
                return false;
            }

        void process(HUSD_Scene &scene, bool finalize);

        void setBucketParms(int mat_id,
                            HUSD_HydraPrim::RenderTag tag,
                            bool lefthand,
                            bool auto_nml)
            {
                myMatID = mat_id;
                myRenderTag = tag;
                myLeftHanded = lefthand;
                myAutoNormal = auto_nml;
            }

        UT_Array<NewPrim> myNewPrims;
        UT_Array<PrimGroup> myPrimGroups;
        UT_Map<int,int>   myIDGroupMap;
        bool              myDirtyFlag = true;
        HUSD_HydraPrim::RenderTag  myRenderTag = HUSD_HydraPrim::TagDefault;
        int                        myMatID = -1;
        bool                       myLeftHanded = false;
        bool                       myAutoNormal = false;
    };

private:
    UT_Map<uint64, RenderTagBucket > myBuckets;
    UT_Map<int, uint64> myPrimBucketMap;
    HUSD_Scene         &myScene;
    bool                myDirtyFlag = false;
    UT_Lock             myLock;
};


void
husd_ConsolidatedPrims::add(const GT_PrimitiveHandle &mesh,
                            const UT_BoundingBoxF &bbox,
                            int prim_id,
                            int mat_id,
                            int dirty_bits,
                            HUSD_HydraPrim::RenderTag tag,
                            bool left_hand,
                            bool auto_nml,
                            UT_Array<UT_BoundingBox> &instance_bbox,
                            int instancer_id)
{
    uint32 umat = uint32(mat_id);
    uint32 utag = uint32(tag) // 0..3b
                | uint32(left_hand ? 0x10:0)
                | uint32(auto_nml ? 0x20:0)
                | uint32(uint32(instancer_id) <<6);
    uint64 bucket = (uint64(utag)<<32U) | uint64(umat);

    UT_AutoLock locker(myLock);
    myDirtyFlag = true;
    
    auto entry = myPrimBucketMap.find(bucket);
    if(entry == myPrimBucketMap.end())
    {
        myBuckets[bucket].setBucketParms(mat_id, tag, left_hand, auto_nml);
        myBuckets[bucket].addPrim(mesh, prim_id, bbox, dirty_bits,instance_bbox);

        myPrimBucketMap[prim_id] = bucket;
    }
    else
    {
        // Ensure it's in the same bucket, otherwise reassign. Mark new
        // collection dirty
        uint64 prev_bucket = entry->second;

        if(prev_bucket != bucket)
        {
            myBuckets[prev_bucket].removePrim(prim_id);
            myPrimBucketMap.erase(prim_id);
        }
            
        myBuckets[bucket].addPrim(mesh, prim_id, bbox, dirty_bits,instance_bbox);
        myPrimBucketMap[prim_id] = bucket;
    }
}

void
husd_ConsolidatedPrims::remove(int prim_id)
{
    UT_AutoLock locker(myLock);

    auto entry = myPrimBucketMap.find(prim_id);
    if(entry != myPrimBucketMap.end())
        if(myBuckets[entry->second].removePrim(prim_id))
        {
            //UTdebugPrint("Remove from bucket");
            myDirtyFlag = true;
        }
}

void
husd_ConsolidatedPrims::selectChange(HUSD_Scene &scene, int prim_id)
{
    UT_AutoLock locker(myLock);

    auto entry = myPrimBucketMap.find(prim_id);
    if(entry != myPrimBucketMap.end())
    {
        auto bucket = myBuckets.find(entry->second);
        if(bucket != myBuckets.end())
            bucket->second.selectChange(scene, prim_id);
    }
}

void
husd_ConsolidatedPrims::processBuckets(bool finalize)
{
    UT_AutoLock locker(myLock);
    
    if(!myDirtyFlag && !finalize)
        return;
    
    myDirtyFlag = false;

    // UT_StopWatch timer;
    // timer.start();
    
    UT_Array<husd_ConsolidatedPrims::RenderTagBucket *> dirty_buckets;
    for(auto &itr : myBuckets)
        if(finalize || itr.second.myDirtyFlag)
            dirty_buckets.append(&itr.second);

    // UTdebugPrint("Process buckets",
    //                dirty_buckets.entries(), "/", myBuckets.size(), finalize);
    if(dirty_buckets.entries() > 0)
    {
#if 1
        HUSD_Scene *scene = &myScene;
        UTparallelFor(UT_BlockedRange<exint>(0, dirty_buckets.entries()),
              [scene,dirty_buckets,finalize](const UT_BlockedRange<exint> &r)
        {
            for(exint i=r.begin(); i!=r.end(); i++)
                dirty_buckets(i)->process(*scene, finalize);
        }, 0, 1);
#else
        for(exint i=0; i<dirty_buckets.entries(); i++)
            dirty_buckets(i)->process(myScene, finalize);
#endif
    }
        
    //timer.stop();
    //UTdebugPrint("Done", timer.getTime() *1000, "ms");
}

void
husd_ConsolidatedPrims::RenderTagBucket::process(HUSD_Scene &scene,
                                                 bool finalize)
{
    if(finalize)
    {
        for(auto &grp : myPrimGroups)
            grp.myComplete = false;
    }
        
    if(!myDirtyFlag)
        return;
    myDirtyFlag = false;

    //UTdebugPrint("Update new prims ", myNewPrims.entries());
    for(auto &prim : myNewPrims)
    {
        int idx = -1;
        for(int i=0; i<myPrimGroups.entries(); i++)
            if(!myPrimGroups(i).myComplete &&
               myPrimGroups(i).myPolyMerger.canAppend(prim.myPrim))
            {
                idx = i;
                break;
            }

        if(idx==-1)
        {
            idx = myPrimGroups.entries();
            myPrimGroups.append();
        }

        int pindex = -1;
        auto &&grp = myPrimGroups(idx);
        if(grp.myEmptySlots.entries())
        {
            pindex = grp.myEmptySlots.last();
            grp.myEmptySlots.removeLast();
            grp.myPolyMerger.replace(pindex, prim.myPrim);
            grp.myBBox(pindex)  = prim.myBBox;
            grp.myInstanceBBox(pindex) = std::move(prim.myInstBBox);
        }
        else
        {
            pindex = grp.myPrimIDs.size();
            grp.myPolyMerger.append(prim.myPrim);
            grp.myBBox.append(prim.myBBox);
            grp.myInstanceBBox.append();
            grp.myInstanceBBox.last() = std::move(prim.myInstBBox);
        }
        
        grp.myPrimIDs[prim.myPrimID] = pindex;
        grp.myDirtyFlag = true;
        grp.myDirtyBits = 0xFFFFFFFF;
        myIDGroupMap[prim.myPrimID] = idx;
    }
    myNewPrims.clear();

    //UTdebugPrint("Prim Groups", myPrimGroups.size(), myIDGroupMap.size());
    UT_Array<PrimGroup *> dirty_groups;
    for(auto &grp : myPrimGroups)
    {
        if(grp.myDirtyFlag && (finalize || 
           (grp.myPolyMerger.getNumSourceFaces() >= MIN_COMPLETE_THRESHOLD &&
            !grp.myComplete)))
        {
            dirty_groups.append(&grp);
        }
        else if(grp.myDirtyFlag)
            myDirtyFlag = true;
    }

    //UTdebugPrint("#dirty", dirty_groups.entries());
    if(dirty_groups.entries() > 0)
    {
#if 1
        HUSD_Scene *pscene = &scene;
        int matid = myMatID;
        HUSD_HydraPrim::RenderTag tag = myRenderTag;
        bool left_handed = myLeftHanded;
        bool auto_nml = myAutoNormal;
        //UTdebugPrint("   parallel", dirty_groups.entries());
        UTparallelForEachNumber(dirty_groups.entries(),
                                [pscene,dirty_groups,matid,tag,left_handed,auto_nml]
                                (const UT_BlockedRange<exint> &r)
        {
            //UTdebugPrint("Range", r.begin(), r.end()-1);
            for(exint i=r.begin(); i!=r.end(); i++)
            {
                dirty_groups(i)->process(*pscene,matid,tag,left_handed,auto_nml);
                dirty_groups(i)->myComplete = true;
            }
        });
#else
        for(exint i=0; i<dirty_groups.entries(); i++)
        {
            dirty_groups(i)->process(scene, myMatID, myRenderTag, myLeftHanded,
                                     myAutoNormal);
            dirty_groups(i)->myComplete = true;
        }
#endif
    }
}
 
void
husd_ConsolidatedPrims::RenderTagBucket::PrimGroup::invalidate()
{
    myDirtyFlag = true;
    myDirtyBits |= (HUSD_HydraGeoPrim::TOP_CHANGE|HUSD_HydraGeoPrim::GEO_CHANGE);
    if(myPrimGroup)
    {
        auto gprim=static_cast<husd_ConsolidatedGeoPrim*>(myPrimGroup.get());
        gprim->setValid(false);
    }
}

void
husd_ConsolidatedPrims::RenderTagBucket::PrimGroup::selectChange(int prim_id)
{
    myDirtyFlag = true;
    myDirtyBits |= (HUSD_HydraGeoPrim::VIS_CHANGE);
    if(myPrimGroup)
    {
        auto gprim=static_cast<husd_ConsolidatedGeoPrim*>(myPrimGroup.get());
        gprim->setValid(false);
    }
}


void
husd_ConsolidatedPrims::RenderTagBucket::PrimGroup::process(
    HUSD_Scene               &scene,
    int                       mat_id,
    HUSD_HydraPrim::RenderTag tag,
    bool                      left_handed,
    bool                      auto_nml)
{
    if(!myDirtyFlag)
        return;

    // UTdebugPrint(this, "#prims", myPrimIDs.size(),
    //              myPolyMerger.getNumSourceFaces(),
    //              myPolyMerger.getNumSourceMeshes());
    if(myPrimIDs.size() > 0)
    {
        if(myDirtyBits & HUSD_HydraGeoPrim::TOP_CHANGE)
            myTopology++;
        if(myDirtyBits & HUSD_HydraGeoPrim::INSTANCE_CHANGE)
        {
            // Transforming alters P & N. 
            myDirtyBits |= HUSD_HydraGeoPrim::GEO_CHANGE;
            myDirtyBits &= ~HUSD_HydraGeoPrim::INSTANCE_CHANGE;
        }

        if(!mySelectionInfo)
        {
            int64 sel[] = { 0, 0 };
            mySelectionInfo = new GT_DAConstantValue<int64>(1, sel, 2);
        }

        GT_AttributeListHandle details;
        auto wnd = new GT_DAConstantValue<int>(1, left_handed?0:1, 1);
        auto consolidated = new GT_DAConstantValue<int>(1, 1, 1);
        auto topology = new GT_DAConstantValue<int64>(1, myTopology, 1);
        auto auton = new GT_DAConstantValue<int64>(1, auto_nml, 1);

        UT_BoundingBoxF box(myBBox(0));
        for(int i=1; i<myBBox.entries(); i++)
            box.enlargeBounds(myBBox(i));

        details = GT_AttributeList::createAttributeList(
                     GT_Names::topology, topology, 
                     GT_Names::consolidated_mesh, consolidated,
                     GT_Names::winding_order, wnd,
                     GT_Names::nml_generated, auton,
                     GT_Names::consolidated_selection, mySelectionInfo.get());
        
        GT_PrimitiveHandle mesh = myPolyMerger.result(details);
        mesh->setPrimitiveTransform(GT_TransformHandle());

        //mesh->dumpAttributeLists("consolidated", false);

        // If there is a mesh with lop_pick_id on it, the mesh is a set of
        // consoldated instances from an instancer.
        int instancer_id = -1;
        UT_IntArray prim_ids;
        if(mesh->getUniformAttributes() &&
           mesh->getUniformAttributes()->get("__instances"))
        {
            // This is an mesh of instances.
            auto ids = mesh->getUniformAttributes()->
                get(GT_Names::lop_pick_id);

            UT_Map<int,int> idmap;
            for(int i=0; i<ids->entries(); i++)
            {
                const int id = ids->getI32(i);
                if(idmap.find(id) == idmap.end())
                {
                    idmap.emplace(id, 0);
                    prim_ids.append(id);
                }
            }
            if(myPrimIDs.size() >= 1)
            {
                for(auto &itr : myPrimIDs)
                {
                    instancer_id = itr.first;
                    break; // should only be one anyway.
                }
            }
            // Boxes for individual instanced.
            myIBBoxList.entries(0);
            for(auto &boxes : myInstanceBBox)
                myIBBoxList.concat(boxes);

            UT_ASSERT(myIBBoxList.entries() == prim_ids.entries());
        }
        else
        {
            // Regular N-prim consolidated mesh.
            for(auto &itr : myPrimIDs)
            {
                prim_ids.append(itr.first);
                myIBBoxList.append(myBBox(itr.second));
            }
            //UTdebugPrint("PrimIDs = ", prim_ids);
        }

        if(!myPrimGroup)
        {
            UT_StringHolder mat_name = scene.lookupMaterial(mat_id);
            UT_WorkBuffer name;

            int index = theUniqueConPrimIndex.exchangeAdd(1);
            name.sprintf("consolidated%d", index);

            auto gprim = new husd_ConsolidatedGeoPrim(scene, mesh, mat_id,
                                                      name.buffer(),
                                                      mySelectionInfo,
                                                      UT_BoundingBox(box));
            gprim->setRenderTag(tag);
            gprim->setMaterial(mat_name);
            gprim->setValid(true);
            gprim->setPrimIDs(prim_ids);
            gprim->setBBoxList(&myIBBoxList);
            gprim->setInstancerPrimID(instancer_id);
            myPrimGroup = gprim;
        }
        else
        {
            auto gprim=static_cast<husd_ConsolidatedGeoPrim*>(myPrimGroup.get());
            gprim->setMesh(mesh, UT_BoundingBox(box));
            gprim->dirty(HUSD_HydraGeoPrim::husd_DirtyBits(myDirtyBits));
            gprim->setPrimIDs(prim_ids);
            gprim->setBBoxList(&myIBBoxList);
            gprim->setInstancerPrimID(instancer_id);
            gprim->setValid(true);
        }
        
        if(!myActiveFlag)
        {
            //UTdebugPrint("Add to list",this, myPrimGroup->geoID());
            scene.addDisplayGeometry(myPrimGroup.get());
            myActiveFlag = true;
        }
    }
    else if(myActiveFlag)
    {
        scene.removeDisplayGeometry(myPrimGroup.get());
        myActiveFlag = false;
    }
    myDirtyBits = 0;
}


class husd_StashedSelection : public UT_LinkNode
{
public:
    husd_StashedSelection(const UT_Map<int,int> &s) : selection(s) {}
    
    UT_Map<int,int> selection;
};

class husd_SceneNode
{
public:
    husd_SceneNode(const UT_StringRef &p,
                   HUSD_Scene::PrimType t,
                   int i,
                   husd_SceneNode *n)
        : myPath(p), myType(t), myParent(n), myID(i), myRecurse(false),
          myPrototypes(nullptr), myInstancerID(-1),
          mySerial(-1) {}
    ~husd_SceneNode();

    int  addInstance(const UT_StringRef &inst_indices,
                     const UT_StringRef &prototype,
                     husd_SceneTree *tree);
        
    void print(int level, int &count);

    class husd_Prototypes
    {
    public:
        UT_StringMap<int>              myInstances;
        UT_Map<int,UT_StringHolder>    myIDPaths;
    };
    
    UT_SmallArray<husd_SceneNode *> myChildren;
    UT_StringMap<husd_Prototypes*> *myPrototypes;
    UT_StringHolder                 myPath;
    husd_SceneNode                 *myParent;
    HUSD_Scene::PrimType            myType;
    int                             myID;
    int                             myInstancerID;
    int                             mySerial;
    bool                            myRecurse;
};

class husd_SceneTree
{
public:
     husd_SceneTree();
    ~husd_SceneTree();

    // Access the tree node. For prims, this will be the prim. For instancers,
    // the topmost parent instancer.
    husd_SceneNode *lookupID(int id) const;

    // Access a tree node by path name.
    husd_SceneNode *lookupPath(const UT_StringRef &path) const;
                               
    // Lookup or create a tree node for `path`. If id != -1, it wil be used,
    // otherwise create a unique one.
    husd_SceneNode *generatePath(const UT_StringRef &path, int id,
                                 HUSD_Scene::PrimType type);
    bool            removeNode(const UT_StringRef &path);

    // Resolve an ID into a path.
    const UT_StringRef &resolveID(int id);

    void            setNodeID(husd_SceneNode *node, int id)
                         { myIDMap[id] = node; }

    // Instanceable reference resolving
    int             addInstanceRef(int pick_id,
                                   const UT_StringRef &path,
                                   int instancer_id);
    void            removeInstanceRef(int pick_id);
    
    void            print();
private:
    bool            removeNodeIfEmpty(husd_SceneNode *node);
    husd_SceneNode *myRoot;
    UT_StringMap<husd_SceneNode *> myPathMap;
    UT_Map<int, husd_SceneNode *>  myIDMap;
};

husd_SceneTree::husd_SceneTree()
{
    UT_StringHolder rootpath = "/"_sh;
    
    myRoot = new husd_SceneNode(rootpath, HUSD_Scene::ROOT,
                                HUSD_HydraPrim::newUniqueId(), nullptr);
    myPathMap[rootpath] = myRoot;
    myIDMap[myRoot->myID] = myRoot;
}

husd_SceneTree::~husd_SceneTree()
{
    for(auto itr : myPathMap)
        delete itr.second;
}

bool
husd_SceneTree::removeNode(const UT_StringRef &spath)
{
    auto entry = myPathMap.find(spath);
    if(entry == myPathMap.end())
        return true; // already removed.

    return removeNodeIfEmpty(entry->second);
}

bool
husd_SceneTree::removeNodeIfEmpty(husd_SceneNode *node)
{
    if(node->myChildren.entries() != 0 || node->myType == HUSD_Scene::ROOT)
        return false;
    
    if(node->myParent)
    {
        node->myParent->myChildren.findAndRemove(node);
        if(node->myParent->myChildren.entries() == 0)
            removeNodeIfEmpty(node->myParent);
    }

    myPathMap.erase(node->myPath);
    myIDMap.erase(node->myID);
    
    if(node->myType == HUSD_Scene::INSTANCER && node->myPrototypes)
    {
        for(auto &proto : *node->myPrototypes)
        {
            for(auto &id : proto.second->myInstances)
                myIDMap.erase(id.second);
        }
    }
    
    delete node;
    return true;
}

husd_SceneNode *
husd_SceneTree::lookupPath(const UT_StringRef &spath) const
{
    auto entry = myPathMap.find(spath);
    if(entry != myPathMap.end())
        return entry->second;

    return nullptr;
}

husd_SceneNode *
husd_SceneTree::generatePath(const UT_StringRef &spath,
                             int id,
                             HUSD_Scene::PrimType prim_type)
{
    UT_StringHolder cache_path(spath);
    if(prim_type == HUSD_Scene::INSTANCER)
        cache_path += "[]";
    
    auto entry = myPathMap.find(cache_path);
    if(entry != myPathMap.end())
    {
        if(prim_type != HUSD_Scene::INVALID_TYPE)
            entry->second->myType = prim_type;
        
        if(id != -1 && id != entry->second->myID)
        {
            entry->second->myID = id;
            myIDMap.erase(entry->second->myID);
            myIDMap[id] = entry->second;
        }
        return entry->second;
    }
    
    SdfPath path(spath.toStdString());
    UT_ASSERT(path.IsAbsolutePath());
    if(!path.IsAbsolutePath())
        return nullptr;

    // Search upward to find the first branch that exists.
    husd_SceneNode *pnode = nullptr;
    UT_StringArray new_branches;
    while(!pnode)
    {
        SdfPath ppath = path.GetParentPath();
        if(ppath == SdfPath::AbsoluteRootPath())
            pnode = myRoot;
        else if(ppath.IsEmpty()) // sanity condition to avoid inf loops.
            break;
        else
        {
            HUSD_Path hpath(ppath);
            UT_StringRef key(hpath.pathStr());
            if(key.isstring())
            {
                auto pentry = myPathMap.find(key);
                if(pentry != myPathMap.end())
                    pnode = pentry->second;
            }
        }
        if(!pnode)
        {
            HUSD_Path hpath(ppath);
            new_branches.append(hpath.pathStr());
        }
        path = ppath;
    }

    if(pnode)
    {
        // Create the branch to the child node.
        for(auto itr = new_branches.rbegin(); itr != new_branches.rend(); ++itr)
        {
            int pid = HUSD_HydraPrim::newUniqueId();
            auto node = new husd_SceneNode(*itr, HUSD_Scene::PATH, pid, pnode);
            pnode->myChildren.append(node);
            pnode = node;
            
            myPathMap[*itr] = node;
            myIDMap[pid] = node;
        }

        // Create the child node.
        if(id == -1)
            id = HUSD_HydraPrim::newUniqueId();
        auto node = new husd_SceneNode(spath, prim_type, id, pnode);
        pnode->myChildren.append(node);
        
        myPathMap[cache_path] = node;
        myIDMap[id] = node;

        return node;
    }

    // UTdebugPrint("Could not generate for", spath);
    
    // Not a good condition.
    return nullptr;
}

husd_SceneNode *
husd_SceneTree::lookupID(int id) const
{
    auto entry = myIDMap.find(id);
    if(entry != myIDMap.end())
        return entry->second;
    return nullptr;
}

const UT_StringRef &
husd_SceneTree::resolveID(int id)
{
    static UT_StringRef theNullPath;
    
    auto node = lookupID(id);
    if (node)
    {
        if(node->myID == id)
            return node->myPath;

        // Instancer.
        if(node->myPrototypes)
        {
            for(auto &proto : *node->myPrototypes)
            {
                auto entry = proto.second->myIDPaths.find(id);
                if(entry != proto.second->myIDPaths.end())
                    return entry->second;
            }
        }
    }

    return theNullPath;
}

int
husd_SceneTree::addInstanceRef(int pick_id,
                               const UT_StringRef &path,
                               int inst_id)
{
    auto pnode = generatePath(path, pick_id, HUSD_Scene::INSTANCE_REF);
    if (pnode)
    {
        pnode->myInstancerID = inst_id;
        return pnode->myID;
    }

    return -1;
}

void
husd_SceneTree::removeInstanceRef(int pick_id)
{
    auto refnode = myIDMap.find(pick_id);
    if(refnode != myIDMap.end())
        removeNodeIfEmpty(refnode->second);
}


husd_SceneNode::~husd_SceneNode()
{
    delete myPrototypes;
}

int
husd_SceneNode::addInstance(const UT_StringRef &inst_indices,
                            const UT_StringRef &prototype,
                            husd_SceneTree *tree)
{
    UT_ASSERT(myType == HUSD_Scene::INSTANCER);
    if(!myPrototypes)
        myPrototypes = new UT_StringMap<husd_Prototypes *>();

    husd_Prototypes *pt = nullptr;
    auto pentry = myPrototypes->find(prototype);
    if(pentry == myPrototypes->end())
    {
        pt = new husd_Prototypes;
        (*myPrototypes)[prototype] = pt;
    }
    else
        pt = pentry->second;
    
    int id = -1;
    auto entry = pt->myInstances.find(inst_indices);
    if(entry != pt->myInstances.end())
        id = entry->second;
    else
    {
        id = HUSD_HydraPrim::newUniqueId();
        pt->myInstances.emplace(inst_indices, id);
        pt->myIDPaths.emplace(id, inst_indices);
        tree->setNodeID(this, id);
        //UTdebugPrint("Set Resolved: ", myID, id, inst_indices);
    }
    return id;
}

void
husd_SceneTree::print()
{
#if UT_ASSERT_LEVEL > 0
    int count = 0;
    myRoot->print(0, count);
    UTdebugPrint("# nodes = ", myPathMap.size(), myIDMap.size(), count);
    // for(auto &itr : myPathMap)
    //     UTdebugPrint( itr.second->myID, itr.first);
#endif
}

void
husd_SceneNode::print(int level,int &count)
{
#if UT_ASSERT_LEVEL > 0
    UT_StringRef type;
    switch(myType)
    {
    case HUSD_Scene::GEOMETRY: type = "geo"; break;
    case HUSD_Scene::LIGHT: type = "light"; break;
    case HUSD_Scene::CAMERA: type = "cam"; break;
    case HUSD_Scene::PATH: type = "xf"; break;
    case HUSD_Scene::ROOT: type = "root"; break;
    case HUSD_Scene::INSTANCE: type = "inst"; break;
    case HUSD_Scene::INSTANCER: type = "Instr"; break;
    case HUSD_Scene::INSTANCE_REF: type = "iref"; break;
    case HUSD_Scene::MATERIAL: type = "mat"; break;
    default:
        break;
    };

    UT_StringHolder space;
    for(int i=0; i<level; i++)
        space+="  ";

    exint num = myPath.countChar('/');
    const char *name = myPath;
    if(num > 1)
    {
        exint idx = myPath.lastCharIndex('/');
        if(idx >= 0)
            name = (const char *)myPath + idx+1;
    }

    UT_StringHolder inst;
    if(myType == HUSD_Scene::INSTANCER && myPrototypes)
    {
        UT_WorkBuffer instb;
        
        auto proto = myPrototypes->begin();
        const int num_inst = proto->second->myInstances.size();
        
        if(myPrototypes->size() > 1)
            instb.sprintf(" #protos=%d  [%d]", (int)myPrototypes->size(),
                          num_inst);
        else
            instb.sprintf(" [%d]", num_inst);
        inst=instb.buffer();
    }

    UTdebugPrint(space, "-", name, type, myID, " #", myChildren.entries(),inst);
    for(int i=0; i<myChildren.entries(); i++)
        myChildren(i)->print(level+1,count);
    
    count++;
#endif
}

// ---------------------------------------------------------------------------

void
HUSD_Scene::debugPrintTree()
{
    myTree->print();
}

void
HUSD_Scene::debugPrintSelection()
{
    UTdebugPrint("Selection (ids)", mySelection.size(),":");
    for(auto itr : mySelection)
        fprintf(stderr, "%d ", itr.first);
    UTdebugPrint("\nSelection (paths):", mySelectionArray);
    
}

int
HUSD_Scene::getMaxGeoIndex()
{
    return theGeoIndex;
}


void
HUSD_Scene::pushScene(HUSD_Scene *scene)
{
    UT_ASSERT( !theCurrentScene );
    theCurrentScene = scene;
}

void
HUSD_Scene::popScene(HUSD_Scene *scene)
{
    UT_ASSERT(theCurrentScene == scene);
    theCurrentScene = nullptr;
}

bool
HUSD_Scene::hasScene()
{
    return theCurrentScene != nullptr;
}

PXR_NS::XUSD_ViewerDelegate *
HUSD_Scene::newDelegate()
{
    UT_ASSERT_P(theCurrentScene);
    return new PXR_NS::XUSD_ViewerDelegate(*theCurrentScene);
}

void
HUSD_Scene::freeDelegate(PXR_NS::XUSD_ViewerDelegate *del)
{
    delete del;
}



// -------------------------------------------------------------------------


HUSD_Scene::HUSD_Scene()
    : myStage(HUSD_FOR_MIRRORING),
      myHighlightID(1),
      mySelectionID(1),
      myGeoSerial(0),
      myModSerial(0),
      myCamSerial(0),
      myLightSerial(0),
      mySelectionResolveSerial(0),
      mySelectionArrayID(0),
      myDeferUpdate(false),
      myRenderIndex(nullptr),
      myRenderParam(nullptr),
      myCurrentRecalledSelection(nullptr),
      myStashedSelectionSizeB(0),
      myCurrentSelectionStashed(0),
      mySelectionArrayNeedsUpdate(false),
      myRenderPrimRes(0,0),
      myConformPolicy(EXPAND_APERTURE),
      mySelectionSerial(0)
{
    myTree = new husd_SceneTree;
    myPrimConsolidator = new husd_ConsolidatedPrims(*this);
}

HUSD_Scene::~HUSD_Scene()      
{
    delete myTree;
    delete myPrimConsolidator;
}

void
HUSD_Scene::addGeometry(HUSD_HydraGeoPrim *geo, bool new_geo)
{
    if(new_geo)
    {
        auto entry = myGeometry.find(geo->geoID());
        if(entry != myGeometry.end())
            myDuplicateGeo.append(entry->second);
        
        myGeometry[ geo->geoID() ] = geo;
        myTree->generatePath(geo->path(), geo->id(), GEOMETRY);
    }
}

void
HUSD_Scene::removeGeometry(HUSD_HydraGeoPrim *geo)
{
    if(geo->index() >= 0)
	removeDisplayGeometry(geo);

    myGeometry.erase(geo->geoID());

    myTree->removeNode(geo->path());
}


void
HUSD_Scene::addDisplayGeometry(HUSD_HydraGeoPrim *geo)
{
    UT_AutoLock lock(myDisplayLock);
    
    if(theFreeGeoIndex.entries())
    {
	geo->setIndex(theFreeGeoIndex.last());
	theFreeGeoIndex.removeLast();
    }
    else
    {
	geo->setIndex(theGeoIndex);
	theGeoIndex++;
    }
    UT_ASSERT(myDisplayGeometry.find(geo->geoID()) == myDisplayGeometry.end());
    myDisplayGeometry[ geo->geoID() ] = geo;

    geometryDisplayed(geo, true);
    myGeoSerial++;
}

void
HUSD_Scene::removeDisplayGeometry(HUSD_HydraGeoPrim *geo)
{
    UT_AutoLock lock(myDisplayLock);

    geometryDisplayed(geo, false);
    
    theFreeGeoIndex.append(geo->index());
    myDisplayGeometry.erase(geo->geoID());
    
    geo->setIndex(-1);
    myGeoSerial++;
}

bool
HUSD_Scene::fillGeometry(UT_Array<HUSD_HydraGeoPrimPtr> &array, int64 &id)
{
    // avoid needlessly refilling the array if it hasn't changed.
    if(id == myGeoSerial)
        return false;

    array.entries(0);
    
    UT_AutoLock lock(myDisplayLock);
    
    array.entries(getMaxGeoIndex());
    array.zero();
    for(auto it : myDisplayGeometry)
    {
        const int idx = it.second->index();
        array(idx) = it.second;
    }

    id = myGeoSerial;
    return true;
}


void
HUSD_Scene::addCamera(HUSD_HydraCamera *cam, bool new_cam)
{
    UT_AutoLock lock(myLightCamLock);
    auto entry = myCameras.find(cam->path());
    if(entry != myCameras.end())
        myDuplicateCam.append(entry->second);
    
    myCameras[ cam->path() ] = cam;
    if(new_cam)
    {
        myTree->generatePath(cam->path(), cam->id(), CAMERA);
        myCamSerial++;
    }
}

void
HUSD_Scene::removeCamera(HUSD_HydraCamera *cam)
{
    UT_AutoLock lock(myLightCamLock);
    myTree->removeNode(cam->path());
    myCameras.erase( cam->path() );
    myCamSerial++;
}

bool
HUSD_Scene::fillCameras(UT_Array<HUSD_HydraCameraPtr> &array, int64 &id)
{
    if(id == myCamSerial)
        return false;

    array.entries(0);
    
    UT_AutoLock lock(myLightCamLock);
    for(auto it : myCameras)
        array.append(it.second);

    id = myCamSerial;
    return true;
}

void
HUSD_Scene::addLight(HUSD_HydraLight *light, bool new_light)
{
    UT_AutoLock lock(myLightCamLock);
    auto entry = myLights.find(light->path());
    if(entry != myLights.end())
        myDuplicateLight.append(entry->second);
        
    myLights[ light->path() ] = light;
    if(new_light)
    {
        myTree->generatePath(light->path(), light->id(), LIGHT);
        myLightSerial++;
    }
}

void
HUSD_Scene::removeLight(HUSD_HydraLight *light)
{
    UT_AutoLock lock(myLightCamLock);
    myTree->removeNode(light->path());

    myLights.erase( light->path() );
    
    myLightSerial++;
}

bool
HUSD_Scene::fillLights(UT_Array<HUSD_HydraLightPtr> &array, int64 &id)
{
    if(id == myLightSerial)
        return false;

    array.entries(0);
    
    UT_AutoLock lock(myLightCamLock);
    for(auto it : myLights)
        array.append(it.second);

    id = myCamSerial;
    return true;
}

void
HUSD_Scene::addMaterial(HUSD_HydraMaterial *mat)
{
    UT_AutoLock lock(myMaterialLock);
    myMaterials[ mat->path() ] = mat;
    myMaterialIDs[ mat->id() ] = mat->path();
}

void
HUSD_Scene::removeMaterial(HUSD_HydraMaterial *mat)
{
    UT_AutoLock lock(myMaterialLock);
    // Make sure to erase the ID first since erasing the material might delete
    // the material itself.
    myMaterialIDs.erase( mat->id() );
    myMaterials.erase( mat->path() );
}

const UT_StringRef &
HUSD_Scene::lookupMaterial(int id) const
{
    auto entry = myMaterialIDs.find(id);
    if(entry != myMaterialIDs.end())
        return entry->second;

    static UT_StringHolder theNullPath;
    return theNullPath;
}


UT_StringHolder
HUSD_Scene::lookupPath(int id, bool allow_instance) const
{
    return resolveID(id, allow_instance);
}

int
HUSD_Scene::lookupGeomId(const UT_StringRef &path)
{
    auto entry = myDisplayGeometry.find(path);
    if(entry != myDisplayGeometry.end())
        return entry->second->id();

    // Path -> Render ID -> Hou Geom ID
    auto rentry = myRenderIDs.find(path);
    if(rentry != myRenderIDs.end())
    {
        auto gentry = myRenderIDtoGeomID.find(rentry->second);
        if(gentry != myRenderIDtoGeomID.end())
            return gentry->second;
    }

    return -1;
}


void
HUSD_Scene::setRenderID(const UT_StringRef &path, int id)
{
    int idx = path.findCharIndex('[');
    if(idx != -1)
    {
        UT_StringView base_v(path.c_str(), idx);
        UT_StringHolder base(base_v);

        int pid = getOrCreateID(base, INSTANCER);
        auto node = myTree->lookupID(pid);
        if(node)
        {
            UT_StringView indices_v(path.c_str()+idx+1,
                                    path.length() - idx -2);
            UT_StringHolder indices(indices_v);
            int inst_id = node->addInstance(indices, UT_StringHolder(), myTree);

            myRenderIDs[path] = id;
            myRenderPaths[id] = path;
            myRenderIDtoGeomID[id] = inst_id;
        }
    }
    else
    {
        myRenderIDs[path] = id;
        myRenderPaths[id] = path;
        int pid = getOrCreateID(path);
        myRenderIDtoGeomID[id] = pid;
    }
}

int
HUSD_Scene::lookupRenderID(const UT_StringRef &path) const
{
    auto entry = myRenderIDs.find(path);
    if(entry != myRenderIDs.end())
        return entry->second;
    return -1;
}

UT_StringHolder
HUSD_Scene::lookupRenderPath(int id) const
{
    auto entry = myRenderPaths.find(id);
    if(entry != myRenderPaths.end())
        return entry->second;
    return UT_StringHolder();
}

int
HUSD_Scene::convertRenderID(int id) const
{
    auto entry = myRenderIDtoGeomID.find(id);
    if(entry != myRenderIDtoGeomID.end())
        return entry->second;
    return -1;
}

void
HUSD_Scene::clearRenderIDs()
{
    myRenderIDs.clear();
    myRenderPaths.clear();
    myRenderIDtoGeomID.clear();
}

bool
HUSD_Scene::setRenderPrimNames(const UT_StringArray &names)
{
    bool same = false;
    if(myRenderPrimNames.entries() == names.entries())
    {
        same = true;
        for(int i=0; i<names.entries(); i++)
            if(names(i) != myRenderPrimNames(i))
            {
                same = false;
                break;
            }
    }
    if(!same)
    {
        myRenderPrimNames = names;
        return true;
    }

    return false;
}

void
HUSD_Scene::setRenderPrimCamera(const UT_StringRef &camname)
{
    myRenderPrimCamera = camname;
}


void
HUSD_Scene::consolidateMesh(const GT_PrimitiveHandle &mesh,
                            const UT_BoundingBoxF &bbox,
                            int prim_id,
                            int mat_id,
                            int dirty_bits,
                            HUSD_HydraPrim::RenderTag tag,
                            bool lefthand,
                            bool auto_nml,
                            UT_Array<UT_BoundingBox> &instance_bbox,
                            int instancer_id)
{
    //UTdebugPrint("Consolidate prim id", prim_id);
    myPrimConsolidator->add(mesh, bbox, prim_id, mat_id, dirty_bits,
                            tag, lefthand, auto_nml, instance_bbox,instancer_id);
}

void
HUSD_Scene::removeConsolidatedPrim(int id)
{
    myPrimConsolidator->remove(id);
}

void
HUSD_Scene::selectConsolidatedPrim(int id)
{
    myPrimConsolidator->selectChange(*this, id);
}

void
HUSD_Scene::processConsolidatedMeshes(bool finalize)
{
    //UTdebugPrint("Process meshes", finalize);
    myPrimConsolidator->processBuckets(finalize);
}

HUSD_HydraGeoPrimPtr
HUSD_Scene::findConsolidatedPrim(int id) const
{
    const UT_StringRef &path = lookupPath(id);
    if(path.isstring())
    {
        auto entry = myGeometry.find(path);
        if(entry != myGeometry.end())
            return entry->second;
    }
            
    return HUSD_HydraGeoPrimPtr();
}

HUSD_Scene::PrimType
HUSD_Scene::getPrimType(int id) const
{
    UT_AutoLock lock(myDisplayLock);
    
    auto node = myTree->lookupID(id);
    if(node)
    {
        if(node->myID != id)
        {
            // Instance (node id is the parent instancer)
            return INSTANCE;
        }
        else
            return node->myType;
    }

    return INVALID_TYPE;
}

int
HUSD_Scene::getParentInstancer(int id, bool topmost) const
{
    UT_AutoLock lock(myDisplayLock);
    
    auto node = myTree->lookupID(id);
    if(node && node->myType == INSTANCER)
    {
        if(topmost)
        {
            while(node)
            {
                if(node->myType == INSTANCER)
                    id = node->myID;
                node = node->myParent;
            }
        }
        else
            id = node->myID;
        
        
        return id;
    }

    return -1;
}

int
HUSD_Scene::getIDForPrim(const UT_StringRef &path,
                         PrimType &prim_type,
                         bool create_path_id)
{
    auto g_entry = myDisplayGeometry.find(path);
    if(g_entry != myDisplayGeometry.end())
    {
        prim_type = GEOMETRY;
        return g_entry->second->id();
    }

    auto l_entry = myLights.find(path);
    if(l_entry != myLights.end())
    {
        prim_type = LIGHT;
        return l_entry->second->id();
    }
    
    auto c_entry = myCameras.find(path);
    if(c_entry != myCameras.end())
    {
        prim_type = CAMERA;
        return c_entry->second->id();
    }

    if(create_path_id)
    {
        int id = getOrCreateID(path, PATH);
        prim_type = getPrimType(id);
        return id;
    }

    prim_type = INVALID_TYPE;
    return -1;
}

int
HUSD_Scene::getOrCreateID(const UT_StringRef &path,
                          PrimType type)
{
    UT_ASSERT(type != INSTANCE && type != INSTANCE_REF);
    
    UT_AutoLock lock(myDisplayLock);
    
    auto prim_node = myTree->generatePath(path, -1, type);
    return prim_node->myID;
}

int
HUSD_Scene::getOrCreateInstanceID(const UT_StringRef &path,
                                  const UT_StringRef &instancer,
                                  const UT_StringRef &prototype)
{
    UT_AutoLock lock(myDisplayLock);
    
    husd_SceneNode *inst_node = nullptr;
    if(instancer)
    {
        UT_StringHolder ipath = instancer;
        ipath += "[]";
        inst_node = myTree->lookupPath(ipath);
        int id = inst_node->addInstance(path, prototype, myTree);
        return id;
    }
    else
    {
        if(path.startsWith(theQuestionMark))
        {
            exint idx = path.findCharIndex(' ', 1);
            UT_ASSERT(idx >= 0);
            UT_StringView inst_sid(path.c_str()+1, idx-1);
            UT_StringHolder id(inst_sid);

            auto entry = myInstancerIDs.find(id.toInt());
            if(entry != myInstancerIDs.end())
            {
                UT_WorkBuffer ipath;
                HUSD_Path hpath(entry->second->GetId());
                
                ipath.strcpy(hpath.pathStr());
                ipath.append("[]");
                inst_node = myTree->lookupPath(ipath);
                return inst_node->addInstance(path, prototype, myTree);
            }
        }
    }

    return -1;
}



const UT_StringSet &
HUSD_Scene::volumesUsingField(const UT_StringRef &field) const
{
    static const UT_StringSet theEmptySet{};
    auto it = myFieldsInVolumes.find(field);

    if (it != myFieldsInVolumes.end())
	return it->second;

    return theEmptySet;
}

void
HUSD_Scene::addVolumeUsingField(const UT_StringHolder &volume,
	const UT_StringHolder &field)
{
    myFieldsInVolumes[field].insert(volume);
}

void
HUSD_Scene::removeVolumeUsingFields(const UT_StringRef &volume)
{
    for (auto &&volumes : myFieldsInVolumes)
	volumes.second.erase(volume);
}

template <class A> void appendPatternPaths(const UT_StringMap<A> &map,
					   const char *pattern,
					   UT_StringArray &paths)
{
    for(auto it : map)
    {
	auto &name = it.first;
	if(name.match(pattern))
	    paths.append(name);
    }
}

void
HUSD_Scene::convertSelection(const char *selection,
			     UT_StringArray &paths)
{
    if(UTisstring(selection))
    {
	UT_WorkArgs args;
	UT_String select(selection, 1);

	select.tokenizeInPlace(args);
	if(args.entries() > 0)
	{
	    for(int i=0; i<args.entries(); i++)
	    {
		UT_String pattern(args(i));
		if(pattern.findChar("*") || pattern.findChar("?"))
		{
		    appendPatternPaths(myDisplayGeometry, pattern, paths);
		    appendPatternPaths(myCameras, pattern, paths);
		    appendPatternPaths(myLights, pattern, paths);
		}
		else
		    paths.append(pattern);
	    }
	}
    }
}

bool
HUSD_Scene::hasInstanceSelections()
{
    for(auto sel : mySelection)
    {
        auto type = getPrimType(sel.first);
        if(type == INSTANCE)
            return true;
    }
    return false;
}

bool
HUSD_Scene::removeInstanceSelections()
{
    UT_IntArray to_remove;
    
    for(auto sel : mySelection)
    {
        auto type = getPrimType(sel.first);
        if(type == INSTANCE)
            to_remove.append(sel.first);
    }
    
    for(auto id : to_remove)
        mySelection.erase(id);

    if(to_remove.entries())
        mySelectionID++;

    return (to_remove.entries() > 0);
}

bool
HUSD_Scene::removePrimSelections()
{
    UT_IntArray to_remove;
    
    for(auto sel : mySelection)
    {
        auto type = getPrimType(sel.first);
        if(type != INSTANCE)
            to_remove.append(sel.first);
    }

    for(auto id : to_remove)
        mySelection.erase(id);

    if(to_remove.entries())
        mySelectionID++;
    
    return (to_remove.entries() > 0);
}

void
HUSD_Scene::selectInstanceLevel(int nest_lvl)
{
    UT_AutoLock lock(myDisplayLock);
    UT_IntArray to_remove;
    UT_Map<int,int> to_add;
    
    for(auto sel : mySelection)
    {
        const int id = sel.first;
        const UT_StringRef &inst_id = myTree->resolveID(id);

        if(inst_id.startsWith(theQuestionMark))
        {
            UT_String select(inst_id.c_str(), 1);
            UT_WorkArgs args;

            select.tokenizeInPlace(args, ' ');
            int max_args = nest_lvl+2;
            if(args.entries() > max_args)
            {
                auto inode = myTree->lookupID(id);
                if(inode->myPrototypes)
                {
                    UT_WorkBuffer instance;

                    // If an instancer has multiple prototypes, each one must
                    // get a selection id when moving up a level.
                    HdInstancer *hdinst = getInstancer(inode->myPath);
                    while(!hdinst->GetParentId().IsEmpty())
                    {
                        hdinst = hdinst->GetDelegate()->GetRenderIndex().
                            GetInstancer(hdinst->GetParentId());
                    }
                    auto instancer = static_cast<XUSD_HydraInstancer *>(hdinst);
                    if(instancer && instancer->prototypeIDs().size() > 1)
                    {
                        for(auto &proto : instancer->prototypeIDs())
                        {
                            auto pnode = myTree->lookupID(proto.first);
                            instance.strcpy(args.getArg(0));
                            instance.append(" ");
                            instance.append(pnode->myPath);
                            
                            for(int i=2; i<max_args; i++)
                            {
                                instance.append(" ");
                                instance.append(args.getArg(i));
                            }               
                            UT_StringRef new_instance(instance.buffer());
                            int new_id = inode->addInstance(new_instance,
                                                            pnode->myPath,
                                                            myTree);
                            to_add.emplace(new_id, 0);
                            selectionModified(new_id);
                        }
                    }
                    else
                    {
                        for(int i=0; i<max_args; i++)
                        {
                            if(i!=0)
                                instance.append(" ");
                            instance.append(args.getArg(i));
                        }
                        UT_StringHolder proto(args.getArg(1));
                        UT_StringRef new_instance(instance.buffer());
                        int new_id = inode->addInstance(new_instance,
                                                        proto, myTree);
                        to_add.emplace(new_id, 0);
                    }
                    to_remove.append(id);
                }
            }
        }
    }

    for(auto add : to_add)
        mySelection.emplace(add.first, 1);
    
    for(auto id : to_remove)
        mySelection.erase(id);

    if(to_remove.entries())
        mySelectionID++;
}

void
HUSD_Scene::setSelection(const UT_StringArray &paths,
                         bool stash_prev_selection)
{
    UT_AutoLock lock(myDisplayLock);

    mySelectionSerial++;
    
    if(stash_prev_selection)
        stashSelection();

    //UTdebugPrint("\nSet selection", paths);
    for(auto entry : mySelection)
    {
        auto pnode = myTree->lookupID(entry.first);
        if(pnode)
            selectionModified(pnode);
    }
    
    mySelectionID++;
    mySelection.clear();

    bool missing = false;

    for(const auto &selpath : paths)
    {
        int idx = selpath.findCharIndex('[');
        if(idx == -1)
        {
            auto pnode = myTree->lookupPath(selpath);
            if(pnode)
            {
                //UTdebugPrint("mod", pnode->myPath, pnode->myID);
                selectionModified(pnode);
                mySelection[pnode->myID] = 1;

                continue;
            }
        }
        
        // Instancer.
        UT_StringHolder instance_path(selpath.c_str(), idx);
        auto instancer = getInstancer(instance_path);
        auto pnode = myTree->lookupPath(instance_path);
        if(instancer && pnode)
        {
            UT_StringHolder indices;
            UT_StringArray inst_keys =
                instancer->resolveInstanceID(*this, selpath, idx, indices);
            for(auto &key : inst_keys)
            {
                const int end_path = key.findCharIndex(' ');
                UT_ASSERT(end_path != -1);
                UT_StringHolder bottom_instancer(key.c_str()+1, // skip ?
                                                 end_path-1);
                auto inode = myTree->lookupPath(bottom_instancer);
                if(inode)
                {
                    const int end_proto = key.findCharIndex(' ', end_path+1);
                    UT_StringHolder bottom_proto(key.c_str()+end_path+1,
                                                     end_proto-1);
                    const int id = inode->addInstance(key, bottom_proto, myTree);
                    //UTdebugPrint("Select instance", id);
                    mySelection[id] = 1;
                }
            }
            selectionModified(pnode);
        }
    }
    // UTdebugPrint("#selected", mySelection.size());
    // for(auto &itr: mySelection) UTdebugPrint(itr.first);
    mySelectionArray = paths;
    mySelectionArrayID = mySelectionID;

    // If some ids failed to resolve, we may need to try again after the
    // scene is updated.
    mySelectionArrayNeedsUpdate = missing;
    if(missing)
    {
        // Don't attempt to resolve unless something changes.
        mySelectionResolveSerial = myGeoSerial + myLightSerial + myCamSerial;
    }
}

void
HUSD_Scene::setHighlight(const UT_StringArray &paths)
{
    UT_AutoLock lock(myDisplayLock);
    
    myHighlightID++;
    myHighlight.clear();

    bool changed = false;

    for(const auto &selpath : paths)
    {
        int idx = selpath.findCharIndex('[');
        if(idx == -1)
        {
            auto pnode = myTree->lookupPath(selpath);
            if(pnode)
            {
                changed = true;
                myHighlight[pnode->myID] = 1;

                continue;
            }
        }
        
        // Instancer.
        UT_StringHolder instance_path(selpath.c_str(), idx);
        auto instancer = getInstancer(instance_path);
        auto pnode = myTree->lookupPath(instance_path);
        if(instancer && pnode)
        {
            UT_StringHolder indices;
            UT_StringArray inst_keys =
                instancer->resolveInstanceID(*this, selpath, idx, indices);
            for(auto &key : inst_keys)
            {
                const int end_path = key.findCharIndex(' ');
                UT_ASSERT(end_path != -1);
                UT_StringHolder bottom_instancer(key.c_str()+1, // skip ?
                                                 end_path-1);
                auto inode = myTree->lookupPath(bottom_instancer);
                if(inode)
                {
                    const int end_proto = key.findCharIndex(' ', end_path+1);
                    UT_StringHolder bottom_proto(key.c_str()+end_path+1,
                                                     end_proto-1);
                    const int id = inode->addInstance(key, bottom_proto, myTree);
                    myHighlight[id] = 1;
                    changed = true;
                }
            }
        }
    }
    if(changed)
        myHighlightID++;
}

bool
HUSD_Scene::selectionModified(int id)
{
    auto pnode = myTree->lookupID(id);
    if(pnode && pnode->mySerial != mySelectionSerial)
    {
        return selectionModified(pnode);
    }
    // else
    //     UTdebugPrint("NO ID", id);

    return false;
}

bool
HUSD_Scene::selectionModified(husd_SceneNode *pnode)
{
    if(pnode->myRecurse || mySelectionSerial == pnode->mySerial)
        return false;

    pnode->mySerial = mySelectionSerial;
    pnode->myRecurse = true;
    
    auto &&selpath = pnode->myPath;
    bool modified = false;

    if(pnode->myType == GEOMETRY)
    {
        auto geo_entry = myGeometry.find(selpath);
        if(geo_entry != myGeometry.end())
        {
            //UTdebugPrint("Mod geo", geo_entry->second->id(), geo_entry->second->isConsolidated());
            geo_entry->second->selectionDirty(true);
            if(geo_entry->second->isConsolidated())
                selectConsolidatedPrim(geo_entry->second->id());
            modified = true;
        }
    }
    else if(pnode->myType == LIGHT)
    {
        UT_AutoLock locker(myLightCamLock);
        auto entry = myLights.find(selpath);
        if(entry != myLights.end())
        {
            entry->second->selectionDirty(true);
            modified = true;
        }
    }
    else if(pnode->myType == CAMERA)
    {
        UT_AutoLock locker(myLightCamLock);
        auto entry = myCameras.find(selpath);
        if(entry != myCameras.end())
        {
            entry->second->selectionDirty(true);
            modified = true;
        }
    }
    else if(pnode->myType == INSTANCER)
    {
        for(auto cnode : pnode->myChildren)
            if(selectionModified(cnode))
                modified = true;

        auto inode = myTree->lookupPath(pnode->myPath);
        if(inode && inode != pnode)
        {
            if(selectionModified(inode))
                modified = true;
        }
    }
    else if(pnode->myType == PATH || pnode->myType == ROOT)
    {
        // Path
        //UTdebugPrint("Mod path", pnode->myID, pnode->myPath,
        //             pnode->myChildren.entries());
        for(auto cnode : pnode->myChildren)
            modified = selectionModified(cnode);
    }
    else if(pnode->myType == INSTANCE_REF)
    {
        //UTdebugPrint("Mod inst ref", pnode->myPath, pnode->myInstancerID);
        for(auto cnode : pnode->myChildren)
            modified = selectionModified(cnode);
        
        UT_ASSERT(pnode->myInstancerID != -1);
        modified = selectionModified(pnode->myInstancerID);
    }

    pnode->myRecurse = false;
    
    return modified;
}

void
HUSD_Scene::redoSelectionList()
{
    if(mySelectionArrayNeedsUpdate)
    {
        int64 serial = myGeoSerial + myLightSerial + myCamSerial;

        // Don't attempt to resolve missing selection paths unless
        // something actually changed (geometry, camera, or lights added).
        if(serial != mySelectionResolveSerial)
            setSelection(mySelectionArray);
    }
}

const UT_StringArray &
HUSD_Scene::getSelectionList()
{
    if(mySelectionID != mySelectionArrayID)
    {
        UT_StringMap<int> selected;
            
	mySelectionArray.clear();
	for(auto sel : mySelection)
	{
            const UT_StringRef &path = resolveID(sel.first, true);
            if(path.isstring() && selected.find(path) == selected.end())
            {
                selected[path]=1;
                mySelectionArray.append(path);
            }
	}
	mySelectionArrayID = mySelectionID;
        mySelectionArrayNeedsUpdate = false;
    }

    return mySelectionArray;
}

bool
HUSD_Scene::selectParents()
{
    UT_AutoLock lock(myDisplayLock);
    mySelectionSerial++;
    
    bool changed = false;
    UT_Map<int, int> selection;
    
    for(auto sel : mySelection)
    {
        const int id = sel.first;

        auto pnode = myTree->lookupID(id);
        if(pnode)
        {
            if(pnode->myType != INSTANCE)
            {
                if(pnode->myParent)
                {
                    int pid = pnode->myParent->myID;
                    auto emp = selection.emplace(pid,1);
                    if(emp.second)
                    {
                        selectionModified(id);
                        selectionModified(pid);
                        changed = true;
                    }
                }
            }
            else
            {
                const UT_StringRef &inst_id = myTree->resolveID(id);
                if(inst_id.countChar(' ') > 3) // nest_level > 2
                {
                    const int pidx = inst_id.lastCharIndex(' ');
                    UT_StringHolder parent_instance(inst_id.c_str(), pidx);
                    
                    const int fidx = inst_id.findCharIndex(' ');
                    const int lidx = inst_id.findCharIndex(' ', fidx+1);
                    UT_StringHolder proto(inst_id.c_str() + fidx, (lidx-fidx-1));
                    
                    const int new_id = pnode->addInstance(parent_instance,
                                                          proto,
                                                          myTree);
                    auto emp = selection.emplace(new_id,1);
                    if(emp.second)
                    {
                        selectionModified(id);
                        selectionModified(new_id);
                        changed = true;
                    }
                }
            }
        }
    }

    if(changed)
    {
        stashSelection();
        mySelection = selection;
        mySelectionID ++;
    }

    return changed;
}

bool
HUSD_Scene::selectChildren(bool all_children)
{
    UT_AutoLock lock(myDisplayLock);
    mySelectionSerial++;

    bool changed = false;
    UT_Map<int, int> selection;

    for(auto sel : mySelection)
    {
        const int id = sel.first;

        auto pnode = myTree->lookupID(id);
        if(pnode && pnode->myChildren.entries())
        {
            for(auto child : pnode->myChildren)
            {
                auto emp = selection.emplace(child->myID,1);
                if(emp.second)
                {
                    selectionModified(id);
                    selectionModified(child->myID);
                    changed = true;
                }
                if(!all_children)
                    break;
            }
        }
        else
        {
            // If no children, don't deselect. 
            selection.emplace(id,1);
        }
    }
    
    if(changed)
    {
        stashSelection();
        mySelection = selection;
        mySelectionID ++;
    }

    return changed;
}

bool
HUSD_Scene::selectSiblings(bool next_sibling)
{
    UT_AutoLock lock(myDisplayLock);
    mySelectionSerial++;
    
    bool changed = false;
    UT_Map<int, int> selection;

    for(auto sel : mySelection)
    {
        const int id = sel.first;

        auto pnode = myTree->lookupID(id);
        if(pnode->myParent)
        {
            int idx = -1;
            for(int i=0; i<pnode->myParent->myChildren.entries(); i++)
                if(pnode->myParent->myChildren(i) == pnode)
                {
                    idx = i;
                    break;
                }
            
            UT_ASSERT(idx!=-1);
            
            if(next_sibling)
            {
                idx++;
                if(pnode->myParent->myChildren.entries() == idx)
                    idx = 0;
            }
            else
            {
                if(idx == 0)
                    idx = pnode->myParent->myChildren.entries()-1;
                else
                    idx--;
            }

            const int sid = pnode->myParent->myChildren(idx)->myID;
            auto emp = selection.emplace(sid,1);
            if(emp.second)
            {
                selectionModified(id);
                selectionModified(sid);
                changed = true;
            }
        }
        else
            selection.emplace(id, 1);
    }

    if(changed)
    {
        stashSelection();
        mySelection = selection;
        mySelectionID ++;
    }

    return changed;
}


void
HUSD_Scene::addToHighlight(int id)
{
    if(myHighlight.find(id) == myHighlight.end())
    {
	auto emp = myHighlight.emplace(id, 1);
        //UTdebugPrint("Highlight", id);
        if(emp.second)
            myHighlightID++;
    }
}

    
void
HUSD_Scene::addPathToHighlight(const UT_StringRef &path)
{
    auto node = myTree->lookupPath(path);
    if(node)
    {
        const int id = node->myID;
    
        if(myHighlight.find(id) == myHighlight.end())
        {
            auto emp = myHighlight.emplace(id, 1);
            if(emp.second)
                myHighlightID++;
        }
    }
}


void
HUSD_Scene::clearHighlight()
{
    //UTdebugPrint("Clear highlight");
    if(myHighlight.size() > 0)
    {
	// for(auto entry : myHighlight)
	//     selectionModified(entry.first);
	myHighlight.clear();
	myHighlightID++;
    }
}

bool
HUSD_Scene::clearSelection()
{
    if(mySelection.size() > 0)
    {
        stashSelection();
        mySelectionSerial++;

	for(auto entry : mySelection)
	    selectionModified(entry.first);
	mySelection.clear();
	mySelectionArray.clear();
	mySelectionID++;
        return true;
    }

    return false;
}

void
HUSD_Scene::setHighlightAsSelection()
{
    //UTdebugPrint("Highlight", myHighlight.size());
    stashSelection();
    makeSelection(myHighlight, false);
}

void
HUSD_Scene::addHighlightToSelection()
{
    stashSelection();
    mySelectionSerial++;

    bool changed = false;
    for(auto entry : myHighlight)
	if(mySelection.find(entry.first) == mySelection.end())
	{
	    mySelection[entry.first] = entry.second;
	    selectionModified(entry.first);
	    changed = true;
	}
    if(changed)
	mySelectionID++;
}

void
HUSD_Scene::intersectHighlightWithSelection()
{
    stashSelection();
    mySelectionSerial++;

    UT_IntArray to_remove;
    for(auto entry : mySelection)
	if(myHighlight.find(entry.first) == myHighlight.end())
	{
	    to_remove.append(entry.first);
	    selectionModified(entry.first);
	}
    for(auto id : to_remove)
	mySelection.erase(id);

    if(to_remove.entries())
	mySelectionID++;
}

void
HUSD_Scene::removeHighlightFromSelection()
{
    stashSelection();
    mySelectionSerial++;

    bool changed = false;
    for(auto entry : myHighlight)
	if(mySelection.find(entry.first) != mySelection.end())
	{
	    mySelection.erase(entry.first);
	    selectionModified(entry.first);
	    changed =  true;
	}
    if(changed)
	mySelectionID++;
}

void
HUSD_Scene::toggleHighlightInSelection()
{
    stashSelection();
    mySelectionSerial++;

    for(auto entry : myHighlight)
    {
	if(mySelection.find(entry.first) != mySelection.end())
	    mySelection.erase(entry.first);
	else
	    mySelection[entry.first] = entry.second;
	selectionModified(entry.first);
    }
    
    if(myHighlight.size() > 0)
	mySelectionID++;
}

bool
HUSD_Scene::isSelected(const HUSD_HydraPrim *prim) const
{
    return isSelected(prim->id());
}

bool
HUSD_Scene::isSelected(int id) const
{
    if(mySelection.size() == 0 || id == -1)
	return false;

    if(mySelection.find(id) != mySelection.end())
	return true;

    UT_AutoLock lock(myDisplayLock);

    auto node = myTree->lookupID(id);
    auto inode = node;

    if(node && node->myType == INSTANCE_REF)
        node = myTree->lookupID(node->myInstancerID);
        
    if(node && node->myType == INSTANCER && node->myPrototypes)
    {
        // id is an instance belonging to an Instancer.
        for(auto &proto : *node->myPrototypes)
        {
            auto entry = proto.second->myIDPaths.find(id);
            if(entry != proto.second->myIDPaths.end())
            {
                const UT_StringRef &instance = entry->second;
                if(instance.startsWith(theQuestionMark))
                {
                    // If nested, check if higher instance levels are selected.
                    // Keep stripping off indices until the topmost instance is
                    // reached, checking if the instancer is selected at each
                    // level.
                    // Instances generated from Render Delegates (setRenderID())
                    // can only have 1 nesting level and don't start with ?. 
                    const int nest_level = instance.countChar(' ') -1;
                    for(int pass =1; pass<nest_level; pass++)
                    {
                        const int idx = instance.lastCharIndex(' ', pass);
                        if(idx >= 0)
                        {
                            UT_StringHolder inst_key(instance.c_str(), idx);
                            // check through all prototypes again because the
                            // instancer will have the parent as a prototype.
                            for(auto &iproto : *node->myPrototypes)
                            {
                                auto ientry =
                                    iproto.second->myInstances.find(inst_key);
                                if(ientry != iproto.second->myInstances.end())
                                {
                                    if(mySelection.find(ientry->second) !=
                                       mySelection.end())
                                    {
                                        return true;
                                    }
                                }
                            }
                        }
                        else
                            break;
                    }
                }
            }
        }
    }

    // Walk up through prims.
    while(inode)
    {
        inode = inode->myParent;
        if(inode && mySelection.find(inode->myID) != mySelection.end())
            return true;
    }
    return false;
}



bool
HUSD_Scene::isHighlighted(const HUSD_HydraPrim *prim) const
{
    return isHighlighted(prim->id());
}

bool
HUSD_Scene::isHighlighted(int id) const
{
    if(myHighlight.size() == 0)
	return false;

    if(myHighlight.find(id) != myHighlight.end())
	return true;

    UT_AutoLock lock(myDisplayLock);
    
    auto node = myTree->lookupID(id);
    if(node && node->myID != id)
    {
        // Instancer.
        
    }

    // Walk up through prims.
    while(node)
    {
        node = node->myParent;
        if(node && myHighlight.find(node->myID) != myHighlight.end())
            return true;
    }
    return false;
}

bool
HUSD_Scene::hasSelection() const
{
    return mySelection.size() > 0;
}

bool
HUSD_Scene::hasHighlight() const
{
    return myHighlight.size() > 0;
}

void
HUSD_Scene::setStage(const HUSD_DataHandle &data,
		     const HUSD_ConstOverridesPtr &overrides)
{
    myStage = data;
    myStageOverrides = overrides;
}

HUSD_PrimHandle
HUSD_Scene::getPrim(const UT_StringHolder &path) const
{
    return HUSD_PrimHandle(myStage, myStageOverrides,
        HUSD_PrimHandle::OVERRIDES_COMPOSE, path);
}

bool
HUSD_Scene::recallPrevSelection()
{
    if(myCurrentRecalledSelection)
    {
        if( myCurrentRecalledSelection->prev())
            myCurrentRecalledSelection = myCurrentRecalledSelection->prev();
        else // can't go back further.
            return false;
    }
    else
    {
        auto last = myStashedSelection.tail();
        stashSelection();
        myCurrentRecalledSelection = last;
    }

    if(!myCurrentRecalledSelection)
        return false;

    auto &selection =
        ((husd_StashedSelection*)myCurrentRecalledSelection)->selection;

    return makeSelection(selection, true);
}

bool
HUSD_Scene::recallNextSelection()
{
    if(!myCurrentRecalledSelection)
        return false;
    
    if( myCurrentRecalledSelection->next())
        myCurrentRecalledSelection = myCurrentRecalledSelection->next();
    else
        return false;

    auto &selection =
        ((husd_StashedSelection*)myCurrentRecalledSelection)->selection;
    
    return makeSelection(selection, true);
}

bool
HUSD_Scene::makeSelection(const UT_Map<int,int> &selection,
                          bool validate)
{
    mySelectionSerial++;

    // Determine any additional selections from instancers with multiple
    // prototypes
    UT_Map<int,int> extra_selection;
    for(auto entry : selection)
    {
        auto pnode = myTree->lookupID(entry.first);
        if(pnode->myType == INSTANCER &&
           pnode->myPrototypes &&
           pnode->myPrototypes->size() > 1)
        {
            auto &path = myTree->resolveID(entry.first);

            int fidx = path.findCharIndex(' ');
            if(fidx != -1)
            {
                int eidx = path.findCharIndex(' ', fidx+1);
                UT_StringView prefix(path.c_str(), fidx);
                UT_StringView suffix(path.c_str() + eidx+1, path.length()-eidx-1);
                UT_StringHolder pre(prefix);
                UT_StringHolder suf(suffix);
                UT_WorkBuffer inst_string;
                
                for(auto &proto : *pnode->myPrototypes)
                {
                    auto pnode = myTree->lookupPath(proto.first);
                    if(pnode)
                    {
                        inst_string.sprintf("%s %d %s",
                                            pre.c_str(), pnode->myID,
                                            suf.c_str());
                        auto ientry = proto.second->myInstances.
                            find(inst_string.buffer());
                        if(ientry != proto.second->myInstances.end())
                            extra_selection[ientry->second] = entry.second;
                    }
                }
            }
        }
    }
   
    // remove anything  not in the highlighted items
    UT_IntArray to_remove;
    for(auto entry : mySelection)
	if(selection.find(entry.first) == selection.end() &&
           extra_selection.find(entry.first) == extra_selection.end())
        {
            to_remove.append(entry.first);
            selectionModified(entry.first);
        }
    
    for(auto id : to_remove)
	mySelection.erase(id);
    
    bool changed = (to_remove.entries() > 0);
    
    // add anything not in the selected items

    //UTdebugPrint("#selected", selection.size());
    for(auto entry : selection)
	if(mySelection.find(entry.first) == mySelection.end())
	{
	    mySelection[entry.first] = entry.second;
	    selectionModified(entry.first);
            //UTdebugPrint("    selected", entry.first);
	    changed = true;
        }
    
    for(auto entry : extra_selection)
	if(mySelection.find(entry.first) == mySelection.end())
	{
	    mySelection[entry.first] = entry.second;
	    selectionModified(entry.first);
            //UTdebugPrint("    selected", entry.first);
	    changed = true;
        }

    // std::cerr << "Selection (" << mySelection.size() << "): ";
    // for(auto id : mySelection)
    //     std::cerr << id.first << " ";
    // std::cerr << "\n";
    
    if(changed)
        mySelectionID ++;

    return changed;
}


void
HUSD_Scene::stashSelection()
{
    if(mySelection.size() == 0 ||
       myCurrentSelectionStashed == mySelectionID)
        return;

    // If we're back a ways in the selection stash and a new one is added,
    // remove all the ones from the current recalled selection.
    if(myCurrentRecalledSelection)
    {
        UT_LinkNode *itr = myStashedSelection.tail();
        while(itr)
        {
            if(itr == myCurrentRecalledSelection)
                break;

            UT_LinkNode *prev = itr->prev();
            myStashedSelection.destroy(itr);
            itr = prev;

        }
        myCurrentRecalledSelection = nullptr;
    }
    
    const int64 size = mySelection.getMemoryUsage(true);

    // above the limit, remove the older selections.
    myStashedSelectionSizeB += size;
    while(myStashedSelectionSizeB > STASHED_SELECTION_MEM_LIMIT)
    {
        auto head = (husd_StashedSelection*) myStashedSelection.head();
        if(!head)
            break;
        
        myStashedSelectionSizeB -= head->selection.getMemoryUsage(true);
        myStashedSelection.destroy(head);
    }

    myStashedSelection.append(new husd_StashedSelection(mySelection) );
    myCurrentSelectionStashed = mySelectionID;
}

void
HUSD_Scene::clearStashedSelections()
{
    myCurrentSelectionStashed = 0;
    myCurrentRecalledSelection = nullptr;

    myStashedSelectionSizeB = 0;
    myStashedSelection.clear();
}

void
HUSD_Scene::addCategory(const UT_StringRef &name, LightCategory cat)
{
    UT_AutoLock lock(myCategoryLock);
    UT_StringMap<int> &map = (cat == CATEGORY_LIGHT) ? myLightLinkCategories
                                                     : myShadowLinkCategories;
    auto entry = map.find(name);
    if(entry == map.end())
        map[name] = 1;
    else
        entry->second++;
}

void
HUSD_Scene::removeCategory(const UT_StringRef &name, LightCategory cat)
{
    UT_AutoLock lock(myCategoryLock);
    UT_StringMap<int> &map = (cat == CATEGORY_LIGHT) ? myLightLinkCategories
                                                     : myShadowLinkCategories;
    auto entry = map.find(name);
    if(entry != map.end())
    {
        entry->second--;
        if(entry->second == 0)
            map.erase(name);
    }
}

bool
HUSD_Scene::isCategory(const UT_StringRef &name, LightCategory cat)
{
    // For now, this doesn't appear to be necessary.
    //UT_AutoLock lock(myCategoryLock);
    UT_StringMap<int> &map = (cat == CATEGORY_LIGHT) ? myLightLinkCategories
                                                     : myShadowLinkCategories;
    return (map.find(name) != map.end());
}

void
HUSD_Scene::pendingRemovalGeom(const UT_StringRef &path,
                               HUSD_HydraGeoPrimPtr prim)
{
    UT_ASSERT(myPendingRemovalGeom.find(path) == myPendingRemovalGeom.end());
    myPendingRemovalGeom[path] = prim;
}

HUSD_HydraGeoPrimPtr
HUSD_Scene::fetchPendingRemovalGeom(const UT_StringRef &path)
{
    auto entry = myPendingRemovalGeom.find(path);
    if(entry != myPendingRemovalGeom.end())
    {
        HUSD_HydraGeoPrimPtr geo = entry->second;
        myPendingRemovalGeom.erase(path);
        return geo;
    }
    
    return nullptr;
}

void
HUSD_Scene::clearPendingRemovalPrims()
{
    for(auto gprim : myPendingRemovalGeom)
    	removeGeometry(gprim.second.get());
    myPendingRemovalGeom.clear();

    for(auto cam : myPendingRemovalCamera)
        removeCamera(cam.second.get());
    myPendingRemovalCamera.clear();

    for(auto light : myPendingRemovalLight)
        removeLight(light.second.get());
    myPendingRemovalLight.clear();
    
    myDuplicateGeo.clear();
    myDuplicateCam.clear();
    myDuplicateLight.clear();
}
    
void
HUSD_Scene::pendingRemovalCamera(const UT_StringRef &path,
                                 HUSD_HydraCameraPtr prim)
{
    myPendingRemovalCamera[path] = prim;
}

HUSD_HydraCameraPtr
HUSD_Scene::fetchPendingRemovalCamera(const UT_StringRef &path)
{
    auto entry = myPendingRemovalCamera.find(path);
    if(entry != myPendingRemovalCamera.end())
    {
        HUSD_HydraCameraPtr cam = entry->second;
        myPendingRemovalCamera.erase(path);
        return cam;
    }
    return nullptr;
}

void
HUSD_Scene::pendingRemovalLight(const UT_StringRef &path,
                                HUSD_HydraLightPtr prim)
{
    myPendingRemovalLight[path] = prim;
}

HUSD_HydraLightPtr
HUSD_Scene::fetchPendingRemovalLight(const UT_StringRef &path)
{
    auto entry = myPendingRemovalLight.find(path);
    if(entry != myPendingRemovalLight.end())
    {
        HUSD_HydraLightPtr cam = entry->second;
        myPendingRemovalLight.erase(path);
        return cam;
    }
    return nullptr;
}

void
HUSD_Scene::addInstancer(const UT_StringRef &path,
                         PXR_NS::XUSD_HydraInstancer *inst)
{
    XUSD_HydraInstancer  *xinst = static_cast<XUSD_HydraInstancer *>(inst);
    {
        HUSD_AutoReadLock lock(myStage, myStageOverrides);
        HUSD_Info info(lock);
        HUSD_Path hpath(inst->GetId());
        UT_StringRef ipath(hpath.pathStr());
        inst->setIsPointInstancer(
            info.isPrimAtPath(ipath, "PointInstancer"_sh) );
    }

    //UTdebugPrint("New instancer", path, xinst->id());
    myTree->generatePath(path, xinst->id(), INSTANCER);
    myInstancers[path] = inst;
    myInstancerIDs[ inst->id() ] = inst;
}

void
HUSD_Scene::removeInstancer(const UT_StringRef &path)
{
    auto instr = myInstancers.find(path);
    if(instr != myInstancers.end())
    {
        auto &existing_refs = instr->second->instanceRefs();
        for(auto itr : existing_refs)
            myTree->removeInstanceRef(itr.first);

        int id = instr->second->id();
        myInstancers.erase(path);
        myInstancerIDs.erase(id);
    }
}

PXR_NS::XUSD_HydraInstancer *
HUSD_Scene::getInstancer(const UT_StringRef &path)
{
    auto entry = myInstancers.find(path);
    if(entry != myInstancers.end())
        return entry->second;
    return nullptr;
}

void
HUSD_Scene::clearInstances(int instr_id, const UT_StringRef &proto_id)
{
    UT_AutoLock lock(myDisplayLock);
    
    auto pnode = myTree->lookupID(instr_id);
    if(pnode && pnode->myPrototypes)
    {
        pnode->myPrototypes->erase(proto_id);
        if(pnode->myPrototypes->size() == 0)
        {
            delete pnode->myPrototypes;
            pnode->myPrototypes = nullptr;
        }
    }
}

void
HUSD_Scene::updateInstanceRefPrims()
{
    //UTdebugPrint("Update Instancers", myInstancers.size());
    for(auto itr : myInstancers)
    {
        auto xinst = UTverify_cast<XUSD_HydraInstancer *>(itr.second);
        if(xinst->isResolved() || xinst->isPointInstancer())
            continue;

        const bool had_refs = xinst->invalidateInstanceRefs();
        
        auto pnode = myTree->lookupID(xinst->id());
        if(pnode && pnode->myPrototypes)
        {
            for(auto &proto : *pnode->myPrototypes)
            {
                auto &instances = proto.second->myInstances;
                for(auto itr : instances)
                    instanceIDLookup(itr.first, itr.second);
            }
        }

        if(had_refs)
        {
            UT_IntArray to_remove;
            auto &existing_refs = xinst->instanceRefs();
            for(auto itr : existing_refs)
                if(itr.second == 0)
                    to_remove.append(itr.first);

            for(int id : to_remove)
            {
                myTree->removeInstanceRef(id);
                xinst->removeInstanceRef(id);
            }
        }
        
        xinst->resolved();
    }
}

UT_StringHolder
HUSD_Scene::resolveID(int id, bool allow_instances) const
{
    const UT_StringRef &path = myTree->resolveID(id);
    if(path.startsWith(theQuestionMark))
    {
        if(allow_instances)
            return instanceIDLookup(path, id);
        else
        {
            UT_StringHolder ipath = instanceIDLookup(path, id);

            const int idx = ipath.findCharIndex(ipath);
            if(idx == -1)
                return ipath;

            UT_StringHolder iprim(ipath.c_str(), idx);
            return iprim;
        }
    }

    return path;
}

UT_StringHolder 
HUSD_Scene::instanceIDLookup(const UT_StringRef &pick_path, int pick_id) const
{
    UT_ASSERT(pick_path.c_str()[0] == '?');
    UT_String pid(pick_path.c_str(), true);
    UT_WorkArgs parts;
    
    pid.tokenize(parts, ' ');

    int ipath_id = SYSatoi32(parts(0)+1);
    
    auto entry = myInstancerIDs.find(ipath_id);
    if(entry != myInstancerIDs.end())
    {
        auto &&instancer = entry->second;
        const UT_StringRef &cached =
            instancer->getCachedResolvedInstance(pick_path);
        if(cached.isstring())
        {
            auto iref = myTree->lookupPath(cached);
            if(iref)
                instancer->addInstanceRef(iref->myID);
            return cached;
        }
        UT_IntArray indices;
        for(int i=parts.entries()-1; i>=2; i--)
        {
            UT_String num(parts(i));
            indices.append(num.toInt());
        }

        //UTdebugPrint("Convert pick ID", pick_path);
        int proto_id = SYSatoi32(parts(1));
        UT_StringArray results = instancer->resolveInstance(proto_id, indices);
        bool first = true;
        for(auto itr = results.rbegin(); itr!=results.rend(); ++itr)
        {
            auto &result = *itr;
            instancer->cacheResolvedInstance(pick_path, result);

            if(result.findCharIndex('[') == -1)
            {
                // Instanceable reference.
                // UTdebugPrint("Instanceable reference",
                //              instancer_path,
                //              instander->id());
                auto iref = myTree->lookupPath(result);
                if(!iref || iref->myType != HUSD_Scene::INSTANCE_REF)
                {
                    int id = first ? pick_id : -1;
                    id = myTree->addInstanceRef(id, result, instancer->id());
                    if (id >= 0)
                        instancer->addInstanceRef(id);
                }
                else
                    instancer->addInstanceRef(iref->myID);

            }
            first = false;
        }
        if(results.entries())
            return results.last();
    }
    
    return UT_StringHolder();
}


void
HUSD_Scene::postUpdate()
{
    processConsolidatedMeshes(true);
    updateInstanceRefPrims();
    clearPendingRemovalPrims();
}

void
HUSD_Scene::adjustAperture(fpreal &apv, fpreal caspect, fpreal iaspect)
{
    XUSD_RenderSettings::HUSD_AspectConformPolicy xpolicy =
        XUSD_RenderSettings::HUSD_AspectConformPolicy::EXPAND_APERTURE;

    if(myConformPolicy == CROP_APERTURE)
        xpolicy=XUSD_RenderSettings::HUSD_AspectConformPolicy::CROP_APERTURE;
    else if(myConformPolicy == ADJUST_HORIZONTAL_APERTURE)
        xpolicy=XUSD_RenderSettings::HUSD_AspectConformPolicy::ADJUST_HAPERTURE;
    else if(myConformPolicy == ADJUST_VERTICAL_APERTURE)
        xpolicy=XUSD_RenderSettings::HUSD_AspectConformPolicy::ADJUST_VAPERTURE;
    else if(myConformPolicy == ADJUST_PIXEL_ASPECT)
    {
        // The viewport will stretch the image to fit the camera area by
        // default.
        return;
    }


    fpreal par = 1.0; // Don't care about this.
    XUSD_RenderSettings::aspectConform(xpolicy, apv, par, caspect, iaspect);
}
