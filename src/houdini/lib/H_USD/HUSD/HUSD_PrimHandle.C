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

#include "HUSD_PrimHandle.h"
#include "HUSD_PropertyHandle.h"
#include "HUSD_Overrides.h"
#include "HUSD_Constants.h"
#include "HUSD_Info.h"
#include "HUSD_PathSet.h"
#include "HUSD_TimeCode.h"
#include "XUSD_ObjectLock.h"
#include "XUSD_OverridesData.h"
#include "XUSD_Utils.h"
#include <gusd/UT_Gf.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_StringStream.h>
#include <UT/UT_Debug.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdLux/light.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/spec.h>
#include <pxr/usd/kind/registry.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
    TfToken
    ComputeDrawMode(const UsdPrim &prim,
            const UT_StringMap<UT_StringHolder> &overrides)
    {
        auto it = overrides.find(prim.GetPath().GetText());

        if (it != overrides.end())
        {
            // The most local setting always wins, regardless of wha that
            // setting is. So return the override value if it exists.
            return TfToken(it->second.toStdString());
        }

        if (UsdGeomModelAPI modelapi = UsdGeomModelAPI(prim))
        {
            if (UsdAttribute drawmodeattr = modelapi.GetModelDrawModeAttr())
            {
                TfToken localdrawmode;

                drawmodeattr.Get(&localdrawmode);
                return localdrawmode;
            }
        }

        if (UsdPrim parent = prim.GetParent())
            return ComputeDrawMode(parent, overrides);

        return UsdGeomTokens->default_;
    }

    bool
    ComputeActive(const UsdPrim &prim, const UT_StringMap<bool> &overrides)
    {
        auto it = overrides.find(prim.GetPath().GetText());

        if (it != overrides.end())
        {
            // If we have an override indicating we are inactive, we are done.
            // There is no way to become active again.
            if (!it->second)
                return it->second;

            // If we have an override indicating that we are active, there
            // may still be an ancestor indicating we are inactive, so we can't
            // stop looking yet. This override simply cancels out the setting
            // from the stage at this current level of the hierarchy.
        }
        else
        {
            if (!prim.IsActive())
                return false;
        }

        if (UsdPrim parent = prim.GetParent())
            return ComputeActive(parent, overrides);

        return true;
    }

    TfToken
    ComputeVisibility(const UsdPrim &prim,
            const UsdTimeCode &time,
            const UT_StringMap<UT_StringHolder> &overrides)
    {
        auto it = overrides.find(prim.GetPath().GetText());

        if (it != overrides.end())
        {
            // If we have an override indicating invisibility, we are done.
            // There is no way to become visible again.
            if (it->second == UsdGeomTokens->invisible.GetText())
                return UsdGeomTokens->invisible;

            // If we have an override indicating inherited visibility, there
            // may still be an ancestor indicating invisibility, so we can't
            // stop looking yet. This override simply cancels out the setting
            // from the stage at this current level of the hierarchy.
        }
        else
        {
            if (UsdGeomImageable ip = UsdGeomImageable(prim))
            {
                TfToken localvis;

                ip.GetVisibilityAttr().Get(&localvis, time);
                if (localvis == UsdGeomTokens->invisible)
                    return UsdGeomTokens->invisible;
            }
        }

        if (UsdPrim parent = prim.GetParent())
            return ComputeVisibility(parent, time, overrides);

        return UsdGeomTokens->inherited;
    }
}

HUSD_PrimHandle::HUSD_PrimHandle()
{
}

HUSD_PrimHandle::HUSD_PrimHandle(const HUSD_DataHandle &data_handle,
	const HUSD_Path &prim_path)
    : HUSD_ObjectHandle(prim_path),
      myDataHandle(data_handle)
{
}

HUSD_PrimHandle::HUSD_PrimHandle(const HUSD_DataHandle &data_handle,
	const HUSD_ConstOverridesPtr &overrides,
        OverridesHandling overrides_handling,
	const HUSD_Path &prim_path)
    : HUSD_ObjectHandle(prim_path, overrides_handling),
      myDataHandle(data_handle),
      myOverrides(overrides)
{
}

HUSD_PrimHandle::~HUSD_PrimHandle()
{
}

const HUSD_DataHandle &
HUSD_PrimHandle::dataHandle() const
{
    return myDataHandle;
}

const HUSD_ConstOverridesPtr &
HUSD_PrimHandle::overrides() const
{
    static const HUSD_ConstOverridesPtr theNullOverrides;

    return (overridesHandling() == OVERRIDES_COMPOSE)
        ? myOverrides
        : theNullOverrides;
}

