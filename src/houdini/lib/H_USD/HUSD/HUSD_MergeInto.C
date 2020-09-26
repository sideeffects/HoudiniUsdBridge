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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_MergeInto.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_Utils.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <vector>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

struct HUSD_MergeInto::husd_MergeIntoPrivate {
    struct MaterialAndPaths
    {
	SdfPath			 myMaterialPath;
	UT_Array<SdfPath>	 myPaths;
    };
    struct XformAndPaths
    {
	GfMatrix4d		 myXform;
	UT_Array<SdfPath>	 myPaths;
    };
    XUSD_LayerArray		 mySubLayers;
    XUSD_TicketArray		 myTicketArray;
    XUSD_LayerArray		 myReplacementLayerArray;
    HUSD_LockedStageArray	 myLockedStageArray;
    UT_StringArray		 myDestPaths;
    UT_StringArray		 mySourceNodePaths;
    UT_StringArray		 mySourcePaths;
    UT_FprealArray		 myFrameOffsets;
    UT_FprealArray		 myFramerateScales;

    UT_Map<SdfLayerRefPtr,MaterialAndPaths>	  myInheritedMaterials;
    UT_Map<SdfLayerRefPtr,XformAndPaths>	  myInheritedXforms;
};

HUSD_MergeInto::HUSD_MergeInto()
    : myPrivate(new HUSD_MergeInto::husd_MergeIntoPrivate()),
      myParentPrimType(HUSD_Constants::getXformPrimType()),
      myMakeUniqueDestPaths(false)
{
}

HUSD_MergeInto::~HUSD_MergeInto()
{
}

bool
HUSD_MergeInto::addHandle(const HUSD_DataHandle &src,
	const UT_StringHolder	&dest_path,
	const UT_StringHolder	&source_node_path,
	const UT_StringHolder	&source_path /*=UT_StringHolder()*/,
	fpreal			 frame_offset /*=0*/,
	fpreal			 framerate_scale /*=1*/,
	bool			 inherit_xform /*=false*/,
	bool			 inherit_material /*=false*/)
{
    HUSD_AutoReadLock	 inlock(src);
    const auto		&indata = inlock.data();
    bool		 success = false;

    if (indata && indata->isStageValid())
    {
	// Record the path to that destination prim (if one is supplied).
	myPrivate->myDestPaths.append(dest_path);
	myPrivate->mySourceNodePaths.append(source_node_path);
	myPrivate->mySourcePaths.append(source_path);
	myPrivate->myFrameOffsets.append(frame_offset);
	myPrivate->myFramerateScales.append(framerate_scale);

	// We must flatten the layers of the stage so that we can use
	// SdfCopySpec safely.  Flattening the layers (even if it's just one
	// layer) smoothes out a lot of problems with time scaling, reference
	// file paths, and other issues that can cause problems if we use
	// copyspec directly from the source layer.
	SdfLayerRefPtr flattenedlayer =
	        indata->createFlattenedLayer(HUSD_WARN_STRIPPED_LAYERS);
	myPrivate->mySubLayers.append(flattenedlayer);

	// Hold onto tickets to keep in memory any cooked OP data referenced
	// by the layers being merged.
	myPrivate->myTicketArray.concat(indata->tickets());
	myPrivate->myReplacementLayerArray.concat(indata->replacements());
	myPrivate->myLockedStageArray.concat(indata->lockedStages());

	if (inherit_xform && source_path != HUSD_Constants::getRootPrimPath())
	{
	    UsdPrim srcprim = indata->stage()->GetPrimAtPath(
	            HUSDgetSdfPath(source_path));
	    // We want to capture the parent's local-to-world transformation
	    // (so we can keep the locally-authored transform stack)
	    if (srcprim && !srcprim.IsPseudoRoot())
	    {
		UsdGeomXformable xformable(srcprim);
		if (xformable)
		{
		    // TODO - revisit this if we need to handle animated xforms
		    GfMatrix4d xform =
		            xformable.ComputeParentToWorldTransform(UsdTimeCode());
		    myPrivate->myInheritedXforms[flattenedlayer].myXform = xform;
		}
	    }
	}

	if (inherit_material && source_path != HUSD_Constants::getRootPrimPath())
	{
	    UsdPrim srcprim = indata->stage()->GetPrimAtPath(
	            HUSDgetSdfPath(source_path));

	    UsdShadeMaterialBindingAPI matAPI(srcprim);
	    // We won't need to reauthor material bindings that are already
	    // directly authored on this primitive, so skip them.
	    if (!matAPI.GetDirectBindingRel())
	    {
		UsdShadeMaterial mat = matAPI.ComputeBoundMaterial();
		if (mat)
		{
		    myPrivate->myInheritedMaterials[flattenedlayer].myMaterialPath =
		            mat.GetPath();
		}
	    }
	}
	
	success = true;
    }

    return success;
}

