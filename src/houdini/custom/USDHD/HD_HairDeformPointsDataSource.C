/*
 * Copyright 2025 Side Effects Software Inc.
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

#include "HD_HairDeformPointsDataSource.h"
#include "HD_HairDeformSchema.h"
#include "HD_HairDeformUtils.h"

#include <iostream>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>

#include <CE/CE_Array.h>
#include <CE/CE_Context.h>
#include <GA/GA_PolyCounts.h>
#include <GA/GA_SplittableRange.h>
#include <GEO/GEO_Interpolate.h>
#include <GEO/GEO_PrimPoly.h>
#include <GEO/GEO_PrimTetrahedron.h>
#include <GA/GA_AIFNumericArray.h>
#include <GU/GU_Cosserat.h>
#include <GU/GU_Detail.h>
#include <GU/GU_GuideCapture.h>
#include <GU/GU_GroomUtils.h>
#include <GU/GU_PointCapture.h>
#include <GU/GU_SurfaceDeform.h>
#include <GU/GU_PointDeform.h>
#include <GU/GU_RayIntersect.h>
#include <GEO/GEO_PointTree.h>
#include <SYS/SYS_Math.h>
#include <UT/UT_Array.h>
#include <UT/UT_BoundingBox.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_Optional.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_ScopeExit.h>
#include <UT/UT_Tracing.h>
#include <SYS/SYS_Compiler.h>

#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/hd/basisCurvesSchema.h>
#include <pxr/imaging/hd/basisCurvesTopologySchema.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/meshTopologySchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/tetMeshSchema.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>

#include <atomic>

// Uncomment to write deformed positions as bgeo.sc for parity debugging.
// Reads the dump directory from $HOUDINI_HAIRDEFORM_DUMP at runtime;
// falls back to /tmp/hairdeform_dump if the env var is not set.
// #define HOUDINI_HAIRDEFORM_DUMP

#define USDHD_HAIRDEFORM_FINISH_KERNELS

PXR_NAMESPACE_OPEN_SCOPE

// Serializes the device phase of the groom deform.
//
// Hydra pulls prims in parallel, but CE_Context is a singleton with one
// in-order queue, so concurrent deforms never overlap on the device anyway.
// The concurrency only multiplies live GPU caches.
//
// Taken only after the skin mesh accessor is released and while this groom's
// own subd eval accessor is held. That one is keyed by prim path, so no two
// grooms contend for it, and nothing else waits on a cache accessor under it.
static UT_Lock  theGpuDeformLock;

using DeformMethod = HD_HairDeformPointsDataSource::DeformMethod;
using PointDeformCEArrays = HD_HairDeformPointsDataSource::PointDeformCEArrays;

namespace
{

TF_DEFINE_PRIVATE_TOKENS(
        _tokens,
        ((neighbours, "neighbours"))
        ((neighbourLengths, "neighbours:lengths"))
        ((rest, "rest"))
        ((captPoints, "pCaptPts"))
        ((captPointLengths, "pCaptPts:lengths"))
        ((captWeights, "pCaptWeights"))
        ((captWeightLengths, "pCaptWeights:lengths"))
        ((barbl, "P_barbl"))
        ((barbr, "P_barbr"))
        ((barborient, "barborient"))
        ((restorient, "restorient"))
        ((orient, "orient"))
        ((guides, "guides"))
        ((guidesLengths, "guides:lengths"))
        ((guideWeights, "weights"))
        ((guideWeightsLengths,"weights:lengths"))
        ((clumppts, "clumppts"))
        ((clumpptsLengths, "clumppts:lengths"))
        ((clumpdists, "clumpdists"))
        ((clumpdistsLengths, "clumpdists:lengths"))
);

static cl::NDRange
elemRange(const cl::Device &d, const cl::Kernel &k, size_t nelem)
{
    size_t grpmult
            = k.getWorkGroupInfo<CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE>(
                    d);
    return cl::NDRange(SYSroundUpToMultipleOf(nelem, grpmult));
}

static cl::KernelFunctor
bind(CE_Context *context, int nelem, cl::Kernel &k)
{
    return k.bind(
            context->getQueue(), elemRange(context->getDevice(), k, nelem),
            cl::NullRange);
}

static void
hdTransformPositions(
        VtVec3fArray &positions,
        const HdContainerDataSourceHandle &xformds,
        float shutterOffset)
{
    if (!xformds) return;
    HdXformSchema xs = HdXformSchema::GetFromParent(xformds);
    if (!xs) return;
    auto matDs = xs.GetMatrix();
    if (!matDs) return;
    GfMatrix4d xform = matDs->GetTypedValue(shutterOffset);
    if (xform == GfMatrix4d(1.0)) return;
    for (auto &p : positions)
        p = GfVec3f(xform.Transform(GfVec3d(p)));
}

SYS_MAYBE_UNUSED
auto &&dumparray = [](const UT_StringHolder &name, auto *data, int count)
{
    UTformat("{}[{}]: ", name, count);
    for (int i = 0; i < count; ++i)
    {
        UTformat("{} ", data[i]);
    }
    UTformat("\n");
};

// Dump CE array by copying to host first
template <typename T>
void dumpCEArray(const UT_StringHolder &name, const CE_Array<T> &cearray, CE_Context &context)
{
    if (cearray.size() == 0)
    {
        UTformat("{}[0]: (empty)\n", name);
        return;
    }
    UT_Array<T> hostarray;
    hostarray.setSize(cearray.size());
    context.readBuffer(cearray.buffer(), cearray.size() * sizeof(T), hostarray.data());
    dumparray(name, hostarray.data(), hostarray.size());
}

auto &&indexFromLength = [](auto &indexarray, int count, const auto &lenarray)
{
    utZoneScopedN("build_index_from_lengths");
    indexarray.setSizeNoInit(count + 1);
    int nindex = 0;
    for (int i = 0; i < count; ++i)
    {
        indexarray[i] = nindex;
        nindex += lenarray[i];
    }
    indexarray[count] = nindex;
};

static const int theNTetEdges = 6;

static void
tetEdgeIndices(int edgeno, int &e0, int &e1)
{
    static const int edgeidx[theNTetEdges][2] = {
            {0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3},
    };

    e0 = edgeidx[edgeno][0];
    e1 = edgeidx[edgeno][1];
}

void
hdAddPointEdge(
        UT_Array<UT_Array<int>> &ringzero,
        int pt1,
        int pt2,
        bool doublevalence)
{
    if (pt1 == pt2)
        return; // No-op.

    UT_Array<int> &ref1 = ringzero(pt1);
    if (ref1.find(pt2) == -1)
        ref1.append(pt2);
    UT_Array<int> &ref2 = ringzero(pt2);
    if (ref2.find(pt1) == -1)
        ref2.append(pt1);
}

template <typename T>
void
initFromArrayOrUseHost(
        CE_Context &context,
        CE_Array<T> &ce_array,
        const T *data,
        int count)
{
    utZoneScopedN("init_array\n");
    using CEType = std::decay_t<decltype(ce_array)>;
    using UTType = std::decay_t<decltype(*data)>;

    static_assert(
            std::is_same_v<T, UTType>,
            "*data element type must match ce_array element type");

#ifdef HD_COSSERAT_USE_HOST_PTR
    if (context->isCPU())
    {
        cl::Buffer buf = cl::Buffer(
                context->getCLContext(),
                CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, sizeof(T) * count,
                SYSconst_cast(data));

        ce_array = CE_Array<T>(std::move(buf));
    }
    else
#endif
    {
        ce_array.initFromData((const T *)data, count);
    }
#ifdef USDHD_HAIRDEFORM_FINISH_KERNELS
    context.getQueue().finish();
#endif
};

VtArray<int>
hdGetVertexCounts(const HdContainerDataSourceHandle &primds)
{
    utZoneScoped;
    // get groom topo values
    auto basiscurves = HdBasisCurvesSchema::GetFromParent(primds);
    if (!basiscurves)
    {
        UT_ErrorLog::error("HairDeform: Basis Curves missing on groom\n");
        return VtIntArray();
    }

    auto curvetopo = HdBasisCurvesTopologySchema::GetFromParent(
            basiscurves.GetContainer());
    if (!curvetopo)
    {
        UT_ErrorLog::error("HairDeform: Curve topology missing on groom\n");
        return VtIntArray();
    }

    auto curvevtxcountshandle = curvetopo.GetCurveVertexCounts();
    if (!curvevtxcountshandle)
    {
        UT_ErrorLog::error("HairDeform: Curve vertex counts missing on groom\n");
        return VtIntArray();
    }

    return curvetopo.GetCurveVertexCounts().get()->GetTypedValue(0.0f);
}

bool
hdFindNeighbours(
        HdContainerDataSourceHandle &primds,
        int ndeformerpts,
        UT_Array<int> &neighbours,
        UT_Array<int> &neighboursindex)
{
    utZoneScoped;
    HdMeshSchema mesh = HdMeshSchema::GetFromParent(primds);
    HdTetMeshSchema tetmesh = HdTetMeshSchema::GetFromParent(primds);
    HdBasisCurvesSchema curves = HdBasisCurvesSchema::GetFromParent(primds);

    if (!mesh && !tetmesh)
    {
        if (curves)
        {
            UT_ErrorLog::error("HairDeform: Deformer of type BasisCurves is only supported with "
                               "\"Use Orient Attribute\" enabled");
        }
        else
        {
            UT_ErrorLog::error("HairDeform: Specified deformer is of an unsupported type. "
                               "Must be a Mesh or TetMesh");
        }
        return false;
    }

    UT_Array<UT_Array<int>> ringzero;
    ringzero.setSize(ndeformerpts);
    if (mesh)
    {
        // build neighbour arrays
        VtIntArray vt_facevertexcounts
                = mesh.GetTopology().GetFaceVertexCounts()->GetTypedValue(0.0f);
        VtIntArray vt_facevertexindices
                = mesh.GetTopology().GetFaceVertexIndices()->GetTypedValue(
                        0.0f);

        UT_Array<int> facevertexstarts;
        facevertexstarts.setSizeNoInit(vt_facevertexcounts.size() + 1);
        facevertexstarts[0] = 0;
        std::inclusive_scan(
                vt_facevertexcounts.begin(), vt_facevertexcounts.end(),
                facevertexstarts.begin() + 1);

        for (int face = 0; face < vt_facevertexcounts.size(); ++face)
        {
            int nvtx = vt_facevertexcounts[face];
            bool is_closed = true;
            int li = is_closed ? (nvtx - 1) : 0;
            int i = is_closed ? 0 : 1;

            int lptoff = vt_facevertexindices[facevertexstarts[face] + li];
            for (; i < nvtx; i++)
            {
                int ptoff = vt_facevertexindices[facevertexstarts[face] + i];
                hdAddPointEdge(ringzero, lptoff, ptoff, !is_closed);
                lptoff = ptoff;
            }
        }
    }
    else if (tetmesh)
    {
        VtArray<GfVec4i> vt_facevertexindices
                = tetmesh.GetTopology().GetTetVertexIndices()->GetTypedValue(
                        0.0f);

        for (int face = 0; face < vt_facevertexindices.size(); ++face)
        {
            for (int e = 0, n = theNTetEdges; e < n; e++)
            {
                int i, j;
                tetEdgeIndices(e, i, j);
                hdAddPointEdge(
                        ringzero, vt_facevertexindices[face][i],
                        vt_facevertexindices[face][j], true);
            }
        }
    }

    int count = 0;
    neighboursindex.append(0);
    for (UT_Array<int> &ptneighbours : ringzero)
    {
        neighbours.bumpCapacity(neighbours.size() + ptneighbours.size());
        neighbours.concat(ptneighbours);

        neighboursindex.bumpCapacity(neighbours.size() + 1);
        neighboursindex.append(count += ptneighbours.size());
    }

#if 0 // Debugging - compare to neighbour arrays computed in SOPs
    auto vt_neighbours =
        getConstPvVal<int>(skinpvs, _tokens->neighbours, 0.0f);
    auto vt_neighbourlengths =
        getConstPvVal<int>(skinpvs, _tokens->neighbourLengths, 0.0f);

    int offset = 0;
    bool good = true;
    for (int i=0; i<vt_defrestpos.size(); ++i)
    {
        UT_Array<int> a;
        UT_Array<int> b;
        UTformat("{}: ", i);
        for (int j=acc->second.myNeighbourindex[i]; j<acc->second.myNeighbourindex[i+1]; ++j)
        {
            UTformat("{} ", acc->second.myNeighbours[j]);
            a.append(acc->second.myNeighbours[j]);
        }
        UTformat("| ");
        for (int j=0; j<(*vt_neighbourlengths)[i]; ++j)
        {
            UTformat("{} ", (*vt_neighbours)[offset]);
            b.append((*vt_neighbours)[offset]);
            ++offset;
        }

        if (a != b)
            good = false;
        UTformat("\n");
    }

    UTformat("good: {}\n", good);
#endif

    return true;
}

SYS_MAYBE_UNUSED
static cl::Kernel
getKernel(
    CE_Context &context,
    const UT_StringHolder &program,
    const UT_StringHolder &kernelname,
    const UT_StringHolder &opts,
    bool recompile)
{
    auto prog = context.loadProgram(program, opts, recompile);
    return context.loadKernel(prog, kernelname);
}

SYS_MAYBE_UNUSED
static cl::KernelFunctor
getKernelFunctor(
    CE_Context &context,
    const UT_StringHolder &program,
    const UT_StringHolder &kernelname,
    const UT_StringHolder &opts,
    int count,
    bool recompile)
{
    cl::Kernel kernel = getKernel(
            context, program, kernelname, opts, recompile);
    return bind(&context, count, kernel);
}

template <typename... Args>
void
enqueueKernel(CE_Context &context, int n, cl::Kernel &kernel, Args &&...args)
{
    cl_uint idx = 0;
    ((kernel.setArg(idx++, std::forward<Args>(args))), ...);
    auto kernelfunctor = bind(&context, n, kernel);
    kernelfunctor();
}


// BasisCurves variant: points are contiguous per curve so primpts is
// identity -- no need for the primpts indirection array.
static void
hdBuildCurveRootMaskCurves(
        CE_Context &context,
        CE_FloatArray &ce_rootmask,
        const cl::Buffer &ce_curveprimptsindex,
        int npts,
        int nprims,
        bool recompile)
{
    ce_rootmask.init(npts);
    auto kernel = getKernel(
            context, "deform/guidedeform.cl", "buildCurveRootMaskCurves",
            "", recompile);

    enqueueKernel(context, nprims, kernel,
            nprims,
            ce_rootmask.buffer(),
            ce_curveprimptsindex,
            npts);
    context.getQueue().finish();
}


void
hdPointDeformInitCEArrays(
        PointDeformCEArrays &cearrays,
        CE_Context &context,
        const UT_Array<int> &neighboursindex,
        const UT_Array<int> &neighbours,
        const VtArray<GfVec3f> &vt_defrestpos,
        const VtArray<GfVec3f> &vt_defanimpos,
        const UT_Span<const int> &captpts,
        const UT_Span<const float> &captweights,
        const UT_Span<const int> &captindex,
        bool useorientattrib,
        const VtArray<GfVec4f> *vt_deformerrestorient,
        const VtArray<GfVec4f> *vt_deformeranimorient,
        const VtArray<GfQuatf> *vt_deformerrestorient_quat,
        const VtArray<GfQuatf> *vt_deformeranimorient_quat)
{
    utZoneScoped;
    static const int theVectorSize = 3;

    // deformer arrays
    initFromArrayOrUseHost(
            context, cearrays.ce_deformeranimpos,
            (float *)vt_defanimpos.cdata(),
            theVectorSize * vt_defanimpos.size());
    initFromArrayOrUseHost(
            context, cearrays.ce_deformerrestpos,
            (float *)vt_defrestpos.cdata(),
            theVectorSize * vt_defrestpos.size());

    initFromArrayOrUseHost(
            context, cearrays.ce_neighbourindex, neighboursindex.data(),
            neighboursindex.size());
    initFromArrayOrUseHost(
            context, cearrays.ce_neighbour, neighbours.data(),
            neighbours.size());

    // capture arrays (pts and weights share the same index)
    initFromArrayOrUseHost(
            context, cearrays.ce_ptsindex, captindex.data(), captindex.size());
    initFromArrayOrUseHost<int>(
            context, cearrays.ce_pts, captpts.data(), captpts.size());

    initFromArrayOrUseHost(
            context, cearrays.ce_weightsindex, captindex.data(), captindex.size());
    initFromArrayOrUseHost(
            context, cearrays.ce_weights, captweights.data(), captweights.size());

    // GfQuatf and GfVec4f both store (x,y,z,w) in memory, matching
    // the OCL quaternion layout.  Raw float casts are safe for either.
    if (useorientattrib)
    {
        UT_Span<const float> restorient_data;
        UT_Span<const float> animorient_data;

        if (vt_deformerrestorient)
        {
            restorient_data = UT_Span<const float>(
                    (const float *)vt_deformerrestorient->cdata(),
                    vt_deformerrestorient->size() * 4);
        }
        else if (vt_deformerrestorient_quat)
        {
            restorient_data = UT_Span<const float>(
                    (const float *)vt_deformerrestorient_quat->cdata(),
                    vt_deformerrestorient_quat->size() * 4);
        }

        if (vt_deformeranimorient)
        {
            animorient_data = UT_Span<const float>(
                    (const float *)vt_deformeranimorient->cdata(),
                    vt_deformeranimorient->size() * 4);
        }
        else if (vt_deformeranimorient_quat)
        {
            animorient_data = UT_Span<const float>(
                    (const float *)vt_deformeranimorient_quat->cdata(),
                    vt_deformeranimorient_quat->size() * 4);
        }

        initFromArrayOrUseHost(context, cearrays.ce_deformerrestorient,
                restorient_data.data(), static_cast<int>(restorient_data.size()));
        initFromArrayOrUseHost(context, cearrays.ce_deformeranimorient,
                animorient_data.data(), static_cast<int>(animorient_data.size()));
    }
}

void
hdPointDeformSetExternals(
        PointDeformCEArrays &cearrays,
        GU_PointDeform::CachedItems &cached_items,
        bool useorientattrib)
{
    auto &buffers = cached_items.getBuffers();

    buffers.myDeformerRestPos.setExternal(cearrays.ce_deformerrestpos.buffer());
    buffers.myDeformerAnimPos.setExternal(cearrays.ce_deformeranimpos.buffer());

    buffers.myPtsIndex.setExternal(cearrays.ce_ptsindex.buffer());
    buffers.myPts.setExternal(cearrays.ce_pts.buffer());
    buffers.myWeightsIndex.setExternal(cearrays.ce_weightsindex.buffer());
    buffers.myWeights.setExternal(cearrays.ce_weights.buffer());

    if (useorientattrib)
    {
        buffers.myDeformerRestOrient.setExternal(cearrays.ce_deformerrestorient.buffer());
        buffers.myDeformerAnimOrient.setExternal(cearrays.ce_deformeranimorient.buffer());
    }
    else
    {
        buffers.myNeighboursIndex.setExternal(cearrays.ce_neighbourindex.buffer());
        buffers.myNeighbours.setExternal(cearrays.ce_neighbour.buffer());
    }
}

bool
hdComputeTransforms(
        GU_PointDeform::CachedItems &cached_items,
        CE_Context &context,
        int npts,
        int ndeformerpts,
        bool useorientattrib,
        bool recompile)
{
    utZoneScoped;
    if (!GUpointDeformInit(
                cached_items, context, npts, ndeformerpts, recompile))
        return false;

    if (useorientattrib)
    {
        if (!GUpointDeformOrientXform(cached_items, context, ndeformerpts, recompile))
            return false;
    }
    else
    {
        if (!GUpointDeformSeqXform(cached_items, context, ndeformerpts, recompile))
            return false;
    }

    GUpointDeformComputeTransform(cached_items, context, npts, recompile);

    return true;
}

static bool
hdDetailFromMesh(
        GU_Detail &gdp,
        const HdMeshSchema &mesh,
        const VtVec3fArray &points)
{
    utZoneScoped;

    if (!mesh.GetTopology())
    {
        UT_ErrorLog::error("HairDeform: Mesh has no topology");
        return false;
    }

    auto faceVertexCountsDs = mesh.GetTopology().GetFaceVertexCounts();
    auto faceVertexIndicesDs = mesh.GetTopology().GetFaceVertexIndices();

    if (!faceVertexCountsDs || !faceVertexIndicesDs)
    {
        UT_ErrorLog::error("HairDeform: Mesh has no face vertex data");
        return false;
    }

    const VtIntArray vt_facevertexcounts = faceVertexCountsDs->GetTypedValue(0.0f);
    const VtIntArray vt_facevertexindices = faceVertexIndicesDs->GetTypedValue(0.0f);

    if (vt_facevertexcounts.empty() || vt_facevertexindices.empty())
    {
        UT_ErrorLog::error("HairDeform: Mesh has empty face vertex data");
        return false;
    }

    GA_RWHandleV3 posh(gdp.getP());
    gdp.appendPointBlock(points.size());
    gdp.forEachPoint([&](GA_Offset ptoff)
    {
        GA_Index ptindex = gdp.pointIndex(ptoff);

        const GfVec3f &gfpos = points[ptindex];
        UT_Vector3 pos(gfpos.GetArray());

        posh.set(ptoff, pos);
    });

    GA_PolyCounts counts;

    SYS_MAYBE_UNUSED
    GA_Size total = 0;

    for (exint face = 0; face < vt_facevertexcounts.size(); ++face)
    {
        exint count = vt_facevertexcounts[face];
        counts.append(count);

        total += count;
    }

    GEO_PrimPoly::buildBlock(&gdp, GA_Offset(0), gdp.getNumPoints(), counts, vt_facevertexindices.cdata());

    return true;
}

static bool
hdDetailFromTetMesh(
        GU_Detail &gdp,
        const HdTetMeshSchema &tetmesh,
        const VtVec3fArray &points)
{
    utZoneScoped;

    if (!tetmesh.GetTopology())
    {
        UT_ErrorLog::error("HairDeform: TetMesh has no topology");
        return false;
    }

    auto tetVertexIndicesDs = tetmesh.GetTopology().GetTetVertexIndices();
    if (!tetVertexIndicesDs)
    {
        UT_ErrorLog::error("HairDeform: TetMesh has no vertex indices");
        return false;
    }

    const VtArray<GfVec4i> vt_tetvertexindices
            = tetVertexIndicesDs->GetTypedValue(0.0f);

    if (vt_tetvertexindices.empty())
    {
        UT_ErrorLog::error("HairDeform: TetMesh has empty vertex indices");
        return false;
    }

    GA_RWHandleV3 posh(gdp.getP());
    gdp.appendPointBlock(points.size());
    gdp.forEachPoint([&](GA_Offset ptoff)
    {
        GA_Index ptindex = gdp.pointIndex(ptoff);
        const GfVec3f &gfpos = points[ptindex];
        UT_Vector3 pos(gfpos.GetArray());
        posh.set(ptoff, pos);
    });

    // Build flat array of tet point indices for buildBlock
    GA_Size ntets = vt_tetvertexindices.size();
    UT_Array<int> tetpointnumbers;
    tetpointnumbers.setCapacity(ntets * 4);
    for (exint tet = 0; tet < ntets; ++tet)
    {
        const GfVec4i &tetidx = vt_tetvertexindices[tet];
        tetpointnumbers.append(tetidx[0]);
        tetpointnumbers.append(tetidx[1]);
        tetpointnumbers.append(tetidx[2]);
        tetpointnumbers.append(tetidx[3]);
    }

    GEO_PrimTetrahedron::buildBlock(&gdp, GA_Offset(0), gdp.getNumPoints(),
            ntets, tetpointnumbers.data());

    return true;
}

static GU_PointCaptureKernelType
hdKernelTypeFromString(const std::string &kerneltype)
{
    if (kerneltype == "truncatedgaussian")
        return GU_PointCaptureKernelType::TRUNCATED_GAUSSIAN;
    else if (kerneltype == "quadratic")
        return GU_PointCaptureKernelType::QUADRATIC;
    else if (kerneltype == "linear")
        return GU_PointCaptureKernelType::LINEAR;
    else // "exponentialbump" or default
        return GU_PointCaptureKernelType::EXPONENTIAL_BUMP;
}

static GU_PointCaptureSmoothingMethod
hdSmoothingMethodFromString(const std::string &smoothingmethod)
{
    if (smoothingmethod == "interpolating")
        return GU_PointCaptureSmoothingMethod::INTERPOLATING;
    else if (smoothingmethod == "none")
        return GU_PointCaptureSmoothingMethod::NONE;
    else // "approximating" or default
        return GU_PointCaptureSmoothingMethod::APPROXIMATING;
}

static GU_PointCaptureTetMeshTreatment
hdTetMeshTreatmentFromString(const std::string &tetmeshtreatment)
{
    if (tetmeshtreatment == "as_solid")
        return GU_PointCaptureTetMeshTreatment::AS_SOLID;
    else if (tetmeshtreatment == "none")
        return GU_PointCaptureTetMeshTreatment::NONE;
    else // "as_surface" or default
        return GU_PointCaptureTetMeshTreatment::AS_SURFACE;
}

static bool
hdDetailFromGeometry(GU_Detail &gdp, const HdMeshSchema &schema, const VtVec3fArray &points)
{
    return hdDetailFromMesh(gdp, schema, points);
}

static bool
hdDetailFromGeometry(GU_Detail &gdp, const HdTetMeshSchema &schema, const VtVec3fArray &points)
{
    return hdDetailFromTetMesh(gdp, schema, points);
}

template <typename HdSchemaT>
static bool
hdComputeSmoothCapture(
        UT_IntArray &captStarts,
        UT_IntArray &captPts,
        UT_FloatArray &captWeights,
        const VtVec3fArray &groomrestpos,
        const HdSchemaT &defschema,
        const VtVec3fArray &deformerrestpos,
        HairDeformSchema &hairdeformschema)
{
    utZoneScopedN("hdComputeSmoothCapture");

    float captureradius = 1.0f;
    std::string kerneltype;
    std::string smoothingmethod;
    int smoothinglevel = 0;
    std::string tetmeshtreatment;

    if (auto scr = hairdeformschema.GetSmoothCaptureRadius())
        captureradius = scr->GetTypedValue(0.0f);
    if (auto kt = hairdeformschema.GetKernelType())
        kerneltype = kt->GetTypedValue(0.0f);
    if (auto sm = hairdeformschema.GetSmoothingMethod())
        smoothingmethod = sm->GetTypedValue(0.0f);
    if (auto sl = hairdeformschema.GetSmoothingLevel())
        smoothinglevel = sl->GetTypedValue(0.0f);
    if (auto tmt = hairdeformschema.GetTetMeshTreatment())
        tetmeshtreatment = tmt->GetTypedValue(0.0f);

    // Create GU_Detail for groom points (just positions, no topology)
    GU_Detail groomgdp;
    groomgdp.appendPointBlock(groomrestpos.size());
    GA_RWHandleV3 groomposh(groomgdp.getP());
    groomgdp.forEachPoint([&](GA_Offset ptoff)
    {
        GA_Index ptindex = groomgdp.pointIndex(ptoff);
        const GfVec3f &gfpos = groomrestpos[ptindex];
        groomposh.set(ptoff, UT_Vector3(gfpos.GetArray()));
    });

    // Create GU_Detail for capture geometry (mesh or tetmesh via overload)
    GU_Detail capturegdp;
    if (!hdDetailFromGeometry(capturegdp, defschema, deformerrestpos))
        return false;

    // Configure GU_PointCapture
    GU_PointCaptureConfiguration config;
    config.myCaptureRadius = captureradius;
    config.myKernelType = hdKernelTypeFromString(kerneltype);
    config.mySmoothingMethod = hdSmoothingMethodFromString(smoothingmethod);
    config.mySmoothingLevel = smoothinglevel;
    config.myTetMeshTreatment = hdTetMeshTreatmentFromString(tetmeshtreatment);

    // Run capture
    GU_PointCapture capture;
    GU_PointCaptureResult result = capture(
            groomgdp,
            config,
            nullptr, // no point group
            capturegdp,
            "pCaptPts",
            "pCaptWeights");

    if (result.myStatus != GU_PointCaptureStatus::PC_SUCCEEDED)
    {
        UT_ErrorLog::warning("HairDeform: GU_PointCapture failed: {}", result.myMessage);
        return false;
    }

    // Extract capture data from array attributes
    const GA_Attribute *ptsAttrib = groomgdp.findIntArray(GA_ATTRIB_POINT, "pCaptPts");
    const GA_Attribute *wtsAttrib = groomgdp.findFloatArray(GA_ATTRIB_POINT, "pCaptWeights");

    if (!ptsAttrib || !wtsAttrib)
    {
        UT_ErrorLog::warning("HairDeform: GU_PointCapture did not create capture attributes");
        return false;
    }

    const GA_AIFNumericArray *ptsAIF = ptsAttrib->getAIFNumericArray();
    const GA_AIFNumericArray *wtsAIF = wtsAttrib->getAIFNumericArray();

    if (!ptsAIF || !wtsAIF)
    {
        UT_ErrorLog::warning("HairDeform: Cannot access numeric array interface for capture attributes");
        return false;
    }

    // Build output arrays
    exint npts = groomgdp.getNumPoints();
    captStarts.setSize(npts + 1);
    captPts.clear();
    captWeights.clear();

    UT_Array<exint> localIndices;
    UT_FloatArray localWeights;

    exint totalEntries = 0;
    for (GA_Offset ptoff : groomgdp.getPointRange())
    {
        GA_Index ptidx = groomgdp.pointIndex(ptoff);
        captStarts[ptidx] = totalEntries;

        ptsAIF->get(ptsAttrib, ptoff, localIndices);
        wtsAIF->get(wtsAttrib, ptoff, localWeights);

        exint count = SYSmin(localIndices.size(), localWeights.size());
        for (exint i = 0; i < count; ++i)
        {
            captPts.append(localIndices[i]);
            captWeights.append(localWeights[i]);
        }
        totalEntries += count;
    }
    captStarts[npts] = totalEntries;

    return true;
}

template <typename TIN, typename TOUT>
void
hdCopyHostDataToDeviceArray(
        CE_Array<TOUT> &outarray,
        const TIN *indata,
        CE_Context &context)
{
    size_t totalsize = outarray.size() * sizeof(float);
    context.writeBuffer(outarray.buffer(), totalsize, indata);
}

template <typename TIN, typename TOUT>
void
hdCopyDeviceArrayToHostData(
        TOUT *outdata,
        const CE_Array<TIN> &inarray,
        CE_Context &context)
{
    size_t totalsize = inarray.size() * sizeof(float);
    context.readBuffer(inarray.buffer(), totalsize, outdata);
}

void
hdGAPosAttribFromVtArray(
        GA_Attribute &pos,
        const VtVec3fArray &positions)
{
    utZoneScoped;

    GA_RWHandleV3 animposattrib(&pos);

    GA_Detail &defgdp = pos.getDetail();

    UTparallelFor(GA_SplittableRange(defgdp.getPointRange()),
    [&](const GA_SplittableRange &r)
    {
        GA_Offset startptoff, endptoff;
        for (GA_Iterator ptit(r);
             ptit.blockAdvance(startptoff, endptoff); )
        {
            for (GA_Offset ptoff = startptoff;
                 ptoff < endptoff;
                 ++ptoff)
            {
                GA_Size index = defgdp.pointIndex(ptoff);
                UT_Vector3 pos(positions[index].data());
                animposattrib.set(ptoff, pos);
            }
        }
    });
}

static void
hdUploadSkinAttrsToCE(
        CE_FloatArray &ce_skinrestpos,
        CE_FloatArray &ce_skinanimpos,
        CE_FloatArray &ce_skinrestnml,
        CE_FloatArray &ce_skinresttan,
        CE_FloatArray &ce_skinanimnml,
        CE_FloatArray &ce_skinanimtan,
        const GU_Detail &skinrestgdp,
        const VtVec3fArray &vt_skinrest,
        const VtVec3fArray &vt_skinanim,
        const GA_Attribute *skinrestnml,
        const GA_Attribute *skinresttan,
        const GA_Attribute *skinanimnml,
        const GA_Attribute *skinanimtan,
        int vectorsize)
{
    utZoneScoped;

    const int nskinpts = skinrestgdp.getNumPoints();

    auto uploadV3Attrib = [&](CE_FloatArray &cearray, const GA_Attribute *attrib)
    {
        UT_FloatArray data;
        data.setSize(vectorsize * nskinpts);
        const GA_ROHandleV3 h(attrib);
        for (GA_Offset ptoff : skinrestgdp.getPointRange())
        {
            int idx = skinrestgdp.pointIndex(ptoff);
            UT_Vector3F v = h.get(ptoff);
            data[idx * vectorsize + 0] = v.x();
            data[idx * vectorsize + 1] = v.y();
            data[idx * vectorsize + 2] = v.z();
        }
        cearray.initFromArray(data);
    };

    ce_skinrestpos.initFromData(
            (const float*)vt_skinrest.cdata(),
            vectorsize * nskinpts);

    ce_skinanimpos.initFromData(
            (const float*)vt_skinanim.cdata(),
            vectorsize * nskinpts);

    uploadV3Attrib(ce_skinrestnml, skinrestnml);
    uploadV3Attrib(ce_skinresttan, skinresttan);
    uploadV3Attrib(ce_skinanimnml, skinanimnml);
    uploadV3Attrib(ce_skinanimtan, skinanimtan);
}

template<typename Container>
struct ArrayAccessorVtArrayPolicy
{
    Container& myContainer;

    ArrayAccessorVtArrayPolicy(Container& container)
        : myContainer(container) {}

    inline UT_Vector3 get(size_t i) const noexcept
    {
        return UT_Vector3(myContainer[i].data());
    }

    inline void set(size_t i, const UT_Vector3 &value) noexcept
    {
        myContainer[i] = GfVec3f(value.data());
    }

    inline size_t size() const noexcept
    {
        return myContainer.size();
    }
};

static bool
hdGetGuideWeightsFromPrimvars(
        const HdPrimvarsSchema &groompvs,
        const VtIntArray &curvevtxcounts,
        UT_IntArray &guideStarts,
        VtArray<int> &guideIndices,
        VtArray<float> &guideWeights)
{
    utZoneScoped;

    using namespace HD_HairDeformUtils;

    auto vt_guides = getConstPvVal<int>(groompvs, _tokens->guides, 0.0f);
    auto vt_guides64 = getConstPvVal<int64>(groompvs, _tokens->guides, 0.0f);
    auto vt_guideslengths = getConstPvVal<int>(
            groompvs, _tokens->guidesLengths, 0.0f);
    auto vt_weights = getConstPvVal<float>(
            groompvs, _tokens->guideWeights, 0.0f);
    auto vt_weightslengths = getConstPvVal<int>(
            groompvs, _tokens->guideWeightsLengths, 0.0f);

    int missing = 0;
    if (!vt_guides.has_value() && !vt_guides64.has_value())
    {
        UT_ErrorLog::error(
                "HairDeform: Guide weights missing 'guides' primvar on groom");
        ++missing;
    }
    if (!vt_guideslengths.has_value())
    {
        UT_ErrorLog::error(
                "HairDeform: Guide weights missing 'guides:lengths' primvar on groom");
        ++missing;
    }
    if (!vt_weights.has_value())
    {
        UT_ErrorLog::error(
                "HairDeform: Guide weights missing 'weights' primvar on groom");
        ++missing;
    }
    if (!vt_weightslengths.has_value())
    {
        UT_ErrorLog::error(
                "HairDeform: Guide weights missing 'weights:lengths' primvar on groom");
        ++missing;
    }
    if (missing > 0)
        return false;

    const VtArray<int> *guides = nullptr;
    VtArray<int> guides32;
    if (vt_guides.has_value())
    {
        guides = &vt_guides.value();
    }
    else if (vt_guides64.has_value())
    {
        const auto &src = vt_guides64.value();
        guides32.resize(src.size());
        std::transform(
                src.begin(), src.end(), guides32.begin(),
                [](int64 v) { return static_cast<int>(v); });
        guides = &guides32;
    }

    int nprims = curvevtxcounts.size();
    if (vt_guideslengths->size() != nprims
        || vt_weightslengths->size() != nprims)
    {
        UT_ErrorLog::error(
                "HairDeform: Guide weights primvar lengths size mismatch: "
                "guides:lengths={}, weights:lengths={}, expected={}",
                vt_guideslengths->size(), vt_weightslengths->size(), nprims);
        return false;
    }

    for (int i = 0; i < nprims; ++i)
    {
        if ((*vt_guideslengths)[i] != (*vt_weightslengths)[i])
        {
            UT_ErrorLog::error(
                    "HairDeform: Guide weights primvar length mismatch at "
                    "primitive {}: guides:lengths={}, weights:lengths={}",
                    i, (*vt_guideslengths)[i], (*vt_weightslengths)[i]);
            return false;
        }
    }

    UT_IntArray guideweightsindex;
    indexFromLength(guideStarts, nprims, vt_guideslengths->cdata());
    indexFromLength(guideweightsindex, nprims, vt_weightslengths->cdata());

    if (guideStarts[guideStarts.size() - 1] != guides->size()
        || guideweightsindex[guideweightsindex.size() - 1]
                   != vt_weights->size())
    {
        UT_ErrorLog::error(
                "HairDeform: Guide weights primvar flattened size mismatch: "
                "guides={}, weights={}",
                guides->size(), vt_weights->size());
        return false;
    }

    if (guideStarts.size() != guideweightsindex.size())
    {
        UT_ErrorLog::error(
                "HairDeform: Guide weights index size mismatch: "
                "guidesindex={}, weightsindex={}",
                guideStarts.size(), guideweightsindex.size());
        return false;
    }

    for (exint i = 0; i < guideweightsindex.size(); ++i)
    {
        if (guideStarts[i] != guideweightsindex[i])
        {
            UT_ErrorLog::error(
                    "HairDeform: Guide weights index mismatch at {}: "
                    "guidesindex={}, weightsindex={}",
                    i, guideStarts[i], guideweightsindex[i]);
            return false;
        }
    }

    guideIndices = *guides;
    guideWeights = vt_weights.value();
    return true;
}

struct RootPointAccessorPolicy
{
    RootPointAccessorPolicy(
            const VtVec3fArray &points,
            const UT_IntArray &starts)
        : myPoints(points)
        , myStarts(starts)
        , myCount(starts.size() ? starts.size() - 1 : 0)
    {}

    inline UT_Vector3 get(size_t i) const noexcept
    {
        const auto &p = myPoints[myStarts[i]];
        return UT_Vector3(p[0], p[1], p[2]);
    }

    inline void set(size_t, const UT_Vector3 &) noexcept {}

    inline size_t size() const noexcept { return myCount; }

    const VtVec3fArray &myPoints;
    const UT_IntArray &myStarts;
    exint myCount = 0;
};

static void
hdComputeEdgeLengthsForCurves(
        CE_Context &context,
        const CE_FloatArray &ce_positions,
        const CE_Int32Array &ce_primptsindex,
        CE_FloatArray &ce_edgelengths,
        int nprims,
        bool recompile)
{
    utZoneScoped;

    auto compute_edge_lengths = getKernelFunctor(
        context, "deform/guidedeform.cl", "computeEdgeLengthsKernel",
        "", nprims, recompile);

    // BasisCurves points are already in curve order, so the kernel runs
    // direct (indirect = 0) and primpoints is unread -- bind any live buffer.
    compute_edge_lengths(
            (int)nprims,
            ce_positions.buffer(),
            ce_edgelengths.buffer(),
            ce_primptsindex.buffer(),
            ce_primptsindex.buffer(),
            0 /*indirect*/);
    context.getQueue().finish();
}

