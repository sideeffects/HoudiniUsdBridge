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

#ifndef __HUSD_ConfigureLayer_h__
#define __HUSD_ConfigureLayer_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>

class HUSD_API HUSD_ConfigureLayer
{
public:
			 HUSD_ConfigureLayer(HUSD_AutoWriteLock &lock);
			~HUSD_ConfigureLayer();

    // Sets Houdini-specific custom data to control the save location and
    // save behavior for this layer.
    bool		 setSavePath(const UT_StringRef &save_path) const;
    bool		 setSaveControl(const UT_StringRef &save_control) const;

    // Sets standard layer metadata items.
    bool		 setStartTime(fpreal64 start_time) const;
    bool		 setEndTime(fpreal64 end_time) const;
    bool		 setTimePerSecond(fpreal64 time_per_second) const;
    bool		 setFramesPerSecond(fpreal64 frames_per_second) const;
    bool		 setDefaultPrim(const UT_StringRef &primpath) const;

    // Stage level metrics.
    bool		 setUpAxis(const UT_StringRef &upaxis) const;
    bool		 setMetersPerUnit(fpreal metersperunit) const;

    // Render settings metadata
    bool                 setRenderSettings(const UT_StringRef &primpath) const;

    // Clears settings for all standard layer metadata items that can be
    // controlled by the above functions. The Houdini specific metadata is
    // unaffected.
    bool		 clearStandardMetadata() const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

