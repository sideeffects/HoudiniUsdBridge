//
// Copyright 2017 Pixar
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
#include "NURBSCurvesWrapper.h"

#include "context.h"
#include "GT_VtArray.h"
#include "tokens.h"
#include "USD_XformCache.h"
#include "UT_Gf.h"

#include <GT/GT_DAIndirect.h>
#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_PrimCurveMesh.h>
#include <GT/GT_PrimNURBSCurveMesh.h>
#include <GT/GT_Refine.h>
#include <GT/GT_RefineParms.h>

#include "pxr/usd/usdGeom/primvarsAPI.h"

#include <numeric>

PXR_NAMESPACE_OPEN_SCOPE

using std::map;

namespace {

map<GT_Basis, TfToken> gtToUsdBasisTranslation = {
    { GT_BASIS_BEZIER,      UsdGeomTokens->bezier },
    { GT_BASIS_BSPLINE,     UsdGeomTokens->bspline },
    { GT_BASIS_CATMULLROM,  UsdGeomTokens->catmullRom },
    { GT_BASIS_CATMULL_ROM, UsdGeomTokens->catmullRom },
    { GT_BASIS_HERMITE,     UsdGeomTokens->hermite }};

map<TfToken,GT_Basis> usdToGtBasisTranslation = {
    { UsdGeomTokens->bezier,     GT_BASIS_BEZIER },
    { UsdGeomTokens->bspline,    GT_BASIS_BSPLINE },
    { UsdGeomTokens->catmullRom, GT_BASIS_CATMULLROM },
    { UsdGeomTokens->hermite,    GT_BASIS_HERMITE }};

} // end of namespace

GusdNURBSCurvesWrapper::
GusdNURBSCurvesWrapper(
        const GT_PrimitiveHandle& sourcePrim,
        const UsdStagePtr& stage,
        const SdfPath& path,
        bool isOverride )
{
    initUsdPrim( stage, path, isOverride );
}

GusdNURBSCurvesWrapper::
GusdNURBSCurvesWrapper(
        const UsdGeomNurbsCurves&   usdCurves, 
        UsdTimeCode                 time,
        GusdPurposeSet              purposes )
    : GusdPrimWrapper( time, purposes )
    , m_usdCurves( usdCurves )
{
}

GusdNURBSCurvesWrapper::
~GusdNURBSCurvesWrapper()
{}

bool GusdNURBSCurvesWrapper::
initUsdPrim(const UsdStagePtr& stage,
            const SdfPath& path,
            bool asOverride)
{
    bool newPrim = true;
    if( asOverride ) {
        UsdPrim existing = stage->GetPrimAtPath( path );
        if( existing ) {
            newPrim = false;
            m_usdCurves = UsdGeomNurbsCurves(stage->OverridePrim( path ));
        }
        else {
            // When fracturing, we want to override the outside surfaces and create
            // new inside surfaces in one export. So if we don't find an existing prim
            // with the given path, create a new one.
            m_usdCurves = UsdGeomNurbsCurves::Define( stage, path );
        }
    }
    else {
        m_usdCurves = UsdGeomNurbsCurves::Define( stage, path );  
    }
    if( !m_usdCurves || !m_usdCurves.GetPrim().IsValid() ) {
        TF_WARN( "Unable to create %s NURBS curves '%s'.", newPrim ? "new" : "override", path.GetText() );
    }
    return bool( m_usdCurves );
}

GT_PrimitiveHandle GusdNURBSCurvesWrapper::
defineForWrite(
        const GT_PrimitiveHandle& sourcePrim,
        const UsdStagePtr& stage,
        const SdfPath& path,
        const GusdContext& ctxt)
{
    return new GusdNURBSCurvesWrapper( 
                    sourcePrim,
                    stage, 
                    path, 
                    ctxt.writeOverlay);
}

GT_PrimitiveHandle GusdNURBSCurvesWrapper::
defineForRead(
        const UsdGeomImageable& sourcePrim, 
        UsdTimeCode             time,
        GusdPurposeSet          purposes )
{
    return new GusdNURBSCurvesWrapper( 
                    UsdGeomNurbsCurves( sourcePrim.GetPrim() ),
                    time, 
                    purposes );
}

