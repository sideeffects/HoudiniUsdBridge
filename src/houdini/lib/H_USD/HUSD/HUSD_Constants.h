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

#ifndef __HUSD_Constants_h__
#define __HUSD_Constants_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>

class HUSD_API HUSD_Constants
{
public:
    static const UT_StringHolder	&getRootPrimPath();
    static const UT_StringHolder	&getRenderSettingsRootPrimPath();

    static const UT_StringHolder	&getHoudiniRendererPluginName();
    static const UT_StringHolder	&getKarmaRendererPluginName();
    static const UT_StringHolder	&getHydraRendererPluginName();

    static const UT_StringHolder	&getXformPrimType();
    static const UT_StringHolder	&getScopePrimType();
    static const UT_StringHolder	&getLuxLightPrimType();
    static const UT_StringHolder	&getGeomGprimType();
    static const UT_StringHolder	&getGeomImageablePrimType();
    static const UT_StringHolder	&getVolumePrimType();
    static const UT_StringHolder	&getOpenVDBAssetPrimType();
    static const UT_StringHolder	&getHoudiniFieldAssetPrimType();
    static const UT_StringHolder	&getPointInstancerPrimType();

    static const UT_StringHolder	&getAutomaticPrimIdentifier();
    static const UT_StringHolder	&getDefaultPrimIdentifier();

    static const UT_StringHolder	&getDefaultClipSetName();

    static const UT_StringHolder	&getHoudiniLayerInfoPrimPath();
    static const UT_StringHolder	&getHoudiniLayerInfoPrimName();
    static const UT_StringHolder	&getHoudiniLayerInfoPrimType();

    static const UT_StringHolder	&getSaveControlExplicit();
    static const UT_StringHolder	&getSaveControlPlaceholder();
    static const UT_StringHolder	&getSaveControlIsFileFromDisk();
    static const UT_StringHolder	&getSaveControlDoNotSave();

    static const UT_StringHolder	&getAttributePointProtoIndices();
    static const UT_StringHolder	&getAttributePointIds();
    static const UT_StringHolder	&getAttributePointInvisibleIds();
    static const UT_StringHolder	&getAttributePointPositions();
    static const UT_StringHolder	&getAttributePointOrientations();
    static const UT_StringHolder	&getAttributePointScales();
    static const UT_StringHolder	&getAttributePointVelocities();
    static const UT_StringHolder	&getAttributePointAngularVelocities();
    static const UT_StringHolder	&getAttributePoints();

    static const UT_StringHolder	&getAttributeVolumeFieldFilePath();
    static const UT_StringHolder	&getAttributeVolumeFieldName();
    static const UT_StringHolder	&getAttributeVolumeFieldIndex();

    static const UT_StringHolder	&getRelationshipPrototypes();

    static const UT_StringHolder	&getPrimSpecifierDefine();
    static const UT_StringHolder	&getPrimSpecifierOverride();
    static const UT_StringHolder	&getPrimSpecifierClass();

    static const UT_StringHolder	&getReferenceTypeFile();
    static const UT_StringHolder	&getReferenceTypePayload();
    static const UT_StringHolder	&getReferenceTypePrim();
    static const UT_StringHolder	&getReferenceTypeInherit();
    static const UT_StringHolder	&getReferenceTypeSpecialize();

    static const UT_StringHolder	&getReferenceEditOpAppendFront();
    static const UT_StringHolder	&getReferenceEditOpAppendBack();
    static const UT_StringHolder	&getReferenceEditOpPrependFront();
    static const UT_StringHolder	&getReferenceEditOpPrependBack();

    static const UT_StringHolder	&getPurposeDefault();
    static const UT_StringHolder	&getPurposeProxy();
    static const UT_StringHolder	&getPurposeRender();
    static const UT_StringHolder	&getPurposeGuide();

    static const UT_StringHolder	&getKindSubComponent();
    static const UT_StringHolder	&getKindComponent();
    static const UT_StringHolder	&getKindGroup();
    static const UT_StringHolder	&getKindAssembly();
    static const UT_StringHolder	&getKindAutomatic();

    static const UT_StringHolder	&getDrawModeDefault();
    static const UT_StringHolder	&getDrawModeOrigin();
    static const UT_StringHolder	&getDrawModeBounds();
    static const UT_StringHolder	&getDrawModeCards();

    static const UT_StringHolder	&getExpansionExplicit();
    static const UT_StringHolder	&getExpansionExpandPrims();
    static const UT_StringHolder	&getExpansionExpandPrimsAndProperties();

    static const UT_StringHolder	&getDefaultBgeoPathAttr();
    static const UT_StringHolder	&getDefaultBgeoAttribPattern();
    static const UT_StringHolder	&getDefaultBgeoPathPrefix();

    static const UT_StringHolder	&getMatPurposeAll();
    static const UT_StringHolder	&getMatPurposeFull();
    static const UT_StringHolder	&getMatPurposePreview();

    static const UT_StringHolder	&getInterpolationConstant();
    static const UT_StringHolder	&getInterpolationVarying();

    static const UT_StringHolder	&getUpAxisY();
    static const UT_StringHolder	&getUpAxisZ();
};

#endif

