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
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <UT/UT_Debug.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/clipsAPI.h>

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
        if (segment.resetClipTime())
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

