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

#include "HUSD_Overrides.h"
#include "HUSD_Constants.h"
#include "HUSD_ExpansionState.h"
#include "HUSD_FindPrims.h"
#include "HUSD_GetAttributes.h"
#include "HUSD_PathSet.h"
#include "HUSD_PointPrim.h"
#include "HUSD_TimeCode.h"
#include "HUSD_XformEditor.h"
#include "UsdHoudini/tokens.h"
#include "UsdHoudini/houdiniSelectableAPI.h"
#include "XUSD_OverridesData.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONValueMap.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_WorkBuffer.h>
#include <gusd/UT_Gf.h>
#include <pxr/pxr.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    // Keep these strings aligned with the HUSD_OverridesLayerId enum defined
    // in HUSD_Utils.h.
    const char *HUSD_LAYER_KEYS[HUSD_OVERRIDES_NUM_LAYERS] = {
        "custom",
        "viewportstate",
        "expansion",
        "purpose",
        "sololights",
        "sologeometry",
        "selectable",
        "base"
    };

    void
    addApiSchema(SdfPrimSpecHandle &primspec, const TfToken &schema)
    {
        VtValue listopval = primspec->GetInfo(UsdTokens->apiSchemas);
        SdfTokenListOp listop = listopval.Get<SdfTokenListOp>();
        auto items = listop.GetPrependedItems();
        items.insert(items.begin(), schema);
        listop.SetPrependedItems(items);
        primspec->SetInfo(UsdTokens->apiSchemas, VtValue::Take(listop));
    }

    void
    removeApiSchema(SdfPrimSpecHandle &primspec, const TfToken &schema)
    {
        // If we have a draw mode setting, assume we have also
        // set the UsdGeomModelAPI schema (and only this
        // schema), and remove it by completely clearing
        // the apiSchema listop from this layer.
        VtValue listopval = primspec->GetInfo(UsdTokens->apiSchemas);
        SdfTokenListOp listop = listopval.Get<SdfTokenListOp>();
        auto items = listop.GetPrependedItems();
        auto it = std::find(items.begin(), items.end(), schema);
        if (it != items.end())
        {
            items.erase(it);
            if (items.empty())
                primspec->ClearInfo(UsdTokens->apiSchemas);
            else
            {
                listop.SetPrependedItems(items);
                primspec->SetInfo(UsdTokens->apiSchemas, VtValue::Take(listop));
            }
        }
    }

    SdfAttributeSpecHandle
    getOrCreateAttributeSpec(const SdfPrimSpecHandle &primspec,
        const TfToken &attributename,
        const SdfValueTypeName &attributetype)
    {
        SdfAttributeSpecHandle attributespec =
            primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    attributename));
        if (!attributespec)
            attributespec = SdfAttributeSpec::New(primspec,
                attributename, attributetype);

        return attributespec;
    }

    bool
    getLocalPrimVisibility(const SdfLayerRefPtr &layer,
            const UsdGeomImageable &imageable,
            const UsdTimeCode &usdtime)
    {
        auto primspec = layer->GetPrimAtPath(imageable.GetPath());
        if (primspec)
        {
            SdfAttributeSpecHandle visspec;

            visspec = primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    UsdGeomTokens->visibility));
            if (visspec && visspec->HasDefaultValue())
            {
                VtValue value = visspec->GetDefaultValue();
                if (value.IsHolding<TfToken>())
                    return (value.UncheckedGet<TfToken>() !=
                        UsdGeomTokens->invisible);
            }
        }

        TfToken primvis;
        return (!imageable.GetVisibilityAttr().Get(&primvis, usdtime) ||
                primvis != UsdGeomTokens->invisible);
    }

    void
    setPrimVisibility(const SdfLayerRefPtr &layer,
            const SdfPath &path,
            const TfToken &vistoken)
    {
        SdfPrimSpecHandle primspec;

        primspec = SdfCreatePrimInLayer(layer, path);
        if (primspec)
        {
            SdfAttributeSpecHandle visspec =
                getOrCreateAttributeSpec(primspec,
                    UsdGeomTokens->visibility,
                    SdfValueTypeNames->Token);
            if (visspec)
                visspec->SetDefaultValue(VtValue(vistoken));
        }
    }

    void
    removePrimVisibility(const SdfLayerRefPtr &layer,
            const SdfPath &path)
    {
        SdfPrimSpecHandle	 primspec;

        primspec = layer->GetPrimAtPath(path);
        if (primspec)
        {
            SdfAttributeSpecHandle	 visspec;

            visspec = primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    UsdGeomTokens->visibility));
            if (visspec)
            {
                primspec->RemoveProperty(visspec);
                layer->RemovePrimIfInert(primspec);
            }
        }
    }

    void
    setAncestorsVisible(const SdfLayerRefPtr &layer,
            const UsdPrim &prim,
            const UsdTimeCode &usdtime,
            const SdfPathSet &setting_visible_paths,
            bool &has_invisible_ancestor)
    {
        // This function is basically the same as UsdGeomImageable::MakeVisible.
        // The difference is basically that we use Sdf APIs to set the visibility
        // opinions into a specific layer.
        UsdPrim parent(prim.GetParent());

        if (parent)
        {
            UsdGeomImageable parentimageable(parent);

            setAncestorsVisible(layer, parent, usdtime,
                setting_visible_paths, has_invisible_ancestor);
            if (parentimageable)
            {
                // If the prim (or any ancestor) is marked invisible, we need to
                // mark this prim visible and all siblings of the passed in prim
                // as invisible.
                bool parent_marked_invisible =
                    !getLocalPrimVisibility(layer, parentimageable, usdtime);
                if (parent_marked_invisible || has_invisible_ancestor)
                {
                    has_invisible_ancestor = true;
                    if (parent_marked_invisible)
                        setPrimVisibility(layer, parent.GetPath(),
                            UsdGeomTokens->inherited);
                    else
                        removePrimVisibility(layer, parent.GetPath());
                    for (auto &&child : parent.GetChildren())
                    {
                        // Don't mark a sibling invisible if it is the specific
                        // child we are interested in, or if it is part of the
                        // set of prims that we are explicitly making visible.
                        if (child.GetPath() != prim.GetPath() &&
                            setting_visible_paths.find(child.GetPath()) ==
                                setting_visible_paths.end())
                            setPrimVisibility(layer, child.GetPath(),
                                UsdGeomTokens->invisible);
                    }
                }
            }
        }
    }

    void
    setAncestorsInvisible(const SdfLayerRefPtr &layer,
            const UsdPrim &prim,
            const UsdTimeCode &usdtime)
    {
        UsdPrim parent = prim.GetParent();
        UsdGeomImageable parentimageable(parent);
        bool found_visible_sibling = false;

        if (parentimageable)
        {
            for (auto &&sibling : parent.GetChildren())
            {
                UsdGeomImageable imageable(sibling);

                if (imageable &&
                    getLocalPrimVisibility(layer, imageable, usdtime))
                {
                    found_visible_sibling = true;
                    break;
                }
            }

            if (!found_visible_sibling)
            {
                setPrimVisibility(layer, parent.GetPath(),
                    UsdGeomTokens->invisible);

                for (auto &&sibling : parent.GetChildren())
                {
                    SdfPath path = sibling.GetPath();
                    SdfPrimSpecHandle prim = layer->GetPrimAtPath(path);
                    if (prim)
                        prim->GetNameParent()->RemoveNameChild(prim);
                }

                setAncestorsInvisible(layer, parent, usdtime);
            }
        }
    }

    std::pair<bool, bool>
    getExpansionInfo(const UsdPrim &prim,
            const HUSD_ExpansionState &expansionstate)
    {
        bool apply_expanded_effect = false;
        bool expanded = false;
        bool prim_is_root = prim.IsPseudoRoot();
        HUSD_Path path(prim.GetPath());
        const HUSD_PathSet &expandedpaths =
            expansionstate.expandedScenePaths();
        const HUSD_PathSet &lockedpaths =
            expansionstate.lockedScenePaths();
        const HUSD_PathSet &lockedexpandedpaths =
            expansionstate.lockedExpandedScenePaths();

        if (prim_is_root)
        {
            // Always treat the root prim as expanded. We can't set visibility
            // at that level so we have to at least look at the root prims.
            apply_expanded_effect = true;
            expanded = prim_is_root;
        }
        else if (prim.IsInstanceProxy())
        {
            // We can't change instance proxies. Just fall through.
        }
        else
        {
            bool is_group = prim.IsGroup();

            // Note that we can be both in a locked area, and also above
            // another sub-locked area.
            if (is_group && lockedpaths.containsDescendant(path))
            {
                // A descendant is locked, so we need to act expanded
                // so that the locked states can be applied rather than
                // overridden at this ancestor level.
                apply_expanded_effect = true;
                expanded = true;
            }
            else if (lockedpaths.containsPathOrAncestor(path))
            {
                if (is_group)
                {
                    // This group is in a "locked" area with no locked
                    // sub-areas. We just need to look at the expansion
                    // state stored in the "locked" path set, and ignore
                    // the actual current expansion state.
                    apply_expanded_effect = true;
                    expanded = lockedexpandedpaths.contains(path);
                }
                else if (prim.IsComponent())
                {
                    if (lockedpaths.contains(path))
                    {
                        // A component that is explicitly locked should be
                        // hidden or displayed based on whether it is locked
                        // expanded or collapsed.
                        apply_expanded_effect = true;
                        expanded = lockedexpandedpaths.contains(path);
                    }
                    else if (!lockedexpandedpaths.contains(
                        prim.GetParent().GetPath()))
                    {
                        // A component inside a locked area where the parent
                        // is not locked expanded should act like it is locked
                        // collapsed (i.e. act like its
                        apply_expanded_effect = true;
                        expanded = false;
                    }
                }
            }
            else
            {
                bool is_expanded = expandedpaths.contains(path);
                bool all_ancestors_expanded = true;

                // If there are no locked expansion states, then the fact that
                // we are here proves we are expanded to the root. Otherwise
                // we need to check all the way up because the expanded set
                // can contain "gaps". We don't want to treat an expanded prim
                // under a "pretending to be expanded because of a locked
                // sibling" as expanded, because an ancestor is collapsed so
                // this prim should be treated as collapsed.
                if (!lockedpaths.empty())
                {
                    HUSD_Path testpath(path.parentPath());
                    while (testpath != HUSD_Path::theRootPrimPath)
                    {
                        if (!expandedpaths.contains(testpath))
                        {
                            all_ancestors_expanded = false;
                            break;
                        }
                        testpath = testpath.parentPath();
                    }
                }

                if (all_ancestors_expanded)
                {
                    // The prim is explicitly expanded from the root all the
                    // way down to this prim (and recall we are not is a
                    // locked area, so we want to respect the expansion).
                    if (prim.IsGroup())
                    {
                        // This (non-instance proxy group kind) prim can be
                        // affected by the expansion state.
                        apply_expanded_effect = true;
                        expanded = is_expanded;
                    }
                }
                else
                {
                    // The prim isn't the root.
                    // The prim isn't in a locked area.
                    // The tree isn't fully expanded down to this prim.
                    // None of our descendants are in the locked set.
                    //
                    // So why are we here? The only possible reason is that
                    // our parent is expanded due to it having a descendant
                    // (on a sibling branch) that is in the locked set. So
                    // we need to have our expansion effect applied, but
                    // we are not expanded. One special case here is that
                    // we want to apply this "collapsed" effect not just
                    // to group kinds, but also component kinds. Otherwise
                    // we'll see full geometry for components that are
                    // not really visible, but simply siblings of branches
                    // are expanded in the "locked" set.
                    if (prim.IsModel())
                    {
                        apply_expanded_effect = true;
                        expanded = false;
                    }
                }
            }
        }

        return std::make_pair(apply_expanded_effect, expanded);
    }

    bool
    setDrawModeFromExpansionState(const UsdPrim &prim,
            const SdfLayerRefPtr &layer,
            const HUSD_ExpansionState &expansionstate,
            UsdGeomBBoxCache &bboxcache)
    {
        bool changed = false;

        if (!prim)
            return changed;

        SdfPrimSpecHandle primspec(layer->GetPrimAtPath(prim.GetPath()));
        SdfAttributeSpecHandle applydrawmodespec;
        SdfAttributeSpecHandle drawmodespec;
        SdfAttributeSpecHandle extentshintspec;
        bool apply_expanded_effect = false;
        bool expanded = false;

        if (primspec)
        {
            drawmodespec = primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    UsdGeomTokens->modelDrawMode));
            applydrawmodespec = primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    UsdGeomTokens->modelApplyDrawMode));
            extentshintspec = primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    UsdGeomTokens->extentsHint));
        }

        std::tie(apply_expanded_effect, expanded) =
            getExpansionInfo(prim, expansionstate);
        if (apply_expanded_effect)
        {
            if (expanded)
            {
                // This group prim is expanded, so we don't want to set
                // it to draw in bounds mode.
                if (primspec)
                {
                    if (drawmodespec || applydrawmodespec || extentshintspec)
                    {
                        // We used to remove the UsdGeomModelAPI schema here
                        // as well, which let us remove this prim as inert.
                        // But around USD 26.03 this caused hydra to not
                        // un-apply the draw mode. So we have to leave the
                        // API schema sitting here (forever?).
                        if (applydrawmodespec)
                            primspec->RemoveProperty(applydrawmodespec);
                        if (drawmodespec)
                            primspec->RemoveProperty(drawmodespec);
                        if (extentshintspec)
                            primspec->RemoveProperty(extentshintspec);
                        layer->RemovePrimIfInert(primspec);
                        changed = true;
                    }
                }
                // This prim is expanded but its children may not be.
                // Continue the traversal.
                for (auto &&child : prim.GetChildren())
                    if (setDrawModeFromExpansionState(
                            child, layer, expansionstate, bboxcache))
                        changed = true;
            }
            else
            {
                // This group prim is collapsed. Set it to draw in
                // bounds mode. Do not traverse into this prim's
                // children since we don't care if the children are
                // expanded. We don't even care to clear the draw mode
                // overrides on the children.
                if (!primspec)
                    primspec = SdfCreatePrimInLayer(layer, prim.GetPath());
                if (primspec)
                {
                    if (!drawmodespec || !applydrawmodespec)
                    {
                        addApiSchema(primspec,
                            UsdSchemaRegistry::GetSchemaTypeName(
                                TfType::Find<UsdGeomModelAPI>()));
                        drawmodespec = getOrCreateAttributeSpec(primspec,
                            UsdGeomTokens->modelDrawMode,
                            SdfValueTypeNames->Token);
                        applydrawmodespec = getOrCreateAttributeSpec(primspec,
                            UsdGeomTokens->modelApplyDrawMode,
                            SdfValueTypeNames->Bool);
                        drawmodespec->SetDefaultValue(
                            VtValue(UsdGeomTokens->bounds));
                        applydrawmodespec->SetDefaultValue(
                            VtValue(true));

                        // Author an extentsHint if one doesn't already exist
                        // on this prim.
                        UsdGeomModelAPI modelapi(prim);
                        if (!modelapi ||
                            !modelapi.GetExtentsHintAttr().HasAuthoredValue())
                        {
                            extentshintspec = getOrCreateAttributeSpec(primspec,
                                UsdGeomTokens->extentsHint,
                                SdfValueTypeNames->Float3Array);
                            if (extentshintspec)
                            {
                                VtVec3fArray extentsHint =
                                    modelapi.ComputeExtentsHint(bboxcache);
                                extentshintspec->SetDefaultValue(
                                    VtValue(extentsHint));
                            }
                        }
                        changed = true;
                    }
                }
            }
        }
        else
        {
            // Prim is not a group. Neither this prim nor any descendant
            // should get a draw mode override. Clear the override layer
            // starting at this prim, and don't traverse into child prims
            // on the stage.
            if (primspec)
            {
                if (primspec->GetNameParent())
                    primspec->GetNameParent()->RemoveNameChild(primspec);
                else
                    layer->RemoveRootPrim(primspec);
                changed = true;
            }
        }

        return changed;
    }

    bool
    setVisibilityFromExpansionState(const UsdPrim &prim,
            const SdfLayerRefPtr &layer,
            const HUSD_ExpansionState &expansionstate)
    {
        bool changed = false;

        if (!prim)
            return changed;

        SdfPrimSpecHandle primspec(layer->GetPrimAtPath(prim.GetPath()));
        SdfAttributeSpecHandle visibilityspec;
        bool apply_expanded_effect = false;
        bool expanded = false;

        if (primspec)
        {
            visibilityspec = primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    UsdGeomTokens->visibility));
        }

        std::tie(apply_expanded_effect, expanded) =
            getExpansionInfo(prim, expansionstate);
        if (apply_expanded_effect)
        {
            if (expanded)
            {
                // This group prim is expanded, so we don't want to set
                // it to make it invisible.
                if (primspec)
                {
                    if (visibilityspec)
                    {
                        primspec->RemoveProperty(visibilityspec);
                        layer->RemovePrimIfInert(primspec);
                        changed = true;
                    }
                }
                // This prim is expanded but its children may not be.
                // Continue the traversal.
                for (auto &&child : prim.GetChildren())
                    if (setVisibilityFromExpansionState(
                            child, layer, expansionstate))
                        changed = true;
            }
            else
            {
                // This group prim is collapsed. Make it invisible. Do not
                // traverse into this prim's children since we don't care
                // if the children are expanded. We don't even care to clear
                // the visibility overrides on the children.
                if (!primspec)
                    primspec = SdfCreatePrimInLayer(layer, prim.GetPath());
                if (primspec)
                {
                    if (!visibilityspec)
                    {
                        addApiSchema(primspec,
                            UsdSchemaRegistry::GetSchemaTypeName(
                                TfType::Find<UsdGeomModelAPI>()));
                        visibilityspec = getOrCreateAttributeSpec(primspec,
                            UsdGeomTokens->visibility,
                            SdfValueTypeNames->Token);
                        visibilityspec->SetDefaultValue(
                            VtValue(UsdGeomTokens->invisible));
                        changed = true;
                    }
                }
            }
        }
        else
        {
            // Prim is not a group. Neither this prim nor any descendant
            // should be made invisible. Clear the override layer
            // starting at this prim, and don't traverse into child prims
            // on the stage.
            if (primspec)
            {
                if (primspec->GetNameParent())
                    primspec->GetNameParent()->RemoveNameChild(primspec);
                else
                    layer->RemoveRootPrim(primspec);
                changed = true;
            }
        }

        return changed;
    }
}