bool GusdNURBSCurvesWrapper::
redefine( const UsdStagePtr& stage,
          const SdfPath& path,
          const GusdContext& ctxt,
          const GT_PrimitiveHandle& sourcePrim )
{
    initUsdPrim( stage, path, ctxt.writeOverlay);
    clearCaches();
    return true;
}

static void
gusdAddAttribute(
        const UT_StringHolder &dest_name,
        const char *usd_name,
        const char *prim_name,
        const GT_DataArrayHandle &data,
        const TfToken &interpolation,
        const GT_DataArrayHandle &seg_end_pt_indices,
        exint num_curves,
        exint num_pts,
        exint num_segment_end_pts,
        GT_AttributeListHandle &vertex_attrs,
        GT_AttributeListHandle &uniform_attrs,
        GT_AttributeListHandle &detail_attrs)
{
    if (interpolation == UsdGeomTokens->varying)
    {
        if (data->entries() < num_segment_end_pts)
        {
            TF_WARN("Not enough values found for attribute: %s:%s", prim_name,
                    usd_name);
        }
        else
        {
            vertex_attrs = vertex_attrs->addAttribute(
                    dest_name,
                    UTmakeIntrusive<GT_DAIndirect>(seg_end_pt_indices, data),
                    true);
        }
    }
    else if (interpolation == UsdGeomTokens->vertex)
    {
        if (data->entries() < num_pts)
        {
            TF_WARN("Not enough values found for attribute: %s:%s", prim_name,
                    usd_name);
        }
        else
            vertex_attrs = vertex_attrs->addAttribute(dest_name, data, true);
    }
    else if (interpolation == UsdGeomTokens->uniform)
    {
        if (data->entries() < num_curves)
        {
            TF_WARN("Not enough values found for attribute: %s:%s", prim_name,
                    usd_name);
        }
        else
            uniform_attrs = uniform_attrs->addAttribute(dest_name, data, true);
    }
    else if (interpolation == UsdGeomTokens->constant)
    {
        if (data->entries() < 1)
        {
            TF_WARN("Not enough values found for attribute: %s:%s", prim_name,
                    usd_name);
        }
        else
            detail_attrs = detail_attrs->addAttribute(dest_name, data, true);
    }
    else
    {
        TF_WARN("Unsupported interpolation type: %s", interpolation.GetText());
    }
}

static UT_IntrusivePtr<GT_Int32Array>
gusdBuildSegEndPointIndices(
        exint num_points,
        const VtIntArray& counts,
        const VtIntArray& orders)
{
    // Build a array that maps `varying` interpolation values, defined on
    // segment end points, to vertices. Depending on the curve's order there may
    // be fewer segment end points than vertices (e.g. 2 less for cubic curves),
    // in which case we duplicate the first and last values.
    auto seg_end_point_indices = UTmakeIntrusive<GT_Int32Array>(num_points, 1);

    GT_Offset src_idx = 0;
    GT_Offset dst_idx = 0;
    for (size_t curve_idx = 0, num_curves = counts.size();
         curve_idx < num_curves; ++curve_idx)
    {
        const int curve_vtx_count = counts[curve_idx];
        const int curve_degree = orders[curve_idx] - 1;

        const int num_seg_end_pts = curve_vtx_count - curve_degree + 1;
        int extra_pts = curve_vtx_count - num_seg_end_pts;
        UT_ASSERT(extra_pts >= 0);
        extra_pts = SYSmax(extra_pts, 0);

        // Note if there is an odd number of extra points, the rounding here
        // means we will duplicate the last value an extra time.
        const int extra_start_pts = extra_pts / 2;

        int i = 0;
        for ( ; i < extra_start_pts; ++i)
            seg_end_point_indices->set(src_idx, dst_idx++);

        for ( ; i < (extra_start_pts + num_seg_end_pts - 1); ++i)
            seg_end_point_indices->set(src_idx++, dst_idx++);

        for ( ; i < curve_vtx_count; ++i)
            seg_end_point_indices->set(src_idx, dst_idx++);

        ++src_idx;
    }

    return seg_end_point_indices;
}

