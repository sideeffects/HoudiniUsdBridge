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
#include <pxr/imaging/hd/basisCurves.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/points.h>
#include <pxr/imaging/hd/volume.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/base/gf/matrix4f.h>

#include "HDUtil.h"

PXR_NAMESPACE_OPEN_SCOPE        // [

/// Common RPrim interface
#define HDEBUG_COMMON_RPRIM() \
    void Finalize(HdRenderParam *param) override; \
    void Sync(HdSceneDelegate *sd, \
                HdRenderParam *param, \
                HdDirtyBits *dirtyBits, \
                const TfToken &repr) override; \
    HdDirtyBits GetInitialDirtyBitsMask() const override; \
    void UpdateRenderTag(HdSceneDelegate *sd, \
                HdRenderParam *param) override; \
  protected: \
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override; \
    void _InitRepr(const TfToken &repr, HdDirtyBits *dirtyBits) override; \
    HDUtil::Primvars    myPrimvars;
    /* end macro */

/// RPrim: BasisCurves
class HDCurves final : public HdBasisCurves
{
public:
    static constexpr const char *class_name = "HDCurves";
    using BaseClass = HdBasisCurves;
    HDCurves(const SdfPath &id);
    ~HDCurves() override;

    /// Sync primitive specific values
    void        syncSpecific(HdSceneDelegate *sd, HdRenderParam *param) {}
    const HDUtil::Primvars      *extraPrimvars() const { return nullptr; }

    HDEBUG_COMMON_RPRIM()
};

class HDMesh final : public HdMesh
{
public:
    static constexpr const char *class_name = "HDMesh";
    using BaseClass = HdMesh;
    HDMesh(const SdfPath &id);
    ~HDMesh() override;

    /// Sync primitive specific values
    void        syncSpecific(HdSceneDelegate *sd, HdRenderParam *param);
    const HDUtil::Primvars      *extraPrimvars() const { return &myTopology; }

    HDEBUG_COMMON_RPRIM()
private:
    HDUtil::Primvars	myTopology;
};

/// RPrim: Points
class HDPoints final : public HdPoints
{
public:
    static constexpr const char *class_name = "HDPoints";
    using BaseClass = HdPoints;
    HDPoints(const SdfPath &id);
    ~HDPoints() override;

    /// Sync primitive specific values
    void        syncSpecific(HdSceneDelegate *sd, HdRenderParam *param) {}
    const HDUtil::Primvars      *extraPrimvars() const { return nullptr; }

    HDEBUG_COMMON_RPRIM()
};

/// RPrim: Volume
class HDVolume final : public HdVolume
{
public:
    static constexpr const char *class_name = "HDVolume";
    using BaseClass = HdVolume;
    HDVolume(const SdfPath &id);
    ~HDVolume() override;

    /// Sync primitive specific values
    void        syncSpecific(HdSceneDelegate *sd, HdRenderParam *param) {}
    const HDUtil::Primvars      *extraPrimvars() const { return nullptr; }

    HDEBUG_COMMON_RPRIM()
};

#undef HDEBUG_COMMON_RPRIM

PXR_NAMESPACE_CLOSE_SCOPE       // ]