HUSD_Overrides::HUSD_Overrides()
    : myData(new XUSD_OverridesData()),
      myVersionId(0),
      myActiveLayerId(HUSD_OVERRIDES_CUSTOM_LAYER)
{
}

HUSD_Overrides::~HUSD_Overrides()
{
}

bool
HUSD_Overrides::getDrawModeOverrides(const UT_StringRef &primpath,
        UT_StringMap<UT_StringHolder> &overrides) const
{
    bool                 found_override = false;
    auto                 path = HUSDgetSdfPath(primpath);
    auto                 layer = myData->layer(HUSD_OVERRIDES_BASE_LAYER);

    while (!path.IsEmpty() && path != SdfPath::AbsoluteRootPath())
    {
        SdfPrimSpecHandle        primspec = layer->GetPrimAtPath(path);

        if (primspec)
        {
            SdfAttributeSpecHandle   drawmodespec;

            drawmodespec = primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    UsdGeomTokens->modelDrawMode));
            if (drawmodespec)
            {
                VtValue              value = drawmodespec->GetDefaultValue();
                
                if (value.IsHolding<TfToken>())
                {
                    TfToken          token = value.Get<TfToken>();

                    overrides.emplace(primspec->GetPath().GetText(),
                        token.GetText());
                    found_override = true;

                    // We can stop when we hit the first override, regardless
                    // of the value.
                    break;
                }
            }
        }
        path = path.GetParentPath();
    }

    return found_override;
}

