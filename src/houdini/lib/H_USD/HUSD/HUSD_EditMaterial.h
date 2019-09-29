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

#ifndef __HUSD_EditMaterial_h__
#define __HUSD_EditMaterial_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"

class OP_Network;

class HUSD_API HUSD_EditMaterial
{
public:
    /// Standard c-tor.
			HUSD_EditMaterial(HUSD_AutoAnyLock &lock);

    /// Creates the VOP children inside the LOP @p parent node 
    /// to reflect the USD material defined at the given @p path.
    /// If succeeds, returns the name of the created material child node.
    UT_StringHolder	loadMaterial( OP_Network &parent_node,
				const UT_StringRef &material_prim_path) const;

private:
    HUSD_AutoAnyLock	&myAnyLock;
};

#endif