// An id attribute is read the same way off the groom and off the skin, so the
// two agree on what a given value means: uniform interpolation, one value per
// curve on the groom and one per polygon on the skin, indexed by element
// number.  No other interpolation is accepted, on either side.
struct HD_IdPrimVar
{
    VtArray<int> myInts;
    VtArray<int64_t> myInt64s;

    exint size() const { return myInts.size() + myInt64s.size(); }
    exint intAt(exint i) const
    {
        return myInts.size() ? (exint)myInts[i] : (exint)myInt64s[i];
    }
};

// element names what one value describes, for the error messages: "curve" on
// the groom, "polygon" on the skin.
static bool
hdReadIdPrimVar(
        const HdContainerDataSourceHandle &primds,
        const UT_StringRef &name,
        const UT_StringRef &element,
        HD_IdPrimVar &out)
{
    HdPrimvarsSchema pvs = HdPrimvarsSchema::GetFromParent(primds);
    if (!pvs)
        return false;

    HdPrimvarSchema pv = pvs.GetPrimvar(TfToken(name.toStdString()));
    if (!pv.IsDefined())
        return false;

    auto interp = pv.GetInterpolation();
    const TfToken interptoken
            = interp ? interp->GetTypedValue(0.0f) : TfToken();
    if (interptoken != HdPrimvarSchemaTokens->uniform)
    {
        UT_ErrorLog::error(
                "HairDeform: Capture id primvar '{}' has {} interpolation; it "
                "has to be uniform, one value per {}",
                name,
                interptoken.IsEmpty() ? "no" : interptoken.GetText(),
                element);
        return false;
    }

    HdSampledDataSourceHandle ds = pv.GetFlattenedPrimvarValue();
    if (!ds)
        return false;

    VtValue val = ds->GetValue(0.0f);
    if (val.IsHolding<VtArray<int>>())
        out.myInts = val.UncheckedGet<VtArray<int>>();
    else if (val.IsHolding<VtArray<int64_t>>())
        out.myInt64s = val.UncheckedGet<VtArray<int64_t>>();
    else
    {
        UT_ErrorLog::error(
                "HairDeform: Capture id primvar '{}' holds {}; it has to be "
                "an integer array",
                name, val.GetTypeName());
        return false;
    }

    if (out.size() == 0)
    {
        UT_ErrorLog::error(
                "HairDeform: Capture id primvar '{}' is empty", name);
        return false;
    }
    return true;
}

