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

#include "HUSD_ConfigurePrims.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_PathSet.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_ConfigurePrims::HUSD_ConfigurePrims(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock), myTimeSampling(HUSD_TimeSampling::NONE)
{
}

HUSD_ConfigurePrims::~HUSD_ConfigurePrims()
{
}

template <typename F>
static inline bool
husdConfigPrim(HUSD_AutoWriteLock &lock,
	const HUSD_FindPrims &findprims,
	F config_fn)
{
    auto		 outdata = lock.data();

    if (!outdata || !outdata->isStageValid())
	return false;

    auto		 stage(outdata->stage());
    bool		 success(true);

    for (auto &&sdfpath : findprims.getExpandedPathSet().sdfPathSet())
    {
	UsdPrim prim = stage->GetPrimAtPath(sdfpath);

	if (!prim || !config_fn(prim))
	    success = false;
    }

    return success;
}

bool
HUSD_ConfigurePrims::setType(const HUSD_FindPrims &findprims,
        const UT_StringRef &primtype) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	prim.SetTypeName(TfToken(primtype.toStdString()));

	return true;
    });
}

bool
HUSD_ConfigurePrims::setActive(const HUSD_FindPrims &findprims,
	bool active) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	prim.SetActive(active);

	return true;
    });
}

bool
HUSD_ConfigurePrims::setKind(const HUSD_FindPrims &findprims,
	const UT_StringRef &kind) const
{
    TfToken	 kind_token(kind);

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	UsdModelAPI modelapi(prim);

	if (!modelapi)
	    return false;

	modelapi.SetKind(kind_token);

	return true;
    });
}

bool
HUSD_ConfigurePrims::setDrawMode(const HUSD_FindPrims &findprims,
	const UT_StringRef &drawmode) const
{
    TfToken	 drawmode_token(drawmode);

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	UsdGeomModelAPI modelapi = UsdGeomModelAPI::Apply(prim);

	if (!modelapi)
	    return false;

	modelapi.CreateModelDrawModeAttr().Set(drawmode_token);

	return true;
    });
}

bool
HUSD_ConfigurePrims::setPurpose(const HUSD_FindPrims &findprims,
	const UT_StringRef &purpose) const
{
    VtValue	 def_value(UsdGeomTokens->default_);
    TfToken	 purpose_token(purpose);

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	UsdGeomImageable imageable(prim);

	if (!imageable)
	    return false;

	imageable.CreatePurposeAttr(def_value).Set(purpose_token);

	return true;
    });
}

bool
HUSD_ConfigurePrims::setProxy(const HUSD_FindPrims &findprims,
	const UT_StringRef &proxy) const
{
    SdfPathVector	 proxy_targets({ HUSDgetSdfPath(proxy) });

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	UsdGeomImageable imageable(prim);

	if (!imageable)
	    return false;

	imageable.CreateProxyPrimRel().SetTargets(proxy_targets);

	return true;
    });
}

bool
HUSD_ConfigurePrims::setInstanceable(const HUSD_FindPrims &findprims,
	bool instanceable) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	// "Gprim" primitives should not be marked as instanceable.
        // Just add a warning, but set the instanceable flag anyway.
	if (prim.IsA<UsdGeomGprim>())
        {
            HUSD_ErrorScope::addWarning(HUSD_ERR_GPRIM_MARKED_INSTANCEABLE,
                prim.GetPath().GetText());
        }

	prim.SetInstanceable(instanceable);

	return true;
    });
}

static inline UsdTimeCode
husdGetEffectiveUsdTimeCode(const HUSD_TimeCode &tc, 
	bool ignore_time_varying_stage, const UsdAttribute &attr)
{
    if (ignore_time_varying_stage || !attr)
	return HUSDgetUsdTimeCode(tc);

    return HUSDgetEffectiveUsdTimeCode(tc, attr);
}

bool
HUSD_ConfigurePrims::setInvisible(const HUSD_FindPrims &findprims,
	HUSD_ConfigurePrims::Visibility vis,
	const HUSD_TimeCode &timecode,
        bool ignore_time_varying_stage) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 stage(outdata->stage());

	success = true;
	for (auto &&sdfpath : findprims.getExpandedPathSet().sdfPathSet())
	{
	    UsdGeomImageable imageable(stage->GetPrimAtPath(sdfpath));

	    if (imageable)
	    {
		// Set the attribute at either the specified time, or at
		// the default if we want it to apply for all time.
		UsdAttribute	 attr = imageable.GetVisibilityAttr();
		UsdTimeCode	 usdtime = husdGetEffectiveUsdTimeCode(
                                    timecode, ignore_time_varying_stage, attr);

                if (!ignore_time_varying_stage)
                    HUSDupdateValueTimeSampling(myTimeSampling, attr);

		if (vis == VISIBILITY_INVISIBLE)
		{
		    // To make the prim invisible for all time, we must block
		    // any existing animated visibility.
		    if (usdtime.IsDefault() && attr)
			attr.Block();

		    // If we didn't already have a visibility attr, create one.
		    if (!attr)
			attr = imageable.CreateVisibilityAttr();

		    attr.Set(UsdGeomTokens->invisible, usdtime);
		}
		else if (vis == VISIBILITY_VISIBLE)
		{
                    imageable.MakeVisible(usdtime);
		}
		else // vis == VISIBILITY_INHERIT
		{
		    // The default for a prim is to be visible, so we only have
		    // to do something if we already have a visibility attr.
		    if (attr)
		    {
			// To make it visible for all time, just block any
			// overrides. Otherwise set the attr at the given time.
			if (usdtime.IsDefault())
			    attr.Block();
			else
			    attr.Set(UsdGeomTokens->inherited, usdtime);
		    }
		}
	    }
	    else
		success = false;
	}
    }

    return success;
}

