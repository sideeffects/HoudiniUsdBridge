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

#include "XUSD_HydraApexDataSource.h"

#include "HUSD_HydraApexSceneEvaluator.h"
#include "XUSD_ApexBakeSceneUtils.h"
#include "XUSD_ApexScene.h"
#include "XUSD_Utils.h"

#include <GA/GA_Handle.h>
#include <GA/GA_Names.h>
#include <GEO/GEO_PrimCamera.h>
#include <GU/GU_Detail.h>
#include <GU/GU_DetailHandle.h>
#include <UT/UT_ConcurrentHashMap.h>
#include <UT/UT_DoubleLock.h>
#include <UT/UT_Map.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_Tracing.h>
#include <gusd/UT_Gf.h>

#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/dataSourceTypeDefs.h>
#include <pxr/imaging/hd/extentSchema.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/usd/usdVol/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
/// For now we report samples at integer frames within the shutter range, along
/// with the boundary times. This matches the sampling behaviour for
/// UsdImagingDataSourceXformMatrix (USD prim transforms).
/// In the future we could provide a way to describe what time samples the APEX
/// scene should provide.
bool
xusdGetApexSampleTimes(
        HdSampledDataSource::Time start_time,
        HdSampledDataSource::Time end_time,
        std::vector<HdSampledDataSource::Time> *out_sample_times)
{
    UT_ASSERT(out_sample_times);
    UT_ASSERT(start_time <= end_time);
    if (start_time > end_time)
        return false;

    const HdSampledDataSource::Time first_sample = SYSceil(start_time);
    const HdSampledDataSource::Time last_sample = SYSfloor(end_time);
    const exint count = SYSrint(last_sample - first_sample) + 1;

    out_sample_times->reserve(count + 2);
    out_sample_times->push_back(start_time);

    for (exint i = 0; i < count; ++i)
    {
        const HdSampledDataSource::Time sample_time = first_sample + i;
        if (sample_time > start_time && sample_time < end_time)
            out_sample_times->push_back(sample_time);
    }

    if (start_time != end_time)
        out_sample_times->push_back(end_time);

    return true;
}

/// Tracks data IDs for determining whether an APEX geometry output has changed
/// (e.g. due to the current scene frame being changed, or a viewer state
/// override).
class xusdGeometryDataIds
{
public:
    xusdGeometryDataIds() = default;

    xusdGeometryDataIds(const GU_Detail &detail)
        : myDetailId(detail.getUniqueId())
        , myMetaCacheCount(detail.getMetaCacheCount())
    {
    }

    bool operator==(const xusdGeometryDataIds &) const = default;

private:
    exint myDetailId = -1;
    exint myMetaCacheCount = -1;
};

/// Data source to lazily marshal data from a SOP attribute.
template <typename T>
class xusdSopAttribDataSource : public HdTypedSampledDataSource<VtArray<T>>
{
public:
    HD_DECLARE_DATASOURCE(xusdSopAttribDataSource<T>);

    ~xusdSopAttribDataSource() override = default;
    UT_NON_COPYABLE(xusdSopAttribDataSource);

    xusdSopAttribDataSource(
            const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
            exint output_idx,
            GA_AttributeOwner attrib_owner,
            const UT_StringHolder &attrib_name)
        : myEvaluator(evaluator)
        , myOutputIdx(output_idx)
        , myAttribOwner(attrib_owner)
        , myAttribName(attrib_name)
    {
    }

    bool GetContributingSampleTimesForInterval(
            HdSampledDataSource::Time start_time,
            HdSampledDataSource::Time end_time,
            std::vector<HdSampledDataSource::Time> *out_sample_times) override
    {
        return xusdGetApexSampleTimes(
                start_time, end_time, out_sample_times);
    }

    VtValue GetValue(HdSampledDataSource::Time shutter_offset) override
    {
        return VtValue(GetTypedValue(shutter_offset));
    }

