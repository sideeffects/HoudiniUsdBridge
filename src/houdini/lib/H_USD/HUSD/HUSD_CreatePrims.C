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
	sdfspecifier,
        sdfspecifier,
	tfparentprimtype);

    if (primspec && !tfprimtype.empty())
        primspec->SetTypeName(tfprimtype);

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
	SdfSpecifier		 sdfspecifier(HUSDgetSdfSpecifier(specifier));
	SdfPrimSpecHandle	 sdfprim;

	sdfprim = createPrimInLayer(outdata->stage(), outlayer,
	    sdfpath, sdfspecifier, primtype, primkind, parent_primtype);

	if (sdfprim)
	{
	    if (myPrimEditorNodeId != OP_INVALID_ITEM_ID)
                HUSDaddPrimEditorNodeId(sdfprim, myPrimEditorNodeId);
	    success = true;
	}
    }

    return success;
}

