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

#include "HUSD_Constants.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Info.h"
#include "HUSD_HydraGeoPrim.h"
#include "HUSD_HydraCamera.h"
#include "HUSD_HydraLight.h"
#include "HUSD_HydraMaterial.h"
#include "HUSD_HydraField.h"
#include "HUSD_Path.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_HydraCamera.h"
#include "XUSD_HydraGeoPrim.h"
#include "XUSD_HydraInstancer.h"
#include "XUSD_HydraLight.h"
#include "XUSD_HydraMaterial.h"
#include "XUSD_RenderSettings.h"
#include "XUSD_ViewerDelegate.h"
#include "XUSD_Utils.h"

#include <GT/GT_AttributeList.h>
#include <GT/GT_CatPolygonMesh.h>
#include <GT/GT_DAConstantValue.h>
#include <GT/GT_Names.h>
#include <GT/GT_PrimInstance.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_Transform.h>
#include <GT/GT_TransformArray.h>
#include <GT/GT_Util.h>
#include <CH/CH_Manager.h>

#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/imaging/hd/camera.h>

#include <UT/UT_Array.h>
#include <UT/UT_Assert.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Lock.h>
#include <UT/UT_String.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_WorkBuffer.h>
#include <SYS/SYS_ParseNumber.h>
#include <iostream>

using namespace UT::Literal;
PXR_NAMESPACE_USING_DIRECTIVE

// 10MB
#define STASHED_SELECTION_MEM_LIMIT exint(10*1024*1024)

static HUSD_Scene *theCurrentScene = nullptr;
static int theGeoIndex = 0;
static UT_IntArray theFreeGeoIndex;

static constexpr UT_StringLit theViewportPrimTokenL("__viewport_settings__");
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
                             const HUSD_Path &path,
                             const GT_DataArrayHandle &sel,
                             const UT_BoundingBox &bbox)
        : HUSD_HydraGeoPrim(scene, path, true), // consolidated
          mySelection(sel)
        {
            myTransform = new GT_TransformArray();
            myMinPrimID = 0;
            myMaxPrimID = 0;
            myValidFlag = false;
            
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
    void setMaterial(const HUSD_Path &path)
        {
            myMaterial.forcedRef(0) = path;
        }

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

    const UT_Array<HUSD_Path> &materials() const override { return myMaterial; }
    bool isValid() const override { return myValidFlag; }

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
    UT_Array<HUSD_Path>     myMaterial;
    UT_BoundingBox          myBBox;
    int                     myMinPrimID;
    int                     myMaxPrimID;
    bool                    myValidFlag;
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
    UT_Map<uint64, uint64> myPrimBucketMap;
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

    // If the prim already exists it a different bucket, remove it from there
    // first. This can happen because of a material assignment change, attribute
    // add/remove, winding order change, etc. without the rprim being removed and
    // readded.
    auto prev_entry = myPrimBucketMap.find(prim_id);
    if(prev_entry != myPrimBucketMap.end() && prev_entry->first != bucket)
        if(myBuckets[prev_entry->second].removePrim(prim_id))
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
        UT_IntArray prim_ids;
        if(mesh->getUniformAttributes() &&
           mesh->getUniformAttributes()->get("__instances"))
        {
            // This is an mesh of instances.
            auto ids = mesh->getUniformAttributes()->
                get(GT_Names::lop_pick_id);

            UT_Set<int> idset;
            for(int i=0; i<ids->entries(); i++)
            {
                const int id = ids->getI32(i);
                if(idset.emplace(id).second)
                    prim_ids.append(id);
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
            myIBBoxList.entries(0);
            for(auto &itr : myPrimIDs)
            {
                prim_ids.append(itr.first);
                myIBBoxList.append(myBBox(itr.second));
            }
            //UTdebugPrint("PrimIDs = ", prim_ids);
        }

        if(!myPrimGroup)
        {
            HUSD_Path mat_name = scene.lookupMaterial(mat_id);
            UT_WorkBuffer name;

            int index = theUniqueConPrimIndex.exchangeAdd(1);
            name.sprintf("/__consolidated%d__", index);

            auto gprim = new husd_ConsolidatedGeoPrim(scene, mesh, mat_id,
                                                      HUSD_Path(name.buffer()),
                                                      mySelectionInfo,
                                                      UT_BoundingBox(box));
            gprim->setRenderTag(tag);
            gprim->setMaterial(mat_name);
            gprim->setValid(true);
            gprim->setPrimIDs(prim_ids);
            myPrimGroup = gprim;
        }
        else
        {
            auto gprim=static_cast<husd_ConsolidatedGeoPrim*>(myPrimGroup.get());
            gprim->setMesh(mesh, UT_BoundingBox(box));
            gprim->dirty(HUSD_HydraGeoPrim::husd_DirtyBits(myDirtyBits));
            gprim->setPrimIDs(prim_ids);
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
        myPrimGroup = nullptr;
        mySelectionInfo = nullptr;
        myEmptySlots.entries(0);
        myPolyMerger.clearAllMeshes();
        myBBox.entries(0);
        myInstanceBBox.entries(0);
        myIBBoxList.entries(0);
        myActiveFlag = false;
    }
    myDirtyBits = 0;
}


class husd_StashedSelection : public UT_LinkNode
{
public:
    husd_StashedSelection(const UT_StringSet &s) : mySelection(s) {}
    
    UT_StringSet mySelection;
};

// ---------------------------------------------------------------------------

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
    : myGeoSerial(0),
      myCamSerial(0),
      myLightSerial(0),
      mySelectionID(0),
      myDeferUpdate(false),
      myRenderIndex(nullptr),
      myRenderParam(nullptr),
      myCurrentRecalledSelection(nullptr),
      myStashedSelectionSizeB(0),
      myCurrentSelectionStashed(0),
      myRenderPrimRes(0,0),
      myConformPolicy(EXPAND_APERTURE),
      myPrimCount(0)
{
    myPrimConsolidator = new husd_ConsolidatedPrims(*this);
}

HUSD_Scene::~HUSD_Scene()
{
    delete myPrimConsolidator;
}

void
HUSD_Scene::addHydraPrim(HUSD_HydraPrim *prim)
{
    UT_AutoLock lock(myIdMapLock);
    myIdToPrimMap.emplace(prim->id(), prim);
}

void
HUSD_Scene::removeHydraPrim(HUSD_HydraPrim *prim)
{
    UT_AutoLock lock(myIdMapLock);
    myIdToPrimMap.erase(prim->id());
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
        addHydraPrim(geo);
    }
}

