/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "nurbsPatchWrapper.h"

#include "GT_VtArray.h"
#include "UT_Gf.h"
#include <GA/GA_NUBBasis.h>
#include <GT/GT_DANumeric.h>
#include <GT/GT_PrimNuPatch.h>
#include <GT/GT_Refine.h>
#include <GT/GT_TrimNuCurves.h>

PXR_NAMESPACE_OPEN_SCOPE

GusdNurbsPatchWrapper::GusdNurbsPatchWrapper(
        const UsdGeomNurbsPatch &usdPatch,
        UsdTimeCode time,
        GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), m_usdPatch(usdPatch)
{
}

GusdNurbsPatchWrapper::~GusdNurbsPatchWrapper() {}

const char *
GusdNurbsPatchWrapper::className() const
{
    return "GusdNurbsPatchWrapper";
}

void
GusdNurbsPatchWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments)
        const
{
    UT_ASSERT_MSG(
            false, "GusdNurbsPatchWrapper::enlargeBounds not implemented");
}

int
GusdNurbsPatchWrapper::getMotionSegments() const
{
    return 1;
}

int64
GusdNurbsPatchWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
GusdNurbsPatchWrapper::doSoftCopy() const
{
    return GT_PrimitiveHandle(new GusdNurbsPatchWrapper(*this));
}

bool
GusdNurbsPatchWrapper::isValid() const
{
    return static_cast<bool>(m_usdPatch);
}

