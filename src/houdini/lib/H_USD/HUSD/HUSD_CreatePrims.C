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

#include "HUSD_CreatePrims.h"
#include "HUSD_Constants.h"
#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include <OP/OP_ItemId.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

static SdfPrimSpecHandle
createPrimInLayer(const UsdStageWeakPtr &stage,
	const SdfLayerHandle &layer,
	const SdfPath &path,
	const SdfSpecifier &sdfspecifier,
	const UT_StringRef &primtype,
	const UT_StringRef &primkind,
	const UT_StringRef &parentprimtype)
{
    std::string		 tfprimtype;
    std::string		 tfparentprimtype;
    SdfPrimSpecHandle	 primspec;

    tfprimtype = HUSDgetPrimTypeAlias(primtype).toStdString();
    if (parentprimtype.isstring())
	tfparentprimtype = HUSDgetPrimTypeAlias(parentprimtype).toStdString();

    primspec = HUSDcreatePrimInLayer(stage, layer, path,
	TfToken(primkind.toStdString()),
	sdfspecifier != SdfSpecifierOver,
	tfparentprimtype);
    if (primspec)
    {
	primspec->SetSpecifier(sdfspecifier);
	if (!tfprimtype.empty())
	    primspec->SetTypeName(tfprimtype);
    }

    return primspec;
}

HUSD_CreatePrims::HUSD_CreatePrims(HUSD_AutoLayerLock &lock)
    : myLayerLock(lock),
      myPrimEditorNodeId(OP_INVALID_ITEM_ID)
{
}

HUSD_CreatePrims::~HUSD_CreatePrims()
{
}

bool
HUSD_CreatePrims::createPrim(const UT_StringRef &primpath,
	const UT_StringRef &primtype,
	const UT_StringRef &primkind,
	const UT_StringRef &specifier,
	const UT_StringRef &parent_primtype) const
{
    SdfLayerHandle	 outlayer = myLayerLock.layer()->layer();
    auto		 outdata = myLayerLock.constData();
    bool		 success = false;

    if (outlayer && outdata->isStageValid())
    {
	SdfPath			 sdfpath(HUSDgetSdfPath(primpath));
	SdfSpecifier		 sdfspecifier;
	SdfPrimSpecHandle	 sdfprim;

	if (specifier == HUSD_Constants::getPrimSpecifierDefine())
	    sdfspecifier = SdfSpecifierDef;
	else if (specifier == HUSD_Constants::getPrimSpecifierOverride())
	    sdfspecifier = SdfSpecifierOver;
	else if (specifier == HUSD_Constants::getPrimSpecifierClass())
	    sdfspecifier = SdfSpecifierClass;

	sdfprim = createPrimInLayer(outdata->stage(), outlayer,
	    sdfpath, sdfspecifier, primtype, primkind, parent_primtype);

	if (sdfprim)
	{
	    if (myPrimEditorNodeId != OP_INVALID_ITEM_ID)
		HUSDsetPrimEditorNodeId(sdfprim, myPrimEditorNodeId);
	    success = true;
	}
    }

    return success;
}