bool
HUSD_Overrides::setDrawMode(HUSD_AutoWriteOverridesLock &lock,
	const HUSD_FindPrims &prims,
	const UT_StringRef &drawmode)
{
    auto	 indata = lock.constData();

    myVersionId++;
    if (indata && indata->isStageValid())
    {
	auto	 stage = indata->stage();
	auto	 pathset = prims.getExpandedPathSet();
	auto	 layer = myData->layer(HUSD_OVERRIDES_BASE_LAYER);

	{
	    // Run through and delete the draw mode override currently set on
	    // any prims we have been asked to change.
	    SdfChangeBlock	 changeblock;

	    for (auto &&path : pathset.sdfPathSet())
	    {
		SdfPrimSpecHandle	 primspec;

		primspec = layer->GetPrimAtPath(path);
		if (primspec)
		{
		    bool                         attempt_removal = false;
		    SdfAttributeSpecHandle	 drawmodespec;
		    SdfAttributeSpecHandle	 applydrawmodespec;

		    drawmodespec = primspec->GetAttributeAtPath(
			SdfPath::ReflexiveRelativePath().AppendProperty(
			    UsdGeomTokens->modelDrawMode));
		    applydrawmodespec = primspec->GetAttributeAtPath(
                        SdfPath::ReflexiveRelativePath().AppendProperty(
                            UsdGeomTokens->modelApplyDrawMode));
		    if (drawmodespec || applydrawmodespec)
		    {
		        // We used to remove the UsdGeomModelAPI schema here
		        // as well, which let us remove this prim as inert.
		        // But around USD 26.03 this caused hydra to not
		        // un-apply the draw mode. So we have to leave the
		        // API schema sitting here (forever?).
		        if (drawmodespec)
			    primspec->RemoveProperty(drawmodespec);
		        if (applydrawmodespec)
		            primspec->RemoveProperty(applydrawmodespec);
		        attempt_removal = true;
		    }

		    SdfAttributeSpecHandle	 extentshintspec;

		    extentshintspec = primspec->GetAttributeAtPath(
                        SdfPath::ReflexiveRelativePath().AppendProperty(
                            UsdGeomTokens->extentsHint));
		    if (extentshintspec)
		    {
		        primspec->RemoveProperty(extentshintspec);
		        attempt_removal = true;
		    }

		    if (attempt_removal)
			layer->RemovePrimIfInert(primspec);
		}
	    }
	}

	{
	    // As a second pass, check the current stage value against the
	    // requested value, and create an override if required.
	    SdfChangeBlock	 changeblock;
	    TfToken		 drawmodetoken(drawmode.toStdString());
	    UsdGeomBBoxCache     bboxcache(UsdTimeCode::EarliestTime(),
                                    UsdGeomImageable::GetOrderedPurposeTokens(),
                                    true);

	    for (auto &&path : pathset.sdfPathSet())
	    {
		UsdPrim		 prim(stage->GetPrimAtPath(path));

		if (prim && !prim.IsPseudoRoot() && prim.IsModel())
		{
		    UsdGeomModelAPI	 modelapi(prim);

		    if (modelapi.ComputeModelDrawMode() != drawmodetoken)
		    {
			SdfPrimSpecHandle	 primspec;

			primspec = SdfCreatePrimInLayer(layer, path);
			if (primspec)
			{
			    SdfAttributeSpecHandle	 drawmodespec;
			    SdfAttributeSpecHandle	 applydrawmodespec;

			    drawmodespec = getOrCreateAttributeSpec(primspec,
				UsdGeomTokens->modelDrawMode,
				SdfValueTypeNames->Token);
			    applydrawmodespec = getOrCreateAttributeSpec(primspec,
                                UsdGeomTokens->modelApplyDrawMode,
                                SdfValueTypeNames->Bool);
			    if (drawmodespec && applydrawmodespec)
                            {
                                addApiSchema(primspec,
                                    UsdSchemaRegistry::GetSchemaTypeName(
                                        TfType::Find<UsdGeomModelAPI>()));
                                drawmodespec->SetDefaultValue(
                                    VtValue(drawmodetoken));
			        applydrawmodespec->SetDefaultValue(
                                    VtValue(true));
                            }

			    // Author an extentsHint if one doesn't already exist
			    // on this prim.
			    if (!modelapi.GetExtentsHintAttr().HasAuthoredValue())
			    {
			        SdfAttributeSpecHandle	 extentshintspec;

			        extentshintspec = getOrCreateAttributeSpec(primspec,
                                    UsdGeomTokens->extentsHint,
                                    SdfValueTypeNames->Float3Array);
			        if (extentshintspec)
			        {
			            VtVec3fArray extentsHint =
                                        modelapi.ComputeExtentsHint(bboxcache);
			            extentshintspec->SetDefaultValue(
                                        VtValue(extentsHint));
			        }
			    }
			}
		    }
		}
	    }
	}
    }

    return true;
}