HUSD_PrimStatus
HUSD_PrimHandle::getStatus() const
{
    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (path() == HUSD_Path::theRootPrimPath)
    {
	return HUSD_PRIM_ROOT;
    }
    else
    {
	XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

	if (!lock.obj())
	{
	    return HUSD_PRIM_UNKNOWN;
	}
	else if (lock.obj().IsInstance())
	{
	    return HUSD_PRIM_INSTANCE;
	}
	else if (lock.obj().HasAuthoredPayloads())
	{
	    return HUSD_PRIM_HASPAYLOAD;
	}
	else if (lock.obj().HasAuthoredReferences() ||
		 lock.obj().HasAuthoredInherits() ||
		 lock.obj().HasAuthoredSpecializes() ||
		 lock.obj().HasVariantSets())
	{
	    return HUSD_PRIM_HASARCS;
	}
	else if (lock.obj().IsInMaster() ||
		 lock.obj().IsInstanceProxy())
	{
	    return HUSD_PRIM_INMASTER;
	}
    }

    return HUSD_PRIM_NORMAL;
}

UT_StringHolder
HUSD_PrimHandle::getPrimType() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);
    UT_StringHolder		 prim_type;

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj())
	prim_type = lock.obj().GetTypeName().GetText();

    return prim_type;
}

UT_StringHolder
HUSD_PrimHandle::getVariantInfo() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);
    UT_String			 prim_variant_info;

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj())
    {
	auto	 vsets = lock.obj().GetVariantSets();

	for (auto &&vset : vsets.GetNames())
	{
	    if (prim_variant_info.isstring())
		prim_variant_info.append(" ");
	    prim_variant_info.append(vset.c_str());
	    prim_variant_info.append(":");
	    prim_variant_info.append(vsets[vset].GetVariantSelection().c_str());
	}
    }

    return prim_variant_info;
}

UT_StringHolder
HUSD_PrimHandle::getKind() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj())
    {
	UsdModelAPI		 modelapi(lock.obj());
	TfToken			 kind;

	if (modelapi && modelapi.GetKind(&kind))
	    return UT_StringHolder(kind.GetText());
    }

    return UT_StringHolder();
}

UT_StringHolder
HUSD_PrimHandle::getPurpose() const
{
    XUSD_AutoObjectLock<UsdGeomImageable>	 lock(*this);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj())
    {
	TfToken			 purpose;

	purpose = lock.obj().ComputePurpose();
	return UT_StringHolder(purpose.GetText());
    }

    return UT_StringHolder();
}

UT_StringHolder
HUSD_PrimHandle::getProxyPath() const
{
    XUSD_AutoObjectLock<UsdGeomImageable>	 lock(*this);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj())
    {
	UsdPrim			 proxy_prim = lock.obj().ComputeProxyPrim();

	if (proxy_prim)
	    return UT_StringHolder(proxy_prim.GetPath().GetText());
    }

    return UT_StringHolder();
}

UT_StringHolder
HUSD_PrimHandle::getSpecifier() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj())
    {
	switch (lock.obj().GetSpecifier())
	{
	    case SdfSpecifierDef:
		return HUSD_Constants::getPrimSpecifierDefine();
	    case SdfSpecifierClass:
		return HUSD_Constants::getPrimSpecifierClass();
	    case SdfSpecifierOver:
		return HUSD_Constants::getPrimSpecifierOverride();
	    case SdfNumSpecifiers:
		// Not a valid value. Just fall through.
		break;
	}
    }

    return UT_StringHolder();
}

UT_StringHolder
HUSD_PrimHandle::getDrawMode(bool *has_override) const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);

    if (has_override)
	*has_override = false;
    if (lock.obj() && !lock.obj().IsPseudoRoot() && lock.obj().IsModel())
    {
        TfToken		 drawmode = UsdGeomTokens->default_;

        // When we want to pull the overrides from the Sdf Layers without
        // composing them onto the LOP stage, we need to emulate the logic
        // used to compose this value from the overrides layers.
        if (myOverrides && overridesHandling() == OVERRIDES_INSPECT)
        {
            UT_StringMap<UT_StringHolder> overrides;

            myOverrides->getDrawModeOverrides(path().pathStr(), overrides);
            drawmode = ComputeDrawMode(lock.obj(), overrides);
        }
        else
        {
            UsdGeomModelAPI	 modelapi(lock.obj());

            if (modelapi)
                drawmode = modelapi.ComputeModelDrawMode();
        }


	if (has_override && myOverrides)
	{
	    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
	    {
		SdfLayerHandle		 overridelayer;

		overridelayer = myOverrides->data().
		    layer((HUSD_OverridesLayerId)i);
		if (overridelayer)
		{
		    auto drawmodespec = overridelayer->
			GetPropertyAtPath(lock.obj().GetPath().AppendProperty(
			    UsdGeomTokens->modelDrawMode));

		    if (drawmodespec)
		    {
			*has_override = true;
			break;
		    }
		}
	    }
	}

	return UT_StringHolder(drawmode.GetText());
    }

    return UT_StringHolder();
}