bool
HUSD_MergeInto::execute(HUSD_AutoLayerLock &lock) const
{
    bool			 success = false;
    auto			 outdata = lock.constData();

    if (outdata && outdata->isStageValid() &&
	lock.layer() && lock.layer()->layer())
    {
	auto		 stage = outdata->stage();
	auto		 outlayer = lock.layer()->layer();
	SdfChangeBlock	 changeblock;
	int		 idx = 0;

	success = true;
	for (auto &&inlayer : myPrivate->mySubLayers)
	{
	    UT_String	 outpathstr;
	    SdfPath	 outroot;
	    std::string	 parent_prim_type;
	    std::string	 primkind = myPrimKind.toStdString();
	    auto	 sourceroot = inlayer->GetPseudoRoot();
	    bool	 mergingrootprim = true;
	    fpreal	 frameoffset = myPrivate->myFrameOffsets(idx);
	    fpreal	 frameratescale = myPrivate->myFramerateScales(idx);

	    if (myPrivate->mySourcePaths(idx) &&
	        myPrivate->mySourcePaths(idx) != HUSD_Constants::getRootPrimPath())
	    {
		SdfPath sourcepath(myPrivate->mySourcePaths(idx).toStdString());
		sourceroot = inlayer->GetPrimAtPath(sourcepath);
		if(!sourceroot)
		{
		    HUSD_ErrorScope::addError(HUSD_ERR_CANT_FIND_PRIM,
					      sourcepath.GetText());
		    continue;
		}
		mergingrootprim = false;
	    }

	    // If the "kind" is set to "automatic", look for the first child
	    // with a non-empty kind, and use the parent kind for that child
	    // kind.
	    if (myPrimKind == HUSD_Constants::getKindAutomatic())
	    {
		primkind = std::string();
		for (auto &&child : sourceroot->GetNameChildren())
		{
		    TfToken	 childkind = child->GetKind();
		    TfToken	 parentkind = HUSDgetParentKind(childkind);

		    if (!parentkind.IsEmpty())
		    {
			primkind = parentkind.GetString();
			break;
		    }
		}
	    }
	    parent_prim_type =
		HUSDgetPrimTypeAlias(myParentPrimType).toStdString();

	    // Get the destination path set when the layer was added.
	    // If no destination prim was provided, generate a path.
	    if (myPrivate->myDestPaths(idx).isstring())
		outpathstr = myPrivate->myDestPaths(idx);
	    else
		outpathstr.sprintf("/input%d", idx);

	    // If requested, make sure we don't conflict with any
	    // existing primitive on the stage or our layer.
	    // (if we're not merging the root prim we handle this later)
	    if (myMakeUniqueDestPaths && mergingrootprim)
	    {
		auto testpath = HUSDgetSdfPath(outpathstr);

		while (stage->GetPrimAtPath(testpath) ||
		    outlayer->GetPrimAtPath(testpath))
		{
		    outpathstr.incrementNumberedName(true);
		    testpath = HUSDgetSdfPath(outpathstr);
		}
	    }

	    outroot = HUSDgetSdfPath(outpathstr);
	    auto parentspec = HUSDcreatePrimInLayer(stage, outlayer, outroot,
		TfToken(primkind), true, parent_prim_type);
	    if (parentspec)
	    {
		if (!parent_prim_type.empty())
		    parentspec->SetTypeName(parent_prim_type);
		parentspec->SetSpecifier(SdfSpecifierDef);
		parentspec->SetCustomData(HUSDgetSourceNodeToken(),
		    VtValue(TfToken(myPrivate->mySourceNodePaths(idx).toStdString())));

		// In the event we're copying a complete layer from the root,
		// we'll instead copy the children.
		std::vector<SdfHandle<SdfPrimSpec>> primstocopy;
		if (mergingrootprim)
		    primstocopy = sourceroot->GetNameChildren().values();
		else
		    primstocopy = { sourceroot };

		for (auto &&prim : primstocopy)
		{
		    // Create a dummy prim to which we copy the source prim.
		    auto inpath = prim->GetPath();

		    // Don't merge in the HoudiniLayerInfo prim.
		    if (inpath.GetString() ==
			HUSD_Constants::getHoudiniLayerInfoPrimPath().c_str())
			continue;

		    auto outpath = outroot.AppendChild(inpath.GetNameToken());

		    // If requested, make sure we don't conflict with any
		    // existing primitive on the stage or our layer.
		    // (if we're merging the root prim we handle this earlier)
		    if (myMakeUniqueDestPaths && !mergingrootprim)
		    {
			UT_String testpathstr = outpath.GetString().c_str();

			while (stage->GetPrimAtPath(outpath) ||
			       outlayer->GetPrimAtPath(outpath))
			{
			    testpathstr.incrementNumberedName(true);
			    outpath = HUSDgetSdfPath(testpathstr);
			}
		    }

		    auto primspec = HUSDcreatePrimInLayer(
			stage, outlayer, outpath, TfToken(),
			true, parent_prim_type);

		    if (!primspec)
		    {
			success = false;
			break;
		    }
		    primspec->SetSpecifier(SdfSpecifierDef);

		    // Specific note when we're merging the root prim:
		    // Even though here we are copying the primspec inpath to
		    // outpath, when it comes to remapping references, we want
		    // references between these separate children to be
		    // updated to point to their new destination locations.
		    // So we pass in the parents of these children as the
		    // root prims for remapping purposes.
		    if (!HUSDcopySpec(inlayer, inpath, outlayer, outpath,
			mergingrootprim ? inpath.GetParentPath() : inpath,
			mergingrootprim ? outpath.GetParentPath() : outpath,
			frameoffset, frameratescale))
		    {
			success = false;
			break;
		    }
		    
		    if (!mergingrootprim && myPrivate->myInheritedXforms.contains(inlayer))
			myPrivate->myInheritedXforms[inlayer].myPaths.append(outpath);
		    if (!mergingrootprim && myPrivate->myInheritedMaterials.contains(inlayer))
			myPrivate->myInheritedMaterials[inlayer].myPaths.append(outpath);
		}
	    }
	    else
		success = false;

	    // If something went wrong in the inner loop, exit the outer
	    // loop too.
	    if (!success)
		break;

	    // Increment the counter for accessing into myDestPaths and
	    // mySourceNodePaths arrays.
	    idx++;
	}

	// Transfer ticket ownership from ourselves to the output data.
	lock.addTickets(myPrivate->myTicketArray);
	lock.addReplacements(myPrivate->myReplacementLayerArray);
	lock.addLockedStages(myPrivate->myLockedStageArray);
    }

    return success;
}

