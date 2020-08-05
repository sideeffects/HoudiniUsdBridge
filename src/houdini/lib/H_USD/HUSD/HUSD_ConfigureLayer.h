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

    // This flag controls whether all future calls to this object should
    // just affect the active layer, or should also be applied to the stage
    // root layer.
    void                 setModifyRootLayer(bool modifyrootlayer);

    // Sets Houdini-specific custom data to control the save location and
    // save behavior for this layer.
    bool		 setSavePath(const UT_StringRef &save_path,
                                bool save_path_is_time_dependent) const;
    bool		 setSaveControl(const UT_StringRef &save_control) const;

    // Sets standard layer metadata items.
    bool		 setStartTime(fpreal64 start_time) const;
    bool		 setEndTime(fpreal64 end_time) const;
    bool		 setTimeCodesPerSecond(fpreal64 time_per_second) const;
    bool		 setFramesPerSecond(fpreal64 frames_per_second) const;
    bool		 setDefaultPrim(const UT_StringRef &primpath) const;
    bool		 setComment(const UT_StringRef &comment) const;

    // Stage level metrics.
    bool		 setUpAxis(const UT_StringRef &upaxis) const;
    bool		 setMetersPerUnit(fpreal metersperunit) const;

    // Render settings metadata
    bool                 setRenderSettings(const UT_StringRef &primpath) const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
    bool                 myModifyRootLayer;
};

#endif

