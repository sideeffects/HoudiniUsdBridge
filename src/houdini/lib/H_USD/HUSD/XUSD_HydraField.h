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
 * NAME:	XUSD_HydraField.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for a hydra light prim (HdRprim)
 */
#ifndef XUSD_HydraField_h
#define XUSD_HydraField_h

#include <pxr/pxr.h>
#include <pxr/imaging/hd/field.h>
#include <GT/GT_Primitive.h>
#include <UT/UT_StringHolder.h>

class HUSD_HydraField;

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_HydraField : public HdField
{
public:
	     XUSD_HydraField(TfToken const& typeId,
			     SdfPath const& primId,
			     HUSD_HydraField &field);
            ~XUSD_HydraField() override;
    
    void Sync(HdSceneDelegate *sceneDelegate,
              HdRenderParam *renderParam,
              HdDirtyBits *dirtyBits) override;

    GT_PrimitiveHandle getGTPrimitive() const;
    const UT_StringHolder &getFieldType() const;

protected:
    HdDirtyBits GetInitialDirtyBitsMask() const override;

private:
    void		 dirtyVolumes(HdSceneDelegate *sceneDelegate);

    UT_StringHolder	 myFieldType;
    HUSD_HydraField	&myField;
    bool		 myDirtyFlag;
};

PXR_NAMESPACE_CLOSE_SCOPE
#endif
