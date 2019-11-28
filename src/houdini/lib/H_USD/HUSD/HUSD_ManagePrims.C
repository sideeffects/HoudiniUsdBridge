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

#include "HUSD_ManagePrims.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include <gusd/UT_Gf.h>
#include <OP/OP_ItemId.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/proxyTypes.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/relationshipSpec.h>
#include <pxr/usd/sdf/valueTypeName.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_ManagePrims::HUSD_ManagePrims(HUSD_AutoLayerLock &lock)
    : myLayerLock(lock),
      myPrimEditorNodeId(OP_INVALID_ITEM_ID)
{
}

HUSD_ManagePrims::~HUSD_ManagePrims()
{
}

bool
HUSD_ManagePrims::copyPrim(const UT_StringRef &source_primpath,
	const UT_StringRef &dest_primpath,
	const UT_StringRef &parentprimtype) const
{
    if (dest_primpath == source_primpath ||
        (dest_primpath.startsWith(source_primpath) &&
         dest_primpath.c_str()[source_primpath.length()] == '/'))
    {
        HUSD_ErrorScope::addWarning(HUSD_ERR_CANT_COPY_PRIM_INTO_ITSELF,
            source_primpath.c_str());
        return false;
    }

    SdfLayerHandle	 layer = myLayerLock.layer()->layer();
    auto		 indata = myLayerLock.constData();
    bool		 success = false;

    if (layer && indata && indata->isStageValid())
    {
	auto		 stage = indata->stage();
	SdfPath		 sdfsrcpath(HUSDgetSdfPath(source_primpath));
	SdfPath		 sdfdestpath(HUSDgetSdfPath(dest_primpath));
	UsdPrim		 existingprim(stage->GetPrimAtPath(sdfdestpath));
	GfMatrix4d	 xform;
	bool		 resets_xform = false;
	bool		 override_xform = false;

	// If the destination prim already exists on the stage, get its
	// transform, and apply it after the copy operation. This code path
	// is used when "de-referencing" a primitive, where we want the
	// prim to stay where it is, not move to the source prim's location.
	if (existingprim)
	{
	    UsdGeomXformable	 xformable(existingprim);

	    if (xformable)
	    {
		xformable.GetLocalTransformation(&xform, &resets_xform);
		override_xform = true;
	    }
	}

	// Make sure the destination prim and its ancestors exist before we
	// try to copy anything into it.
	HUSDcreatePrimInLayer(stage, layer, sdfdestpath, TfToken(), true,
	    HUSDgetPrimTypeAlias(parentprimtype).toStdString());
	success = HUSDcopySpec(layer, sdfsrcpath, layer, sdfdestpath);

	// If requested, override the xform on the new prim location with
	// its previous value.
	if (override_xform)
	{
	    SdfPrimSpecHandle	 destprim(layer->GetPrimAtPath(sdfdestpath));

	    if (destprim)
	    {
		TfToken			 op_name("xformOp:transform");
		VtArray<TfToken>	 op_order({op_name});
		SdfAttributeSpecHandle	 op_spec;
		SdfAttributeSpecHandle	 op_order_spec;

                HUSDsetPrimEditorNodeId(destprim, myPrimEditorNodeId);
		if (resets_xform)
		    op_order = VtArray<TfToken>({
			UsdGeomXformOpTypes->resetXformStack,
			op_name });
		// Try to get the existing attribute. If it doesn't exist,
		// create a new attribute.
		op_spec = destprim->GetAttributeAtPath(
		    SdfPath::ReflexiveRelativePath().
		    AppendProperty(op_name));
		if (!op_spec)
		    op_spec = SdfAttributeSpec::New(destprim,
			op_name, SdfValueTypeNames->Matrix4d);
		if (op_spec)
		    op_spec->SetDefaultValue(VtValue(xform));
		// Try to get the existing attribute. If it doesn't exist,
		// create a new attribute.
		op_order_spec = destprim->GetAttributeAtPath(
		    SdfPath::ReflexiveRelativePath().
		    AppendProperty(UsdGeomTokens->xformOpOrder));
		if (!op_order_spec)
		    op_order_spec = SdfAttributeSpec::New(destprim,
			UsdGeomTokens->xformOpOrder,
			SdfValueTypeNames->TokenArray,
			SdfVariabilityUniform);
		if (op_order_spec)
		    op_order_spec->SetDefaultValue(VtValue(op_order));
	    }
	}
    }

    return success;
}

void
husdUpdateInternalReferences(const SdfPath &srcpath,
        const SdfPath &destpath,
        const SdfPrimSpecHandle &primspec)
{
    primspec->GetReferenceList().ModifyItemEdits(
        [&](const SdfReference &ref) {
            if (ref.GetAssetPath().empty() &&
                ref.GetPrimPath().HasPrefix(srcpath))
            {
                SdfReference destref(std::string(),
                    ref.GetPrimPath().ReplacePrefix(
                        srcpath, destpath, false));
                return BOOST_NS::optional<SdfReference>(destref);
            }

            return BOOST_NS::optional<SdfReference>(ref);
        });

    for (auto &&childspec : primspec->GetNameChildren())
        husdUpdateInternalReferences(srcpath, destpath, childspec);
}

