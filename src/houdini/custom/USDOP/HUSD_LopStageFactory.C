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
		"rootlayer",
		ArGetResolver().
		    CreateDefaultContextForAsset(assetpath.toStdString()),
		loadset);
	}
    }

    return UsdStageRefPtr();
}

PXR_NAMESPACE_CLOSE_SCOPE

