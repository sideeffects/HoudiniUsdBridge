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

#include "XUSD_TicketRegistry.h"
#include "XUSD_Ticket.h"
#include "XUSD_Utils.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_Array.h>
#include <UT/UT_NonCopyable.h>
#include <SYS/SYS_Math.h>
#include <pxr/usd/sdf/layer.h>

PXR_NAMESPACE_OPEN_SCOPE

class RegistryEntry : public UT_IntrusiveRefCounter<RegistryEntry>,
		      public UT_NonCopyable
{
public:
			 RegistryEntry(const UT_StringHolder &nodepath,
				const XUSD_TicketArgs &args,
				const GU_DetailHandle &gdh)
			     : myNodePath(nodepath),
			       myCookArgs(args),
			       myGdh(gdh),
			       myTicketCount(0)
			 {
			     if (myGdh.isValid())
				 myGdh.addPreserveRequest();
			 }
			~RegistryEntry()
			 {
			     if (myGdh.isValid())
				 myGdh.removePreserveRequest();
			 }

    bool		 setGdh(const GU_DetailHandle &gdh)
			 {
			     if (myGdh != gdh)
			     {
				 if (myGdh.isValid())
				     myGdh.removePreserveRequest();
				 myGdh = gdh;
				 if (myGdh.isValid())
				     myGdh.addPreserveRequest();

				 return true;
			     }

			     return false;
			 }
    GU_DetailHandle	 getGdh()
			 {
			     return myGdh;
			 }

    XUSD_TicketPtr	 createTicket()
			 {
			     myTicketCount++;
			     return XUSD_TicketPtr(
				new XUSD_Ticket(myNodePath, myCookArgs));
			 }
    bool		 returnTicket()
			 {
			     myTicketCount--;
			     return (myTicketCount == 0);
			 }

    bool		 matches(const UT_StringRef &nodepath,
				const XUSD_TicketArgs &args)
			 {
			     return nodepath == myNodePath &&
				    args == myCookArgs;
			 }
    std::string          getLayerIdentifier() const
                         {
                             return SdfLayer::CreateIdentifier(
                                    myNodePath.toStdString(), myCookArgs);
                         }

private:
    UT_StringHolder	 myNodePath;
    XUSD_TicketArgs	 myCookArgs;
    GU_DetailHandle	 myGdh;
    int			 myTicketCount;
};
typedef UT_IntrusivePtr<RegistryEntry> RegistryEntryPtr;

static UT_Array<RegistryEntryPtr> theRegistryEntries;

XUSD_TicketPtr
XUSD_TicketRegistry::createTicket(const UT_StringHolder &nodepath,
	const XUSD_TicketArgs &args,
	const GU_DetailHandle &gdh)
{
    for (int i = 0, n = theRegistryEntries.size(); i < n; i++)
    {
	if (theRegistryEntries(i)->matches(nodepath, args))
	{
	    if (theRegistryEntries(i)->setGdh(gdh))
	    {
		SdfLayerHandle	 layer;
		
		layer = SdfLayer::Find(nodepath.toStdString(), args);
		if (layer)
                {
                    // Clear the whole cache of automatic ref prim paths,
                    // because the layer we are reloading may be used by any
                    // stage, and so may affect the default/automatic default
                    // prim of any stage.
                    HUSDclearBestRefPathCache();
		    layer->Reload(true);
                }
	    }

	    return theRegistryEntries(i)->createTicket();
	}
    }

    theRegistryEntries.append(new RegistryEntry(nodepath, args, gdh));
    return theRegistryEntries.last()->createTicket();
}

GU_DetailHandle
XUSD_TicketRegistry::getGeometry(const UT_StringRef &nodepath,
	const XUSD_TicketArgs &args)
{
    for (int i = 0, n = theRegistryEntries.size(); i < n; i++)
    {
	if (theRegistryEntries(i)->matches(nodepath, args))
	{
	    return theRegistryEntries(i)->getGdh();
	}
    }

    return GU_DetailHandle();
}

void
XUSD_TicketRegistry::returnTicket(const UT_StringHolder &nodepath,
	const XUSD_TicketArgs &args)
{
    for (int i = 0, n = theRegistryEntries.size(); i < n; i++)
    {
	if (theRegistryEntries(i)->matches(nodepath, args))
	{
	    if (theRegistryEntries(i)->returnTicket())
            {
                HUSDclearBestRefPathCache(
                    theRegistryEntries(i)->getLayerIdentifier());
		theRegistryEntries.removeIndex(i);
            }
	    break;
	}
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

