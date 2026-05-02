/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 */

#include "gsplatWrapper.h"

#include "GT_VtArray.h"
#include "UT_Gf.h"

#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_PrimPointMesh.h>
#include <GT/GT_Refine.h>
#include <UT/UT_Optional.h>

static constexpr UT_StringLit theGSAlphaName("GS_Alpha");
static constexpr UT_StringLit theGSSPHRName("GS_SPH_R");
static constexpr UT_StringLit theGSSPHGName("GS_SPH_G");
static constexpr UT_StringLit theGSSPHBName("GS_SPH_B");

PXR_NAMESPACE_OPEN_SCOPE

/// Helper function for translating particle attributes, e.g. opacity,
/// orientation, etc
template <typename T>
static inline bool
gusdAddParticleAttrib(
        GT_AttributeListHandle &attribs,
        const UT_StringHolder &name,
        GT_Type type,
        const UsdGeomImageable &prim,
        const UsdAttribute &usd_attr,
        const UsdTimeCode &time,
        UT_Optional<exint> expected_size)
{
    VtArray<T> values;
    if (!usd_attr || !usd_attr.Get(&values, time))
        return false;

    if (expected_size.has_value() && values.size() < *expected_size)
    {
        UT_WorkBuffer msg;
        msg.format(
                "Not enough values found for {0} in {1}. Expected {2}, got {3}",
                usd_attr.GetName().GetString(), prim.GetPath().GetAsString(),
                *expected_size, values.size());
        TF_WARN("%s", msg.buffer());
        return false;
    }

    attribs = attribs->addAttribute(
            name, UTmakeIntrusive<GusdGT_VtArray<T>>(values, type), true);
    return true;
}

template <typename Vec3T>
static inline void
gusdConvertSphericalHarmonics(
        GT_AttributeListHandle &attribs,
        const UsdVolParticleField3DGaussianSplat &prim,
        const UsdAttribute &coefficients_attr,
        const UsdTimeCode &time,
        exint num_points)
{
    int degree = 0;
    UsdAttribute degree_attr = prim.GetRadianceSphericalHarmonicsDegreeAttr();
    if (!degree_attr || !degree_attr.Get(&degree, time))
        return;

    VtArray<Vec3T> coefficients;
    if (!coefficients_attr || !coefficients_attr.Get(&coefficients, time))
        return;

    const exint tuple_size = (degree + 1) * (degree + 1);
    const exint total_size = num_points * tuple_size;
    if (coefficients.size() < total_size)
    {
        UT_WorkBuffer msg;
        msg.format(
                "Not enough values found for {0} in {1}. Expected {2}, got {3}",
                coefficients_attr.GetName().GetString(),
                prim.GetPath().GetAsString(), total_size,
                coefficients.size());
        TF_WARN("%s", msg.buffer());
        return;
    }

    // Split the float3 USD attribute into the GS_SPH_[RGB] attributes for SOPs.
    using GtScalarT = typename GusdPodTupleTraits<Vec3T>::ValueType;
    auto sph_r = UTmakeIntrusive<GT_DANumeric<GtScalarT>>(
            num_points, tuple_size);
    GtScalarT *sph_r_data = sph_r->data();

    auto sph_g = UTmakeIntrusive<GT_DANumeric<GtScalarT>>(
            num_points, tuple_size);
    GtScalarT *sph_g_data = sph_g->data();

    auto sph_b = UTmakeIntrusive<GT_DANumeric<GtScalarT>>(
            num_points, tuple_size);
    GtScalarT *sph_b_data = sph_b->data();

    // Create a const span for the loop below to avoid a COW detach for VtArray.
    TfSpan<const Vec3T> coefficients_span = coefficients.AsConst();
    UTparallelForLightItems(
            UT_BlockedRange<exint>(0, total_size),
            [&](const UT_BlockedRange<exint> &range)
    {
        for (exint i : range.items())
        {
            const Vec3T &c = coefficients_span[i];

            sph_r_data[i] = c[0];
            sph_g_data[i] = c[1];
            sph_b_data[i] = c[2];
        }
    });

    attribs = attribs->addAttribute(theGSSPHRName.asHolder(), sph_r, true);
    attribs = attribs->addAttribute(theGSSPHGName.asHolder(), sph_g, true);
    attribs = attribs->addAttribute(theGSSPHBName.asHolder(), sph_b, true);
}

GusdGSplatWrapper::GusdGSplatWrapper(
        const UsdVolParticleField3DGaussianSplat &usd_gsplat,
        UsdTimeCode time,
        GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), myUsdGSplat(usd_gsplat)
{
}

GusdGSplatWrapper::~GusdGSplatWrapper() = default;

const char *
GusdGSplatWrapper::className() const
{
    return "GusdGSplatWrapper";
}

void
GusdGSplatWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "GusdGSplatWrapper::enlargeBounds not implemented");
}

int
GusdGSplatWrapper::getMotionSegments() const
{
    return 1;
}

