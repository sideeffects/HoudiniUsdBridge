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
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/relationshipSpec.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usd/tokens.h>
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
}

HUSD_MirrorRootLayer::HUSD_MirrorRootLayer(
        const UT_StringRef &freecamsavepath /*=UT_StringRef()*/)
    : myData(new XUSD_MirrorRootLayerData(freecamsavepath))
{
    createEmptyViewportCamera();
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
    if (!myXformSamples.isEmpty())
    {
        w.jsonBeginArray();
        for (int i = 0; i < myXformSamples.size(); i++)
        {
            w.jsonBeginMap();
            w.jsonKeyValue("time", myXformSampleTimes[i]);
            w.jsonKeyToken("xform");
            w.jsonUniformArray(16, myXformSamples[i].data());
            w.jsonEndMap();
        }
        w.jsonEndArray();
    }
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
    w.jsonKeyValue("myPreserveDepthOfField", myPreserveDepthOfField);
    w.jsonEndMap();
}

void
HUSD_MirrorRootLayer::clear()
{
    // Rather than actually clearing the mirror root layer, we want
    // to just reinitialize it to its default values.
    myData->initializeLayerData();
    createEmptyViewportCamera();
}

XUSD_MirrorRootLayerData &
HUSD_MirrorRootLayer::data() const
{
    return *myData;
}