// Bucket the skin polygons by id, one closest-point tree per bucket.  The
// trees reference gdp, so they live and die with the skin mesh cache entry.
static bool
hdIdBucketsFromSkinPrimVar(
        HD_HairDeformIdBuckets &buckets,
        const GU_Detail &gdp,
        const HD_IdPrimVar &ids,
        const UT_StringRef &name)
{
    utZoneScoped;

    UT_Array<GA_OffsetArray> bucketprims;
    const exint nvalues = ids.size();
    const exint nprims = gdp.getNumPrimitives();

    // Uniform means one value per polygon, so a short primvar leaves the tail
    // of the skin in no bucket at all.  Say so here: read as a mismatch later
    // it surfaces as curves whose id "the skin has no polygons for", which
    // sends the user looking at the groom instead of at the skin.
    if (nvalues < nprims)
    {
        UT_ErrorLog::error(
                "HairDeform: Capture id primvar '{}' on the skin holds {} "
                "values for {} polygons; a uniform id needs one value per "
                "polygon",
                name, nvalues, nprims);
        return false;
    }

    {
    utZoneScopedN("assign_prims_to_buckets");
    for (GA_Index primidx = 0; primidx < nprims; ++primidx)
    {
        const GA_Offset primoff = gdp.primitiveOffset(primidx);
        if (gdp.getPrimitiveVertexCount(primoff) < 1)
            continue;

        const exint id = ids.intAt(primidx);
        int bucket;
        auto it = buckets.myIntToBucket.find(id);
        if (it != buckets.myIntToBucket.end())
            bucket = it->second;
        else
        {
            bucket = (int)bucketprims.size();
            buckets.myIntToBucket[id] = bucket;
            bucketprims.append();
        }
        bucketprims[bucket].append(primoff);
    }
    utZoneValue((uint64)bucketprims.size());
    }

    if (bucketprims.isEmpty())
    {
        UT_ErrorLog::error(
                "HairDeform: No skin polygon carries a '{}' value to bucket "
                "it by", name);
        return false;
    }

    // One tree per bucket, built in parallel: with a feather-sized patch each
    // build is far too small to keep its own threads busy.
    {
    utZoneScopedN("build_bucket_trees");
    utZoneValue((uint64)bucketprims.size());
    buckets.myTrees.setSize(bucketprims.size());
    UTparallelFor(
        UT_BlockedRange<exint>(0, bucketprims.size()),
        [&](const UT_BlockedRange<exint> &r)
    {
        utZoneScopedN("task");
        GA_PrimitiveGroupUPtr group = gdp.createDetachedPrimitiveGroup();
        for (exint b = r.begin(), end = r.end(); b < end; ++b)
        {
            for (GA_Offset primoff : bucketprims[b])
                group->addOffset(primoff);

            auto tree = UTmakeUnique<GU_RayIntersect>();
            tree->init(&gdp, group.get());
            buckets.myTrees[b] = std::move(tree);

            // Take back exactly what was set rather than clear(), which walks
            // every page of the detail and would make the loop cost buckets
            // times skin polygons.  init() is done with the group by here.
            for (GA_Offset primoff : bucketprims[b])
                group->removeOffset(primoff);
        }
    });
    }

    return true;
}

static const HD_HairDeformIdBuckets *
hdFindIdBuckets(
        const HD_HairDeformSkinMeshCache &skincache,
        const UT_StringHolder &name)
{
    auto it = skincache.myIdBuckets.find(name);
    return it == skincache.myIdBuckets.end() ? nullptr : &it->second;
}

// One skin bucket per groom curve.  A groom id the skin doesn't carry fails
// the whole capture: leaving those curves behind while the rest deform reads
// as a broken groom, and on a subdivision skin OSD rejects a partial upload
// anyway, so both paths say so instead.
static bool
hdBucketIndicesFromCurveIds(
        UT_Array<int> &curvebucketindices,
        const HD_HairDeformIdBuckets &buckets,
        const HD_IdPrimVar &ids,
        exint ncurves,
        const UT_StringRef &name)
{
    utZoneScoped;

    // A short primvar is a fault of the whole primvar rather than of the
    // curves that run off its end, so it is worth saying so on its own: read
    // as a mismatch it would send the user looking at ids that are fine.
    const exint nvalues = ids.size();
    if (nvalues < ncurves)
    {
        UT_ErrorLog::error(
                "HairDeform: Capture id primvar '{}' on the groom curves "
                "holds {} values for {} curves; a uniform id needs one value "
                "per curve",
                name, nvalues, ncurves);
        return false;
    }

    curvebucketindices.setSizeNoInit(ncurves);

    // A lookup per curve, and the bucket maps are only read.
    std::atomic<exint> nunmatchedtotal{0};
    UTparallelFor(
        UT_BlockedRange<exint>(0, ncurves),
        [&](const UT_BlockedRange<exint> &r)
    {
        exint nunmatchedlocal = 0;
        for (exint c = r.begin(), end = r.end(); c < end; ++c)
        {
            const int bucket = buckets.findBucket(ids.intAt(c));
            curvebucketindices[c] = bucket;
            if (bucket < 0)
                ++nunmatchedlocal;
        }
        nunmatchedtotal += nunmatchedlocal;
    });
    const exint nunmatched = nunmatchedtotal.load();

    if (nunmatched == ncurves && ncurves > 0)
    {
        UT_ErrorLog::error(
                "HairDeform: No groom curve id in '{}' matches any skin "
                "polygon; check that both prims carry the same values",
                name);
        return false;
    }
    if (nunmatched > 0)
    {
        UT_ErrorLog::error(
                "HairDeform: {} of {} groom curves carry a '{}' id the skin "
                "has no polygons for; every curve has to match -- give them an "
                "id the skin carries, or clear Capture ID Attribute to capture "
                "against the whole skin",
                nunmatched, ncurves, name);
        return false;
    }
    return true;
}

// Every point of a curve captures with its curve's bucket, feather barbs
// included: a capture id lives on the curve, so all of its points share it.
// False if a barb has no shaft point to take its bucket from, which would
// leave it unbucketed -- see hdBucketIndicesFromCurveIds for why that fails
// the capture rather than dropping the point.
static bool
hdPointBucketIndicesFromCurves(
        UT_Array<int> &pointbucketindices,
        const UT_Array<int> &curvebucketindices,
        const UT_IntArray &curveprimptsindex,
        const HD_HairDeformBarbLayout &barbs,
        exint nptsexp)
{
    const exint ncurves = curvebucketindices.size();
    const exint nshaftpts = curveprimptsindex[ncurves];

    pointbucketindices.setSizeNoInit(nptsexp);
    for (exint c = 0; c < ncurves; ++c)
    {
        for (exint p = curveprimptsindex[c]; p < curveprimptsindex[c + 1]; ++p)
            pointbucketindices[p] = curvebucketindices[c];
    }
    for (exint p = nshaftpts; p < nptsexp; ++p)
    {
        const exint shaft = barbs.shaftPoint(p);
        if (shaft < 0 || shaft >= nshaftpts)
        {
            UT_ErrorLog::error(
                    "HairDeform: Barb point {} of {} hangs off shaft point {}, "
                    "which is outside the {} shaft points",
                    p, nptsexp, shaft, nshaftpts);
            return false;
        }
        pointbucketindices[p] = pointbucketindices[shaft];
    }
    return true;
}

// Project positions onto the skin, restricted to each position's id bucket
// when matching is on.  A null bucketindices with matching on means the
// resolve failed and is a hard error: falling back to the whole skin would
// bind points across the very patch boundaries the id is there to enforce.
template <typename PA>
static bool
hdSurfaceInterpOffsetsFromIdBuckets(
        UT_IntArray &primptstarts,
        UT_IntArray &ptindices,
        UT_FloatArray &ptweights,
        const PA &positions,
        const GU_Detail &skingdp,
        const GU_RayIntersect &skinrayintersect,
        const HD_HairDeformIdBuckets *buckets,
        const UT_Array<int> *bucketindices)
{
    if (buckets)
    {
        if (!bucketindices)
            return false;

        if (bucketindices->size() != (exint)positions.size())
        {
            UT_ErrorLog::error(
                    "HairDeform: Capture id bucket count ({}) does not match "
                    "the captured position count ({})",
                    bucketindices->size(), positions.size());
            return false;
        }

        GU_SurfaceDeform::surfaceInterpOffsetsFromResolver(
                primptstarts, ptindices, ptweights, positions, skingdp,
                [buckets, bucketindices](
                        exint i, const UT_Vector3 &pos, GU_MinInfo &mininfo)
                {
                    if (const GU_RayIntersect *tree
                            = buckets->tree((*bucketindices)[i]))
                        tree->minimumPoint(pos, mininfo);
                });
        return true;
    }

    GU_SurfaceDeform::surfaceInterpOffsets(
            primptstarts, ptindices, ptweights, positions, skingdp,
            skinrayintersect);
    return true;
}

// Compute curve skin capture into a cache struct (CPU arrays only).
static bool
hdComputeCurveSkinCaptureToCache(
        HD_HairDeformSurfaceTopoCache &cache,
        const GU_Detail &skinrestgdp,
        const GU_RayIntersect &skinrayintersect,
        const VtVec3fArray &points,
        const UT_IntArray &primptsindex,
        const HD_HairDeformIdBuckets *idbuckets,
        const UT_Array<int> *bucketindices)
{
    utZoneScoped;

    using RootAcc = const GU_SurfaceDeform::ArrayAccessor<
            UT_Vector3,
            const VtVec3fArray &,
            RootPointAccessorPolicy>;

    RootPointAccessorPolicy rootpolicy(points, primptsindex);
    RootAcc rootacc(points, rootpolicy);

    if (!hdSurfaceInterpOffsetsFromIdBuckets(
                cache.myPrimPtStarts,
                cache.myPtIndices,
                cache.myPtWeights,
                rootacc,
                skinrestgdp,
                skinrayintersect,
                idbuckets,
                bucketindices))
        return false;

    return true;
}

// Upload cached skin capture CPU arrays to CE.
static void
hdUploadCurveSkinCapture(
        CE_Int32Array &ce_skinptstarts,
        CE_Int32Array &ce_skinptindices,
        CE_FloatArray &ce_skinptweights,
        const HD_HairDeformSurfaceTopoCache &cache)
{
    ce_skinptstarts.initFromArray(cache.myPrimPtStarts);
    ce_skinptindices.initFromArray(cache.myPtIndices);
    ce_skinptweights.initFromArray(cache.myPtWeights);
}

bool
hdComputeGuideOffsets(
    UT_Array<int> &guidestarts,
    UT_Array<int> &guideindices,
    UT_Array<float> &guideweights,
    const UT_IntArray &skinprimptstarts,
    const UT_IntArray &skinptindices,
    const UT_FloatArray &skinptweights,
    exint n,
    const VtArray<int> &vt_guides,
    const UT_Array<int> &guidesindex,
    const VtArray<float> &vt_guideweights,
    const UT_Array<int> &guideweightsindex)
{
    utZoneScoped;

    // Validate that guides and weights index arrays match
    if (guidesindex.size() != guideweightsindex.size())
    {
        UT_ErrorLog::error("HairDeform: hdComputeGuideOffsets: guidesindex size ({}) != guideweightsindex size ({})",
                          guidesindex.size(), guideweightsindex.size());
        return false;
    }
    for (exint i = 0; i < guidesindex.size(); ++i)
    {
        if (guidesindex[i] != guideweightsindex[i])
        {
            UT_ErrorLog::error("HairDeform: hdComputeGuideOffsets: index mismatch at {}: guidesindex={}, guideweightsindex={}",
                              i, guidesindex[i], guideweightsindex[i]);
            return false;
        }
    }

    UT_Array<int> weightcounts;
    weightcounts.setSize(n);
    {
    utZoneScopedN("counts");
    UTparallelFor(
        UT_BlockedRange<exint>(0, n),
        [&](const UT_BlockedRange<exint> &r)
    {
        UT_Map<int, float> tempmap;

        for (exint i = r.begin(), end = r.end(); i < end; ++i)
        {
            tempmap.clear();
            GA_Size ptstart = skinprimptstarts[i];
            GA_Size ptend = skinprimptstarts[i+1];

            // Blend guides/weights from all contributing skin points
            for (GA_Size k = ptstart; k < ptend; ++k)
            {
                int ptidx = skinptindices[k];
                float pw = skinptweights[k];

                // Skip low-weight points (matches SOP behavior)
                if (pw < SYS_FTOLERANCE)
                    continue;

                // Read guides for this mesh point from flat Hydra array
                int guidestart = guidesindex[ptidx];
                int guideend = guidesindex[ptidx + 1];

                for (int g = guidestart; g < guideend; ++g)
                {
                    int guideIdx = vt_guides[g];
                    float weight = vt_guideweights[g] * pw;

                    // Accumulate (blend duplicate guide indices)
                    auto it = tempmap.find(guideIdx);
                    if (it != tempmap.end())
                        it->second += weight;
                    else
                        tempmap[guideIdx] = weight;
                }
            }

            weightcounts[i] = tempmap.size();
        }
    });
    }

    guidestarts.setSizeNoInit(n+1);

    {
    utZoneScopedN("scan");

    // count to start/end array
    guidestarts[0] = 0;
    std::inclusive_scan(
            weightcounts.begin(), weightcounts.end(),
            guidestarts.begin() + 1);
    }

    GA_Size offsetcount = guidestarts[guidestarts.size()-1];
    guideindices.setSizeNoInit(offsetcount);
    guideweights.setSizeNoInit(offsetcount);

    {
    utZoneScopedN("offsets");
    UTparallelFor(
        UT_BlockedRange<exint>(0, n),
        [&](const UT_BlockedRange<exint> &r)
    {
        UT_Map<int, float> tempmap;

        for (exint i = r.begin(), end = r.end(); i < end; ++i)
        {
            tempmap.clear();
            GA_Size ptstart = skinprimptstarts[i];
            GA_Size ptend = skinprimptstarts[i+1];

            // Blend guides/weights from all contributing skin points
            float totalweight = 0.0f;
            for (GA_Size k = ptstart; k < ptend; ++k)
            {
                int ptidx = skinptindices[k];
                float pw = skinptweights[k];

                // Skip low-weight points (matches SOP behavior)
                if (pw < SYS_FTOLERANCE)
                    continue;

                totalweight += pw;

                // Read guides for this mesh point from flat Hydra array
                int guidestart = guidesindex[ptidx];
                int guideend = guidesindex[ptidx + 1];

                for (int g = guidestart; g < guideend; ++g)
                {
                    int guideIdx = vt_guides[g];
                    float weight = vt_guideweights[g] * pw;

                    // Accumulate (blend duplicate guide indices)
                    auto it = tempmap.find(guideIdx);
                    if (it != tempmap.end())
                        it->second += weight;
                    else
                        tempmap[guideIdx] = weight;
                }
            }

            // Normalize and output
            float invtotal = totalweight > 0.0f ? 1.0f / totalweight : 0.0f;
            int j = 0;
            for (const auto &entry : tempmap)
            {
                guideindices[guidestarts[i] + j] = entry.first;
                guideweights[guidestarts[i] + j] = entry.second * invtotal;
                ++j;
            }
        }
    });
    }

    return true;
}

static bool
hdComputeGIMGuideOffsets(
        UT_IntArray &guidestarts,
        UT_IntArray &guideindices,
        UT_FloatArray &guideweights,
        GIMSurfaceTopoCacheMapType &gimsurfacetopocachemap,
        const UT_StringHolder &groompath,
        const VtVec3fArray &groompoints,
        const UT_IntArray &curveprimptsindex,
        const HdContainerDataSourceHandle &guideinterpds,
        const SdfPath &guideinterpprimpath,
        ResolvedPointsCacheMapType *resolvedpointscachemap,
        const VtIntArray &curvevtxcounts)
{
    utZoneScoped;

    using namespace HD_HairDeformUtils;

    // Validate guide interpolation mesh input
    if (!guideinterpds)
    {
        UT_ErrorLog::error(
                "HairDeform: Guide interpolation mesh input not connected");
        return false;
    }

    HdMeshSchema gimesh = HdMeshSchema::GetFromParent(guideinterpds);
    if (!gimesh)
    {
        UT_ErrorLog::error(
                "HairDeform: Guide interpolation input is not a Mesh");
        return false;
    }

    // Get guide mesh primvars for guides and weights array attributes
    HdPrimvarsSchema gimpvs = HdPrimvarsSchema::GetFromParent(guideinterpds);
    if (!gimpvs)
    {
        UT_ErrorLog::error(
                "HairDeform: Guide interpolation mesh has no primvars");
        return false;
    }

    // Get guide mesh points
    auto vt_gim_points = getAnimPoints(
            gimpvs, guideinterpprimpath, 0.0f, resolvedpointscachemap);
    if (!vt_gim_points.has_value())
    {
        UT_ErrorLog::error(
                "HairDeform: Guide interpolation mesh has no points");
        return false;
    }

    // Fetch guides and weights array attributes with their lengths
    auto vt_gimguides = getConstPvVal<int>(
            gimpvs, _tokens->guides, 0.0f);
    auto vt_gimguides64 = getConstPvVal<int64>(
            gimpvs, _tokens->guides, 0.0f);
    auto vt_gimguideslengths = getConstPvVal<int>(
            gimpvs, _tokens->guidesLengths, 0.0f);
    auto vt_gimweights = getConstPvVal<float>(
            gimpvs, _tokens->guideWeights, 0.0f);
    auto vt_gimweightslengths = getConstPvVal<int>(
            gimpvs, _tokens->guideWeightsLengths, 0.0f);

    int missing = 0;
    if (!vt_gimguides.has_value() && !vt_gimguides64.has_value())
    {
        UT_ErrorLog::warning(
                "HairDeform: Guide interpolation mesh missing 'guides' attribute");
        ++missing;
    }
    if (!vt_gimguideslengths.has_value())
    {
        UT_ErrorLog::warning(
                "HairDeform: Guide interpolation mesh missing 'guides:lengths' attribute");
        ++missing;
    }
    if (!vt_gimweights.has_value())
    {
        UT_ErrorLog::warning(
                "HairDeform: Guide interpolation mesh missing 'weights' attribute");
        ++missing;
    }
    if (!vt_gimweightslengths.has_value())
    {
        UT_ErrorLog::warning(
                "HairDeform: Guide interpolation mesh missing 'weights:lengths' attribute");
        ++missing;
    }
    if (missing > 0)
        return false;

    const VtArray<int> *gim_guides = nullptr;
    VtArray<int> gim_guides32;
    if (vt_gimguides.has_value())
    {
        gim_guides = &vt_gimguides.value();
    }
    else if (vt_gimguides64.has_value())
    {
        const auto &src = vt_gimguides64.value();
        gim_guides32.resize(src.size());
        std::transform(
                src.begin(), src.end(), gim_guides32.begin(),
                [](int64 v) { return static_cast<int>(v); });
        gim_guides = &gim_guides32;
    }

    // Build index arrays from length arrays
    int ngimpts = vt_gim_points->size();
    UT_Array<int> guidesindex, guideweightsindex;
    indexFromLength(
            guidesindex, ngimpts, vt_gimguideslengths->cdata());
    indexFromLength(
            guideweightsindex, ngimpts, vt_gimweightslengths->cdata());

    // Build GU_Detail from guide mesh (for surface interpolation)
    GU_Detail gimgdp;
    hdDetailFromMesh(gimgdp, gimesh, vt_gim_points.value());

    // Create ray intersect for the guide interpolation mesh
    GU_RayIntersect gimrayintersect(&gimgdp);

    // Get surface capture for curves - project onto guide interp mesh
    // Key by groom path since capture depends on both GIM and groom curve roots
    GIMSurfaceTopoCacheMapType::accessor surfacc;
    bool surfinserted = gimsurfacetopocachemap.insert(
            surfacc, groompath);

    if (!surfinserted)
    {
        HD_HairDeformUtils::cacheLog(
                "HairDeform: CACHE HIT _gimsurfacetopocachemap[{}]",
                groompath);
    }
    else
    {
        HD_HairDeformUtils::cacheLog(
                "HairDeform: CACHE MISS _gimsurfacetopocachemap[{}], computing...",
                groompath);
        using RootAcc = const GU_SurfaceDeform::ArrayAccessor<
                UT_Vector3,
                const VtVec3fArray &,
                RootPointAccessorPolicy>;

        RootPointAccessorPolicy rootpolicy(
                groompoints, curveprimptsindex);
        RootAcc rootacc(groompoints, rootpolicy);

        GU_SurfaceDeform::surfaceInterpOffsets(
                surfacc->second.myPrimPtStarts,
                surfacc->second.myPtIndices,
                surfacc->second.myPtWeights, rootacc, gimgdp,
                gimrayintersect);
    }

    // Compute guide offsets from Hydra guide/weight arrays
    // Key by groom path only - each groom has one GIM, invalidation uses dependant pattern
    int nprims = curvevtxcounts.size();
    bool success = hdComputeGuideOffsets(
            guidestarts,
            guideindices,
            guideweights,
            surfacc->second.myPrimPtStarts,
            surfacc->second.myPtIndices,
            surfacc->second.myPtWeights, nprims,
            *gim_guides, guidesindex,
            vt_gimweights.value(), guideweightsindex);
    if (!success)
    {
        UT_ErrorLog::error(
                "HairDeform: Failed to compute Guide Interpolation Mesh offsets.",
                groompath);
        return false;
    }

    return true;
}

void
hdMakeRestPoints(
    RestPointsCacheMapType &restpointscachemap,
    const UT_StringHolder &primpath,
    int npts,
    HdPrimvarsSchema &groompvs,
    const VtVec3fArray &vt_points)

