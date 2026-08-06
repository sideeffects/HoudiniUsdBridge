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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#pragma once

#include <pxr/pxr.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/coordSys.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/material.h>

PXR_NAMESPACE_OPEN_SCOPE        // [

/// Common SPrim interface
#define HDEBUG_COMMON_SPRIM() \
    void Finalize(HdRenderParam *param) override; \
    void Sync(HdSceneDelegate *sd, \
                HdRenderParam *param, \
                HdDirtyBits *dirtyBits) override; \
    HdDirtyBits GetInitialDirtyBitsMask() const override; \
    /* end macro */

/// SPrim: Camera
class HDCamera final : public HdCamera
{
public:
    static constexpr const char *class_name = "HDCamera";
    HDCamera(const SdfPath &id);
    ~HDCamera() override;

    HDEBUG_COMMON_SPRIM()
};

/// SPrim: CoordSys
class HDCoordSys final : public HdCoordSys
{
public:
    static constexpr const char *class_name = "HDCoordSys";
    HDCoordSys(const SdfPath &id);
    ~HDCoordSys() override;

    HDEBUG_COMMON_SPRIM()
};

/// SPrim: Light
class HDLight final : public HdLight
{
public:
    static constexpr const char *class_name = "HDLight";
    HDLight(const SdfPath &id);
    ~HDLight() override;

    HDEBUG_COMMON_SPRIM()
};

/// SPrim: LightFilter
class HDLightFilter final : public HdSprim
{
public:
    static constexpr const char *class_name = "HDLightFilter";
    HDLightFilter(const SdfPath &id);
    ~HDLightFilter() override;

    HDEBUG_COMMON_SPRIM()
};


/// SPrim: Material
class HDMaterial final : public HdMaterial
{
public:
    static constexpr const char *class_name = "HDMaterial";
    HDMaterial(const SdfPath &id);
    ~HDMaterial() override;

    HDEBUG_COMMON_SPRIM()
};


#undef HDEBUG_COMMON_SPRIM

PXR_NAMESPACE_CLOSE_SCOPE       // ]
