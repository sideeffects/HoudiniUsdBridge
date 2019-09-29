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

#ifndef __HUSD_EditClips_h__
#define __HUSD_EditClips_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>

class HUSD_ClipSegment
{
public:
                         HUSD_ClipSegment(int clipindex,
                                fpreal duration)
                             : myClipIndex(clipindex),
                               myDuration(duration),
                               myResetClipTime(false)
                         { }

    int                  clipIndex() const
                         { return myClipIndex; }
    fpreal               duration() const
                         { return myDuration; }

    void                 setResetClipTime(bool reset)
                         { myResetClipTime = reset; }
    bool                 resetClipTime() const
                         { return myResetClipTime; }

private:
    int                  myClipIndex;
    fpreal               myDuration;
    bool                 myResetClipTime;
};

typedef UT_Array<HUSD_ClipSegment> HUSD_ClipSegmentArray;

class HUSD_API HUSD_EditClips
{
public:
			 HUSD_EditClips(HUSD_AutoWriteLock &lock);
			~HUSD_EditClips();

    bool                 setClipPrimPath(const UT_StringRef &primpath,
                                const UT_StringRef &clipsetname,
                                const UT_StringRef &clipprimpath) const;
    bool                 setClipManifestFile(const UT_StringRef &primpath,
                                const UT_StringRef &clipsetname,
                                const UT_StringRef &manifestfile) const;
    bool                 setClipFiles(const UT_StringRef &primpath,
                                const UT_StringRef &clipsetname,
                                const UT_StringArray &clipfiles) const;
    bool                 setClipSegments(const UT_StringRef &primpath,
                                const UT_StringRef &clipsetname,
                                fpreal starttime,
                                fpreal clipstarttime,
                                fpreal cliptimescale,
                                const HUSD_ClipSegmentArray &segments) const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

