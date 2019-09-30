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
 * NAME:	XUSD_HydraGeoPrim.C (HUSD Library, C++)
 *
 * COMMENTS:	Container for a hydra geometry prim (HdRprim)
 */

#include "XUSD_HydraGeoPrim.h"
#include "XUSD_HydraInstancer.h"
#include "XUSD_HydraField.h"
#include "XUSD_HydraUtils.h"
#include "XUSD_SceneGraphDelegate.h"
#include "XUSD_Format.h"
#include "XUSD_Tokens.h"
#include "HUSD_HydraGeoPrim.h"
#include "HUSD_HydraMaterial.h"
#include "HUSD_Scene.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <gusd/UT_Gf.h>

#include <GT/GT_AttributeList.h>
#include <GT/GT_DAConstant.h>
#include <GT/GT_DAConstantValue.h>
#include <GT/GT_DAIndexedString.h>
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DANumeric.h>
#include <GT/GT_Names.h>
#include <GT/GT_Primitive.h>
#include <GT/GT_PrimPointMesh.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_PrimSubdivisionMesh.h>
#include <GT/GT_PrimCurveMesh.h>
#include <GT/GT_PrimVDB.h>
#include <GT/GT_PrimInstance.h>
#include <GT/GT_Util.h>

// Debug stuff
#include <UT/UT_Debug.h>
#include <UT/UT_Lock.h>
#include "HUSD_GetAttributes.h"


using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE


XUSD_HydraGeoPrim::XUSD_HydraGeoPrim(TfToken const& type_id,
				     SdfPath const& prim_id,
				     SdfPath const& instancer_id,
				     HUSD_Scene &scene)
    : HUSD_HydraGeoPrim(scene, prim_id.GetText()),
      myHydraPrim(nullptr),
      myPrimBase(nullptr)
{
    if(type_id == HdPrimTypeTokens->mesh)
    {
	auto prim = 
	    new XUSD_HydraGeoMesh(type_id, prim_id, instancer_id,
				  myGTPrim, myInstance, myDirtyMask, *this);
	myHydraPrim = prim;
	myPrimBase = prim;
    }
    
    else if(type_id == HdPrimTypeTokens->basisCurves)
    {
	auto prim = 
	    new XUSD_HydraGeoCurves(type_id, prim_id, instancer_id,
				    myGTPrim, myInstance, myDirtyMask, *this);
	myHydraPrim = prim;
	myPrimBase = prim;
    }
    else if(type_id == HdPrimTypeTokens->volume)
    {
	auto prim = 
	    new XUSD_HydraGeoVolume(type_id, prim_id, instancer_id,
				    myGTPrim, myInstance, myDirtyMask, *this);
	myHydraPrim = prim;
	myPrimBase = prim;
    }
    else if(type_id == HdPrimTypeTokens->points)
    {
	auto prim = 
	    new XUSD_HydraGeoPoints(type_id, prim_id, instancer_id,
				    myGTPrim, myInstance, myDirtyMask, *this);
	myHydraPrim = prim;
	myPrimBase = prim;
    }
}

XUSD_HydraGeoPrim::~XUSD_HydraGeoPrim()
{
    delete myHydraPrim;
}

UT_StringHolder
XUSD_HydraGeoPrim::getTopLevelPath(HdSceneDelegate *sdel,
                                   SdfPath const& prim_id,
                                   SdfPath const& instancer_id)
{
    if(instancer_id.IsEmpty())
        return prim_id.GetText();
    
    auto instancer= sdel->GetRenderIndex().GetInstancer(instancer_id);
    while(instancer)
    {
        if(instancer->GetParentId().IsEmpty())
            return instancer->GetId().GetText();
        
        instancer=sdel->GetRenderIndex().GetInstancer(instancer->GetParentId());
    }

    return prim_id.GetText();
}

void
XUSD_HydraGeoPrim::updateGTSelection()
{
    if(myPrimBase)
	myPrimBase->updateGTSelection();
}

void
XUSD_HydraGeoPrim::clearGTSelection()
{
    if(myPrimBase)
	myPrimBase->clearGTSelection();
}

// ------------------------------------------------------------------------

void
XUSD_HydraGeoBase::resetPrim()
{
    myGTPrim.reset();

    for(auto it : myAttribMap)
    {
        GT_Owner attrib_owner;
        int interp;
        bool computed;
        void *data;
        UTlhsTuple(attrib_owner, interp, computed, data) = it.second;
        if(data)
            delete (HdExtComputationPrimvarDescriptor *)data;
    }
    myAttribMap.clear();
    myInstanceTransforms.reset();
}

void
XUSD_HydraGeoBase::clearDirty(HdDirtyBits *dirty_bits) const
{
    if(*dirty_bits)
	myHydraPrim.bumpVersion();
    
    *dirty_bits = (*dirty_bits & HdChangeTracker::Varying);
}

bool
XUSD_HydraGeoBase::isDeferred(HdRenderParam *rparm,
			      HdDirtyBits &bits) const
{
    auto srparm = static_cast<XUSD_SceneGraphRenderParam *>(rparm);

    srparm->scene().bumpModSerial();
    
    if(srparm->scene().isDeferredUpdate())
    {
	// Remember the dirty bits we are deferring. Combine the current
	// dirty bits with any existing dirty bits in case the prim is
	// changed in differetn ways by different edit operations. We need
	// to track the union of all changes.
	myHydraPrim.setDeferredBits(bits | myHydraPrim.deferredBits());
	// Clear the dirty bits, or else the HdChangeTracker will record the
	// fact that the current bits are dirty, so subsequent edits of the
	// same type will not be recorded as changes, and so the adapter will
	// not be called to update the value cache. We would be left fetching
	// an out of date value from the cache when we perform our updates.
	bits = (bits & HdChangeTracker::Varying);
	return true;
    }
    
    myHydraPrim.setDeferredBits(0);
    return false;
}
    

GEO_ViewportLOD
XUSD_HydraGeoBase::checkVisibility(HdSceneDelegate *scene,
				   const SdfPath   &id,
				   HdDirtyBits     *dirty_bits)
{
    if(*dirty_bits & HdChangeTracker::DirtyVisibility)
    {
	myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::LOD_CHANGE;
	*dirty_bits = *dirty_bits & ~HdChangeTracker::DirtyVisibility;
    }
    
    GEO_ViewportLOD lod = GEO_VIEWPORT_FULL;

    // check for visibility.
    bool vis = scene->GetVisible(id);
    if(!vis)
	lod = GEO_VIEWPORT_HIDDEN;
    else
    {
	// TODO: Hopefully replace with a scene->GetDrawMode() call.
#if 0
	SdfPathVector parents = id.GetPrefixes();
	for(auto &p : parents)
	{
	    TfToken vis;
	    if(XUSD_HydraUtils::evalAttrib(vis,scene,p,
					   HusdHdPrimvarTokens()->viewLOD))
	    {
		UTdebugPrint(p.GetText(), vis.GetText());
		if(vis == HusdHdPrimValueTokens()->bounds ||
		   vis == HusdHdPrimValueTokens()->cards) // no support for this
		    lod = GEO_VIEWPORT_BOX;
		else if(vis == HusdHdPrimValueTokens()->origin)
		    lod = GEO_VIEWPORT_CENTROID;
		else if(vis == HusdHdPrimValueTokens()->full)
		    lod = GEO_VIEWPORT_FULL;
	    }
	}
#endif
    }
    
    if(myInstance && myInstance->getDetailAttributes())
    {
	auto loda = myInstance->getDetailAttributes()->
	    get(GT_Names::view_lod_mask);
	if(loda)
	{
	    auto *lodd = static_cast<GT_DAConstantValue<int> *>(loda.get());
	    lodd->set(1<<lod);
	}
    }
    return lod;
}

