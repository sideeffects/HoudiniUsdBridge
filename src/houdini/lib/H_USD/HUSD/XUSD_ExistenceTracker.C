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

#include "XUSD_ExistenceTracker.h"
#include "XUSD_AutoCollection.h"
#include "XUSD_FindPrimsTask.h"
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/base/gf/interval.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    class xusd_FindImageablePrimPaths final : public XUSD_FindPrimPathsTaskData
    {
    public:
        void addToThreadData(const UsdPrim &prim, bool *prune) override
        {
            // We are only interested in imageable primitives (since only
            // these primitives respect visibility).
            UsdGeomImageable      imageable(prim);
            if (imageable)
                XUSD_FindPrimPathsTaskData::addToThreadData(prim, prune);
        }
    };
}

XUSD_ExistenceTracker::XUSD_ExistenceTracker()
    : myOldTimeCode(UsdTimeCode::EarliestTime()),
      myNewTimeCode(UsdTimeCode::EarliestTime()),
      myFirstUse(true)
{
}

XUSD_ExistenceTracker::~XUSD_ExistenceTracker()
{
}

void
XUSD_ExistenceTracker::collectNewStageData(const UsdStageRefPtr &newstage)
{
    UsdPrim root = newstage->GetPseudoRoot();
    auto predicate = HUSDgetUsdPrimPredicate(HUSD_TRAVERSAL_DEFAULT_DEMANDS);
    xusd_FindImageablePrimPaths data;

    myNewPaths.clear();
    XUSDfindPrims(root, data, predicate, nullptr, nullptr);
    data.gatherPathsFromThreads(myNewPaths);
}

namespace
{
    void
    copySamples(const GfInterval &interval,
            const UsdAttribute &visattr,
            const SdfAttributeSpecHandle &visspec)
    {
        VtValue sample;
        std::vector<double> sampletimes;
        visattr.GetTimeSamplesInInterval(interval, &sampletimes);
        if (sampletimes.size() > 0)
        {
            SdfTimeSampleMap samples = visspec->GetTimeSampleMap();

            for (auto &&sampletime : sampletimes)
            {
                if (visattr.Get(&sample, UsdTimeCode(sampletime)))
                    samples.emplace(sampletime, sample);
            }
            visspec->SetField(SdfFieldKeys->TimeSamples, samples);
        }
    }

    void
    setVisibility(const UsdStageRefPtr &combinedstage,
        const UsdTimeCode &oldtimecode,
        const UsdTimeCode &timecode,
        const SdfPath &path,
        const SdfLayerRefPtr &vislayer,
        bool visible)
    {
        SdfPrimSpecHandle            primspec;

        primspec = SdfCreatePrimInLayer(vislayer, path);
        if (primspec)
        {
            SdfAttributeSpecHandle   visspec;
            SdfPath                  visattrpath;
            bool                     visspecisnew = false;

            visattrpath = SdfPath::ReflexiveRelativePath().
                AppendProperty(UsdGeomTokens->visibility);
            visspec = primspec->GetAttributeAtPath(visattrpath);
            if (!visspec)
            {
                visspec = SdfAttributeSpec::New(primspec,
                    UsdGeomTokens->visibility, SdfValueTypeNames->Token);
                visspecisnew = true;
            }

            if (visspec)
            {
                SdfTimeSampleMap samples = visspec->GetTimeSampleMap();
                VtValue currentvalue = visible
                    ? VtValue(UsdGeomTokens->inherited)
                    : VtValue(UsdGeomTokens->invisible);
                VtValue oldvalue = visible
                    ? VtValue(UsdGeomTokens->invisible)
                    : VtValue(UsdGeomTokens->inherited);

                // Note that up to this point, we don't care if the primitive
                // actually exists on the stage. We are only looking at the
                // visibility layer. Now we look at the stage to possibly
                // copy over authored visibility from the stage.
                UsdGeomImageable imageable = UsdGeomImageable::Get(
                    combinedstage, path);
                if (imageable)
                {
                    auto visattr = imageable.GetVisibilityAttr();

                    if (visattr)
                    {
                        if (visspecisnew)
                        {
                            // Copy over any time samples from the stage for
                            // the interval up to the current time. Only do
                            // this the first time we create the visiblity
                            // attribute on our visibility layer.
                            GfInterval interval(
                                -std::numeric_limits<double>::infinity(),
                                timecode.GetValue());
                            copySamples(interval, visattr, visspec);
                            // Re-fetch the samples from the visspec, as we
                            // may have added new samples in copySamples.
                            samples = visspec->GetTimeSampleMap();
                        }

                        if (visible)
                        {
                            // Get the current visibility for the newly added
                            // prim from the composed stage.
                            visattr.Get(&currentvalue, timecode);
                        }
                        else
                        {
                            // A prim is removed, but it still exists on the
                            // composed stage. We grab the visibility of that
                            // prim from the old time code, and explicitly set
                            // that value as a time sample at the old time code.
                            visattr.Get(&oldvalue, oldtimecode);
                        }
                    }
                }

                samples[oldtimecode.GetValue()] = oldvalue;
                samples[timecode.GetValue()] = currentvalue;
                visspec->SetField(SdfFieldKeys->TimeSamples, samples);
            }
        }
    }
}

