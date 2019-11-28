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

#ifndef __XUSD_Ticket_h__
#define __XUSD_Ticket_h__

#include "HUSD_API.h"
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_StringHolder.h>
#include <pxr/usd/sdf/fileFormat.h>

PXR_NAMESPACE_OPEN_SCOPE

typedef SdfFileFormat::FileFormatArguments XUSD_TicketArgs;

class XUSD_Ticket : public UT_IntrusiveRefCounter<XUSD_Ticket>
{
public:
			 XUSD_Ticket(const UT_StringHolder &nodepath,
				 const XUSD_TicketArgs &args);
    virtual		~XUSD_Ticket();

private:
    UT_StringHolder	 myNodePath;
    XUSD_TicketArgs	 myCookArgs;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

