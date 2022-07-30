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

#include "XUSD_LockedGeo.h"
#include "XUSD_LockedGeoRegistry.h"
#include "XUSD_Utils.h"
#include "HUSD_Utils.h"
#include <UT/UT_Lock.h>
#include <pxr/usd/sdf/layer.h>

PXR_NAMESPACE_OPEN_SCOPE

XUSD_LockedGeo::XUSD_LockedGeo(const UT_StringHolder &nodepath,
        const XUSD_LockedGeoArgs &args,
        const GU_ConstDetailHandle &gdh)
    : myNodePath(nodepath),
      myCookArgs(args),
      myGdh(gdh)
{
    if (myGdh.isValid())
        myGdh.addPreserveRequest();
}

XUSD_LockedGeo::~XUSD_LockedGeo()
{
    XUSD_LockedGeoRegistry::returnLockedGeo(this);
    if (myGdh.isValid())
        myGdh.removePreserveRequest();
}

bool
XUSD_LockedGeo::setGdh(const GU_ConstDetailHandle &gdh)
{
    if (myGdh != gdh)
    {
        // The gdh has changed. Update our gdh to the new value,
        // and reload the associated layer. But acquire the "reload"
        // lock first so we can be sure there isn't a background thread
        // syncing a stage on a background thread using this layer.
        UT_AutoLock lockscope(HUSDgetLayerReloadLock());

        if (myGdh.isValid())
            myGdh.removePreserveRequest();
        myGdh = gdh;
        if (myGdh.isValid())
            myGdh.addPreserveRequest();

        SdfLayerHandle layer;

        layer = SdfLayer::Find(myNodePath.toStdString(), myCookArgs);
        if (layer)
        {
            // Clear the whole cache of automatic ref prim paths,
            // because the layer we are reloading may be used by any
            // stage, and so may affect the default/automatic default
            // prim of any stage.
            HUSDclearBestRefPathCache();
            layer->Reload(true);
        }

        return true;
    }

    return false;
}
GU_ConstDetailHandle
XUSD_LockedGeo::getGdh()
{
    return myGdh;
}

bool
XUSD_LockedGeo::matches(const UT_StringRef &nodepath,
        const XUSD_LockedGeoArgs &args)
{
    return nodepath == myNodePath && args == myCookArgs;
}
std::string
XUSD_LockedGeo::getLayerIdentifier() const
{
    return SdfLayer::CreateIdentifier(myNodePath.toStdString(), myCookArgs);
}

PXR_NAMESPACE_CLOSE_SCOPE

