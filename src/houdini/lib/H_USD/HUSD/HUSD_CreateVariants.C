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

#include "HUSD_CreateVariants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/propertySpec.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    class CheckOpinions
    {
        public:
            CheckOpinions(const std::string &variantname,
                    const SdfPath &srcroot,
                    const SdfPath &destroot,
                    const UsdStageRefPtr &stage,
                    const UsdTimeCode &timecode,
                    UT_StringArray &weakeropinions)
                : myVariantName(variantname),
                  mySrcRoot(srcroot),
                  myDestRoot(destroot),
                  myStage(stage),
                  myTimeCode(timecode),
                  myWeakerOpinions(weakeropinions)
            { }

            void operator()(const SdfPath &srcpath)
            {
                if (srcpath.IsPropertyPath())
                {
                    // Replace the root prefix for the variant source with
                    // the destination prefix of the prim with the variants.
                    SdfPath destpath =
                        srcpath.ReplacePrefix(mySrcRoot, myDestRoot, false);
                    UsdObject obj =
                        myStage->GetObjectAtPath(destpath);
                    UsdProperty prop =
                        obj ? obj.As<UsdProperty>() : UsdProperty();

                    if (prop)
                    {
                        // Check for weaker time-specific opinions.
                        if (!findWeakVariantOpinions(prop, myTimeCode) &&
                            myTimeCode != UsdTimeCode::Default())
                        {
                            // If we didn't find any, check for weaker
                            // default opinions.
                            findWeakVariantOpinions(prop,
                                UsdTimeCode::Default());
                        }
                    }
                }
            }

        private:
            bool findWeakVariantOpinions(const UsdProperty &prop,
                    const UsdTimeCode &timecode)
            {
                SdfPropertySpecHandleVector stack =
                    prop.GetPropertyStack(timecode);

                // If there is only one opinion, it must be the variant.
                if (stack.size() > 1)
                {
                    for (auto it = stack.begin(); it != stack.end(); ++it)
                    {
                        if ((*it)->GetPath().ContainsPrimVariantSelection())
                        {
                            // Any variant opinion that isn't the first (i.e.
                            // strongest) opinion indicates a possible problem.
                            if (it != stack.begin())
                            {
                                SdfPath weakpath =
                                    prop.GetPath().MakeRelativePath(myDestRoot);
                                UT_WorkBuffer buf;

                                buf.sprintf("%s -- %s",
                                    myVariantName.c_str(),
                                    weakpath.GetString().c_str());
                                myWeakerOpinions.append(buf.buffer());

                                return true;
                            }
                        }
                    }
                }

                return false;
            }

            const std::string       &myVariantName;
            const SdfPath           &mySrcRoot;
            const SdfPath           &myDestRoot;
            const UsdStageRefPtr    &myStage;
            const UsdTimeCode       &myTimeCode;
            UT_StringArray          &myWeakerOpinions;
    };

    void
    checkForWeakVariantOpinions(
            const std::string &variantname,
            const SdfPrimSpecHandle &variantprim,
            const UsdPrim &usdprim,
            const UsdTimeCode &timecode,
            UT_StringArray &weakeropinions)
    {
        // We want to traverse all the attributes defined in the variant
        // and make sure those opinions are being realized in the composed
        // scene.
        CheckOpinions callback(variantname,
                variantprim->GetPath(),
                usdprim.GetPath(),
                usdprim.GetStage(),
                timecode,
                weakeropinions);

        variantprim->GetLayer()->Traverse(variantprim->GetPath(), callback);
    }
}

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
	const UT_StringRef &variantset,
        bool checkopinions,
        const HUSD_TimeCode &checkopinionstimecode,
        UT_StringArray &weakeropinions) const
{
    auto		 outdata = lock.data();
    bool		 success = false;

    if (outdata && outdata->isStageValid() &&
	primpath.isstring() && variantset.isstring())
    {
        UsdTimeCode      tc(HUSDgetUsdTimeCode(checkopinionstimecode));
	SdfPath	         sdfpath = HUSDgetSdfPath(primpath.toStdString());
	auto	         outstage = outdata->stage();
	auto	         prim = outstage->GetPrimAtPath(sdfpath);

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
                // Get the variant selections set on the active layer so we
                // can restore them once we're done authoring the variants.
                auto primspec = outdata->activeLayer()->GetPrimAtPath(sdfpath);
                auto oldvarselmap = primspec
                    ? primspec->GetVariantSelections()
                    : SdfVariantSelectionProxy();

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
                    auto         srcprim = srclayer->GetPrimAtPath(srcpath);

		    // If the source primitive doesn't exist, that's okay. It
		    // just means we are creating a variant that doesn't have
		    // any overrides. But we can't call HUSDcopySpec or we'll
		    // get a cryptic error message.
		    if (srcprim)
			success = HUSDcopySpec(srclayer, srcpath,
			    outdata->activeLayer(), dstpath);

		    if (!success)
			break;

                    // If we have been asked to check the application of the
                    // variant opinions, set the variant selection and do the
                    // check.
                    if (srcprim && checkopinions)
                    {
                        vset.SetVariantSelection(variantname);
                        checkForWeakVariantOpinions(variantname,
                            srcprim, prim, tc, weakeropinions);
                    }
		}

                if (checkopinions)
                {
                    primspec = outdata->activeLayer()->GetPrimAtPath(sdfpath);
                    if (primspec)
                    {
                        if (oldvarselmap)
                            primspec->GetVariantSelections() = oldvarselmap;
                        else
                            primspec->GetVariantSelections().clear();
                    }
                }
	    }
	}
    }

    return success;
}

