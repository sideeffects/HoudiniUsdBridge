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

HUSD_PrimHandle::HUSD_PrimHandle()
{
}

HUSD_PrimHandle::HUSD_PrimHandle(const HUSD_DataHandle &data_handle,
	const UT_StringHolder &prim_path,
	const UT_StringHolder &prim_name)
    : HUSD_ObjectHandle(prim_path, prim_name),
      myDataHandle(data_handle)
{
}

HUSD_PrimHandle::HUSD_PrimHandle(const HUSD_DataHandle &data_handle,
	const HUSD_ConstOverridesPtr &overrides,
	const UT_StringHolder &prim_path,
	const UT_StringHolder &prim_name)
    : HUSD_ObjectHandle(prim_path, prim_name),
      myDataHandle(data_handle),
      myOverrides(overrides)
{
}

HUSD_PrimHandle::~HUSD_PrimHandle()
{
}

HUSD_PrimStatus
HUSD_PrimHandle::getStatus() const
{
    if (name() == theRootPrimName)
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

    if (lock.obj())
	prim_type = lock.obj().GetTypeName().GetText();

    return prim_type;
}

UT_StringHolder
HUSD_PrimHandle::getVariantInfo() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);
    UT_String			 prim_variant_info;

    if (lock.obj())
    {
	auto	 vsets = lock.obj().GetVariantSets();

	for (auto &&vset : vsets.GetNames())
	{
	    if (prim_variant_info.isstring())
		prim_variant_info.append(", ");
	    prim_variant_info.append(vset.c_str());
	    prim_variant_info.append(" : ");
	    prim_variant_info.append(vsets[vset].GetVariantSelection().c_str());
	}
    }

    return prim_variant_info;
}

UT_StringHolder
HUSD_PrimHandle::getKind() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);

    if (lock.obj())
    {
	UsdModelAPI		 mdlapi(lock.obj());
	TfToken			 kind;

	if (mdlapi && mdlapi.GetKind(&kind))
	    return UT_StringHolder(kind.GetText());
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
	UsdGeomModelAPI		 mdlapi(lock.obj());
	TfToken			 drawmode = mdlapi.ComputeModelDrawMode();

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

UT_StringHolder
HUSD_PrimHandle::getPurpose() const
{
    XUSD_AutoObjectLock<UsdGeomImageable>	 lock(*this);

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
HUSD_PrimHandle::getIcon() const
{
    HUSD_AutoReadLock		 readlock(myDataHandle, myOverrides);
    HUSD_Info			 info(readlock);

    return info.getIcon(path());
}

HUSD_PrimAttribState
HUSD_PrimHandle::getActive() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(*this);
    HUSD_PrimAttribState	 active = HUSD_NOTAPPLICABLE;

    if (lock.obj() && !lock.obj().IsPseudoRoot())
    {
	SdfLayerHandle overridelayer;

	active = lock.obj().IsActive() ? HUSD_TRUE : HUSD_FALSE;

	if (myOverrides)
	{
	    for (int i = 0; i < HUSD_OVERRIDES_NUM_LAYERS; i++)
	    {
		overridelayer = myOverrides->data().
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
	    UsdAttribute	 visattr = imageable.GetVisibilityAttr();

	    visible = (imageable.ComputeVisibility(usdtime) !=
		       UsdGeomTokens->invisible) ? HUSD_TRUE : HUSD_FALSE;
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
        std::vector<std::string>     paths;

        if (lock.obj().IsA<UsdLuxLight>())
        {
            if (myOverrides &&
                !myOverrides->isEmpty(HUSD_OVERRIDES_SOLO_LIGHTS_LAYER))
            {
                std::string path = lock.obj().GetPath().GetString();

                myOverrides->getSoloLights(paths);
                if (std::find(paths.begin(), paths.end(), path) == paths.end())
                    state = HUSD_SOLO_FALSE;
                else
                    state = HUSD_SOLO_TRUE;
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
                if (std::find(paths.begin(), paths.end(), path) == paths.end())
                    state = HUSD_SOLO_FALSE;
                else
                    state = HUSD_SOLO_TRUE;
            }
            else
                state = HUSD_SOLO_NOSOLO;
        }
    }

    return state;
}

int64
HUSD_PrimHandle::getDescendants(HUSD_PrimTraversalDemands demands) const
{
    XUSD_AutoObjectLock<UsdPrim>     lock(*this);
    int64                            descendants = 0;

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
HUSD_PrimHandle::hasAnyOverrides() const
{
    XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

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

bool
HUSD_PrimHandle::hasPayload() const
{
    XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

    if (!lock.obj())
	return false;
    return lock.obj().HasAuthoredPayloads();
}

bool
HUSD_PrimHandle::isDefined() const
{
    XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

    if (lock.obj() && lock.obj().IsDefined())
	return true;

    return false;
}

bool
HUSD_PrimHandle::hasChildren(HUSD_PrimTraversalDemands demands) const
{
    XUSD_AutoObjectLock<UsdPrim>	 lock(*this);

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

    if (lock.obj())
    {
	auto p(HUSDgetUsdPrimPredicate(demands));

	for (auto &&child : lock.obj().
		GetFilteredChildren(UsdTraverseInstanceProxies(p)))
	{
	    children.append(
		HUSD_PrimHandle(dataHandle(),
		overrides(),
		child.GetPath().GetString(),
		child.GetPath().GetName()));
	}
    }
}

void
HUSD_PrimHandle::getProperties(UT_Array<HUSD_PropertyHandle> &props,
	bool include_attributes,
	bool include_relationships) const
{
    HUSD_AutoReadLock		 readlock(myDataHandle, myOverrides);
    HUSD_Info			 info(readlock);
    UT_ArrayStringSet		 prop_names;

    if (include_attributes)
	info.getAttributeNames(path(), prop_names);
    if (include_relationships)
	info.getRelationshipNames(path(), prop_names);

    for (auto &&prop_name : prop_names)
	props.append(HUSD_PropertyHandle(*this, prop_name));
}

void
HUSD_PrimHandle::updateOverrides(const HUSD_ConstOverridesPtr &overrides)
{
    myOverrides = overrides;
}

void
HUSD_PrimHandle::getAttributeNames(UT_ArrayStringSet &attrib_names) const
{
    HUSD_AutoReadLock		 readlock(myDataHandle, myOverrides);
    HUSD_Info			 info(readlock);

    info.getAttributeNames(path(), attrib_names);
}

void
HUSD_PrimHandle::extractAttributes(
	const UT_ArrayStringSet &which_attribs,
	const HUSD_TimeCode &tc,
	UT_Options &values)
{
    HUSD_AutoReadLock		 readlock(myDataHandle, myOverrides);
    HUSD_Info			 info(readlock);

    info.extractAttributes(path(), which_attribs, tc, values);
}

