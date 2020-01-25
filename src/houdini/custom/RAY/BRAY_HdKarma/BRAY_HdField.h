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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#ifndef __BRAY_HD_FIELD_H__
#define __BRAY_HD_FIELD_H__

#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/field.h>
#include <GT/GT_Handles.h>
#include <UT/UT_Lock.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringSet.h>

PXR_NAMESPACE_OPEN_SCOPE

///
/// HdField represents an actual data of field that might not be 
/// actually renderable.
/// 

class BRAY_HdField : public HdField
{
public:

    BRAY_HdField(const TfToken& typeId, const SdfPath& primId);

    virtual			~BRAY_HdField() = default;

    virtual void		Sync(HdSceneDelegate* sceneDelegate,
				     HdRenderParam* renderParam,
				     HdDirtyBits* dirtyBits) override;

    GT_PrimitiveHandle		getGTPrimitive() const
				{ return myField; }

    const UT_StringHolder&	getFieldName() const
				{ return myFieldName; };

    const TfToken&		getFieldType() const
				{ return myFieldType; }

    const UT_SmallArray<GfMatrix4d>&	getXfms() const
				{ return myXfm; }

    /// Returns true if registered
    bool			registerVolume(const UT_StringHolder& volume);

protected:

    virtual HdDirtyBits		GetInitialDirtyBitsMask() const override
				{ return AllDirty; }

    void			dirtyVolumes(HdSceneDelegate* sceneDelegate);

private:

    void			updateGTPrimitive();

    GT_PrimitiveHandle		myField;
    TfToken			myFieldType;
    UT_StringHolder 		myFilePath;
    UT_StringHolder		myFieldName;
    UT_SmallArray<GfMatrix4d>	myXfm;
    UT_StringSet		myVolumes;
    int				myFieldIdx;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
