//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "UHI_GeometryLightAdapter.h"
#include <HUSD/XUSD_Tokens.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImaging/indexProxy.h>
#include <pxr/usdImaging/usdImaging/tokens.h>
#include <pxr/usd/usdLux/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    typedef UsdHImagingGeometryLightAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
}

UsdHImagingGeometryLightAdapter::~UsdHImagingGeometryLightAdapter() 
{
}

bool
UsdHImagingGeometryLightAdapter::IsSupported(
        UsdImagingIndexProxy const* index) const
{
    return index->IsSprimTypeSupported(
	HusdHdPrimTypeTokens()->sprimGeometryLight);
}

SdfPath
UsdHImagingGeometryLightAdapter::Populate(UsdPrim const& prim, 
                            UsdImagingIndexProxy* index,
                            UsdImagingInstancerContext const* instancerContext)
{
    SdfPath cachePath = prim.GetPath();
    if (index->IsPopulated(cachePath))
        return cachePath;

    // add the geometry reference path as a string attribute, to bypass 
    // limitations in hydra (hydra cannot query relationship information from 
    // lights :/)
    UsdPrim tmpPrim = prim;
    if (UsdRelationship geometryRel = 
	prim.GetRelationship(UsdLuxTokens->geometry))
    {
	SdfPathVector targets;
	if ( geometryRel.GetTargets(&targets) && !targets.empty() )
	{
	    if (UsdPrim geoPrim = _GetPrim(targets[0]))
	    {
		if (geoPrim.GetTypeName().GetString() == "Xform")
		{
		    UsdPrim::SiblingRange children = geoPrim.GetAllChildren();
		    if (!children.empty())
			geoPrim = *children.begin();
		}

		// set the geometryPath string attribute on the light
		UsdAttribute geometryPathAttr =
		    tmpPrim.CreateAttribute(TfToken("geometryPath"),
					    SdfValueTypeNames->String);
		if (geometryPathAttr)
		    geometryPathAttr.Set(geoPrim.GetPath().GetString());

		//  also put flag on geoemtry
		//  TODO: somehow remove flag off old geometry
		UsdAttribute isAreaLightGeoAttr = geoPrim.CreateAttribute(
		    TfToken("karma:object:isarealightgeo"),
		    SdfValueTypeNames->Bool);
		if (isAreaLightGeoAttr)
		{
		    isAreaLightGeoAttr.Set(true);
		    index->MarkRprimDirty(
			geoPrim.GetPath(),
			HdChangeTracker::DirtyParams);
		}
	    }
	}
    }

    index->InsertSprim(
	HusdHdPrimTypeTokens()->sprimGeometryLight, cachePath, tmpPrim);
    HD_PERF_COUNTER_INCR(UsdImagingTokens->usdPopulatedPrimCount);

    return cachePath;
}

void
UsdHImagingGeometryLightAdapter::_RemovePrim(SdfPath const& cachePath,
                                         UsdImagingIndexProxy* index)
{
    index->RemoveSprim(
	HusdHdPrimTypeTokens()->sprimGeometryLight, cachePath);
}

PXR_NAMESPACE_CLOSE_SCOPE

