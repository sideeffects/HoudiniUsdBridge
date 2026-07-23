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
#include "HUSD_PathSet.h"

class HUSD_API HUSD_LayerCheckpoint
{
public:
			 HUSD_LayerCheckpoint();
                        ~HUSD_LayerCheckpoint();

    // If modified_prims is provided, a copy of it is stored alongside the
    // layer, and can be retrieved again later by passing a set to restore().
    void                 create(const HUSD_AutoAnyLock &lock,
                                const HUSD_PathSet *modified_prims = nullptr);
    // If modified_prims is provided, the set of prims stored at create()
    // time (if any) is inserted into it.
    bool                 restore(const HUSD_AutoLayerLock &layerlock,
                                HUSD_PathSet *modified_prims = nullptr);

private:
    PXR_NS::XUSD_LayerPtr myLayer;
    HUSD_PathSet          myModifiedPrims;
};

#endif