    VtArray<T> GetTypedValue(HdSampledDataSource::Time shutter_offset) override
    {
        utZoneScopedN("xusdSopAttribDataSource convert");
        utZoneTextSH(myAttribName);

        // We maintain a cache of the marshalled data since the same value can
        // be requested multiple times. We keep a separate cache for each
        // shutter offset, and invalidate the cache entry if the APEX output
        // geometry has changed.
        GU_ConstDetailHandle gdh = myEvaluator->getGeometry(
                myOutputIdx, shutter_offset);
        if (!gdh.isValid())
        {
            UT_ASSERT_MSG(false, "Failed to evaluate APEX geometry output");
            return {};
        }

        const GU_Detail &detail = *gdh.gdp();
        xusdGeometryDataIds data_ids(detail);

        typename SampleMap::accessor accessor;
        myCachedSamples.insert(accessor, shutter_offset);

        CacheEntry &entry = accessor->second;
        if (entry.myDataIds != data_ids)
        {
            using UtT = typename GusdUT_Gf::TypeEquivalence<T>::AltType;
            GA_ROHandleT<UtT> attrib = detail.findAttribute(
                    myAttribOwner, myAttribName);

            if (attrib.isValid())
            {
                // Note we don't apply any transform to the world-space points
                // from APEX. In xusdBuildShapeOverlay() we also override the
                // prim's transform to account for this.
                XUSD_ApexBakeSceneUtils::convertAttribute(
                        detail, attrib, /*world_to_prim_xform=*/nullptr,
                        entry.myConvertedAttrib);

                entry.myDataIds = data_ids;
            }
            else
            {
                UT_ASSERT_MSG(
                        false, "Could not find attribute '%s'",
                        myAttribName.c_str());
            }
        }

        return entry.myConvertedAttrib;
    }

private:
    struct CacheEntry
    {
        VtArray<T> myConvertedAttrib;
        xusdGeometryDataIds myDataIds;
    };

    HUSD_HydraApexSceneEvaluatorConstPtr myEvaluator;
    const exint myOutputIdx;
    GA_AttributeOwner myAttribOwner;
    UT_StringHolder myAttribName;

    using SampleMap = UT_ConcurrentHashMap<fpreal, CacheEntry>;
    SampleMap myCachedSamples;
};

/// Utility for computing and caching bounds from the deformed point positions.
/// This is shared between the two data sources for providing the min and max
/// values of the extent.
class xusdExtentQuery
{
public:
    xusdExtentQuery(const HdSampledDataSourceHandle &points_source)
        : myPointsSource(points_source)
    {
    }

    bool getContributingSampleTimesForInterval(
            HdSampledDataSource::Time start_time,
            HdSampledDataSource::Time end_time,
            std::vector<HdSampledDataSource::Time> *out_sample_times)
    {
        // The points' time samples determine the extent's time samples
        return myPointsSource->GetContributingSampleTimesForInterval(
                start_time, end_time, out_sample_times);
    }

    GfRange3f getExtent(HdSampledDataSource::Time shutter_offset)
    {
        utZoneScopedN("xusdExtentCache::getExtent");

        VtValue points_value = myPointsSource->GetValue(shutter_offset);

        VtArray<GfVec3f> points_f;
        VtArray<GfVec3h> points_h;
        bool is_half = false;

        if (points_value.IsHolding<VtVec3hArray>())
        {
            points_h = points_value.Get<VtVec3hArray>();
            is_half = true;
        }
        else if (points_value.IsHolding<VtVec3fArray>())
            points_f = points_value.Get<VtVec3fArray>();
        else
            return GfRange3f();

        typename SampleMap::accessor accessor;
        myCachedSamples.insert(accessor, shutter_offset);

        // We can rely on VtArray's COW behaviour here - if the points' data
        // source is returning the exact same array, the extent is up to date.
        CacheEntry &entry = accessor->second;
        if (is_half && !entry.mySourcePointsH.IsIdentical(points_h))
        {
            entry.myExtent = XUSD_ApexBakeSceneUtils::computeExtentFromPoints(
                    points_h);
            entry.mySourcePointsH = points_h;
        }
        else if (!entry.mySourcePoints.IsIdentical(points_f))
        {
            entry.myExtent = XUSD_ApexBakeSceneUtils::computeExtentFromPoints(
                    points_f);
            entry.mySourcePoints = points_f;
        }

        return entry.myExtent;
    }

private:
    struct CacheEntry
    {
        GfRange3f myExtent;
        VtArray<GfVec3f> mySourcePoints;
        VtArray<GfVec3h> mySourcePointsH;
    };