{
    utZoneScoped;

    using namespace HD_HairDeformUtils;

    RestPointsCacheMapType::accessor acc;
    bool inserted = restpointscachemap.insert(
            acc, primpath);

    if (!inserted)
    {
        utZoneScopedN("copy_position");
        HD_HairDeformUtils::cacheLog("HairDeform: CACHE HIT rest points for prim {}", primpath);
    }
    else
    {
        utZoneScopedN("expand_barbs");
        //
        // Cached barb expansion
        //
        HD_HairDeformRestPointsCache &rpcache = acc->second;

        VtValue barbl_value, barbr_value;
        int nbarblpts = 0, nbarbrpts = 0;

        HD_HairDeformUtils::cacheLog("HairDeform: CACHE MISS rest points");
        BarbFloats barbl = getBarbData(
                barbl_value, nbarblpts, groompvs, _tokens->barbl, npts, 3);
        BarbFloats barbr = getBarbData(
                barbr_value, nbarbrpts, groompvs, _tokens->barbr, npts, 3);

        VtVec3fArray &pos = rpcache.myPoints;

        // hasBarbOrient() is the same test, for the topology and primvars.
        auto vt_barborient = getConstPvVal<GfQuatf>(
                groompvs, _tokens->barborient, 0.0f);

        bool have_barbl = barbl.isValid();
        bool have_barbr = barbr.isValid();
        bool have_barborient = vt_barborient.has_value();
        bool all_feather_attrs = have_barbl && have_barbr && have_barborient;
        bool no_feather_attrs = !have_barbl && !have_barbr && !have_barborient;

        if (!all_feather_attrs)
        {
            if (!no_feather_attrs)
                UT_ErrorLog::error(
                        "HairDeform: Feather attributes incomplete. "
                        "Need at least P_barbl, P_barbr and barborient\n");

            resizeUninitialized(pos, vt_points.size());
            std::uninitialized_copy(
                    vt_points.begin(), vt_points.end(), pos.begin());
        }
        else
        {
            // P_barbl/P_barbr are shaft-relative, in the shaft's orient
            // frame; nothing else is.
            expandBarbs(
                    pos, npts, 3, nbarblpts, nbarbrpts, barbl, barbr,
                    &(vt_barborient.value()), vt_points,
                    /* xform_to_object */ true);

            rpcache.myBarbLayout.myNumShaftPts = npts;
            rpcache.myBarbLayout.myNumBarbL = nbarblpts;
            rpcache.myBarbLayout.myNumBarbR = nbarbrpts;
        }
    }
}
void
hdInitOrientAttribs(
        UT_Array<UT_Quaternion> &orients,
        UT_Array<UT_Quaternion> &restorients,
        UT_Array<int> &flags,
        const VtVec3fArray &points,
        const VtIntArray &curvevtxcounts,
        const UT_Array<int> &curvevtxindex,
        exint npts)
{
    utZoneScoped;

    orients.setSizeNoInit(npts);
    restorients.setSizeNoInit(npts);
    flags.setSizeNoInit(npts);

    UTparallelFor(
            UT_BlockedRange<exint>(0, curvevtxcounts.size()),
            [&](const UT_BlockedRange<exint> &r)
    {
        for (exint i = r.begin(), end = r.end(); i < end; ++i)
        {
            int lastpt;
            UT_Vector3 lastpos;
            UT_QuaternionF last_q;
            last_q.identity();
            int j = 0;
            int n = curvevtxcounts[i];

            // ok to access i+1 here because index array is one larger
            // than the counts array
            for (int pt = curvevtxindex[i]; pt < curvevtxindex[i + 1];
                 ++pt, ++j)
            {
                flags[pt] = GU_Cosserat::computeInitFlags(j, n);

                // Compute rotation along curve.
                // In the first ieration, just write last.. values.
                // Each subsequent iteration then computes the previous
                // point's orientation.
                if (j == 0)
                {
                    GfVec3f temppos = points[pt];
                    lastpt = pt;
                    lastpos = UT_Vector3(temppos[0], temppos[1], temppos[2]);
                    continue;
                }

                GfVec3f temppos = points[pt];
                UT_Vector3 pos = UT_Vector3(temppos[0], temppos[1], temppos[2]);

                last_q = GU_Cosserat::computeMinimalTwistOrient(
                        lastpos, pos, last_q);

                orients[lastpt] = last_q;

                lastpt = pt;
                lastpos = pos;
            }

            // set last point to second-to-last's orient
            orients[lastpt] = last_q;

            // now that we have orients for the entire curve,
            // run a second pass to compute restorient
            UT_QuaternionF restorient;
            for (int pt = curvevtxindex[i]; pt < curvevtxindex[i + 1];
                 ++pt, ++j)
            {
                int flag = flags[pt];

                // Compute rotation along curve.
                // In the first ieration, just write last.. values.
                // Each subsequent iteration then computes the previous
                // point's orientation.
                // Relative orientation from this frame to the next in
                // rest space. Stored as a unit quaternion; the bend
                // stiffness scale (4*kbend/lRest) is applied in the
                // cosserat_update_orient kernel using the post-LBS
                // rest lengths so the balance is always correct.
                UT_QuaternionF restorient(0, 0, 0, 1);  // identity
                if (flag & ((int)GU_Cosserat::PointFlags::HASNEXT))
                {
                    const UT_QuaternionF orient = orients[pt];
                    int nextpt = pt + 1;
                    const UT_QuaternionF nextorient = orients[nextpt];

                    UT_QuaternionF invorient(orient);
                    invorient.invert();

                    restorient = invorient * nextorient;
                    restorient.normalize();
                }

                restorients[pt] = restorient;
            }
        }
    });
}

} // anonymous namespace

bool
HD_HairDeformPointsDataSource::_PrepareSkinCaptureData(
        SkinCaptureCEData &skince,
        const GU_Detail &skinrestgdp,
        const GA_Attribute *cachedrestnml,
        const GA_Attribute *cachedresttan,
        const VtVec3fArray &vt_skinrestpos,
        const VtVec3fArray &vt_skinanimpos)
{
    using namespace HD_HairDeformUtils;
    utZoneScopedN("prepare_skin_capture_data");

    constexpr int vectorsize = 3;

    // Use cached rest N/T if available, otherwise compute from gdp.
    // Both attributes are required for downstream uploads.
    GA_AttributeUPtr skinrestnml_tmp, skinresttan_tmp;
    const GA_Attribute *skinrestnml = cachedrestnml;
    const GA_Attribute *skinresttan = cachedresttan;
    if (!skinrestnml || !skinresttan)
    {
        GU_SurfaceDeform::computeFrames(
                skinrestgdp, skinrestgdp.getP(),
                skinrestnml_tmp, skinresttan_tmp);
        skinrestnml = skinrestnml_tmp.get();
        skinresttan = skinresttan_tmp.get();
    }

    // Compute anim N/T frames (must run each frame)
    GA_AttributeUPtr skinanimnml, skinanimtan;
    GA_AttributeUPtr skinanimpos = skinrestgdp.createDetachedTupleAttribute(
            GA_ATTRIB_POINT, GA_STORE_REAL32, 3);
    hdGAPosAttribFromVtArray(*skinanimpos.get(), vt_skinanimpos);
    GU_SurfaceDeform::computeFrames(
            skinrestgdp, skinanimpos.get(), skinanimnml, skinanimtan);

    // Upload skin attributes to CE
    hdUploadSkinAttrsToCE(
            skince.ce_skinrestpos,
            skince.ce_skinanimpos,
            skince.ce_skinrestnml,
            skince.ce_skinresttan,
            skince.ce_skinanimnml,
            skince.ce_skinanimtan,
            skinrestgdp,
            vt_skinrestpos,
            vt_skinanimpos,
            skinrestnml,
            skinresttan,
            skinanimnml.get(),
            skinanimtan.get(),
            vectorsize);

    return true;
}

void
HD_HairDeformPointsDataSource::_ComputeCurveSkinXforms(
        CE_Context &context,
        CE_FloatArray &ce_curve_skinxform,
        CE_Int32Array &ce_curve_skinptstarts,
        CE_Int32Array &ce_curve_skinptindices,
        CE_FloatArray &ce_curve_skinptweights,
        SkinCaptureCEData &skince,
        exint ncurveprims,
        bool recompile)
{
    utZoneScopedN("compute_curve_skin_xforms");
    constexpr int xformsize = 16;  // 4x4 matrix
    ce_curve_skinxform.init(xformsize * ncurveprims);

    if (ncurveprims <= 0)
        return;

    auto xformkernel = getKernel(
            context, "deform/surfacedeform.cl",
            "surfaceDeformComputeWeightedXform",
            "", recompile);

    enqueueKernel(context, ncurveprims, xformkernel,
            (int)ncurveprims,
            (int*)nullptr,  // group
            ce_curve_skinxform.buffer(),
            ce_curve_skinptstarts.buffer(),
            ce_curve_skinptindices.buffer(),
            ce_curve_skinptweights.buffer(),
            skince.ce_skinrestpos.buffer(),
            skince.ce_skinrestnml.buffer(),
            skince.ce_skinresttan.buffer(),
            skince.ce_skinanimpos.buffer(),
            skince.ce_skinanimnml.buffer(),
            skince.ce_skinanimtan.buffer());
    context.getQueue().finish();
}

bool
HD_HairDeformPointsDataSource::_ComputeGuideDeform(
        CE_Context &context,
        CE_FloatArray &ce_main_pos_out,
        PointDeformCEArrays &pointdeform_cearrays,
        CE_FloatArray &ce_main_skinxform,
        const VtVec3fArray &restPoints,
        const UT_IntArray &curveprimptsindex,
        const VtIntArray &vt_curvevtxcounts,
        const HdPrimvarsSchema &groompvs,
        const VtVec3fArray &vt_deformerrestpos,
        const VtVec3fArray &vt_deformeranimpos,
        DeformMethod deformmethod,
        const GU_GuideCaptureParms &gsiParms,
        exint npts,
        bool recompile)
{
    using namespace HD_HairDeformUtils;
    utZoneScopedN("guide_deform");

    constexpr int vectorsize = 3;

    // Compute or fetch guide indices/weights
    GuideInterpCacheMapType::accessor guideacc;
    bool guideinserted = _guideinterpcachemap->insert(
            guideacc, UT_StringHolder(_primpath.GetText()));

    // The capture blocks below only run on insert, so an entry left
    // half-built by a failed capture would read as a hit on the next
    // cook and deform against empty guide arrays.
    bool guidecacheok = !guideinserted;
    UT_SCOPE_EXIT
    {
        if (!guidecacheok)
            _guideinterpcachemap->erase(guideacc);
    };

    if (!guideinserted)
    {
        HD_HairDeformUtils::cacheLog(
                "HairDeform: CACHE HIT _guideinterpcachemap[{}]",
                _primpath.GetText());
    }
    else
    {
        HD_HairDeformUtils::cacheLog(
                "HairDeform: CACHE MISS _guideinterpcachemap[{}], computing...",
                _primpath.GetText());
    }

    if (guideinserted
        && deformmethod == DeformMethod::GUIDEINTERPOLATIONMESH)
    {
        if (!hdComputeGIMGuideOffsets(
                    guideacc->second.myGuideStarts,
                    guideacc->second.myGuideIndices,
                    guideacc->second.myGuideWeights,
                    *_gimsurfacetopocachemap,
                    UT_StringHolder(_primpath.GetText()),
                    restPoints,
                    curveprimptsindex,
                    _guideinterpds,
                    _guideinterpprimpath,
                    _resolvedpointscachemap.get(),
                    vt_curvevtxcounts))
        {
            return false;
        }

        guideacc->second.myUseVtGuideWeights = false;
        guideacc->second.myGuideIndicesVt.clear();
        guideacc->second.myGuideWeightsVt.clear();
    }

    if (guideinserted && deformmethod == DeformMethod::GUIDEWEIGHTS)
    {
        if (!hdGetGuideWeightsFromPrimvars(
                    groompvs, vt_curvevtxcounts,
                    guideacc->second.myGuideStarts,
                    guideacc->second.myGuideIndicesVt,
                    guideacc->second.myGuideWeightsVt))
        {
            return false;
        }

        guideacc->second.myUseVtGuideWeights = true;
    }

    if (guideinserted && deformmethod == DeformMethod::GUIDESHAPEINTERPOLATION)
    {
        utZoneScopedN("gsi_capture");

        // Build minimal GU_Details for groom and guide curves.
        // BasisCurves in Hydra are contiguous: curve i occupies a
        // sequential block of points starting at curveprimptsindex[i].
        const int groomNPts   = (int)restPoints.size();
        const int groomNPrims = (int)vt_curvevtxcounts.size();

        const auto &vt_defvtxcounts = hdGetVertexCounts(_deformerds);
        const int guideNPts   = (int)vt_deformerrestpos.size();
        const int guideNPrims = (int)vt_defvtxcounts.size();

        // Sequential index arrays (points already contiguous per curve)
        UT_IntArray groomIdx(groomNPts);
        for (int i = 0; i < groomNPts; ++i) groomIdx[i] = i;
        UT_IntArray guideIdx(guideNPts);
        for (int i = 0; i < guideNPts; ++i) guideIdx[i] = i;

        GA_PolyCounts groomCounts, guideCounts;
        for (int ci = 0; ci < groomNPrims; ++ci)
            groomCounts.append(vt_curvevtxcounts[ci]);
        for (int ci = 0; ci < guideNPrims; ++ci)
            guideCounts.append(vt_defvtxcounts[ci]);

        GU_Detail groomGdp, guideGdp;

        groomGdp.appendPointBlock(groomNPts);
        for (int pi = 0; pi < groomNPts; ++pi)
            groomGdp.setPos3(GA_Offset(pi),
                UT_Vector3(restPoints[pi][0], restPoints[pi][1], restPoints[pi][2]));
        GEO_PrimPoly::buildBlock(&groomGdp, GA_Offset(0), groomNPts,
            groomCounts, groomIdx.array(), /*open=*/true);

        guideGdp.appendPointBlock(guideNPts);
        for (int pi = 0; pi < guideNPts; ++pi)
            guideGdp.setPos3(GA_Offset(pi),
                UT_Vector3(vt_deformerrestpos[pi][0],
                           vt_deformerrestpos[pi][1],
                           vt_deformerrestpos[pi][2]));
        GEO_PrimPoly::buildBlock(&guideGdp, GA_Offset(0), guideNPts,
            guideCounts, guideIdx.array(), /*open=*/true);

        if (!GU_GuideCaptureShape(
                guideacc->second.myGuideStarts,
                guideacc->second.myGuideIndices,
                guideacc->second.myGuideWeights,
                groomGdp, guideGdp, gsiParms))
        {
            UT_ErrorLog::error(
                "HairDeform: Guide shape interpolation capture failed");
            return false;
        }

        guideacc->second.myUseVtGuideWeights = false;
        guideacc->second.myGuideIndicesVt.clear();
        guideacc->second.myGuideWeightsVt.clear();
    }

    guidecacheok = true;

    {
    utZoneScopedN("guide_deform_opencl");

    const char *guidemethodname
            = (deformmethod == DeformMethod::GUIDEWEIGHTS)
                    ? "Guide Weights"
                    : (deformmethod == DeformMethod::GUIDESHAPEINTERPOLATION)
                        ? "Guide Shape Interpolation"
                        : "Guide Interpolation Mesh";

    // Set to true to skip guide interpolation and just apply surface deform transform
    // This applies the per-curve skin transform to all points (like SURFACEDEFORM mode)
    constexpr bool surfaceDeformOnly = false;

    int defnpts = vt_deformeranimpos.size();

    // Guide deform requires curve deformers (guides)
    HdBasisCurvesSchema defcurves = HdBasisCurvesSchema::GetFromParent(_deformerds);
    if (!defcurves)
    {
        UT_ErrorLog::error(
                "HairDeform: {} method requires BasisCurves deformer",
                guidemethodname);
        return false;
    }

    const auto &vt_deformervtxcounts = hdGetVertexCounts(_deformerds);
    int defnprims = vt_deformervtxcounts.size();
    UT_IntArray defprimptsindex;
    if (defnprims > 0)
    {
        indexFromLength(
                defprimptsindex, defnprims,
                vt_deformervtxcounts.cdata());
    }

    // Initialize CE arrays
    CE_Int32Array ce_main_guidestarts, ce_main_guideindices;
    CE_FloatArray ce_main_guideweights;
    CE_FloatArray ce_main_edgelengths, ce_def_edgelengths;

    ce_main_guidestarts.initFromArray(guideacc->second.myGuideStarts);
    if (guideacc->second.myUseVtGuideWeights)
    {
        ce_main_guideindices.initFromData(
                guideacc->second.myGuideIndicesVt.cdata(),
                guideacc->second.myGuideIndicesVt.size());
        ce_main_guideweights.initFromData(
                guideacc->second.myGuideWeightsVt.cdata(),
                guideacc->second.myGuideWeightsVt.size());
    }
    else
    {
        ce_main_guideindices.initFromArray(guideacc->second.myGuideIndices);
        ce_main_guideweights.initFromArray(guideacc->second.myGuideWeights);
    }

    ce_main_edgelengths.init(npts);
    ce_def_edgelengths.init(defnpts);

    // Initialize deformer position CE arrays for guide interpolation
    pointdeform_cearrays.ce_deformerrestpos.initFromData(
            (const float *)vt_deformerrestpos.cdata(),
            vectorsize * defnpts);
    pointdeform_cearrays.ce_deformeranimpos.initFromData(
            (const float *)vt_deformeranimpos.cdata(),
            vectorsize * defnpts);

    int nprims = vt_curvevtxcounts.size();

    pointdeform_cearrays.ce_primptsindex.initFromArray(curveprimptsindex);

    // Build CE topology arrays for deformer curves
    CE_Int32Array ce_def_primptsindex;
    if (defnprims > 0)
        ce_def_primptsindex.initFromArray(defprimptsindex);

    hdComputeEdgeLengthsForCurves(
            context,
            ce_main_pos_out,
            pointdeform_cearrays.ce_primptsindex,
            ce_main_edgelengths,
            nprims,
            recompile);

    hdComputeEdgeLengthsForCurves(
            context,
            pointdeform_cearrays.ce_deformerrestpos,
            ce_def_primptsindex,
            ce_def_edgelengths,
            defnprims,
            recompile);

    // Build point-to-curve mapping on GPU
    CE_Int32Array ce_main_pointprims;
    ce_main_pointprims.init(npts);
    {
        auto ptmap_kernel = getKernel(
            context, "deform/guidedeform.cl", "buildPointToCurveMap",
            "", recompile);

        enqueueKernel(context, nprims, ptmap_kernel,
                (int)nprims,
                pointdeform_cearrays.ce_primptsindex.buffer(),
                ce_main_pointprims.buffer());
        context.getQueue().finish();
    }

    // Call deform kernel
    if (surfaceDeformOnly)
    {
        // Simple surface deform: just apply per-curve transform to all points
        auto kernel = getKernel(
            context, "deform/surfacedeform.cl", "surfaceDeformBasisCurves",
            "", recompile);

        enqueueKernel(context, npts, kernel,
                (int)npts,
                (int*)nullptr,  // group
                ce_main_pos_out.buffer(),  // outP
                ce_main_pos_out.buffer(),  // P (input)
                ce_main_skinxform.buffer(),
                ce_main_pointprims.buffer());
        context.getQueue().finish();
    }
    else
    {
        // Full guide interpolation deform (BasisCurves path)
        auto guide_deform = getKernel(
            context, "deform/guidedeform.cl", "weightedGuideDeformBasisCurves",
            "", recompile);

        // SOP only uses orient_blend when useorientattrib is enabled.
        float orient_blend = 0.0f;
        HairDeformSchema hairdeformschema = HairDeformSchema::GetFromParent(_primds);
        bool useorientattrib_for_blend = false;
        if (auto value = hairdeformschema.GetUseOrientAttrib())
            useorientattrib_for_blend = value->GetTypedValue(0.0f);
        if (useorientattrib_for_blend)
        {
            if (auto orientblendds = hairdeformschema.GetOrientBlend())
                orient_blend = orientblendds->GetTypedValue(0.0f);
        }

        const cl::Buffer *orient_rest_buf;
        const cl::Buffer *orient_anim_buf;
        CE_FloatArray ce_dummy_orient;

        if (orient_blend > 0.0f
            && pointdeform_cearrays.ce_deformerrestorient.size() > 0
            && pointdeform_cearrays.ce_deformeranimorient.size() > 0)
        {
            orient_rest_buf = &pointdeform_cearrays.ce_deformerrestorient.buffer();
            orient_anim_buf = &pointdeform_cearrays.ce_deformeranimorient.buffer();
        }
        else
        {
            orient_blend = 0.0f;
            ce_dummy_orient.init(4);
            orient_rest_buf = &ce_dummy_orient.buffer();
            orient_anim_buf = &ce_dummy_orient.buffer();
        }

        enqueueKernel(context, npts, guide_deform,
                (int)npts,
                (int*)nullptr,  // group
                ce_main_pos_out.buffer(),
                ce_main_pos_out.buffer(),
                ce_main_skinxform.buffer(),
                pointdeform_cearrays.ce_primptsindex.buffer(),
                ce_main_pointprims.buffer(),
                ce_main_edgelengths.buffer(),
                ce_main_guidestarts.buffer(),
                ce_main_guideindices.buffer(),
                ce_main_guideweights.buffer(),
                ce_def_primptsindex.buffer(),
                ce_def_edgelengths.buffer(),
                pointdeform_cearrays.ce_deformerrestpos.buffer(),
                pointdeform_cearrays.ce_deformeranimpos.buffer(),
                orient_blend,
                *orient_rest_buf,
                *orient_anim_buf);
        context.getQueue().finish();

    }
    }

    return true;
}

