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

#include "HUSD_EditClips.h"
#include "HUSD_Constants.h"
#include "HUSD_EditReferences.h"
#include "HUSD_ErrorScope.h"
#include "XUSD_Data.h"
#include "XUSD_ExistenceTracker.h"
#include "XUSD_Utils.h"
#include <UT/UT_ErrorManager.h>
#include <UT/UT_Interval.h>
#include <UT/UT_StdUtil.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/clipsAPI.h>
#include <pxr/usd/usdUtils/stitchClips.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_EditClips::HUSD_EditClips(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_EditClips::~HUSD_EditClips()
{
}

static inline UsdClipsAPI
husdGetClipsAPI(HUSD_AutoWriteLock &lock, const UT_StringRef &primpath)
{
    auto data = lock.data();
    if (!data || !data->isStageValid())
	return UsdClipsAPI();

    SdfPath	sdfpath(HUSDgetSdfPath(primpath));
    auto	stage = data->stage();

    return UsdClipsAPI(stage->GetPrimAtPath(sdfpath));

}

bool
HUSD_EditClips::setClipPrimPath(const UT_StringRef &primpath,
        const UT_StringRef &clipsetname,
        const UT_StringRef &clipprimpath) const
{
    auto clipsapi = husdGetClipsAPI(myWriteLock, primpath);
    if( !clipsapi )
	return false;

    SdfPath sdfpath = HUSDgetSdfPath(clipprimpath);
    clipsapi.SetClipPrimPath(sdfpath.GetString(), clipsetname.toStdString());

    return true;
}

bool
HUSD_EditClips::setClipManifestFile(const UT_StringRef &primpath,
        const UT_StringRef &clipsetname,
        const UT_StringRef &manifestfile) const
{
    auto clipsapi = husdGetClipsAPI(myWriteLock, primpath);
    if( !clipsapi )
	return false;

    if (manifestfile.isstring())
    {
        SdfAssetPath assetpath(manifestfile.toStdString());
        clipsapi.SetClipManifestAssetPath(assetpath, clipsetname.toStdString());
    }

    return true;
}

bool
HUSD_EditClips::setClipFiles(const UT_StringRef &primpath,
	const UT_StringRef &clipsetname,
        const UT_StringArray &clipfiles) const
{
    auto clipsapi = husdGetClipsAPI(myWriteLock, primpath);
    if( !clipsapi )
	return false;

    VtArray<SdfAssetPath> paths;
    for (auto &&file : clipfiles)
        paths.push_back(SdfAssetPath(file.toStdString()));
    clipsapi.SetClipAssetPaths(paths, clipsetname.toStdString());

    return true;
}

bool
HUSD_EditClips::setClipSegments(const UT_StringRef &primpath,
        const UT_StringRef &clipsetname,
        fpreal starttime,
        fpreal clipstarttime,
        fpreal cliptimescale,
        const HUSD_ClipSegmentArray &segments) const
{
    auto clipsapi = husdGetClipsAPI(myWriteLock, primpath);
    if( !clipsapi )
	return false;

    VtVec2dArray cliptimes;
    VtVec2dArray clipactives;
    fpreal totalstagetime = starttime;
    fpreal totalcliptime = clipstarttime;
    for (auto &&segment : segments)
    {
        if (segment.useForcedClipStartTime())
            totalcliptime = segment.forcedClipStartTime();
        else if (segment.resetClipTime())
            totalcliptime = clipstarttime;
        cliptimes.push_back(GfVec2d(totalstagetime, totalcliptime));
        clipactives.push_back(GfVec2d(totalstagetime, segment.clipIndex()));
        if (SYSisGreater(segment.duration(), 1.0))
        {
            fpreal endstagetime = totalstagetime +
                (segment.duration() - 1.0);
            fpreal endcliptime = totalcliptime +
                (segment.duration() - 1.0) * cliptimescale;

            if (segment.firstAndLastFramesMatch())
                cliptimes.push_back(
                    GfVec2d(endstagetime + 1.0, endcliptime + cliptimescale));
            else
                cliptimes.push_back(GfVec2d(endstagetime, endcliptime));
        }
        totalstagetime += segment.duration();
        totalcliptime += segment.duration() * cliptimescale;
    }
    clipsapi.SetClipActive(clipactives, clipsetname.toStdString());
    clipsapi.SetClipTimes(cliptimes, clipsetname.toStdString());

    return true;
}

