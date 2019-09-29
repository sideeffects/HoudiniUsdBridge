/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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
	const UT_StringHolder &dest_path,
	const UT_StringHolder &source_node_path)
{
    HUSD_AutoReadLock	 inlock(src);
    auto		 indata = inlock.data();
    bool		 success = false;

    if (indata && indata->isStageValid())
    {
	// Record the path to that destination prim (if one is supplied).
	myDestPaths.append(dest_path);
	mySourceNodePaths.append(source_node_path);

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
	    UT_String	 outpath;
	    SdfPath	 outroot;
	    std::string	 parent_prim_type;
	    std::string	 primkind = myPrimKind.toStdString();

	    // If the "kind" is set to "automatic", look for the first child
	    // with a non-empty kind, and use the parent kind for that child
	    // kind.
	    if (myPrimKind == HUSD_Constants::getKindAutomatic())
	    {
		primkind = std::string();
		for (auto &&child : inlayer->GetPseudoRoot()->GetNameChildren())
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
		outpath = myDestPaths(idx);
	    else
		outpath.sprintf("/input%d", idx);

	    // If requested, make sure we don't conflict with any
	    // existing primitive on the stage or our layer.
	    if (myMakeUniqueDestPaths)
	    {
		auto testpath = HUSDgetSdfPath(outpath);

		while (stage->GetPrimAtPath(testpath) ||
		    outlayer->GetPrimAtPath(testpath))
		{
		    outpath.incrementNumberedName(true);
		    testpath = HUSDgetSdfPath(outpath);
		}
	    }

	    outroot = HUSDgetSdfPath(outpath);
	    auto parentspec = HUSDcreatePrimInLayer(stage, outlayer, outroot,
		TfToken(primkind), true, parent_prim_type);
	    if (parentspec)
	    {
		if (!parent_prim_type.empty())
		    parentspec->SetTypeName(parent_prim_type);
		parentspec->SetSpecifier(SdfSpecifierDef);
		parentspec->SetCustomData(HUSDgetSourceNodeToken(),
		    VtValue(TfToken(mySourceNodePaths(idx).toStdString())));

		for (auto &&child : inlayer->GetPseudoRoot()->GetNameChildren())
		{
		    // Create a dummy prim to which we copy the source prim.
		    auto inpath = child->GetPath();

		    // Don't merge in the HoudiniLayerInfo prim.
		    if (inpath.GetString() ==
			HUSD_Constants::getHoudiniLayerInfoPrimPath().c_str())
			continue;

		    auto outpath = outroot.AppendChild(inpath.GetNameToken());
		    auto primspec = HUSDcreatePrimInLayer(
			stage, outlayer, outpath, TfToken(),
			true, parent_prim_type);

		    if (!primspec)
		    {
			success = false;
			break;
		    }
		    primspec->SetSpecifier(SdfSpecifierDef);

		    // Even though we are copying the primspec inpath to
		    // outpath, when it comes to remapping references, we want
		    // references between these separate children to be
		    // updated to point to their new destination locations.
		    // So we pass in the parents of these children as the
		    // root prims for remapping purposes.
		    if (!HUSDcopySpec(inlayer, inpath, outlayer, outpath,
			    inpath.GetParentPath(), outpath.GetParentPath()))
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

