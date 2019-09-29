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

