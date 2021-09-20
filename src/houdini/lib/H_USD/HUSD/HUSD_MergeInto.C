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
#include <OP/OP_Director.h>
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
	UsdTimeCode		 myTC;
	UT_Array<SdfPath>	 myPaths;
    };
    XUSD_LayerArray		 mySubLayers;
    XUSD_LockedGeoArray		 myLockedGeoArray;
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
      myMakeUniqueDestPaths(false),
      myDestPathMode(PATH_IS_PARENT)
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
	bool			 keep_xform /*=false*/,
	bool			 keep_material /*=false*/,
	const HUSD_TimeCode	&time_code /*= HUSD_TimeCode()*/)
{
    HUSD_AutoReadLock	 inlock(src);
    const auto		&indata = inlock.data();
    bool		 success = false;

    if (indata && indata->isStageValid())
    {
	// We must flatten the layers of the stage so that we can use
	// SdfCopySpec safely.  Flattening the layers (even if it's just one
	// layer) smoothes out a lot of problems with time scaling, reference
	// file paths, and other issues that can cause problems if we use
	// copyspec directly from the source layer.
	SdfLayerRefPtr flattenedlayer =
	        indata->createFlattenedLayer(HUSD_WARN_STRIPPED_LAYERS);

	if (source_path && source_path != HUSD_Constants::getRootPrimPath())
	{
	    SdfPath sourcepath(source_path.toStdString());
	    if (!flattenedlayer->GetPrimAtPath(sourcepath))
	    {
		HUSD_ErrorScope::addWarning(
		        indata->stage()->GetPrimAtPath(sourcepath)
		        ? HUSD_ERR_PRIM_IN_REFERENCE
		        : HUSD_ERR_CANT_FIND_PRIM
		        , sourcepath.GetText());
		return false;
	    }
	}

	// Record the path to that destination prim (if one is supplied).
	myPrivate->myDestPaths.append(dest_path);
	myPrivate->mySourceNodePaths.append(source_node_path);
	myPrivate->mySourcePaths.append(source_path);
	myPrivate->myFrameOffsets.append(frame_offset);
	myPrivate->myFramerateScales.append(framerate_scale);
	myPrivate->mySubLayers.append(flattenedlayer);

	// Hold onto lockedgeos to keep in memory any cooked OP data referenced
	// by the layers being merged.
	myPrivate->myLockedGeoArray.concat(indata->lockedGeos());
	myPrivate->myReplacementLayerArray.concat(indata->replacements());
	myPrivate->myLockedStageArray.concat(indata->lockedStages());

	if (keep_xform && source_path != HUSD_Constants::getRootPrimPath())
	{
	    UsdPrim srcprim = indata->stage()->GetPrimAtPath(
	            HUSDgetSdfPath(source_path));
	    // We want to capture the parent's local-to-world transformation
	    // (so we can keep the locally-authored transform stack)
	    if (srcprim && !srcprim.IsPseudoRoot())
	    {
		UsdTimeCode xform_tc = UsdTimeCode::Default();
		HUSD_TimeSampling ts =
		        HUSDgetWorldTransformTimeSampling(srcprim.GetParent());
		if (ts != HUSD_TimeSampling::NONE)
		    xform_tc = HUSDgetUsdTimeCode(time_code);
		UsdGeomXformable xformable(srcprim);
		if (xformable)
		{
		    myPrivate->myInheritedXforms[flattenedlayer].myTC = xform_tc;
		    GfMatrix4d xform =
		            xformable.ComputeParentToWorldTransform(xform_tc);
		    myPrivate->myInheritedXforms[flattenedlayer].myXform = xform;
		}
	    }
	}

	if (keep_material && source_path != HUSD_Constants::getRootPrimPath())
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

// TODO - extend this "batch" version to support material/xform keeping
bool
HUSD_MergeInto::addHandle(
        const HUSD_DataHandle &src,
        const UT_StringArray  &dest_paths,
        const UT_StringHolder &source_node_path,
        const UT_StringArray  &source_paths,
        fpreal                 frame_offset    /*=0*/,
        fpreal                 framerate_scale /*=1*/)
{
    UT_ASSERT(source_paths.size() == dest_paths.size());
    if (source_paths.size() != dest_paths.size())
        return false;
    
    HUSD_AutoReadLock inlock(src);
    const auto &indata = inlock.data();
    bool success = false;

    if (indata && indata->isStageValid())
    {
        // We must flatten the layers of the stage so that we can use
        // SdfCopySpec safely.  Flattening the layers (even if it's just one
        // layer) smoothes out a lot of problems with time scaling, reference
        // file paths, and other issues that can cause problems if we use
        // copyspec directly from the source layer.
        SdfLayerRefPtr flattenedlayer
                = indata->createFlattenedLayer(HUSD_WARN_STRIPPED_LAYERS);

        for (const auto &source_path : source_paths)
        {
            if (source_path && source_path != HUSD_Constants::getRootPrimPath())
            {
                SdfPath sourcepath(source_path.toStdString());
                if (!flattenedlayer->GetPrimAtPath(sourcepath))
                {
                    HUSD_ErrorScope::addWarning(
                            indata->stage()->GetPrimAtPath(sourcepath) ?
                            HUSD_ERR_PRIM_IN_REFERENCE :
                            HUSD_ERR_CANT_FIND_PRIM,
                            sourcepath.GetText());
                    return false;
                }
            }
        }

        // Record the path to that destination prim (if one is supplied).
        for (size_t i = source_paths.size(); i-->0;)
        {
            myPrivate->myDestPaths.append(dest_paths[i]);
            myPrivate->mySourceNodePaths.append(source_node_path);
            myPrivate->mySourcePaths.append(source_paths[i]);
            myPrivate->myFrameOffsets.append(frame_offset);
            myPrivate->myFramerateScales.append(framerate_scale);
            myPrivate->mySubLayers.append(flattenedlayer);
        }

        // Hold onto lockedgeos to keep in memory any cooked OP data referenced
        // by the layers being merged.
        myPrivate->myLockedGeoArray.concat(indata->lockedGeos());
        myPrivate->myReplacementLayerArray.concat(indata->replacements());
        myPrivate->myLockedStageArray.concat(indata->lockedStages());

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

	    const UT_StringHolder &sourcepath = myPrivate->mySourcePaths(idx);
	    if (sourcepath && sourcepath != HUSD_Constants::getRootPrimPath())
	    {
		sourceroot = inlayer->GetPrimAtPath(SdfPath(sourcepath.toStdString()));
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
            
            if (myDestPathMode == PATH_IS_TARGET && outroot.IsAbsoluteRootPath())
            {
                HUSD_ErrorScope::addError(HUSD_ERR_CANT_COPY_DIRECTLY_INTO_ROOT);
                return false;
            }
            
	    auto parentspec = HUSDcreatePrimInLayer(
                stage, outlayer,
                (myDestPathMode == PATH_IS_TARGET) ? outroot.GetParentPath() : outroot,
		TfToken(primkind), SdfSpecifierDef, SdfSpecifierDef,
                parent_prim_type);
	    if (parentspec)
	    {
                if (!parentspec->GetPath().IsAbsoluteRootPath())
                {
                    OP_Node *source_node = OPgetDirector()->findNode(
                        myPrivate->mySourceNodePaths(idx));

                    if (!parent_prim_type.empty())
                        parentspec->SetTypeName(parent_prim_type);

                    if (source_node)
                        parentspec->SetCustomData(HUSDgetSourceNodeToken(),
                            VtValue(source_node->getUniqueId()));
                }

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

		    auto outpath = (myDestPathMode == PATH_IS_PARENT)
                            ? outroot.AppendChild(inpath.GetNameToken())
                            : outroot;

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

		    auto primspec = (myDestPathMode == PATH_IS_PARENT)
                            ? HUSDcreatePrimInLayer(
                                    stage, outlayer, outpath, TfToken(),
                                    SdfSpecifierDef, SdfSpecifierDef,
                                    parent_prim_type)
                            : parentspec;

		    if (!primspec)
		    {
			success = false;
			break;
		    }

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
		    
		    if (!mergingrootprim &&
                        myPrivate->myInheritedXforms.contains(inlayer))
			myPrivate->myInheritedXforms[inlayer].
                            myPaths.append(outpath);
		    if (!mergingrootprim &&
                        myPrivate->myInheritedMaterials.contains(inlayer))
			myPrivate->myInheritedMaterials[inlayer].
                            myPaths.append(outpath);
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

	// Transfer lockedgeo ownership from ourselves to the output data.
	lock.addLockedGeos(myPrivate->myLockedGeoArray);
	lock.addReplacements(myPrivate->myReplacementLayerArray);
	lock.addLockedStages(myPrivate->myLockedStageArray);
    }

    return success;
}

bool
HUSD_MergeInto::postExecuteAssignXform(
	HUSD_AutoWriteLock &lock,
	const UT_StringRef &xform_suffix) const
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
	const auto &tc = entry.second.myTC;
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

	    // Make sure we have a unique name for our TransformOp
	    UT_StringHolder tmp_suffix = xform_suffix;
	    HUSDgenerateUniqueTransformOpSuffix(tmp_suffix, xformable);

	    // Build a new transform stack starting with our inherited parent
	    // xform brought into the space of the current parent xform,
	    // and then the old local stack.
	    UsdGeomXformOp xformop = xformable.AddTransformOp(
	            UsdGeomXformOp::PrecisionDouble,
	            TfToken(tmp_suffix.c_str()));
	    xformop.Set(xform * parentxform.GetInverse(), tc);
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
HUSD_MergeInto::areInheritedXformsAnimated() const
{
    for (const auto &entry : myPrivate->myInheritedXforms)
    {
        if (!entry.second.myTC.IsDefault())
        {
            return true;
        }
    }
    return false;
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
	    UsdShadeMaterialBindingAPI::Apply(prim).Bind(material);
	}
    }
    return true;
}