bool
HUSD_EditClips::flattenClipFiles(const UT_StringRef &primpath,
        const UT_StringRef &clipsetname,
        const UT_StringArray &clipfilesavepaths) const
{
    auto clipsapi = husdGetClipsAPI(myWriteLock, primpath);
    if (clipsapi)
    {
        auto topology = HUSDcreateAnonymousLayer(
            myWriteLock.constData()->stage()->GetRootLayer());
        std::string stdclipsetname = clipsetname.toStdString();
        VtArray<SdfAssetPath> clipfiles;
        std::string clipprimpath;

        if (!clipsapi.GetClipAssetPaths(&clipfiles, stdclipsetname) ||
            !clipsapi.GetClipPrimPath(&clipprimpath, stdclipsetname) ||
            clipfiles.size() != clipfilesavepaths.size())
            return false;

        for (int i = 0, n = clipfiles.size(); i < n; i++)
        {
            SdfAssetPath clipfile = clipfiles[i];
            UsdStageRefPtr clipstage = UsdStage::Open(clipfile.GetAssetPath());
            if (!clipstage)
            {
                HUSD_ErrorScope::addError(HUSD_ERR_CANT_FIND_LAYER,
                    clipfile.GetAssetPath().c_str());
                return false;
            }
            auto clipflat = clipstage->Flatten(false);
            HUSDsetSavePath(clipflat, clipfilesavepaths[i], false);
            HUSDsetCreatorNode(clipflat, myWriteLock.dataHandle().nodeId());
            myWriteLock.data()->addHeldLayer(clipflat);
            clipfiles[i] = SdfAssetPath(clipflat->GetIdentifier());
        }
        clipsapi.SetClipAssetPaths(clipfiles, stdclipsetname);
    }

    return true;
}

bool
HUSD_EditClips::createClipManifestFile(const UT_StringRef &primpath,
        const UT_StringRef &clipsetname,
        const UT_StringRef &manifestfile) const
{
    auto clipsapi = husdGetClipsAPI(myWriteLock, primpath);
    if (clipsapi)
    {
        auto manifest = clipsapi.GenerateClipManifest(
            clipsetname.toStdString(), false);

        if (manifest)
        {
            HUSDsetSavePath(manifest, manifestfile, false);
            HUSDsetCreatorNode(manifest, myWriteLock.dataHandle().nodeId());
            clipsapi.SetClipManifestAssetPath(
                SdfAssetPath(manifest->GetIdentifier()));
            myWriteLock.data()->addHeldLayer(manifest);
            return true;
        }
    }

    return false;
}

bool
HUSD_EditClips::createClipTopologyFile(const UT_StringRef &primpath,
        const UT_StringRef &clipsetname,
        const UT_StringRef &topologyfile) const
{
    auto clipsapi = husdGetClipsAPI(myWriteLock, primpath);
    if (clipsapi)
    {
        auto topology = HUSDcreateAnonymousLayer(
            myWriteLock.constData()->stage()->GetRootLayer());
        std::string stdclipsetname = clipsetname.toStdString();
        VtArray<SdfAssetPath> clipfiles;
        std::string clipprimpath;

        if (!clipsapi.GetClipAssetPaths(&clipfiles, stdclipsetname) ||
            !clipsapi.GetClipPrimPath(&clipprimpath, stdclipsetname))
            return false;

        std::vector<std::string> stdclipfiles;
        bool madetopology = false;

        for (auto &&clipfile : clipfiles)
            stdclipfiles.push_back(clipfile.GetAssetPath());

        // Create a block for an error scope. The function to create a
        // topology file calls "Save" on the resulting layer, which is
        // not allowed for anonymous layers, and so raises a USD error.
        UT_ErrorManager errman;
        {
            HUSD_ErrorScope errorscope(&errman);
            madetopology = UsdUtilsStitchClipsTopology(topology, stdclipfiles);
        }

        // If the topology creation fails, then and only then do we care
        // about any USD errors that may have been generated.
        if (madetopology)
        {
            HUSD_EditReferences editrefs(myWriteLock);

            HUSDsetSavePath(topology, topologyfile, false);
            HUSDsetCreatorNode(topology, myWriteLock.dataHandle().nodeId());
            editrefs.addReference(primpath,
                topology->GetIdentifier().c_str(), clipprimpath);
            myWriteLock.data()->addHeldLayer(topology);
            return true;
        }
        else
            UTgetErrorManager()->stealErrors(errman);
    }

    return false;
}