bool
XUSD_HydraGeoBase::addBBoxAttrib(HdSceneDelegate* sceneDelegate,
				 const SdfPath		&id,
				 GT_AttributeListHandle &detail,
				 const GT_Primitive	*gt_prim) const
{
    GfRange3d extents = sceneDelegate->GetExtent(id);
    UT_BoundingBox bbox(extents.GetMin()[0],
			extents.GetMin()[1],
			extents.GetMin()[2],
			extents.GetMax()[0],
			extents.GetMax()[1],
			extents.GetMax()[2]);
    if(bbox.isValid())
    {
	GT_Util::addBBoxAttrib(bbox, detail);
	return true;
    }
    else
    {
	bbox.makeInvalid();
	gt_prim->enlargeBounds(&bbox, 1);
	if(bbox.isValid())
	{
	    GT_Util::addBBoxAttrib(bbox, detail);
	    return true;
	}
    }

    return false;
}

bool
XUSD_HydraGeoBase::processInstancerOverrides(
    HdSceneDelegate         *sd,
    const SdfPath           &inst_id,
    const SdfPath           &proto_id,
    HdDirtyBits             *dirty_bits,
    int                      inst_level,
    int                     &ninst)
{
    const auto	&descs = sd->GetPrimvarDescriptors(inst_id,
                                                   HdInterpolationInstance);
    
    VtIntArray instanceIndices = sd->GetInstanceIndices(inst_id, proto_id);
    ninst = instanceIndices.size();

    if(inst_level == myInstanceAttribStack.entries())
        myInstanceAttribStack.append();

    myInstanceAttribStack(inst_level).nInst = ninst;
    
    // UTdebugPrint("Process instance level",
    //              inst_id, proto_id, inst_level, ninst);
    GT_DataArrayHandle ind_mapping;
    GT_AttributeListHandle alist = myInstanceAttribStack(inst_level).attribs;
    UT_StringMap<bool> exists;
    for (exint i = 0, n = descs.size(); i < n; ++i)
    {
        auto &name = descs[i].name;
        UT_StringHolder usd_attrib(name.GetText());
        //UTdebugPrint("Instance attrib: usdame =", usd_attrib);
        auto entry = myExtraAttribs.find(usd_attrib);
        if(entry == myExtraAttribs.end())
            continue;

        //UTdebugPrint("Inst", usd_attrib, entry->second);
        GT_DataArrayHandle attr;
        if(HdChangeTracker::IsPrimvarDirty(*dirty_bits, inst_id, name) ||
           (*dirty_bits & (HdChangeTracker::DirtyInstancer |
                           HdChangeTracker::DirtyInstanceIndex)))
        {
            auto value = sd->Get(inst_id,name);
            if(!value.IsEmpty())
            {
                attr = XUSD_HydraUtils::attribGT(value,
                                                 GT_TYPE_NONE,
                                                 XUSD_HydraUtils::newDataId());
                if(attr->entries() > ninst)
                {
                    if(!ind_mapping)
                    {
                        auto ind = new GT_DANumeric<int>(ninst,1);
                        for(int i=0; i<ninst; i++)
                            ind->set(instanceIndices[i], i);
                        ind_mapping = ind;
                    }
                    
                    attr = new GT_DAIndirect(ind_mapping, attr);
                }
            }
            // else
            //     UTdebugPrint("Empty attrib!");
        }
        
        
        if(!attr && alist)
            attr = alist->get(entry->second);
        
        if(attr)
        {
            exists[ entry->second ] = true;
            if(ninst < 0)
                ninst = attr->entries();
            if(alist)
                alist = alist->addAttribute(entry->second, attr, true);
            else
                alist = GT_AttributeList::createAttributeList(entry->second,
                                                              attr);
        }
    }

    if(alist)
    {
        UT_StringArray to_remove;
        for(int i=0; i<alist->entries(); i++)
            if(exists.find(alist->getNames()(i)) == exists.end())
                to_remove.append(alist->getNames()(i));
        alist = alist->removeAttributes(to_remove);
    }

    if(alist && alist->entries() > 0)
    {
        myInstanceAttribStack(inst_level).attribs = alist;

        UT_Array<UT_Options> *optlist  = 
            myInstanceAttribStack(inst_level).options;
        if(!optlist)
        {
            optlist = new UT_Array<UT_Options>();
            myInstanceAttribStack(inst_level).options = optlist;
        }
        
        optlist->entries(ninst);

        for(int i=0; i<ninst; i++)
        {
            UT_Options &opts = (*optlist)(i);

            for(int ai=0; ai<alist->entries(); ai++)
            {
                GT_DataArray *array = alist->get(ai).get();
                auto storage = array->getStorage();
                auto tsize = array->getTupleSize();
                auto &name = alist->getName(ai);
                
                bool is_int = false;
                bool is_float = false;
                
                if(storage == GT_STORE_UINT8 || storage == GT_STORE_INT16 ||
                   storage == GT_STORE_INT32 || storage == GT_STORE_INT64)
                    is_int = true;
                else if(storage == GT_STORE_REAL16 ||
                        storage == GT_STORE_REAL32 ||
                        storage == GT_STORE_REAL64)
                    is_float = true;
                else if(storage != GT_STORE_STRING)
                    continue;
                
                if(tsize == 1)
                {
                    if(is_int)
                        opts.setOptionI(name, array->getI64(i));
                    else if(is_float)
                        opts.setOptionF(name, array->getF64(i));
                    else
                        opts.setOptionS(name, array->getS(i));
                }
                else if(is_float)
                {
                    if(tsize == 2)
                    {
                        UT_Vector2D v( array->getF64(i,0),
                                       array->getF64(i,1));
                        opts.setOptionV2(name, v);
                    }
                    else if(tsize == 3)
                    {
                        UT_Vector3D v( array->getF64(i,0),
                                       array->getF64(i,1),
                                       array->getF64(i,2) );
                        opts.setOptionV3(name, v);
                    }
                    else if(tsize == 4)
                    {
                        UT_Vector4D v( array->getF64(i,0),
                                       array->getF64(i,1),
                                       array->getF64(i,2),
                                       array->getF64(i,3) );
                        opts.setOptionV4(name, v);
                    }
                }
                else if(is_int)
                {
                    UT_Int64Array v;
                    for(int it=0; it<tsize; it++)
                        v.append(array->getI64(i,it));
                    opts.setOptionIArray(name, v);
                }
                else
                {
                    UT_StringArray v;
                    for(int it=0; it<tsize; it++)
                        v.append(array->getS(i,it));
                    opts.setOptionSArray(name, v);
                }
            }
        }
    }
    else
        myInstanceAttribStack(inst_level).clear();

    return alist != nullptr;
}

void
XUSD_HydraGeoBase::buildShaderInstanceOverrides(
    HdSceneDelegate         *sd,
    const SdfPath           &inst_id,
    const SdfPath           &proto_id,
    HdDirtyBits             *dirty_bits)
{
    bool has_overrides = false;
    auto xinst = sd->GetRenderIndex().GetInstancer(inst_id);
    int ninst = 1;
    int lvl = 0;
 
    //UTdebugPrint("Build instancers", inst_id);
    SdfPath id = inst_id;
    SdfPath pid = proto_id;
    while(xinst)
    {
        int num;
        if(processInstancerOverrides(sd, id, pid, dirty_bits, lvl, num))
            has_overrides = true;
        
        ninst *= num;

        pid = id;
        id = xinst->GetParentId();
        if(id.IsEmpty())
            break;
        
        xinst = sd->GetRenderIndex().GetInstancer(id);
        lvl++;
    }
    
    myHydraPrim.hasMaterialOverrides(has_overrides);

    if(has_overrides)
    {
        //UTdebugPrint("has overrides", ninst);
        auto overrides = new GT_DAIndexedString(ninst);
        myInstanceOverridesAttrib = overrides;
        
        if(lvl == 0)
        {
            // easy case, no nesting.
            const UT_Array<UT_Options> *opt_array =
                myInstanceAttribStack(0).options;
            
            UT_ASSERT(opt_array->entries() == ninst);
            for(int i=0; i<ninst; i++)
            {
                const UT_Options &opts = (*opt_array)(i);
                assignOverride(&opts, overrides, i);
            }
        }
        else
        {
            int idx = 0;
            processNestedOverrides(lvl, overrides, nullptr, idx);
            // should have filled the entire flat array.
            UT_ASSERT(idx == ninst);
        }
        
        if(!myInstanceMatID ||
           myInstanceMatID->entries() != myInstanceOverridesAttrib->entries())
        {
            const int n = myInstanceOverridesAttrib->entries();
            myInstanceMatID = new GT_DANumeric<int>(n, 1);
        }
    }
    else
        myInstanceOverridesAttrib = nullptr;
}

