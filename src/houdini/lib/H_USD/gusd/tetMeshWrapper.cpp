/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "tetMeshWrapper.h"

#include "GT_VtArray.h"
#include "UT_Gf.h"

#include <GEO/GEO_PrimTetrahedron.h>
#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_Names.h>
#include <GT/GT_PrimTetMesh.h>
#include <GT/GT_Refine.h>

PXR_NAMESPACE_OPEN_SCOPE

GusdTetMeshWrapper::GusdTetMeshWrapper(
        const UsdGeomTetMesh &tet_mesh,
        UsdTimeCode time,
        GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), myUsdTetMesh(tet_mesh)
{
}

const char *
GusdTetMeshWrapper::className() const
{
    return "GusdTetMeshWrapper";
}

void
GusdTetMeshWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "GusdTetMeshWrapper::enlargeBounds not implemented");
}

int
GusdTetMeshWrapper::getMotionSegments() const
{
    return 1;
}

int64
GusdTetMeshWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
GusdTetMeshWrapper::doSoftCopy() const
{
    return UTmakeIntrusive<GusdTetMeshWrapper>(*this);
}

bool
GusdTetMeshWrapper::isValid() const
{
    return static_cast<bool>(myUsdTetMesh);
}

static void
gusdAddAttribute(
        const UT_StringHolder &dest_name,
        const char *usd_name,
        const char *prim_name,
        const GT_DataArrayHandle &data,
        const TfToken &interpolation,
        exint num_tets,
        exint num_pts,
        exint num_verts,
        GT_AttributeListHandle *vertex_attrs,
        GT_AttributeListHandle *point_attrs,
        GT_AttributeListHandle *uniform_attrs,
        GT_AttributeListHandle *detail_attrs)
{
    if (interpolation == UsdGeomTokens->vertex)
    {
        if (data->entries() < num_pts)
        {
            TF_WARN("Not enough values found for attribute: %s:%s", prim_name,
                    usd_name);
        }
        else
            *point_attrs = (*point_attrs)->addAttribute(dest_name, data, true);
    }
    else if (interpolation == UsdGeomTokens->faceVarying)
    {
        if (data->entries() < num_verts)
        {
            TF_WARN("Not enough values found for attribute: %s:%s", prim_name,
                    usd_name);
        }
        else
            *vertex_attrs = (*vertex_attrs)->addAttribute(dest_name, data, true);
    }
    else if (interpolation == UsdGeomTokens->uniform)
    {
        if (data->entries() < num_tets)
        {
            TF_WARN("Not enough values found for attribute: %s:%s", prim_name,
                    usd_name);
        }
        else
            *uniform_attrs = (*uniform_attrs)->addAttribute(dest_name, data, true);
    }
    else if (interpolation == UsdGeomTokens->constant)
    {
        if (data->entries() < 1)
        {
            TF_WARN("Not enough values found for attribute: %s:%s", prim_name,
                    usd_name);
        }
        else
            *detail_attrs = (*detail_attrs)->addAttribute(dest_name, data, true);
    }
    else
    {
        TF_WARN("Unsupported interpolation type: %s", interpolation.GetText());
    }
}

static UT_Vector3i
gusdSurfaceFaceKey(const GfVec3i &face)
{
    UT_Vector3i result = GusdUT_Gf::Cast(face);

    // Sort the vertex indices.
    if (result[0] > result[1])
        std::swap(result[0], result[1]);
    if (result[0] > result[2])
        std::swap(result[0], result[2]);
    if (result[1] > result[2])
        std::swap(result[1], result[2]);

    return result;
}

static GfVec3i
gusdGetTetFace(const GfVec4i &tet_vertices, int face)
{
    const int *face_vertices = GEO_PrimTetrahedron::fastFaceIndices(face);
    return GfVec3i(
            tet_vertices[face_vertices[0]], tet_vertices[face_vertices[1]],
            tet_vertices[face_vertices[2]]);
}