bool
HUSD_ManagePrims::movePrim(const UT_StringRef &source_primpath,
	const UT_StringRef &dest_primpath,
	const UT_StringRef &parentprimtype) const
{
    // If the source and dest are the same, we haven't actually been asked
    // to do anything, so immediately exit and report success.
    if (dest_primpath == source_primpath)
        return true;

    if (dest_primpath.startsWith(source_primpath) &&
        dest_primpath.c_str()[source_primpath.length()] == '/')
    {
        HUSD_ErrorScope::addWarning(HUSD_ERR_CANT_MOVE_PRIM_INTO_ITSELF,
            source_primpath.c_str());
        return false;
    }

    bool		 success = false;

    success = copyPrim(source_primpath, dest_primpath, parentprimtype);
    if (success)
    {
	SdfLayerHandle	 layer = myLayerLock.layer()->layer();
	SdfPath		 sdf_srcpath(HUSDgetSdfPath(source_primpath));
	SdfPath		 sdf_destpath(HUSDgetSdfPath(dest_primpath));

	// Update internal references to this prim or any of its children to
	// point to the dest_primpath.
        for (auto &&rootspec : layer->GetRootPrims())
            husdUpdateInternalReferences(sdf_srcpath, sdf_destpath, rootspec);
    }

    if (success)
	success = deletePrim(source_primpath);

    return success;
}

bool
HUSD_ManagePrims::deletePrim(const UT_StringRef &primpath) const
{
    SdfLayerHandle	 layer = myLayerLock.layer()->layer();
    bool		 success = false;

    if (layer)
    {
	SdfPath			 sdfpath(HUSDgetSdfPath(primpath));
	SdfPrimSpecHandle	 primspec = layer->GetPrimAtPath(sdfpath);

	if (primspec && primspec->GetRealNameParent())
	    success = primspec->GetRealNameParent()->RemoveNameChild(primspec);
    }

    return success;
}

bool
HUSD_ManagePrims::setPrimReference(const UT_StringRef &primpath,
	const UT_StringRef &reffilepath,
	const UT_StringRef &refprimpath,
	bool as_payload) const
{
    SdfLayerHandle	 layer = myLayerLock.layer()->layer();
    bool		 success = false;

    if (layer)
    {
	SdfPath			 sdfpath(HUSDgetSdfPath(primpath));
	SdfPrimSpecHandle	 primspec = layer->GetPrimAtPath(sdfpath);

	if (primspec)
	{
            SdfPath bestrefprimpath;
            UsdStageRefPtr stage;

            HUSDsetPrimEditorNodeId(primspec, myPrimEditorNodeId);
            bestrefprimpath = HUSDgetBestRefPrimPath(
                reffilepath, SdfFileFormat::FileFormatArguments(),
                refprimpath, stage);
	    primspec->ClearPayloadList();
	    primspec->ClearReferenceList();
	    if (as_payload)
		primspec->GetPayloadList().GetAddedItems().Insert(-1,
		    SdfPayload(reffilepath.toStdString(),
			bestrefprimpath));
	    else
		primspec->GetReferenceList().GetAddedItems().Insert(-1,
		    SdfReference(reffilepath.toStdString(),
			bestrefprimpath));
	    success = true;
	}
    }

    return success;
}

bool
HUSD_ManagePrims::setPrimXform(const UT_StringRef &primpath,
	const UT_Matrix4D &xform) const
{
    SdfLayerHandle	 layer = myLayerLock.layer()->layer();
    bool		 success = false;

    if (layer)
    {
	SdfPath			 sdfpath(HUSDgetSdfPath(primpath));
	SdfPrimSpecHandle	 primspec = layer->GetPrimAtPath(sdfpath);

	if (primspec)
	{
	    SdfAttributeSpecHandle	 opspec;
	    SdfAttributeSpecHandle	 xformspec;
	    static const TfToken	 theXformToken("xformOp:transform");

            HUSDsetPrimEditorNodeId(primspec, myPrimEditorNodeId);
	    xformspec = primspec->GetAttributeAtPath(primspec->GetPath().
		AppendProperty(theXformToken));
	    if (!xformspec)
		xformspec = SdfAttributeSpec::New(primspec,
		    theXformToken.GetString(),
		    SdfValueTypeNames->Matrix4d);
	    opspec = primspec->GetAttributeAtPath(primspec->GetPath().
		AppendProperty(UsdGeomTokens->xformOpOrder));
	    if (!opspec)
		opspec = SdfAttributeSpec::New(primspec,
		    UsdGeomTokens->xformOpOrder.GetString(),
		    SdfValueTypeNames->TokenArray,
		    SdfVariabilityUniform);
	    if (xformspec && opspec)
	    {
		VtArray<TfToken>		 opvalue;

		xformspec->SetDefaultValue(VtValue(GusdUT_Gf::Cast(xform)));
		opvalue.push_back(theXformToken);
		opspec->SetDefaultValue(VtValue(opvalue));

		success = true;
	    }
	}
    }

    return success;
}

bool
HUSD_ManagePrims::setPrimVariant(const UT_StringRef &primpath,
	const UT_StringRef &variantset,
	const UT_StringRef &variantname)
{
    SdfLayerHandle	 layer = myLayerLock.layer()->layer();
    bool		 success = false;

    if (layer)
    {
	SdfPath			 sdfpath(HUSDgetSdfPath(primpath));
	SdfPrimSpecHandle	 primspec = layer->GetPrimAtPath(sdfpath);

	if (primspec)
	{
	    std::string	 vsetstr = variantset.toStdString();
	    std::string	 vnamestr = variantname.toStdString();

            HUSDsetPrimEditorNodeId(primspec, myPrimEditorNodeId);
	    primspec->SetVariantSelection(vsetstr, vnamestr);
	    success = true;
	}
    }

    return success;
}

