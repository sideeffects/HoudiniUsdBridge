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

#include "HUSD_CreateVariants.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

class HUSD_CreateVariants::husd_CreateVariantsPrivate {
public:
    XUSD_LayerArray		 myVariantLayers;
    XUSD_TicketArray		 myTicketArray;
    XUSD_LayerArray		 myReplacementLayerArray;
    HUSD_LockedStageArray	 myLockedStageArray;
};

HUSD_CreateVariants::HUSD_CreateVariants()
    : myPrivate(new HUSD_CreateVariants::husd_CreateVariantsPrivate())
{
}

HUSD_CreateVariants::~HUSD_CreateVariants()
{
}

bool
HUSD_CreateVariants::addHandle(const HUSD_DataHandle &src,
	const UT_StringHolder &srcpath,
	const UT_StringHolder &variantname)
{
    HUSD_AutoReadLock	 inlock(src);
    auto		 indata = inlock.data();
    bool		 success = false;

    if (indata && indata->isStageValid())
    {
	mySrcPaths.append(srcpath);
	myVariantNames.append(variantname);
	myPrivate->myVariantLayers.append(
	    indata->createFlattenedLayer(HUSD_IGNORE_STRIPPED_LAYERS));
	myPrivate->myTicketArray.concat(indata->tickets());
	myPrivate->myReplacementLayerArray.concat(indata->replacements());
	myPrivate->myLockedStageArray.concat(indata->lockedStages());
	success = true;
    }

    return success;
}

bool
HUSD_CreateVariants::execute(HUSD_AutoWriteLock &lock,
	const UT_StringRef &primpath,
	const UT_StringRef &variantset) const
{
    auto		 outdata = lock.data();
    bool		 success = false;

    if (outdata && outdata->isStageValid() &&
	primpath.isstring() && variantset.isstring())
    {
	SdfPath	 sdfpath = HUSDgetSdfPath(primpath.toStdString());
	auto	 outstage = outdata->stage();
	auto	 prim = outstage->GetPrimAtPath(sdfpath);

	// If the prim doesn't exist, this operation fails. The creation of
	// the prim, if necessary, should be handle by HUSD_CreatePrims.
	if (prim)
	{
	    auto vsets = prim.GetVariantSets();
	    auto vsetnames = vsets.GetNames();
	    if (std::find(vsetnames.begin(), vsetnames.end(),
		    variantset.toStdString()) == vsetnames.end())
		vsets.AddVariantSet(variantset.toStdString(),
		    UsdListPositionBackOfAppendList);
	    auto vset = vsets.GetVariantSet(variantset.toStdString());

	    if (vset)
	    {
		success = true;
		outdata->addTickets(myPrivate->myTicketArray);
		outdata->addReplacements(myPrivate->myReplacementLayerArray);
		outdata->addLockedStages(myPrivate->myLockedStageArray);
		auto vnames = vset.GetVariantNames();

		for (int i = 0, n = myVariantNames.entries(); i < n; i++)
		{
		    auto variantname = myVariantNames(i).toStdString();
		    auto dstpath = sdfpath.AppendVariantSelection(
			    variantset.toStdString(), variantname);

		    if (std::find(vnames.begin(), vnames.end(),
			    variantname) == vnames.end())
		    {
			// If the requested variant selection doesn't exist
			// yet, create a variant with the supplied name.
			vset.AddVariant(variantname,
			    UsdListPositionBackOfAppendList);
		    }
		    else if (!outdata->activeLayer()->GetPrimAtPath(dstpath))
		    {
			// If the variant already exists, we may be authoring
			// to a new layer in which there is no prim spec for
			// this variant. SdfCopySpec requires the destination
			// prim to exist, so create the prim spec here.
			SdfCreatePrimInLayer(outdata->activeLayer(), dstpath);
		    }

		    auto	 srclayer = myPrivate->myVariantLayers(i);
		    auto	 srcpath = HUSDgetSdfPath(mySrcPaths(i));

		    // If the source primitive doesn't exist, that's okay. It
		    // just means we are creating a variant that doesn't have
		    // any overrides. But we can't call HUSDcopySpec or we'll
		    // get a cryptic error message.
		    if (srclayer->GetPrimAtPath(srcpath))
			success = HUSDcopySpec(srclayer, srcpath,
			    outdata->activeLayer(), dstpath);

		    if (!success)
			break;
		}
	    }
	}
    }

    return success;
}

