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
#include "HUSD_DataHandle.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_MirrorRootLayerData.h"
#include "XUSD_Utils.h"
#include <UT/UT_JSONWriter.h>
#include <iostream>
#include <gusd/UT_Gf.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/relationshipSpec.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usd/stage.h>

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

HUSD_MirrorRootLayer::HUSD_MirrorRootLayer(
        const UT_StringRef &freecamsavepath /*=UT_StringRef()*/)
    : myData(new XUSD_MirrorRootLayerData(freecamsavepath)),
      myViewportCameraCreated(false)
{
}

HUSD_MirrorRootLayer::~HUSD_MirrorRootLayer()
{
}

void
HUSD_MirrorRootLayer::CameraParms::dump() const
{
    UT_AutoJSONWriter   w(std::cerr, false);
    dump(w);
}

void
HUSD_MirrorRootLayer::CameraParms::dump(UT_JSONWriter &w) const
{
    w.jsonBeginMap();
    w.jsonKeyToken("xform");
    w.jsonUniformArray(16, myXform.data());
    w.jsonKeyValue("myFocalLength", myFocalLength);
    w.jsonKeyValue("myHAperture", myHAperture);
    w.jsonKeyValue("myHApertureOffset", myHApertureOffset);
    w.jsonKeyValue("myVAperture", myVAperture);
    w.jsonKeyValue("myVApertureOffset", myVApertureOffset);
    w.jsonKeyValue("myNearClip", myNearClip);
    w.jsonKeyValue("myFarClip", myFarClip);
    w.jsonKeyValue("myIsOrtho", myIsOrtho);
    w.jsonKeyValue("mySetCamParms", mySetCamParms);
    w.jsonKeyValue("mySetCropParms", mySetCropParms);
    w.jsonEndMap();
}

void
HUSD_MirrorRootLayer::clear()
{
    // Rather than actually clearing the mirror root layer, we actually want
    // to just reinitialize it to its default values.
    myData->initializeLayerData();
    myViewportCameraCreated = false;
}

XUSD_MirrorRootLayerData &
HUSD_MirrorRootLayer::data() const
{
    return *myData;
}

void
HUSD_MirrorRootLayer::createViewportCamera(
        const HUSD_DataHandle &datahandle,
        const UT_StringRef &refcamera,
        const CameraParms &camparms,
        const HUSD_TimeCode &timecode)
{
    static std::set<TfToken> theSkipAttributes({
        TfToken("karma:camera:use_lensshader", TfToken::Immortal),
        TfToken("karma:camera:lensshadervop", TfToken::Immortal)
    });

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

        if (!myViewportCameraCreated)
        {
            if (myData->cameraLayer())
            {
                SdfReference r(myData->cameraLayer()->GetIdentifier(), campath);
                primspec->GetReferenceList().GetExplicitItems().push_back(r);
            }
            else
                primspec->SetTypeName("Camera");
            myViewportCameraCreated = true;
        }

        if (refcamera.isstring())
        {
            HUSD_AutoReadLock lock(datahandle);

            if (lock.data() && lock.data()->isStageValid())
            {
                UsdStageRefPtr stage = lock.data()->stage();
                SdfPath refcamerapath = HUSDgetSdfPath(refcamera);
                UsdPrim refcameraprim = stage->GetPrimAtPath(refcamerapath);
                UsdTimeCode usdtimecode = HUSDgetUsdTimeCode(timecode);

                // We don't want to copy attributes from light primitives.
                if (refcameraprim && refcameraprim.IsA<UsdGeomCamera>())
                {
                    // If mySetCamParms is false and mySetCropParms is true,
                    // then we're doing a render region from the camera and
                    // want to keep DOF. Otherwise we're tumbling free and need
                    // to clear fStop.
                    bool disable_dof = camparms.mySetCamParms ||
                        !camparms.mySetCropParms;

                    // We have an actual USD camera primitive to copy from.
                    // Grab all its property values (including the exact prim
                    // type) and copy them to the free camera primitive.
                    primspec->SetTypeName(refcameraprim.GetTypeName());
                    for (auto &&attr : refcameraprim.GetAttributes())
                    {
                        if (theSkipAttributes.find(attr.GetName()) !=
                            theSkipAttributes.end())
                            continue;

                        SdfPath attrpath = SdfPath::ReflexiveRelativePath().
                            AppendProperty(attr.GetName());
                        SdfAttributeSpecHandle attrspec =
                            primspec->GetAttributeAtPath(attrpath);

                        if (!attrspec)
                            attrspec = SdfAttributeSpec::New(
                                primspec,
                                attr.GetName(),
                                attr.GetTypeName(),
                                attr.GetVariability(),
                                attr.IsCustom());

                        UT_ASSERT(attrspec);
                        if (attrspec)
                        {
                            if (attr.GetName() == UsdGeomTokens->fStop &&
                                disable_dof)
                            {
                                attrspec->SetDefaultValue(VtValue(0.0f));
                            }
                            else
                            {
                                VtValue value;

                                attr.Get(&value, usdtimecode);
                                attrspec->SetDefaultValue(value);
                            }
                        }
                    }
                    for (auto &&rel : refcameraprim.GetRelationships())
                    {
                        SdfPath relpath = SdfPath::ReflexiveRelativePath().
                            AppendProperty(rel.GetName());
                        SdfRelationshipSpecHandle relspec =
                            primspec->GetRelationshipAtPath(relpath);

                        if (!relspec)
                            relspec = SdfRelationshipSpec::New(
                                primspec,
                                rel.GetName(),
                                rel.IsCustom());

                        UT_ASSERT(relspec);
                        if (relspec)
                        {
                            SdfPathVector targets;
                            auto explicit_targets =
                                relspec->GetTargetPathList().GetExplicitItems();

                            rel.GetTargets(&targets);
                            explicit_targets.insert(explicit_targets.begin(),
                                targets.begin(), targets.end());
                        }
                    }
                }
            }
        }

        SdfAttributeSpecHandle attrspec;

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
    }
}

size_t
format(char *buf, size_t sz, const HUSD_MirrorRootLayer::CameraParms &p)
{
    UT_WorkBuffer       tmp;
    UT_AutoJSONWriter   w(tmp);
    p.dump(*w);
    UT::Format::Writer  writer(buf, sz);
    UT::Format::Formatter<>     f;
    return f.format(writer, "{}", {tmp});
}
