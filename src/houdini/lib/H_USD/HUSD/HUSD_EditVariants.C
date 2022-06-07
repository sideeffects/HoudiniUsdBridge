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

#include "HUSD_EditVariants.h"
#include "HUSD_Constants.h"
#include "HUSD_FindPrims.h"
#include "HUSD_PathSet.h"
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

	for (auto &&sdfpath : findprims.getExpandedPathSet().sdfPathSet())
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

		    if (HUSD_Constants::getBlockVariantValue() == vnamestr)
			vset.BlockVariantSelection();
                    else if (vnamestr.empty())
			vset.ClearVariantSelection();
		    else
			vset.SetVariantSelection(vnamestr);
		}
	    }
	}
	success = true;
    }

    return success;
}

