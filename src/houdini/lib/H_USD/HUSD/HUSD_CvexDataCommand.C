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
 */


#include "HUSD_CvexDataCommand.h"
#include <VEX/VEX_GeoCommand.h>

HUSD_CvexDataCommand::HUSD_CvexDataCommand()
    : myVexGeoCommands( new UT_Array<VEX_GeoCommandQueue<HUSD_VEX_PREC>> )
{
}

HUSD_CvexDataCommand::~HUSD_CvexDataCommand()
{
}

void
HUSD_CvexDataCommand::setCommandQueueCount( int count )
{
    myVexGeoCommands->setSize( count );
}

int
HUSD_CvexDataCommand::getCommandQueueCount() const
{
    return myVexGeoCommands->size();
}

VEX_GeoCommandQueue<HUSD_VEX_PREC> &
HUSD_CvexDataCommand::getCommandQueue( int index )
{ 
    return (*myVexGeoCommands)[ index ];
}

