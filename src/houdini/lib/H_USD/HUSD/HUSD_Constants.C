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
    static constexpr UT_StringLit s("/");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getRenderSettingsRootPrimPath()
{
    static constexpr UT_StringLit s("/Render");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getHoudiniRendererPluginName()
{
    static constexpr UT_StringLit s("HD_HoudiniRendererPlugin");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getKarmaRendererPluginName()
{
    static constexpr UT_StringLit s("BRAY_HdKarma");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getHydraRendererPluginName()
{
    static constexpr UT_StringLit s("HdStormRendererPlugin");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getXformPrimType()
{
    static constexpr UT_StringLit s("UsdGeomXform");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getScopePrimType()
{
    static constexpr UT_StringLit s("UsdGeomScope");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getLuxLightPrimType()
{
    static constexpr UT_StringLit s("UsdLuxLight");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getGeomCameraPrimType()
{
    static constexpr UT_StringLit s("UsdGeomCamera");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getGeomGprimPrimType()
{
    static constexpr UT_StringLit s("UsdGeomGprim");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getGeomBoundablePrimType()
{
    static constexpr UT_StringLit s("UsdGeomBoundable");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getGeomImageablePrimType()
{
    static constexpr UT_StringLit s("UsdGeomImageable");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getVolumePrimType()
{
    static constexpr UT_StringLit s("UsdVolVolume");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getOpenVDBAssetPrimType()
{
    static constexpr UT_StringLit s("UsdVolOpenVDBAsset");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getHoudiniFieldAssetPrimType()
{
    static constexpr UT_StringLit s("UsdHoudiniHoudiniFieldAsset");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getPointInstancerPrimType()
{
    static constexpr UT_StringLit s("UsdGeomPointInstancer");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getMaterialPrimTypeName()
{
    static constexpr UT_StringLit s("Material");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getShaderPrimTypeName()
{
    static constexpr UT_StringLit s("Shader");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getAutomaticPrimIdentifier()
{
    static constexpr UT_StringLit s("automaticPrim");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getDefaultPrimIdentifier()
{
    static constexpr UT_StringLit s("defaultPrim");

    return s.asHolder();
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
    static constexpr UT_StringLit s("/HoudiniLayerInfo");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getHoudiniLayerInfoPrimName()
{
    static constexpr UT_StringLit s("HoudiniLayerInfo");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getHoudiniLayerInfoPrimType()
{
    static constexpr UT_StringLit s("HoudiniLayerInfo");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getHoudiniFreeCameraPrimPath()
{
    static constexpr UT_StringLit s("/__HoudiniFreeCamera__");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getSaveControlExplicit()
{
    static constexpr UT_StringLit s("Explicit");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getSaveControlPlaceholder()
{
    static constexpr UT_StringLit s("Placeholder");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getSaveControlIsFileFromDisk()
{
    static constexpr UT_StringLit s("IsFileFromDisk");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getSaveControlDoNotSave()
{
    static constexpr UT_StringLit s("DoNotSave");

    return s.asHolder();
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
HUSD_Constants::getAttributeExtentsHint()
{
    static const UT_StringHolder s(UsdGeomTokens->extentsHint);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getAttributeDrawModeColor()
{
    static const UT_StringHolder s(UsdGeomTokens->modelDrawModeColor);

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
HUSD_Constants::getEditOpAppendFront()
{
    static constexpr UT_StringLit s("appendfront");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getEditOpAppendBack()
{
    static constexpr UT_StringLit s("appendback");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getEditOpPrependFront()
{
    static constexpr UT_StringLit s("prependfront");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getEditOpPrependBack()
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
HUSD_Constants::getFakeKindXform()
{
    static constexpr UT_StringLit s("__xform__");

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
HUSD_Constants::getCollectionPrefix()
{
    static constexpr UT_StringLit s("collection:");

    return s.asHolder();
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
    static constexpr UT_StringLit s("path,name");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getDefaultBgeoAttribPattern()
{
    static constexpr UT_StringLit s("* ^__* ^usd*");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getDefaultBgeoPathPrefix()
{
    static constexpr UT_StringLit s("/Geometry");

    return s.asHolder();
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

const UT_StringHolder &
HUSD_Constants::getInvisible()
{
    static const UT_StringHolder s(UsdGeomTokens->invisible);

    return s;
}

const UT_StringHolder &
HUSD_Constants::getIconCustomDataName()
{
    static constexpr UT_StringLit s("icon");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getBlockVariantValue()
{
    static constexpr UT_StringLit s("<block>");

    return s.asHolder();
}

const UT_StringHolder &
HUSD_Constants::getVolumeSopSuffix()
{
    static constexpr UT_StringLit s(".volumes");

    return s.asHolder();
}