bool
HD_HairDeformPointsDataSource::_ComputeSubdSkinXforms(
        CE_Context &context,
        CE_FloatArray &ce_xform,
        int ncurves,
        const HD_HairDeformRestPointsCache &restpoints,
        const UT_IntArray &curveprimptsindex,
        const VtVec3fArray &vt_skinrestpos,
        const VtVec3fArray &vt_skinanimpos,
        bool perpointcapture,
        const UT_StringHolder &captureidattrib,
        UT_Lock::Scope *lockscope)
{
    using namespace HD_HairDeformUtils;
    utZoneScopedN("subd_compute_skin_xforms");

    // Get skin mesh cache (has OSD topology + ray intersect)
    SkinMeshCacheMapType::const_accessor skinacc;
    if (!_skinmeshcachemap->find(
            skinacc, UT_StringHolder(_skinprimpath.GetText())))
    {
        UT_ErrorLog::error("HairDeform: Skin mesh cache not found");
        return false;
    }
    auto &skincache = skinacc->second;
    if (!skincache.mySubdTopology || !skincache.myGdp
        || !skincache.myRayIntersect)
    {
        UT_ErrorLog::error(
            "HairDeform: Skin mesh cache incomplete for subd path");
        return false;
    }

    // Get or create subd eval cache for this groom
    SkinSubdEvalCacheMapType::accessor subdacc;
    bool subdinserted = _skinsubdcachemap->insert(
            subdacc, UT_StringHolder(_primpath.GetText()));
    auto &subdcache = subdacc->second;

    // The cached patch coords are captured at whatever count the caller asked
    // for last time -- per point or per curve -- so a switch between the two
    // has to recapture, or evaluate() would write one result per cached coord
    // into buffers sized for the new count.
    if (!subdinserted && subdcache.myPatchFace.size() != ncurves)
    {
        cacheLog("HairDeform: subd eval capture count changed - recapturing");
        subdinserted = true;
    }

    // Capture path (cache miss): project curve roots onto skin,
    // convert to ptex patch coordinates
    if (subdinserted)
    {
        cacheLog("HairDeform: CACHE MISS subd eval cache - capturing");

        // Positions to capture: every point with per-point capture,
        // otherwise the first point of each curve.
        UT_Array<UT_Vector3F> rootPositions(ncurves, ncurves);
        for (int i = 0; i < ncurves; ++i)
        {
            int ptidx = perpointcapture ? i : curveprimptsindex[i];
            const GfVec3f &p = restpoints.myPoints[ptidx];
            rootPositions[i] = UT_Vector3F(p[0], p[1], p[2]);
        }

        // Project onto rest skin surface to get coarse face/u/v
        UT_IntArray coarseFace;
        UT_FloatArray coarseU, coarseV;
        coarseFace.setSizeNoInit(ncurves);
        coarseU.setSizeNoInit(ncurves);
        coarseV.setSizeNoInit(ncurves);

        const GU_Detail &gdp = *skincache.myGdp;
        const GU_RayIntersect &rayintersect = *skincache.myRayIntersect;

        // Only now that the capture is actually going to run is it worth
        // resolving the groom's ids.
        const HD_HairDeformIdBuckets *idbuckets = nullptr;
        UT_Array<int> bucketindices;
        if (captureidattrib.isstring())
        {
            idbuckets = hdFindIdBuckets(skincache, captureidattrib);
            if (!idbuckets)
            {
                UT_ErrorLog::error(
                        "HairDeform: Capture id buckets missing for skin '{}'",
                        _skinprimpath.GetText());
                _skinsubdcachemap->erase(subdacc);
                return false;
            }

            if (!_BucketIndicesFromCaptureIds(
                        bucketindices, *idbuckets, captureidattrib,
                        perpointcapture, curveprimptsindex, restpoints))
            {
                _skinsubdcachemap->erase(subdacc);
                return false;
            }
            if (bucketindices.size() != ncurves)
            {
                UT_ErrorLog::error(
                        "HairDeform: Subd capture id bucket count ({}) does "
                        "not match the captured position count ({})",
                        bucketindices.size(), ncurves);
                _skinsubdcachemap->erase(subdacc);
                return false;
            }

        }

        UTparallelFor(
            UT_BlockedRange<exint>(0, ncurves),
            [&](const UT_BlockedRange<exint> &r)
            {
                for (exint i = r.begin(), end = r.end(); i < end; ++i)
                {
                    GU_MinInfo mininfo{};
                    if (idbuckets)
                    {
                        if (const GU_RayIntersect *tree
                                = idbuckets->tree(bucketindices[i]))
                            tree->minimumPoint(rootPositions[i], mininfo);
                    }
                    else
                    {
                        rayintersect.minimumPoint(rootPositions[i], mininfo);
                    }
                    GA_Offset primoff = mininfo.prim.offset();
                    if (primoff != GA_INVALID_OFFSET)
                    {
                        coarseFace[i] =
                            (int)gdp.primitiveIndex(primoff);
                        coarseU[i] = mininfo.u1;
                        coarseV[i] = mininfo.v1;
                    }
                    else
                    {
                        coarseFace[i] = -1;
                        coarseU[i] = 0.5f;
                        coarseV[i] = 0.5f;
                    }
                }
            });

        // Convert coarse face/u/v to ptex patch coordinates
        if (!skincache.mySubdTopology->convertToPatch(
                UT_Span<const int>(
                    coarseFace.array(), coarseFace.size()),
                UT_Span<const float>(
                    coarseU.array(), coarseU.size()),
                UT_Span<const float>(
                    coarseV.array(), coarseV.size()),
                subdcache.myPatchFace,
                subdcache.myPatchU,
                subdcache.myPatchV))
        {
            UT_ErrorLog::error(
                "HairDeform: OSD patch coord conversion failed");
            // Capture only runs on insert, so an entry left without patch
            // coords would be uploaded empty on every later cook.
            _skinsubdcachemap->erase(subdacc);
            return false;
        }

        subdcache.myPatchCoordsUploaded = false;
        subdcache.myEvalInitialized = false;
    }
    else
    {
        cacheLog("HairDeform: CACHE HIT subd eval cache");
    }

    // Initialize GPU evaluators if needed
    if (!subdcache.myEvalInitialized)
    {
        subdcache.myRestEval = UTmakeUnique<GU_OSDEval>();
        subdcache.myAnimEval = UTmakeUnique<GU_OSDEval>();

        if (!subdcache.myRestEval->initEvaluator(context, /*tuple_size=*/3)
            || !subdcache.myAnimEval->initEvaluator(context, /*tuple_size=*/3)
            || !subdcache.myRestEval->setTopology(
                *skincache.mySubdTopology, context)
            || !subdcache.myAnimEval->setTopology(
                *skincache.mySubdTopology, context))
        {
            UT_ErrorLog::error(
                "HairDeform: OSD evaluator initialization failed");
            return false;
        }

        subdcache.myEvalInitialized = true;
        subdcache.myPatchCoordsUploaded = false;
    }

    // Upload patch coords if needed
    if (!subdcache.myPatchCoordsUploaded)
    {
        const UT_Span<const int> facespan(
            subdcache.myPatchFace.array(),
            subdcache.myPatchFace.size());
        const UT_Span<const float> uspan(
            subdcache.myPatchU.array(),
            subdcache.myPatchU.size());
        const UT_Span<const float> vspan(
            subdcache.myPatchV.array(),
            subdcache.myPatchV.size());

        if (!subdcache.myRestEval->setPatchCoords(facespan, uspan, vspan)
            || !subdcache.myAnimEval->setPatchCoords(
                facespan, uspan, vspan))
        {
            UT_ErrorLog::error(
                "HairDeform: OSD patch coord upload failed");
            return false;
        }
        subdcache.myPatchCoordsUploaded = true;
    }

    // Release skin mesh accessor before GPU work — only topology and
    // evaluators are needed from here.
    skinacc.release();

    // Everything below is device-resident until the caller has applied
    // ce_xform, so this is where a groom starts costing GPU memory.
    if (lockscope)
        lockscope->lock();

    // Upload coarse P to GPU and evaluate limit surface
    CE_FloatArray ce_restcoarseP;
    CE_FloatArray ce_animcoarseP;
    ce_restcoarseP.initFromData(
            (const float *)vt_skinrestpos.cdata(),
            3 * vt_skinrestpos.size());
    ce_animcoarseP.initFromData(
            (const float *)vt_skinanimpos.cdata(),
            3 * vt_skinanimpos.size());

    // Output buffers for limit surface evaluation
    CE_FloatArray ce_restP, ce_restDu, ce_restDv;
    CE_FloatArray ce_animP, ce_animDu, ce_animDv;
    ce_restP.init(ncurves * 3);
    ce_restDu.init(ncurves * 3);
    ce_restDv.init(ncurves * 3);
    ce_animP.init(ncurves * 3);
    ce_animDu.init(ncurves * 3);
    ce_animDv.init(ncurves * 3);

    {
        utZoneScopedN("subd_cl_eval");
        cl::Buffer restout = ce_restP.buffer();
        cl::Buffer restduout = ce_restDu.buffer();
        cl::Buffer restdvout = ce_restDv.buffer();
        cl::Buffer animout = ce_animP.buffer();
        cl::Buffer animduout = ce_animDu.buffer();
        cl::Buffer animdvout = ce_animDv.buffer();

        bool restok = subdcache.myRestEval->evaluate(
                ce_restcoarseP.buffer(), restout,
                &restduout, &restdvout);
        bool animok = subdcache.myAnimEval->evaluate(
                ce_animcoarseP.buffer(), animout,
                &animduout, &animdvout);

        if (!restok || !animok)
        {
            UT_ErrorLog::error(
                "HairDeform: OSD limit surface evaluation failed");
            return false;
        }
    }

    // Compute per-curve 4x4 transforms from limit P and derivatives
    {
        utZoneScopedN("subd_compute_xform");
        bool recompile = false;
        GU_SurfaceDeform::computeXform(
                context, recompile, ncurves,
                ce_xform,
                ce_restP.buffer(),
                ce_restDu.buffer(),
                ce_restDv.buffer(),
                ce_animP.buffer(),
                ce_animDu.buffer(),
                ce_animDv.buffer());
        context.finish();
    }

    return true;
}

bool
HD_HairDeformPointsDataSource::_ApplySubdSkinXforms(
        CE_Context &context,
        CE_FloatArray &ce_main_pos_out,
        const UT_IntArray &curveprimptsindex,
        exint npts,
        CE_FloatArray &ce_xform,
        CE_FloatArray *ce_mask,
        bool perpointxform,
        const HD_HairDeformBarbLayout *barbs)
{
    using namespace HD_HairDeformUtils;
    utZoneScopedN("subd_apply_skin_xforms");

    const int ncurves = curveprimptsindex.size();
    const bool hasbarbs = !perpointxform && barbs && barbs->hasBarbs();
    const exint nshaftpts = hasbarbs ? barbs->myNumShaftPts : npts;

    // Per-point xforms already map 1:1 to points, so skip indexedXform and
    // its two identity index buffers.
    if (perpointxform && !ce_mask)
    {
        utZoneScopedN("subd_xform");
        GU_SurfaceDeform::xform(
                context, /*recompile=*/false, npts,
                ce_main_pos_out.buffer(),
                ce_main_pos_out.buffer(),
                ce_xform.buffer());
        return true;
    }

    // Build point-to-xform mapping for indexedXform
    CE_Array<int> ce_pointPrimsIndex;
    CE_Array<int> ce_pointPrims;
    {
        UT_IntArray pointPrimsIndex(npts, npts);
        UT_IntArray pointPrims(npts, npts);

        if (perpointxform)
        {
            for (int p = 0; p < npts; ++p)
            {
                pointPrimsIndex[p] = p;
                pointPrims[p] = p;
            }
        }
        else
        {
            for (int c = 0; c < ncurves; ++c)
            {
                int start = curveprimptsindex[c];
                int end = (c + 1 < ncurves)
                    ? curveprimptsindex[c + 1]
                    : (int)nshaftpts;
                for (int p = start; p < end; ++p)
                {
                    pointPrimsIndex[p] = p;
                    pointPrims[p] = c;
                }
            }

            // Barbs take their shaft point's curve xform, so a feather moves
            // rigidly as one piece. Shaft entries above are already filled in.
            for (exint p = nshaftpts; p < npts; ++p)
            {
                pointPrimsIndex[p] = (int)p;
                pointPrims[p] = pointPrims[barbs->shaftPoint(p)];
            }
        }

        ce_pointPrimsIndex.initFromData(pointPrimsIndex.data(), npts);
        ce_pointPrims.initFromData(pointPrims.data(), npts);
    }

    // Apply per-curve transforms
    {
        utZoneScopedN("subd_indexed_xform");
        bool recompile = false;
        cl::Buffer maskbuffer;
        const cl::Buffer *maskptr = nullptr;
        if (ce_mask)
        {
            maskbuffer = ce_mask->buffer();
            maskptr = &maskbuffer;
        }

        GU_SurfaceDeform::indexedXform(
                context, recompile, npts,
                ce_main_pos_out.buffer(),
                ce_main_pos_out.buffer(),
                ce_xform.buffer(),
                ce_pointPrimsIndex.buffer(),
                ce_pointPrims.buffer(),
                /*grp=*/nullptr,
                maskptr);
    }

    return true;
}

bool
HD_HairDeformPointsDataSource::_ReadPointCapturePrimVars(
        const HdPrimvarsSchema &groompvs,
        UT_Optional<PointCapturePrimVars> &pcaptpvs)
{
    utZoneScopedN("read_point_capture_primvars");
    using namespace HD_HairDeformUtils;

    auto vt_cpts = getConstPvVal<int>(groompvs, _tokens->captPoints, 0.0f);
    auto vt_cptslengths = getConstPvVal<int>(groompvs, _tokens->captPointLengths, 0.0f);
    auto vt_cweights = getConstPvVal<float>(groompvs, _tokens->captWeights, 0.0f);
    auto vt_cweightslengths = getConstPvVal<int>(groompvs, _tokens->captWeightLengths, 0.0f);

    bool valid = true;
    valid &= checkHandle(vt_cpts, _tokens->captPoints, "groom", false);
    valid &= checkHandle(vt_cptslengths, _tokens->captPointLengths, "groom", false);
    valid &= checkHandle(vt_cweights, _tokens->captWeights, "groom", false);
    valid &= checkHandle(vt_cweightslengths, _tokens->captWeightLengths, "groom", false);

    if (!valid)
        return false;

    // Validate that pts and weights have matching structure
    if (vt_cptslengths->size() != vt_cweightslengths->size())
    {
        UT_ErrorLog::error(
                "HairDeform: Capture lengths size mismatch: "
                "pCaptPts:lengths={}, pCaptWeights:lengths={}",
                vt_cptslengths->size(), vt_cweightslengths->size());
        return false;
    }

    for (size_t i = 0; i < vt_cptslengths->size(); ++i)
    {
        if ((*vt_cptslengths)[i] != (*vt_cweightslengths)[i])
        {
            UT_ErrorLog::error(
                    "HairDeform: Capture lengths mismatch at {}: "
                    "pCaptPts:lengths={}, pCaptWeights:lengths={}",
                    i, (*vt_cptslengths)[i], (*vt_cweightslengths)[i]);
            return false;
        }
    }

    // Convert lengths to index (shared for pts and weights)
    UT_IntArray idx;
    indexFromLength(idx, vt_cptslengths->size(), *vt_cptslengths);
    pcaptpvs.emplace(PointCapturePrimVars{
            *vt_cpts, *vt_cweights, std::move(idx)});

    return true;
}

bool
HD_HairDeformPointsDataSource::_ComputePointCapture(
        const VtVec3fArray &vt_points,
        const VtVec3fArray &vt_deformerrestpos,
        HairDeformSchema &hairdeformschema)
{
    utZoneScopedN("compute_point_capture");
    using namespace HD_HairDeformUtils;

    bool smoothcapture = false;
    if (auto sc = hairdeformschema.GetSmoothCapture())
        smoothcapture = sc->GetTypedValue(0.0f);

    PointDeformCaptureCacheMapType::accessor captacc;
    bool captinserted = _pointdeformcapturecachemap->insert(
            captacc, UT_StringHolder(_primpath.GetText()));

    // The capture below only runs on insert, so an entry left empty by a
    // failure would keep failing after the bad input is fixed.
    bool captok = false;
    UT_SCOPE_EXIT
    {
        if (!captok)
            _pointdeformcapturecachemap->erase(captacc);
    };

    if (captinserted)
    {
        if (smoothcapture)
        {
            // Use GU_PointCapture for smooth capture
            HdMeshSchema defmesh = HdMeshSchema::GetFromParent(_deformerds);
            HdTetMeshSchema deftetmesh = HdTetMeshSchema::GetFromParent(_deformerds);
            if (defmesh)
            {
                HD_HairDeformUtils::cacheLog(
                        "HairDeform: Computing smooth capture (Mesh) for {}",
                        _primpath.GetText());
                if (!hdComputeSmoothCapture(
                        captacc->second.captStarts,
                        captacc->second.captPts,
                        captacc->second.captWeights,
                        vt_points,
                        defmesh,
                        vt_deformerrestpos,
                        hairdeformschema))
                {
                    return false;
                }
            }
            else if (deftetmesh)
            {
                HD_HairDeformUtils::cacheLog(
                        "HairDeform: Computing smooth capture (TetMesh) for {}",
                        _primpath.GetText());
                if (!hdComputeSmoothCapture(
                        captacc->second.captStarts,
                        captacc->second.captPts,
                        captacc->second.captWeights,
                        vt_points,
                        deftetmesh,
                        vt_deformerrestpos,
                        hairdeformschema))
                {
                    return false;
                }
            }
            else
            {
                UT_ErrorLog::error(
                        "HairDeform: Smooth capture requires Mesh or TetMesh deformer");
                return false;
            }
        }
        else
        {
            // Use BVH-based point capture
            HD_HairDeformUtils::cacheLog(
                    "HairDeform: Computing point capture via BVH for {}",
                    _primpath.GetText());

            float captureradius = 1.0f;
            int capturemaxpoints = 10;
            int captureminpoints = 1;

            if (auto cr = hairdeformschema.GetCaptureRadius())
                captureradius = cr->GetTypedValue(0.0f);
            if (auto cmp = hairdeformschema.GetCaptureMaxPoints())
                capturemaxpoints = cmp->GetTypedValue(0.0f);
            if (auto cminp = hairdeformschema.GetCaptureMinPoints())
                captureminpoints = cminp->GetTypedValue(0.0f);

            GUpointDeformComputeCapture(
                    captacc->second.captStarts,
                    captacc->second.captPts,
                    captacc->second.captWeights,
                    {(const UT_Vector3F *)vt_points.cdata(), vt_points.size()},
                    {(const UT_Vector3F *)vt_deformerrestpos.cdata(), vt_deformerrestpos.size()},
                    captureradius,
                    capturemaxpoints,
                    captureminpoints);

            if (captacc->second.captStarts.size() <= 1)
            {
                UT_ErrorLog::error(
                        "HairDeform: BVH point capture produced empty indices");
                return false;
            }
        }
    }

    // Check if cache has valid data
    captok = captacc->second.captStarts.size() > 1;
    return captok;
}

bool
HD_HairDeformPointsDataSource::_InitPointDeform(
        CE_Context &context,
        CE_FloatArray &ce_main_pos_out,
        PointDeformCEArrays &pointdeform_cearrays,
        GU_PointDeform::CachedItems &pointdeform_cacheditems,
        const VtVec3fArray &vt_deformerrestpos,
        const VtVec3fArray &vt_deformeranimpos,
        const UT_Span<const int> &captpts,
        const UT_Span<const float> &captweights,
        const UT_Span<const int> &captindex,
        exint npts,
        int ndeformerpts,
        bool useorientattrib,
        const VtArray<GfVec4f> *vt_deformerrestorient,
        const VtArray<GfVec4f> *vt_deformeranimorient,
        const VtArray<GfQuatf> *vt_deformerrestorient_quat,
        const VtArray<GfQuatf> *vt_deformeranimorient_quat,
        bool recompile)
{
    utZoneScopedN("init_point_deform");
    using namespace HD_HairDeformUtils;

    DeformerCacheMapType::const_accessor defacc;
    bool cache_found = _deformercachemap->find(
            defacc, UT_StringHolder(_deformerprimpath.GetText()));

    if (!cache_found)
    {
        UT_ErrorLog::error("HairDeform: Deformer CACHING failed");
        return false;
    }

    hdPointDeformInitCEArrays(
            pointdeform_cearrays, context,
            defacc->second.myNeighbourindex,
            defacc->second.myNeighbours,
            vt_deformerrestpos,
            vt_deformeranimpos,
            captpts,
            captweights,
            captindex,
            useorientattrib,
            vt_deformerrestorient,
            vt_deformeranimorient,
            vt_deformerrestorient_quat,
            vt_deformeranimorient_quat);

    hdPointDeformSetExternals(
            pointdeform_cearrays, pointdeform_cacheditems, useorientattrib);

    pointdeform_cacheditems.getBuffers().myPos.setExternal(
            ce_main_pos_out.buffer());

    if (!hdComputeTransforms(
                pointdeform_cacheditems, context, npts, ndeformerpts,
                useorientattrib, recompile))
        return false;

    return true;
}

