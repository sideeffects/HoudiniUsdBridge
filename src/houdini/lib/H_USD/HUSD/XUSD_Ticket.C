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

#include "XUSD_Ticket.h"
#include "XUSD_TicketRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

XUSD_Ticket::XUSD_Ticket(const UT_StringHolder &nodepath,
	const XUSD_TicketArgs &args)
    : myNodePath(nodepath),
      myCookArgs(args)
{
}

XUSD_Ticket::~XUSD_Ticket()
{
    XUSD_TicketRegistry::returnTicket(myNodePath, myCookArgs);
}

PXR_NAMESPACE_CLOSE_SCOPE

