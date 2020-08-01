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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_SpecHandle.h"
#include "HUSD_Overrides.h"
#include "XUSD_SpecLock.h"
#include <UT/UT_StringStream.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/sdf/abstractData.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/variantSpec.h>

using namespace UT::Literal;
PXR_NAMESPACE_USING_DIRECTIVE

static const UT_StringHolder	 theDataTypeKey = "Data Type"_sh;

HUSD_SpecHandle::HUSD_SpecHandle()
{
}

HUSD_SpecHandle::HUSD_SpecHandle(const UT_StringHolder &identifier)
    : myIdentifier(identifier),
      myPrimPath(HUSD_Path::theRootPrimPath)
{
}

HUSD_SpecHandle::HUSD_SpecHandle(const UT_StringHolder &identifier,
	const HUSD_Path &prim_path)
    : myIdentifier(identifier),
      myPrimPath(prim_path)
{
}

HUSD_SpecHandle::~HUSD_SpecHandle()
{
}

UT_StringHolder
HUSD_SpecHandle::getSpecType() const
{
    XUSD_AutoSpecLock	 lock(*this);
    UT_StringHolder	 spec_type;

    if (lock.spec())
	spec_type = lock.spec()->GetTypeName().GetText();

    return spec_type;
}

void
HUSD_SpecHandle::getChildren(UT_Array<HUSD_SpecHandle> &children) const
{
    XUSD_AutoSpecLock	 lock(*this);

    if (lock.spec())
    {
	for (auto &&child : lock.spec()->GetNameChildren().keys())
	{
	    children.append(
		HUSD_SpecHandle(myIdentifier,
		lock.spec()->GetPath().AppendChild(TfToken(child))));
	}

	for (auto &&it : lock.spec()->GetVariantSets())
	{
	    SdfVariantSetSpecHandle vset = it.second;
	    for (auto &&variant : vset->GetVariants())
	    {
		children.append(
		    HUSD_SpecHandle(myIdentifier,
			lock.spec()->GetPath().AppendVariantSelection(
			    it.first, variant->GetName())));
	    }
	}
    }
}