void
XUSD_HydraGeoBase::processNestedOverrides(int level,
                                          GT_DAIndexedString *overrides,
                                          const UT_Options *input_opt,
                                          int &index) const
{
    const int ninst = myInstanceAttribStack(level).nInst;
    auto opt_array = myInstanceAttribStack(level).options;

    //UTdebugPrint("Nested", level, ninst);
    for(int i=0; i<ninst; i++)
    {
        const UT_Options *opt = opt_array ? &(*opt_array)(i) : nullptr;
        UT_Options new_opt_set;
        const UT_Options *final_opt = nullptr;

        if(input_opt && input_opt->getNumOptions()>0 &&
           opt && opt->getNumOptions()>0)
        {
            new_opt_set.merge(*opt);
            new_opt_set.merge(*input_opt);

            final_opt = &new_opt_set;
        }
        else if(input_opt && input_opt->getNumOptions()>0)
            final_opt = input_opt;
        else if(opt && opt->getNumOptions()>0)
            final_opt = opt;
        else
            final_opt = &new_opt_set;

        if(level == 0)
        {
            assignOverride(final_opt, overrides, index);
            index++;
        }
        else
            processNestedOverrides(level-1, overrides, final_opt, index);
    }
}

void
XUSD_HydraGeoBase::assignOverride(const UT_Options *options,
                                  GT_DAIndexedString *overrides,
                                  int index) const
{
    UT_StringHolder val;
    if(options && options->getNumOptions())
    {
        UT_WorkBuffer sbuf;
        options->appendPyDictionary(sbuf);
        val = sbuf.buffer();
    }
    //UTdebugPrint(index, val);
    overrides->setString(index, 0, val);
}

void
XUSD_HydraGeoBase::buildTransforms(HdSceneDelegate *scene_delegate,
				   const SdfPath  &proto_id,
				   const SdfPath  &instr_id,
				   HdDirtyBits    *dirty_bits,
				   GT_TransformHandle &th)
{
    bool only_prim_transform = instr_id.IsEmpty();

    if(!instr_id.IsEmpty() &&
	(HdChangeTracker::IsInstancerDirty(*dirty_bits, proto_id) ||
	 HdChangeTracker::IsInstanceIndexDirty(*dirty_bits, proto_id) ||
	 (myDirtyMask & HUSD_HydraGeoPrim::INSTANCE_CHANGE)))
    {
	// Instance transforms
	auto xinst = UTverify_cast<XUSD_HydraInstancer *>(
	    scene_delegate->GetRenderIndex().GetInstancer(instr_id));
	if(xinst)
	{
	    // Make sure to sync the primvars before trying to compute transforms
            xinst->syncPrimvars(true);

            int levels = xinst->GetInstancerNumLevels(
                                scene_delegate->GetRenderIndex(),
                                *myHydraPrim.rprim());
	    myInstanceTransforms = XUSD_HydraUtils::createTransformArray(
		xinst->computeTransformsAndIDs(proto_id, true, &myPrimTransform,
                                               levels-1,
                                               myHydraPrim.instanceIDs(),
                                               &myHydraPrim.scene()));

	    myInstanceId++;
	    auto tr =
		static_cast<XUSD_HydraTransforms *>(myInstanceTransforms.get());
	    tr->setDataId(myInstanceId);
	    myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::INSTANCE_CHANGE;
	    only_prim_transform = false;

            myHydraPrim.setPointInstanced(xinst->isPointInstancer());
	}
	else
	    only_prim_transform = true;
    }

    if (only_prim_transform)
    {
	UT_Matrix4D mat = GusdUT_Gf::Cast(myPrimTransform);
	th = new GT_Transform(&mat, 1);
	if(myInstanceTransforms && myInstanceTransforms->entries() != 0)
	{
	    myInstanceTransforms.reset();
	    myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::INSTANCE_CHANGE;
	}
        myHydraPrim.instanceIDs().entries(0);
    }
}

bool
XUSD_HydraGeoBase::updateAttrib(const TfToken	         &usd_attrib,
				const UT_StringRef       &gt_attrib,
				HdSceneDelegate	         *scene_delegate,
				const SdfPath	         &id,
				HdDirtyBits	         *dirty_bits,
				GT_Primitive		 *gt_prim,
				GT_AttributeListHandle   (&attrib_list)[4],
				int			 *point_freq_num,
				bool			  set_point_freq,
				bool			 *exists)
{
    if(exists)
	*exists = false;
    
    auto entry = myAttribMap.find(usd_attrib.GetText());
    if(entry == myAttribMap.end())
	return false;
	
    GT_Owner attrib_owner;
    int interp;
    bool computed;
    void *data;
    UTlhsTuple(attrib_owner, interp, computed, data) = entry->second;
    if(attrib_owner == GT_OWNER_INVALID)
	return false;

    bool changed = false;
    GT_DataArrayHandle attr; 

    if(HdChangeTracker::IsPrimvarDirty(*dirty_bits, id, usd_attrib))
    {
	if(computed)
	{
            auto primd = (HdExtComputationPrimvarDescriptor *) data;
            HdExtComputationPrimvarDescriptorVector cvar;
            cvar.emplace_back(*primd);
                         
            HdExtComputationUtils::ValueStore value_store
                = HdExtComputationUtils::GetComputedPrimvarValues(
                    cvar, scene_delegate);
            auto val = value_store.find(usd_attrib);
            if(val != value_store.end())
            {
                auto id = XUSD_HydraUtils::newDataId();
                attr = XUSD_HydraUtils::attribGT(val->second,
                                                 GT_TYPE_NONE, id);
            }
            
            changed = true;
	}
	else
	{
	    attr = XUSD_HydraUtils::attribGT(scene_delegate->Get(id,usd_attrib),
					     GT_TYPE_NONE,
					     XUSD_HydraUtils::newDataId());
	}

	if(attr)
	{
	    myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::GEO_CHANGE;
	    changed = true;
	}
    }

    if(!attr)
    {
        // Houdini viewport doesn't natively support primitive normals, they are
        // upcast to vertex attribs.
        if(gt_attrib == GA_Names::N && attrib_owner == GT_OWNER_PRIMITIVE)
            attrib_owner = GT_OWNER_VERTEX;
        
        if(gt_prim && gt_prim->getAttributeList(attrib_owner))
            attr = gt_prim->getAttributeList(attrib_owner)->get(gt_attrib);
    }
    

    if(attr && attr->entries() > 0)
    {
	// Some pixar meshes have #vertices == #points, which is very
	// different from how our polymeshes work. Change them to point
	// frequency.
	if(set_point_freq && point_freq_num)
	    *point_freq_num = attr->entries();
	else if(attrib_owner == GT_OWNER_VERTEX && point_freq_num)
	{
	    if(attr->entries() == *point_freq_num)
		attrib_owner = GT_OWNER_POINT;
	}

	if(!computed)
	    attr = attr->harden();
	
	if(attrib_list[attrib_owner])
	    attrib_list[attrib_owner] = attrib_list[attrib_owner]->
		addAttribute(gt_attrib, attr, true);
	else
	    attrib_list[attrib_owner] =
		GT_AttributeList::createAttributeList(gt_attrib, attr);

	if(exists)
	    *exists = true;
    }
    return changed;
}

