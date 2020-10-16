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

#include "HUSD_MirrorRootLayer.h"
#include "HUSD_Constants.h"
#include "XUSD_MirrorRootLayerData.h"
#include "XUSD_Utils.h"
#include <gusd/UT_Gf.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    template <typename T>
    void
    setSdfAttribute(const SdfPrimSpecHandle &primspec,
                    const TfToken &attrname,
                    const SdfValueTypeName &attrtype,
                    T value)
    {
        SdfPath attrpath = SdfPath::ReflexiveRelativePath().
            AppendProperty(attrname);
        SdfAttributeSpecHandle attrspec;

        if (!(attrspec = primspec->GetAttributeAtPath(attrpath)))
            attrspec = SdfAttributeSpec::New(primspec, attrname, attrtype,
                SdfVariabilityVarying);
        if (attrspec)
            attrspec->SetDefaultValue(VtValue(value));
    }

    void
    clearSdfAttribute(const SdfPrimSpecHandle &primspec,
                      const TfToken &attrname)
    {
        SdfPath attrpath = SdfPath::ReflexiveRelativePath().
            AppendProperty(attrname);

        SdfAttributeSpecHandle attrspec = primspec->
            GetAttributeAtPath(attrpath);
        if (attrspec)
            primspec->RemoveProperty(attrspec);
    }
}

HUSD_MirrorRootLayer::HUSD_MirrorRootLayer()
    : myData(new XUSD_MirrorRootLayerData())
{
}

HUSD_MirrorRootLayer::~HUSD_MirrorRootLayer()
{
}

void
HUSD_MirrorRootLayer::clear()
{
    myData->layer()->Clear();
}

XUSD_MirrorRootLayerData &
HUSD_MirrorRootLayer::data() const
{
    return *myData;
}

void
HUSD_MirrorRootLayer::createViewportCamera(
        const UT_StringRef &refcamera,
        const CameraParms &camparms)
{
    auto     campath = HUSDgetHoudiniFreeCameraSdfPath();
    auto     layer = myData->layer();
    auto     primspec = layer->GetPrimAtPath(campath);

    if (primspec)
    {
        VtTokenArray xformops( { TfToken("xformOp:transform") } );
        SdfPath xformpath = SdfPath::ReflexiveRelativePath().
            AppendProperty(xformops[0]);
        SdfPath xformorderpath = SdfPath::ReflexiveRelativePath().
            AppendProperty(UsdGeomTokens->xformOpOrder);
        SdfAttributeSpecHandle attrspec;

        primspec->GetReferenceList().GetExplicitItems().clear();
        if (refcamera.isstring())
        {
            SdfPath      refcamerapath = HUSDgetSdfPath(refcamera);
            SdfReference ref(std::string(), refcamerapath);

            primspec->GetReferenceList().GetExplicitItems().push_back(ref);
        }
        else if (myData->cameraLayer())
        {
            SdfReference ref(myData->cameraLayer()->GetIdentifier(), campath);

            primspec->GetReferenceList().GetExplicitItems().push_back(ref);
        }
        else
            primspec->SetTypeName("Camera");

        // Transform.
        if (!(attrspec = primspec->GetAttributeAtPath(xformpath)))
            attrspec = SdfAttributeSpec::New(primspec,
                xformops[0],
                SdfValueTypeNames->Matrix4d,
                SdfVariabilityVarying);
        if (attrspec)
        {
            attrspec->SetDefaultValue(
                VtValue(GusdUT_Gf::Cast(camparms.myXform)));
            if (!(attrspec = primspec->GetAttributeAtPath(xformorderpath)))
                attrspec = SdfAttributeSpec::New(primspec,
                    UsdGeomTokens->xformOpOrder,
                    SdfValueTypeNames->TokenArray,
                    SdfVariabilityUniform);
            if (attrspec)
                attrspec->SetDefaultValue(VtValue(xformops));
        }


        if(camparms.mySetCamParms || camparms.mySetCropParms)
        {
            float hap  = (float)camparms.myHAperture;
            float vap  = (float)camparms.myVAperture;
            float hapo = (float)camparms.myHApertureOffset;
            float vapo = (float)camparms.myVApertureOffset;

            setSdfAttribute(primspec,
                            UsdGeomTokens->horizontalAperture,
                            SdfValueTypeNames->Float,
                            hap);
            setSdfAttribute(primspec,
                            UsdGeomTokens->verticalAperture,
                            SdfValueTypeNames->Float,
                            vap);
            setSdfAttribute(primspec,
                            UsdGeomTokens->horizontalApertureOffset,
                            SdfValueTypeNames->Float,
                            hapo);
            setSdfAttribute(primspec,
                            UsdGeomTokens->verticalApertureOffset,
                            SdfValueTypeNames->Float,
                            vapo);
        }
        else
        {
            clearSdfAttribute(primspec,UsdGeomTokens->horizontalAperture);
            clearSdfAttribute(primspec,UsdGeomTokens->verticalAperture);
            clearSdfAttribute(primspec,UsdGeomTokens->horizontalApertureOffset);
            clearSdfAttribute(primspec,UsdGeomTokens->verticalApertureOffset);
        }
        
        if(camparms.mySetCamParms)
        {
            setSdfAttribute(primspec,
                            UsdGeomTokens->focalLength,
                            SdfValueTypeNames->Float,
                            (float)camparms.myFocalLength);
            setSdfAttribute(primspec,
                            UsdGeomTokens->clippingRange,
                            SdfValueTypeNames->Float2,
                            GfVec2f(camparms.myNearClip, camparms.myFarClip));
            setSdfAttribute(primspec,
                            UsdGeomTokens->projection,
                            SdfValueTypeNames->Token,
                            camparms.myIsOrtho
                            ? UsdGeomTokens->orthographic
                            : UsdGeomTokens->perspective);
        }
        else
        {
            clearSdfAttribute(primspec,UsdGeomTokens->focalLength);
            clearSdfAttribute(primspec,UsdGeomTokens->clippingRange);
            clearSdfAttribute(primspec,UsdGeomTokens->projection);
        }
    }
}