bool
HD_HairDeformPointsDataSource::_InitSkinIdBuckets(
        const UT_StringHolder &captureidattrib)
{
    utZoneScopedN("ensure_skin_id_buckets");

    // The common case is a hit, and every groom bound to this skin comes
    // through here -- probe with a shared accessor so they don't serialise.
    {
        SkinMeshCacheMapType::const_accessor probe;
        if (_skinmeshcachemap->find(
                    probe, UT_StringHolder(_skinprimpath.GetText()))
            && probe->second.myIdBuckets.find(captureidattrib)
                    != probe->second.myIdBuckets.end())
            return true;
    }

    // A write accessor, because the buckets are shared by every groom bound to
    // this skin.  It is held across the build, so a groom arriving mid-build
    // waits for it -- which is the point, they would otherwise each build
    // their own copy of the same trees.  The build is isolated because it
    // runs in parallel: stealing another groom's task that wants this skin
    // would block this thread on its own accessor.
    SkinMeshCacheMapType::accessor skinacc;
    if (!_skinmeshcachemap->find(
                skinacc, UT_StringHolder(_skinprimpath.GetText()))
        || !skinacc->second.myGdp)
    {
        UT_ErrorLog::error("HairDeform: Skin mesh cache not found");
        return false;
    }

    auto &sd = skinacc->second;
    if (sd.myIdBuckets.find(captureidattrib) != sd.myIdBuckets.end())
        return true;

    HD_IdPrimVar ids;
    if (!hdReadIdPrimVar(_skinds, captureidattrib, "polygon", ids))
    {
        // hdReadIdPrimVar says why when the primvar is there but unusable.
        UT_ErrorLog::error(
                "HairDeform: Skin '{}' has no usable '{}' primvar to match "
                "capture ids against",
                _skinprimpath.GetText(), captureidattrib);
        return false;
    }

    HD_HairDeformIdBuckets buckets;
    bool built = false;
    UTisolate([&]()
    {
        built = hdIdBucketsFromSkinPrimVar(
                buckets, *sd.myGdp, ids, captureidattrib);
    });
    if (!built)
        return false;

    HD_HairDeformUtils::cacheLog(
            "HairDeform: built {} '{}' capture id buckets for skin '{}'",
            buckets.myTrees.size(), captureidattrib,
            _skinprimpath.GetText());

    sd.myIdBuckets.emplace(captureidattrib, std::move(buckets));
    return true;
}

bool
HD_HairDeformPointsDataSource::_BucketIndicesFromCaptureIds(
        UT_Array<int> &bucketindices,
        const HD_HairDeformIdBuckets &buckets,
        const UT_StringHolder &captureidattrib,
        bool perpoint,
        const UT_IntArray &curveprimptsindex,
        const HD_HairDeformRestPointsCache &restpoints)
{
    utZoneScopedN("bucket_indices_from_capture_ids");

    HD_IdPrimVar ids;
    if (!hdReadIdPrimVar(_primds, captureidattrib, "curve", ids))
    {
        // hdReadIdPrimVar says why when the primvar is there but unusable.
        UT_ErrorLog::error(
                "HairDeform: Groom curves have no usable '{}' primvar to "
                "match against the skin", captureidattrib);
        return false;
    }

    const exint ncurves = curveprimptsindex.size() - 1;
    if (!perpoint)
        return hdBucketIndicesFromCurveIds(
                bucketindices, buckets, ids, ncurves, captureidattrib);

    // Per-point capture wants one entry per expanded point, and the id still
    // lives on the curve.
    UT_Array<int> curvebucketindices;
    if (!hdBucketIndicesFromCurveIds(
                curvebucketindices, buckets, ids, ncurves, captureidattrib))
        return false;

    return hdPointBucketIndicesFromCurves(
            bucketindices, curvebucketindices, curveprimptsindex,
            restpoints.myBarbLayout, restpoints.myPoints.size());
}

bool
HD_HairDeformPointsDataSource::_InitCurveSkinCapture(
        CE_Int32Array &ce_skinptstarts,
        CE_Int32Array &ce_skinptindices,
        CE_FloatArray &ce_skinptweights,
        const HD_HairDeformSkinMeshCache &skincache,
        const UT_IntArray &curveprimptsindex,
        const HD_HairDeformRestPointsCache &restpoints,
        const UT_StringHolder &captureidattrib)
{
    utZoneScopedN("init_curve_skin_capture");

    const exint ncurves = curveprimptsindex.size() - 1;

    CurveSkinCaptureCacheMapType::accessor surfacc;
    bool inserted = _maincurveskincapturecachemap->insert(
            surfacc, UT_StringHolder(_primpath.GetText()));

    // The capture arrays are sized for the curve count they were taken at, so
    // a hit at a different count has to recapture -- the xform kernel reads
    // one start pair per curve and would run off the end.
    if (!inserted && surfacc->second.myPrimPtStarts.size() != ncurves + 1)
    {
        HD_HairDeformUtils::cacheLog(
                "HairDeform: curve skin capture count changed - recapturing");
        inserted = true;
    }

    if (inserted)
    {
        // Only now that the capture is going to run is it worth resolving the
        // groom's ids.
        const HD_HairDeformIdBuckets *idbuckets = nullptr;
        UT_Array<int> bucketindices;
        if (captureidattrib.isstring())
        {
            idbuckets = hdFindIdBuckets(skincache, captureidattrib);
            if (!idbuckets)
            {
                UT_ErrorLog::error(
                        "HairDeform: Capture id buckets missing for skin '{}'",
                        _skinprimpath.GetText());
                _maincurveskincapturecachemap->erase(surfacc);
                return false;
            }

            if (!_BucketIndicesFromCaptureIds(
                        bucketindices, *idbuckets, captureidattrib,
                        /*perpoint*/ false, curveprimptsindex, restpoints))
            {
                _maincurveskincapturecachemap->erase(surfacc);
                return false;
            }
        }

        if (!hdComputeCurveSkinCaptureToCache(
                    surfacc->second, *skincache.myGdp,
                    *skincache.myRayIntersect, restpoints.myPoints,
                    curveprimptsindex, idbuckets,
                    idbuckets ? &bucketindices : nullptr))
        {
            UT_ErrorLog::error(
                    "HairDeform: Groom curve skin capture failed, skipping "
                    "deformation");
            // Capture only runs on insert, so an entry left half filled would
            // be uploaded on every later cook.
            _maincurveskincapturecachemap->erase(surfacc);
            return false;
        }
    }

    hdUploadCurveSkinCapture(
            ce_skinptstarts, ce_skinptindices, ce_skinptweights,
            surfacc->second);
    return true;
}

bool
HD_HairDeformPointsDataSource::_InitSurfaceTopo(
        const UT_IntArray &curveprimptsindex,
        const HD_HairDeformRestPointsCache &restpoints,
        const UT_StringHolder &captureidattrib)
{
    utZoneScopedN("init_surface_topo");
    SurfaceTopoCacheMapType::accessor surfacc;
    bool surfinserted = _surfacetopocachemap->insert(
            surfacc, UT_StringHolder(_primpath.GetText()));

    // The capture arrays are sized for the point count they were taken at, so
    // a change in the expanded count -- a different barb count, or a switch to
    // or from per-point capture -- has to recapture.
    if (!surfinserted
        && surfacc->second.myPrimPtStarts.size()
                != (exint)restpoints.myPoints.size() + 1)
    {
        HD_HairDeformUtils::cacheLog(
                "HairDeform: surface capture count changed - recapturing");
        surfinserted = true;
    }

    if (!surfinserted)
    {
        HD_HairDeformUtils::cacheLog("HairDeform: USING CACHED surface deform capture");
        return true;
    }

    // Cache miss - compute surface capture
    SkinMeshCacheMapType::const_accessor skinacc;
    if (!_skinmeshcachemap->find(
            skinacc, UT_StringHolder(_skinprimpath.GetText()))
        || !skinacc->second.myGdp || !skinacc->second.myRayIntersect)
    {
        UT_ErrorLog::error("HairDeform: Skin mesh caching failed");
        // Don't leave the empty entry behind: the branch above reports it
        // as a valid cached capture on the next cook.
        _surfacetopocachemap->erase(surfacc);
        return false;
    }

    // Only now that the capture is actually going to run is it worth
    // resolving the groom's ids.
    const HD_HairDeformIdBuckets *idbuckets = nullptr;
    UT_Array<int> bucketindices;
    if (captureidattrib.isstring())
    {
        idbuckets = hdFindIdBuckets(skinacc->second, captureidattrib);
        if (!idbuckets)
        {
            UT_ErrorLog::error(
                    "HairDeform: Capture id buckets missing for skin '{}'",
                    _skinprimpath.GetText());
            _surfacetopocachemap->erase(surfacc);
            return false;
        }

        if (!_BucketIndicesFromCaptureIds(
                    bucketindices, *idbuckets, captureidattrib,
                    /*perpoint*/ true, curveprimptsindex, restpoints))
        {
            _surfacetopocachemap->erase(surfacc);
            return false;
        }
    }

    const UT_Span<const UT_Vector3F> pointspan(
            (const UT_Vector3F *)restpoints.myPoints.cdata(),
            restpoints.myPoints.size());
    HD_HairDeformUtils::cacheLog("HairDeform: Caching surface deform capture");
    const GU_Detail &skingdp = *skinacc->second.myGdp;
    if (!hdSurfaceInterpOffsetsFromIdBuckets(
                surfacc->second.myPrimPtStarts,
                surfacc->second.myPtIndices,
                surfacc->second.myPtWeights,
                pointspan,
                skingdp,
                *skinacc->second.myRayIntersect,
                idbuckets,
                idbuckets ? &bucketindices : nullptr))
    {
        UT_ErrorLog::error(
                "HairDeform: Per-point surface capture failed, disabling "
                "deformation");
        _surfacetopocachemap->erase(surfacc);
        return false;
    }

    return true;
}

// Bounds of the deformed points, scanned once while they are still cache hot
// so the extent data sources only have to read a corner.
static GfRange3d
hdPointsExtent(const VtVec3fArray &pts)
{
    utZoneScopedN("points_extent");
    if (pts.empty())
        return GfRange3d();

    class hdBBoxTask
    {
    public:
        hdBBoxTask(const GfVec3f *pts)
            : myPts(pts)
        {
            myBox.makeInvalid();
        }
        hdBBoxTask(hdBBoxTask &src, UT_Split)
            : myPts(src.myPts)
        {
            myBox.makeInvalid();
        }
        void operator()(const UT_BlockedRange<exint> &r)
        {
            for (exint i = r.begin(), end = r.end(); i < end; ++i)
                myBox.enlargeBounds(myPts[i].data());
        }
        void join(const hdBBoxTask &src) { myBox.enlargeBounds(src.myBox); }

        const UT_BoundingBoxF &box() const { return myBox; }

    private:
        const GfVec3f *myPts;
        UT_BoundingBoxF myBox;
    };

    hdBBoxTask task(pts.cdata());
    UTparallelReduceLightItems(
            UT_BlockedRange<exint>(0, pts.size()), task);

    const UT_BoundingBoxF &box = task.box();
    return GfRange3d(
            GfVec3d(box.xmin(), box.ymin(), box.zmin()),
            GfVec3d(box.xmax(), box.ymax(), box.zmax()));
}

VtValue
HD_HairDeformPointsDataSource::GetValue(const Time shutterOffset)
{
    return VtValue(GetTypedValue(shutterOffset));
}

// The accessor only hands out the entry; the compute runs under its
// myExclusive, so a second puller of the same offset -- extent min and max,
// or the primvars source -- helps with it and reuses the result.  An accessor
// held across the compute would deadlock a thread that steals that second
// pull while waiting on _ComputePoints()'s own parallel work.
//
// _ComputePoints() must not pull this data source: a self-pull at the same
// offset re-enters myExclusive and deadlocks.
HD_HairDeformPointsDataSource::CachedPoints
HD_HairDeformPointsDataSource::_CachedPointsFor(const Time shutterOffset)
{
    UT_SharedPtr<CachedPointsEntry> entry;
    {
        PointsCacheMap::accessor acc;
        if (_cachedResult.insert(acc, shutterOffset))
            acc->second = UTmakeShared<CachedPointsEntry>();
        entry = acc->second;
    }

    CachedPointsCompute compute{*this, *entry, shutterOffset};
    entry->myExclusive.execute(compute);
    if (!compute.myComputed)
        HD_HairDeformUtils::cacheLog(
                "HairDeform: PointsDataSource HIT '{}' offset {}",
                _primpath.GetText(), shutterOffset);
    return entry->myResult;
}

void
HD_HairDeformPointsDataSource::CachedPointsCompute::operator()()
{
    HD_HairDeformUtils::cacheLog(
            "HairDeform: PointsDataSource COMPUTE '{}' offset {}",
            mySource._primpath.GetText(), myShutterOffset);
    myEntry.myResult.myPoints = mySource._ComputePoints(myShutterOffset);
    myEntry.myResult.myExtent = hdPointsExtent(myEntry.myResult.myPoints);
    myComputed = true;
}

VtVec3fArray
HD_HairDeformPointsDataSource::GetTypedValue(const Time shutterOffset)
{
    utZoneScoped;
    UT_StringHolder zonetext;
    zonetext.format("points: {}", shutterOffset);
    utZoneTextSH(UT_StringHolder(zonetext.buffer()));

    return _CachedPointsFor(shutterOffset).myPoints;
}

GfRange3d
HD_HairDeformPointsDataSource::GetExtent(const Time shutterOffset)
{
    utZoneScoped;
    UT_StringHolder zonetext;
    zonetext.format("extent: {}", shutterOffset);
    utZoneTextSH(UT_StringHolder(zonetext.buffer()));

    return _CachedPointsFor(shutterOffset).myExtent;
}