void
HUSD_Scene::removeGeometry(HUSD_HydraGeoPrim *geo)
{
    if(geo->index() >= 0)
	removeDisplayGeometry(geo);

    myGeometry.erase(geo->geoID());
    removeHydraPrim(geo);
}

void
HUSD_Scene::addDisplayGeometry(HUSD_HydraGeoPrim *geo)
{
    UT_AutoLock lock(myDisplayLock);
    myGeoSerial++;
    
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
}

void
HUSD_Scene::removeDisplayGeometry(HUSD_HydraGeoPrim *geo)
{
    UT_AutoLock lock(myDisplayLock);
    myGeoSerial++;

    geometryDisplayed(geo, false);
    
    theFreeGeoIndex.append(geo->index());
    myDisplayGeometry.erase(geo->geoID());
    
    geo->setIndex(-1);
}

bool
HUSD_Scene::fillGeometry(UT_Array<HUSD_HydraGeoPrimPtr> &array, int64 &id)
{
    UT_AutoLock lock(myDisplayLock);
    
    // avoid needlessly refilling the array if it hasn't changed.
    if(id == myGeoSerial)
        return false;

    array.entries(0);
    
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
HUSD_Scene::addField(HUSD_HydraField *field)
{
    UT_AutoLock lock(myFieldLock);
    myFields[ field->path() ] = field;
}

void
HUSD_Scene::removeField(HUSD_HydraField *field)
{
    UT_AutoLock lock(myFieldLock);
    myFields.erase( field->path() );
}

void
HUSD_Scene::addCamera(HUSD_HydraCamera *cam, bool new_cam)
{
    UT_AutoLock lock(myLightCamLock);
    auto entry = myCameras.find(cam->path());
    if(entry != myCameras.end())
        myDuplicateCam.append(entry->second);
    
    myCameras[ cam->path() ] = cam;
    addHydraPrim(cam);
    if(new_cam)
        myCamSerial++;
}

void
HUSD_Scene::removeCamera(HUSD_HydraCamera *cam)
{
    UT_AutoLock lock(myLightCamLock);
    myCameras.erase( cam->path() );
    removeHydraPrim(cam);
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
    addHydraPrim(light);
    if(new_light)
        myLightSerial++;
}

void
HUSD_Scene::removeLight(HUSD_HydraLight *light)
{
    UT_AutoLock lock(myLightCamLock);
    myLights.erase( light->path() );
    removeHydraPrim(light);
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

    id = myLightSerial;
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

HUSD_Path
HUSD_Scene::lookupMaterial(int id) const
{
    auto entry = myMaterialIDs.find(id);
    if(entry != myMaterialIDs.end())
        return entry->second;

    static HUSD_Path theNullPath;
    return theNullPath;
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
    myPrimConsolidator->add(mesh, bbox, prim_id, mat_id, dirty_bits,
        tag, lefthand, auto_nml, instance_bbox, instancer_id);
}

void
HUSD_Scene::removeConsolidatedPrim(int id)
{
    myPrimConsolidator->remove(id);
}

void
HUSD_Scene::processConsolidatedMeshes(bool finalize)
{
    myPrimConsolidator->processBuckets(finalize);
}

HUSD_Scene::PrimType
HUSD_Scene::getPrimType(int id) const
{
    {
        UT_AutoLock lock(myInstanceIdMapLock);
        if (myInstanceIdToRenderKeyMap.size() > id &&
            myInstanceIdToRenderKeyMap[id].myPickId != -1)
            return INSTANCE;
    }

    {
        UT_AutoLock lock(myIdMapLock);
        auto primit = myIdToPrimMap.find(id);
        if (primit != myIdToPrimMap.end())
        {
            if (myLights.contains(primit->second->path()))
                return LIGHT;
            if (myCameras.contains(primit->second->path()))
                return CAMERA;
            if (myGeometry.contains(primit->second->path()))
                return GEOMETRY;
        }
    }

    return INVALID_TYPE;
}

HUSD_RenderKey
HUSD_Scene::getRenderKey(int id, HUSD_Path &light_cam_path) const
{
    if(id < 0)
        return HUSD_RenderKey();
    
    light_cam_path = HUSD_Path();
    if (myInstanceIdToRenderKeyMap.size() > id &&
        myInstanceIdToRenderKeyMap[id].myPickId != -1)
        return myInstanceIdToRenderKeyMap[id];

    auto it = myIdToPrimMap.find(id);
    if (it != myIdToPrimMap.end())
    {
        XUSD_HydraGeoPrim *geoprim =
            dynamic_cast<XUSD_HydraGeoPrim *>(it->second);
        if (geoprim)
            return HUSD_RenderKey(geoprim->rprim()->GetPrimId());
        else if (dynamic_cast<HUSD_HydraCamera *>(it->second) ||
                 dynamic_cast<HUSD_HydraLight *>(it->second))
            light_cam_path = it->second->path();
    }

    return HUSD_RenderKey();
}

UT_IntArray
HUSD_Scene::getOrCreateInstanceIds(int primid, int numinst)
{
    UT_IntArray result;
    UT_AutoLock lock(myInstanceIdMapLock);

    result.setSize(numinst);
    HUSD_RenderKey key(primid);
    auto it = myRenderKeyToInstanceIdsMap.find(key);
    if (it == myRenderKeyToInstanceIdsMap.end())
        it = myRenderKeyToInstanceIdsMap.emplace(key, UT_IntArray()).first;
    it->second.setCapacityIfNeeded(numinst);
    for (int instid = 0; instid < numinst; instid++)
    {
        if (instid >= it->second.size())
        {
            HUSD_RenderKey key(primid, instid);
            int newid = HUSD_HydraPrim::newUniqueId();
            myInstanceIdToRenderKeyMap.forcedRef(newid) = key;
            it->second.append(newid);
        }
        result[instid] = it->second[instid];
    }

    return result;
}

const HUSD_PathSet &
HUSD_Scene::volumesUsingField(const HUSD_Path &field) const
{
    static const HUSD_PathSet theEmptySet;
    auto it = myFieldsInVolumes.find(field);

    if (it != myFieldsInVolumes.end())
	return it->second;

    return theEmptySet;
}

void
HUSD_Scene::addVolumeUsingField(const HUSD_Path &volume,
	const HUSD_Path &field)
{
    myFieldsInVolumes[field].insert(volume);
}

void
HUSD_Scene::removeVolumeUsingFields(const HUSD_Path &volume)
{
    for (auto &&volumes : myFieldsInVolumes)
	volumes.second.erase(volume);
}

bool
HUSD_Scene::removeInstanceSelections()
{
    bool changed = false;
    for (auto it = mySelection.begin(); it != mySelection.end();)
    {
        if (it->endsWith("]"))
        {
            it = mySelection.erase(it);
            changed = true;
        }
        else
            ++it;
    }
    if (changed)
        mySelectionID++;
    return changed;
}

bool
HUSD_Scene::removePrimSelections()
{
    bool changed = false;
    for (auto it = mySelection.begin(); it != mySelection.end();)
    {
        if (!it->endsWith("]"))
        {
            it = mySelection.erase(it);
            changed = true;
        }
        else
            ++it;
    }
    if (changed)
        mySelectionID++;
    return changed;
}

bool
HUSD_Scene::setSelectionPaths(const HUSD_PathSet &paths,
        const UT_StringSet &pathswithinstanceids,
        bool stash_selection)
{
    UT_StringSet pathset(pathswithinstanceids);
    for (auto &&path : paths)
        pathset.insert(path.pathStr());
    return setSelectionPaths(pathset, stash_selection);
}

bool
HUSD_Scene::setSelectionPaths(const UT_StringArray &paths,
        bool stash_selection)
{
    UT_StringSet pathset;
    pathset.insert(paths.begin(), paths.end());
    return setSelectionPaths(pathset, stash_selection);
}

bool
HUSD_Scene::setSelectionPaths(const UT_StringSet &paths,
        bool stash_prev_selection)
{
    if (paths != mySelection)
    {
        if (stash_prev_selection)
            stashSelection();

        mySelection = paths;
        mySelectionID++;

        return true;
    }

    return false;
}

bool
HUSD_Scene::selectParents()
{
    UT_StringSet newsel;
    for (auto &&path : mySelection)
    {
        if (path.endsWith("]"))
        {
            UT_StringHolder primpath(path.c_str(), path.findCharIndex('['));
            newsel.insert(primpath);
        }
        else
        {
            SdfPath sdfpath = HUSDgetSdfPath(path);
            newsel.insert(sdfpath.GetParentPath().GetString());
        }
    }

    return setSelectionPaths(newsel);
}

bool
HUSD_Scene::selectChildren(bool all_children)
{
    HUSD_AutoReadLock lock(myStage, myStageOverrides, myStagePostLayers);
    HUSD_Info info(lock);
    UT_StringSet newsel;
    for (auto &&path : mySelection)
    {
        if (!path.endsWith("]"))
        {
            SdfPath sdfpath = HUSDgetSdfPath(path);
            UT_StringArray children;

            info.getChildren(path, children);
            if (!children.isEmpty())
            {
                for (auto &&child : children)
                {
                    SdfPath sdfchild =
                        sdfpath.AppendChild(TfToken(child.toStdString()));
                    if (!lock.constData()->stage()->
                         GetPrimAtPath(sdfchild).IsA<UsdGeomImageable>())
                        continue;
                    newsel.insert(sdfchild.GetString());
                    if (!all_children)
                        break;
                }
            }
        }
    }

    return setSelectionPaths(newsel);
}

bool
HUSD_Scene::selectSiblings(bool next_sibling)
{
    HUSD_AutoReadLock lock(myStage, myStageOverrides, myStagePostLayers);
    HUSD_Info info(lock);
    UT_StringSet newsel;
    for (auto &&path : mySelection)
    {
        if (path.endsWith("]"))
        {
            int numpartidx = path.lastCharIndex('[');
            UT_StringHolder pathpart(path.c_str(), numpartidx);
            UT_StringHolder numstr(path.c_str() + numpartidx + 1,
                                   path.length() - numpartidx - 1);
            int instidx = SYSatoi(numstr.c_str()) + (next_sibling ? 1 : -1);
            // Can't accurately find siblings of nested instances.
            if (!pathpart.endsWith("]"))
            {
                int numinst = info.getPointInstancerInstanceCount(pathpart,
                    HUSD_TimeCode(CHgetSampleFromTime(CHgetEvalTime())));
                if (instidx >= numinst)
                    instidx = 0;
                else if (instidx < 0)
                    instidx = numinst - 1;
            }
            else if (instidx < 0)
                instidx = 0;
            UT_StringHolder newpath;
            newpath.sprintf("%s[%d]", pathpart.c_str(), instidx);
            newsel.insert(newpath);
        }
        else
        {
            SdfPath sdfpath = HUSDgetSdfPath(path);
            SdfPath sdfparentpath = sdfpath.GetParentPath();
            std::string name = sdfpath.GetName();
            UT_StringArray children;

            info.getChildren(sdfparentpath.GetString(), children);
            if (children.size() < 2)
            {
                newsel.insert(path);
                continue;
            }
            for (int i = 0, n = children.size(); i < n; ++i)
            {
                if (children[i] == name)
                {
                    for (int delta = 1; delta <= n; delta++)
                    {
                        int isib = 0;
                        if (next_sibling)
                            isib = (i + delta) % n;
                        else
                            isib = (i + n - delta) % n;
                        SdfPath sdfsibling = sdfparentpath.AppendChild(
                            TfToken(children[isib].toStdString()));
                        if (!lock.constData()->stage()->
                             GetPrimAtPath(sdfsibling).IsA<UsdGeomImageable>())
                            continue;
                        newsel.insert(sdfsibling.GetString());
                        break;
                    }
                    break;
                }
            }
        }
    }

    return setSelectionPaths(newsel);
}

bool
HUSD_Scene::clearSelection()
{
    if(mySelection.size() > 0)
    {
        stashSelection();
        mySelection.clear();
        mySelectionID++;
        return true;
    }

    return false;
}

bool
HUSD_Scene::isSelected(const HUSD_Path &path) const
{
    return isSelected(path.pathStr());
}

bool
HUSD_Scene::isSelected(const UT_StringRef &path) const
{
    return mySelection.contains(path);
}

bool
HUSD_Scene::hasSelection() const
{
    return !mySelection.empty();
}

void
HUSD_Scene::setStage(const HUSD_DataHandle &data,
		     const HUSD_ConstOverridesPtr &overrides,
		     const HUSD_ConstPostLayersPtr &postlayers)
{
    myStage = data;
    myStageOverrides = overrides;
    myStagePostLayers = postlayers;
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
        ((husd_StashedSelection*)myCurrentRecalledSelection)->mySelection;

    return setSelectionPaths(selection, false);
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
        ((husd_StashedSelection*)myCurrentRecalledSelection)->mySelection;
    
    return setSelectionPaths(selection, false);
}

void
HUSD_Scene::stashSelection()
{
    if(mySelection.empty() ||
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
        
        myStashedSelectionSizeB -= head->mySelection.getMemoryUsage(true);
        myStashedSelection.destroy(head);
    }

    myStashedSelection.append(new husd_StashedSelection(mySelection) );
    myCurrentSelectionStashed = mySelectionID;
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
HUSD_Scene::pendingRemovalGeom(const HUSD_Path &path,
                               HUSD_HydraGeoPrimPtr prim)
{
    UT_ASSERT(myPendingRemovalGeom.find(path) == myPendingRemovalGeom.end());
    myPendingRemovalGeom[path] = prim;
    prim->setPendingDelete(true);
}

HUSD_HydraGeoPrimPtr
HUSD_Scene::fetchPendingRemovalGeom(const HUSD_Path &path,
                                    const UT_StringRef &prim_type)
{
    auto entry = myPendingRemovalGeom.find(path);
    if(entry != myPendingRemovalGeom.end())
    {
        auto xprim = static_cast<PXR_NS::XUSD_HydraGeoPrim*>(entry->second.get());
        if(xprim->primType().GetText() == prim_type)
        {
            HUSD_HydraGeoPrimPtr geo = entry->second;
            myPendingRemovalGeom.erase(path);
            geo->setPendingDelete(false);
            return geo;
        }
        else
        {
            // We found some pending geometry, but it's the wrong type, so we
            // have to create new geometry. We have to remove the pending
            // geometry now or else when we get around to cleaning up the
            // pending geometry, we'll end up removing the _new_ geometry,
            // resulting in stale pointers and crashes.
            removeGeometry(xprim);
            myPendingRemovalGeom.erase(entry);
        }
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

    for(auto inst : myPendingRemovalInstancer)
        delete inst.second;
    myPendingRemovalInstancer.clear();
    
    myDuplicateGeo.clear();
    myDuplicateCam.clear();
    myDuplicateLight.clear();
}
    
void
HUSD_Scene::pendingRemovalCamera(const HUSD_Path &path,
                                 HUSD_HydraCameraPtr prim)
{
    myPendingRemovalCamera[path] = prim;
    prim->setPendingDelete(true);
}

HUSD_HydraCameraPtr
HUSD_Scene::fetchPendingRemovalCamera(const HUSD_Path &path)
{
    auto entry = myPendingRemovalCamera.find(path);
    if(entry != myPendingRemovalCamera.end())
    {
        HUSD_HydraCameraPtr cam = entry->second;
        myPendingRemovalCamera.erase(path);
        cam->setPendingDelete(false);
        return cam;
    }
    return nullptr;
}

void
HUSD_Scene::pendingRemovalLight(const HUSD_Path &path,
                                HUSD_HydraLightPtr prim)
{
    myPendingRemovalLight[path] = prim;
    prim->setPendingDelete(true);
}

HUSD_HydraLightPtr
HUSD_Scene::fetchPendingRemovalLight(const HUSD_Path &path)
{
    auto entry = myPendingRemovalLight.find(path);
    if(entry != myPendingRemovalLight.end())
    {
        HUSD_HydraLightPtr light = entry->second;
        myPendingRemovalLight.erase(path);
        light->setPendingDelete(true);
        return light;
    }
    return nullptr;
}

XUSD_HydraInstancer *
HUSD_Scene::fetchPendingRemovalInstancer(const HUSD_Path &path)
{
    auto entry = myPendingRemovalInstancer.find(path);
    if(entry != myPendingRemovalInstancer.end())
    {
        XUSD_HydraInstancer *inst = entry->second;
        myPendingRemovalInstancer.erase(path);
        return inst;
    }
    return nullptr;
}

void
HUSD_Scene::pendingRemovalInstancer(const HUSD_Path &path,
                                    XUSD_HydraInstancer *inst)
{
    myPendingRemovalInstancer[path] = inst;
}


bool
HUSD_Scene::isCamera(const UT_StringRef &path) const
{
    HUSD_AutoReadLock lock(myStage, myStageOverrides, myStagePostLayers);
    HUSD_Info info(lock);

    return (info.isPrimType(path, HUSD_Constants::getGeomCameraPrimType()));
}

bool
HUSD_Scene::isLight(const UT_StringRef &path) const
{
    HUSD_AutoReadLock lock(myStage, myStageOverrides, myStagePostLayers);
    HUSD_Info info(lock);

    return (info.hasPrimAPI(path, HUSD_Constants::getLuxLightAPIName()));
}

void
HUSD_Scene::addInstancer(const HUSD_Path &path,
                         PXR_NS::XUSD_HydraInstancer *inst)
{
    myInstancers[path] = inst;
}

void
HUSD_Scene::removeInstancer(const HUSD_Path &path)
{
    myInstancers.erase(path);
}

PXR_NS::XUSD_HydraInstancer *
HUSD_Scene::getInstancer(const HUSD_Path &path)
{
    auto entry = myInstancers.find(path);
    if(entry != myInstancers.end())
        return entry->second;

    return nullptr;
}

void
HUSD_Scene::postUpdate()
{
    processConsolidatedMeshes(true);
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