bool
GusdTetMeshWrapper::refine(GT_Refine &refiner, const GT_RefineParms *parms) const
{
    if (!isValid())
    {
        TF_WARN("Invalid prim");
        return false;
    }

    const bool for_viewport = GT_GEOPrimPacked::useViewportLOD(parms);

    VtVec4iArray tet_vtx_indices;
    if (!myUsdTetMesh.GetTetVertexIndicesAttr().Get(&tet_vtx_indices, m_time)
        || tet_vtx_indices.empty())
    {
        TF_WARN("tetVertexIndices could not be read from prim: <%s>",
                myUsdTetMesh.GetPath().GetText());
        return false;
    }

    TfToken orientation;
    const bool reverse_winding = myUsdTetMesh.GetOrientationAttr().Get(
                                         &orientation, m_time)
                                 && orientation == UsdGeomTokens->rightHanded;
    if (reverse_winding)
    {
        for (GfVec4i &tet_verts : tet_vtx_indices)
            std::swap(tet_verts[0], tet_verts[3]);
    }

    // Convert to a flat list of integers for GT.
    auto vtx_list = UTmakeIntrusive<GT_Int32Array>(
            tet_vtx_indices.front().data(), tet_vtx_indices.size() * 4, 1);

    auto pt_attribs = UTmakeIntrusive<GT_AttributeList>(
            UTmakeIntrusive<GT_AttributeMap>());
    auto vtx_attribs = UTmakeIntrusive<GT_AttributeList>(
            UTmakeIntrusive<GT_AttributeMap>());
    auto uniform_attribs = UTmakeIntrusive<GT_AttributeList>(
            UTmakeIntrusive<GT_AttributeMap>());
    auto constant_attribs = UTmakeIntrusive<GT_AttributeList>(
            UTmakeIntrusive<GT_AttributeMap>());

    VtVec3fArray points;
    if (!myUsdTetMesh.GetPointsAttr().Get(&points, m_time))
    {
        TF_WARN("points could not be read from prim: <%s>",
                myUsdTetMesh.GetPath().GetText());
        return false;
    }
    pt_attribs = pt_attribs->addAttribute(
            GA_Names::P,
            UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(points, GT_TYPE_POINT),
            true);

    const exint num_pts = points.size();
    const exint num_tets = tet_vtx_indices.size();
    const exint num_verts = 4 * num_tets;

    if (reverse_winding)
        addReversePolygonsAttrib(uniform_attribs, num_tets);

    VtVec3fArray normals;
    if (myUsdTetMesh.GetNormalsAttr().Get(&normals, m_time))
    {
        auto gt_normals = UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(
                normals, GT_TYPE_NORMAL);
        TfToken interp = myUsdTetMesh.GetNormalsInterpolation();

        gusdAddAttribute(
                GA_Names::N,
                myUsdTetMesh.GetNormalsAttr().GetBaseName().GetText(),
                myUsdTetMesh.GetPrim().GetPath().GetText(), gt_normals, interp,
                num_tets, num_pts, num_verts, &vtx_attribs, &pt_attribs,
                &uniform_attribs, &constant_attribs);
    }

    if (!for_viewport)
    {
        VtVec3fArray velocities;
        if (myUsdTetMesh.GetVelocitiesAttr().Get(&velocities, m_time))
        {
            auto gt_vels = UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(
                    velocities, GT_TYPE_VECTOR);

            gusdAddAttribute(
                    GA_Names::v,
                    myUsdTetMesh.GetVelocitiesAttr().GetBaseName().GetText(),
                    myUsdTetMesh.GetPrim().GetPath().GetText(), gt_vels,
                    UsdGeomTokens->vertex, num_tets, num_pts, num_verts,
                    &vtx_attribs, &pt_attribs, &uniform_attribs,
                    &constant_attribs);
        }

        VtVec3fArray accels;
        if (myUsdTetMesh.GetAccelerationsAttr().Get(&accels, m_time))
        {
            auto gt_accels = UTmakeIntrusive<GusdGT_VtArray<GfVec3f>>(
                    accels, GT_TYPE_VECTOR);

            gusdAddAttribute(
                    GA_Names::accel,
                    myUsdTetMesh.GetAccelerationsAttr().GetBaseName().GetText(),
                    myUsdTetMesh.GetPrim().GetPath().GetText(), gt_accels,
                    UsdGeomTokens->vertex, num_tets, num_pts, num_verts,
                    &vtx_attribs, &pt_attribs, &uniform_attribs,
                    &constant_attribs);
        }
    }

    loadPrimvars(
            *myUsdTetMesh.GetSchemaClassPrimDefinition(), m_time, parms,
            num_tets, num_pts, num_verts, myUsdTetMesh.GetPath().GetString(),
            &vtx_attribs, &pt_attribs, &uniform_attribs, &constant_attribs);

    UT_IntrusivePtr<GT_Int32Array> face_verts;
    if (for_viewport)
    {
        // Build the face-vertex topology, which is required if the tet mesh is
        // refined for display inside a packed USD primitive.
        face_verts = UTmakeIntrusive<GT_Int32Array>(4 * num_tets, 3);
        for (exint tet_idx = 0; tet_idx < num_tets; ++tet_idx)
        {
            for (int i = 0; i < 4; ++i)
            {
                const exint face_idx = 4 * tet_idx + i;
                face_verts->setTuple(
                        GEO_PrimTetrahedron::fastFaceIndices(i), face_idx);
            }
        }

        // Build the shared faces mask from the list of surface faces, which
        // typically should already be authored on the USD tet mesh.
        // This is only required when refining for display purposes.
        VtVec3iArray surf_faces;
        if (!myUsdTetMesh.GetSurfaceFaceVertexIndicesAttr().Get(&surf_faces, m_time))
        {
            // Compute if necessary.
            if (!UsdGeomTetMesh::ComputeSurfaceFaces(
                        myUsdTetMesh, &surf_faces, m_time))
            {
                TF_WARN("Surface faces could not be computed for prim: <%s>",
                        myUsdTetMesh.GetPath().GetText());
                return false;
            }
        }

        // Convert to a hash table, with the keys containing the *sorted*
        // vertices of each surface face. This also means that the 'orientation'
        // property does not matter here.
        UT_Set<UT_Vector3i> surf_face_set;
        surf_face_set.reserve(surf_faces.size());
        for (const GfVec3i &face : surf_faces)
            surf_face_set.insert(gusdSurfaceFaceKey(face));

        auto shared_faces = UTmakeIntrusive<GT_Int32Array>(num_tets, 1);
        for (exint tet_idx = 0; tet_idx < num_tets; ++tet_idx)
        {
            int32 smask = 0;

            // Check each face to see if it's shared, and add it to the mask.
            const GfVec4i &tet_vertices = tet_vtx_indices[tet_idx];
            for (int i = 0; i < 4; ++i)
            {
                if (!surf_face_set.contains(gusdSurfaceFaceKey(
                            gusdGetTetFace(tet_vertices, i))))
                {
                    smask |= (1 << i);
                }
            }

            shared_faces->set(smask, tet_idx);
        }

        uniform_attribs = uniform_attribs->addAttribute(
                GT_Names::sharedface, shared_faces, true);
    }

    GT_ElementSetMapPtr tet_sets, point_sets;
    if (!for_viewport)
    {
        loadSubsets(
                myUsdTetMesh, constant_attribs,
                /*uniform_element_type=*/UsdGeomTokens->tetrahedron, tet_sets,
                uniform_attribs, num_tets, point_sets, pt_attribs, num_pts,
                parms, m_time);
    }

    auto gt_mesh = UTmakeIntrusive<GT_PrimTetMesh>(
            vtx_list, vtx_attribs, pt_attribs, uniform_attribs,
            constant_attribs, face_verts);
    gt_mesh->setPrimitiveTransform(getPrimitiveTransform());
    gt_mesh->setPointSetMap(point_sets);
    gt_mesh->setTetSetMap(tet_sets);

    refiner.addPrimitive(gt_mesh);
    return true;
}

GT_PrimitiveHandle
GusdTetMeshWrapper::defineForRead(
        const UsdGeomImageable &source_prim,
        UsdTimeCode time,
        GusdPurposeSet purposes)
{
    return UTmakeIntrusive<GusdTetMeshWrapper>(
            UsdGeomTetMesh(source_prim.GetPrim()), time, purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