void
HUSD_MirrorRootLayer::createEmptyViewportCamera()
{
    auto     campath = HUSDgetHoudiniFreeCameraSdfPath();
    auto     layer = myData->layer();
    auto     primspec = layer->GetPrimAtPath(campath);

    if (primspec)
    {
        if (myData->cameraLayer())
        {
            SdfReference r(myData->cameraLayer()->GetIdentifier(), campath);
            primspec->GetReferenceList().GetExplicitItems().push_back(r);
        }
        else
            primspec->SetTypeName("Camera");
    }
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
    static const std::string theStashSuffix("_stash");
    static const UT_Map<TfToken, TfToken> theCamEffectsAttribs({
        { UsdGeomTokens->fStop,
          TfToken(UsdGeomTokens->fStop.GetString() + theStashSuffix) },
        { UsdGeomTokens->shutterOpen,
          TfToken(UsdGeomTokens->shutterOpen.GetString() + theStashSuffix) },
        { UsdGeomTokens->shutterClose,
          TfToken(UsdGeomTokens->shutterClose.GetString() + theStashSuffix) },
    });

    auto     campath = HUSDgetHoudiniFreeCameraSdfPath();
    auto     layer = myData->layer();
    auto     primspec = layer->GetPrimAtPath(campath);

    if (primspec)
    {
        SdfChangeBlock changeblock;
        VtTokenArray xformops( { TfToken("xformOp:transform") } );
        SdfPath xformpath = SdfPath::ReflexiveRelativePath().
            AppendProperty(xformops[0]);
        SdfPath xformorderpath = SdfPath::ReflexiveRelativePath().
            AppendProperty(UsdGeomTokens->xformOpOrder);

        if (refcamera.isstring())
        {
            HUSD_AutoReadLock lock(datahandle,
                                   HUSD_AutoReadLock::OVERRIDES_UNCHANGED);

            if (lock.data() && lock.data()->isStageValid())
            {
                UsdStageRefPtr stage = lock.data()->stage();
                SdfPath refcamerapath = HUSDgetSdfPath(refcamera);
                UsdPrim refcameraprim = stage->GetPrimAtPath(refcamerapath);
                UsdTimeCode usdtimecode = HUSDgetUsdTimeCode(timecode);

                // We don't want to copy attributes from light primitives.
                if (refcameraprim && refcameraprim.IsA<UsdGeomCamera>())
                {
                    // Clear any properties from the free camera that do not
                    // exist on the reference camera.
                    for (auto &&prop : primspec->GetProperties())
                        if (!refcameraprim.HasProperty(prop->GetNameToken()))
                            primspec->RemoveProperty(prop);

                    TfTokenVector apis = refcameraprim.GetAppliedSchemas();

                    // We have an actual USD camera primitive to copy from.
                    // Grab all its property values (including the exact prim
                    // type and any API schemas) and copy them to the free
                    // camera primitive.
                    primspec->SetTypeName(refcameraprim.GetTypeName());
                    primspec->SetField(UsdTokens->apiSchemas,
                        VtValue(SdfTokenListOp::CreateExplicit(apis)));
                    for (auto &&attr : refcameraprim.GetAttributes())
                    {
                        if (theSkipAttributes.find(attr.GetName()) !=
                            theSkipAttributes.end())
                            continue;

                        // We force a zero fStop to eliminate depth of field
                        // unless we have been explicitly instructed not to.
                        // This happens in the case of render regions, or
                        // active manipulation of a view locked to the current
                        // camera. Otherwise we're tumbling free and need to
                        // clear the fStop.
                        bool force_zero_fstop =
                            attr.GetName() == UsdGeomTokens->fStop &&
                            !camparms.myPreserveDepthOfField;

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
                            VtValue value;
                            attr.Get(&value, usdtimecode);

                            auto it = theCamEffectsAttribs.find(attr.GetName());
                            if (it != theCamEffectsAttribs.end())
                            {
                                SdfPath stashattrpath =
                                    SdfPath::ReflexiveRelativePath().
                                        AppendProperty(it->second);
                                SdfAttributeSpecHandle stashattrspec =
                                    primspec->GetAttributeAtPath(stashattrpath);
                                if (!stashattrspec)
                                    stashattrspec = SdfAttributeSpec::New(
                                        primspec,
                                        it->second,
                                        attr.GetTypeName(),
                                        attr.GetVariability(),
                                        attr.IsCustom());

                                // Stash the value most recently pulled from a
                                // real camera so we can continue to use it
                                // even if camera effects get turned off (which
                                // will clear the most recent values) then
                                // turned on again (when we no longer have a
                                // link back to the most recent camera).
                                stashattrspec->SetDefaultValue(value);
                                if (camparms.myDoCamEffects && !force_zero_fstop)
                                    attrspec->SetDefaultValue(value);
                                else
                                    attrspec->SetDefaultValue(VtValue(0.0f));
                            }
                            else
                                attrspec->SetDefaultValue(value);
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
                            explicit_targets.clear();
                            explicit_targets.insert(explicit_targets.begin(),
                                targets.begin(), targets.end());
                        }
                    }
                }
            }
        }
        else if (camparms.myDoCamEffects)
        {
            for (auto &&it : theCamEffectsAttribs)
            {
                SdfPath attrpath = SdfPath::ReflexiveRelativePath().
                    AppendProperty(it.first);
                SdfAttributeSpecHandle attrspec =
                    primspec->GetAttributeAtPath(attrpath);
                SdfPath stashattrpath = SdfPath::ReflexiveRelativePath().
                    AppendProperty(it.second);
                SdfAttributeSpecHandle stashattrspec =
                    primspec->GetAttributeAtPath(stashattrpath);

                if (attrspec && stashattrspec)
                {
                    // We force a zero fStop to eliminate depth of field
                    // unless we have been explicitly instructed not to.
                    // This happens in the case of render regions, or
                    // active manipulation of a view locked to the current
                    // camera. Otherwise we're tumbling free and need to
                    // clear the fStop.
                    bool force_zero_fstop =
                        attrpath.GetName() == UsdGeomTokens->fStop &&
                        !camparms.myPreserveDepthOfField;

                    VtValue value;
                    if (force_zero_fstop)
                        value = VtValue(0.0f);
                    else
                        value = stashattrspec->GetDefaultValue();
                    attrspec->SetDefaultValue(value);
                }
            }
        }
        else
        {
            for (auto &&it : theCamEffectsAttribs)
            {
                SdfPath attrpath = SdfPath::ReflexiveRelativePath().
                    AppendProperty(it.first);
                SdfAttributeSpecHandle attrspec =
                    primspec->GetAttributeAtPath(attrpath);

                if (attrspec)
                    attrspec->SetDefaultValue(VtValue(0.0));
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
            // Set the xform default value.
            attrspec->SetDefaultValue(
                VtValue(GusdUT_Gf::Cast(camparms.myXform)));

            // If we have time samples, set them on the free camera as time
            // samples. Otherwise clear the time samples map.
            if (!camparms.myXformSamples.isEmpty())
            {
                SdfTimeSampleMap timesamples;
                for (int i = 0; i < camparms.myXformSamples.size(); i++)
                    timesamples.emplace(camparms.myXformSampleTimes[i],
                        GusdUT_Gf::Cast(camparms.myXformSamples[i]));
                attrspec->SetField(SdfFieldKeys->TimeSamples, timesamples);
            }
            else
                attrspec->ClearField(SdfFieldKeys->TimeSamples);

            // Set the xformOpOrder to use the xform matrix set above.
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
    UT::Format::Formatter     f;
    return f.format(writer, "{}", {tmp});
}
