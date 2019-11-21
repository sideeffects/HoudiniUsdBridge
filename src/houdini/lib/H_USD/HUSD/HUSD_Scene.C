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
 * NAME:	HUSD_SceneGraphDelegate.C (HUSD Library, C++)
 *
 * COMMENTS:	Scene info for the native Houdini viewport renderer
 */
#include "HUSD_Scene.h"

#include "HUSD_HydraGeoPrim.h"
#include "HUSD_HydraCamera.h"
#include "HUSD_HydraLight.h"
#include "HUSD_HydraMaterial.h"

#include "XUSD_SceneGraphDelegate.h"
#include "XUSD_HydraCamera.h"
#include "XUSD_HydraGeoPrim.h"
#include "XUSD_HydraMaterial.h"

#include "HUSD_DataHandle.h"
#include "HUSD_PrimHandle.h"

#include <pxr/imaging/hd/camera.h>

#include <UT/UT_Assert.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Lock.h>
#include <UT/UT_String.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_WorkBuffer.h>

#include <UT/UT_StackTrace.h>
#include <iostream>
#define NO_HIGHLIGHT   0
#define LEAF_HIGHLIGHT 1
#define PATH_HIGHLIGHT 2

// 10MB
#define STASHED_SELECTION_MEM_LIMIT exint(10*1024*1024)

static HUSD_Scene *theCurrentScene = nullptr;
static int theGeoIndex = 0;
static UT_IntArray theFreeGeoIndex;

class husd_StashedSelection : public UT_LinkNode
{
public:
    husd_StashedSelection(const UT_Map<int,int> &s) : selection(s) {}
    
    UT_Map<int,int> selection;
};

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

PXR_NS::XUSD_SceneGraphDelegate *
HUSD_Scene::newDelegate()
{
    UT_ASSERT_P(theCurrentScene);
    return new PXR_NS::XUSD_SceneGraphDelegate(*theCurrentScene);
}

void
HUSD_Scene::freeDelegate(PXR_NS::XUSD_SceneGraphDelegate *del)
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
      myRenderPrimRes(0,0)
{
}

HUSD_Scene::~HUSD_Scene()
{
}

void
HUSD_Scene::addGeometry(HUSD_HydraGeoPrim *geo)
{
    myGeometry[ geo->geoID() ] = geo;
}