bool
HUSD_EditClips::compactFlattenedClipFiles(const UT_StringRef &primpath,
        const UT_StringRef &clipsetname) const
{
    auto clipsapi = husdGetClipsAPI(myWriteLock, primpath);
    if (clipsapi)
    {
        auto topology = HUSDcreateAnonymousLayer(
            myWriteLock.constData()->stage()->GetRootLayer());
        std::string stdclipsetname = clipsetname.toStdString();
        VtArray<SdfAssetPath> clipfiles;
        VtVec2dArray clipactive;
        VtVec2dArray cliptimes;
        std::string clipprimpath;

        if (!clipsapi.GetClipAssetPaths(&clipfiles, stdclipsetname) ||
            clipfiles.empty() ||
            !clipsapi.GetClipPrimPath(&clipprimpath, stdclipsetname) ||
            clipprimpath.empty() ||
            !clipsapi.GetClipActive(&clipactive, stdclipsetname) ||
            clipactive.empty() ||
            !clipsapi.GetClipTimes(&cliptimes, stdclipsetname) ||
            cliptimes.empty())
            return false;

        UT_Map<int, UT_Array<UT_Interval>> clipintervalsmap;
        auto sortpredicate =
            [](const GfVec2d &a, const GfVec2d &b) { return a[0] < b[0]; };
        std::sort(clipactive.begin(), clipactive.end(), sortpredicate);
        std::sort(cliptimes.begin(), cliptimes.end(), sortpredicate);
        int cliptimesidx = 0;
        int ncliptimes = cliptimes.size();
        // Advance to the first cliptimes entry that corresponds to the first
        // clipactive entry.
        while (cliptimesidx < ncliptimes &&
               SYSisLess(cliptimes[cliptimesidx][0], (*clipactive.begin())[0]))
            ++cliptimesidx;
        if (cliptimesidx > 0 &&
            SYSisGreater(cliptimes[cliptimesidx][0], (*clipactive.begin())[0]))
            --cliptimesidx;

        for (int i = 0, n = clipactive.size();
             i < n && cliptimesidx < ncliptimes; i++)
        {
            int clipidx = (int)clipactive[i][1];
            auto clipintervals = clipintervalsmap.find(clipidx);
            if (clipintervals == clipintervalsmap.end())
                clipintervals = clipintervalsmap.emplace(
                    clipidx, UT_Array<UT_Interval>()).first;

            fpreal stagetime = clipactive[i][0];
            fpreal mincliptime = cliptimes[cliptimesidx][1] - SYS_FTOLERANCE_D;
            fpreal maxcliptime = cliptimes[cliptimesidx][1] + SYS_FTOLERANCE_D;
            while (cliptimesidx < ncliptimes &&
                   SYSisLessOrEqual(cliptimes[cliptimesidx][0], stagetime))
            {
                mincliptime = SYSmin(mincliptime,
                    cliptimes[cliptimesidx][1] - SYS_FTOLERANCE_D);
                ++cliptimesidx;
                if (cliptimesidx < ncliptimes)
                    maxcliptime = SYSmax(maxcliptime,
                        cliptimes[cliptimesidx][1] - SYS_FTOLERANCE_D);
            }
            clipintervals->second.append(UT_Interval(mincliptime, maxcliptime));

            // Before the next iteration, skip forward to the last clipTimes
            // entry that has the same stage time as our current clipTimes
            // entry. Doubling up the stage time in subsequent clipTimes
            // entries is how clip discontinuities are expressed.
            while (cliptimesidx < ncliptimes - 1 &&
                   cliptimes[cliptimesidx][0] == cliptimes[cliptimesidx+1][0])
                ++cliptimesidx;

        }

        for (int i = 0, n = clipfiles.size(); i < n; i++)
        {
            SdfAssetPath clipfile = clipfiles[i];
            SdfLayerRefPtr cliplayer = SdfLayer::Find(clipfile.GetAssetPath());
            if (!cliplayer)
            {
                HUSD_ErrorScope::addError(HUSD_ERR_CANT_FIND_LAYER,
                    clipfile.GetAssetPath().c_str());
                continue;
            }
            if (!cliplayer->IsAnonymous())
            {
                HUSD_ErrorScope::addError(HUSD_ERR_COMPACTING_INVALID_LAYER,
                    clipfile.GetAssetPath().c_str());
                continue;
            }

            // This is a layer we can compact in-place.
            auto &clipintervals = clipintervalsmap[i];
            SdfChangeBlock changeblock;
            cliplayer->Traverse(SdfPath::AbsoluteRootPath(),
                [&cliplayer, &clipintervals] (const SdfPath &specpath) {
                    if (specpath.IsPropertyPath())
                    {
                        SdfAttributeSpecHandle attrspec =
                            cliplayer->GetAttributeAtPath(specpath);
                        if (!attrspec)
                            return;
                        attrspec->ClearDefaultValue();

                        auto timesamples = attrspec->GetTimeSampleMap();
                        bool changed = false;
                        for (auto it = timesamples.begin();
                                  it != timesamples.end(); )
                        {
                            if (std::find_if(clipintervals.begin(),
                                    clipintervals.end(),
                                    [&it](const UT_Interval &interval) {
                                        return interval.contains(it->first);
                                    }) == clipintervals.end())
                            {
                                it = timesamples.erase(it);
                                changed = true;
                            }
                            else
                                ++it;
                        }
                        if (changed)
                            attrspec->SetInfo(SdfFieldKeys->TimeSamples,
                                VtValue(timesamples));
                    }
                });
        }
    }

    return true;
}