void
XUSD_HydraGeoBase::createInstance(HdSceneDelegate          *scene_delegate,
				  const SdfPath		   &proto_id,
				  const SdfPath		   &inst_id,
				  HdDirtyBits		   *dirty_bits,
				  GT_Primitive		   *geo,
				  GEO_ViewportLOD	    lod,
				  int			    mat_id,
				  bool			    instance_change)
{
#if 0
    static UT_Lock theLock;
    UT_AutoLock lock(theLock);
#endif

    if(!inst_id.IsEmpty())
        myHydraPrim.setPath(
            myHydraPrim.getTopLevelPath(scene_delegate, proto_id, inst_id) );

    GT_AttributeListHandle detail, uniform;

    // render pass token
    HUSD_HydraPrim::RenderTag tag = HUSD_HydraPrim::renderTag(
					scene_delegate->GetRenderTag(proto_id));
    myHydraPrim.setRenderTag(tag);

    // lod
    auto loda = new GT_DAConstantValue<int>(1, 1<<lod);
    detail = GT_AttributeList::createAttributeList(GT_Names::view_lod_mask,
						   loda);

    int ntransforms = myInstanceTransforms ? myInstanceTransforms->entries():1;

    auto lodu = new GT_DAConstantValue<int>(ntransforms, lod);
    uniform = GT_AttributeList::createAttributeList(GT_Names::view_lod, lodu);

    const int nt = myInstanceTransforms ? myInstanceTransforms->entries() : 1;
    auto &&inames = myHydraPrim.instanceIDs();

    myHydraPrim.setInstanced(nt > 1);
    
    // Prim IDs
    if(instance_change)
    {
	if(inames.entries() == 0)
	{
	    // identifer
	    myPickIDArray = new GT_DAConstantValue<int>(1, myHydraPrim.id());
	    mySelection   = new GT_DAConstantValue<int>(1, 0);
	}
	else
	{
	    myPickIDArray = new GT_DANumeric<int>(inames.array(), nt, 1);
	    auto sel = new GT_DANumeric<int>(nt,1);
	    memset(sel->data(), 0, sizeof(int)*nt);
	    mySelection = sel;
	}
    }

    detail = detail->addAttribute(GT_Names::lop_pick_id, myPickIDArray, true);
    uniform = uniform->addAttribute(GT_Names::selection, mySelection, true);
    if(myInstanceOverridesAttrib)
    {
        uniform = uniform->addAttribute(
            GA_Names::material_override, myInstanceOverridesAttrib, true);

        uniform = uniform->addAttribute("MatID", myInstanceMatID, true);
    }

    
    // BBox
    if(*dirty_bits & HdChangeTracker::DirtyExtent)
	if(!addBBoxAttrib(scene_delegate, proto_id, detail, geo))
	    addBBoxAttrib(scene_delegate, inst_id, detail, geo);

    if(mat_id != -1)
    {
	auto matda = new GT_DAConstantValue<int>(1, mat_id);
	detail = detail->addAttribute("MatID", matda, true);
	//UTdebugPrint("assign material", mat_id);
    }

    // create the container packed prim.
    myInstance = new GT_PrimInstance(geo, myInstanceTransforms,
				     GT_GEOOffsetList(), // no offsets exist.
				     uniform,  detail);

    myGTPrim = geo;

    if(myHydraPrim.index() == -1)
	myHydraPrim.scene().addDisplayGeometry(&myHydraPrim);
}

void
XUSD_HydraGeoBase::removeFromDisplay()
{
    if(myHydraPrim.index() != -1)
	myHydraPrim.scene().removeDisplayGeometry(&myHydraPrim);
}


void
XUSD_HydraGeoBase::updateGTSelection()
{
    auto &scene = myHydraPrim.scene();
    auto &ipaths = myHydraPrim.instanceIDs();
    const int ni = ipaths.entries();

    if(ni > 0)
    {
	auto sel_da = static_cast<GT_DANumeric<int> *>(mySelection.get());
	if(sel_da)
	{
	    if(scene.hasSelection())
	    {
                if(myHydraPrim.isPointInstanced() &&
                   scene.isSelected(myHydraPrim.id()))
                {
                    for(int i=0; i<ni; i++)
                        sel_da->set(1, i);
                }
                else
                {
                    UT_ASSERT(ni == sel_da->entries());
                    for(int i=0; i<ni; i++)
                        sel_da->set(scene.isSelected(ipaths(i)), i);
                }
	    }
	    else
	    {
		for(int i=0; i<ni; i++)
		    sel_da->set(0, i);
	    }
	}
    }
    else
    {
	auto sel_da = static_cast<GT_DAConstantValue<int> *>(mySelection.get());
	if(sel_da)
	{
	    if(myHydraPrim.scene().hasSelection())
	    {
		bool selected = myHydraPrim.scene().isSelected(&myHydraPrim);
		sel_da->set(selected ?1 :0);
	    }
	    else
            {
		sel_da->set(0);
            }
	}
    }
}

void
XUSD_HydraGeoBase::clearGTSelection()
{
    const int ni =  myHydraPrim.instanceIDs().entries();
    if(ni > 0)
    {
	auto sel_da = static_cast<GT_DANumeric<int> *>(mySelection.get());
	if(sel_da)
            for(int i=0; i<ni; i++)
                sel_da->set(0, i);
    }
    else
    {
	auto sel_da = static_cast<GT_DAConstantValue<int> *>(mySelection.get());
	if(sel_da)
            sel_da->set(0);
    }
}


// -------------------------------------------------------------------------

XUSD_HydraGeoMesh::XUSD_HydraGeoMesh(TfToken const& type_id,
				     SdfPath const& prim_id,
				     SdfPath const& instancer_id,
				     GT_PrimitiveHandle &gt_prim,
				     GT_PrimitiveHandle &instance,
				     int &dirty,
				     XUSD_HydraGeoPrim &hprim)
    : HdMesh(prim_id, instancer_id),
      XUSD_HydraGeoBase(gt_prim, instance, dirty, hprim),
      myTopHash(0),
      myIsSubD(false),
      myIsLeftHanded(true),
      myRefineLevel(0)
{
}

XUSD_HydraGeoMesh::~XUSD_HydraGeoMesh()
{
    //UTdebugPrint("Delete prim ", myId);
}
  
void
XUSD_HydraGeoMesh::Finalize(HdRenderParam *renderParam)
{
    resetPrim();
    myCounts.reset();
    myVertex.reset();
   
    HdRprim::Finalize(renderParam);
    
    //UTdebugPrint("Finalize prim ");
}


HdDirtyBits
XUSD_HydraGeoMesh::GetInitialDirtyBitsMask() const
{
    static const int	mask 	= HdChangeTracker::AllDirty;

    return (HdDirtyBits)mask;
}

HdDirtyBits
XUSD_HydraGeoMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
XUSD_HydraGeoMesh::_InitRepr(TfToken const &representation,
			     HdDirtyBits *dirty_bits)
{
}