    HdSampledDataSourceHandle myPointsSource;

    using SampleMap = UT_ConcurrentHashMap<fpreal, CacheEntry>;
    SampleMap myCachedSamples;
};

/// Data source for the min/max extents of a deforming shape.
class xusdExtentDataSource : public HdTypedSampledDataSource<GfVec3d>
{
public:
    HD_DECLARE_DATASOURCE(xusdExtentDataSource);

    xusdExtentDataSource(
            const UT_SharedPtr<xusdExtentQuery> &extent_cache,
            bool is_min)
        : myExtentQuery(extent_cache)
        , myIsMin(is_min)
    {
    }

    bool GetContributingSampleTimesForInterval(
            HdSampledDataSource::Time start_time,
            HdSampledDataSource::Time end_time,
            std::vector<HdSampledDataSource::Time> *out_sample_times) override
    {
        return myExtentQuery->getContributingSampleTimesForInterval(
                start_time, end_time, out_sample_times);
    }

    VtValue GetValue(HdSampledDataSource::Time shutter_offset) override
    {
        return VtValue(GetTypedValue(shutter_offset));
    }

    GfVec3d GetTypedValue(HdSampledDataSource::Time shutter_offset) override
    {
        const GfRange3f extent = myExtentQuery->getExtent(shutter_offset);
        return myIsMin ? extent.GetMin() : extent.GetMax();
    }

private:
    UT_SharedPtr<xusdExtentQuery> myExtentQuery;
    bool myIsMin = false;
};

/// Data source which returns the transform of a GA primitive that has an
/// intrinsic transform.
class xusdPrimXformDataSource : public HdTypedSampledDataSource<GfMatrix4d>
{
public:
    HD_DECLARE_DATASOURCE(xusdPrimXformDataSource)

    xusdPrimXformDataSource(
            const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
            exint output_idx)
        : myEvaluator(evaluator), myOutputIdx(output_idx)
    {
    }
    ~xusdPrimXformDataSource() override = default;
    UT_NON_COPYABLE(xusdPrimXformDataSource);

    bool GetContributingSampleTimesForInterval(
            HdSampledDataSource::Time start_time,
            HdSampledDataSource::Time end_time,
            std::vector<HdSampledDataSource::Time> *out_sample_times) override
    {
        return xusdGetApexSampleTimes(
                start_time, end_time, out_sample_times);
    }

    VtValue GetValue(HdSampledDataSource::Time shutter_offset) override
    {
        return VtValue(GetTypedValue(shutter_offset));
    }

    GfMatrix4d GetTypedValue(HdSampledDataSource::Time shutter_offset) override
    {
        GU_ConstDetailHandle gdh = myEvaluator->getGeometry(
                myOutputIdx, shutter_offset);
        if (!gdh.isValid())
        {
            UT_ASSERT_MSG(false, "Failed to evaluate APEX geometry output");
            return GfMatrix4d(1.0);
        }

        const GU_Detail &detail = *gdh.gdp();
        UT_ASSERT_MSG(
                detail.getNumPrimitives() == 1,
                "Rigs with multiple transforming prims are not supported");
        if (detail.getNumPrimitives() != 1)
            return GfMatrix4d(1.0);

        const GA_Offset primoff = detail.primitiveOffset(GA_Index(0));
        const GA_Primitive *prim = detail.getPrimitive(primoff);
        if (!prim->hasLocalTransform())
        {
            UT_ASSERT_MSG(false, "Expected a transforming primitive!");
            return GfMatrix4d(1.0);
        }

        UT_Matrix4D xform;
        prim->getLocalTransform4(xform);

        return GusdUT_Gf::Cast(xform);
    }

private:
    const HUSD_HydraApexSceneEvaluatorConstPtr myEvaluator;
    const exint myOutputIdx;
};

