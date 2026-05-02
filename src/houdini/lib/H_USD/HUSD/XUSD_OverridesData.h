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

    // Return the layer for each a specific type of override.
    const SdfLayerRefPtr	&layer(HUSD_OverridesLayerId layer_id) const;
    // These methods should only be called by HUSD_Overrides.
    void			 lockToData(XUSD_Data *data);
    void			 unlockFromData(XUSD_Data *data);

    // Provide a way to explicitly clear our "held" sublayers for use
    // by the HUSD_Overrides::clear methods. Not strictly necessary, but
    // it seems good practice to release these layers right away.
    void                         clearSubLayers(HUSD_OverridesLayerId layer_id);

private:
    XUSD_Data			*myLockedToData;
    SdfLayerRefPtr		 myLayer[HUSD_OVERRIDES_NUM_LAYERS];
    SdfLayerRefPtrVector         mySubLayers[HUSD_OVERRIDES_NUM_LAYERS];
};

PXR_NAMESPACE_CLOSE_SCOPE