bool
HUSD_Overrides::getActiveOverrides(const UT_StringRef &primpath,
        UT_StringMap<bool> &overrides) const
{
    bool                 found_override = false;
    auto                 path = HUSDgetSdfPath(primpath);
    auto                 layer = myData->layer(HUSD_OVERRIDES_BASE_LAYER);

    while (!path.IsEmpty() && path != SdfPath::AbsoluteRootPath())
    {
        SdfPrimSpecHandle        primspec = layer->GetPrimAtPath(path);

        if (primspec)
        {
            bool active = primspec->GetActive();
            overrides.emplace(primspec->GetPath().GetText(), active);
            found_override = true;

            // We can stop when we hit the first override marking this
            // prim or an ancestor as inactive.
            if (!active)
                break;
        }
        path = path.GetParentPath();
    }

    return found_override;
}

bool
HUSD_Overrides::setActive(HUSD_AutoWriteOverridesLock &lock,
	const HUSD_FindPrims &prims, bool active)
{
    auto	 indata = lock.constData();

    myVersionId++;
    if (indata && indata->isStageValid())
    {
	auto	 stage = indata->stage();
	auto	 pathset = prims.getExpandedPathSet();
	auto	 layer = myData->layer(HUSD_OVERRIDES_BASE_LAYER);

	{
	    // Run through and delete the "active" override currently set on
	    // any prims we have been asked to change.
	    SdfChangeBlock	 changeblock;

	    for (auto &&path : pathset.sdfPathSet())
	    {
		SdfPrimSpecHandle	 primspec;

		primspec = layer->GetPrimAtPath(path);
		if (primspec)
		{
		    primspec->ClearActive();
		    layer->RemovePrimIfInert(primspec);
		}
	    }
	}
	{
	    // As a second pass, check the current stage value against the
	    // requested value, and create an override if required.
	    SdfChangeBlock	 changeblock;

	    for (auto &&path : pathset.sdfPathSet())
	    {
		UsdPrim			 prim(stage->GetPrimAtPath(path));

		if (prim && (prim.IsActive() != active))
		{
		    SdfPrimSpecHandle	 primspec;

		    primspec = SdfCreatePrimInLayer(layer, path);
		    if (primspec)
			primspec->SetActive(active);
		}
	    }
	}
    }

    return true;
}

bool
HUSD_Overrides::getVisibleOverrides(const UT_StringRef &primpath,
        UT_StringMap<UT_StringHolder> &overrides) const
{
    bool                 found_override = false;
    auto                 path = HUSDgetSdfPath(primpath);
    auto                 layer = myData->layer(HUSD_OVERRIDES_BASE_LAYER);

    while (!path.IsEmpty() && path != SdfPath::AbsoluteRootPath())
    {
        SdfPrimSpecHandle        primspec = layer->GetPrimAtPath(path);

        if (primspec)
        {
            SdfAttributeSpecHandle   visspec;

            visspec = primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    UsdGeomTokens->visibility));
            if (visspec)
            {
                VtValue              value = visspec->GetDefaultValue();
                
                if (value.IsHolding<TfToken>())
                {
                    TfToken          token = value.Get<TfToken>();

                    overrides.emplace(primspec->GetPath().GetText(),
                        token.GetText());
                    found_override = true;

                    // We can stop when we hit the first override marking this
                    // prim or an ancestor as invisible.
                    if (token == UsdGeomTokens->invisible)
                        break;
                }
            }
        }
        path = path.GetParentPath();
    }

    return found_override;
}

