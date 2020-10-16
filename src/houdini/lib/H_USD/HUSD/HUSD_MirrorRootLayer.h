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

#ifndef __HUSD_MirrorRootLayer_h__
#define __HUSD_MirrorRootLayer_h__

#include "HUSD_API.h"
#include <UT/UT_Matrix4.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_MirrorRootLayerData;

PXR_NAMESPACE_CLOSE_SCOPE

// This class contains content that should be copied to the root layer of a
// mirrored HUSD_Datahandle. This is separate from teh HUSD_Overrides session
// layers, because those can be enabled or disabled by user preference. The
// data in this layer must always exist, and is makes sense to allow it to be
// overridden by the data in the HUSD_Overrides, so we put it into the root
// layer of the mirrored stage.
//
// For now this root layer holds the USD camera primitive used when free
// tumbling in the viewport. This camera can either be a default camera or
// a reference to an existing camera, with modifications to the transforms.
class HUSD_API HUSD_MirrorRootLayer
{
public:
				 HUSD_MirrorRootLayer();
				~HUSD_MirrorRootLayer();

    void			 clear();
    PXR_NS::XUSD_MirrorRootLayerData &data() const;

    class CameraParms
    {
    public:
        UT_DMatrix4              myXform;
        fpreal                   myFocalLength = 50.0;
        fpreal                   myHAperture = 41.4214;
        fpreal                   myHApertureOffset = 0.0;
        fpreal                   myVAperture = 41.4214;
        fpreal                   myVApertureOffset = 0.0;
        fpreal                   myNearClip = 0.1;
        fpreal                   myFarClip = 10000.0;
        bool                     myIsOrtho = false;
        bool                     mySetCamParms = true;
        bool                     mySetCropParms = false;
    };

    // Configure a USD camera primitive for use in the viewport.
    void                         createViewportCamera(
                                        const UT_StringRef &refcamera,
                                        const CameraParms &camparms);

private:
    UT_UniquePtr<PXR_NS::XUSD_MirrorRootLayerData>	 myData;
};

#endif