HUSD_PrimAttribState
HUSD_PrimHandle::getActive() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);
    HUSD_PrimAttribState	 active = HUSD_NOTAPPLICABLE;

    if (lock.obj() && !lock.obj().IsPseudoRoot())
    {
        // When we want to pull the overrides from the Sdf Layers without
        // composing them onto the LOP stage, we need to emulate the logic
        // used to compose this value from the overrides layers.
        if (myOverrides && overridesHandling() == OVERRIDES_INSPECT)
        {
            UT_StringMap<bool> overrides;

            myOverrides->getActiveOverrides(path().pathStr(), overrides);
            active = ComputeActive(lock.obj(), overrides)
                ? HUSD_TRUE : HUSD_FALSE;
        }
        else
        {
            active = lock.obj().IsActive() ? HUSD_TRUE : HUSD_FALSE;
        }

	if (myOverrides)
	{
	    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
	    {
                SdfLayerHandle overridelayer = myOverrides->data().
		    layer((HUSD_OverridesLayerId)i);

		if (overridelayer)
		{
		    auto primspec = overridelayer->
			GetPrimAtPath(lock.obj().GetPath());

		    if (primspec && primspec->HasActive())
		    {
			active = (HUSDstateAsBool(active))
			    ? HUSD_OVERRIDDEN_TRUE
			    : HUSD_OVERRIDDEN_FALSE;
			break;
		    }
		}
	    }
	}
    }

    return active;
}

HUSD_PrimAttribState
HUSD_PrimHandle::getVisible(const HUSD_TimeCode &timecode) const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);
    HUSD_PrimAttribState	 visible = HUSD_NOTAPPLICABLE;

    if (lock.obj())
    {
	UsdGeomImageable	 imageable(lock.obj());
	UsdTimeCode		 usdtime(HUSDgetUsdTimeCode(timecode));

	if (imageable)
	{
            // When we want to pull the overrides from the Sdf Layers without
            // composing them onto the LOP stage, we need to emulate the logic
            // used to compose this value from the overrides layers.
            if (myOverrides && overridesHandling() == OVERRIDES_INSPECT)
            {
                UT_StringMap<UT_StringHolder> overrides;

                myOverrides->getVisibleOverrides(path().pathStr(), overrides);
                visible = (ComputeVisibility(lock.obj(), usdtime, overrides) !=
                           UsdGeomTokens->invisible) ? HUSD_TRUE : HUSD_FALSE;
            }
            else
            {
                visible = (imageable.ComputeVisibility(usdtime) !=
                           UsdGeomTokens->invisible) ? HUSD_TRUE : HUSD_FALSE;
            }

            UsdAttribute	 visattr = imageable.GetVisibilityAttr();

            if (visattr)
            {
                if (HUSDvalueMightBeTimeVarying(visattr))
                    visible = (HUSDstateAsBool(visible))
                        ? HUSD_ANIMATED_TRUE
                        : HUSD_ANIMATED_FALSE;

                if (myOverrides)
                {
                    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
                    {
                        SdfLayerHandle	 overridelayer;
                        SdfSpecHandle	 visspec;

                        overridelayer = myOverrides->data().
                            layer((HUSD_OverridesLayerId)i);
                        if (overridelayer)
                            visspec = overridelayer->
                                GetPropertyAtPath(
                                    lock.obj().GetPath().AppendProperty(
                                    UsdGeomTokens->visibility));

                        if (visspec)
                        {
                            visible = (HUSDstateAsBool(visible))
                                ? HUSD_OVERRIDDEN_TRUE
                                : HUSD_OVERRIDDEN_FALSE;
                            break;
                        }
                    }
                }
            }
	}
    }

    return visible;
}

HUSD_SoloState
HUSD_PrimHandle::getSoloState() const
{
    XUSD_AutoObjectLock<UsdPrim>     lock(*this);
    HUSD_SoloState                   state = HUSD_SOLO_NOTAPPLICABLE;

    if (lock.obj() && !lock.obj().IsPseudoRoot())
    {
        SdfLayerHandle               layer;
        HUSD_PathSet                 paths;

        // The solo state doesn't represent an actual feature on the stage (at
        // least not directly), so it always needs to be read directly from the
        // overrides layer. So we don't care what the overrides handling
        // setting is. 
        if (lock.obj().IsA<UsdLuxLight>())
        {
            if (myOverrides &&
                !myOverrides->isEmpty(HUSD_OVERRIDES_SOLO_LIGHTS_LAYER))
            {
                std::string path = lock.obj().GetPath().GetString();

                myOverrides->getSoloLights(paths);
                if (paths.contains(path))
                    state = HUSD_SOLO_TRUE;
                else
                    state = HUSD_SOLO_FALSE;
            }
            else
                state = HUSD_SOLO_NOSOLO;
        }
        else if (lock.obj().IsA<UsdGeomImageable>())
        {
            if (myOverrides &&
                !myOverrides->isEmpty(HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER))
            {
                std::string path = lock.obj().GetPath().GetString();

                myOverrides->getSoloGeometry(paths);
                if (paths.contains(path))
                    state = HUSD_SOLO_TRUE;
                else
                    state = HUSD_SOLO_FALSE;
            }
            else
                state = HUSD_SOLO_NOSOLO;
        }
    }

    return state;
}

