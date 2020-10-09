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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraCamera.C (HUSD Library, C++)
 *
 * COMMENTS:	Container for a hydra geometry prim (HdRprim)
 */
#include "XUSD_HydraCamera.h"
#include "XUSD_HydraUtils.h"
#include "HUSD_HydraCamera.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/usdGeom/tokens.h>  // for camera property tokens
#include <pxr/base/vt/value.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/matrix4d.h>
#include <GT/GT_Primitive.h>

#include <UT/UT_Debug.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE


XUSD_HydraCamera::XUSD_HydraCamera(TfToken const& typeId,
				   SdfPath const& primId,
				   HUSD_HydraCamera &cam)
    : HdCamera(primId),
      myCamera(cam)
{
    // UTdebugPrint("Create scene prim ",
    // 		 typeId.GetText(), primId.GetText());
}

XUSD_HydraCamera::~XUSD_HydraCamera()
{
    //UTdebugPrint("Delete scene prim ", GetID().GetText());
}

void
XUSD_HydraCamera::Sync(HdSceneDelegate *del,
		       HdRenderParam *renderParam,
		       HdDirtyBits *dirtyBits)
{
    SdfPath const &id = GetId();

    if (!TF_VERIFY(del))
        return;

    // Change tracking
    HdDirtyBits bits = *dirtyBits;

    if (bits & DirtyViewMatrix)
	myCamera.Transform(XUSD_HydraUtils::fullTransform(del, id));

    if(bits & DirtyProjMatrix)
    {
	fpreal32 hap, vap, ho, vo,fl;
	XUSD_HydraUtils::evalCameraAttrib(hap, del, id,
				    UsdGeomTokens->horizontalAperture);
	XUSD_HydraUtils::evalCameraAttrib(vap, del, id,
				    UsdGeomTokens->verticalAperture);
	XUSD_HydraUtils::evalCameraAttrib(ho, del, id,
				    UsdGeomTokens->horizontalApertureOffset);
	XUSD_HydraUtils::evalCameraAttrib(vo, del, id,
				    UsdGeomTokens->verticalApertureOffset);
        XUSD_HydraUtils::evalCameraAttrib(fl, del, id,
                                          UsdGeomTokens->focalLength);
	TfToken proj;
	XUSD_HydraUtils::evalCameraAttrib(proj, del, id,
				    UsdGeomTokens->projection);
	myCamera.ApertureW(hap);
	myCamera.ApertureH(vap);
	myCamera.Projection(proj.GetText());
        myCamera.ApertureOffsets(UT_Vector2D(ho,vo));
        myCamera.FocalLength(fl);
    }

    if(bits & DirtyParams)
    {
        fpreal32 fd, fs;
        XUSD_HydraUtils::evalCameraAttrib(fd, del, id,
                                          UsdGeomTokens->focusDistance);
        XUSD_HydraUtils::evalCameraAttrib(fs, del, id,
                                          UsdGeomTokens->fStop);
        myCamera.FocusDistance(fd);
        myCamera.FStop(fs);
    }
    
    // Not exactly sure what 'dirty clip planes' refers to, but just in case
    // near far is part of it...
    if (bits & (DirtyClipPlanes | DirtyProjMatrix))
    {
	// Get other attributes from the USD prim through the scene delegate.
	// Then store the resulting values on this object.
	GfRange1f clip;
	XUSD_HydraUtils::evalCameraAttrib(clip, del, id,
				    UsdGeomTokens->clippingRange);
	myCamera.NearClip(clip.GetMin());
	myCamera.FarClip(clip.GetMax());
    }

    if(bits)
	myCamera.bumpVersion();

    // Call base class to sync too
    HdCamera::Sync(del, renderParam, dirtyBits);

    *dirtyBits = Clean;
    myCamera.setInitialized();
}

HdDirtyBits
XUSD_HydraCamera::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE

