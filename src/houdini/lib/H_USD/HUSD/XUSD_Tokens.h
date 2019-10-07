//
// Copyright 2016 Pixar
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
#ifndef HUSD_TOKENS_H
#define HUSD_TOKENS_H

#include "HUSD_API.h"
#include <pxr/pxr.h>
#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE

struct HusdHdPrimTypeTokensType
{
    HUSD_API HusdHdPrimTypeTokensType();

    const TfToken sprimGeometryLight;
    const TfToken bprimHoudiniFieldAsset;
    const TfToken openvdbAsset;
};
extern HUSD_API TfStaticData<HusdHdPrimTypeTokensType> &HusdHdPrimTypeTokens();


/// Tokens for light parameters
struct HusdHdLightTokensType
{
    HUSD_API HusdHdLightTokensType();

    // Light parms
    const TfToken attenStart;
    const TfToken attenType;
    const TfToken attenDist;
    const TfToken coneAngle;
    const TfToken coneSoftness;
    const TfToken coneDelta;
    const TfToken coneRolloff;
    const TfToken diffuse;
    const TfToken specular;

    // Shadow parms
    const TfToken shadowIntensity;

    // Values
    const TfToken none;
    const TfToken physical;
    const TfToken halfDistance;
    const TfToken activeRadiusEnable;
    const TfToken activeRadius;
    const TfToken shadowType;
    const TfToken shadowOff;
    const TfToken projectMap;
    const TfToken projectAngle;
    const TfToken clipNear;
    const TfToken clipFar;
};
extern HUSD_API TfStaticData<HusdHdLightTokensType> &HusdHdLightTokens();


struct HusdHdPrimvarTokensType
{
    HUSD_API HusdHdPrimvarTokensType();

    // instancing primvars
    const TfToken translate;
    const TfToken rotate;
    const TfToken scale;
    const TfToken instanceTransform;
    const TfToken viewLOD;
    const TfToken uv;
};
extern HUSD_API TfStaticData<HusdHdPrimvarTokensType> &HusdHdPrimvarTokens();

struct HusdHdPrimValueTokenType
{
    HUSD_API HusdHdPrimValueTokenType();

    // model:drawMode values
    const TfToken bounds;
    const TfToken cards;
    const TfToken origin;
    const TfToken full;
    const TfToken render;
};
extern HUSD_API TfStaticData<HusdHdPrimValueTokenType> &HusdHdPrimValueTokens();

struct HusdHdMaterialTokensType
{
    HUSD_API HusdHdMaterialTokensType();

    // mat types
    const TfToken usdPreviewMaterial;
    const TfToken usdPrimvarReader;
    const TfToken usdUVTexture;

    // preview mat parms
    const TfToken bias;
    const TfToken diffuseColor;
    const TfToken emissiveColor;
    const TfToken specularColor;
    const TfToken clearcoat;
    const TfToken clearcoatRoughness;
    const TfToken displacement;
    const TfToken file;
    const TfToken ior;
    const TfToken metallic;
    const TfToken normal;
    const TfToken occlusion;
    const TfToken opacity;
    const TfToken roughness;
    const TfToken scale;
    const TfToken useSpecWorkflow;
    const TfToken varname;
    const TfToken wrapS;
    const TfToken wrapT;
};
extern HUSD_API TfStaticData<HusdHdMaterialTokensType> &HusdHdMaterialTokens();

struct HusdHdRenderStatsTokensType
{
    HUSD_API HusdHdRenderStatsTokensType();

    const TfToken rendererName;		// {TfToken} - Render delgate name
    const TfToken rendererVersion;	// {GfVec3i} - Render delgate version

    // Percent done is a value between 0 and 100.  fractionDone is a fraction
    // between 0.0 and 1.0
    const TfToken percentDone;		// {float32/64, int32/64}
    const TfToken fractionDone;		// {float32/64 }

    const TfToken cameraRays;		// {int32/64, uint32/64 }
    const TfToken indirectRays;		// {int32/64, uint32/64 }
    const TfToken occlusionRays;	// {int32/64, uint32/64 }
    const TfToken lightGeoRays;		// {int32/64, uint32/64 }
    const TfToken probeRays;		// {int32/64, uint32/64 }

    // Counts are the "raw/individual" number and the "instanced" number
    const TfToken polyCounts;		// {GfSize2, GfVec2i} (raw, instanced)
    const TfToken curveCounts;		// {GfSize2, GfVec2i}
    const TfToken pointCounts;		// {GfSize2, GfVec2i}
    const TfToken pointMeshCounts;	// {GfSize2, GfVec2i}
    const TfToken volumeCounts;		// {GfSize2, GfVec2i}
    const TfToken proceduralCounts;	// {GfSize2, GfVec2i}
    const TfToken lightCounts;		// {int32/int64, uint32/uint64}
    const TfToken lightTreeCounts;	// {int32/int64, uint32/uint64}
    const TfToken cameraCounts;		// {int32/int64, uint32/uint64}

    const TfToken octreeBuildTime;	// {fpreal64/32}
    const TfToken loadClockTime;	// {fpreal64/32}
    const TfToken loadUTime;		// {fpreal64/32}
    const TfToken loadSTime;		// {fpreal64/32}
    const TfToken loadMemory;		// {int64,uint64} (in bytes)

    const TfToken totalClockTime;	// {fpreal64/32}
    const TfToken totalUTime;		// {fpreal64/32}
    const TfToken totalSTime;		// {fpreal64/32}
    const TfToken totalMemory;		// {int64,uint64}

    const TfToken peakMemory;		// {int64, uint64} (in bytes)
};

extern HUSD_API TfStaticData<HusdHdRenderStatsTokensType> &
HusdHdRenderStatsTokens();

PXR_NAMESPACE_CLOSE_SCOPE

#endif //HUSD_TOKENS_H

