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

#include "HUSD_EditVariants.h"
#include "HUSD_FindPrims.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usd/variantSets.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_EditVariants::HUSD_EditVariants(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_EditVariants::~HUSD_EditVariants()
{
}

bool
HUSD_EditVariants::setVariant(const HUSD_FindPrims &findprims,
	const UT_StringRef &variantset,
	const UT_StringRef &variantname,
        int variantsetindex,
        int variantnameindex)
{
    auto		 outdata = myWriteLock.data();
    bool		 success = false;

    if (outdata && outdata->isStageValid())
    {
	std::string	 vsetstr = variantset.toStdString();
	std::string	 vnamestr = variantname.toStdString();
	auto		 stage = outdata->stage();

	for (auto &&sdfpath : findprims.getExpandedPathSet())
	{
	    auto		 prim = stage->GetPrimAtPath(sdfpath);

	    if (prim)
	    {
                if (variantsetindex >= 0)
                {
                    auto vsets = prim.GetVariantSets();
                    std::vector<std::string> names = vsets.GetNames();

                    if (names.size() > 0)
                    {
                        // Make sure the index is in the valid range.
                        variantsetindex = variantsetindex % names.size();
                        vsetstr = names[variantsetindex];
                    }
                    else
                        vsetstr.clear();
                }

		auto	 vset = prim.GetVariantSet(vsetstr);

		if (vset)
		{
                    if (variantnameindex >= 0)
                    {
                        std::vector<std::string> names = vset.GetVariantNames();

                        if (names.size() > 0)
                        {
                            // Make sure the index is in the valid range.
                            variantnameindex = variantnameindex % names.size();
                            vnamestr = names[variantnameindex];
                        }
                        else
                            vnamestr.clear();
                    }

		    if (!vnamestr.empty())
			vset.SetVariantSelection(vnamestr);
		    else
			vset.ClearVariantSelection();
		}
	    }
	}
	success = true;
    }

    return success;
}

