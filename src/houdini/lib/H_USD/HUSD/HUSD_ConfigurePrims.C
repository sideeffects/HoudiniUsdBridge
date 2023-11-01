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
#include "HUSD_AssetPath.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_PathSet.h"
#include "HUSD_Token.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include "UsdHoudini/houdiniEditableAPI.h"
#include "UsdHoudini/houdiniSelectableAPI.h"
#include <pxr/usd/kind/registry.h>
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
        {
            success = false;
            break;
        }
    }

    return success;
}

template <typename F>
static inline bool
husdConfigPrimFromPath(HUSD_AutoWriteLock &lock,
                       const HUSD_PathSet &pathset,
                       F config_fn)
{
    auto		 outdata = lock.data();

    if (!outdata || !outdata->isStageValid())
        return false;

    auto		 stage(outdata->stage());
    bool		 success(true);

    for (auto &&sdfpath : pathset.sdfPathSet())
    {
        if (!config_fn(sdfpath))
            success = false;
    }

    return success;
}

bool
HUSD_ConfigurePrims::setType(const HUSD_FindPrims &findprims,
        const UT_StringRef &primtype) const
{
    std::string tfprimtype = HUSDgetPrimTypeAlias(primtype).toStdString();

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	prim.SetTypeName(TfToken(tfprimtype));

	return true;
    });
}

bool
HUSD_ConfigurePrims::setSpecifier(const HUSD_FindPrims &findprims,
        const UT_StringRef &specifier) const
{
    SdfSpecifier sdfspecifier = HUSDgetSdfSpecifier(specifier);

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
      prim.SetSpecifier(sdfspecifier);

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
husdMakePrimAndAncestorsActive(UsdStageRefPtr stage, SdfPath primpath,
                               bool emit_warning_on_action)
{
    bool has_inactive_ancestor = false;
    
    if (primpath.IsEmpty() || primpath == SdfPath::AbsoluteRootPath())
        return has_inactive_ancestor;
    
    // Check to see if our current prim exists (i.e., it's active)
    UsdPrim prim = stage->GetPrimAtPath(primpath);
    if (!prim)
    {
        // If no prim was found it may be because an ancestor is inactive,
        // so recurse up the hierarchy before checking again
        has_inactive_ancestor = husdMakePrimAndAncestorsActive(
                stage, primpath.GetParentPath(), false);
        
        // It's still possible that no prim can be found for this primpath,
        // generally because either:
        // 1 - The user specified a primpath that doesn't (yet?) exist
        // 2 - This function was called while a Sdf change block is active and
        //     the stage isn't recomposing, so the recursive calls to change the
        //     ancestors haven't generated any observable result here
        // We don't consider this an error condition. We can still proceed with
        // setting the visibility for ancestors and siblings.
        prim = stage->GetPrimAtPath(primpath);
    }
    
    // Similar to UsdGeomImageable::MakeVisible, we need to make siblings of
    // previously-inactive ancestors inactive...
    if (has_inactive_ancestor)
    {
        if (emit_warning_on_action)
            HUSD_ErrorScope::addWarning(HUSD_ERR_INACTIVE_ANCESTOR_FOUND);
        
        UsdPrim parent = prim ? prim.GetParent() :
                                stage->GetPrimAtPath(primpath.GetParentPath());
        if (parent)
        {
            for (const UsdPrim &child_prim : parent.GetAllChildren())
            {
                if (child_prim != prim)
                    child_prim.SetActive(false);
            }
        }
    }
    // ... but make ourselves active
    if (prim && !prim.IsActive())
    {
        prim.SetActive(true);
        has_inactive_ancestor = true;
    }
    return has_inactive_ancestor;
}

bool
HUSD_ConfigurePrims::makePrimsAndAncestorsActive(
    const HUSD_PathSet &pathset,
    bool emit_warning_on_action /*=false*/) const
{
    return husdConfigPrimFromPath(myWriteLock, pathset, [&](SdfPath path)
    {
      husdMakePrimAndAncestorsActive(myWriteLock.data()->stage(), path,
                                     emit_warning_on_action);

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
HUSD_ConfigurePrims::fixKindHierarchy(const HUSD_FindPrims &findprims) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
	UsdModelAPI modelapi(prim);

	if (!modelapi)
	    return false;

        TfToken kind;
        if (modelapi.GetKind(&kind) && KindRegistry::IsA(kind, KindTokens->model))
        {
            for (UsdPrim p = prim.GetParent(); !p.IsPseudoRoot(); p = p.GetParent())
            {
                UsdModelAPI pmodelapi(p);
                if (pmodelapi)
                    if (!pmodelapi.GetKind(&kind) || !KindRegistry::IsA(kind, KindTokens->group))
                        pmodelapi.SetKind(KindTokens->group);
            }
        }

	return true;
    });
}

bool
HUSD_ConfigurePrims::fixGprimHierarchy(const HUSD_FindPrims &findprims) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
                          {
                              UsdModelAPI modelapi(prim);

                              if (!modelapi)
                                  return false;

                              std::function<void(UsdPrim&)> deactivateChildGprims;
                              deactivateChildGprims = [&deactivateChildGprims](UsdPrim &prim)
                              {
                                  for (auto childPrim: prim.GetChildren())
                                  {
                                      if (childPrim.IsA<UsdGeomGprim>() && prim.IsActive())
                                          deactivateChildGprims(childPrim);
                                  }
                                  prim.SetActive(false);
                                  return;
                              };

                              deactivateChildGprims(prim);
                              return true;
                          });
}