bool
HUSD_ConfigurePrims::setVariantSelection(const HUSD_FindPrims &findprims,
	const UT_StringRef &variantset,
	const UT_StringRef &variant) const
{
    std::string	 vset_str(variantset.toStdString());
    std::string	 variant_str(variant.toStdString());

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	if (!prim.GetVariantSets().HasVariantSet(vset_str))
	    return false;

	return prim.GetVariantSets().SetSelection(vset_str,variant_str);
    });
}

bool
HUSD_ConfigurePrims::setAssetName(const HUSD_FindPrims &findprims,
	const UT_StringRef &name) const
{
    std::string	 name_str(name);

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	UsdModelAPI modelapi(prim);

	if (!modelapi)
	    return false;

	modelapi.SetAssetName(name_str);

	return true;
    });
}

bool
HUSD_ConfigurePrims::setAssetIdentifier(const HUSD_FindPrims &findprims,
	const UT_StringRef &identifier) const
{
    SdfAssetPath asset_path(identifier.toStdString());

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	UsdModelAPI modelapi(prim);

	if (!modelapi)
	    return false;

	modelapi.SetAssetIdentifier(asset_path);

	return true;
    });
}

bool
HUSD_ConfigurePrims::setAssetVersion(const HUSD_FindPrims &findprims,
	const UT_StringRef &version) const
{
    std::string	 version_str(version.toStdString());

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	UsdModelAPI modelapi(prim);

	if (!modelapi)
	    return false;

	modelapi.SetAssetVersion(version_str);

	return true;
    });
}

bool
HUSD_ConfigurePrims::setAssetDependencies(const HUSD_FindPrims &findprims,
	const UT_StringArray &dependencies) const
{
    VtArray<SdfAssetPath>	 asset_paths;

    for (auto &&identifier : dependencies)
	asset_paths.push_back(SdfAssetPath(identifier.toStdString()));

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	UsdModelAPI modelapi(prim);

	if (!modelapi)
	    return false;

	modelapi.SetPayloadAssetDependencies(asset_paths);

	return true;
    });
}

bool
HUSD_ConfigurePrims::setEditorNodeId(const HUSD_FindPrims &findprims,
	int nodeid) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	HUSDsetPrimEditorNodeId(prim, nodeid);

	return true;
    });
}

bool
HUSD_ConfigurePrims::applyAPI(const HUSD_FindPrims &findprims,
	const UT_StringRef &schema) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	auto		 layer(outdata->activeLayer());
	UsdSchemaRegistry &registry = UsdSchemaRegistry::GetInstance();
	TfToken		 tf_schema(schema.toStdString());
	TfType		 schema_type = registry.GetTypeFromName(tf_schema);

	if (registry.IsAppliedAPISchema(schema_type))
	{
	    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
	    {
		if (prim.HasAPI(schema_type))
		    return true;

		// This code is lifted from UsdAPISchemaBase. It differs in
		// that we have already verified that the prim doesn't have
		// the specified API schema.
		SdfPrimSpecHandle primspec;

		primspec = SdfCreatePrimInLayer(layer, prim.GetPath());
		if (!primspec)
		    return false;

		SdfTokenListOp listOp = primspec->
		    GetInfo(UsdTokens->apiSchemas).
			UncheckedGet<SdfTokenListOp>();
		TfTokenVector all_api_schemas = listOp.IsExplicit()
		    ? listOp.GetExplicitItems()
		    : listOp.GetPrependedItems();

		all_api_schemas.push_back(tf_schema);
		SdfTokenListOp prepended_list_op;
		prepended_list_op.SetPrependedItems(all_api_schemas);

		if (auto result = listOp.ApplyOperations(prepended_list_op))
		{
		    primspec->SetInfo(UsdTokens->apiSchemas, VtValue(*result));
		    return true;
		}

		return false;
	    });
	}
    }

    return false;
}

bool
HUSD_ConfigurePrims::getIsTimeVarying() const
{
    return HUSDisTimeVarying(myTimeSampling);
}