/// Utility for caching the translated camera parameters from the APEX output
/// geometry.
class xusdCameraQuery
{
public:
    xusdCameraQuery(
            const SdfPath &prim_path,
            const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
            exint output_idx)
        : myPrimPath(prim_path)
        , myEvaluator(evaluator)
        , myOutputIdx(output_idx)
    {
    }

    bool getContributingSampleTimesForInterval(
            HdSampledDataSource::Time start_time,
            HdSampledDataSource::Time end_time,
            std::vector<HdSampledDataSource::Time> *out_sample_times)
    {
        return xusdGetApexSampleTimes(
                start_time, end_time, out_sample_times);
    }

    VtValue getCameraParm(
            HdSampledDataSource::Time shutter_offset,
            const TfToken &parm_name)
    {
        GU_ConstDetailHandle gdh = myEvaluator->getGeometry(
                myOutputIdx, shutter_offset);
        if (!gdh.isValid())
        {
            TF_WARN("<%s>: failed to evaluate APEX geometry output",
                    myPrimPath.GetText());
            return VtValue();
        }

        const GU_Detail &detail = *gdh.gdp();
        xusdGeometryDataIds data_ids(detail);

        typename SampleMap::accessor accessor;
        myCachedSamples.insert(accessor, shutter_offset);

        CacheEntry &entry = accessor->second;
        if (entry.myDataIds != data_ids)
        {
            entry.myDataIds = data_ids;
            entry.myCameraParms.clear();

            const GEO_PrimCamera *geo_camera = nullptr;
            if (detail.getNumPrimitives() == 1)
            {
                GA_Offset primoff = detail.primitiveOffset(0);
                if (detail.getPrimitiveTypeId(primoff) == GA_PRIMCAMERA)
                {
                    geo_camera = UTverify_cast<const GEO_PrimCamera *>(
                            detail.getPrimitive(primoff));
                }
            }

            if (!geo_camera)
            {
                TF_WARN("<%s>: output geometry is expected to contain a single "
                        "camera primitive",
                        myPrimPath.GetText());
                return VtValue();
            }

            convertCameraParms(geo_camera->getParms(), entry.myCameraParms);
        }

        return entry.myCameraParms.get(parm_name, VtValue());
    }

private:
    using CameraParmMap = UT_Map<TfToken, VtValue, TfToken::HashFunctor>;

    /// Translate the camera parameters to Hydra, which scales camera parameters
    /// from 1/10 scene units. See XUSD_HydraGeoImport.C for the inverse.
    static void convertCameraParms(
            const UT_CameraParms &ut_parms,
            CameraParmMap &hydra_parms)
    {
        XUSD_CameraParms usd_parms;
        HUSDconvertCameraParms(ut_parms, usd_parms);

        switch (usd_parms.myCamera.GetProjection())
        {
            case GfCamera::Perspective:
                hydra_parms[HdCameraSchemaTokens->projection]
                        = HdCameraSchemaTokens->perspective;
                break;
            case GfCamera::Orthographic:
                hydra_parms[HdCameraSchemaTokens->projection]
                        = HdCameraSchemaTokens->orthographic;
                break;
            default:
                UT_ASSERT_MSG(false, "Unhandled camera projection type");
                break;
        }

        hydra_parms[HdCameraSchemaTokens->focalLength]
                = static_cast<float>(
                        usd_parms.myCamera.GetFocalLength()
                        * GfCamera::FOCAL_LENGTH_UNIT);
        hydra_parms[HdCameraSchemaTokens->horizontalAperture]
                = static_cast<float>(
                        usd_parms.myCamera.GetHorizontalAperture()
                        * GfCamera::APERTURE_UNIT);
        hydra_parms[HdCameraSchemaTokens->verticalAperture]
                = static_cast<float>(
                        usd_parms.myCamera.GetVerticalAperture()
                        * GfCamera::APERTURE_UNIT);
        hydra_parms[HdCameraSchemaTokens->horizontalApertureOffset]
                = static_cast<float>(
                        usd_parms.myCamera.GetHorizontalApertureOffset()
                        * GfCamera::APERTURE_UNIT);
        hydra_parms[HdCameraSchemaTokens->verticalApertureOffset]
                = static_cast<float>(
                        usd_parms.myCamera.GetVerticalApertureOffset()
                        * GfCamera::APERTURE_UNIT);

        const GfRange1f clipping_range = usd_parms.myCamera.GetClippingRange();
        hydra_parms[HdCameraSchemaTokens->clippingRange]
                = GfVec2f(clipping_range.GetMin(), clipping_range.GetMax());
        hydra_parms[HdCameraSchemaTokens->focusDistance]
                = usd_parms.myCamera.GetFocusDistance();
        hydra_parms[HdCameraSchemaTokens->fStop]
                = usd_parms.myCamera.GetFStop();
    }