bool
HUSD_ConfigurePrims::fixPrimvarInterpolation(const HUSD_FindPrims &findprims,
                                             const UT_StringHolder &primvarPath) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
                          {
                              UsdModelAPI modelapi(prim.GetParent());

                              if (!modelapi)
                                  return false;


                              UsdAttribute attr = prim.GetStage()->GetAttributeAtPath(
                                      SdfPath(primvarPath.toStdString()));
                              // possibly better than just attr.Block()?
                              UsdGeomPrimvar primvar(attr);
                              primvar.SetInterpolation(UsdGeomTokens->constant);

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
	UsdGeomModelAPI geommodelapi = UsdGeomModelAPI::Apply(prim);
	if (!geommodelapi)
            return false;
        geommodelapi.CreateModelDrawModeAttr().Set(drawmode_token);
	return true;
    });
}

// Apply GeomModelAPI and turn on model:applyDrawMode if it is not a component
// Call this to prepare the prim to draw cards or bounding box standins when/if drawMode is set on a parent.
// The attributes for the cards (color, texture maps, etc) can then be set with a HUSD_SetAttributes
bool
HUSD_ConfigurePrims::setApplyDrawMode(const HUSD_FindPrims &findprims,
        bool apply) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
        UsdGeomModelAPI geommodelapi = UsdGeomModelAPI::Apply(prim);
        if (!geommodelapi)
            return false;
        UsdModelAPI modelapi(prim);
        TfToken kind;
        if (!modelapi.GetKind(&kind) ||
            !KindRegistry::IsA(kind, KindTokens->component))
            geommodelapi.CreateModelApplyDrawModeAttr(VtValue(apply));
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
		UsdAttribute	 attr = imageable.CreateVisibilityAttr();
		UsdTimeCode	 usdtime = husdGetEffectiveUsdTimeCode(
                                    timecode, ignore_time_varying_stage, attr);

                if (!ignore_time_varying_stage)
                    HUSDupdateValueTimeSampling(myTimeSampling, attr);

		if (vis == VISIBILITY_INVISIBLE)
		{
		    // To make the prim invisible for all time, we must block
		    // any existing animated visibility.
		    if (usdtime.IsDefault())
			attr.Block();

		    attr.Set(UsdGeomTokens->invisible, usdtime);
		}
		else if (vis == VISIBILITY_VISIBLE)
		{
                    imageable.MakeVisible(usdtime);
		}
		else // vis == VISIBILITY_INHERIT
		{
                    // To make it visible for all time, just block any
                    // overrides. Otherwise set the attr at the given time.
                    if (usdtime.IsDefault())
                        attr.Block();
                    else
                        attr.Set(UsdGeomTokens->inherited, usdtime);
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

static bool
husdShouldSetExtentsHint(const UsdPrim& prim)
{
    if (UsdGeomModelAPI geommodelapi{prim})
    {
        if (geommodelapi.GetExtentsHintAttr())
            return true;
    }
    else if (!UsdGeomImageable{prim})
        return false;

    // Instance proxy prims should be factored into extents calculations.
    for (auto child : prim.GetFilteredDescendants(
            UsdTraverseInstanceProxies(UsdPrimDefaultPredicate)))
    {
        if (UsdGeomBoundable{child})
            return true;
        if (UsdGeomModelAPI geommodelapi{prim})
            if (geommodelapi.GetExtentsHintAttr())
                return true;
    }
    return false;
}

bool
HUSD_ConfigurePrims::setComputedExtents(const HUSD_FindPrims &findprims,
	const HUSD_TimeCode &timecode,
        Clear clear,
        HUSD_PathSet *overwrite_prims) const
{
    UsdGeomBBoxCache bbox_cache(HUSDgetNonDefaultUsdTimeCode(timecode),
        UsdGeomImageable::GetOrderedPurposeTokens());

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
        bool overwrite = !overwrite_prims ||
            overwrite_prims->contains(prim.GetPath());

	if (UsdGeomBoundable boundable{prim})
        {
            UsdAttribute extentattr = boundable.GetExtentAttr();
            if (extentattr.HasAuthoredValue() && !overwrite)
                return true;

            // Always read extent information from a non-default time.
            VtVec3fArray extent;
            if (!boundable.ComputeExtentFromPlugins(boundable,
                    HUSDgetNonDefaultUsdTimeCode(timecode), &extent))
                return true; // ignore errors

            HUSD_TimeSampling time_sampling =
                HUSDgetValueTimeSampling(extentattr);
            HUSDupdateTimeSampling(myTimeSampling, time_sampling);
            if (clear == CLEAR && extentattr)
                extentattr.Clear();
            boundable.CreateExtentAttr().Set(extent, HUSDgetUsdTimeCode(
                HUSDgetEffectiveTimeCode(timecode, time_sampling)));
        }
        else if (husdShouldSetExtentsHint(prim))
        {
            UsdGeomModelAPI geommodelapi = UsdGeomModelAPI::Apply(prim);
            UT_ASSERT(geommodelapi);

            UsdAttribute extentattr = geommodelapi.GetExtentsHintAttr();
            if (extentattr && !overwrite)
                return true;

            HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;
            VtVec3fArray extent(geommodelapi.ComputeExtentsHint(bbox_cache));
            if (clear == CLEAR)
            {
                time_sampling = HUSDgetBoundsTimeSampling(prim, false);
                HUSDupdateTimeSampling(myTimeSampling, time_sampling);
                if (extentattr)
                    extentattr.Clear();
            }
            else
            {
                // We've already run with CLEAR, so we've already run the
                // more expensive husdGetChildExtentsTimeSampling function
                // to check for time-varying descendants. Now we can just
                // check if the existing extentattr is using time samples
                // (which it will be if the expensive check found time
                // samples).
                time_sampling = HUSDgetValueTimeSampling(extentattr);
            }
            geommodelapi.SetExtentsHint(extent, HUSDgetUsdTimeCode(
                HUSDgetEffectiveTimeCode(timecode, time_sampling)));
        }
        if (overwrite_prims && !overwrite)
            overwrite_prims->insert(prim.GetPath());

	return true;
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

template<typename UtValueType>
bool
HUSD_ConfigurePrims::setAssetInfo(const HUSD_FindPrims &findprims,
        const UT_StringRef &key,
        const UtValueType &value) const
{
    std::string  key_str(key.toStdString());
    VtValue      vt_value = HUSDgetVtValue(value);

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
        {
            UsdModelAPI modelapi(prim);

            if (!modelapi)
                return false;

            VtDictionary asset_info;

            // GetAssetInfo returns false if there is no asset info set.
            modelapi.GetAssetInfo(&asset_info);
            asset_info.SetValueAtPath(key_str, vt_value);
            modelapi.SetAssetInfo(asset_info);

            return true;
        });
}

bool
HUSD_ConfigurePrims::removeAssetInfo(const HUSD_FindPrims &findprims,
        const UT_StringRef &key) const
{
    std::string	 key_str(key.toStdString());

    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
        {
            UsdModelAPI modelapi(prim);

            if (!modelapi)
                return false;

            VtDictionary asset_info;

            // GetAssetInfo returns false if there is no asset info set.
            if (modelapi.GetAssetInfo(&asset_info))
            {
                if (asset_info.GetValueAtPath(key_str))
                {
                    asset_info.EraseValueAtPath(key_str);
                    modelapi.SetAssetInfo(asset_info);
                }
            }

            return true;
        });
}

