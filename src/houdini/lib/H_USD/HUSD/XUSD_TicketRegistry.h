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