VtVec3fArray
HD_HairDeformPointsDataSource::_ComputePoints(const Time shutterOffset)
{
    utZoneScoped;

    using namespace HD_HairDeformUtils;

    // settings to be exposed later
    DeformMethod deformmethod = DeformMethod::NONE;

    bool recompile = false;
    bool useorientattrib = false;
    bool perpointcapture = false;
    UT_StringHolder captureidattrib;
    bool preserveshapeenable = false;
    int preserveshapeiterations = 0;
    bool preserveshapelockroots = true;
    float preserveshapekstretch = 0.01f;
    float preserveshapekbend = 0.001f;
    float preserveshaperefposstrength = 0.0f;
    float preserveclumpsstiffness = 0.0f;
    float preserveclumpsdamping = 1.0f;
    bool preserveclumpsenable = false;
    int preserveclumpsmaxneighbors = 50;
    int preserveclumpsmaxconstraints = 3;

    std::string deformmethodstr;

    HairDeformSchema hairdeformschema
            = HairDeformSchema::GetFromParent(_primds);
    if (hairdeformschema)
    {
        if (auto value = hairdeformschema.GetPreserveShapeEnable())
            preserveshapeenable = value->GetTypedValue(0.0f);
        if (auto value = hairdeformschema.GetPreserveShapeIterations())
            preserveshapeiterations = value->GetTypedValue(0.0f);
        if (auto value = hairdeformschema.GetPreserveShapeLockRoots())
            preserveshapelockroots = value->GetTypedValue(0.0f);

        if (auto value = hairdeformschema.GetPreserveShapeKStretch())
            preserveshapekstretch = value->GetTypedValue(0.0f);
        if (auto value = hairdeformschema.GetPreserveShapeKBend())
            preserveshapekbend = value->GetTypedValue(0.0f);
        if (auto value = hairdeformschema.GetPreserveShapeRefPosStrength())
            preserveshaperefposstrength = value->GetTypedValue(0.0f);
        if (auto value = hairdeformschema.GetPreserveClumpsStiffness())
            preserveclumpsstiffness = value->GetTypedValue(0.0f);
        if (auto value = hairdeformschema.GetPreserveClumpsDamping())
            preserveclumpsdamping = value->GetTypedValue(0.0f);
        if (auto value = hairdeformschema.GetPreserveClumpsEnable())
            preserveclumpsenable = value->GetTypedValue(0.0f);
        if (auto value = hairdeformschema.GetPreserveClumpsMaxNeighbors())
            preserveclumpsmaxneighbors = value->GetTypedValue(0.0f);
        if (auto value = hairdeformschema.GetPreserveClumpsMaxConstraints())
            preserveclumpsmaxconstraints = value->GetTypedValue(0.0f);


        if (auto value = hairdeformschema.GetUseOrientAttrib())
            useorientattrib = value->GetTypedValue(0.0f);

        if (auto value = hairdeformschema.GetPerPointCapture())
            perpointcapture = value->GetTypedValue(0.0f);

        if (auto value = hairdeformschema.GetCaptureIdAttrib())
            captureidattrib = UT_StringHolder(value->GetTypedValue(0.0f));

        if (auto value = hairdeformschema.GetDeformMethod())
            deformmethodstr = value->GetTypedValue(0.0f);

        if (deformmethodstr == HairDeformSchemaTokens->pointdeform)
            deformmethod = DeformMethod::POINTDEFORM;
        else if (deformmethodstr == HairDeformSchemaTokens->surfacedeform)
            deformmethod = DeformMethod::SURFACEDEFORM;
        else if (deformmethodstr == HairDeformSchemaTokens->guideinterpolationmesh)
            deformmethod = DeformMethod::GUIDEINTERPOLATIONMESH;
        else if (deformmethodstr == HairDeformSchemaTokens->guideweights)
            deformmethod = DeformMethod::GUIDEWEIGHTS;
        else if (deformmethodstr == HairDeformSchemaTokens->guideshapeinterpolation)
            deformmethod = DeformMethod::GUIDESHAPEINTERPOLATION;
    }

    GU_GuideCaptureParms gsiParms;
    if (hairdeformschema)
    {
        if (auto v = hairdeformschema.GetGsiMaxCandidates())
            gsiParms.max_candidates = v->GetTypedValue(0.0f);
        if (auto v = hairdeformschema.GetGsiSearchRadius())
            gsiParms.search_radius = v->GetTypedValue(0.0f);
        if (auto v = hairdeformschema.GetGsiNSamples())
            gsiParms.nsamples = v->GetTypedValue(0.0f);
        if (auto v = hairdeformschema.GetGsiMinGuides())
            gsiParms.min_guides = v->GetTypedValue(0.0f);
        if (auto v = hairdeformschema.GetGsiMaxGuides())
            gsiParms.max_guides = v->GetTypedValue(0.0f);
        if (auto v = hairdeformschema.GetGsiSigmaScale())
            gsiParms.sigma_scale = v->GetTypedValue(0.0f);
        if (auto v = hairdeformschema.GetGsiWeightThreshold())
            gsiParms.weight_threshold = v->GetTypedValue(0.0f);
        if (auto v = hairdeformschema.GetGsiLengthPenaltyScale())
            gsiParms.length_penalty_scale = v->GetTypedValue(0.0f);
    }

    // An empty deformmethod string means deformation was intentionally disabled
    // (the LOP Python writes "" when deformenable is off). A non-empty but
    // unrecognized string means a misconfigured node.
    const bool deformenable = !deformmethodstr.empty();

    if (deformmethod == DeformMethod::NONE && deformenable)
        UT_ErrorLog::error("HairDeform: No valid deform method specified");

    if (deformmethod == DeformMethod::POINTDEFORM)
    {
        HdMeshSchema defmesh
                = HdMeshSchema::GetFromParent(_deformerds);
        HdTetMeshSchema deftetmesh
                = HdTetMeshSchema::GetFromParent(_deformerds);
        HdBasisCurvesSchema defcurves
                = HdBasisCurvesSchema::GetFromParent(_deformerds);

        if (!defmesh && !deftetmesh && !defcurves)
        {
            UT_ErrorLog::error(
                    "HairDeform: Point Deform requires a "
                    "Mesh, TetMesh, or BasisCurves deformer.");
            deformmethod = DeformMethod::NONE;
        }
    }

    // get skin primvars (for surface deform and guide interpolation mesh)
    HdPrimvarsSchema skinpvs = HdPrimvarsSchema::GetFromParent(_skinds);
    HdMeshSchema skinmesh = HdMeshSchema::GetFromParent(_skinds);
    if ((deformmethod == DeformMethod::SURFACEDEFORM
         || deformmethod == DeformMethod::GUIDEINTERPOLATIONMESH
         || deformmethod == DeformMethod::GUIDEWEIGHTS
         || deformmethod == DeformMethod::GUIDESHAPEINTERPOLATION) && !skinmesh)
    {
        UT_ErrorLog::error(
                "HairDeform: Surface Deform, Guide Interpolation Mesh, "
                "Guide Weights, or Guide Shape Interpolation method selected "
                "but skin is not a Mesh. Deformation disabled.");
        deformmethod = DeformMethod::NONE;
    }

    // If point deform has a skin mesh relationship, use the combined path
    // (surface deform for roots, point deform for non-roots).
    bool pointdeform_has_skin
        = deformmethod == DeformMethod::POINTDEFORM && skinmesh;

    // get deformer primvars (guides/deformer curves)
    HdPrimvarsSchema deformerpvs = HdPrimvarsSchema::GetFromParent(_deformerds);
    if (deformmethod == DeformMethod::POINTDEFORM && !deformerpvs)
    {
        // no deformer, so don't deform
        // better not to fail entirely, as the groom will just disappear
        // on users then.
        UT_ErrorLog::error("HairDeform: Deformer has no primvars");
        deformmethod = DeformMethod::NONE;
    }

    // get groom primvars
    HdPrimvarsSchema groompvs = HdPrimvarsSchema::GetFromParent(_primds);
    if (!groompvs)
    {
        UT_ErrorLog::error("HairDeform: Primvars schema missing on groom");
        return VtVec3fArray();
    }

    UT_Optional<PointCapturePrimVars> pcaptpvs;

    // Read groom points (used for POINTDEFORM capture and rest points)
    auto vt_points = getConstPvVal<GfVec3f>(groompvs, HdTokens->points, 0.0f);
    if (!checkHandle(vt_points, HdTokens->points, "groom"))
        return VtVec3fArray();

    // Rest primvar on the groom — required when deformation is disabled so
    // the Cosserat solve can use rest-pose positions for init while solving
    // on the animated points authored by the user's own deformation.
    auto vt_groomrestpos = getConstPvVal<GfVec3f>(groompvs, _tokens->rest, 0.0f);

    // Only evaluate animated position at shutterOffset, everything else at
    // 0.0f. GetContributingSampleTimesForInterval() reports the union of
    // skin's and deformer's point and transform sample times, so shutterOffset
    // is one of those.

    // Read deformer positions (used for POINTDEFORM, GUIDEINTERPOLATIONMESH, and GUIDEWEIGHTS)
    auto vt_deformerrestpos = getConstPvVal<GfVec3f>(deformerpvs, _tokens->rest, 0.0f);
    auto vt_deformeranimpos = getAnimPoints(deformerpvs, _deformerprimpath,
            shutterOffset, _resolvedpointscachemap.get());

    // Read skin positions (used for SURFACEDEFORM, GUIDEINTERPOLATIONMESH, GUIDEWEIGHTS)
    auto vt_skinrestpos = getConstPvVal<GfVec3f>(skinpvs, _tokens->rest, 0.0f);
    auto vt_skinanimpos = getAnimPoints(skinpvs, _skinprimpath,
            shutterOffset, _resolvedpointscachemap.get());

    if (deformmethod != DeformMethod::NONE)
    {
        utZoneScopedN("deformer_caches");

        if (deformmethod == DeformMethod::SURFACEDEFORM
            || deformmethod == DeformMethod::GUIDEINTERPOLATIONMESH
            || deformmethod == DeformMethod::GUIDEWEIGHTS
            || deformmethod == DeformMethod::GUIDESHAPEINTERPOLATION
            || pointdeform_has_skin)
        {
            bool valid = true;
            valid &= checkHandle(vt_skinrestpos, _tokens->rest, "skin");
            valid &= checkHandle(vt_skinanimpos, HdTokens->points, "skin");
            if (!valid)
                return *vt_points;
        }

        if (deformmethod != DeformMethod::SURFACEDEFORM)
        {
            bool valid = true;
            valid &= checkHandle(vt_deformerrestpos, _tokens->rest, "deformer");
            valid &= checkHandle(vt_deformeranimpos, HdTokens->points, "deformer");
            if (!valid)
                return *vt_points;
        }
    }

    if (!deformenable && preserveshapeenable)
    {
        if (!checkHandle(vt_groomrestpos, _tokens->rest, "groom"))
        {
            UT_ErrorLog::error(
                    "HairDeform: Groom 'rest' primvar required when "
                    "deformation is disabled and Preserve Shape is on.");
            return *vt_points;
        }
    }

    // Read deformer orient primvars when useorientattrib is enabled.
    // A primvar may hold GfVec4f or GfQuatf depending on how it was
    // authored and composed.  Read the raw VtValue once and accept
    // whichever type it holds.
    UT_Optional<const VtArray<GfVec4f>> vt_deformerrestorient;
    UT_Optional<const VtArray<GfVec4f>> vt_deformeranimorient;
    UT_Optional<const VtArray<GfQuatf>> vt_deformerrestorient_quat;
    UT_Optional<const VtArray<GfQuatf>> vt_deformeranimorient_quat;
    if (useorientattrib && deformerpvs)
    {
        // A primvar may hold GfVec4f or GfQuatf depending on how it
        // was authored and composed.  Read the raw VtValue once and
        // accept whichever type it holds.
        auto readOrient = [&](const TfToken &token, Time time,
                UT_Optional<const VtArray<GfVec4f>> &out_vec4,
                UT_Optional<const VtArray<GfQuatf>> &out_quat)
        {
            HdPrimvarSchema pv = deformerpvs.GetPrimvar(token);
            if (!pv.IsDefined())
                return;
            HdSampledDataSourceHandle ds = pv.GetFlattenedPrimvarValue();
            if (!ds)
                return;
            VtValue val = ds->GetValue(time);
            if (val.IsHolding<VtArray<GfVec4f>>())
                out_vec4.emplace(val.Get<VtArray<GfVec4f>>());
            else if (val.IsHolding<VtArray<GfQuatf>>())
                out_quat.emplace(val.Get<VtArray<GfQuatf>>());
        };
        readOrient(_tokens->restorient, 0.0f,
                vt_deformerrestorient, vt_deformerrestorient_quat);
        readOrient(_tokens->orient, shutterOffset,
                vt_deformeranimorient, vt_deformeranimorient_quat);
    }


    if (deformmethod == DeformMethod::POINTDEFORM && useorientattrib)
    {
        const bool restorient_available
                = vt_deformerrestorient.has_value()
                || vt_deformerrestorient_quat.has_value();
        const bool orient_available
                = vt_deformeranimorient.has_value()
                || vt_deformeranimorient_quat.has_value();

        if (!restorient_available)
        {
            UT_ErrorLog::error(
                    "HairDeform: useorientattrib enabled but required "
                    "restorient primvar missing.");
        }
        if (!orient_available)
        {
            UT_ErrorLog::error(
                    "HairDeform: useorientattrib enabled but required orient "
                    "primvar missing.");
        }

        if (!restorient_available || !orient_available)
        {
            return *vt_points;
        }

        const exint restorient_count = vt_deformerrestorient
                ? vt_deformerrestorient->size()
                : vt_deformerrestorient_quat->size();
        const exint orient_count = vt_deformeranimorient
                ? vt_deformeranimorient->size()
                : vt_deformeranimorient_quat->size();
        const exint deformerrestpos_count = vt_deformerrestpos->size();
        const exint deformeranimpos_count = vt_deformeranimpos->size();
        if (restorient_count != orient_count
            || restorient_count != deformerrestpos_count
            || orient_count != deformeranimpos_count)
        {
            UT_ErrorLog::error(
                    "HairDeform: element count mismatch: "
                    "deformerrestpos={}, deformeranimpos={}, "
                    "restorient={}, orient={}",
                    deformerrestpos_count, deformeranimpos_count,
                    restorient_count, orient_count);
            return *vt_points;
        }
    }

    // Apply prim transforms to animated positions (rest positions stay in local space)
    VtVec3fArray deformeranimpos_xformed;
    VtVec3fArray skinanimpos_xformed;
    {
        utZoneScopedN("transform_anim_positions");
        if (vt_deformeranimpos)
        {
            deformeranimpos_xformed = *vt_deformeranimpos;
            hdTransformPositions(deformeranimpos_xformed, _deformerds, shutterOffset);
        }
        if (vt_skinanimpos)
        {
            skinanimpos_xformed = *vt_skinanimpos;
            hdTransformPositions(skinanimpos_xformed, _skinds, shutterOffset);
        }
    }

    bool use_pcaptpvs = false;

    if (deformmethod != DeformMethod::NONE)
    {
        utZoneScopedN("deformer_caches");

        if (deformmethod == DeformMethod::POINTDEFORM)
        {
            use_pcaptpvs = _ReadPointCapturePrimVars(groompvs, pcaptpvs);
            if (!use_pcaptpvs)
            {
                if (!_ComputePointCapture(
                        *vt_points, *vt_deformerrestpos, hairdeformschema))
                {
                    UT_ErrorLog::error(
                            "HairDeform: Point Capture attributes missing and "
                            "could not compute, disabling deformation");
                    deformmethod = DeformMethod::NONE;
                }
            }

            DeformerCacheMapType::accessor defacc;
            bool definserted = _deformercachemap->insert(
                    defacc, UT_StringHolder(_deformerprimpath.GetText()));
            if (definserted)
                HD_HairDeformUtils::cacheLog("HairDeform: CACHE MISS deformer cache");
            else
                HD_HairDeformUtils::cacheLog("HairDeform: CACHE HIT deformer cache");

            const bool need_neighbours = !useorientattrib;
            const bool missing_neighbours
                    = defacc->second.myNeighbourindex.size() == 0;
            if (need_neighbours && (definserted || missing_neighbours))
            {
                if (!hdFindNeighbours(
                            _deformerds, vt_deformerrestpos->size(),
                            defacc->second.myNeighbours,
                            defacc->second.myNeighbourindex))
                {
                    deformmethod = DeformMethod::NONE;
                }
            }
        }

        if (deformmethod == DeformMethod::SURFACEDEFORM
            || deformmethod == DeformMethod::GUIDEINTERPOLATIONMESH
            || deformmethod == DeformMethod::GUIDEWEIGHTS
            || deformmethod == DeformMethod::GUIDESHAPEINTERPOLATION
            || pointdeform_has_skin)
        {
            SkinMeshCacheMapType::accessor skinacc;
            bool skininserted = _skinmeshcachemap->insert(
                    skinacc, UT_StringHolder(_skinprimpath.GetText()));
            if (skininserted)
                HD_HairDeformUtils::cacheLog("HairDeform: CACHE MISS skin mesh cache");
            else
                HD_HairDeformUtils::cacheLog("HairDeform: CACHE HIT skin mesh cache");
            if (skininserted)
            {
                auto &sd = skinacc->second;
                HdMeshSchema mesh = HdMeshSchema::GetFromParent(_skinds);
                if (mesh && vt_skinrestpos && vt_skinanimpos)
                {
                    GU_Detail &gdp = sd.myGdp.emplace();
                    if (!hdDetailFromMesh(gdp, mesh, *vt_skinrestpos))
                    {
                        UT_ErrorLog::error(
                            "HairDeform: skin prim has no geometry");
                        sd.myGdp.reset();
                    }
                    else
                    {
                        // Check subdivision scheme on the skin mesh
                        auto schemeDs = mesh.GetSubdivisionScheme();
                        bool isSubd = schemeDs
                            && schemeDs->GetTypedValue(0.f) != TfToken("none");

                        if (isSubd)
                        {
                            // Build OSD topology for subd meshes
                            sd.mySubdTopology = UTmakeUnique<GU_OSDTopology>();
                            if (!sd.mySubdTopology->createFromDetail(gdp))
                            {
                                UT_ErrorLog::warning(
                                    "HairDeform: OSD topology build failed, "
                                    "falling back to poly path");
                                sd.mySubdTopology.reset();
                            }
                        }

                        if (!sd.mySubdTopology)
                        {
                            // Poly path: compute N/T frames
                            GU_SurfaceDeform::computeFrames(
                                gdp, gdp.getP(), sd.myNormal, sd.myTangent);
                        }

                        sd.myRayIntersect.emplace(&gdp);
                    }
                }

                // The cache only fills on insert, so an entry left empty
                // by a failed build would keep failing even after the
                // missing input is authored. Drop it and retry next cook.
                if (!sd.myGdp)
                    _skinmeshcachemap->erase(skinacc);
            }
        }
    }

    const auto &vt_curvevtxcounts = hdGetVertexCounts(_primds);
    if (!vt_curvevtxcounts.size())
        return *vt_points;

    UT_IntArray curveprimptsindex;
    indexFromLength(
            curveprimptsindex, vt_curvevtxcounts.size(),
            vt_curvevtxcounts.cdata());

    int npts = vt_points->size();
    int ndeformerpts = vt_deformeranimpos ? vt_deformeranimpos->size() : 0;
    int orientsize = 4, vectorsize = 3;

    hdMakeRestPoints(
        *_restpointscachemap,
        UT_StringHolder(_primpath.GetText()),
        npts,
        groompvs,
        vt_points.value());

    RestPointsCacheMapType::const_accessor rpacc;
    if (!_restpointscachemap->find(
            rpacc, UT_StringHolder(_primpath.GetText())))
    {
        UT_ErrorLog::error("HairDeform: Rest point CACHING failed");
        return *vt_points;
    }
    auto &rpmap = rpacc->second;

    // Expanded point count: equal to npts for a plain groom, but larger for
    // feathers, where hdMakeRestPoints appends the barb points.
    const exint nptsexp = rpmap.myPoints.size();

    // Detect whether skin mesh is subd (topology already built in cache)
    bool skinIsSubd = false;
    if (deformmethod == DeformMethod::SURFACEDEFORM
        || deformmethod == DeformMethod::GUIDEINTERPOLATIONMESH
        || deformmethod == DeformMethod::GUIDEWEIGHTS
        || deformmethod == DeformMethod::GUIDESHAPEINTERPOLATION
        || pointdeform_has_skin)
    {
        SkinMeshCacheMapType::const_accessor skinacc;
        if (_skinmeshcachemap->find(
                skinacc, UT_StringHolder(_skinprimpath.GetText())))
            skinIsSubd = skinacc->second.mySubdTopology != nullptr;
    }

    // Per-point capture replaces the per-curve-root capture with one frame
    // per point, so a curve bends with the skin instead of transforming
    // rigidly, and feather barbs follow the surface too.
    const bool perpoint_surface
            = perpointcapture && deformmethod == DeformMethod::SURFACEDEFORM;

    // Deform methods that capture against the skin.
    const bool needs_surface_skin
            = deformmethod == DeformMethod::SURFACEDEFORM
            || deformmethod == DeformMethod::GUIDEINTERPOLATIONMESH
            || deformmethod == DeformMethod::GUIDEWEIGHTS
            || deformmethod == DeformMethod::GUIDESHAPEINTERPOLATION
            || pointdeform_has_skin;

    // Capture id matching keeps a groom point off skin it doesn't belong to:
    // it only ever captures against skin polygons carrying its own id.  The
    // skin side is bucketed here, up front: the buckets are shared between
    // grooms, and this is a cache probe once they are built.  Which bucket
    // each curve or point belongs to waits for whichever capture misses its
    // own cache and asks.
    if (captureidattrib.isstring() && needs_surface_skin
        && !_InitSkinIdBuckets(captureidattrib))
    {
        UT_ErrorLog::error(
                "HairDeform: Capture id matching failed, disabling "
                "deformation");
        deformmethod = DeformMethod::NONE;
    }

    if ((deformmethod == DeformMethod::SURFACEDEFORM || pointdeform_has_skin)
            && skinmesh
            && vt_skinrestpos
            && vt_skinanimpos)
    {
        if (!skinIsSubd && perpoint_surface)
        {
            // Poly path: init surface topo capture. Only per-point capture
            // reads _surfacetopocachemap; every other path captures into
            // _maincurveskincapturecachemap below.
            if (!_InitSurfaceTopo(
                        curveprimptsindex, rpmap, captureidattrib))
                deformmethod = DeformMethod::NONE;
        }
    }
    else if (deformmethod == DeformMethod::SURFACEDEFORM)
    {
        if (!vt_skinrestpos)
            UT_ErrorLog::error("HairDeform: Skin mesh has no rest position");
        if (!vt_skinanimpos)
            UT_ErrorLog::error("HairDeform: Skin mesh has no anim position");
        deformmethod = DeformMethod::NONE;
    }

    bool use_opencl = preserveshapeenable
        || deformmethod == DeformMethod::POINTDEFORM
        || deformmethod == DeformMethod::SURFACEDEFORM
        || deformmethod == DeformMethod::GUIDEINTERPOLATIONMESH
        || deformmethod == DeformMethod::GUIDEWEIGHTS
        || deformmethod == DeformMethod::GUIDESHAPEINTERPOLATION
        || pointdeform_has_skin;

    // Initialize to rest positions so the bgeo dump and early-exit paths always
    // produce meaningful output even when the OpenCL deformation throws.
    VtVec3fArray newpos = rpmap.myPoints;

    // OpenCL deformation - catch cl::Error
    try
    {
        utZoneScopedN("opencl");
        // CE prep
        CE_Context *context = CE_Context::getContext();
        context->sweepInUseMemory();

        // groom CE arrays
        CE_Array<float> ce_main_pos_out;
        GU_PointDeform::CachedItems pointdeform_cacheditems;
        PointDeformCEArrays pointdeform_cearrays;

        if (use_opencl)
            ce_main_pos_out.initFromData(
                    (const float *)rpmap.myPoints.cdata(),
                    vectorsize * rpmap.myPoints.size());

        if (deformmethod == DeformMethod::POINTDEFORM)
        {
            UT_Span<const int> pcaptindex;
            UT_Span<const int> pcaptpts;
            UT_Span<const float> pcaptweights;

            // Declare outside conditional so it stays alive until _InitPointDeform completes
            PointDeformCaptureCacheMapType::const_accessor captacc;

            if (use_pcaptpvs)
            {
                pcaptindex = UT_Span<const int>(
                        pcaptpvs->myIndex.data(), pcaptpvs->myIndex.size());
                pcaptpts = UT_Span<const int>(
                        pcaptpvs->myVtCaptPts.cdata(), pcaptpvs->myVtCaptPts.size());
                pcaptweights = UT_Span<const float>(
                        pcaptpvs->myVtCaptWeights.cdata(), pcaptpvs->myVtCaptWeights.size());
            }
            else
            {
                if (_pointdeformcapturecachemap->find(
                        captacc, UT_StringHolder(_primpath.GetText())))
                {
                    pcaptpts = captacc->second.captPts;
                    pcaptweights = captacc->second.captWeights;
                    pcaptindex = captacc->second.captStarts;
                }
                else
                {
                    UT_ErrorLog::error(
                            "HairDeform: Point capture cache not found for {}",
                            UT_StringHolder(_primpath.GetText()));
                    deformmethod = DeformMethod::NONE;
                }
            }

            if (deformmethod == DeformMethod::POINTDEFORM
                && !_InitPointDeform(
                    *context, ce_main_pos_out, pointdeform_cearrays,
                    pointdeform_cacheditems,
                    vt_deformerrestpos.value(), deformeranimpos_xformed,
                    pcaptpts, pcaptweights, pcaptindex,
                    npts, ndeformerpts,
                    useorientattrib,
                    vt_deformerrestorient ? &vt_deformerrestorient.value() : nullptr,
                    vt_deformeranimorient ? &vt_deformeranimorient.value() : nullptr,
                    vt_deformerrestorient_quat ? &vt_deformerrestorient_quat.value() : nullptr,
                    vt_deformeranimorient_quat ? &vt_deformeranimorient_quat.value() : nullptr,
                    recompile))
            {
                deformmethod = DeformMethod::NONE;
            }
        }

        // must be declared outside of the cosserat rods conditional block
        // because kernels might finish after that goes out of scope.
        CE_Array<float> ce_orients;
        CE_Array<float> ce_restorients;
        CE_Array<int> ce_flags;

        // Clump constraint arrays for cosserat rods
        CE_Array<int> ce_clumppts;
        CE_Array<int> ce_clumpptsindex;
        CE_Array<float> ce_clumpdists;

        // hdInitOrientAttribs outputs are cached per-prim in
        // _orientattribscachemap. The accessor here keeps the cache entry
        // locked for the duration of upload + cosserat init. The orients/
        // restorients/flags references below alias into the cache entry.
        OrientAttribsCacheMapType::accessor _orientAttribsAcc;

        // index arrays, built from Vt lengths arrays
        UT_Array<int> curvevtxindex;

        // close to 1.0 converges faster but becomes unstable.
        constexpr float damping_ratio = 1.5f;

        GU_Cosserat::SimSettings cosserat_sim_settings{
                .myDamping = damping_ratio,
                .myKStretch = preserveshapekstretch,
                .myKBend = preserveshapekbend,
                // SOP scales refposstrength by kstretch — match that here.
                .myRefPosMult = preserveshaperefposstrength * preserveshapekstretch,
                .myClumpStiffness = preserveclumpsstiffness * preserveshapekstretch,
                .myClumpDamping = preserveclumpsdamping,
                .myLockRoots = preserveshapelockroots};

        GU_Cosserat::CachedItems cosserat_cached_items{};

        // When deformation is disabled, use the groom's rest primvar for
        // Cosserat init (rest lengths/orientations) then solve on the
        // animated points — mirroring the SOP's deformenable=off path.
        CE_Array<float> ce_groom_rest_pos;

        if (preserveshapeenable)
        {
            utZoneScopedN("preserveshape");
            indexFromLength(
                    curvevtxindex, vt_curvevtxcounts.size(),
                    vt_curvevtxcounts.cdata());

            context->getQueue().finish();

            const VtVec3fArray &cosserat_rest_pts = !deformenable
                    ? *vt_groomrestpos : rpmap.myPoints;

            {
                utZoneScopedN("init_orient_attribs");
                UT_StringHolder cachekey(_primpath.GetText());
                const bool inserted = _orientattribscachemap->insert(
                        _orientAttribsAcc, cachekey);
                auto &entry = _orientAttribsAcc->second;

                const void *rp_data  = cosserat_rest_pts.cdata();
                const size_t rp_size = cosserat_rest_pts.size();
                const void *cnt_data = vt_curvevtxcounts.cdata();
                const size_t cnt_size = vt_curvevtxcounts.size();

                const bool stale = !inserted
                    && (entry.myRestPtsData != rp_data
                        || entry.myRestPtsSize != rp_size
                        || entry.myCountsData  != cnt_data
                        || entry.myCountsSize  != cnt_size);

                if (inserted || stale)
                {
                    HD_HairDeformUtils::cacheLog(
                            "HairDeform: CACHE {} orientattribs",
                            inserted ? "MISS" : "STALE");
                    hdInitOrientAttribs(
                            entry.myOrients, entry.myRestOrients, entry.myFlags,
                            cosserat_rest_pts, vt_curvevtxcounts,
                            curvevtxindex, npts);
                    entry.myRestPtsData = rp_data;
                    entry.myRestPtsSize = rp_size;
                    entry.myCountsData  = cnt_data;
                    entry.myCountsSize  = cnt_size;
                }
                else
                {
                    HD_HairDeformUtils::cacheLog(
                            "HairDeform: CACHE HIT orientattribs");
                }
            }

            auto &orients      = _orientAttribsAcc->second.myOrients;
            auto &restorients  = _orientAttribsAcc->second.myRestOrients;
            auto &flags        = _orientAttribsAcc->second.myFlags;

            initFromArrayOrUseHost(
                    *context, ce_orients, (float *)orients.data(),
                    orientsize * orients.size());

            initFromArrayOrUseHost(
                    *context, ce_flags, flags.data(), flags.size());

            initFromArrayOrUseHost(
                    *context, ce_restorients, (float *)restorients.data(),
                    orientsize * restorients.size());

            auto &buffers = cosserat_cached_items.getBuffers();

            if (!deformenable)
            {
                ce_groom_rest_pos.initFromData(
                        (const float *)vt_groomrestpos->cdata(),
                        vectorsize * vt_groomrestpos->size());
                buffers.myPos.setExternal(ce_groom_rest_pos.buffer());
            }
            else
                buffers.myPos.setExternal(ce_main_pos_out.buffer());
            buffers.myOrient.setExternal(ce_orients.buffer());
            buffers.myRestOrient.setExternal(ce_restorients.buffer());
            buffers.myFlags.setExternal(ce_flags.buffer());

            // Clump constraint buffers
            bool doclumping = preserveclumpsenable
                              && preserveclumpsstiffness > 0.0f;

            if (doclumping)
            {
                auto vt_clumppts = getConstPvVal<int>(
                        groompvs, _tokens->clumppts, 0.0f);
                auto vt_clumpptsLengths = getConstPvVal<int>(
                        groompvs, _tokens->clumpptsLengths, 0.0f);
                auto vt_clumpdists = getConstPvVal<float>(
                        groompvs, _tokens->clumpdists, 0.0f);
                auto vt_clumpdistsLengths = getConstPvVal<int>(
                        groompvs, _tokens->clumpdistsLengths, 0.0f);

                if (vt_clumppts && vt_clumpptsLengths
                    && vt_clumpdists && vt_clumpdistsLengths
                    && vt_clumpptsLengths->size() == npts
                    && vt_clumpdistsLengths->size() == npts)
                {
                    // Use pre-authored primvar clump topology
                    UT_Array<int> clumpptsindex;
                    indexFromLength(
                            clumpptsindex, npts,
                            vt_clumpptsLengths->cdata());

                    initFromArrayOrUseHost(
                            *context, ce_clumppts,
                            vt_clumppts->cdata(), vt_clumppts->size());
                    initFromArrayOrUseHost(
                            *context, ce_clumpptsindex,
                            clumpptsindex.data(), clumpptsindex.size());
                    initFromArrayOrUseHost(
                            *context, ce_clumpdists,
                            vt_clumpdists->cdata(), vt_clumpdists->size());
                }
                else
                {
                    // Auto-compute clump topology from rest positions
                    utZoneScopedN("clump_auto_compute");
                    UT_StringHolder cachekey(_primpath.GetText());

                    ClumpTopoCacheMapType::accessor acc;
                    if (_clumptopocachemap->insert(acc, cachekey))
                    {
                        // Cache miss -- compute
                        cacheLog("HairDeform: CACHE MISS clumptopo");
                        auto &cache = acc->second;

                        // Build point-to-curve map from vertex counts
                        UT_IntArray ptcurve;
                        ptcurve.setSizeNoInit(npts);
                        int ptidx = 0;
                        for (exint c = 0;
                             c < vt_curvevtxcounts.size(); c++)
                        {
                            for (int v = 0; v < vt_curvevtxcounts[c];
                                 v++)
                                ptcurve[ptidx++] = (int)c;
                        }

                        // Build point tree from rest positions
                        const VtVec3fArray &clump_rest_pts
                                = !deformenable ? *vt_groomrestpos
                                                : rpmap.myPoints;
                        GEO_PointTreeInt tree;
                        tree.build(UT_Span<const UT_Vector3>(
                            reinterpret_cast<const UT_Vector3 *>(
                                clump_rest_pts.cdata()),
                            npts));

                        // Pass 1: count neighbors per point
                        UT_IntArray neighborcounts;
                        neighborcounts.setSizeNoInit(npts);

                        UTparallelFor(
                            UT_BlockedRange<exint>(0, npts),
                            [&](const UT_BlockedRange<exint> &r)
                        {
                            GEO_PointTreeInt::IdxArrayType candidates;
                            UT_FloatArray dists;
                            for (exint i = r.begin(); i < r.end();
                                 i++)
                            {
                                int mycurve = ptcurve[i];
                                int searchcount
                                        = preserveclumpsmaxneighbors;
                                UT_Vector3 pos(
                                        clump_rest_pts[i][0],
                                        clump_rest_pts[i][1],
                                        clump_rest_pts[i][2]);
                                tree.findNearestGroupIdx(
                                        pos, SYS_FPREAL_MAX,
                                        searchcount, candidates,
                                        dists);

                                int count = 0;
                                for (exint j = 0;
                                     j < candidates.size()
                                     && count < preserveclumpsmaxconstraints;
                                     j++)
                                {
                                    int nbr = candidates[j];
                                    if (nbr == i)
                                        continue;
                                    if (ptcurve[nbr] == mycurve)
                                        continue;
                                    count++;
                                }
                                neighborcounts[i] = count;
                            }
                        });

                        // Build prefix-sum index
                        cache.myClumpPtsIndex.setSizeNoInit(npts + 1);
                        cache.myClumpPtsIndex[0] = 0;
                        std::inclusive_scan(
                                neighborcounts.begin(),
                                neighborcounts.end(),
                                cache.myClumpPtsIndex.begin() + 1);

                        int total = cache.myClumpPtsIndex[npts];
                        cache.myClumpPts.setSizeNoInit(total);
                        cache.myClumpDists.setSizeNoInit(total);

                        // Pass 2: fill flat arrays
                        UTparallelFor(
                            UT_BlockedRange<exint>(0, npts),
                            [&](const UT_BlockedRange<exint> &r)
                        {
                            GEO_PointTreeInt::IdxArrayType candidates;
                            UT_FloatArray dists;
                            for (exint i = r.begin(); i < r.end();
                                 i++)
                            {
                                int mycurve = ptcurve[i];
                                int searchcount
                                        = preserveclumpsmaxneighbors;
                                UT_Vector3 pos(
                                        clump_rest_pts[i][0],
                                        clump_rest_pts[i][1],
                                        clump_rest_pts[i][2]);
                                tree.findNearestGroupIdx(
                                        pos, SYS_FPREAL_MAX,
                                        searchcount, candidates,
                                        dists);

                                int offset
                                        = cache.myClumpPtsIndex[i];
                                int count = 0;
                                for (exint j = 0;
                                     j < candidates.size()
                                     && count < preserveclumpsmaxconstraints;
                                     j++)
                                {
                                    int nbr = candidates[j];
                                    if (nbr == i)
                                        continue;
                                    if (ptcurve[nbr] == mycurve)
                                        continue;
                                    cache.myClumpPts[offset + count]
                                            = nbr;
                                    cache.myClumpDists[offset + count]
                                            = SYSsqrt(dists[j]);
                                    count++;
                                }
                            }
                        });

                        // Pass 3: symmetrize — for every directed edge (a→b), ensure (b→a) exists.
                        GUcosseratSymmetrizeClumpConstraints(
                                cache.myClumpPts,
                                cache.myClumpPtsIndex,
                                cache.myClumpDists,
                                npts);
                    }
                    else
                    {
                        cacheLog("HairDeform: CACHE HIT clumptopo");
                    }

                    auto &clumpcache = acc->second;
                    initFromArrayOrUseHost(
                            *context, ce_clumppts,
                            clumpcache.myClumpPts.data(),
                            clumpcache.myClumpPts.size());
                    initFromArrayOrUseHost(
                            *context, ce_clumpptsindex,
                            clumpcache.myClumpPtsIndex.data(),
                            clumpcache.myClumpPtsIndex.size());
                    initFromArrayOrUseHost(
                            *context, ce_clumpdists,
                            clumpcache.myClumpDists.data(),
                            clumpcache.myClumpDists.size());
                }

                buffers.myClumpPts.setExternal(ce_clumppts.buffer());
                buffers.myClumpPtsIndex.setExternal(
                        ce_clumpptsindex.buffer());
                buffers.myClumpDists.setExternal(
                        ce_clumpdists.buffer());
            }
            // Important to initialize before applying deformation
            GUcosseratInit(
                    *context, npts, cosserat_sim_settings,
                    cosserat_cached_items, recompile);

            // Switch solve position to animated points after init so the
            // quasi-static solve corrects the user's deformation, not rest.
            if (!deformenable)
                cosserat_cached_items.getBuffers().myPos.setExternal(
                        ce_main_pos_out.buffer());

#ifdef USDHD_HAIRDEFORM_FINISH_KERNELS
            context->getQueue().finish();
#endif
        }

        int nprims = vt_curvevtxcounts.size();

        // Locked inside _ComputeSubdSkinXforms, once the host-side capture has
        // run, and held to the end of this block because ce_main_skinxform outlives
        // the call that fills it.
        UT_Lock::Scope gpulockscope(theGpuDeformLock, /*acquire=*/false);

        CE_FloatArray ce_main_skinxform;

        // Compute skinxform for each curve root based on either polygon or
        // subd skin geometry
        if (deformmethod != DeformMethod::NONE)
        {
            if (needs_surface_skin)
            {
                pointdeform_cearrays.ce_primptsindex.initFromArray(curveprimptsindex);

                SkinCaptureCEData skince;

                if (!skinIsSubd)
                {
                    // A shared accessor: everything read out of the entry
                    // below is const, and every groom bound to this skin comes
                    // through here.
                    SkinMeshCacheMapType::const_accessor skinacc;
                    if (!_skinmeshcachemap->find(
                            skinacc,
                            UT_StringHolder(_skinprimpath.GetText()))
                        || !skinacc->second.myGdp)
                    {
                        UT_ErrorLog::error(
                            "HairDeform: Skin mesh cache not found");
                        if (deformmethod == DeformMethod::SURFACEDEFORM)
                            return rpmap.myPoints;
                        return newpos;
                    }
                    auto &skincache = skinacc->second;

                    if (!_PrepareSkinCaptureData(
                                skince,
                                *skincache.myGdp,
                                skincache.myNormal.get(),
                                skincache.myTangent.get(),
                                vt_skinrestpos.value(),
                                skinanimpos_xformed))
                    {
                        if (deformmethod == DeformMethod::SURFACEDEFORM)
                            return rpmap.myPoints;
                        return newpos;
                    }

                    if (perpoint_surface)
                    {
                        // Per-point capture, computed by _InitSurfaceTopo
                        // over the expanded rest points.
                        SurfaceTopoCacheMapType::const_accessor surfacc;
                        if (!_surfacetopocachemap->find(
                                    surfacc,
                                    UT_StringHolder(_primpath.GetText())))
                        {
                            UT_ErrorLog::error(
                                "HairDeform: Per-point surface capture missing");
                            if (deformmethod == DeformMethod::SURFACEDEFORM)
                                return rpmap.myPoints;
                            return newpos;
                        }
                        hdUploadCurveSkinCapture(
                                skince.ce_mainskinptstarts,
                                skince.ce_mainskinptindices,
                                skince.ce_mainskinptweights,
                                surfacc->second);
                    }
                    else
                    {
                        // Main curve skin capture -- cached (topological)
                        if (!_InitCurveSkinCapture(
                                    skince.ce_mainskinptstarts,
                                    skince.ce_mainskinptindices,
                                    skince.ce_mainskinptweights,
                                    skincache,
                                    curveprimptsindex,
                                    rpmap,
                                    captureidattrib))
                        {
                            if (deformmethod == DeformMethod::SURFACEDEFORM)
                                return rpmap.myPoints;
                            return newpos;
                        }
                    }

                    // One weighted frame per point with per-point capture,
                    // per curve otherwise.
                    _ComputeCurveSkinXforms(
                            *context,
                            ce_main_skinxform,
                            skince.ce_mainskinptstarts,
                            skince.ce_mainskinptindices,
                            skince.ce_mainskinptweights,
                            skince,
                            perpoint_surface ? nptsexp : nprims,
                            recompile);
                }
                else
                {
                    if (!_ComputeSubdSkinXforms(
                            *context,
                            ce_main_skinxform,
                            perpoint_surface ? nptsexp : nprims,
                            rpmap,
                            curveprimptsindex,
                            vt_skinrestpos.value(),
                            skinanimpos_xformed,
                            perpoint_surface,
                            captureidattrib,
                            &gpulockscope))
                    {
                        if (deformmethod == DeformMethod::SURFACEDEFORM)
                            return rpmap.myPoints;
                        return newpos;
                    }
                }
            }
        }

        if (deformmethod == DeformMethod::POINTDEFORM)
        {
            if (pointdeform_has_skin)
            {
                CE_FloatArray ce_rootmask;
                hdBuildCurveRootMaskCurves(
                        *context,
                        ce_rootmask,
                        pointdeform_cearrays.ce_primptsindex.buffer(),
                        npts,
                        nprims,
                        recompile);

                if (!_ApplySubdSkinXforms(
                        *context, ce_main_pos_out,
                        curveprimptsindex, npts,
                        ce_main_skinxform,
                        &ce_rootmask))
                {
                    return rpmap.myPoints;
                }

                // pointdeform masked translate expects non-root mask (1 for non-root).
                auto invertmask = getKernel(
                        *context, "deform/guidedeform.cl", "invertMask",
                        "", recompile);
                enqueueKernel(*context, npts, invertmask,
                        (int)npts,
                        (int*)nullptr,  // group
                        ce_rootmask.buffer());
                context->getQueue().finish();

                cl::Buffer rootmask = ce_rootmask.buffer();
                GUpointDeformTranslate(
                        ce_main_pos_out.buffer(),
                        ce_main_pos_out.buffer(),
                        pointdeform_cacheditems,
                        *context,
                        npts,
                        recompile,
                        &rootmask);
            }
            else
            {
                GUpointDeformTranslate(
                        ce_main_pos_out.buffer(),
                        ce_main_pos_out.buffer(),
                        pointdeform_cacheditems,
                        *context,
                        npts,
                        recompile);
            }
        }
        else if (deformmethod == DeformMethod::SURFACEDEFORM)
        {
            // Either way every expanded point moves: per point with its own
            // frame, or rigidly with its curve, barbs included.
            if (!_ApplySubdSkinXforms(
                    *context,
                    ce_main_pos_out,
                    curveprimptsindex,
                    nptsexp,
                    ce_main_skinxform,
                    /*ce_mask=*/nullptr,
                    perpoint_surface,
                    &rpmap.myBarbLayout))
            {
                return rpmap.myPoints;
            }
        }
        else if (deformmethod == DeformMethod::GUIDEINTERPOLATIONMESH
                 || deformmethod == DeformMethod::GUIDEWEIGHTS
                 || deformmethod == DeformMethod::GUIDESHAPEINTERPOLATION)
        {
            // Upload orient CE arrays for guide deform orient blend.
            // (For POINTDEFORM these are initialized by _InitPointDeform.)
            // GfQuatf and GfVec4f both use (x,y,z,w) memory layout.
            if (useorientattrib)
            {
                auto uploadOrient = [&](CE_Array<float> &ce_arr,
                        const UT_Optional<const VtArray<GfVec4f>> &vec4,
                        const UT_Optional<const VtArray<GfQuatf>> &quat)
                {
                    if (vec4)
                        initFromArrayOrUseHost(*context, ce_arr,
                                (const float *)vec4->cdata(),
                                static_cast<int>(vec4->size() * 4));
                    else if (quat)
                        initFromArrayOrUseHost(*context, ce_arr,
                                (const float *)quat->cdata(),
                                static_cast<int>(quat->size() * 4));
                };
                uploadOrient(pointdeform_cearrays.ce_deformerrestorient,
                        vt_deformerrestorient, vt_deformerrestorient_quat);
                uploadOrient(pointdeform_cearrays.ce_deformeranimorient,
                        vt_deformeranimorient, vt_deformeranimorient_quat);
            }

            if (!_ComputeGuideDeform(
                   *context, ce_main_pos_out, pointdeform_cearrays,
                   ce_main_skinxform,
                   rpmap.myPoints, curveprimptsindex, vt_curvevtxcounts,
                   groompvs, vt_deformerrestpos.value(), deformeranimpos_xformed,
                   deformmethod, gsiParms, npts, recompile))
            {
               return rpmap.myPoints;
            }
        }

        if (preserveshapeenable)
        {
            if (deformmethod == DeformMethod::POINTDEFORM)
            {
                GUpointDeformRotateQuat(
                        ce_orients.buffer(), ce_orients.buffer(),
                        pointdeform_cacheditems, *context, npts, recompile);
            }

            GUcosseratInitRefPos(*context, npts, cosserat_cached_items);

            for (int i = 0; i < preserveshapeiterations; ++i)
            {
                GUcosseratIter(
                        *context, npts, cosserat_sim_settings,
                        cosserat_cached_items);
#ifdef USDHD_HAIRDEFORM_FINISH_KERNELS
                context->getQueue().finish();
#endif

                GUcosseratUpdateOrient(
                        *context, npts, cosserat_sim_settings,
                        cosserat_cached_items);
#ifdef USDHD_HAIRDEFORM_FINISH_KERNELS
                context->getQueue().finish();
#endif

            }
            GUcosseratFinalize(*context, npts, cosserat_cached_items);
        }

        if (use_opencl)
        {
            // If we're on GPU, perform the final copy to newpos
            utZoneScopedN("final_copy");
            hdCopyDeviceArrayToHostData(newpos.data(), ce_main_pos_out, *context);
        }

        context->getQueue().finish();
    }
    catch (cl::Error &err)
    {
        UT_WorkBuffer errmsg;
        errmsg.sprintf("HairDeform: OpenCL Exception: %s (%i)", err.what(), (int)err.err());
        UT_ErrorLog::error(errmsg.buffer());
    }

    if (deformmethod == DeformMethod::NONE && !use_opencl)
    {
        // return rest points as we have no change
        return rpmap.myPoints;
    }

#ifdef HOUDINI_HAIRDEFORM_DUMP
    {
        const char *dumpdir = getenv("HOUDINI_HAIRDEFORM_DUMP");
        if (!dumpdir)
            dumpdir = "/tmp/hairdeform_dump";

        UT_WorkBuffer san;
        san.strcpy(_primpath.GetText());
        san.substitute("/", "_");
        san.substitute(":", "_");
        san.substitute(" ", "_");

        UT_WorkBuffer fpath;
        fpath.sprintf("%s/hydra_%s.bgeo.sc", dumpdir, san.buffer());

        GU_Detail gdp;
        GA_Offset ptstart = gdp.appendPointBlock((GA_Size)newpos.size());
        GA_RWHandleV3 Ph(gdp.getP());
        for (exint i = 0; i < (exint)newpos.size(); ++i)
        {
            const GfVec3f &p = newpos[i];
            Ph.set(ptstart + i, UT_Vector3F(p[0], p[1], p[2]));
        }
        gdp.save(fpath.buffer(), nullptr);
    }
#endif

    return newpos;
}