bool
HUSD_ConfigurePrims::clearAssetInfo(const HUSD_FindPrims &findprims) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
        {
            UsdModelAPI modelapi(prim);

            if (!modelapi)
                return false;

            VtDictionary asset_info;

            if (modelapi.GetAssetInfo(&asset_info))
                modelapi.SetAssetInfo(VtDictionary());

            return true;
        });
}

bool
HUSD_ConfigurePrims::setEditable(const HUSD_FindPrims &findprims,
        bool editable) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
        UsdHoudiniHoudiniEditableAPI editableapi =
            UsdHoudiniHoudiniEditableAPI::Apply(prim);

        editableapi.CreateHoudiniEditableAttr(VtValue(editable));

        return true;
    });
}

bool
HUSD_ConfigurePrims::setSelectable(const HUSD_FindPrims &findprims,
        bool selectable) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
        {
            UsdHoudiniHoudiniSelectableAPI selectableapi =
                UsdHoudiniHoudiniSelectableAPI::Apply(prim);

            selectableapi.CreateHoudiniSelectableAttr(VtValue(selectable));

            return true;
        });
}

bool
HUSD_ConfigurePrims::setHideInUi(const HUSD_FindPrims &findprims,
        bool hide) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
        prim.SetHidden(hide);

        return true;
    });
}