bool
HUSD_Overrides::setVisible(HUSD_AutoWriteOverridesLock &lock,
	const HUSD_FindPrims &prims,
	const HUSD_TimeCode &timecode,
	bool visible)
{
    auto	 indata = lock.constData();

    myVersionId++;
    if (indata && indata->isStageValid())
    {
	auto	 stage = indata->stage();
	auto	 pathset = prims.getExpandedPathSet();
	auto	 layer = myData->layer(HUSD_OVERRIDES_BASE_LAYER);

	{
	    // Run through and delete the "visible" override currently set on
	    // any prims we have been asked to change.
	    SdfChangeBlock	 changeblock;

	    for (auto &&path : pathset.sdfPathSet())
                removePrimVisibility(layer, path);
	}

	{
	    // As a second pass, check the current stage value against the
	    // requested value, and create an override if required. Because
	    // visibility is an animatable attribute, the best we can do is
	    // set the default value.
	    SdfChangeBlock	 changeblock;
	    TfToken		 vistoken(visible
				    ? UsdGeomTokens->inherited
				    : UsdGeomTokens->invisible);
	    UsdTimeCode		 usdtime(
				    HUSDgetNonDefaultUsdTimeCode(timecode));

	    for (auto &&path : pathset.sdfPathSet())
	    {
                UsdPrim                  prim(stage->GetPrimAtPath(path));
		UsdGeomImageable	 imageable(prim);

		if (imageable &&
                    imageable.ComputeVisibility(usdtime) != vistoken)
		{
                    TfToken primvis;

                    // Author an opinion on the target prim to make it
                    // invisible, or to mark it as visible if it is currently
                    // explicitly marked as invisible. If it isn't marked as
                    // invisible, it must be invisible because of an ancestor.
                    if (!visible ||
                        (imageable.GetVisibilityAttr() &&
                         imageable.GetVisibilityAttr().Get(&primvis, usdtime) &&
                         primvis == UsdGeomTokens->invisible))
                        setPrimVisibility(layer, path, vistoken);
		}

                // Whether we just marked the target prim visible or not,
                // we now need to make sure that none of our ancestors are
                // marked as invisible. If they are, mark them visible,
                // and set all their children (except the path to the
                // target) as invisible. Going in reverse, if all our siblings
                // are marked invisible, clear these opinions and mark the
                // parent prim invisible.
                if (visible)
                {
                    bool has_invisible_ancestor = false;
                    setAncestorsVisible(layer, prim.GetPrim(),
                        usdtime, pathset.sdfPathSet(), has_invisible_ancestor);
                }
                else
                {
                    setAncestorsInvisible(layer, prim, usdtime);
                }
	    }
	}
    }

    return true;
}

bool
HUSD_Overrides::getSelectableOverrides(const UT_StringRef &primpath,
        UT_StringMap<bool> &overrides) const
{
    bool                 found_override = false;
    auto                 path = HUSDgetSdfPath(primpath);
    auto                 layer = myData->layer(HUSD_OVERRIDES_SELECTABLE_LAYER);

    while (!path.IsEmpty() && path != SdfPath::AbsoluteRootPath())
    {
        SdfPrimSpecHandle        primspec = layer->GetPrimAtPath(path);

        if (primspec)
        {
            SdfAttributeSpecHandle   selspec;

            selspec = primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    UsdHoudiniTokens->houdiniSelectable));
            if (selspec)
            {
                VtValue              value = selspec->GetDefaultValue();

                if (value.IsHolding<bool>())
                {
                    bool             selectable = value.Get<bool>();

                    overrides.emplace(primspec->GetPath().GetText(),
                        selectable);
                    found_override = true;

                    // We can stop when we hit the first explicit override,
                    // since no values further up the hierarchy matter.
                    break;
                }
            }
        }
        path = path.GetParentPath();
    }

    return found_override;
}

bool
HUSD_Overrides::setSelectable(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_FindPrims &prims,
        bool selectable,
        bool solo)
{
    auto	 indata = lock.constData();

    myVersionId++;
    if (indata && indata->isStageValid())
    {
        auto	 stage = indata->stage();
        auto	 pathset = prims.getExpandedPathSet();
        auto	 layer = myData->layer(HUSD_OVERRIDES_SELECTABLE_LAYER);

        if (solo)
        {
            // Delete all existing selectable opinions.
            layer->Clear();
        }
        else
        {
            SdfChangeBlock	 changeblock;

            // Run through and delete the "active" override currently set on
            // any prims we have been asked to change.
            for (auto &&path : pathset.sdfPathSet())
            {
                SdfPrimSpecHandle	 primspec;

                primspec = layer->GetPrimAtPath(path);
                if (primspec)
                {
                    SdfAttributeSpecHandle	 selspec;

                    selspec = primspec->GetAttributeAtPath(
                        SdfPath::ReflexiveRelativePath().AppendProperty(
                            UsdHoudiniTokens->houdiniSelectable));
                    if (selspec)
                    {
                        removeApiSchema(primspec,
                            UsdSchemaRegistry::GetSchemaTypeName(TfType::
                                Find<UsdHoudiniHoudiniSelectableAPI>()));
                        primspec->RemoveProperty(selspec);
                        layer->RemovePrimIfInert(primspec);
                    }
                }
            }
        }

        {
            auto addOpinion = [](const SdfLayerRefPtr &layer,
                                  const SdfPath &path,
                                  bool selectable)
            {
                SdfPrimSpecHandle	 primspec;

                primspec = SdfCreatePrimInLayer(layer, path);
                if (primspec)
                {
                    SdfAttributeSpecHandle	 selspec;

                    selspec = getOrCreateAttributeSpec(primspec,
                        UsdHoudiniTokens->houdiniSelectable,
                        SdfValueTypeNames->Bool);
                    if (selspec)
                    {
                        addApiSchema(primspec,
                            UsdSchemaRegistry::GetSchemaTypeName(TfType::
                                    Find<UsdHoudiniHoudiniSelectableAPI>()));
                        selspec->SetDefaultValue(VtValue(selectable));
                    }
                }
            };
            SdfChangeBlock	 changeblock;

            // If we are soloing, start by marking all root primitives as
            // having the opposite of the selectable state requested for these
            // specific primitives.
            if (solo)
            {
                for (auto &&prim : stage->GetPseudoRoot().GetAllChildren())
                {
                    addOpinion(layer, prim.GetPrimPath(), !selectable);
                }
            }

            // Check the current stage value against the requested value,
            // and create an override if required. If we are soloing, always
            // create the explicit opinion.
            for (auto &&path : pathset.sdfPathSet())
            {
                UsdPrim		 prim(stage->GetPrimAtPath(path));

                if (prim && (solo || HUSDisPrimSelectable(prim) != selectable))
                    addOpinion(layer, path, selectable);
            }
        }
    }

    return true;
}

bool
HUSD_Overrides::clearSelectable(HUSD_AutoWriteOverridesLock &lock)
{
    auto	 indata = lock.constData();

     myVersionId++;
    if (indata && indata->isStageValid())
    {
        auto layer = myData->layer(HUSD_OVERRIDES_SELECTABLE_LAYER);

        layer->Clear();
    }

    return true;
}

bool
HUSD_Overrides::setSoloLights(HUSD_AutoWriteOverridesLock &lock,
	const HUSD_FindPrims &prims)
{
    SdfChangeBlock changeblock;
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_LIGHTS_LAYER);
    const XUSD_PathSet &sololights = prims.getExpandedPathSet().sdfPathSet();

    myVersionId++;
    layer->Clear();
    // Preserve the expanded list of soloed paths, without any modifiction.
    // Just the exact paths specified by the user.
    HUSDsetSoloLightPaths(layer, prims.getExpandedPathSet());

    // If no primitives are in the solo list, turn off soloing.
    if (!sololights.empty())
    {
        HUSD_FindPrims       alllights(lock, prims.traversalDemands());
        UT_WorkBuffer        pattern;

        pattern.sprintf("%%type:%s",
            HUSD_Constants::getLuxLightAPIName().c_str());
        alllights.addPattern(pattern.buffer(),
            OP_INVALID_NODE_ID,
            HUSD_TimeCode());

        // Activate or deactivate each light depending on whether or not it is
        // in the user-specified set (including any descendants). We must do
        // the explicit activation in case some of these lights are deactivated
        // in the base layer, or they are references to prims in the anti-set
        // and thus will be deactivated by this loop.
	for (auto &&path : alllights.getExpandedPathSet().sdfPathSet())
	{
	    SdfPrimSpecHandle	 primspec;

	    primspec = SdfCreatePrimInLayer(layer, path);
	    if (primspec)
            {
                if (sololights.containsPathOrAncestor(path))
                    primspec->SetActive(true);
                else
                    primspec->SetActive(false);
            }
	}
    }

    return true;
}

bool
HUSD_Overrides::addSoloLights(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_FindPrims &prims)
{
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_LIGHTS_LAYER);
    HUSD_PathSet paths;

    HUSDgetSoloLightPaths(layer, paths);
    paths.insert(prims.getExpandedPathSet());

    return setSoloLights(lock, HUSD_FindPrims(lock, paths));
}

bool
HUSD_Overrides::removeSoloLights(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_FindPrims &prims)
{
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_LIGHTS_LAYER);
    HUSD_PathSet paths;

    HUSDgetSoloLightPaths(layer, paths);
    paths.erase(prims.getExpandedPathSet());

    return setSoloLights(lock, HUSD_FindPrims(lock, paths));
}

bool
HUSD_Overrides::getSoloLights(HUSD_PathSet &paths) const
{
    HUSDgetSoloLightPaths(
        myData->layer(HUSD_OVERRIDES_SOLO_LIGHTS_LAYER), paths);

    return (paths.size() > 0);
}

bool
HUSD_Overrides::setSoloGeometry(HUSD_AutoWriteOverridesLock &lock,
	const HUSD_FindPrims &prims)
{
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER);
    SdfChangeBlock changeblock;

    myVersionId++;
    layer->Clear();
    // Preserve the expanded list of soloed paths, without any modification.
    // Just the exact paths specified by the user.
    HUSDsetSoloGeometryPaths(layer, prims.getExpandedPathSet());

    // If no primitives are in the solo list, turn off soloing.
    if (!prims.getExpandedPathSet().empty())
    {
        HUSD_FindPrims       sologeo(lock, prims.getExpandedPathSet(),
                                prims.traversalDemands());
        HUSD_FindPrims       allgeo(lock, prims.traversalDemands());
        UT_WorkBuffer        pattern;

        // We have to add all ancestors and descendants to the set of solo
        // prims to ensure that inherited visibility is set all the way down to
        // any explicitly solo'ed prims, and their children. This is in case
        // any ancestors are marked as invisible on some other layer.
        sologeo.addDescendants();
        sologeo.addAncestors();
        pattern.sprintf("%%type(%s) - %%type(%s)",
            HUSD_Constants::getGeomBoundablePrimType().c_str(),
            HUSD_Constants::getLuxLightAPIName().c_str());
        allgeo.addPattern(pattern.buffer(),
            OP_INVALID_NODE_ID,
            HUSD_TimeCode());

        // Mark each geometry primitives visibliity depending on whether or not
        // it is in the user-specified set (including any descendants). We must
        // set visiblity explicitly in case some of these primitives are
        // invisible in the base layer, or they are references to prims in the
        // anti-set and thus will be made invisible by this loop.
        const XUSD_PathSet &sologeoset =
            sologeo.getExpandedPathSet().sdfPathSet();
        XUSD_PathSet invisibleset;

	for (auto &&path : allgeo.getExpandedPathSet().sdfPathSet())
	{
            if (sologeoset.contains(path))
            {
                SdfPrimSpecHandle	 primspec;

                primspec = SdfCreatePrimInLayer(layer, path);
                if (primspec)
                {
                    SdfAttributeSpecHandle	 visspec;

                    visspec = getOrCreateAttributeSpec(primspec,
                        UsdGeomTokens->visibility,
                        SdfValueTypeNames->Token);
                    if (visspec)
                        visspec->SetDefaultValue(
                            VtValue(UsdGeomTokens->inherited));
                }
            }
            else
                invisibleset.emplace(path);
	}

        // The invisibleset is likely to be very large, so we want to minimize
        // it to reduce the number of edits to the stage.
        if (lock.data() && lock.data()->isStageValid())
            HUSDgetMinimalPathsForInheritableProperty(
                false, lock.data()->stage(), invisibleset);
	for (auto &&path : invisibleset)
	{
	    SdfPrimSpecHandle	 primspec;

	    primspec = SdfCreatePrimInLayer(layer, path);
	    if (primspec)
            {
                SdfAttributeSpecHandle	 visspec;

                visspec = getOrCreateAttributeSpec(primspec,
                    UsdGeomTokens->visibility,
                    SdfValueTypeNames->Token);
                if (visspec)
                    visspec->SetDefaultValue(
                        VtValue(UsdGeomTokens->invisible));
            }
	}
    }

    return true;
}

bool
HUSD_Overrides::addSoloGeometry(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_FindPrims &prims)
{
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER);
    HUSD_PathSet paths;

    HUSDgetSoloGeometryPaths(layer, paths);
    paths.insert(prims.getExpandedPathSet());

    return setSoloGeometry(lock, HUSD_FindPrims(lock, paths));
}

bool
HUSD_Overrides::removeSoloGeometry(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_FindPrims &prims)
{
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER);
    HUSD_PathSet paths;

    HUSDgetSoloGeometryPaths(layer, paths);
    paths.erase(prims.getExpandedPathSet());

    return setSoloGeometry(lock, HUSD_FindPrims(lock, paths));
}

bool
HUSD_Overrides::setDisplayOpacity(HUSD_AutoWriteOverridesLock &lock,
	const HUSD_FindPrims &prims,
	const HUSD_TimeCode &timecode,
	fpreal opacity)
{
    auto	 indata = lock.constData();

    myVersionId++;
    if (indata && indata->isStageValid())
    {
	auto	 stage = indata->stage();
	auto	 pathset = prims.getExpandedPathSet();
	auto	 layer = myData->layer(HUSD_OVERRIDES_BASE_LAYER);

	{
	    // As a second pass, check the current stage value against the
	    // requested value, and create an override if required. Because
	    // visibility is an animatable attribute, the best we can do is
	    // set the default value.
	    SdfChangeBlock	 changeblock;

	    for (auto &&path : pathset.sdfPathSet())
	    {
		UsdGeomImageable	 prim(stage->GetPrimAtPath(path));

		if (prim)
		{
		    SdfPrimSpecHandle	 primspec;

		    primspec = SdfCreatePrimInLayer(layer, path);
		    if (primspec)
		    {
			SdfAttributeSpecHandle	 opacspec;

			opacspec = getOrCreateAttributeSpec(primspec,
			    UsdGeomTokens->primvarsDisplayOpacity,
			    SdfValueTypeNames->FloatArray);

			if (opacspec)
			{
			    VtArray<float> vtarray;
			    vtarray.push_back(opacity);
			    VtValue arrayvalue(vtarray);
			    opacspec->SetDefaultValue(VtValue(vtarray));
			}

			opacspec->SetInfo(
				UsdGeomTokens->interpolation,
				VtValue(UsdGeomTokens->constant));
		    }
		}
	    }
	}
    }

    return true;
}