void
XUSD_ExistenceTracker::authorVisibility(const UsdStageRefPtr &combinedstage,
        const UsdTimeCode &timecode)
{
    myOldTimeCode = myNewTimeCode;
    myNewTimeCode = timecode;
    if (!myFirstUse)
    {
        XUSD_PathSet addedprims;
        XUSD_PathSet removedprims;

        std::set_difference(myNewPaths.begin(), myNewPaths.end(),
            myOldPaths.begin(), myOldPaths.end(),
            std::inserter(addedprims, addedprims.begin()));
        std::set_difference(myOldPaths.begin(), myOldPaths.end(),
            myNewPaths.begin(), myNewPaths.end(),
            std::inserter(removedprims, removedprims.begin()));

        myOldPaths.swap(myNewPaths);
        if (!addedprims.empty() || !removedprims.empty())
        {
            if (!myVisibilityLayer)
                myVisibilityLayer = HUSDcreateAnonymousLayer();

            // We only need to set visibility on the topmost prim that has
            // been added or removed.
            addedprims.removeDescendants();
            removedprims.removeDescendants();

            for (auto &&path : addedprims)
            {
                // Author "invisible" opinion at myOldTimeCode, and "visible"
                // opinion at myNewTimeCode.
                setVisibility(combinedstage, myOldTimeCode, myNewTimeCode, path,
                    myVisibilityLayer, true);
                myModifiedPaths[path] = true;
            }
            for (auto &&path : removedprims)
            {
                // Author "visible" opinion at myOldTimeCode, and "invisible"
                // opinion at myNewTimeCode.
                setVisibility(combinedstage, myOldTimeCode, myNewTimeCode, path,
                    myVisibilityLayer, false);
                myModifiedPaths[path] = false;
            }
        }

        // For paths that we previously modified to mark them visible,
        // we need to keep reading the current visibilty attribute
        // value from the stage, and copying over any time sampled
        // values between the last time and the new time.
        for (auto &&it : myModifiedPaths)
        {
            if (!it.second || addedprims.contains(it.first))
                continue;

            SdfAttributeSpecHandle visspec =
                myVisibilityLayer->GetAttributeAtPath(
                    it.first.AppendProperty(UsdGeomTokens->visibility));
            if (!visspec)
                continue;

            UsdGeomImageable imageable =
                UsdGeomImageable::Get(combinedstage, it.first);
            if (!imageable)
                continue;

            auto visattr = imageable.GetVisibilityAttr();
            if (!visattr)
                continue;

            GfInterval interval(myOldTimeCode.GetValue(),
                myNewTimeCode.GetValue(), false, true);
            copySamples(interval, visattr, visspec);
        }
    }
    else
    {
        // Initialize the set of combined paths with all the paths from the
        // first "new" stage added to the combined stage.
        myOldPaths.swap(myNewPaths);
        myFirstUse = false;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
