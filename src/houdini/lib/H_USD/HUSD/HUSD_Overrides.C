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
#include "HUSD_FindPrims.h"
#include "HUSD_PathSet.h"
#include "HUSD_TimeCode.h"
#include "XUSD_OverridesData.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_JSONValue.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Keep these strings aligned with the HUSD_OverridesLayerId enum defined
// in HUSD_Utils.h.
static const char *HUSD_LAYER_KEYS[HUSD_OVERRIDES_NUM_LAYERS] = {
    "custom",
    "sololights",
    "sologeometry",
    "base"
};

HUSD_Overrides::HUSD_Overrides()
    : myData(new XUSD_OverridesData()),
      myVersionId(0)
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
                SdfPath::ReflexiveRelativePath().
                AppendProperty(UsdGeomTokens->modelDrawMode));
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
	    // Run through and delete the "active" override currently set on
	    // any prims we have been asked to change.
	    SdfChangeBlock	 changeblock;

	    for (auto &&path : pathset.sdfPathSet())
	    {
		SdfPrimSpecHandle	 primspec;

		primspec = layer->GetPrimAtPath(path);
		if (primspec)
		{
		    SdfAttributeSpecHandle	 drawmodespec;

		    drawmodespec = primspec->GetAttributeAtPath(
			SdfPath::ReflexiveRelativePath().
			AppendProperty(UsdGeomTokens->modelDrawMode));
		    if (drawmodespec)
		    {
			primspec->RemoveProperty(drawmodespec);
			layer->RemovePrimIfInert(primspec);
		    }
		}
	    }
	}

	{
	    // As a second pass, check the current stage value against the
	    // requested value, and create an override if required.
	    SdfChangeBlock	 changeblock;
	    TfToken		 drawmodetoken(drawmode.toStdString());

	    for (auto &&path : pathset.sdfPathSet())
	    {
		UsdPrim		 prim(stage->GetPrimAtPath(path));

		if (prim && prim.IsModel())
		{
		    UsdGeomModelAPI	 modelapi(prim);

		    if (modelapi.ComputeModelDrawMode() != drawmodetoken)
		    {
			SdfPrimSpecHandle	 primspec;

			primspec = SdfCreatePrimInLayer(layer, path);
			if (primspec)
			{
			    SdfAttributeSpecHandle	 drawmodespec;

			    drawmodespec = SdfAttributeSpec::New(primspec,
				UsdGeomTokens->modelDrawMode,
				SdfValueTypeNames->Token);
			    if (drawmodespec)
				drawmodespec->SetDefaultValue(
				    VtValue(drawmodetoken));
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
                SdfPath::ReflexiveRelativePath().
                AppendProperty(UsdGeomTokens->visibility));
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
	    // Run through and delete the "active" override currently set on
	    // any prims we have been asked to change.
	    SdfChangeBlock	 changeblock;

	    for (auto &&path : pathset.sdfPathSet())
	    {
		SdfPrimSpecHandle	 primspec;

		primspec = layer->GetPrimAtPath(path);
		if (primspec)
		{
		    SdfAttributeSpecHandle	 visspec;

		    visspec = primspec->GetAttributeAtPath(
			SdfPath::ReflexiveRelativePath().
			AppendProperty(UsdGeomTokens->visibility));
		    if (visspec)
		    {
			primspec->RemoveProperty(visspec);
			layer->RemovePrimIfInert(primspec);
		    }
		}
	    }
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
		UsdGeomImageable	 prim(stage->GetPrimAtPath(path));

		if (prim && prim.ComputeVisibility(usdtime) != vistoken)
		{
		    SdfPrimSpecHandle	 primspec;

		    primspec = SdfCreatePrimInLayer(layer, path);
		    if (primspec)
		    {
			SdfAttributeSpecHandle	 visspec;

			visspec = SdfAttributeSpec::New(primspec,
			    UsdGeomTokens->visibility,
			    SdfValueTypeNames->Token);
			if (visspec)
			    visspec->SetDefaultValue(VtValue(vistoken));
		    }
		}
	    }
	}
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
            HUSD_Constants::getLuxLightPrimType().c_str());
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
    // Preserve the expanded list of soloed paths, without any modifiction.
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
        pattern.sprintf("%%type:%s",
            HUSD_Constants::getGeomImageablePrimType().c_str());
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

                    visspec = SdfAttributeSpec::New(primspec,
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

                visspec = SdfAttributeSpec::New(primspec,
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

			opacspec = SdfAttributeSpec::New(primspec,
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
HUSD_Overrides::save(std::ostream &os) const
{
    UT_AutoJSONWriter	 writer(os, false);
    UT_JSONWriter	&w = *writer;

    w.jsonBeginMap();
    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
    {
	auto		 layer = myData->layer((HUSD_OverridesLayerId)i);
	std::string	 str;

	layer->ExportToString(&str);
	w.jsonKeyToken(HUSD_LAYER_KEYS[i]);
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
HUSD_Overrides::clear(const UT_StringRef &fromprim)
{
    auto sdfpath = HUSDgetSdfPath(fromprim);

    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
    {
        auto layer = myData->layer((HUSD_OverridesLayerId)i);

        if (!sdfpath.IsEmpty() && sdfpath != SdfPath::AbsoluteRootPath())
        {
            // Don't allow branch-local manipulation of the solo layers,
            // since the result is likely to be meaningless.
            if (i != HUSD_OVERRIDES_SOLO_LIGHTS_LAYER &&
                i != HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER)
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
            layer->Clear();
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
        layer->Clear();
    myVersionId++;
}

bool
HUSD_Overrides::isEmpty() const
{
    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
	if (!HUSDisLayerEmpty(myData->layer((HUSD_OverridesLayerId)i)))
	    return false;

    return true;
}

bool
HUSD_Overrides::isEmpty(HUSD_OverridesLayerId layer_id) const
{
    return HUSDisLayerEmpty(myData->layer(layer_id));
}

