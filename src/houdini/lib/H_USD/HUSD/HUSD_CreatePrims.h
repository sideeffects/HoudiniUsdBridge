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

#ifndef __HUSD_CreatePrims_h__
#define __HUSD_CreatePrims_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>

class HUSD_API HUSD_CreatePrims
{
public:
			 HUSD_CreatePrims(HUSD_AutoLayerLock &lock);
			~HUSD_CreatePrims();

    // Create a new primitive (and any non-existent ancestor prims). If the
    // primitive already exists on the stage, nothing happens. The kind and
    // type of the prim are _not_ modified.
    bool		 createPrim(const UT_StringRef &primpath,
				const UT_StringRef &primtype,
				const UT_StringRef &primkind,
				const UT_StringRef &specifier,
				const UT_StringRef &parent_primtype) const;

    int			 primEditorNodeId() const
			 { return myPrimEditorNodeId; }
    void		 setPrimEditorNodeId(int nodeid)
			 { myPrimEditorNodeId = nodeid; }

private:
    const HUSD_AutoLayerLock	&myLayerLock;
    int				 myPrimEditorNodeId;
};

#endif