void
HUSD_Scene::removeGeometry(HUSD_HydraGeoPrim *geo)
{
    if(geo->index() >= 0)
	removeDisplayGeometry(geo);
    
    myGeometry.erase(geo->geoID());
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

    myDisplayGeometry[ geo->geoID() ] = geo;
    myNameIDLookup[ geo->id() ] = { geo->path(), GEOMETRY };

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
    myNameIDLookup.erase( geo->id() );
    
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
HUSD_Scene::addCamera(HUSD_HydraCamera *cam)
{
    UT_AutoLock lock(myLightCamLock);
    myCameras[ cam->path() ] = cam;
    myNameIDLookup[ cam->id() ] = { cam->path(), CAMERA };
    myCamSerial++;
}

void
HUSD_Scene::removeCamera(HUSD_HydraCamera *cam)
{
    UT_AutoLock lock(myLightCamLock);
    myNameIDLookup.erase( cam->id() );
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
HUSD_Scene::addLight(HUSD_HydraLight *light)
{
    UT_AutoLock lock(myLightCamLock);
    myLights[ light->path() ] = light;
    myNameIDLookup[ light->id() ] = { light->path(), LIGHT };
    myLightSerial++;
}

void
HUSD_Scene::removeLight(HUSD_HydraLight *light)
{
    UT_AutoLock lock(myLightCamLock);
    myNameIDLookup.erase( light->id() );
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
    myNameIDLookup[ mat->id() ] = { mat->path(), MATERIAL };
}

void
HUSD_Scene::removeMaterial(HUSD_HydraMaterial *mat)
{
    UT_AutoLock lock(myMaterialLock);
    myNameIDLookup.erase( mat->id() );
    myMaterials.erase( mat->path() );
}

const UT_StringRef &
HUSD_Scene::lookupPath(int id) const
{
    static UT_StringHolder theNullString;
    
    auto entry = myNameIDLookup.find(id);
    if(entry != myNameIDLookup.end())
	return entry->second.myFirst;

    return theNullString;
}

int64
HUSD_Scene::getMaterialID(const UT_StringRef &path)
{
    static UT_Lock theMatIDLock;
    static int64 theMatIDIndex = 1;

    UT_AutoLock lock(theMatIDLock);

    int64 id = -1;
    auto entry = myMatIDs.find(path);
    if(entry != myMatIDs.end())
	id = entry->second;
    else
    {
	id = theMatIDIndex;
	myMatIDs[path] = id;
    }

    return id;
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


HUSD_Scene::PrimType
HUSD_Scene::getPrimType(int id) const
{
    auto entry = myNameIDLookup.find(id);
    if(entry != myNameIDLookup.end())
	return entry->second.mySecond;

    return INVALID_TYPE;
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
    UT_AutoLock lock(myDisplayLock);

    int id = -1;
    auto entry = myPathIDs.find(path);
    if(entry == myPathIDs.end())
    {
	id = HUSD_HydraPrim::newUniqueId();
	myPathIDs[path] = id;

        if(path.findCharIndex('[') >= 0)
            type = INSTANCE;
        
	myNameIDLookup[id] = { path, type };
    }
    else
	id = entry->second;

    return id;
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
HUSD_Scene::removeInstanceSelections()
{
    UT_IntArray to_remove;
    for(auto sel : mySelection)
    {
        int id = sel.first;
        auto entry = myNameIDLookup.find(id);
        if(entry != myNameIDLookup.end())
        {
            auto &name = entry->second.myFirst;
            if(name.endsWith("]"))
                to_remove.append(id);
        }
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
        int id = sel.first;
        auto entry = myNameIDLookup.find(id);
        if(entry != myNameIDLookup.end())
        {
            auto &name = entry->second.myFirst;
            if(!name.endsWith("]"))
                to_remove.append(id);
        }
    }

    for(auto id : to_remove)
        mySelection.erase(id);

    if(to_remove.entries())
        mySelectionID++;
    
    return (to_remove.entries() > 0);
}



void
HUSD_Scene::setSelection(const UT_StringArray &paths,
                         bool stash_prev_selection)
{
    if(stash_prev_selection)
        stashSelection();
    
    mySelectionID++;
    mySelection.clear();

    bool missing = false;

    if(paths.size() > 0)
    {
	for(auto selpath : paths)
	{
            auto geo_entry = myDisplayGeometry.find(selpath);
            if(geo_entry != myDisplayGeometry.end())
            {
                if(!geo_entry->second->isInstanced() ||
                   geo_entry->second->isPointInstanced())
                {
                    mySelection[geo_entry->second->id()] = LEAF_HIGHLIGHT;
                    geo_entry->second->selectionDirty(true);
                    continue;
                }
            }

	    // see if a path exists (instance or higher-level branch)
	    bool found = false;
            bool no_path_id = false;
	    int id = -1;

	    auto name_entry = myPathIDs.find(selpath);
	    if(name_entry != myPathIDs.end())
		id = name_entry->second;
            else
                no_path_id = true;

            {
                UT_AutoLock locker(myDisplayLock);
                for(auto it : myDisplayGeometry)
                {
                    auto &&geo = it.second;

                    // Direct ref to rprim
                    if(geo->path() == selpath && (!geo->isInstanced() ||
                                                  geo->isPointInstanced()))
                    {
                        geo->selectionDirty(true);
                        mySelection[geo->id()] = LEAF_HIGHLIGHT;
                        found = true;
                    }
                    // Referenced Instance
                    else if(id != -1 && id != geo->id() && geo->hasPathID(id))
                    {
                        geo->selectionDirty(true);
                        mySelection[id] = LEAF_HIGHLIGHT;
                        found = true;
                    }
                    else if(geo->isInstanced())
                    {
                        if(id != -1)
                        {
                            for(auto iid : geo->instanceIDs())
                                if(iid == id)
                                {
                                    geo->selectionDirty(true);
                                    mySelection[id] = LEAF_HIGHLIGHT;
                                    found = true;
                                    break;
                                }
                        }
                        UT_StringHolder ipath = geo->path();
                        if(!strncmp(selpath.c_str(), ipath.c_str(),
                                    selpath.length()))
                        {
                            if(ipath[selpath.length()] == '/')
                            {
                                geo->selectionDirty(true);
                                mySelection[id] = PATH_HIGHLIGHT;
                                found = true;
                            }
                        }
                    }
                }
            }
	    if(found)
		continue;

            {
                UT_AutoLock locker(myLightCamLock);
                
                for(auto it : myLights)
                {
                    auto &&light = it.second;
                    if(light->path() == selpath)
                    {
                        light->selectionDirty(true);
                        mySelection[light->id()] = LEAF_HIGHLIGHT;
                        found = true;
                    }
                }
                if(found)
                    continue;
	    
                for(auto it : myCameras)
                {
                    auto &&cam = it.second;
                    if(cam->path() == selpath)
                    {
                        cam->selectionDirty(true);
                        mySelection[cam->id()] = LEAF_HIGHLIGHT;
                        found = true;
                    }
                }
            }
	    if(found)
		continue;
	    
            // If we have no existing ref, this must be a branch. Check if
            // the ref already exists with a trailing slash (indicating a
            // branch). If not, create a new path id for it.
	    if(name_entry == myPathIDs.end())
	    {
                UT_String    branchpath(selpath.c_str());

		if(!branchpath.endsWith("/"))
                {
                    branchpath.append('/');
                    name_entry = myPathIDs.find(branchpath);
                }

                if(name_entry == myPathIDs.end())
                {
                    id = HUSD_HydraPrim::newUniqueId();
                    myPathIDs[ branchpath ] = id;
                    myNameIDLookup[id] = { branchpath, PATH };
                }
                else
                    id = name_entry->second;
                selectionModified(id);
	    }

            // Prim isn't missing if it's a render setting prim, otherwise we
            // need to resolve later when more prims are processed.
            if(no_path_id && !selpath.startsWith("/Render/"))
                missing = true;
	    
	    mySelection[id] = PATH_HIGHLIGHT;
	}
    }

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
	mySelectionArray.clear();
	for(auto sel : mySelection)
	{
	    int id = sel.first;
	    auto entry = myNameIDLookup.find(id);
	    if(entry != myNameIDLookup.end())
		mySelectionArray.append(entry->second.myFirst);
	}
	mySelectionArrayID = mySelectionID;
        mySelectionArrayNeedsUpdate = false;
    }

    return mySelectionArray;
}

bool
HUSD_Scene::selectParents()
{
    bool changed = false;
    UT_Map<int, int> selection;
    
    for(auto sel : mySelection)
    {
        int id = sel.first;
        auto entry = myNameIDLookup.find(id);
        if(entry != myNameIDLookup.end())
        {
            auto &name = entry->second.myFirst;

            int iidx = name.lastCharIndex('[');
            if(iidx >= 0) // instance
            {
                // Another instance above must exist. We don't climb out of
                // instances, as instanecs and prims are very different
                // entities.
                if (name.lastCharIndex('[', 2) >= 0)
                {
                    UT_StringView pname(name, iidx);
                    UT_StringHolder parent_name(pname);
                    
                    int pid = getOrCreateID(parent_name, INSTANCE);
                    selection[pid] = LEAF_HIGHLIGHT;
                    selectionModified(pid);
                    changed = true;
                }
                else
                    selection[id] = sel.second;
                    
            }
            else // regular prim
            {
                int idx = name.lastCharIndex('/');

                // avoid a trailing / and do not collapse to /
                if(idx > 0 && idx == name.length()-1)
                    idx = name.lastCharIndex('/', 2);
                
                if(idx > 0)
                {
                    UT_StringView pname(name, idx);
                    UT_StringHolder parent_name(pname);

                    int pid = getOrCreateID(parent_name, PATH);
                    selection[pid] = PATH_HIGHLIGHT;
                    selectionModified(pid);
                    changed = true;
                }
                else
                    selection[id] = sel.second;
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
    bool changed = false;
    UT_Map<int, int> selection;

    auto ptrav = HUSD_PrimTraversalDemands(
	HUSD_TRAVERSAL_ACTIVE_PRIMS |
	HUSD_TRAVERSAL_DEFINED_PRIMS |
	HUSD_TRAVERSAL_LOADED_PRIMS | 
	HUSD_TRAVERSAL_NONABSTRACT_PRIMS);

    for(auto sel : mySelection)
    {
        const int id = sel.first;
        auto entry = myNameIDLookup.find(id);
        if(entry != myNameIDLookup.end())
        {
            auto &name = entry->second.myFirst;

            // ignore instances. Possible TODO: find child instances somehow?
            if(name.findCharIndex('[') < 0)
            {
                HUSD_PrimHandle ph(myStage, myStageOverrides, name,
                                   UT_StringHolder());
                if(ph.hasChildren(ptrav))
                {
                    UT_Array<HUSD_PrimHandle> children;
                    ph.getChildren(children, ptrav);
                    if(children.entries())
                    {
                        if(all_children)
                        {
                            for(auto &it : children)
                            {
                                auto &path = it.path();
                                PrimType type = INVALID_TYPE;
                                int cid = getIDForPrim(path, type, true);
                                if(cid != -1)
                                {
                                    selection[cid] = type;
                                    selectionModified(cid);
                                    changed = true;
                                }
                            }
                            if(changed)
                            {
                                selectionModified(id);
                                continue;
                            }
                        }
                        else 
                        {
                            auto &path = children(0).path();
                            PrimType type = INVALID_TYPE;
                            int cid = getIDForPrim(path, type, true);
                            if(cid != -1)
                            {
                                selection[cid] = type;
                                selectionModified(cid);
                                selectionModified(id);
                                changed = true;
                                continue;
                            }
                        }
                    }
                }
            }
            
            selection[id] = sel.second;
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
    bool changed = false;
    UT_Map<int, int> selection;

    auto ptrav = HUSD_PrimTraversalDemands(
	HUSD_TRAVERSAL_ACTIVE_PRIMS |
	HUSD_TRAVERSAL_DEFINED_PRIMS |
	HUSD_TRAVERSAL_LOADED_PRIMS | 
	HUSD_TRAVERSAL_NONABSTRACT_PRIMS);

    for(auto sel : mySelection)
    {
        const int id = sel.first;
        auto entry = myNameIDLookup.find(id);
        if(entry != myNameIDLookup.end())
        {
            auto &name = entry->second.myFirst;

            // ignore instances. Possible TODO: find child instances somehow?
            if(name.findCharIndex('[') < 0)
            {
                int pidx = name.lastCharIndex('/');
                if(pidx != -1)
                {
                    UT_StringView parent_path(name, pidx);
                    HUSD_PrimHandle ph(myStage, myStageOverrides,
                                       UT_StringHolder(parent_path),
                                       UT_StringHolder());
                    
                    if(ph.hasChildren(ptrav))
                    {
                        UT_Array<HUSD_PrimHandle> children;
                        ph.getChildren(children, ptrav);
                        if(children.entries() > 1)
                        {
                            int cidx = -1;
                            for(int i = 0; i<children.entries(); i++)
                                if(children(i).path() == name)
                                {
                                    cidx = i;
                                    break;
                                }
                            if(cidx != -1)
                            {
                                cidx += next_sibling ? 1 : -1;
                                if(cidx < 0)
                                    cidx = children.entries()-1;
                                else if(cidx == children.entries())
                                    cidx = 0;

                                UT_StringHolder path = children(cidx).path();
                                PrimType type = INVALID_TYPE;
                                int sid = getIDForPrim(path, type, true);
                                if(sid != -1)
                                {
                                    selection[sid] = type;
                                    selectionModified(sid);
                                    selectionModified(id);
                                    changed = true;
                                    continue;
                                }
                             }
                        }
                    }
                }
            }
            
            selection[id] = sel.second;
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


void
HUSD_Scene::selectionModified(int id) const
{
    auto name_entry = myNameIDLookup.find(id);
    if(name_entry != myNameIDLookup.end())
    {
	auto &name = name_entry->second.myFirst;
	const bool branch = name_entry->second.mySecond == PATH;
	
	{
	    auto entry = myDisplayGeometry.find(name);
	    if(entry != myDisplayGeometry.end())
	    {
		entry->second->selectionDirty(true);
		return;
	    }
	}
	{
	    auto entry = myCameras.find(name);
	    if(entry != myCameras.end())
	    {
		entry->second->selectionDirty(true);
		return;
	    }
	}
	{
	    auto entry = myLights.find(name);
	    if(entry != myLights.end())
	    {
		entry->second->selectionDirty(true);
		return;
	    }
	}

	// If we're here then we have a prim that is not a leaf node.
	for(auto it : myDisplayGeometry)
	{
	    if(it.second->hasPathID(id))
	    {
		it.second->selectionDirty(true);
	    }
	    else if(it.first.startsWith(name) &&
		    (branch || it.first[name.length()] == '/'))
	    {
		it.second->selectionDirty(true);
	    }

	    // Instancing
	    else if(it.second->instanceIDs().entries())
	    {
		for(auto iid : it.second->instanceIDs())
		{
                    if(iid == id)
                    {
                        it.second->selectionDirty(true);
                        break;
                    }
                    
		    auto iname_entry = myNameIDLookup.find(iid);
		    if(iname_entry != myNameIDLookup.end())
		    {
			auto &iname = iname_entry->second.myFirst;
			if(iname == name || (iname.startsWith(name) &&
                                             iname[name.length()]=='['))
			{
			    it.second->selectionDirty(true);
			    break;
			}
		    }
		}
	    }
	}
	
	for(auto it : myLights)
	    if(it.first.startsWith(name) &&
	       (branch || it.first[name.length()] == '/'))
		it.second->selectionDirty(true);
	for(auto it : myCameras)
	    if(it.first.startsWith(name) &&
	       (branch || it.first[name.length()] == '/'))
		it.second->selectionDirty(true);
    }
}

void
HUSD_Scene::addToHighlight(int id)
{
    if(myHighlight.find(id) == myHighlight.end())
    {
	myHighlight[id] = LEAF_HIGHLIGHT;
	myHighlightID++;
    }
}

void
HUSD_Scene::addPathToHighlight(const UT_StringHolder &path)
{
    auto name_entry = myPathIDs.find(path);
    int id = 0;
    if(name_entry == myPathIDs.end())
    {
	id = HUSD_HydraPrim::newUniqueId();
	myPathIDs[ path ] = id;
	myNameIDLookup[id] = { path, PATH };
    }
    else
	id = name_entry->second;
    
    if(myHighlight.find(id) == myHighlight.end())
    {
	myHighlight[id] = PATH_HIGHLIGHT;
	myHighlightID++;
    }
}


void
HUSD_Scene::addInstanceToHighlight(int id)
{
    auto path = lookupPath(id);
    if(!path.isstring())
        return;

    // Not an instance unless there is at least one [] 
    const char *paths = path.c_str();
    if(!strchr(paths, '['))
        return;
    
    if(myHighlight.find(id) == myHighlight.end())
    {
	myHighlight[id] = LEAF_HIGHLIGHT;
	myHighlightID++;
    }
}

void
HUSD_Scene::clearHighlight()
{
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
    stashSelection();
    makeSelection(myHighlight, false);
}

void
HUSD_Scene::addHighlightToSelection()
{
    stashSelection();

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
    if(mySelection.size() == 0)
	return false;

    if(mySelection.find(prim->id()) != mySelection.end())
	return true;

    if(prim->isInstanced())
    {
	for(auto id : prim->instanceIDs())
	    if(mySelection.find(id) != mySelection.end())
		return true;
    }

    for(auto it : mySelection)
	if(it.second == PATH_HIGHLIGHT) // selection is on a path with children
	{
	    const int id = it.first;
	    auto name_entry = myNameIDLookup.find(id);
	    if(name_entry != myNameIDLookup.end())
	    {
		auto &name = name_entry->second.myFirst;
		const bool branch = name.endsWith("/");

		if(prim->path().startsWith(name) &&
		   (branch || prim->path()[name.length()] == '/'))
		{
		    return true;
		}
	    }
	}


    return false;
}

bool
HUSD_Scene::isSelected(int id) const
{
    if(mySelection.size() == 0)
	return false;

    if(mySelection.find(id) != mySelection.end())
	return true;

    auto name_entry = myNameIDLookup.find(id);
    if(name_entry != myNameIDLookup.end())
    {
	auto &path = name_entry->second.myFirst;
        const bool is_instance = path.endsWith("]");

	for(auto it : mySelection)
	{
	    // selection is on a path with children
	    if(it.second == PATH_HIGHLIGHT) 
	    {
		const int id = it.first;
		auto name_entry = myNameIDLookup.find(id);
		if(name_entry != myNameIDLookup.end())
		{
		    auto &name = name_entry->second.myFirst;
		    if(path == name)
			return true;
		    
		    if(path.startsWith(name))
                    {
                        const bool branch = name.endsWith("/");
                        if(branch && path[name.length()] == '/')
                            return true;
                    }
		}
	    }
            else if(is_instance)
            {
		const int id = it.first;
		auto name_entry = myNameIDLookup.find(id);
		if(name_entry != myNameIDLookup.end())
		{
		    auto &name = name_entry->second.myFirst;
                    if(path.startsWith(name))
                        return true;
                }
            }
	}
    }

    return false;
}



bool
HUSD_Scene::isHighlighted(const HUSD_HydraPrim *prim) const
{
    if(myHighlight.size() == 0)
	return false;

    // in the highlight as a prim
    if(myHighlight.find(prim->id()) != myHighlight.end())
	return true;

    // look for a highlighted parent path
    for(auto it : myHighlight)
	if(it.second == PATH_HIGHLIGHT) // highlight is on a path with children
	{
	    const int id = it.first;
	    auto name_entry = myNameIDLookup.find(id);
	    if(name_entry != myNameIDLookup.end())
	    {
                auto &name = name_entry->second.myFirst;
                if(prim->path() == name)
                    return true;
                
		if(prim->path().startsWith(name) &&
		   (prim->path()[name.length()] == '/' ||
                    prim->path()[name.length()] == '['))
		    return true;
	    }
	}
    
    return false;
}

bool
HUSD_Scene::isHighlighted(int id) const
{
    if(myHighlight.size() == 0)
	return false;

    // in the highlight as a prim
    if(myHighlight.find(id) != myHighlight.end())
	return true;

    auto entry = myNameIDLookup.find(id);
    if(entry == myNameIDLookup.end())
        return false;
    
    auto && path = entry->second.myFirst;
    
    // look for a highlighted parent path
    for(auto it : myHighlight)
	if(it.second == PATH_HIGHLIGHT) // highlight is on a path with children
	{
	    const int id = it.first;
	    auto name_entry = myNameIDLookup.find(id);
	    if(name_entry != myNameIDLookup.end())
	    {
                auto &name = name_entry->second.myFirst;
                if(path == name)
                    return true;
                
		if(path.startsWith(name) &&
		   (path[name.length()] == '/' ||
                    path[name.length()] == '['))
		    return true;
	    }
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
    UT_StringHolder null;
    return HUSD_PrimHandle(myStage, myStageOverrides, path, null);
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
    // remove anything  not in the highlighted items
    UT_IntArray to_remove;
    for(auto entry : mySelection)
	if(selection.find(entry.first) == selection.end())
	{
	    to_remove.append(entry.first);
	    selectionModified(entry.first);
	}
    for(auto id : to_remove)
	mySelection.erase(id);
    
    bool changed = (to_remove.entries() > 0);
    
    // add anything not in the selected items
    
    for(auto entry : selection)
	if(mySelection.find(entry.first) == mySelection.end())
	{
            if(validate)
            {
                // Don't add prims that no longer exist to the selection.
                auto &path = lookupPath(entry.first);
                if (path.lastCharIndex('[') < 0) // ignore point instances
                {
                    HUSD_PrimHandle prim(myStage, myStageOverrides, path,
                                         UT_StringHolder());
                    if(!prim.isDefined())
                        continue;
                }
            }
	    mySelection[entry.first] = entry.second;
	    selectionModified(entry.first);
	    changed = true;
	}
    
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
