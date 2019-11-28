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

#ifndef __XUSD_TicketRegistry_h__
#define __XUSD_TicketRegistry_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "XUSD_Ticket.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_StringHolder.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class HUSD_API XUSD_TicketRegistry
{
public:
    static XUSD_TicketPtr	 createTicket(const UT_StringHolder &nodepath,
					const XUSD_TicketArgs &args,
					const GU_DetailHandle &gdh);
    static GU_DetailHandle	 getGeometry(const UT_StringRef &nodepath,
					const XUSD_TicketArgs &args);

private:
    static void			 returnTicket(const UT_StringHolder &nodepath,
					const XUSD_TicketArgs &args);

    friend class XUSD_Ticket;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

