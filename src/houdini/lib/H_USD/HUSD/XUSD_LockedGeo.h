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

#ifndef __XUSD_LockedGeo_h__
#define __XUSD_LockedGeo_h__

#include "HUSD_API.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_StringHolder.h>
#include <pxr/usd/sdf/fileFormat.h>

PXR_NAMESPACE_OPEN_SCOPE

typedef SdfFileFormat::FileFormatArguments XUSD_LockedGeoArgs;

// This object contains a GU_Detail created by a SOP node which is likely to
// be loaded into USD through our BGEO SdfFileFormat plugin (it's possibly it
// won't actually be loaded if the SOP geometry is referenced by an unloaded
// payload arc, but we have no way to know if or when that payload might be
// loaded).
//
// This object should only be create by the XUSD_LockedGeoRegistry, and
// any XUSD_Data that might load this SOP layer needs to keep a shared pointer
// to this object as sidecar data to the USD stage in XUSD_Data.

class HUSD_API XUSD_LockedGeo :
        public UT_IntrusiveRefCounter<XUSD_LockedGeo>,
        public UT_NonCopyable
{
public:
			~XUSD_LockedGeo();

private:
    friend class XUSD_LockedGeoRegistry;

                        XUSD_LockedGeo(const UT_StringHolder &nodepath,
				const XUSD_LockedGeoArgs &args,
				const GU_DetailHandle &gdh);

    bool		 setGdh(const GU_DetailHandle &gdh);
    GU_DetailHandle	 getGdh();

    bool		 matches(const UT_StringRef &nodepath,
				const XUSD_LockedGeoArgs &args);
    std::string          getLayerIdentifier() const;

    UT_StringHolder	 myNodePath;
    XUSD_LockedGeoArgs	 myCookArgs;
    GU_DetailHandle	 myGdh;
};

typedef UT_IntrusivePtr<XUSD_LockedGeo> XUSD_LockedGeoPtr;

PXR_NAMESPACE_CLOSE_SCOPE

#endif