    struct CacheEntry
    {
        CameraParmMap myCameraParms;
        xusdGeometryDataIds myDataIds;
    };

    const SdfPath myPrimPath;
    const HUSD_HydraApexSceneEvaluatorConstPtr myEvaluator;
    const exint myOutputIdx;

    using SampleMap = UT_ConcurrentHashMap<fpreal, CacheEntry>;
    SampleMap myCachedSamples;
};

/// Data source for an animated camera parameter.
template <typename T>
class xusdCameraParmDataSource : public HdTypedSampledDataSource<T>
{
public:
    HD_DECLARE_DATASOURCE(xusdCameraParmDataSource<T>);

    xusdCameraParmDataSource(
            const UT_SharedPtr<xusdCameraQuery> &camera_query,
            const TfToken &parm_name)
        : myCameraQuery(camera_query)
        , myParmName(parm_name)
    {
    }

    bool GetContributingSampleTimesForInterval(
            HdSampledDataSource::Time start_time,
            HdSampledDataSource::Time end_time,
            std::vector<HdSampledDataSource::Time> *out_sample_times) override
    {
        return myCameraQuery->getContributingSampleTimesForInterval(
                start_time, end_time, out_sample_times);
    }

    VtValue GetValue(HdSampledDataSource::Time shutter_offset) override
    {
        return VtValue(GetTypedValue(shutter_offset));
    }

    T GetTypedValue(HdSampledDataSource::Time shutter_offset) override
    {
        VtValue value = myCameraQuery->getCameraParm(
                shutter_offset, myParmName);

        if (value.IsEmpty())
            return T{};

        UT_ASSERT(value.IsHolding<T>());
        return value.Get<T>();
    }

private:
    UT_SharedPtr<xusdCameraQuery> myCameraQuery;
    const TfToken myParmName;
};
} // namespace

/// Adds data source which computes an updated bounding box from the new point
/// positions.
static void
xusdInitExtentDataSource(
        TfTokenVector &names,
        std::vector<HdDataSourceBaseHandle> &values,
        const HdSampledDataSourceHandle &positions)
{
    UT_ASSERT(positions);
    if (!positions)
        return;

    // TODO - incorporate widths when computing extent for points / curves
    auto extent_cache = UTmakeShared<xusdExtentQuery>(positions);
    auto min_source = xusdExtentDataSource::New(extent_cache, /*is_min*/ true);
    auto max_source = xusdExtentDataSource::New(extent_cache, /*is_min*/ false);
    auto extent_override = HdExtentSchema::Builder()
                                   .SetMin(min_source)
                                   .SetMax(max_source)
                                   .Build();

    names.push_back(HdExtentSchema::GetSchemaToken());
    values.push_back(extent_override);
}