void
XUSD_HydraGeoMesh::Sync(HdSceneDelegate *scene_delegate,
			HdRenderParam *rparm,
			HdDirtyBits *dirty_bits,
			TfToken const &representation)
{
    if(isDeferred(rparm, *dirty_bits))
    {
        if(myHydraPrim.index() == -1)
            myHydraPrim.scene().addDisplayGeometry(&myHydraPrim);
	return;
    }

    SdfPath const	&id = GetId();

    UT_AutoLock prim_lock(myHydraPrim.lock());
#if 0
    static UT_Lock theDebugLock;
    UT_AutoLock locker(theDebugLock);
    UTdebugPrint("Sync", id.GetText(), myHydraPrim.id(),
       		 GetInstancerId().IsEmpty() ? "" : "instanced",
                 GetInstancerId().GetText(),
     		 representation.GetText());
    HdChangeTracker::DumpDirtyBits(*dirty_bits);
#endif
    GT_Primitive       *gt_prim = myGTPrim.get();
    int64		top_id = 1;
    UT_Array<GT_PrimSubdivisionMesh::Tag> subd_tags;

    // Materials
    bool		dirty_materials = false;
	
    if(*dirty_bits & HdChangeTracker::DirtyMaterialId)
    {
	SdfPath mat_id = scene_delegate->GetMaterialId(GetId());

	_SetMaterialId(scene_delegate->GetRenderIndex().GetChangeTracker(),
		       mat_id);

	myHydraPrim.setMaterial(mat_id.GetText());
        myExtraAttribs.clear();
        myMaterialID = -1;

        if(!mat_id.IsEmpty())
        {
            UT_StringHolder path(mat_id.GetText());
            auto entry = myHydraPrim.scene().materials().find(path);
            if(entry != myHydraPrim.scene().materials().end())
            {
                auto &hmat = entry->second;
                if(hmat->isValid())
                {
                    // ensure these attribs are present on the geometry.
                    for(auto &it : hmat->requiredUVs())
                        myExtraAttribs[it.first] = it.first;
                    for(auto &it : hmat->shaderParms())
                        myExtraAttribs[it.second] = it.first;
                
                    myMaterialID = hmat->getMaterialID();
                }
            }
        }

	myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::MAT_CHANGE;
	dirty_materials = true;
    }

    // Available attributes
    if(!gt_prim || myAttribMap.size() == 0 ||
       	HdChangeTracker::IsTopologyDirty(*dirty_bits, id))
    {
	XUSD_HydraUtils::buildAttribMap(scene_delegate, id, myAttribMap);
    }

    GEO_ViewportLOD lod = checkVisibility(scene_delegate, id, dirty_bits);
    if(lod == GEO_VIEWPORT_HIDDEN)
	return;

    // Instancing
    GT_TransformHandle th;
    
    // Transforms
    if (!gt_prim || HdChangeTracker::IsTransformDirty(*dirty_bits, id))
    {
	myPrimTransform = GfMatrix4d(scene_delegate->GetTransform(id));
	myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::INSTANCE_CHANGE;
    }
    
    // Topology
    if(gt_prim && gt_prim->getDetailAttributes())
    {
	auto top = gt_prim->getDetailAttributes()->get(GT_Names::topology);
	if(top)
	    top_id = top->getI64(0);
    }

    bool need_gt_update = (!myCounts || !myVertex || !gt_prim);

    if (need_gt_update || dirty_materials ||
	HdChangeTracker::IsTopologyDirty(*dirty_bits, id))
    {
	auto &&top = HdMeshTopology(GetMeshTopology(scene_delegate), 0);
	
	if(HdChangeTracker::IsTopologyDirty(*dirty_bits, id))
	{
	    int64 top_hash = top.ComputeHash();

	    //UTdebugPrint("Orient", top.GetOrientation().GetText());
	    myIsLeftHanded = (top.GetOrientation() != HdTokens->rightHanded);
	
	    if(need_gt_update || top_hash != myTopHash)
	    {
		myTopHash = top_hash;
		if(top.GetNumPoints() > 0)
		{
		    myCounts =
		     XUSD_HydraUtils::createGTArray(top.GetFaceVertexCounts());
		    myVertex =
		     XUSD_HydraUtils::createGTArray(top.GetFaceVertexIndices());

		    if (top.GetScheme()==PxOsdOpenSubdivTokens->catmullClark ||
			top.GetScheme()==PxOsdOpenSubdivTokens->catmark)
		    {
			myIsSubD = true;
		    }
		    else
			myIsSubD = false;
		}
		else
		{
		    myCounts.reset();
		    myVertex.reset();
		    myIsSubD = false;
		}
		top_id = XUSD_HydraUtils::newDataId();
		myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::TOP_CHANGE;
	    }
	}
	
	if(dirty_materials)
	{
	    auto &subsets = top.GetGeomSubsets();
	    if(subsets.size() > 0)
	    {
                UT_Map<int,int> materials;
		auto matid_da = new GT_DANumeric<int>(top.GetNumFaces(), 1);
		memset(matid_da->data(), 0xFF, matid_da->entries()*sizeof(int));
		
		for(auto &subset : subsets)
		{
		    UT_StringHolder mapname(subset.materialId.GetText());

		    // UTdebugPrint("Subset name", subset.id.GetText());
		    // UTdebugPrint("Material =", mapname);
		    // UTdebugPrint("# faces =", subset.indices.size());
		    auto entry = myHydraPrim.scene().materials().find(mapname);
		    if(entry != myHydraPrim.scene().materials().end())
		    {
			auto &hmat = entry->second;
			// ensure these attribs are present on the generated
			// geometry.
			for(auto &it : hmat->requiredUVs())
			    myExtraAttribs[it.first] = it.first;
                        for(auto &it : hmat->shaderParms())
                            myExtraAttribs[it.second] = it.first;
                        
			int matid = hmat->isValid() ? hmat->getMaterialID() : -1;
			for(auto index : subset.indices)
			    matid_da->set(matid, index);

                        materials[ matid] = 1;
		    }
		}
                auto mats_da = new GT_DANumeric<int>(materials.size(), 1);
                int *data = mats_da->data();
                for(auto it : materials)
                    *data++ = it.first;
                
		myMatIDArray = matid_da;
                myMaterialsArray = mats_da;
	    }
	    else
            {
		myMatIDArray.reset();
		myMaterialsArray.reset();
            }
	}
    }

    if(!myCounts || !myVertex)
    {
	myInstance.reset();
	myGTPrim.reset();
	clearDirty(dirty_bits);
	removeFromDisplay();
	return;
    }

    if(!GetInstancerId().IsEmpty())
    {
        // UTdebugPrint("Primvar attribs");
        // for(auto a_it : myExtraAttribs)
        //     UTdebugPrint("  ", a_it.first);

        buildShaderInstanceOverrides(scene_delegate,
                                     GetInstancerId(),
                                     id, dirty_bits);
    }
    else
    {
        myHydraPrim.hasMaterialOverrides(false);
        myInstanceAttribList = nullptr;
        myInstanceOverridesAttrib = nullptr;
        myInstanceMatID = nullptr;
        myInstanceTransforms = nullptr;
    }

    buildTransforms(scene_delegate, id, GetInstancerId(), dirty_bits, th);
    if(myInstanceTransforms && myInstanceTransforms->entries() == 0)
    {
        // zero instance transforms means nothing should be displayed.
        removeFromDisplay();
        return;
    }
        
    if(*dirty_bits & HdChangeTracker::DirtyDisplayStyle)
	myRefineLevel = scene_delegate->GetDisplayStyle(id).refineLevel;

    if (HdChangeTracker::IsSubdivTagsDirty(*dirty_bits, id) &&
	myIsSubD &&
	myRefineLevel > 0)
    {
	XUSD_HydraUtils::processSubdivTags(
	    scene_delegate->GetSubdivTags(id), subd_tags);
    }

    // Populate attributes
    GT_AttributeListHandle attrib_list[GT_OWNER_MAX];
    
    const bool has_n = (myAttribMap.find(HdTokens->normals.GetText()) !=
			myAttribMap.end());
    auto wnd = new GT_DAConstantValue<int>(1, myIsLeftHanded?0:1, 1);
    auto top = new GT_DAConstantValue<int64>(1, top_id, 1);
    auto nmlgen = new GT_DAConstantValue<int>(1, !has_n, 1);
    attrib_list[GT_OWNER_DETAIL] =
	GT_AttributeList::createAttributeList(GT_Names::topology,top,
					      GT_Names::winding_order,wnd,
					      GT_Names::nml_generated,nmlgen);
    int point_freq = 0;
    bool pnt_exists = false;
    updateAttrib(HdTokens->points, "P"_sh, scene_delegate, id, dirty_bits,
		 gt_prim, attrib_list, &point_freq, true, &pnt_exists);

    if(!pnt_exists)
    {
	myInstance.reset();
	myGTPrim.reset();
	clearDirty(dirty_bits);
	removeFromDisplay();
	return;
    }

    // additional, optional attributes
    updateAttrib(HdTokens->displayColor, "Cd"_sh,
		 scene_delegate, id, dirty_bits, gt_prim, attrib_list,
		 &point_freq);
    updateAttrib(HdTokens->normals, "N"_sh,
		 scene_delegate, id, dirty_bits, gt_prim, attrib_list,
		 &point_freq);
    updateAttrib(HdTokens->displayOpacity, "Alpha"_sh,
		 scene_delegate, id, dirty_bits, gt_prim, attrib_list);

    for(auto &itr : myExtraAttribs)
    {
	auto &attrib = itr.first;
	auto entry = myAttribMap.find(attrib);
	if(entry != myAttribMap.end())
	{
	    TfToken htoken(attrib);
	    updateAttrib(htoken, attrib, scene_delegate, id,
			 dirty_bits, gt_prim, attrib_list, &point_freq);
	}
    }

    if(myMatIDArray)
    {
	if(attrib_list[GT_OWNER_UNIFORM])
	    attrib_list[GT_OWNER_UNIFORM] = attrib_list[GT_OWNER_UNIFORM]
		->addAttribute("MatID"_sh, myMatIDArray, true);
	else
	    attrib_list[GT_OWNER_UNIFORM] =
		GT_AttributeList::createAttributeList("MatID"_sh, myMatIDArray);

        attrib_list[GT_OWNER_DETAIL] = attrib_list[GT_OWNER_DETAIL]
		->addAttribute("materials"_sh, myMaterialsArray, true);
    }

    // uniform and detail normals aren't supported by the renderer.
    // convert to vertex and point normals instead.
    if(attrib_list[GT_OWNER_UNIFORM] &&
       attrib_list[GT_OWNER_UNIFORM]->get(GA_Names::N))
    {
	GT_DataArrayHandle nml=attrib_list[GT_OWNER_UNIFORM]->get(GA_Names::N);
	const int nprim = myCounts->entries();
	const int nvert = myVertex->entries();
	auto index = new GT_DANumeric<int>(nvert, 1);
	int *data = index->data();
	int idx = 0;
	for(int i=0; i<nprim; i++)
	{
	    const int count = myCounts->getI32(i);
	    for(int j=0; j<count && idx<nvert; j++,idx++)
		data[idx] = i;
	}

	GT_DataArrayHandle indexh = index;
	GT_DataArrayHandle nh = new GT_DAIndirect(index, nml);

	if(attrib_list[GT_OWNER_VERTEX])
	{
	    attrib_list[GT_OWNER_VERTEX] = 
		attrib_list[GT_OWNER_VERTEX]->addAttribute(GA_Names::N,nh,true);
	}
	else
	    attrib_list[GT_OWNER_VERTEX] = 
		GT_AttributeList::createAttributeList(GA_Names::N,nh);
	
	attrib_list[GT_OWNER_UNIFORM]
	    = attrib_list[GT_OWNER_UNIFORM]->removeAttribute(GA_Names::N);
    }
    else if(attrib_list[GT_OWNER_DETAIL] &&
	    attrib_list[GT_OWNER_DETAIL]->get(GA_Names::N))
    {
	GT_DataArrayHandle nml=attrib_list[GT_OWNER_DETAIL]->get(GA_Names::N);
	GT_DataArrayHandle nh = new GT_DAConstant(nml, 0, point_freq);
	
	attrib_list[GT_OWNER_POINT] = 
	    attrib_list[GT_OWNER_POINT]->addAttribute(GA_Names::N,nh, true);

	attrib_list[GT_OWNER_DETAIL] =
	    attrib_list[GT_OWNER_DETAIL]->removeAttribute(GA_Names::N);

    }
        
    // build mesh
    GT_PrimPolygonMesh *mesh = nullptr;
    if(myIsSubD && myRefineLevel > 0)
    {
	auto smesh = new GT_PrimSubdivisionMesh(myCounts, myVertex,
						attrib_list[GT_OWNER_POINT],
						attrib_list[GT_OWNER_VERTEX],
						attrib_list[GT_OWNER_UNIFORM],
						attrib_list[GT_OWNER_DETAIL]);
	for (int i = 0; i < subd_tags.size(); ++i)
	    smesh->appendTag(subd_tags[i]);
	
	mesh = smesh;
    }
    else
    {
	mesh = new GT_PrimPolygonMesh(myCounts, myVertex,
				      attrib_list[GT_OWNER_POINT],
				      attrib_list[GT_OWNER_VERTEX],
				      attrib_list[GT_OWNER_UNIFORM],
				      attrib_list[GT_OWNER_DETAIL]);
    }

    auto norm_mesh = mesh->createPointNormalsIfMissing();
    if(norm_mesh)
    {
	delete mesh;
	mesh = norm_mesh;
    }

#if 0
    static UT_Lock theLock;
    theLock.lock();
    mesh->dumpAttributeLists("XUSD_HydraGeoPrim", false);
    theLock.unlock();
#endif
    
    if(th)
	mesh->setPrimitiveTransform(th);

    createInstance(scene_delegate, id, GetInstancerId(), dirty_bits, mesh, lod,
		   myMaterialID, 
		   (*dirty_bits & (HdChangeTracker::DirtyInstancer |
				   HdChangeTracker::DirtyInstanceIndex )));
    
    clearDirty(dirty_bits);
}

