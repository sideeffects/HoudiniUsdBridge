/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	HD_HoudiniRendererPlugin.C
 *
 * COMMENTS:	Render delegate for the native Houdini viewport renderer
 */

#include "HUSD_LopStageFactory.h"
#include <LOP/LOP_Network.h>
#include <LOP/LOP_Node.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_OPEN_SCOPE

void
newStageFactory(UT_Array<XUSD_StageFactory *> *factories)
{
    factories->append(new HUSD_LopStageFactory());
}

UsdStageRefPtr
HUSD_LopStageFactory::createStage(UsdStage::InitialLoadSet loadset,
	int nodeid) const
{
    LOP_Node	*lop = CAST_LOPNODE(OP_Node::lookupNode(nodeid));

    if (lop)
    {
	LOP_Network	*lopnet=dynamic_cast<LOP_Network *>(lop->getCreator());
	UT_String	 assetpath;

	// Get the resolver context asset path from either the LOP node or the
	// LOP Network. The node takes priority.
	if (lop->getResolverContextAssetPath(assetpath) ||
	    (lopnet && lopnet->getResolverContextAssetPath(assetpath)))
	{
	    return UsdStage::CreateInMemory(
		"root.usd",
		ArGetResolver().
		    CreateDefaultContextForAsset(assetpath.toStdString()),
		loadset);
	}
    }

    return UsdStageRefPtr();
}

PXR_NAMESPACE_CLOSE_SCOPE