bool 
GusdNURBSCurvesWrapper::refine( 
    GT_Refine& refiner, 
    const GT_RefineParms* parms) const
{
    if(!isValid()) 
        return false;

    bool refineForViewport = GT_GEOPrimPacked::useViewportLOD(parms);

    const UsdGeomNurbsCurves& usdCurves = m_usdCurves;

    GT_AttributeListHandle gtVertexAttrs = new GT_AttributeList( new GT_AttributeMap() );
    GT_AttributeListHandle gtUniformAttrs = new GT_AttributeList( new GT_AttributeMap() );
    GT_AttributeListHandle gtDetailAttrs = new GT_AttributeList( new GT_AttributeMap() );

   // vertex counts
    UsdAttribute countsAttr = usdCurves.GetCurveVertexCountsAttr();
    if(!countsAttr) {
        TF_WARN( "Invalid USD vertex count attribute for NURB Curve. %s",
                 usdCurves.GetPrim().GetPath().GetText() );
        return false;
    }

    VtIntArray usdCounts;
    countsAttr.Get(&usdCounts, m_time);
    auto gtVertexCounts = new GusdGT_VtArray<int32>( usdCounts );

    UsdAttribute orderAttr = usdCurves.GetOrderAttr();
    if( !orderAttr ) {
        TF_WARN( "Invalid USD order attribute for NURB Curve. %s",
                 usdCurves.GetPrim().GetPath().GetText() );
        return false;
    }

    VtIntArray usdOrder;
    orderAttr.Get( &usdOrder, m_time );

    if( usdOrder.size() != usdCounts.size() ) {
        TF_WARN("Incorrect number of values given for USD order attribute for "
                "NURB Curve. %s",
                usdCurves.GetPrim().GetPath().GetText());
        return false;
    }
    GT_DataArrayHandle gtOrder = new GusdGT_VtArray<int32>( usdOrder );

    const int num_curves = usdCounts.size();
    if (num_curves == 0)
        return false; // Nothing to do.

    int numPoints = std::accumulate(usdCounts.begin(), usdCounts.end(), 0);
    int totalOrder = std::accumulate(usdOrder.begin(), usdOrder.end(), 0);
    int numSegs = numPoints - totalOrder + usdCounts.size(); // # of points minus degree.
    int numSegEndPoints = numSegs + usdCounts.size();
    int numKnots = numPoints + totalOrder;

    // point positions
    UsdAttribute pointsAttr = usdCurves.GetPointsAttr();
    if(!pointsAttr) {
        TF_WARN( "Invalid USD points attribute for NURB Curve. %s",
                 usdCurves.GetPrim().GetPath().GetText() );
        return false;
    }

    VtVec3fArray usdPoints;
    pointsAttr.Get(&usdPoints, m_time);

    if( usdPoints.size() < numPoints ) {
        TF_WARN( "Not enough points specified for NURBS Curve. %s. Expected %d, got %zd",
                 usdCurves.GetPrim().GetPath().GetText(),
                 numPoints, usdPoints.size() );
        return false;
    }

    GT_DataArrayHandle gtPoints = new GusdGT_VtArray<GfVec3f>(usdPoints,GT_TYPE_POINT);
    gtVertexAttrs = gtVertexAttrs->addAttribute( "P", gtPoints, true );

    GT_Basis basis = GT_BASIS_LINEAR;
    GT_DataArrayHandle gtKnots;

    if( !refineForViewport ) {

        basis = GT_BASIS_BSPLINE;

        UsdAttribute knotsAttr = usdCurves.GetKnotsAttr();
        if( !knotsAttr ) {
            TF_WARN( "Invalid USD order attribute for NURB Curve. %s",
                     usdCurves.GetPrim().GetPath().GetText() );
        } 
        else {        
            VtDoubleArray usdKnots;
            knotsAttr.Get( &usdKnots, m_time );

            if( usdKnots.size() >= numKnots ) {
                gtKnots = new GusdGT_VtArray<fpreal64>( usdKnots );
            }
            else if ( usdKnots.size() == numKnots - 2 ) {

                // There was a time when the maya exported did not duplicate
                // the end points when it should. 
                auto knotArray = new GT_Real64Array( numKnots, 1 );
                knotArray->set( usdKnots[0], 0 );
                for( size_t i = 0; i < usdKnots.size(); ++i ) {
                    knotArray->set( usdKnots[i], i+1 );
                }
                knotArray->set( usdKnots[usdKnots.size()-1], numKnots-1 );
                gtKnots = knotArray;
            }
            else {
                TF_WARN( "Not enough NURBS curve knot values specified. %s. Expected %d, got %zd",
                     usdCurves.GetPrim().GetPath().GetText(),
                     numKnots, usdKnots.size() );
            }
        }

        auto seg_end_point_indices = gusdBuildSegEndPointIndices(
                numPoints, usdCounts, usdOrder);

        UsdAttribute widthsAttr = usdCurves.GetWidthsAttr();
        VtFloatArray usdWidths;
        if( widthsAttr.Get(&usdWidths, m_time) )
        {
            // Convert from diameter to radius for pscale.
            for (fpreal32 &val : usdWidths)
                val *= 0.5;

            gusdAddAttribute(
                    GA_Names::pscale, widthsAttr.GetName().GetText(),
                    usdCurves.GetPath().GetText(),
                    UTmakeIntrusive<GusdGT_VtArray<fpreal32>>(usdWidths),
                    usdCurves.GetWidthsInterpolation(), seg_end_point_indices,
                    num_curves, numPoints, numSegEndPoints, gtVertexAttrs,
                    gtUniformAttrs, gtDetailAttrs);
        }
        // velocities
        UsdAttribute velAttr = usdCurves.GetVelocitiesAttr();
        VtVec3fArray vtVec3Array;
        if( velAttr.Get(&vtVec3Array, m_time) )
        {
            gusdAddAttribute(
                    GA_Names::v, velAttr.GetName().GetText(),
                    usdCurves.GetPath().GetText(),
                    UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(
                            vtVec3Array, GT_TYPE_VECTOR),
                    UsdGeomTokens->vertex, seg_end_point_indices, num_curves,
                    numPoints, numSegEndPoints, gtVertexAttrs, gtUniformAttrs,
                    gtDetailAttrs);
        }
        // accelerations
        UsdAttribute accelAttr = usdCurves.GetAccelerationsAttr();
        if( accelAttr.Get(&vtVec3Array, m_time) )
        {
            gusdAddAttribute(
                    GA_Names::accel, accelAttr.GetName().GetText(),
                    usdCurves.GetPath().GetText(),
                    UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(
                            vtVec3Array, GT_TYPE_VECTOR),
                    UsdGeomTokens->vertex, seg_end_point_indices, num_curves,
                    numPoints, numSegEndPoints, gtVertexAttrs, gtUniformAttrs,
                    gtDetailAttrs);
        }
        // normals
        UsdAttribute normAttr = usdCurves.GetNormalsAttr();
        VtVec3fArray usdNormals;
        if(normAttr.Get(&usdNormals, m_time) )
        {
            gusdAddAttribute(
                    GA_Names::N, normAttr.GetName().GetText(),
                    usdCurves.GetPath().GetText(),
                    UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(
                            usdNormals, GT_TYPE_NORMAL),
                    usdCurves.GetNormalsInterpolation(), seg_end_point_indices,
                    num_curves, numPoints, numSegEndPoints, gtVertexAttrs,
                    gtUniformAttrs, gtDetailAttrs);
        }
        // Load primvars. segEndPointIndicies are used if we need to expand primvar arrays
        // from a value at segment end points to values in point attributes.
        loadPrimvars(*m_usdCurves.GetSchemaClassPrimDefinition(), m_time, parms,
                     usdCounts.size(), usdPoints.size(), numSegEndPoints,
                     usdCurves.GetPath().GetString(), NULL, &gtVertexAttrs,
                     &gtUniformAttrs, &gtDetailAttrs, seg_end_point_indices);
    } else {

        UsdGeomPrimvar colorPrimvar = UsdGeomPrimvarsAPI(
            usdCurves).GetPrimvar(GusdTokens->Cd);
        if( !colorPrimvar || !colorPrimvar.GetAttr().HasAuthoredValue() ) {
            colorPrimvar = UsdGeomPrimvarsAPI(
                usdCurves).GetPrimvar(GusdTokens->displayColor);
        }

        if( colorPrimvar && colorPrimvar.GetAttr().HasAuthoredValue()) {
            UT_IntrusivePtr<GT_Int32Array> seg_end_point_indices;
            if (colorPrimvar.GetInterpolation() == UsdGeomTokens->varying)
            {
                seg_end_point_indices = gusdBuildSegEndPointIndices(
                        numPoints, usdCounts, usdOrder);
            }

            gusdAddAttribute(
                    GA_Names::Cd, colorPrimvar.GetName().GetText(),
                    usdCurves.GetPath().GetText(),
                    convertPrimvarData(colorPrimvar, m_time),
                    colorPrimvar.GetInterpolation(), seg_end_point_indices,
                    num_curves, numPoints, numSegEndPoints, gtVertexAttrs,
                    gtUniformAttrs, gtDetailAttrs);
        }
    }

    auto prim = new GT_PrimCurveMesh( 
        basis, 
        gtVertexCounts,
        gtVertexAttrs,
        gtUniformAttrs,
        gtDetailAttrs,
        false );

    if( !refineForViewport ) {
        if( gtOrder ) {
            prim->setOrder( gtOrder );
        }
        if( gtKnots ) {
            prim->setKnots( gtKnots );
        }
    }

    // set local transform
    UT_Matrix4D mat;

    if( !GusdUSD_XformCache::GetInstance().GetLocalToWorldTransform( 
            usdCurves.GetPrim(),
            m_time, 
            mat ) ) {
        TF_WARN( "Failed to compute transform" );
        return false;
    }

    prim->setPrimitiveTransform( getPrimitiveTransform() );
    refiner.addPrimitive( prim );
    return true;
}