bool
HD_HairDeformPointsDataSource::GetContributingSampleTimesForInterval(
        const Time startTime,
        const Time endTime,
        std::vector<Time> *const outSampleTimes)
{
    // hdTransformPositions() bakes the skin's and deformer's transforms into
    // the deformed points, so a rigidly transformed skin moves the groom just
    // as much as animated skin points do. Both have to contribute sample
    // times.
    std::vector<HdSampledDataSourceHandle> sources;

    auto addSources = [&](const HdContainerDataSourceHandle &primds)
    {
        if (!primds)
            return;
        if (HdPrimvarsSchema pvs = HdPrimvarsSchema::GetFromParent(primds))
        {
            // Flattened, to match how _ComputePoints() reads the points: an
            // indexed primvar animated through its indices only varies on the
            // flattened source.
            if (HdPrimvarSchema pv = pvs.GetPrimvar(HdTokens->points))
            {
                if (HdSampledDataSourceHandle ds
                        = pv.GetFlattenedPrimvarValue())
                    sources.push_back(ds);
            }
        }
        if (HdXformSchema xs = HdXformSchema::GetFromParent(primds))
        {
            if (HdMatrixDataSourceHandle ds = xs.GetMatrix())
                sources.push_back(ds);
        }
    };

    addSources(_skinds);
    addSources(_deformerds);

    return HdGetMergedContributingSampleTimesForInterval(
            sources.size(), sources.data(), startTime, endTime, outSampleTimes);
}

PXR_NAMESPACE_CLOSE_SCOPE