bool
GusdNurbsPatchWrapper::refine(GT_Refine &refiner, const GT_RefineParms *parms) const
{
    if (!isValid())
    {
        TF_WARN("Invalid prim");
        return false;
    }

    int uorder = 0;
    if (!m_usdPatch.GetUOrderAttr().Get(&uorder, m_time))
    {
        TF_WARN("uOrder could not be read from prim: <%s>",
                m_usdPatch.GetPath().GetText());
        return false;
    }

    int vorder = 0;
    if (!m_usdPatch.GetVOrderAttr().Get(&vorder, m_time))
    {
        TF_WARN("vOrder could not be read from prim: <%s>",
                m_usdPatch.GetPath().GetText());
        return false;
    }

    VtArray<double> uknot_values;
    if (!m_usdPatch.GetUKnotsAttr().Get(&uknot_values, m_time))
    {
        TF_WARN("uKnots could not be read from prim: <%s>",
                m_usdPatch.GetPath().GetText());
        return false;
    }
    auto uknots = UTmakeIntrusive<GusdGT_VtArray<double>>(uknot_values);

    VtArray<double> vknot_values;
    if (!m_usdPatch.GetVKnotsAttr().Get(&vknot_values, m_time))
    {
        TF_WARN("vKnots could not be read from prim: <%s>",
                m_usdPatch.GetPath().GetText());
        return false;
    }
    auto vknots = UTmakeIntrusive<GusdGT_VtArray<double>>(vknot_values);

    VtVec3fArray points;
    if (!m_usdPatch.GetPointsAttr().Get(&points, m_time))
    {
        TF_WARN("points could not be read from prim: <%s>",
                m_usdPatch.GetPath().GetText());
        return false;
    }
    auto P = UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(points, GT_TYPE_POINT);
    const int ucount = uknots->entries() - uorder;
    const int vcount = vknots->entries() - vorder;
    if (points.size() != (ucount * vcount))
    {
        TF_WARN("Invalid size (%d, expected %d) for 'points' (prim <%s>)",
                int(points.size()), ucount * vcount,
                m_usdPatch.GetPath().GetText());
        return false;
    }

    auto vertex_attribs = GT_AttributeList::createAttributeList(GA_Names::P, P);

    VtArray<double> weights;
    if (m_usdPatch.GetPointWeightsAttr().Get(&weights, m_time))
    {
        vertex_attribs = vertex_attribs->addAttribute(
                GA_Names::Pw, UTmakeIntrusive<GusdGT_VtArray<double>>(weights),
                true);
    }

    VtVec3fArray normals;
    if (m_usdPatch.GetNormalsAttr().Get(&normals, m_time)
        && m_usdPatch.GetNormalsInterpolation() == UsdGeomTokens->vertex)
    {
        vertex_attribs = vertex_attribs->addAttribute(
                GA_Names::N,
                UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(
                        normals, GT_TYPE_NORMAL),
                true);
    }

    VtVec3fArray velocities;
    if (m_usdPatch.GetVelocitiesAttr().Get(&velocities, m_time))
    {
        vertex_attribs = vertex_attribs->addAttribute(
                GA_Names::v,
                UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(
                        velocities, GT_TYPE_VECTOR),
                true);
    }

    VtVec3fArray accelerations;
    if (m_usdPatch.GetAccelerationsAttr().Get(&accelerations, m_time))
    {
        vertex_attribs = vertex_attribs->addAttribute(
                GA_Names::accel,
                UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(
                        accelerations, GT_TYPE_VECTOR),
                true);
    }

    auto detail_attribs = UTmakeIntrusive<GT_AttributeList>(
            UTmakeIntrusive<GT_AttributeMap>());
    loadPrimvars(
            *m_usdPatch.GetSchemaClassPrimDefinition(), m_time, parms, 0,
            points.size(), 0, m_usdPatch.GetPath().GetAsString(), nullptr,
            &vertex_attribs, nullptr, &detail_attribs, nullptr);

    // Load trim curves.
    UT_UniquePtr<GT_TrimNuCurves> trim;
    {
        VtArray<int> counts;
        m_usdPatch.GetTrimCurveCountsAttr().Get(&counts, m_time);

        VtArray<int> vertex_counts;
        m_usdPatch.GetTrimCurveVertexCountsAttr().Get(&vertex_counts, m_time);

        VtArray<int> orders;
        m_usdPatch.GetTrimCurveOrdersAttr().Get(&orders, m_time);

        VtArray<double> knots;
        m_usdPatch.GetTrimCurveKnotsAttr().Get(&knots, m_time);

        VtArray<GfVec2d> ranges;
        m_usdPatch.GetTrimCurveRangesAttr().Get(&ranges, m_time);

        VtArray<GfVec3d> points;
        m_usdPatch.GetTrimCurvePointsAttr().Get(&points, m_time);

        // Split the ranges into separate arrays for the min & max values.
        auto min_vals = UTmakeIntrusive<GT_DANumeric<double>>(ranges.size(), 1);
        auto max_vals = UTmakeIntrusive<GT_DANumeric<double>>(ranges.size(), 1);
        for (size_t i = 0, n = ranges.size(); i < n; ++i)
        {
            min_vals->data()[i] = ranges[i][0];
            max_vals->data()[i] = ranges[i][1];
        }

        if (!counts.empty())
        {
            trim = UTmakeUnique<GT_TrimNuCurves>(
                    UTmakeIntrusive<GusdGT_VtArray<int>>(counts),
                    UTmakeIntrusive<GusdGT_VtArray<int>>(vertex_counts),
                    UTmakeIntrusive<GusdGT_VtArray<int>>(orders),
                    UTmakeIntrusive<GusdGT_VtArray<double>>(knots), min_vals,
                    max_vals, UTmakeIntrusive<GusdGT_VtArray<GfVec3d>>(points));
            if (!trim->isValid())
            {
                trim.reset();

                TF_WARN("Invalid trim curves for prim <%s>",
                        m_usdPatch.GetPath().GetText());
            }
        }
    }

    auto patch = UTmakeIntrusive<GT_PrimNuPatch>(
            uorder, uknots, vorder, vknots, vertex_attribs, detail_attribs);
    patch->setPrimitiveTransform(getPrimitiveTransform());
    patch->setTrimCurves(std::move(trim));

    // Reverse the orientation if needed.
    TfToken orientation;
    m_usdPatch.GetOrientationAttr().Get(&orientation, m_time);
    if (orientation == UsdGeomTokens->rightHanded)
        patch = patch->reverseU();

    refiner.addPrimitive(patch);
    return true;
}

GT_PrimitiveHandle
GusdNurbsPatchWrapper::defineForRead(const UsdGeomImageable &sourcePrim,
                               UsdTimeCode time, GusdPurposeSet purposes)
{
    return new GusdNurbsPatchWrapper(
            UsdGeomNurbsPatch(sourcePrim.GetPrim()), time, purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
