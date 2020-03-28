/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	HUSD_LayerCheckpoint.h (HUSD Library, C++)
 *
 * COMMENTS:	Data structure for hold a copy of the active layer.
 */

#ifndef __HUSD_LayerCheckpoint_h__
#define __HUSD_LayerCheckpoint_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"

class HUSD_API HUSD_LayerCheckpoint
{
public:
			 HUSD_LayerCheckpoint();
                        ~HUSD_LayerCheckpoint();

    void                 create(const HUSD_AutoAnyLock &lock);
    bool                 restore(const HUSD_AutoLayerLock &layerlock);

private:
    PXR_NS::XUSD_LayerPtr myLayer;
};

#endif