bool
HUSD_MergeInto::postExecuteAssignXform(
	HUSD_AutoWriteLock &lock,
	const UT_StringHolder &xform_suffix) const
{
    // Early-out if there's nothing to do
    if (myPrivate->myInheritedXforms.empty())
	return true;
    
    const auto &outdata = lock.data();
    if (!outdata || !outdata->isStageValid())
	return false;
    auto stage = outdata->stage();
    
    for (const auto &entry : myPrivate->myInheritedXforms)
    {
	const auto &xform = entry.second.myXform;
	for (const auto &primpath : entry.second.myPaths)
	{
	    UsdPrim prim = stage->GetPrimAtPath(primpath);
	    UT_ASSERT(prim);
	    if (!prim || prim.IsPseudoRoot())
	    {
		continue;
	    }

	    // Get parent (i.e., destination) transform
	    UsdGeomXformable xformable(prim);
	    UT_ASSERT(xformable);
	    if (!xformable)
	    {
		continue;
	    }
	    // TODO - revisit this if we need to handle animated xforms
	    GfMatrix4d parentxform =
	        xformable.ComputeParentToWorldTransform(UsdTimeCode());

	    // Capture the current local xform stack.
	    // NOTE - This *must* be done before we call `AddTransformOp` below
	    bool isreset;
	    std::vector<UsdGeomXformOp> oldxformorder
	            = xformable.GetOrderedXformOps(&isreset);

	    // Build a new transform stack starting with our inherited parent
	    // xform brought into the space of the current parent xform,
	    // and then the old local stack.
	    UsdGeomXformOp xformop = xformable.AddTransformOp(
	            UsdGeomXformOp::PrecisionDouble,
	            TfToken(xform_suffix.c_str()));
	    xformop.Set(xform * parentxform.GetInverse());
	    std::vector<UsdGeomXformOp> newxformorder;
	    newxformorder.push_back(xformop);
	    for (const auto &oldxformop : oldxformorder)
		newxformorder.push_back(oldxformop);
	    xformable.SetXformOpOrder(newxformorder);
	}
    }
    return true;
}

bool
HUSD_MergeInto::postExecuteAssignMaterial(HUSD_AutoWriteLock &lock) const
{
    // Early-out if there's nothing to do
    if (myPrivate->myInheritedMaterials.empty())
	return true;

    const auto &outdata = lock.data();
    if (!outdata || !outdata->isStageValid())
	return false;
    auto stage = outdata->stage();
    
    for (const auto &entry : myPrivate->myInheritedMaterials)
    {
	const SdfPath &materialpath = entry.second.myMaterialPath;
	if (materialpath.IsEmpty())
	    continue;
	
	UsdShadeMaterial material(stage->GetPrimAtPath(materialpath));
	if (!material)
	{
	    HUSD_ErrorScope::addWarning(HUSD_ERR_MISSING_MATERIAL_IN_TARGET,
	                                materialpath.GetText());
	    continue;
	}
	
	for (const SdfPath &primpath : entry.second.myPaths)
	{
	    UsdPrim prim = stage->GetPrimAtPath(primpath);
	    UT_ASSERT(prim);
	    if (!prim)
	    {
		continue;
	    }
	    UsdShadeMaterialBindingAPI matAPI(prim);
	    matAPI.Bind(material);
	    
	}
    }
    return true;
}