/// Adds data source which overrides the world transform to identity.
/// The APEX scene is in world space, so this avoids needing to transform
/// the points, normals etc back into the prim local space.
static void
xusdInitIdentityXformDataSource(
        TfTokenVector &names,
        std::vector<HdDataSourceBaseHandle> &values)
{
    HdContainerDataSourceHandle xform_override
            = HdXformSchema::Builder()
                      .SetMatrix(
                              HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                                      GfMatrix4d().SetIdentity()))
                      .SetResetXformStack(
                              HdRetainedTypedSampledDataSource<bool>::New(true))
                      .Build();

    names.push_back(HdXformSchema::GetSchemaToken());
    values.push_back(xform_override);
}

static void
xusdInitPointBasedDataSources(
        UT_Span<const XUSD_ApexShapePrimvarInfo> shape_primvars,
        const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
        exint output_idx,
        TfTokenVector &names,
        std::vector<HdDataSourceBaseHandle> &values)
{
    static constexpr int theAttribCapacity = 2;
    TfSmallVector<TfToken, theAttribCapacity> primvar_names;
    TfSmallVector<HdDataSourceBaseHandle, theAttribCapacity> primvar_values;

    HdSampledDataSourceHandle points_override;

    for (const XUSD_ApexShapePrimvarInfo &primvar_info : shape_primvars)
    {
        HdSampledDataSourceHandle primvar_override;

        if (primvar_info.myTypeInfo == SdfValueTypeNames->Half3Array
            || primvar_info.myTypeInfo == SdfValueTypeNames->Normal3hArray
            || primvar_info.myTypeInfo == SdfValueTypeNames->Point3hArray
            || primvar_info.myTypeInfo == SdfValueTypeNames->Vector3hArray)
        {
            primvar_override = xusdSopAttribDataSource<GfVec3h>::New(
                    evaluator, output_idx, primvar_info.myOwner,
                    primvar_info.myName);
        }
        else if (
                primvar_info.myTypeInfo == SdfValueTypeNames->Float3Array
                || primvar_info.myTypeInfo == SdfValueTypeNames->Normal3fArray
                || primvar_info.myTypeInfo == SdfValueTypeNames->Point3fArray
                || primvar_info.myTypeInfo == SdfValueTypeNames->Vector3fArray)
        {
            primvar_override = xusdSopAttribDataSource<GfVec3f>::New(
                    evaluator, output_idx, primvar_info.myOwner,
                    primvar_info.myName);
        }
        else if (primvar_info.myTypeInfo == SdfValueTypeNames->QuathArray)
        {
            primvar_override = xusdSopAttribDataSource<GfQuath>::New(
                    evaluator, output_idx, primvar_info.myOwner,
                    primvar_info.myName);
        }
        else if (primvar_info.myTypeInfo == SdfValueTypeNames->QuatfArray)
        {
            primvar_override = xusdSopAttribDataSource<GfQuatf>::New(
                    evaluator, output_idx, primvar_info.myOwner,
                    primvar_info.myName);
        }
        else
        {
            UT_ASSERT_MSG(
                    false, "unsupported primvar type %s for %s",
                    primvar_info.myTypeInfo.GetAsToken().GetText(),
                    primvar_info.myName.c_str());
            continue;
        }

        primvar_names.push_back(primvar_info.myUsdName);
        primvar_values.push_back(
                HdPrimvarSchema::Builder()
                        .SetPrimvarValue(primvar_override)
                        .SetInterpolation(
                                HdPrimvarSchema::BuildInterpolationDataSource(
                                        primvar_info.myInterpolation))
                        .Build());

        if (primvar_info.myName == GA_Names::P)
            points_override = primvar_override;
    }

    // Record our child data sources.
    names.push_back(HdPrimvarsSchema::GetSchemaToken());
    values.push_back(
            HdPrimvarsSchema::BuildRetained(
                    primvar_names.size(), primvar_names.data(),
                    primvar_values.data()));

    // Set the world transform to identity, since the points are already in
    // world space.
    xusdInitIdentityXformDataSource(names, values);
    
    // Add data source for updated extents.
    xusdInitExtentDataSource(names, values, points_override);
}

