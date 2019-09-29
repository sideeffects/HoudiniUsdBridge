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
    : myIdentifier(identifier)
{
    myIdentifier = identifier;
    myPrimPath = SdfPath::AbsoluteRootPath().GetString();
    myPrimName = "/"_sh;
}

HUSD_SpecHandle::HUSD_SpecHandle(const UT_StringHolder &identifier,
	const UT_StringHolder &prim_path,
	const UT_StringHolder &prim_name)
    : myIdentifier(identifier),
      myPrimPath(prim_path),
      myPrimName(prim_name)
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
		lock.spec()->GetPath().AppendChild(
		    TfToken(child)).GetString(),
		child));
	}

	for (auto &&it : lock.spec()->GetVariantSets())
	{
	    SdfVariantSetSpecHandle vset = it.second;
	    for (auto &&variant : vset->GetVariants())
	    {
		children.append(
		    HUSD_SpecHandle(myIdentifier,
			lock.spec()->GetPath().AppendVariantSelection(
			    it.first, variant->GetName()).GetString(),
			variant->GetName()));
	    }
	}
    }
}