// -------------------------------------------------------------------------

XUSD_HydraGeoCurves::XUSD_HydraGeoCurves(TfToken const& type_id,
					 SdfPath const& prim_id,
					 SdfPath const& instancer_id,
					 GT_PrimitiveHandle &prim,
					 GT_PrimitiveHandle &instance,
					 int &dirty,
					 XUSD_HydraGeoPrim &hprim)
    : HdBasisCurves(prim_id, instancer_id),
      XUSD_HydraGeoBase(prim, instance, dirty, hprim),
      myBasis(GT_BASIS_LINEAR),
      myWrap(false)
{
}

XUSD_HydraGeoCurves::~XUSD_HydraGeoCurves()
{
}

void
XUSD_HydraGeoCurves::Sync(HdSceneDelegate *scene_delegate,
			  HdRenderParam *rparm,
			  HdDirtyBits *dirty_bits,
			  TfToken const &representation)
{
    if(isDeferred(rparm, *dirty_bits))
    {
        if(myHydraPrim.index() == -1)
            myHydraPrim.scene().addDisplayGeometry(&myHydraPrim);
	return;
    }
    
    SdfPath const      &id = GetId();
    GT_Primitive       *gt_prim = myBasisCurve.get();
    int64		top_id = 1;

    // UTdebugPrint("Sync", id.GetText(), myHydraPrim.id(),
    //     		 GetInstancerId().GetText(),
    //     		 representation.GetText());
    // HdChangeTracker::DumpDirtyBits(*dirty_bits);
    
    UT_AutoLock prim_lock(myHydraPrim.lock());
    
    // available attributes
    if(!gt_prim || myAttribMap.size() == 0 ||
       	HdChangeTracker::IsTopologyDirty(*dirty_bits, id))
    {
	UT_Map<GT_Owner, GT_Owner> remap;
	remap[GT_OWNER_POINT] = GT_OWNER_VERTEX;
	XUSD_HydraUtils::buildAttribMap(scene_delegate, id, myAttribMap,
					&remap);
    }

    // Visibility
    GEO_ViewportLOD lod = checkVisibility(scene_delegate, id, dirty_bits);
    if(lod == GEO_VIEWPORT_HIDDEN)
	return;

    // Transforms
    if (!gt_prim || HdChangeTracker::IsTransformDirty(*dirty_bits, id))
    {
	myPrimTransform = GfMatrix4d(scene_delegate->GetTransform(id));
	myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::INSTANCE_CHANGE;
    }

    GT_TransformHandle th;
    buildTransforms(scene_delegate, id, GetInstancerId(), dirty_bits, th);
    if(myInstanceTransforms && myInstanceTransforms->entries() == 0)
    {
	// zero instance transforms means nothing should be displayed.
	removeFromDisplay();
	return;
    }

    // Topology
    if(gt_prim && gt_prim->getDetailAttributes())
    {
	auto top = gt_prim->getDetailAttributes()->get(GT_Names::topology);
	if(top)
	    top_id = top->getI64(0);
    }
    
    if (!myCounts || !gt_prim || 
	HdChangeTracker::IsTopologyDirty(*dirty_bits, id))
    {
	auto top = GetBasisCurvesTopology(scene_delegate);
	top_id ++;

	TfToken ctype = top.GetCurveType();
	if(ctype == HdTokens->cubic)
	{
	    TfToken basis = top.GetCurveBasis();
	    if(basis == HdTokens->bezier)
		myBasis = GT_BASIS_BEZIER;
	    else if(basis == HdTokens->bSpline)
		myBasis = GT_BASIS_BSPLINE;
	    else if(basis == HdTokens->catmullRom)
		myBasis = GT_BASIS_CATMULLROM;
	}
	else
	    myBasis = GT_BASIS_LINEAR;
	    
	myWrap = (top.GetCurveWrap() == HdTokens->periodic);

	if(top.GetCurveWrap() != HdTokens->segmented)
	    myCounts=XUSD_HydraUtils::createGTArray(top.GetCurveVertexCounts());
	else
	{
	    int num = top.CalculateNeededNumberOfControlPoints();
	    myCounts = new GT_DAConstantValue<int32>(num, 2, 1);
	}

	if(top.HasIndices())
	    myIndices = XUSD_HydraUtils::createGTArray(top.GetCurveIndices());
	else
	    myIndices.reset();
	
	myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::TOP_CHANGE;
    }

    GT_AttributeListHandle attrib_list[GT_OWNER_MAX];
    
    auto top = new GT_DAConstantValue<int64>(1, top_id, 1);
    attrib_list[GT_OWNER_DETAIL] =
	GT_AttributeList::createAttributeList(GT_Names::topology,top);
    
    bool pnt_exists = false;
    updateAttrib(HdTokens->points, "P"_sh, scene_delegate, id, dirty_bits,
		 gt_prim, attrib_list, nullptr, false, &pnt_exists);
    if(!pnt_exists)
    {
	myInstance.reset();
	myGTPrim.reset();
	clearDirty(dirty_bits);
	return;
    }

    updateAttrib(HdTokens->displayColor, "Cd"_sh,
		 scene_delegate, id, dirty_bits, gt_prim, attrib_list);
    updateAttrib(HdTokens->displayOpacity, "Alpha"_sh,
		 scene_delegate, id, dirty_bits, gt_prim, attrib_list);

    GT_PrimitiveHandle ph;
    GT_AttributeListHandle verts;
    if(myIndices)
	verts = attrib_list[GT_OWNER_VERTEX]->createIndirect(myIndices);
    else
	verts = attrib_list[GT_OWNER_VERTEX];
	    
    auto cmesh = new GT_PrimCurveMesh(myBasis, myCounts, verts,
				      attrib_list[GT_OWNER_UNIFORM],
				      attrib_list[GT_OWNER_DETAIL],
				      myWrap);
    myBasisCurve = cmesh;
    if(myBasis != GT_BASIS_LINEAR)
    {
 	ph = cmesh->refineToLinear();
	if(!ph)
	    ph = cmesh;
	UT_ASSERT(ph);
    }
    else
	ph = cmesh;

    if(th)
	ph->setPrimitiveTransform(th);
    
    createInstance(scene_delegate, id, GetInstancerId(), dirty_bits, ph.get(),
		   lod, -1,
		   (*dirty_bits & (HdChangeTracker::DirtyInstancer |
				   HdChangeTracker::DirtyInstanceIndex)));

    // cmesh->dumpAttributeLists("XUSD_HydraGeoCurves", false);
    // if(attrib_list[GT_OWNER_VERTEX])
    // 	attrib_list[GT_OWNER_VERTEX]->dumpList("verts", false);
    
    clearDirty(dirty_bits);
}
    