static void
xusdInitGSplatDataSources(
        UT_Span<const XUSD_ApexShapePrimvarInfo> shape_primvars,
        const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
        exint output_idx,
        TfTokenVector &names,
        std::vector<HdDataSourceBaseHandle> &values)
{
    // Currently only points and orientations are animated, so this can reuse
    // the same code as point-based prims.
    xusdInitPointBasedDataSources(
            shape_primvars, evaluator, output_idx, names, values);
}

static void
xusdInitCameraDataSources(
        const SdfPath &prim_path,
        const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
        exint output_idx,
        TfTokenVector &names,
        std::vector<HdDataSourceBaseHandle> &values)
{
    // Add a data source for converting the camera prim's transform to the Hydra
    // camera's transform.
    static const auto theResetXformSource
            = HdRetainedTypedSampledDataSource<bool>::New(true);

    auto xform_source = xusdPrimXformDataSource::New(evaluator, output_idx);
    HdContainerDataSourceHandle xform_override
            = HdXformSchema::Builder()
                      .SetMatrix(xform_source)
                      .SetResetXformStack(theResetXformSource)
                      .Build();

    names.push_back(HdXformSchema::GetSchemaToken());
    values.push_back(xform_override);

    // Add a data source for animated camera parameters.
    // This matches the set of animated parameters supported by the APEX Bake
    // Scene LOP.
    auto camera_query = UTmakeShared<xusdCameraQuery>(
            prim_path, evaluator, output_idx);
    auto camera_override
            = HdCameraSchema::Builder()
                      .SetProjection(
                              xusdCameraParmDataSource<TfToken>::New(
                                      camera_query,
                                      HdCameraSchemaTokens->projection))
                      .SetFocalLength(
                              xusdCameraParmDataSource<float>::New(
                                      camera_query,
                                      HdCameraSchemaTokens->focalLength))
                      .SetHorizontalAperture(
                              xusdCameraParmDataSource<float>::New(
                                      camera_query,
                                      HdCameraSchemaTokens->horizontalAperture))
                      .SetVerticalAperture(
                              xusdCameraParmDataSource<float>::New(
                                      camera_query,
                                      HdCameraSchemaTokens->verticalAperture))
                      .SetHorizontalApertureOffset(
                              xusdCameraParmDataSource<float>::New(
                                      camera_query,
                                      HdCameraSchemaTokens->horizontalApertureOffset))
                      .SetVerticalApertureOffset(
                              xusdCameraParmDataSource<float>::New(
                                      camera_query,
                                      HdCameraSchemaTokens->verticalApertureOffset))
                      .SetClippingRange(
                              xusdCameraParmDataSource<GfVec2f>::New(
                                      camera_query,
                                      HdCameraSchemaTokens->clippingRange))
                      .SetFocusDistance(
                              xusdCameraParmDataSource<float>::New(
                                      camera_query,
                                      HdCameraSchemaTokens->focusDistance))
                      .SetFStop(
                              xusdCameraParmDataSource<float>::New(
                                      camera_query,
                                      HdCameraSchemaTokens->fStop))
                      .Build();

    names.push_back(HdCameraSchema::GetSchemaToken());
    values.push_back(camera_override);
}

XUSD_HydraApexShapeDataSource::XUSD_HydraApexShapeDataSource(
        const SdfPath &prim_path,
        HUSD_ApexShapeType shape_type,
        UT_Span<const XUSD_ApexShapePrimvarInfo> shape_primvars,
        const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
        exint output_idx)
    : myPrimPath(prim_path)
    , myShapeType(shape_type)
    , myEvaluator(evaluator)
    , myOutputIdx(output_idx)
{
    switch (myShapeType)
    {
        case HUSD_ApexShapeType::Mesh:
        case HUSD_ApexShapeType::Points:
        case HUSD_ApexShapeType::BasisCurves:
            xusdInitPointBasedDataSources(
                    shape_primvars, myEvaluator, myOutputIdx, myNames,
                    myValues);
            break;
        case HUSD_ApexShapeType::GSplats:
            xusdInitGSplatDataSources(
                    shape_primvars, myEvaluator, myOutputIdx, myNames,
                    myValues);
            break;
        case HUSD_ApexShapeType::Camera:
            xusdInitCameraDataSources(
                    myPrimPath, myEvaluator, myOutputIdx, myNames, myValues);
            break;
    }

}