bool
HUSD_ConfigurePrims::addEditorNodeId(const HUSD_FindPrims &findprims,
	int nodeid) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
        HUSDaddPrimEditorNodeId(prim, nodeid);

	return true;
    });
}

bool
HUSD_ConfigurePrims::clearEditorNodeIds(const HUSD_FindPrims &findprims) const
{
    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim)
    {
        HUSDclearPrimEditorNodeIds(prim);

        return true;
    });
}

bool
HUSD_ConfigurePrims::applyAPI(const HUSD_FindPrims &findprims,
    const UT_StringRef &schema) const
{
    return applyAPI(findprims, schema, nullptr);
}

bool
HUSD_ConfigurePrims::applyAPI(const HUSD_FindPrims &findprims,
	const UT_StringRef &schema,
        UT_StringSet *failedapis) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	UsdSchemaRegistry &registry = UsdSchemaRegistry::GetInstance();
	TfToken		 tf_schema(schema.toStdString());
	TfType		 schema_type = registry.GetTypeFromName(tf_schema);

	if (registry.IsAppliedAPISchema(schema_type) &&
            !registry.IsMultipleApplyAPISchema(schema_type))
	{
	    return husdConfigPrim(myWriteLock, findprims, [&](UsdPrim &prim) {
                if (prim.GetTypeName().IsEmpty() ||
                    prim.CanApplyAPI(schema_type))
                    return prim.ApplyAPI(schema_type);

                // Add a warning, unless we have already added this warning.
                UT_WorkBuffer buf;
                buf.format("{} to {}",
                    schema, prim.GetPrimPath().GetAsString());
                if (!failedapis || !failedapis->contains(buf.buffer()))
                {
                    if (failedapis)
                        failedapis->insert(buf.buffer());
                    HUSD_ErrorScope::addWarning(
                        HUSD_ERR_FAILED_TO_APPLY_SCHEMA, buf.buffer());
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

#define HUSD_EXPLICIT_INSTANTIATION(UtType)				\
    template HUSD_API_TINST bool					\
    HUSD_ConfigurePrims::setAssetInfo(					\
	const HUSD_FindPrims	&findprims,				\
	const UT_StringRef	&key,					\
	const UtType		&value) const;				\

// Keep the list of supported data types here synchronized with the list of
// data types in the comment in the header file. Otherwise there is no way to
// know which data types can be used to call these templated functions.
HUSD_EXPLICIT_INSTANTIATION(bool)
HUSD_EXPLICIT_INSTANTIATION(int)
HUSD_EXPLICIT_INSTANTIATION(int64)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector2i)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector3i)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector4i)
HUSD_EXPLICIT_INSTANTIATION(fpreal32)
HUSD_EXPLICIT_INSTANTIATION(fpreal64)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector2F)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector3F)
HUSD_EXPLICIT_INSTANTIATION(UT_Vector4F)
HUSD_EXPLICIT_INSTANTIATION(UT_QuaternionF)
HUSD_EXPLICIT_INSTANTIATION(UT_QuaternionH)
HUSD_EXPLICIT_INSTANTIATION(UT_Matrix3D)
HUSD_EXPLICIT_INSTANTIATION(UT_Matrix4D)
HUSD_EXPLICIT_INSTANTIATION(UT_StringHolder)
HUSD_EXPLICIT_INSTANTIATION(UT_Array<UT_StringHolder>)
HUSD_EXPLICIT_INSTANTIATION(HUSD_AssetPath)
HUSD_EXPLICIT_INSTANTIATION(UT_Array<HUSD_AssetPath>)
HUSD_EXPLICIT_INSTANTIATION(HUSD_Token)
HUSD_EXPLICIT_INSTANTIATION(UT_Array<HUSD_Token>)

#undef HUSD_EXPLICIT_INSTANTIATION