bool
HUSD_EditClips::authorExistenceTrackingVisibility(
        const UT_StringRef &primpath,
        const UT_StringRef &clipsetname) const
{
    auto clipsapi = husdGetClipsAPI(myWriteLock, primpath);
    if (clipsapi)
    {
        std::string stdclipsetname = clipsetname.toStdString();
        VtArray<SdfAssetPath> clipfiles;
        VtVec2dArray clipactive;
        std::string clipprimpath;

        if (!clipsapi.GetClipAssetPaths(&clipfiles, stdclipsetname) ||
            clipfiles.empty() ||
            !clipsapi.GetClipPrimPath(&clipprimpath, stdclipsetname) ||
            clipprimpath.empty() ||
            !clipsapi.GetClipActive(&clipactive, stdclipsetname) ||
            clipactive.empty())
            return false;

        // We need at least two clip files for there to be any chance of
        // needing existence tracking.
        if (clipfiles.size() >= 2)
        {
            auto deststage = myWriteLock.data()->stage();
            SdfPath sdfprimpath(primpath.toStdString());
            SdfPath sdfclipprimpath(clipprimpath);
            XUSD_ExistenceTracker existence_tracker;

            for (int i = 0, n = clipactive.size(); i < n; i++)
            {
                auto clipfile = clipfiles[clipactive[i][1]];
                auto clipstage = UsdStage::CreateInMemory();
                auto prim = clipstage->DefinePrim(sdfprimpath,
                    TfToken(HUSD_Constants::getXformPrimType()));
                prim.GetReferences().AddReference(
                    SdfReference(clipfile.GetResolvedPath().empty()
                        ? clipfile.GetAssetPath()
                        : clipfile.GetResolvedPath(),
                    sdfclipprimpath));
                existence_tracker.collectNewStageData(clipstage);
                existence_tracker.authorVisibility(deststage, clipactive[i][0]);
            }
            if (existence_tracker.getVisibilityLayer())
                HUSDstitchLayers(myWriteLock.data()->activeLayer(),
                    existence_tracker.getVisibilityLayer());
        }

        return true;
    }

    return false;
}