const char* GusdNURBSCurvesWrapper::
className() const
{
    return "GusdNURBSCurvesWrapper";
}


void GusdNURBSCurvesWrapper::
enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    // TODO
}


int GusdNURBSCurvesWrapper::
getMotionSegments() const
{
    // TODO
    return 1;
}


int64 GusdNURBSCurvesWrapper::
getMemoryUsage() const
{
    // TODO
    return 0;
}


GT_PrimitiveHandle GusdNURBSCurvesWrapper::
doSoftCopy() const
{
    // TODO
    return GT_PrimitiveHandle(new GusdNURBSCurvesWrapper(*this));
}


bool 
GusdNURBSCurvesWrapper::isValid() const
{
    return bool( m_usdCurves );
}

bool 
GusdNURBSCurvesWrapper::updateFromGTPrim(
     const GT_PrimitiveHandle&  sourcePrim,
     const UT_Matrix4D&         houXform,
     const GusdContext&         ctxt,
     GusdSimpleXformCache&      xformCache
      )
{
    if( !m_usdCurves ) {
        TF_WARN( "Attempting to update invalid curve prim" );
        return false;
    }

    const GT_PrimCurveMesh* gtCurves
        = dynamic_cast<const GT_PrimCurveMesh*>(sourcePrim.get());
    if(!gtCurves) {
        TF_WARN( "Attempting to update curve of wrong type %s", sourcePrim->className() );
        return false;
    }

    bool overlayTransforms = ctxt.overlayTransforms;

    // While I suppose we could write both points and transforms, it gets confusing,
    // and I don't this its necessary so lets not.
    if( ctxt.overlayPoints || ctxt.overlayAll ) {
        overlayTransforms = false;
    }

    UsdTimeCode geoTime = ctxt.time;
    if( ctxt.writeStaticGeo ) {
        geoTime = UsdTimeCode::Default();
    }

    GfMatrix4d xform = computeTransform( 
                            m_usdCurves.GetPrim().GetParent(),
                            geoTime,
                            houXform,
                            xformCache );

    GfMatrix4d loc_xform = computeTransform( 
                            m_usdCurves.GetPrim(),
                            geoTime,
                            houXform,
                            xformCache );

    // If we are writing points for an overlay but not writing transforms, 
    // then we have to transform the points into the proper space.
    bool transformPoints = 
        (ctxt.overlayPoints || ctxt.overlayAll) && 
        !GusdUT_Gf::Cast(loc_xform).isIdentity();

    GT_Owner attrOwner = GT_OWNER_INVALID;
    GT_DataArrayHandle houAttr;
    UsdAttribute usdAttr;
    
    if( !ctxt.writeOverlay && ctxt.purpose != UsdGeomTokens->default_ ) {
        m_usdCurves.GetPurposeAttr().Set( ctxt.purpose );
    }

    // intrinsic attributes ----------------------------------------------------

    if( !ctxt.writeOverlay || ctxt.overlayAll || overlayTransforms || ctxt.overlayPoints ) {

        // extent ------------------------------------------------------------------
        houAttr = GusdGT_Utils::getExtentsArray(sourcePrim);
        usdAttr = m_usdCurves.GetExtentAttr();
        if(houAttr && usdAttr && transformPoints ) {
             houAttr = GusdGT_Utils::transformPoints( houAttr, loc_xform );
        }
        updateAttributeFromGTPrim( GT_OWNER_INVALID, "extents", houAttr, usdAttr, geoTime );
    }

    // transform ---------------------------------------------------------------
    if( !ctxt.writeOverlay || ctxt.overlayAll || overlayTransforms) {
        updateTransformFromGTPrim( xform, geoTime, 
                                   ctxt.granularity == GusdContext::PER_FRAME );
    }

    // visibility ---------------------------------------------------------------

    updateVisibilityFromGTPrim(sourcePrim, geoTime, 
                               (!ctxt.writeOverlay || ctxt.overlayAll) && 
                                ctxt.granularity == GusdContext::PER_FRAME );

    if( !ctxt.writeOverlay || ctxt.overlayAll || ctxt.overlayPoints ) {
        
        // P
        houAttr = sourcePrim->findAttribute("P", attrOwner, 0);
        usdAttr = m_usdCurves.GetPointsAttr();
        if(houAttr && usdAttr && transformPoints ) {
            houAttr = GusdGT_Utils::transformPoints( houAttr, loc_xform );
        }
        updateAttributeFromGTPrim( attrOwner, "P", houAttr, usdAttr, geoTime );
    }

    if( !ctxt.writeOverlay || ctxt.overlayAll ) {

        UsdTimeCode topologyTime = ctxt.time;
        if( ctxt.writeStaticTopology ) {
            topologyTime = UsdTimeCode::Default();
        }

        // Vertex counts
        usdAttr = m_usdCurves.GetCurveVertexCountsAttr();
        auto gtCurveCounts = gtCurves->getCurveCounts();

        updateAttributeFromGTPrim( GT_OWNER_INVALID, "vertexcounts",
                                   gtCurveCounts, usdAttr, topologyTime );

        // Order
        usdAttr = m_usdCurves.GetOrderAttr();
        if( gtCurves->isUniformOrder() ) {
            VtIntArray val( gtCurveCounts->entries() );
            for( size_t i = 0; i < val.size(); ++i ) {
                val[i] = gtCurves->uniformOrder();
            }
            usdAttr.Set( val );
        }
        else {
            GT_DataArrayHandle buffer;
            const int64* gtOrders = gtCurves->varyingOrders()->getI64Array( buffer );
            VtIntArray val( gtCurves->varyingOrders()->entries() );
             for( size_t i = 0; i < val.size(); ++i ) {
                val[i] = gtOrders[i];
            }
            usdAttr.Set( val );           
        }

        // Knots 
        GT_DataArrayHandle knotTmpBuffer;
        usdAttr = m_usdCurves.GetKnotsAttr();
        GT_DataArrayHandle gtKnots = gtCurves->knots();
        if( gtKnots->getStorage() != GT_STORE_REAL64 ) {
            gtKnots = new GT_Real64Array( gtKnots->getF64Array( knotTmpBuffer ),
                                          gtKnots->entries(), 1 );
        }

        updateAttributeFromGTPrim( GT_OWNER_INVALID, "knots",
                                   gtKnots, usdAttr, geoTime );      
    }


    if( !ctxt.writeOverlay || ctxt.overlayAll || ctxt.overlayPoints ) {
        // N
        houAttr = sourcePrim->findAttribute("N", attrOwner, 0);
        usdAttr = m_usdCurves.GetNormalsAttr();
        updateAttributeFromGTPrim( attrOwner, "N", houAttr, usdAttr, geoTime );

        // v
        houAttr = sourcePrim->findAttribute("v", attrOwner, 0);
        usdAttr = m_usdCurves.GetVelocitiesAttr();
        updateAttributeFromGTPrim( attrOwner, "v", houAttr, usdAttr, geoTime );
        
        // pscale & width
        houAttr = sourcePrim->findAttribute("width", attrOwner, 0);
        if(!houAttr) {
            houAttr = sourcePrim->findAttribute("pscale", attrOwner, 0);
        }

        usdAttr = m_usdCurves.GetWidthsAttr();

        updateAttributeFromGTPrim( attrOwner, "width", houAttr, usdAttr, geoTime );
        m_usdCurves.SetWidthsInterpolation( UsdGeomTokens->vertex );
    }

    // -------------------------------------------------------------------------
    
    // primvars ----------------------------------------------------------------
    
    if( !ctxt.writeOverlay || ctxt.overlayAll || ctxt.overlayPrimvars ) {

        UsdTimeCode primvarTime = ctxt.time;
        if( ctxt.writeStaticPrimvars ) {
            primvarTime = UsdTimeCode::Default();
        }

        GusdGT_AttrFilter filter = ctxt.attributeFilter;

        filter.appendPattern(GT_OWNER_VERTEX, "^P ^N ^v ^width ^pscale ^visible");
        if(const GT_AttributeListHandle vtxAttrs = sourcePrim->getVertexAttributes()) {
            GusdGT_AttrFilter::OwnerArgs owners;
            owners << GT_OWNER_VERTEX;
            filter.setActiveOwners(owners);
            updatePrimvarFromGTPrim( 
                vtxAttrs, filter, UsdGeomTokens->vertex, primvarTime );
        }
        filter.appendPattern(GT_OWNER_CONSTANT, "^visible");
        if(const GT_AttributeListHandle constAttrs = sourcePrim->getDetailAttributes()) {
            GusdGT_AttrFilter::OwnerArgs owners;
            owners << GT_OWNER_CONSTANT;
            filter.setActiveOwners(owners);
            updatePrimvarFromGTPrim( constAttrs, filter, UsdGeomTokens->constant, primvarTime );
        }
        filter.appendPattern(GT_OWNER_UNIFORM, "^visible");
        if(const GT_AttributeListHandle uniformAttrs = sourcePrim->getUniformAttributes()) {
            GusdGT_AttrFilter::OwnerArgs owners;
            owners << GT_OWNER_UNIFORM;
            filter.setActiveOwners(owners);
            updatePrimvarFromGTPrim( uniformAttrs, filter, UsdGeomTokens->uniform, primvarTime );
        }

        // If we have a "Cd" attribute, write it as both "Cd" and "displayColor".
        // The USD guys promise me that this data will get "deduplicated" so there
        // is not cost for doing this.
        GT_Owner own;
        if(GT_DataArrayHandle Cd = sourcePrim->findAttribute( "Cd", own, 0 )) {

            GT_AttributeMapHandle attrMap = new GT_AttributeMap();
            GT_AttributeListHandle attrList = new GT_AttributeList( attrMap );
            attrList = attrList->addAttribute( "displayColor", Cd, true );
            GusdGT_AttrFilter filter( "*" );
            GusdGT_AttrFilter::OwnerArgs owners;
            owners << own;
            filter.setActiveOwners(owners);
            updatePrimvarFromGTPrim( attrList, filter, s_ownerToUsdInterpCurve[own], primvarTime );
        }
    }

    // -------------------------------------------------------------------------
    return GusdPrimWrapper::updateFromGTPrim(sourcePrim, houXform, ctxt, xformCache);
}

PXR_NAMESPACE_CLOSE_SCOPE