void
XUSD_HydraGeoCurves::Finalize(HdRenderParam *rparms)
{
    resetPrim();
    HdRprim::Finalize(rparms);
}

HdDirtyBits
XUSD_HydraGeoCurves::GetInitialDirtyBitsMask() const
{
    static const int	mask
	= HdChangeTracker::Clean
	| HdChangeTracker::InitRepr
	| HdChangeTracker::DirtyPoints
	| HdChangeTracker::DirtyTopology
	| HdChangeTracker::DirtyTransform
	| HdChangeTracker::DirtyVisibility
	| HdChangeTracker::DirtyDisplayStyle
	| HdChangeTracker::DirtyCullStyle
	| HdChangeTracker::DirtyDoubleSided
	| HdChangeTracker::DirtySubdivTags
	| HdChangeTracker::DirtyPrimvar
	| HdChangeTracker::DirtyNormals
	| HdChangeTracker::DirtyInstanceIndex
	;

    return (HdDirtyBits)mask;
}

HdDirtyBits
XUSD_HydraGeoCurves::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
XUSD_HydraGeoCurves::_InitRepr(TfToken const &representation,
			       HdDirtyBits *dirty_bits)
{

}


// -------------------------------------------------------------------------

XUSD_HydraGeoVolume::XUSD_HydraGeoVolume(TfToken const& type_id,
					 SdfPath const& prim_id,
					 SdfPath const& instancer_id,
					 GT_PrimitiveHandle &gt_prim,
					 GT_PrimitiveHandle &instance,
					 int &dirty,
					 XUSD_HydraGeoPrim &hprim)
    : HdVolume(prim_id, instancer_id),
      XUSD_HydraGeoBase(gt_prim, instance, dirty, hprim)
{
    hprim.needsGLStateCheck(true);
}

XUSD_HydraGeoVolume::~XUSD_HydraGeoVolume()
{
}
  
void
XUSD_HydraGeoVolume::Finalize(HdRenderParam *rparm)
{
    // Here we clear out any resources.
    myHydraPrim.scene().removeVolumeUsingFields(GetId().GetString());

    resetPrim();
    HdRprim::Finalize(rparm);
}


HdDirtyBits
XUSD_HydraGeoVolume::GetInitialDirtyBitsMask() const
{
    static const int	mask
	= HdChangeTracker::Clean
	| HdChangeTracker::DirtyTransform
	| HdChangeTracker::DirtyVisibility
	| HdChangeTracker::DirtyCullStyle
	| HdChangeTracker::DirtyTopology
	;

    return (HdDirtyBits)mask;
}

