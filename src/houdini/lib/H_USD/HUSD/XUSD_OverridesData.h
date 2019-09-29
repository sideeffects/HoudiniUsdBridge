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

#include "HUSD_Utils.h"
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_Data;

class XUSD_OverridesData
{
public:
				 XUSD_OverridesData();
				~XUSD_OverridesData();

    const SdfLayerRefPtr	&layer(HUSD_OverridesLayerId layer_id) const;

    // These methods should only be called by HUSD_Overrides.
    void			 lockToData(XUSD_Data *data);
    void			 unlockFromData(XUSD_Data *data);

private:
    XUSD_Data			*myLockedToData;
    SdfLayerRefPtr		 myLayer[HUSD_OVERRIDES_NUM_LAYERS];
};

PXR_NAMESPACE_CLOSE_SCOPE

