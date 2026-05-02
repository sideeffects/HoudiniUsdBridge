/*
 * Copyright 2026 Side Effects Software Inc.
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
*/

#pragma once

#include <SYS/SYS_Types.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_Lock.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_StringHolder.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/smallVector.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

class HUSD_HydraApexSceneEvaluator;
using HUSD_HydraApexSceneEvaluatorConstPtr
        = UT_IntrusivePtr<const HUSD_HydraApexSceneEvaluator>;

enum class HUSD_ApexShapeType : uint8;

PXR_NAMESPACE_OPEN_SCOPE

/// Hydra data source for the overlays applied to a deforming shape (e.g.
/// points, normals etc)
class XUSD_HydraApexShapeDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(XUSD_HydraApexShapeDataSource);

    XUSD_HydraApexShapeDataSource(
            const SdfPath &prim_path,
            HUSD_ApexShapeType shape_type,
            const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
            exint output_idx);
    ~XUSD_HydraApexShapeDataSource() override;
    UT_NON_COPYABLE(XUSD_HydraApexShapeDataSource);

    TfTokenVector GetNames() override;
    HdDataSourceBaseHandle Get(const TfToken &name) override;

    static HdDataSourceLocatorSet getDefaultLocators(
            HUSD_ApexShapeType shape_type);

private:
    void ensureInitialized();

    UT_Lock myLock;
    bool myInitialized = false;

    const SdfPath myPrimPath;
    HUSD_ApexShapeType myShapeType;
    HUSD_HydraApexSceneEvaluatorConstPtr myEvaluator;
    const exint myOutputIdx;

    TfTokenVector myNames;
    std::vector<HdDataSourceBaseHandle> myValues;
};

/// Hydra data source for transform bindings: the prim transform follows a joint
/// transform from the skeleton geometry.
/// For child prims under the prim where the transform binding was applied, the
/// relative transform is combined with the joint transform.
class XUSD_HydraApexXformDataSource
    : public HdTypedSampledDataSource<GfMatrix4d>
{
public:
    HD_DECLARE_DATASOURCE(XUSD_HydraApexXformDataSource)

    XUSD_HydraApexXformDataSource(
            const SdfPath &prim_path,
            const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
            exint output_idx,
            const UT_StringHolder &joint_name,
            const GfMatrix4d &child_xform);
    ~XUSD_HydraApexXformDataSource() override;
    UT_NON_COPYABLE(XUSD_HydraApexXformDataSource);

    bool GetContributingSampleTimesForInterval(
            HdSampledDataSource::Time start_time,
            HdSampledDataSource::Time end_time,
            std::vector<HdSampledDataSource::Time> *out_sample_times) override;

    VtValue GetValue(HdSampledDataSource::Time shutter_offset) override;
    GfMatrix4d GetTypedValue(HdSampledDataSource::Time shutter_offset) override;

private:
    const SdfPath myPrimPath;
    const HUSD_HydraApexSceneEvaluatorConstPtr myEvaluator;
    const exint myOutputIdx;
    const UT_StringHolder myJointName;
    const GfMatrix4d myChildXform;
};

PXR_NAMESPACE_CLOSE_SCOPE
