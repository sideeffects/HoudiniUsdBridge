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

#include "HUSD_Overrides.h"
#include "HUSD_FindPrims.h"
#include "HUSD_TimeCode.h"
#include "XUSD_OverridesData.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_JSONValue.h>
#include <HUSD/HUSD_Constants.h>
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

	    for (auto &&path : pathset)
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
	    // requested value, and create an override if required. Because
	    // visibility is an animatable attribute, the best we can do is
	    // set the default value.
	    SdfChangeBlock	 changeblock;
	    TfToken		 drawmodetoken(drawmode.toStdString());

	    for (auto &&path : pathset)
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

	    for (auto &&path : pathset)
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

	    for (auto &&path : pathset)
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

	    for (auto &&path : pathset)
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

	    for (auto &&path : pathset)
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

    myVersionId++;
    layer->Clear();

    // Add descendants, and a requirement that we only want lights.
    HUSD_FindPrims	         light_prims(prims);
    std::vector<std::string>     sdfpaths;

    light_prims.setBaseTypeName(HUSD_Constants::getLuxLightPrimType());
    // Preserve the expanded list of soloed paths, without descendents or
    // ancestors added. Just the paths specified by the user.
    for (auto &&sdfpath : light_prims.getExpandedPathSet())
        sdfpaths.push_back(sdfpath.GetString());
    HUSDsetSoloLightPaths(layer, sdfpaths);

    light_prims.addDescendants();

    // If no lights are in the solo list, turn off soloing.
    if (!light_prims.getExpandedPathSet().empty())
    {
	// Deactivate any prims in the anti-set.
	for (auto &&path : light_prims.getExcludedPathSet(true))
	{
	    SdfPrimSpecHandle	 primspec;

	    primspec = SdfCreatePrimInLayer(layer, path);
	    if (primspec)
		primspec->SetActive(false);
	}

	// Activate any prims in the original set. This is in case they are
	// deactivated in the base layer, or they are references to prims in
	// the anti-set and thus were disabled incidentally by the loop above.
	for (auto &&sdfpath : light_prims.getExpandedPathSet())
	{
	    SdfPrimSpecHandle	 primspec;

	    primspec = SdfCreatePrimInLayer(layer, sdfpath);
	    if (primspec)
		primspec->SetActive(true);
	}
    }

    return true;
}

bool
HUSD_Overrides::addSoloLights(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_FindPrims &prims)
{
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_LIGHTS_LAYER);
    std::vector<std::string> paths;

    HUSDgetSoloLightPaths(layer, paths);
    for (auto &&sdfpath : prims.getExpandedPathSet())
        paths.push_back(sdfpath.GetString());

    return setSoloLights(lock, HUSD_FindPrims(lock, paths));
}

bool
HUSD_Overrides::removeSoloLights(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_FindPrims &prims)
{
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_LIGHTS_LAYER);
    const XUSD_PathSet &pathset = prims.getExpandedPathSet();
    std::vector<std::string> paths;
    std::vector<std::string> newpaths;

    HUSDgetSoloLightPaths(layer, paths);
    for (auto &&path : paths)
        if (pathset.find(SdfPath(path)) == pathset.end())
            newpaths.push_back(path);

    return setSoloLights(lock, HUSD_FindPrims(lock, newpaths));
}

bool
HUSD_Overrides::getSoloLights(std::vector<std::string> &prims) const
{
    HUSDgetSoloLightPaths(
        myData->layer(HUSD_OVERRIDES_SOLO_LIGHTS_LAYER), prims);

    return (prims.size() > 0);
}

bool
HUSD_Overrides::setSoloGeometry(HUSD_AutoWriteOverridesLock &lock,
	const HUSD_FindPrims &prims)
{
    SdfChangeBlock changeblock;
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER);

    myVersionId++;
    layer->Clear();

    // Add descendants, and a requirement that we only want imageable prims.
    HUSD_FindPrims               geo_prims(prims);
    std::vector<std::string>     sdfpaths;

    // Preserve the expanded list of soloed paths, without descendents or
    // ancestors added. Just the paths specified by the user.
    for (auto &&sdfpath : geo_prims.getExpandedPathSet())
        sdfpaths.push_back(sdfpath.GetString());
    HUSDsetSoloGeometryPaths(layer, sdfpaths);

    geo_prims.addDescendants();
    geo_prims.addAncestors();
    geo_prims.setBaseTypeName(HUSD_Constants::getGeomImageablePrimType());

    // If no lights are in the solo list, turn off soloing.
    if (!geo_prims.getExpandedPathSet().empty())
    {
	// Mark any prims in the anti-set invisible.
	for (auto &&path : geo_prims.getExcludedPathSet(true))
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
                    visspec->SetDefaultValue(VtValue(UsdGeomTokens->invisible));
            }
	}

        // Set any prims in the original set to inherit visibility. This is in
        // case they are invisible in the base layer, or they are references to
        // prims in the anti-set and thus were made invisible incidentally by
        // the loop above.
	for (auto &&sdfpath : geo_prims.getExpandedPathSet())
	{
	    SdfPrimSpecHandle	 primspec;

	    primspec = SdfCreatePrimInLayer(layer, sdfpath);
	    if (primspec)
            {
                SdfAttributeSpecHandle	 visspec;

                visspec = SdfAttributeSpec::New(primspec,
                    UsdGeomTokens->visibility,
                    SdfValueTypeNames->Token);
                if (visspec)
                    visspec->SetDefaultValue(VtValue(UsdGeomTokens->inherited));
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
    std::vector<std::string> paths;

    HUSDgetSoloGeometryPaths(layer, paths);
    for (auto &&sdfpath : prims.getExpandedPathSet())
        paths.push_back(sdfpath.GetString());

    return setSoloGeometry(lock, HUSD_FindPrims(lock, paths));
}

bool
HUSD_Overrides::removeSoloGeometry(HUSD_AutoWriteOverridesLock &lock,
        const HUSD_FindPrims &prims)
{
    auto layer = myData->layer(HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER);
    const XUSD_PathSet &pathset = prims.getExpandedPathSet();
    std::vector<std::string> paths;
    std::vector<std::string> newpaths;

    HUSDgetSoloGeometryPaths(layer, paths);
    for (auto &&path : paths)
        if (pathset.find(SdfPath(path)) == pathset.end())
            newpaths.push_back(path);

    return setSoloGeometry(lock, HUSD_FindPrims(lock, newpaths));
}

bool
HUSD_Overrides::getSoloGeometry(std::vector<std::string> &prims) const
{
    HUSDgetSoloGeometryPaths(
        myData->layer(HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER), prims);

    return (prims.size() > 0);
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