HdDirtyBits
XUSD_HydraGeoVolume::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
XUSD_HydraGeoVolume::_InitRepr(TfToken const &representation,
			       HdDirtyBits *dirty_bits)
{
}

void
XUSD_HydraGeoVolume::Sync(HdSceneDelegate *scene_delegate,
			  HdRenderParam *rparm,
			  HdDirtyBits *dirty_bits,
			  TfToken const &representation)
{
    if(isDeferred(rparm, *dirty_bits))
    {
        if(myHydraPrim.index() == -1)
            myHydraPrim.scene().addDisplayGeometry(&myHydraPrim);
	return;
    }

    SdfPath const &id = GetId();
  
    GU_ConstDetailHandle	 gdh;
    GT_PrimitiveHandle		 gtvolume;
    
    UT_AutoLock prim_lock(myHydraPrim.lock());
    
    // available attributes
    if(myAttribMap.size() == 0 ||
       	HdChangeTracker::IsTopologyDirty(*dirty_bits, id))
    {
	UT_Map<GT_Owner, GT_Owner> remap;
	remap[GT_OWNER_POINT] = GT_OWNER_VERTEX;
	XUSD_HydraUtils::buildAttribMap(scene_delegate, id, myAttribMap,
					&remap);
    }
    
    // Visibility
    GEO_ViewportLOD lod = checkVisibility(scene_delegate, id, dirty_bits);
    if(lod == GEO_VIEWPORT_HIDDEN)
    {
	removeFromDisplay();
	return;
    }

    // Transforms
    if (!gtvolume || HdChangeTracker::IsTransformDirty(*dirty_bits, id))
    {
	myPrimTransform = GfMatrix4d(scene_delegate->GetTransform(id));
	myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::INSTANCE_CHANGE;
    }
    
    GT_TransformHandle th;
    buildTransforms(scene_delegate, id, GetInstancerId(), dirty_bits, th);
    if(myInstanceTransforms && myInstanceTransforms->entries() == 0)
    {
	// zero instance transforms means nothing should be displayed.
	removeFromDisplay();
	return;
    }

    // 3D texture for the volume.
    for (auto &&desc : scene_delegate->GetVolumeFieldDescriptors(id))
    {
	HdBprim const *bprim = scene_delegate->GetRenderIndex().GetBprim(
	    desc.fieldPrimType, desc.fieldId);

	if (bprim)
	{
	    const XUSD_HydraField *field =
		static_cast<const XUSD_HydraField *>(bprim);

	    gtvolume = field->getGTPrimitive();
	    myHydraPrim.scene().addVolumeUsingField(
		id.GetString(), desc.fieldId.GetString());
	    myDirtyMask |= HUSD_HydraGeoPrim::TOP_CHANGE;
	    break;
	}
    }

    // If there were no field prims for this volumes, just exit.
    if (!gtvolume)
    {
	removeFromDisplay();
	return;
    }

    if(*dirty_bits & HdChangeTracker::DirtyTopology)
	myDirtyMask |= HUSD_HydraGeoPrim::TOP_CHANGE;

    clearDirty(dirty_bits);

    // create the container packed prim.
    createInstance(scene_delegate, id, GetInstancerId(), dirty_bits,
		   gtvolume.get(), lod, -1,
		   (*dirty_bits & (HdChangeTracker::DirtyInstancer |
				   HdChangeTracker::DirtyInstanceIndex)));
    if(th)
	gtvolume->setPrimitiveTransform(th);

}

// --------------------------------------------------------------------------

XUSD_HydraGeoPoints::XUSD_HydraGeoPoints(TfToken const& type_id,
					 SdfPath const& prim_id,
					 SdfPath const& instancer_id,
					 GT_PrimitiveHandle &gt_prim,
					 GT_PrimitiveHandle &instance,
					 int &dirty,
					 XUSD_HydraGeoPrim &hprim)
    : HdPoints(prim_id, instancer_id),
      XUSD_HydraGeoBase(gt_prim, instance, dirty, hprim)
{
}

XUSD_HydraGeoPoints::~XUSD_HydraGeoPoints()
{
}

void
XUSD_HydraGeoPoints::Sync(HdSceneDelegate *scene_delegate,
			  HdRenderParam *rparm,
			  HdDirtyBits *dirty_bits,
			  TfToken const &representation)
{
    if(isDeferred(rparm, *dirty_bits))
    {
        if(myHydraPrim.index() == -1)
            myHydraPrim.scene().addDisplayGeometry(&myHydraPrim);
	return;
    }
    
    SdfPath const      &id = GetId();
    GT_Primitive       *gt_prim = myGTPrim.get();
    GT_AttributeListHandle attrib_list[GT_OWNER_MAX];

    UT_AutoLock prim_lock(myHydraPrim.lock());
    
    // available attributes
    if(!gt_prim || myAttribMap.size() == 0 ||
       	HdChangeTracker::IsTopologyDirty(*dirty_bits, id))
    {
	XUSD_HydraUtils::buildAttribMap(scene_delegate, id, myAttribMap);
    }
    
    // Visibility
    GEO_ViewportLOD lod = checkVisibility(scene_delegate, id, dirty_bits);
    if(lod == GEO_VIEWPORT_HIDDEN)
    {
	removeFromDisplay();
	return;
    }

    // Transforms
    if (!gt_prim || HdChangeTracker::IsTransformDirty(*dirty_bits, id))
    {
	myPrimTransform = GfMatrix4d(scene_delegate->GetTransform(id));
	myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::INSTANCE_CHANGE;
    }
    
    GT_TransformHandle th;
    buildTransforms(scene_delegate, id, GetInstancerId(), dirty_bits, th);
    if(myInstanceTransforms && myInstanceTransforms->entries() == 0)
    {
	// zero instance transforms means nothing should be displayed.
	removeFromDisplay();
	return;
    }

    updateAttrib(HdTokens->points, "P"_sh,
		 scene_delegate, id, dirty_bits, gt_prim, attrib_list);
    updateAttrib(HdTokens->displayColor, "Cd"_sh,
		 scene_delegate, id, dirty_bits, gt_prim, attrib_list);

    auto points = new GT_PrimPointMesh(attrib_list[GT_OWNER_POINT],
				       attrib_list[GT_OWNER_DETAIL]);

    createInstance(scene_delegate, id, GetInstancerId(), dirty_bits, points,
		   lod, -1,
 		   (*dirty_bits & (HdChangeTracker::DirtyInstancer |
				   HdChangeTracker::DirtyInstanceIndex)));
    if(th)
	points->setPrimitiveTransform(th);
    
    clearDirty(dirty_bits);   
}
    
void
XUSD_HydraGeoPoints::Finalize(HdRenderParam *rparm)
{
    resetPrim();
    HdRprim::Finalize(rparm);
}

HdDirtyBits
XUSD_HydraGeoPoints::GetInitialDirtyBitsMask() const
{
    static const int	mask
	= HdChangeTracker::Clean
	| HdChangeTracker::InitRepr
	| HdChangeTracker::DirtyPoints
	| HdChangeTracker::DirtyTopology
	| HdChangeTracker::DirtyTransform
	| HdChangeTracker::DirtyVisibility
	| HdChangeTracker::DirtyCullStyle
	| HdChangeTracker::DirtyDoubleSided
	| HdChangeTracker::DirtySubdivTags
	| HdChangeTracker::DirtyPrimvar
	| HdChangeTracker::DirtyNormals
	| HdChangeTracker::DirtyInstanceIndex
	;
    return mask;
}


HdDirtyBits
XUSD_HydraGeoPoints::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
XUSD_HydraGeoPoints::_InitRepr(TfToken const &representation,
			       HdDirtyBits *dirty_bits)
{
}

 
PXR_NAMESPACE_CLOSE_SCOPE