bool
HUSD_PrimHandle::hasAnyOverrides() const
{
    XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

    // This method is only interested in the overrides themselves, not the
    // composed USD primitive, so we don't need to change its behavior based
    // on the overridesHandling value.
    if (lock.obj() && !lock.obj().IsPseudoRoot())
    {
	if (myOverrides)
	{
	    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
	    {
		SdfLayerHandle overridelayer;

		overridelayer = myOverrides->data().
		    layer((HUSD_OverridesLayerId)i);
		if (overridelayer)
		{
		    auto primspec = overridelayer->
			GetPrimAtPath(lock.obj().GetPath());

		    if (primspec)
			return true;
		}
	    }
	}
    }

    return false;
}

int64
HUSD_PrimHandle::getDescendants(HUSD_PrimTraversalDemands demands) const
{
    XUSD_AutoObjectLock<UsdPrim>     lock(*this);
    int64                            descendants = 0;

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj() && !lock.obj().IsPseudoRoot())
    {
        auto p(HUSDgetUsdPrimPredicate(demands));

        for (auto child :
             lock.obj().GetFilteredDescendants(UsdTraverseInstanceProxies(p)))
            descendants++;
    }

    return descendants;
}

bool
HUSD_PrimHandle::hasPayload() const
{
    XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (!lock.obj())
	return false;
    return lock.obj().HasAuthoredPayloads();
}

bool
HUSD_PrimHandle::isDefined() const
{
    XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj() && lock.obj().IsDefined())
	return true;

    return false;
}

bool
HUSD_PrimHandle::hasChildren(HUSD_PrimTraversalDemands demands) const
{
    XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (!lock.obj())
	return false;

    auto p(HUSDgetUsdPrimPredicate(demands));

    return !lock.obj().
	GetFilteredChildren(UsdTraverseInstanceProxies(p)).empty();
}

void
HUSD_PrimHandle::getChildren(UT_Array<HUSD_PrimHandle> &children,
	HUSD_PrimTraversalDemands demands) const
{
    XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj())
    {
	auto p(HUSDgetUsdPrimPredicate(demands));

	for (auto &&child : lock.obj().
		GetFilteredChildren(UsdTraverseInstanceProxies(p)))
	{
	    children.append(
		HUSD_PrimHandle(dataHandle(),
		myOverrides,
                overridesHandling(),
		child.GetPath()));
	}
    }
}

UT_StringHolder
HUSD_PrimHandle::getIcon() const
{
    HUSD_AutoReadLock		 readlock(myDataHandle, overrides());
    HUSD_Info			 info(readlock);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    return info.getIcon(path().pathStr());
}

void
HUSD_PrimHandle::getProperties(UT_Array<HUSD_PropertyHandle> &props,
	bool include_attributes,
	bool include_relationships,
	bool include_shader_inputs) const
{
    HUSD_AutoReadLock		 readlock(myDataHandle, overrides());
    HUSD_Info			 info(readlock);
    UT_ArrayStringSet		 prop_names;

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (include_attributes)
	info.getAttributeNames(path().pathStr(), prop_names);
    if (include_relationships)
	info.getRelationshipNames(path().pathStr(), prop_names);
    if (include_shader_inputs)
	info.getShaderInputAttributeNames(path().pathStr(), prop_names);

    for (auto &&prop_name : prop_names)
	props.append(HUSD_PropertyHandle(*this, prop_name));
}

void
HUSD_PrimHandle::getAttributeNames(UT_ArrayStringSet &attrib_names) const
{
    HUSD_AutoReadLock		 readlock(myDataHandle, overrides());
    HUSD_Info			 info(readlock);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    info.getAttributeNames(path().pathStr(), attrib_names);
}

void
HUSD_PrimHandle::extractAttributes(
	const UT_ArrayStringSet &which_attribs,
	const HUSD_TimeCode &tc,
	UT_Options &values)
{
    HUSD_AutoReadLock		 readlock(myDataHandle, overrides());
    HUSD_Info			 info(readlock);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    info.extractAttributes(path().pathStr(), which_attribs, tc, values);
}