int64
GusdGSplatWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
GusdGSplatWrapper::doSoftCopy() const
{
    return UTmakeIntrusive<GusdGSplatWrapper>(*this);
}

bool
GusdGSplatWrapper::isValid() const
{
    return static_cast<bool>(myUsdGSplat);
}

bool
GusdGSplatWrapper::refine(GT_Refine &refiner, const GT_RefineParms *parms) const
{
    if (!isValid())
        return false;

    const bool for_viewport = GT_GEOPrimPacked::useViewportLOD(parms);

    auto pt_attribs = UTmakeIntrusive<GT_AttributeList>(
            UTmakeIntrusive<GT_AttributeMap>());
    auto constant_attribs = UTmakeIntrusive<GT_AttributeList>(
            UTmakeIntrusive<GT_AttributeMap>());

    // Translate the standard attributes: position, orientation, etc, each of
    // which can be float or half-precision.
    UsdAttribute positions_attr;
    bool has_positions;
    if (myUsdGSplat.UsesFloatPositions(&positions_attr))
    {
        has_positions = gusdAddParticleAttrib<GfVec3f>(
                pt_attribs, GA_Names::P, GT_TYPE_POINT, myUsdGSplat,
                positions_attr, m_time, /*expected_size=*/UT_NULLOPT);
    }
    else
    {
        has_positions = gusdAddParticleAttrib<GfVec3h>(
                pt_attribs, GA_Names::P, GT_TYPE_POINT, myUsdGSplat,
                positions_attr, m_time, /*expected_size=*/UT_NULLOPT);
    }

    if (!has_positions)
    {
        TF_WARN("points could not be read from prim: <%s>",
                myUsdGSplat.GetPath().GetText());
        return false;
    }

    const exint num_points = pt_attribs->get(GA_Names::P)->entries();

    UsdAttribute orientations_attr;
    if (myUsdGSplat.UsesFloatOrientations(&orientations_attr))
    {
        gusdAddParticleAttrib<GfQuatf>(
                pt_attribs, GA_Names::orient, GT_TYPE_QUATERNION, myUsdGSplat,
                orientations_attr, m_time, num_points);
    }
    else
    {
        gusdAddParticleAttrib<GfQuath>(
                pt_attribs, GA_Names::orient, GT_TYPE_QUATERNION, myUsdGSplat,
                orientations_attr, m_time, num_points);
    }

    UsdAttribute scales_attr;
    if (myUsdGSplat.UsesFloatScales(&scales_attr))
    {
        gusdAddParticleAttrib<GfVec3f>(
                pt_attribs, GA_Names::scale, GT_TYPE_NONE, myUsdGSplat,
                scales_attr, m_time, num_points);
    }
    else
    {
        gusdAddParticleAttrib<GfVec3h>(
                pt_attribs, GA_Names::scale, GT_TYPE_NONE, myUsdGSplat,
                scales_attr, m_time, num_points);
    }

    // Opacity translates to the GS_Alpha attribute.
    UsdAttribute opacities_attr;
    if (myUsdGSplat.UsesFloatOpacities(&opacities_attr))
    {
        gusdAddParticleAttrib<float>(
                pt_attribs, theGSAlphaName.asHolder(), GT_TYPE_NONE,
                myUsdGSplat, opacities_attr, m_time, num_points);
    }
    else
    {
        gusdAddParticleAttrib<GfHalf>(
                pt_attribs, theGSAlphaName.asHolder(), GT_TYPE_NONE,
                myUsdGSplat, opacities_attr, m_time, num_points);
    }

    // Translate the spherical harmonics.
    UsdAttribute coefficients_attr;
    if (myUsdGSplat.UsesFloatRadianceCoefficients(&coefficients_attr))
    {
        gusdConvertSphericalHarmonics<GfVec3f>(
                pt_attribs, myUsdGSplat, coefficients_attr, m_time, num_points);
    }
    else
    {
        gusdConvertSphericalHarmonics<GfVec3h>(
                pt_attribs, myUsdGSplat, coefficients_attr, m_time, num_points);
    }

    // Translate any additional primvars when unpacking.
    if (!for_viewport)
    {
        loadPrimvars(
                *myUsdGSplat.GetSchemaClassPrimDefinition(), m_time, parms, 0,
                num_points, 0, myUsdGSplat.GetPath().GetAsString(), nullptr,
                &pt_attribs, nullptr, &constant_attribs, nullptr);
    }

    auto gt_points = UTmakeIntrusive<GT_PrimPointMesh>(
            pt_attribs, constant_attribs);
    gt_points->setPrimitiveTransform(getPrimitiveTransform());
    refiner.addPrimitive(gt_points);

    return true;
}

GT_PrimitiveHandle
GusdGSplatWrapper::defineForRead(
        const UsdGeomImageable &source_prim,
        UsdTimeCode time,
        GusdPurposeSet purposes)
{
    return UTmakeIntrusive<GusdGSplatWrapper>(
            UsdVolParticleField3DGaussianSplat(source_prim.GetPrim()), time,
            purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
