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

#include <GT/GT_Names.h>
#include <GT/GT_Primitive.h>
#include <GT/GT_PrimInstance.h>


HUSD_HydraGeoPrim::HUSD_HydraGeoPrim(HUSD_Scene &scene,
                                     const HUSD_Path &path,
                                     bool consolidated)
    : HUSD_HydraPrim(scene, path),
      myDirtyMask(ALL_DIRTY),
      myDeferBits(0),
      myIndex(-1),
      myNeedGLStateCheck(false),
      myIsVisible(true),
      myIsInstanced(false),
      myHasMatOverrides(false),
      myIsConsolidated(consolidated)
{
}

HUSD_HydraGeoPrim::~HUSD_HydraGeoPrim()
{
}

bool
HUSD_HydraGeoPrim::getLocalBounds(UT_BoundingBox &box) const
{
    bool valid = false;
    if(myInstance && myInstance->getDetailAttributes())
    {
        auto &&bmn = myInstance->getDetailAttributes()->get(GT_Names::bboxmin);
        auto &&bmx = myInstance->getDetailAttributes()->get(GT_Names::bboxmax);
        if(bmn && bmx)
        {
            box.setBounds(bmn->getF32(0,0),
                          bmn->getF32(0,1),
                          bmn->getF32(0,2),
                          bmx->getF32(0,0),
                          bmx->getF32(0,1),
                          bmx->getF32(0,2));
        }
        else
        {
            box.makeInvalid();
            myGTPrim->enlargeBounds(&box, 1);
        }

        if(!box.isValid())
            return false;
        
        if(myGTPrim->getPrimitiveTransform())
        {
            UT_Matrix4F imat;
            myGTPrim->getPrimitiveTransform()->getMatrix(imat);
            box.transform(imat);
        }
	
        if(myInstance->getPrimitiveTransform())
        {
            UT_Matrix4F imat;
            myInstance->getPrimitiveTransform()->getMatrix(imat);
            box.transform(imat);
        }
        valid = true;
    }
    else if(myInstance)
    {
        box.makeInvalid();
	myInstance->enlargeBounds(&box, 1);
        valid = box.isValid();
    }

    return valid;
}

bool
HUSD_HydraGeoPrim::getBounds(UT_BoundingBox &box) const
{
    UT_BoundingBox lbox;
    if(!getLocalBounds(lbox))
        return false;

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
