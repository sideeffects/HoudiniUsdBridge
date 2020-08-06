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
#include <UT/UT_Set.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <vector>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

class HUSD_MergeInto::husd_MergeIntoPrivate {
public:
    XUSD_LayerArray		 mySubLayers;
    XUSD_TicketArray		 myTicketArray;
    XUSD_LayerArray		 myReplacementLayerArray;
    HUSD_LockedStageArray	 myLockedStageArray;
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
	const fpreal		 frame_offset /*=0*/,
	const fpreal		 framerate_scale /*=1*/)
{
    HUSD_AutoReadLock	 inlock(src);
    auto		 indata = inlock.data();
    bool		 success = false;

    if (indata && indata->isStageValid())
    {
	// Record the path to that destination prim (if one is supplied).
	myDestPaths.append(dest_path);
	mySourceNodePaths.append(source_node_path);
	mySourcePaths.append(source_path);
	myFrameOffsets.append(frame_offset);
	myFramerateScales.append(framerate_scale);

	// We must flatten the layers of the stage so that we can use
	// SdfCopySpec safely.  Flattening the layers (even if it's just one
	// layer) smoothes out a lot of problems with time scaling, reference
	// file paths, and other issues that can cause problems if we use
	// copyspec directly from the source layer.
	myPrivate->mySubLayers.append(
	    indata->createFlattenedLayer(HUSD_WARN_STRIPPED_LAYERS));

	// Hold onto tickets to keep in memory any cooked OP data referenced
	// by the layers being merged.
	myPrivate->myTicketArray.concat(indata->tickets());
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
	    fpreal	 frameoffset = myFrameOffsets(idx);
	    fpreal	 frameratescale = myFramerateScales(idx);

	    if (mySourcePaths(idx) &&
		mySourcePaths(idx) != HUSD_Constants::getRootPrimPath())
	    {
		SdfPath sourcepath(mySourcePaths(idx).toStdString());
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
	    if (myDestPaths(idx).isstring())
                outpathstr = myDestPaths(idx);
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
		    VtValue(TfToken(mySourceNodePaths(idx).toStdString())));

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