XUSD_HydraApexShapeDataSource::~XUSD_HydraApexShapeDataSource() = default;

TfTokenVector
XUSD_HydraApexShapeDataSource::GetNames()
{
    return myNames;
}

HdDataSourceBaseHandle
XUSD_HydraApexShapeDataSource::Get(const TfToken &name)
{
    // Just do a linear search since we have a small fixed upper bound on the
    // number of entries.
    for (size_t i = 0, n = myNames.size(); i < n; ++i)
    {
        if (myNames[i] == name)
            return myValues[i];
    }

    return nullptr;
}

/*static*/
HdDataSourceLocatorSet
XUSD_HydraApexShapeDataSource::getDefaultLocators(
        HUSD_ApexShapeType shape_type)
{
    HdDataSourceLocatorSet dirty_locators;

    switch (shape_type)
    {
        case HUSD_ApexShapeType::Mesh:
        case HUSD_ApexShapeType::Points:
        case HUSD_ApexShapeType::BasisCurves:
            dirty_locators.append(HdPrimvarsSchema::GetPointsLocator());
            dirty_locators.append(HdPrimvarsSchema::GetNormalsLocator());
            dirty_locators.append(HdExtentSchema::GetDefaultLocator());
            break;
        case HUSD_ApexShapeType::GSplats:
            dirty_locators.append(
                    HdPrimvarsSchema::GetDefaultLocator().Append(
                            UsdVolTokens->positions));
            dirty_locators.append(
                    HdPrimvarsSchema::GetDefaultLocator().Append(
                            UsdVolTokens->orientations));
            dirty_locators.append(HdExtentSchema::GetDefaultLocator());
            break;
        case HUSD_ApexShapeType::Camera:
            dirty_locators.append(HdCameraSchema::GetDefaultLocator());
            break;
    }

    dirty_locators.append(HdXformSchema::GetDefaultLocator());
    return dirty_locators;
}

XUSD_HydraApexXformDataSource::XUSD_HydraApexXformDataSource(
        const SdfPath &prim_path,
        const HUSD_HydraApexSceneEvaluatorConstPtr &evaluator,
        exint output_idx,
        const UT_StringHolder &joint_name,
        const GfMatrix4d &child_xform)
    : myPrimPath(prim_path)
    , myEvaluator(evaluator)
    , myOutputIdx(output_idx)
    , myJointName(joint_name)
    , myChildXform(child_xform)
{
}

XUSD_HydraApexXformDataSource::~XUSD_HydraApexXformDataSource() = default;

bool
XUSD_HydraApexXformDataSource::GetContributingSampleTimesForInterval(
        HdSampledDataSource::Time start_time,
        HdSampledDataSource::Time end_time,
        std::vector<HdSampledDataSource::Time> *out_sample_times)
{
    return xusdGetApexSampleTimes(
            start_time, end_time, out_sample_times);
}

VtValue
XUSD_HydraApexXformDataSource::GetValue(
        HdSampledDataSource::Time shutter_offset)
{
    return VtValue(GetTypedValue(shutter_offset));
}

GfMatrix4d
XUSD_HydraApexXformDataSource::GetTypedValue(
        HdSampledDataSource::Time shutter_offset)
{
    GU_ConstDetailHandle gdh = myEvaluator->getGeometry(
            myOutputIdx, shutter_offset);
    if (!gdh.isValid())
    {
        UT_ASSERT_MSG(false, "Failed to evaluate APEX geometry output");
        return myChildXform;
    }

    UT_Matrix4D joint_xform;
    if (!XUSD_ApexBakeSceneUtils::getSkelJointXform(
                myPrimPath, *gdh.gdp(), myJointName, joint_xform))
    {
        return myChildXform;
    }

    return myChildXform * GusdUT_Gf::Cast(joint_xform);
}

PXR_NAMESPACE_CLOSE_SCOPE
