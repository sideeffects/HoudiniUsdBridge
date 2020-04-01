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

#include "HUSD_Constants.h"
#include <gusd/USD_Utils.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/usd/usd/clipsAPI.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/kind/registry.h>

PXR_NAMESPACE_USING_DIRECTIVE

const UT_StringHolder &
HUSD_Constants::getRootPrimPath()
{
    static const UT_StringHolder s("/");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getRenderSettingsRootPrimPath()
{
    static const UT_StringHolder s("/Render");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getHoudiniRendererPluginName()
{
    static const UT_StringHolder s("HD_HoudiniRendererPlugin");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getKarmaRendererPluginName()
{
    static const UT_StringHolder s("BRAY_HdKarma");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getHydraRendererPluginName()
{
    static const UT_StringHolder s("HdStormRendererPlugin");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getXformPrimType()
{
    static const UT_StringHolder s("UsdGeomXform");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getScopePrimType()
{
    static const UT_StringHolder s("UsdGeomScope");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getLuxLightPrimType()
{
    static const UT_StringHolder s("UsdLuxLight");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getGeomGprimPrimType()
{
    static const UT_StringHolder s("UsdGeomGprim");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getGeomBoundablePrimType()
{
    static const UT_StringHolder s("UsdGeomBoundable");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getGeomImageablePrimType()
{
    static const UT_StringHolder s("UsdGeomImageable");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getVolumePrimType()
{
    static const UT_StringHolder s("UsdVolVolume");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getOpenVDBAssetPrimType()
{
    static const UT_StringHolder s("UsdVolOpenVDBAsset");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getHoudiniFieldAssetPrimType()
{
    static const UT_StringHolder s("UsdHoudiniHoudiniFieldAsset");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getPointInstancerPrimType()
{
    static const UT_StringHolder s("UsdGeomPointInstancer");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAutomaticPrimIdentifier()
{
    static const UT_StringHolder
        s("automaticPrim");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getDefaultPrimIdentifier()
{
    static const UT_StringHolder
        s("defaultPrim");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getDefaultClipSetName()
{
    static const UT_StringHolder s(UsdClipsAPISetNames->default_);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getHoudiniLayerInfoPrimPath()
{
    static const UT_StringHolder s("/HoudiniLayerInfo");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getHoudiniLayerInfoPrimName()
{
    static const UT_StringHolder s("HoudiniLayerInfo");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getHoudiniLayerInfoPrimType()
{
    static const UT_StringHolder s("HoudiniLayerInfo");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getSaveControlExplicit()
{
    static const UT_StringHolder s("Explicit");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getSaveControlPlaceholder()
{
    static const UT_StringHolder s("Placeholder");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getSaveControlIsFileFromDisk()
{
    static const UT_StringHolder s("IsFileFromDisk");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getSaveControlDoNotSave()
{
    static const UT_StringHolder s("DoNotSave");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributePointProtoIndices()
{
    static const UT_StringHolder s(UsdGeomTokens->protoIndices);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributePointIds()
{
    static const UT_StringHolder s(UsdGeomTokens->ids);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributePointInvisibleIds()
{
    static const UT_StringHolder s(UsdGeomTokens->invisibleIds);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributePointPositions()
{
    static const UT_StringHolder s(UsdGeomTokens->positions);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributePointOrientations()
{
    static const UT_StringHolder s(UsdGeomTokens->orientations);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributePointScales()
{
    static const UT_StringHolder s(UsdGeomTokens->scales);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributePointVelocities()
{
    static const UT_StringHolder s(UsdGeomTokens->velocities);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributePointAngularVelocities()
{
    static const UT_StringHolder s(UsdGeomTokens->angularVelocities);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributePoints()
{
    static const UT_StringHolder s(UsdGeomTokens->points);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getRelationshipPrototypes()
{
    static const UT_StringHolder s(UsdGeomTokens->prototypes);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributeVolumeFieldFilePath()
{
    static const UT_StringHolder s(UsdVolTokens->filePath);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributeVolumeFieldIndex()
{
    static const UT_StringHolder s(UsdVolTokens->fieldIndex);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributeVolumeFieldName()
{
    static const UT_StringHolder s(UsdVolTokens->fieldName);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getPrimSpecifierDefine()
{
    static constexpr UT_StringLit s("def");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getPrimSpecifierOverride()
{
    static constexpr UT_StringLit s("over");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getPrimSpecifierClass()
{
    static constexpr UT_StringLit s("class");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getReferenceTypeFile()
{
    static constexpr UT_StringLit s("file");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getReferenceTypePayload()
{
    static constexpr UT_StringLit s("payload");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getReferenceTypePrim()
{
    static constexpr UT_StringLit s("prim");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getReferenceTypeInherit()
{
    static constexpr UT_StringLit s("inherit");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getReferenceTypeSpecialize()
{
    static constexpr UT_StringLit s("specialize");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getReferenceEditOpAppendFront()
{
    static constexpr UT_StringLit s("appendfront");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getReferenceEditOpAppendBack()
{
    static constexpr UT_StringLit s("appendback");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getReferenceEditOpPrependFront()
{
    static constexpr UT_StringLit s("prependfront");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getReferenceEditOpPrependBack()
{
    static constexpr UT_StringLit s("prependback");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getPurposeDefault()
{
    static const UT_StringHolder s(UsdGeomTokens->default_);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getPurposeProxy()
{
    static const UT_StringHolder s(UsdGeomTokens->proxy);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getPurposeRender()
{
    static const UT_StringHolder s(UsdGeomTokens->render);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getPurposeGuide()
{
    static const UT_StringHolder s(UsdGeomTokens->guide);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getKindSubComponent()
{
    static const UT_StringHolder s(KindTokens->subcomponent);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getKindComponent()
{
    static const UT_StringHolder s(KindTokens->component);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getKindGroup()
{
    static const UT_StringHolder s(KindTokens->group);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getKindAssembly()
{
    static const UT_StringHolder s(KindTokens->assembly);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getKindAutomatic()
{
    static constexpr UT_StringLit s("__automatic__");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getDrawModeDefault()
{
    static const UT_StringHolder s(UsdGeomTokens->default_);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getDrawModeOrigin()
{
    static const UT_StringHolder s(UsdGeomTokens->origin);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getDrawModeBounds()
{
    static const UT_StringHolder s(UsdGeomTokens->bounds);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getDrawModeCards()
{
    static const UT_StringHolder s(UsdGeomTokens->cards);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getExpansionExplicit()
{
    static const UT_StringHolder s(UsdTokens->explicitOnly);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getExpansionExpandPrims()
{
    static const UT_StringHolder s(UsdTokens->expandPrims);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getExpansionExpandPrimsAndProperties()
{
    static const UT_StringHolder s(UsdTokens->expandPrimsAndProperties);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getDefaultBgeoPathAttr()
{
    static const UT_StringHolder s("path,name");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getDefaultBgeoAttribPattern()
{
    static const UT_StringHolder
	s("* ^__* ^usd*");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getDefaultBgeoPathPrefix()
{
    static const UT_StringHolder
	s("/Geometry");

    return s;
}

const UT_StringHolder &
HUSD_Constants::getMatPurposeAll()
{
    static const UT_StringHolder s(UsdShadeTokens->allPurpose);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getMatPurposeFull()
{
    static const UT_StringHolder s(UsdShadeTokens->full);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getMatPurposePreview()
{
    static const UT_StringHolder s(UsdShadeTokens->preview);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getInterpolationConstant()
{
    static const UT_StringHolder s(UsdGeomTokens->constant);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getInterpolationVarying()
{
    static const UT_StringHolder s(UsdGeomTokens->varying);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getUpAxisY()
{
    static const UT_StringHolder s(UsdGeomTokens->y);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getUpAxisZ()
{
    static const UT_StringHolder s(UsdGeomTokens->z);

    return s;
}

