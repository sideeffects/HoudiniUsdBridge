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
 * NAME:	HUSD_HydraGeoPrim.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for GT prim repr of a hydro geometry (R) prim
 */
#include "HUSD_HydraGeoPrim.h"
#include "XUSD_HydraGeoPrim.h"

#include <pxr/imaging/hd/tokens.h>

#include <GT/GT_Primitive.h>
#include <GT/GT_PrimInstance.h>


HUSD_HydraGeoPrim::HUSD_HydraGeoPrim(HUSD_Scene &scene,
                                     const char *geo_id,
                                     bool consolidated)
    : HUSD_HydraPrim(scene, geo_id),
      myDirtyMask(ALL_DIRTY),
      myDeferBits(0),
      myIndex(-1),
      myNeedGLStateCheck(false),
      myIsVisible(true),
      myIsInstanced(false),
      myIsConsolidated(consolidated),
      myHasMatOverrides(false)
{
}

HUSD_HydraGeoPrim::~HUSD_HydraGeoPrim()
{
}


bool
HUSD_HydraGeoPrim::getBounds(UT_BoundingBox &box) const
{
    UT_BoundingBox lbox;
    if(myInstance && myInstance->getDetailAttributes())
    {
	auto &&bmn = myInstance->getDetailAttributes()->get("__bboxmin");
	auto &&bmx = myInstance->getDetailAttributes()->get("__bboxmax");
	if(bmn && bmx)
	{
	    lbox.setBounds(bmn->getF32(0,0),
			   bmn->getF32(0,1),
			   bmn->getF32(0,2),
			   bmx->getF32(0,0),
			   bmx->getF32(0,1),
			   bmx->getF32(0,2));
	}
	else
	{
	    lbox.makeInvalid();
	    myGTPrim->enlargeBounds(&lbox, 1);
	}

	if(myGTPrim->getPrimitiveTransform())
	{
	    UT_Matrix4F imat;
	    myGTPrim->getPrimitiveTransform()->getMatrix(imat);
	    lbox.transform(imat);
	}
	
	if(myInstance->getPrimitiveTransform())
	{
	    UT_Matrix4F imat;
	    myInstance->getPrimitiveTransform()->getMatrix(imat);
	    lbox.transform(imat);
	}

	if(myInstance->getPrimitiveType() == GT_PRIM_INSTANCE)
	{
	    auto inst = static_cast<const GT_PrimInstance*>(myInstance.get());
	    auto &trans = inst->transforms();
	    if(trans)
	    {
		const int n = trans->entries();
		UT_BoundingBox total;
		total.makeInvalid();
		
		for(int i=0; i<n; i++)
		{
		    UT_BoundingBox ibox(lbox);
		    UT_Matrix4F imat;
		    trans->get(i)->getMatrix(imat);
		    ibox.transform(imat);
		    
		    total.enlargeBounds(ibox);
		}
		
		box = total;
	    }
	    else
		box = lbox;
	}
    }
    else if(myInstance)
    {
	lbox.makeInvalid();
	myInstance->enlargeBounds(&lbox, 1);
    }
    else
	return false;
    
    return true;
}
void
HUSD_HydraGeoPrim::setVisible(bool v)
{
    if(v != myIsVisible)
    {
	myIsVisible = v;
	myDirtyMask = myDirtyMask | HUSD_HydraGeoPrim::VIS_CHANGE;
    }
}
