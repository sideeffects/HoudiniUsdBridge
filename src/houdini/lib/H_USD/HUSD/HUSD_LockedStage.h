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

#ifndef __HUSD_LockedStage_h__
#define __HUSD_LockedStage_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Utils.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>

// This class wraps a stage that is constructed from a LOP node by the
// HUSD_LockedStageRegistry, and which is guaranteed not to change, even if
// the node recooks. This is used primarily by reference and sublayer LOPs
// which can reference their inputs. But we may reference the same input
// multiple times, with different context options values. So we actually
// need to reference a locked copy of the LOP's stage.
//
// Only the HUSD_LockedStageRegistry singleton should create these objects,
// though they may be destroyed by anyone that holds onto a shared pointer
// to one. Only const methods are exposed publicly.
class HUSD_API HUSD_LockedStage
{
public:
				~HUSD_LockedStage();

    bool			 isValid() const;
    bool			 strippedLayers() const;
    const UT_StringHolder	&getRootLayerIdentifier() const
				 { return myRootLayerIdentifier; }

private:
				 HUSD_LockedStage(const HUSD_DataHandle &data,
					bool strip_layers);

    bool			 lockStage(const HUSD_DataHandle &data,
					bool strip_layers);

    class husd_LockedStagePrivate;

    UT_UniquePtr<husd_LockedStagePrivate>	 myPrivate;
    UT_StringHolder				 myRootLayerIdentifier;
    bool					 myStrippedLayers;
    friend class				 HUSD_LockedStageRegistry;
};

#endif