bool
HUSD_Overrides::getSoloGeometry(HUSD_PathSet &paths) const
{
    HUSDgetSoloGeometryPaths(
        myData->layer(HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER), paths);

    return (paths.size() > 0);
}

bool
HUSD_Overrides::showPurpose(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_FindPrims &prims,
        const UT_StringRef &purpose)
{
    auto        indata = lock.constData();
    auto        stage = indata->stage();
    auto        layer = myData->layer(HUSD_OVERRIDES_PURPOSE_LAYER);
    const auto& pathset = prims.getExpandedPathSet();

    myVersionId++;
    if (!pathset.empty())
    {
        HUSD_FindPrims purposegeo(lock, prims.getExpandedPathSet(),
            prims.traversalDemands());

        // Add all descendants of the selected prim to the set of prims for
        // which the required purpose is to be set to default. The parent prims
        // may have different overrides which should not affect the child prims
        // from this prim down.
        purposegeo.addDescendants();

        const XUSD_PathSet &purposegeoset =
            purposegeo.getExpandedPathSet().sdfPathSet();

        {
            SdfChangeBlock changeblock;

            // First remove existing purpose and visibility overrides on any
            // prims and their children we have been asked to change.
            for (auto &&path : purposegeoset)
            {
                if (const auto &primspec = layer->GetPrimAtPath(path))
                {
                    primspec->GetRealNameParent()->RemoveNameChild(primspec);
                }
            }
        }

        {
            SdfChangeBlock changeblock;

            // As a second pass, check the current stage value against the
            // requested value, and create an override if required.
            for (auto &&path : purposegeoset)
            {
                UsdGeomImageable	 prim(stage->GetPrimAtPath(path));

                if (prim)
                {
                    TfToken primpurpose;

                    // Look for an authored purpose. If there isn't one, make
                    // sure the geoset doesn't contain ancestors of this prim
                    // because we don't want to create purpose attributes where
                    // it's not required. Skip if it finds ancestors. We will
                    // hit the highest ancestor in other iterations.
                    if (prim.GetPurposeAttr().HasAuthoredValue())
                        prim.GetPurposeAttr().Get(&primpurpose);
                    else if (purposegeoset.containsAncestor(path))
                        continue;
                    else
                        primpurpose = prim.ComputePurpose();

                    if (primpurpose == TfToken(purpose))
                    {
                        SdfPrimSpecHandle primspec = SdfCreatePrimInLayer(
                            layer, path);

                        if (primspec)
                        {
                            SdfAttributeSpecHandle purposespec
                                = getOrCreateAttributeSpec(
                                    primspec, UsdGeomTokens->purpose,
                                    SdfValueTypeNames->Token);

                            if (purposespec)
                                purposespec->SetDefaultValue(
                                    VtValue(UsdGeomTokens->default_));
                        }
                    }
                    else if (primpurpose != TfToken(UsdGeomTokens->default_))
                    {
                        SdfPrimSpecHandle primspec = SdfCreatePrimInLayer(
                            layer, path);

                        if (primspec)
                        {
                            SdfAttributeSpecHandle visspec
                                = getOrCreateAttributeSpec(
                                    primspec, UsdGeomTokens->visibility,
                                    SdfValueTypeNames->Token);

                            if (visspec)
                                visspec->SetDefaultValue(
                                    VtValue(UsdGeomTokens->invisible));
                        }
                    }
                }
            }
        }
    }

    return true;
}

GU_DetailHandle
HUSD_Overrides::setXforms(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_TimeCode &timecode,
        bool global_xform,
        const UT_Matrix4D &handle_xform,
        const UT_Matrix4D &pivot_xform,
        bool set_pivot_on_primary_prim,
        const UT_StringArray &selected_paths,
        const GU_ConstDetailHandle &deltagdh)
{
    HUSD_AutoWriteLock writelock(lock);

    // Special case: if we are asked to just clear "/", that means we should
    // clear the override active layer fully and re-author all the xforms in
    // deltageo.
    auto active_layer = writelock.data()->activeLayer();
    HUSD_PathSet selected_path_set;
    bool author_full_delta_layer =
        selected_paths.size() == 1 && selected_paths[0] == "/";

    if (author_full_delta_layer)
    {
        {
            SdfChangeBlock changeblock;

            for (auto &&rootprim : active_layer->GetRootPrims())
                active_layer->RemoveRootPrim(rootprim);
        }
    }
    else
    {
        // Clear the current contents of the viewport state layer so when we
        // read from the stage the results won't be affected by our current
        // overrides. We only need to clear prims that we want to modify with
        // this operation (and their children). Start by converting the
        // selected paths into an HUSD_PathSet.
        for (auto &&path : selected_paths)
        {
            UT_StringHolder primpath;
            int64 instanceid = -1;
            HUSDsplitInstanceIdAndPath(path, primpath, instanceid);
            selected_path_set.insert(HUSD_Path(primpath));
        }
        // Then go through the path set and delete prims from the layer.
        {
            SdfChangeBlock changeblock;

            active_layer->Traverse(SdfPath::AbsoluteRootPath(),
                [&selected_path_set, &active_layer](const SdfPath &path)
                {
                    // We have to clear the ancestors of any selected prims so
                    // we can get an accurate starting transform for all the
                    // selected prims (without any delta applied).
                    if (selected_path_set.containsPathOrDescendant(path))
                    {
                        auto primspec = active_layer->GetPrimAtPath(path);
                        for (auto &&attrspec : primspec->GetAttributes())
                            primspec->RemoveProperty(attrspec);
                    }
                });
        }
    }

    GU_DetailHandle out_deltagdh;
    HUSD_PathSet modified_paths;
    bool time_varying = false;
    bool edit_only_selected_prims = !author_full_delta_layer;

    if (active_layer->GetNumSubLayerPaths() == 0)
    {
        SdfLayerRefPtr inverse_layer = SdfLayer::CreateAnonymous();
        HUSD_XformEditor::applyEdits(writelock,
            deltagdh.gdp(),
            true,
            UT_StringHolder::theEmptyString,
            HUSD_XFORM_COMMON_API_APPEND,
            timecode,
            [] (const UT_StringRef &path) { return true; },
            modified_paths,
            time_varying);
        // Move the xforms onto the inverse sublayer on the active layer.
        inverse_layer->TransferContent(active_layer);
        active_layer->Clear();
        active_layer->GetSubLayerPaths().push_back(inverse_layer->GetIdentifier());
        // The first time we author the xforms into a clea overrides layer, we
        // have to author the xforms for every prim in the delta.
        edit_only_selected_prims = false;
    }

    // If this is a "re-author everything" request, we don't want to make
    // a copy of the input gdh (and we'll just return an empty gdh).
    if (!author_full_delta_layer)
    {
        out_deltagdh = HUSD_XformEditor::getDeltaWithParmXforms(
            selected_paths,
            timecode,
            deltagdh,
            global_xform ? nullptr : &handle_xform,
            (global_xform && !set_pivot_on_primary_prim) ? nullptr : &pivot_xform,
            global_xform ? &handle_xform : nullptr,
            set_pivot_on_primary_prim,
            lock,
            false);
        if (!out_deltagdh)
            return out_deltagdh;
    }

    std::function<bool(const UT_StringRef & path)> allow_all_edits_fn =
        [] (const UT_StringRef &path) { return true; };
    std::function<bool(const UT_StringRef & path)> allow_selected_edits_fn =
        [&selected_path_set] (const UT_StringRef &path) {
            // Use the same logic to clear the attributes at the start of this
            // function so we will repopulate everything we lost.
            return selected_path_set.containsPathOrDescendant(HUSD_Path(path));
        };
    // Apply the edits from our modified gdh if we built one, otherwise we
    // just use the gdh that was passed in, unmodified.
    HUSD_XformEditor::applyEdits(writelock,
        author_full_delta_layer ? deltagdh.gdp() : out_deltagdh.gdp(),
        false,
        UT_StringHolder::theEmptyString,
        HUSD_XFORM_COMMON_API_APPEND,
        timecode,
        edit_only_selected_prims
            ? allow_selected_edits_fn
            : allow_all_edits_fn,
        modified_paths,
        time_varying);

    return out_deltagdh;
}

