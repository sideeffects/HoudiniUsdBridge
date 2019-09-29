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

#ifndef __HUSD_CvexDataCommand__
#define __HUSD_CvexDataCommand__

#include "HUSD_API.h"

#include <UT/UT_UniquePtr.h>
#include <VEX/VEX_PodTypes.h>

class HUSD_TimeCode; 
class HUSD_AutoWriteLock;

template <VEX_Precision PREC> class VEX_GeoCommandQueue;
template <typename T> class UT_Array;


/// Specify the VEX runtime precision for HUSD execution of VEX programs.
static constexpr VEX_Precision HUSD_VEX_PREC = VEX_64;


/// Abstracts the object that handles USD VEX edit functions that submitted
/// the stage modificaiton requests with it.
class HUSD_API HUSD_CvexDataCommand
{
public:
    /// Creates the cvex code object given the string and its meaning.
	     HUSD_CvexDataCommand();
    virtual ~HUSD_CvexDataCommand();

    /// Applies the commands from the queue.
    virtual void    apply(HUSD_AutoWriteLock &writelock,
                            const HUSD_TimeCode &time_code) = 0;

    /// Sets the number of command queues, usually one for each thread.
    void	    setCommandQueueCount( int count );
    int		    getCommandQueueCount() const;

    /// Returns the vex data command queue (ie, usd data edit requests).
    VEX_GeoCommandQueue<HUSD_VEX_PREC> &getCommandQueue( int index );

private:
    // Note, using unique ptr to avoid include bloat in the header.
    UT_UniquePtr<UT_Array<VEX_GeoCommandQueue<HUSD_VEX_PREC>>> myVexGeoCommands;
};

#endif

