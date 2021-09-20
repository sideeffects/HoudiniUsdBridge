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

#ifndef __XUSD_LockedGeoRegistry_h__
#define __XUSD_LockedGeoRegistry_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "XUSD_LockedGeo.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_StringHolder.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

// This class is used to keep alive SOP cook results that are referenced by
// USD. Adding an entry to the registry involves passing in the cooked SOP
// result, along with the SOP path and arguments to be used during the
// conversion from SOPs to USD. The returned shared pointer is kept as
// sidecar data along with any stage that might refer to this SOP layer in
// any way. This ensures the SOP won't modify the GU_Detail with a recook
// as long as a USD stage needs the old GU_Detail (the USD VtArrays use the
// same pointers to memory as the GU_Detail attributes in some cases).
//
// The getGeometry method is used by the SdfFileFormat plugin for loading
// USD from a GU_Detail using a SOP path. Because of the sidecar shared
// pointers, it should always be true that if SOP geometry is being loaded
// through this plugin, at least some XUSD_Data will be holding a shared
// pointer that points to the GU_Detail.

class HUSD_API XUSD_LockedGeoRegistry
{
public:
    static XUSD_LockedGeoPtr     createLockedGeo(
                                        const UT_StringHolder &nodepath,
					const XUSD_LockedGeoArgs &args,
					const GU_DetailHandle &gdh);
    static GU_DetailHandle	 getGeometry(const UT_StringHolder &nodepath,
					const XUSD_LockedGeoArgs &args);

private:
    friend class XUSD_LockedGeo;

    static void			 returnLockedGeo(XUSD_LockedGeo *lockedgeo);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