bool
HUSD_Overrides::setExpansionStateDrawMode(HUSD_AutoAnyLock &lock,
        const HUSD_ExpansionState &expansionstate)
{
    auto	 indata = lock.constData();

    if (indata && indata->isStageValid())
    {
        auto             stage = indata->stage();
        auto             layer = myData->layer(HUSD_OVERRIDES_EXPANSION_LAYER);
        UsdGeomBBoxCache bboxcache(UsdTimeCode::EarliestTime(),
                            UsdGeomImageable::GetOrderedPurposeTokens(),
                            true);

        if (setDrawModeFromExpansionState(
                stage->GetPseudoRoot(), layer, expansionstate, bboxcache))
            myVersionId++;
    }

    return true;
}

bool
HUSD_Overrides::setExpansionStateVisibility(HUSD_AutoAnyLock &lock,
        const HUSD_ExpansionState &expansionstate)
{
    auto	 indata = lock.constData();

    if (indata && indata->isStageValid())
    {
        auto         stage = indata->stage();
        auto         layer = myData->layer(HUSD_OVERRIDES_EXPANSION_LAYER);

        if (setVisibilityFromExpansionState(
                stage->GetPseudoRoot(), layer, expansionstate))
            myVersionId++;
    }

    return true;
}

void
HUSD_Overrides::lockToData(XUSD_Data *data)
{
    myData->lockToData(data);
}

void
HUSD_Overrides::unlockFromData(XUSD_Data *data)
{
    // Anything could have been done to the custom layer while we were
    // locked to the XUSD_Data object, so we have to assume something
    // changed, and bump our version id.
    myData->unlockFromData(data);
    myVersionId++;
}

void
HUSD_Overrides::save(std::ostream &os,
        const UT_Array<HUSD_OverridesLayerId> &layerids) const
{
    UT_AutoJSONWriter	 writer(os, false);
    UT_JSONWriter	&w = *writer;

    w.jsonBeginMap();
    for (auto &&id : layerids)
    {
	auto		 layer = myData->layer(id);
	std::string	 str;

	layer->ExportToString(&str);
	w.jsonKeyToken(HUSD_LAYER_KEYS[id]);
	w.jsonString(str.c_str());
    }
    w.jsonEndMap();
}

bool
HUSD_Overrides::load(UT_IStream &is)
{
    UT_AutoJSONParser	 parser(is);
    UT_JSONValue	 rootvalue;

    myVersionId++;
    if (!rootvalue.parseValue(parser) || !rootvalue.getMap())
	return false;

    const UT_JSONValueMap	*map = rootvalue.getMap();

    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
    {
	auto		 layer = myData->layer((HUSD_OverridesLayerId)i);
	const UT_JSONValue *value = map->get(HUSD_LAYER_KEYS[i]);

	layer->Clear();

	if (!value || !value->getStringHolder())
	    continue;

	if (!layer->ImportFromString(value->getStringHolder()->toStdString()))
	    return false;
    }

    return true;
}

void
HUSD_Overrides::copy(const HUSD_Overrides &src)
{
    myVersionId++;
    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
	myData->layer((HUSD_OverridesLayerId)i)->TransferContent(
	    src.myData->layer((HUSD_OverridesLayerId)i));
}

void
HUSD_Overrides::clear(const UT_Array<HUSD_OverridesLayerId> &layerids,
        const UT_StringRef &fromprim)
{
    auto sdfpath = HUSDgetSdfPath(fromprim);

    for (auto &&id : layerids)
    {
        auto layer = myData->layer(id);

        if (!sdfpath.IsEmpty() && sdfpath != SdfPath::AbsoluteRootPath())
        {
            // Don't allow branch-local manipulation of the solo layers,
            // since the result is likely to be meaningless.
            if (id != HUSD_OVERRIDES_SOLO_LIGHTS_LAYER &&
                id != HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER)
            {
                auto prim = layer->GetPrimAtPath(sdfpath);

                if (prim)
                {
                    if (prim->GetNameParent())
                        prim->GetNameParent()->RemoveNameChild(prim);
                    else
                        layer->RemoveRootPrim(prim);
                }
            }
        }
        else
        {
            myData->clearSubLayers(id);
            layer->Clear();
        }
    }
    myVersionId++;
}

void
HUSD_Overrides::clear(HUSD_OverridesLayerId layer_id,
        const UT_StringRef &fromprim)
{
    auto layer = myData->layer(layer_id);
    auto sdfpath = HUSDgetSdfPath(fromprim);

    if (!sdfpath.IsEmpty() && sdfpath != SdfPath::AbsoluteRootPath())
    {
        // Don't allow branch-local manipulation of the solo layers,
        // since the result is likely to be meaningless.
        if (layer_id != HUSD_OVERRIDES_SOLO_LIGHTS_LAYER &&
            layer_id != HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER)
        {
            auto prim = layer->GetPrimAtPath(sdfpath);

            if (prim)
            {
                if (prim->GetNameParent())
                    prim->GetNameParent()->RemoveNameChild(prim);
                else
                    layer->RemoveRootPrim(prim);
            }
        }
    }
    else
    {
        myData->clearSubLayers(layer_id);
        layer->Clear();
    }
    myVersionId++;
}

bool
HUSD_Overrides::isEmpty(const UT_Array<HUSD_OverridesLayerId> &layerids) const
{
    for (auto &&id : layerids)
	if (!HUSDisLayerEmpty(myData->layer(id)))
	    return false;

    return true;
}

bool
HUSD_Overrides::isEmpty(HUSD_OverridesLayerId layer_id) const
{
    return HUSDisLayerEmpty(myData->layer(layer_id));
}

