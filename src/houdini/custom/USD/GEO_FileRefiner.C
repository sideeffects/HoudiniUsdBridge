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
 */

#include "GEO_FileRefiner.h"
#include "GEO_FilePrimAgentUtils.h"
#include "GEO_FilePrimInstancerUtils.h"
#include "GEO_FilePrimVolumeUtils.h"
#include <gusd/purpose.h>
#include <gusd/primWrapper.h>
#include <gusd/GU_PackedUSD.h>
#include <gusd/GT_PackedUSD.h>
#include <gusd/GU_USD.h>
#include <gusd/stageCache.h>

#include <HUSD/HUSD_Constants.h>
#include <HUSD/HUSD_Utils.h>
#include <HUSD/XUSD_LockedGeoRegistry.h>
#include <GOP/GOP_Manager.h>
#include <GU/GU_Agent.h>
#include <GU/GU_PackedDisk.h>
#include <GEO/GEO_Primitive.h>
#include <GEO/GEO_PrimitiveP.h>
#include <GEO/GEO_PrimVolume.h>
#include <GT/GT_AttributeMerge.h>
#include <GT/GT_PrimCollect.h>
#include <GT/GT_PrimInstance.h>
#include <GT/GT_PrimVDB.h>
#include <GT/GT_PrimVolume.h>
#include <GT/GT_GEOAttributeFilter.h>
#include <GT/GT_GEOPackedAgent.h>
#include <GT/GT_GEOPrimCollect.h>
#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_GEOPrimTPSurf.h>
#include <GT/GT_DANumeric.h>
#include <GT/GT_PrimPointMesh.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_PrimSubdivisionMesh.h>
#include <GT/GT_GEODetail.h>
#include <GT/GT_PrimTube.h>
#include <GT/GT_Util.h>
#include <UT/UT_Algorithm.h>

#include <pxr/base/plug/registry.h>

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

GEO_FileRefiner::GEO_FileRefiner(
    GEO_FileRefinerCollector&   collector,
    const SdfPath&          pathPrefix,
    const UT_StringArray&   pathAttrNames,
    bool                    prefixAbsolutePaths)
    : m_collector( collector )
    , m_pathPrefix( pathPrefix )
    , m_pathAttrNames( pathAttrNames )
    , m_prefixAbsolutePaths( prefixAbsolutePaths )
    , m_topologyId( GA_INVALID_DATAID )
    , m_markMeshesAsSubd( false )
    , m_handleUsdPackedPrims( GEO_USD_PACKED_IGNORE )
    , m_handlePackedPrims( GEO_PACKED_XFORMS )
    , m_handleAgents( GEO_AGENT_INSTANCED_SKELROOTS )
    , m_handleNurbsSurfs( GEO_NURBSSURF_MESHES )
{
}

GEO_FileRefiner::~GEO_FileRefiner()
{
}

GEO_FileRefiner
GEO_FileRefiner::createSubRefiner(
        const SdfPath &pathPrefix,
        const UT_StringArray &pathAttrNames,
        const GEO_AgentShapeInfoPtr &agentShapeInfo)
{
    GEO_FileRefiner subrefiner(m_collector, pathPrefix, pathAttrNames,
                               m_prefixAbsolutePaths);
    subrefiner.m_overridePath = m_overridePath;
    subrefiner.m_handleUsdPackedPrims = m_handleUsdPackedPrims;
    subrefiner.m_handlePackedPrims = m_handlePackedPrims;
    subrefiner.m_handleAgents = m_handleAgents;
    subrefiner.m_handleNurbsSurfs = m_handleNurbsSurfs;
    subrefiner.m_agentShapeInfo =
        agentShapeInfo ? agentShapeInfo : m_agentShapeInfo;

    return subrefiner;
}

/// Find all string attributes from the provided list that exist on the
/// geometry.
static void
geoFindStringAttribs(const GU_Detail &gdp, GA_AttributeOwner owner,
                     const UT_StringArray &attrib_names,
                     UT_Array<GA_ROHandleS> &attribs)
{
    for (const UT_StringHolder &attrib_name : attrib_names)
    {
        GA_ROHandleS attrib = gdp.findStringTuple(owner, attrib_name);
        if (attrib.isValid())
            attribs.append(attrib);
    }
}

/// Compute a data ID for the detail's topology and path attributes.
static GA_DataId
geoComputeTopologyId(const GU_Detail &gdp, const UT_StringArray &pathAttrNames)
{
    UT_Array<const GA_Attribute *> path_attrs;
    bool topology_ids_valid = true;

    // If we are using a path attribute to split geometry into
    // pieces, then changes to the path attribute values may
    // also indicate a change in scene graph topology.
    for (auto &&path_attr_name : pathAttrNames)
    {
        const GA_Attribute *path_attr;

        path_attr = gdp.findPrimitiveAttribute(path_attr_name);
        if (path_attr)
        {
            path_attrs.append(path_attr);
            if (path_attr->getDataId() == GA_INVALID_DATAID)
                topology_ids_valid = false;
        }
    }
    if (gdp.getTopology().getDataId() == GA_INVALID_DATAID)
        topology_ids_valid = false;

    // If anything has an invalid data id, our topology id must also
    // be left with an invalid value.
    if (topology_ids_valid)
    {
        SYS_HashType hash = 0;

        SYShashCombine(hash, gdp.getTopology().getDataId());
        for (int i = 0, n = path_attrs.size(); i < n; i++)
        {
            SYShashCombine(hash, pathAttrNames(i));
            SYShashCombine(hash, path_attrs(i)->getDataId());
        }

        return hash;
    }
    else
        return GA_INVALID_DATAID;
}

namespace
{
struct Partition
{
    Partition(const GA_Range &range, bool subd) : myRange(range), mySubd(subd)
    {
    }

    GA_Range myRange;
    bool mySubd;
};
} // namespace

static SYS_FORCE_INLINE const UT_StringHolder &
geoFindPartition(const UT_Array<GA_ROHandleS> &partition_attribs,
                 const GU_Detail &gdp, GA_AttributeOwner owner,
                 GA_Offset offset)
{
    // Put all volume primitives in the same partition so that they are
    // processed in index order (RFE 98536).
    if (owner == GA_ATTRIB_PRIMITIVE)
    {
        const GA_PrimitiveTypeId primtype = gdp.getPrimitiveTypeId(offset);
        if (primtype == GEO_PRIMVOLUME || primtype == GEO_PRIMVDB)
            return UT_StringHolder::theEmptyString;
    }

    for (const GA_ROHandleS &partition_attrib : partition_attribs)
    {
        const UT_StringHolder &partition = partition_attrib.get(offset);

        if (partition.isstring())
            return partition;
    }

    return UT_StringHolder::theEmptyString;
}

/// Partitions the provided point / primitive range using the given list of
/// string partition attributes.
static void
geoPartitionRange(const GU_Detail &gdp, const GA_Range &range, bool subd,
                  const UT_Array<GA_ROHandleS> &partition_attribs,
                  UT_Array<Partition> &partitions)
{
    if (partition_attribs.isEmpty())
    {
        partitions.append(Partition(range, subd));
        return;
    }

    const GA_AttributeOwner owner = partition_attribs[0]->getOwner();

    // Maintain the ordering in which the partitions were encountered when
    // traversing the geometry.
    UT_StringMap<exint> partition_map;
    UT_Array<GA_OffsetList> partition_offsetlists;
    for (GA_Offset offset : range)
    {
        const UT_StringHolder &partition =
            geoFindPartition(partition_attribs, gdp, owner, offset);

        const exint pidx = UTfindOrInsert(partition_map, partition, [&]() {
            return partition_offsetlists.append();
        });
        partition_offsetlists[pidx].append(offset);
    }

    const GA_IndexMap &index_map = gdp.getIndexMap(range.getOwner());
    partitions.setCapacity(partition_offsetlists.size());
    for (const GA_OffsetList &partition_offsets : partition_offsetlists)
    {
        partitions.append(
            Partition(GA_Range(index_map, partition_offsets), subd));
    }
}

void
GEO_FileRefiner::refineDetail(
        const GU_ConstDetailHandle &detail,
        const GT_RefineParms &refineParms,
        const GT_TransformHandle &xform)
{
    m_refineParms = refineParms;

    // Deal with unused points separately from GT_GEODetail::makeDetail() so
    // that we can e.g. control whether they are partitioned, or if they are
    // imported when the geometry also contains primitives.
    m_refineParms.setShowUnusedPoints(false);

    GU_DetailHandleAutoReadLock detailLock( detail );
    const GU_Detail *gdp = detailLock.getGdp();
    UT_Array<GA_ROHandleS> partitionAttrs;

    m_topologyId = geoComputeTopologyId(*gdp, m_pathAttrNames);

    GOP_Manager gop;
    const GA_PrimitiveGroup *importPrimGroup = nullptr;
    const GA_PointGroup *importPointGroup = nullptr;
    GA_PrimitiveGroupUPtr nonUsdGroup(
        gdp->createDetachedPrimitiveGroup());
    GA_PrimitiveTypeId packedusd_typeid = GusdGU_PackedUSD::typeId();

    geoFindStringAttribs(*gdp, GA_ATTRIB_PRIMITIVE, m_pathAttrNames,
                         partitionAttrs);

    bool ok = true;
    if (m_importGroup.isstring())
    {
        switch (m_importGroupType)
        {
        case GA_ATTRIB_PRIMITIVE:
        {
            importPrimGroup = gop.parsePrimitiveDetached(
                    m_importGroup, gdp, false, ok);
            if (!ok)
                TF_WARN("Invalid primitive group '%s'", m_importGroup.c_str());

            break;
        }
        case GA_ATTRIB_POINT:
        {
            importPointGroup = gop.parsePointDetached(
                    m_importGroup, gdp, false, ok);
            if (!ok)
                TF_WARN("Invalid point group '%s'", m_importGroup.c_str());

            // The referenced primitives should be imported too.
            if (importPointGroup && gdp->getNumPrimitives() > 0)
            {
                GA_PrimitiveGroupUPtr referenced_prims
                        = gdp->createDetachedPrimitiveGroup();
                referenced_prims->combine(importPointGroup);

                // Transfer ownership to the GOP_Manager for consistency with
                // parsePrimitiveDetached().
                importPrimGroup = referenced_prims.get();
                gop.appendAdhocGroup(std::move(referenced_prims));
            }

            break;
        }
        default:
            UT_ASSERT_MSG(false, "Unsupported group type");
            break;
        }
    }

    // Parse the subdivision group if subdivision is enabled.
    const bool subd = m_refineParms.getPolysAsSubdivision();
    const GA_PrimitiveGroup *subdGroup = nullptr;
    if (subd && m_subdGroup.isstring())
    {
        subdGroup = gop.parsePrimitiveDetached(m_subdGroup, gdp, false, ok);
        if (!ok)
            TF_WARN("Invalid primitive group '%s'", m_subdGroup.c_str());
    }

    nonUsdGroup->addAll();
    if (m_handleUsdPackedPrims == GEO_USD_PACKED_IGNORE)
    {
	GA_Range allPrimRange = gdp->getPrimitiveRange(importPrimGroup);
	for (auto primIt = allPrimRange.begin(); !primIt.atEnd(); ++primIt)
	{
	    GEO_ConstPrimitiveP prim(gdp, *primIt);

	    if (prim->getTypeId() == packedusd_typeid)
		nonUsdGroup->remove(prim);
	}
    }
    if (importPrimGroup)
	*nonUsdGroup &= *importPrimGroup;

    if (m_refineParms.getHeightFieldConvert())
    {
        bool hasheightfield = false;

        // Check to see if there's any heightfield in the detail. If yes,
        // enable volume coalescing so that they're collected and converted to
        // mesh.
        if (gdp->hasVolumePrimitives())
        {
            const GA_Primitive *prim;
            GA_FOR_ALL_GROUP_PRIMITIVES(gdp, nonUsdGroup.get(), prim)
            {
                if (prim->getTypeId() != GA_PRIMVOLUME)
                    continue;
                auto &&vol = UTverify_cast<const GEO_PrimVolume *>(prim);
                if (vol->getVisualization() == GEO_VOLUMEVIS_HEIGHTFIELD)
                {
                    m_refineParms.setCoalesceVolumes(true);
                    hasheightfield = true;
                    break;
                }
            }
        }
        // If there's no heightfield, don't bother with convert.
        m_refineParms.setHeightFieldConvert(hasheightfield);
    }

    // If there is a subdivision group, split based on that group and then
    // further partition based on the partition attributes.
    GA_PrimitiveGroupUPtr nonUsdSubdGroup;
    UT_Array<Partition> partitions;
    if (!subdGroup)
    {
        geoPartitionRange(*gdp, gdp->getPrimitiveRange(nonUsdGroup.get()), subd,
                          partitionAttrs, partitions);
    }
    else
    {
        nonUsdSubdGroup = gdp->createDetachedPrimitiveGroup();
        nonUsdSubdGroup->copyMembership(*nonUsdGroup);
        *nonUsdSubdGroup &= *subdGroup;
        geoPartitionRange(*gdp, gdp->getPrimitiveRange(nonUsdSubdGroup.get()),
                          /* subd */ true, partitionAttrs, partitions);

        *nonUsdGroup -= *subdGroup;
        geoPartitionRange(*gdp, gdp->getPrimitiveRange(nonUsdGroup.get()),
                          /* subd */ false, partitionAttrs, partitions);
    }

    // Refine each geometry partition to prims that can be written to USD.
    // The results are accumulated in buffer in the refiner.
    for (const Partition &partition : partitions)
    {
	GT_PrimitiveHandle detailPrim =
	    GT_GEODetail::makeDetail(detail, &partition.myRange);

        m_refineParms.setPolysAsSubdivision(partition.mySubd);
	if(detailPrim)
        {
            if (xform)
                detailPrim = detailPrim->copyTransformed(xform);

	    detailPrim->refine(*this, &m_refineParms);
        }
    }

    // Unless a primitive group was specified, refine the unused points
    // (possibly partitioned by an attribute).
    GA_OffsetList unused_pts;
    if (!(m_importGroupType == GA_ATTRIB_PRIMITIVE && importPrimGroup)
        && gdp->findUnusedPoints(&unused_pts))
    {
        partitions.clear();
        partitionAttrs.clear();

        // Filter by the import point group.
        if (importPointGroup)
        {
            GA_OffsetList filtered_pts;
            filtered_pts.reserve(importPointGroup->entries());

            for (GA_Offset ptoff : unused_pts)
            {
                if (importPointGroup->contains(ptoff))
                    filtered_pts.append(ptoff);
            }

            unused_pts = std::move(filtered_pts);
        }

        geoFindStringAttribs(*gdp, GA_ATTRIB_POINT, m_pathAttrNames,
                             partitionAttrs);

        GA_Range pt_range(gdp->getPointMap(), unused_pts);
        geoPartitionRange(*gdp, pt_range, false, partitionAttrs, partitions);

        for (const Partition &partition : partitions)
        {
            GT_PrimitiveHandle prim = GT_GEODetail::makePointMesh(
                    detail, &partition.myRange);
            if (xform)
                prim = prim->copyTransformed(xform);

            addPrimitive(prim);
        }
    }

    m_overridePath = SdfPath();
    m_overridePurpose = TfToken();
}

void
GEO_FileRefiner::refinePrim(
        const GT_Primitive &prim,
        const GT_RefineParms &parms)
{
    m_refineParms = parms;

    // If the GT prim contains a detail (e.g. the contents of a packed prim),
    // determine the correct topology id.
    if (prim.getPrimitiveType() == GT_PRIM_DETAIL)
    {
        auto prim_detail = UTverify_cast<const GT_GEODetail *>(&prim);
        GU_ConstDetailHandle gdh = prim_detail->getGeometry();
        UT_ASSERT(gdh.isValid());
        m_topologyId = geoComputeTopologyId(*gdh.gdp(), m_pathAttrNames);
    }

    prim.refine(*this, &m_refineParms);
}

const GEO_FileRefiner::GEO_FileGprimArray &
GEO_FileRefiner::finish()
{
    for (auto &&it : m_pointInstancers)
        it.second->finishAddingInstances();

    m_collector.finish(*this);
    return m_collector.m_gprims;
}

std::string
GEO_FileRefiner::createPrimPath(
        const std::string &primName,
        const SdfPath &prefix,
        bool prefixAbsolutePaths /*=false*/)
{
    std::string primPath;

    if( !primName.empty() && primName[0] == '/' && !prefixAbsolutePaths )
    {
        // Use an explicit absolute path
        primPath = primName;
    }
    else
    {
        // add prefix to relative path
        primPath = prefix.GetString();
        if( !primName.empty() )
	{
            if( primPath.empty() || primPath.back() != '/' )
                primPath += "/";
            // This might result in a double '/' if the path is absolute and
            // prefixAbsolutePaths is true, but the call to HUSDmakeValidUsdPath
            // will clean this up.
            primPath += primName;
        }
        else if( !primPath.empty() && primPath.back() != '/' )
            primPath += '/';
    }

    // USD is persnikity about having a leading slash
    if( primPath[0] != '/' )
        primPath = "/" + primPath;
    // Lastly we check for any invalid characters
    UT_String primPath_str(primPath.c_str());
    if (HUSDmakeValidUsdPath(primPath_str, false))
	primPath = primPath_str.toStdString();

    return primPath;
}

static GT_DataArrayHandle
geoGetStringAttrib(
        const GT_Primitive &prim,
        const UT_StringRef &attr_name,
        GT_Owner *out_owner = nullptr)
{
    GT_Owner owner;
    GT_DataArrayHandle attrib = prim.findAttribute(attr_name, owner, 0);
    if (attrib && attrib->getStorage() != GT_STORE_STRING)
        attrib.reset();

    if (attrib && out_owner)
        *out_owner = owner;

    return attrib;
}

static std::string
geoGetStringAttribValue(
        const GT_Primitive &prim,
        const UT_StringRef &attr_name,
        const TfToken &default_value)
{
    GT_DataArrayHandle attrib = geoGetStringAttrib(prim, attr_name);
    if (attrib)
    {
        UT_StringHolder value = attrib->getS(0);
        if (value)
            return value.toStdString();
    }

    return default_value.GetString();
}

/// Returns the path that should be used for the given skeleton.
static std::string
geoGetSkeletonPath(const GT_Primitive &prim)
{
    static constexpr UT_StringLit theSkelPathAttrib("usdskelpath");
    return geoGetStringAttribValue(
            prim, theSkelPathAttrib.asRef(), GEO_AgentPrimTokens->skeleton);
}

/// Returns the path that should be used for the given skeleton animation.
static std::string
geoGetSkelAnimationPath(const GT_Primitive &prim)
{
    static constexpr UT_StringLit theAnimPathAttrib("usdanimpath");
    return geoGetStringAttribValue(
            prim, theAnimPathAttrib.asRef(), GEO_AgentPrimTokens->animation);
}

static constexpr UT_StringLit theInstancerPathAttrib("usdinstancerpath");

/// Returns the 'usdinstancerpath' string attribute.
static GT_DataArrayHandle
geoFindInstancerPathAttrib(const GT_Primitive &prim, GT_Owner &owner)
{
    GT_DataArrayHandle path_attrib =
        prim.findAttribute(theInstancerPathAttrib.asRef(), owner, 0);
    if (path_attrib && path_attrib->getStorage() != GT_STORE_STRING)
        path_attrib.reset();

    return path_attrib;
}

/// Returns the instancer path that should be used, from the given packed
/// primitive's attribs.
static UT_StringHolder
geoGetInstancerPath(const GT_AttributeListHandle &attribs)
{
    GT_DataArrayHandle path_attrib;
    if (attribs)
        path_attrib = attribs->get(theInstancerPathAttrib.asRef());

    if (path_attrib && path_attrib->getStorage() == GT_STORE_STRING)
    {
        UT_StringHolder path = path_attrib->getS(0);
        if (path)
            return path;
    }

    return GusdUSD_Utils::TokenToStringHolder(
            GEO_PointInstancerPrimTokens->instances);
}

/// Partition the GT_PrimInstance's entries based on the 'usdinstancerpath'
/// attribute (if it exists).
static void
geoPartitionInstances(const GT_PrimInstance &instance_prim,
                      UT_StringArray &instancer_paths,
                      UT_Array<UT_Array<exint>> &instancer_indices)
{
    GT_Owner owner;
    GT_DataArrayHandle path_attrib =
        geoFindInstancerPathAttrib(instance_prim, owner);

    if (!path_attrib || owner == GT_OWNER_DETAIL)
    {
        // Same path for all instances.
        UT_StringHolder path;

        if (path_attrib) // owner == GT_OWNER_DETAIL
            path = path_attrib->getS(0);

        if (!path)
        {
            path = GusdUSD_Utils::TokenToStringHolder(
                GEO_PointInstancerPrimTokens->instances);
        }

        instancer_paths.append(path);
        // If there is only one partition, we don't need the (trivial) list of
        // indices.
        instancer_indices.append();
    }
    else
    {
        UT_StringMap<exint> known_paths;

        for (exint i = 0, n = instance_prim.entries(); i < n; ++i)
        {
            UT_StringHolder path = path_attrib->getS(i);
            if (!path)
            {
                path = GusdUSD_Utils::TokenToStringHolder(
                    GEO_PointInstancerPrimTokens->instances);
            }

            exint path_idx;

            auto it = known_paths.find(path);
            if (it != known_paths.end())
                path_idx = it->second;
            else
            {
                path_idx = instancer_paths.append(path);
                instancer_indices.append();
                known_paths[path] = path_idx;
            }

            instancer_indices[path_idx].append(i);
        }
    }
}

UT_IntrusivePtr<GT_PrimPointInstancer>
GEO_FileRefiner::addPointInstancer(const UT_StringHolder &orig_instancer_path,
                                   const TfToken &purpose)
{
    SdfPath instancer_path(
            createPrimPath(orig_instancer_path.toStdString(), m_pathPrefix,
                           m_prefixAbsolutePaths));

    UT_IntrusivePtr<GT_PrimPointInstancer> &instancer =
        m_pointInstancers[instancer_path];
    if (!instancer)
    {
        instancer.reset(new GT_PrimPointInstancer());
        GEO_PathHandle path = m_collector.add(
                instancer_path,
                /* addNumericSuffix */ false, instancer,
                UT_Matrix4D::getIdentityMatrix(), m_topologyId, purpose,
                m_agentShapeInfo);
        instancer->setPath(path);
    }

    return instancer;
}

static GU_ConstDetailHandle
geoGetPackedGeometry(const GT_GEOPrimPacked &gtpacked)
{
    GU_ConstDetailHandle embedded_geo = gtpacked.getPackedDetail();
    if (!embedded_geo.isValid())
    {
        GU_DetailHandle unpacked_gdh;
        unpacked_gdh.allocateAndSet(new GU_Detail());

        UT_Matrix4D *no_xform = nullptr;
        gtpacked.getPrim()->sharedImplementation()->unpack(
                *unpacked_gdh.gdpNC(), no_xform);

        embedded_geo = unpacked_gdh;
    }

    return embedded_geo;
}

static GT_TransformHandle
geoGetPackedTransform(const GT_GEOPrimPacked &gtpacked)
{
    GT_TransformHandle xform = gtpacked.getPrimitiveTransform();
    if (gtpacked.transformed())
    {
        UT_Matrix4D prim_xform;
        gtpacked.getPrim()->getFullTransform4(prim_xform);
        if (xform)
            xform = xform->preMultiply(prim_xform);
        else
            xform = UTmakeIntrusive<GT_Transform>(&prim_xform, 1);
    }

    return xform;
}

int
GEO_FileRefiner::addPointInstancerPrototype(GT_PrimPointInstancer &instancer,
                                            GT_GEOPrimPacked &gtpacked,
                                            const TfToken &purpose,
                                            const std::string &primPath,
                                            const std::string &primName,
                                            bool addNumericSuffix)
{
    // Add a prototype for the packed primitive's geometry, if
    // it hasn't been seen before.
    int proto_index = instancer.findPrototype(gtpacked);
    if (proto_index >= 0)
        return proto_index;

    // Unless there is an absolute path, make the prototype a child of the
    // point instancer. The prototype is named based on the first instance
    // encountered.
    SdfPath init_prototype_path;
    const bool is_relative = !primName.empty() && primName[0] != '/';
    if (is_relative)
    {
        const TfToken &prototypes_group =
            GEO_PointInstancerPrimTokens->Prototypes;

        UT_WorkBuffer path;
        path.format("{0}/{1}/{2}", instancer.getPath()->GetString(),
                    prototypes_group.GetString(), primName);

        UT_String validpath;
        path.stealIntoString(validpath);
        HUSDmakeValidUsdPath(validpath, false);

        init_prototype_path = SdfPath(validpath.c_str());
    }
    else
        init_prototype_path = SdfPath(primPath);

    GT_PackedInstanceKey key = GTpackedInstanceKey(gtpacked);

    // Add or re-use an existing prototype for the instanced geometry.
    GEO_PathHandle prototype_path = UTfindOrInsert(
        m_knownInstancedGeos, key, [&]() {
            auto prototype_prim = new GT_PrimPackedInstance(&gtpacked);
            prototype_prim->setIsPrototype(true);
            // If the prototype is a child of the point instancer, it doesn't
            // need to be explicitly set as invisible since it will be pruned
            // out regardless.
            prototype_prim->setIsVisible(is_relative);

            GEO_PathHandle path = m_collector.add(
                    init_prototype_path, addNumericSuffix, prototype_prim,
                    UT_Matrix4D::getIdentityMatrix(), m_topologyId, purpose,
                    m_agentShapeInfo);

            // Refine the embedded geometry, unless it is a file reference.
            GA_PrimitiveTypeId packed_type = gtpacked.getPrim()->getTypeId();
            if (packed_type != GU_PackedDisk::typeId())
            {
                GEO_FileRefiner sub_refiner = createSubRefiner(
                        *path, m_pathAttrNames);

                GU_ConstDetailHandle embedded_geo
                        = geoGetPackedGeometry(gtpacked);
                sub_refiner.refineDetail(embedded_geo, m_refineParms);
            }

            return path;
        });

    return instancer.addPrototype(gtpacked, prototype_path);
}

GEO_PathHandle
GEO_FileRefiner::addNativePrototype(GT_GEOPrimPacked &gtpacked,
                                    const TfToken &purpose,
                                    const std::string &primPath,
                                    bool addNumericSuffix)
{
    GT_PackedInstanceKey key = GTpackedInstanceKey(gtpacked);

    return UTfindOrInsert(m_knownInstancedGeos, key, [&]() {
        SdfPath path = SdfPath(primPath);
        TfToken name = path.GetNameToken();
        path = path.ReplaceName(GEO_PointInstancerPrimTokens->Prototypes);
        path = path.AppendChild(name);

        auto prototype_prim = new GT_PrimPackedInstance(&gtpacked);
        prototype_prim->setIsPrototype(true);
        prototype_prim->setIsVisible(false);

        GEO_PathHandle prototype_path = m_collector.add(
                path, addNumericSuffix, prototype_prim,
                UT_Matrix4D::getIdentityMatrix(), m_topologyId, purpose,
                m_agentShapeInfo);

        GEO_FileRefiner sub_refiner = createSubRefiner(
                *prototype_path, m_pathAttrNames);

        GU_ConstDetailHandle embedded_geo = geoGetPackedGeometry(gtpacked);
        sub_refiner.refineDetail(embedded_geo, m_refineParms);

        return prototype_path;
    });
}

UT_IntrusivePtr<GT_PrimVolumeCollection>
GEO_FileRefiner::addVolumeCollection(const GT_Primitive &field_prim,
                                     const std::string &field_name,
                                     const TfToken &purpose)
{
    static constexpr UT_StringLit theVolumePathAttrib("usdvolumepath");

    const std::string volume_path = geoGetStringAttribValue(
            field_prim, theVolumePathAttrib.asRef(),
            GEO_VolumePrimTokens->volume);
    const bool custom_path = (volume_path != GEO_VolumePrimTokens->volume);

    SdfPath target_volume_path(createPrimPath(volume_path, m_pathPrefix,
                                              m_prefixAbsolutePaths));
    UT_IntrusivePtr<GT_PrimVolumeCollection> &volume =
        m_volumeCollections[target_volume_path];

    // Unless the user directly specified the volume path, start a new volume
    // prim if a field with the same name is seen.
    if (volume && !custom_path && volume->hasField(field_name))
        volume.reset();

    if (!volume)
    {
        volume.reset(new GT_PrimVolumeCollection());
        GEO_PathHandle volume_path = m_collector.add(
                target_volume_path, /* addNumericSuffix */ !custom_path, volume,
                UT_Matrix4D::getIdentityMatrix(), m_topologyId, purpose,
                m_agentShapeInfo);
        volume->setPath(volume_path);
    }

    return volume;
}

void
GEO_FileRefiner::refineAgentShapes(
        const GT_PrimitiveHandle &src_prim,
        const SdfPath &root_path,
        const GU_AgentDefinition &defn,
        const UT_Array<GEO_AgentShapeInfoPtr> &shapes)
{
    const GU_AgentShapeLibConstPtr &shapelib = defn.shapeLibrary();
    if (!shapelib)
        return;

    GU_ConstDetailHandle shapelib_gdh = shapelib->detail();
    GT_GEODetailList dtl_prim(shapelib_gdh);
    auto detail_attribs = dtl_prim.getDetailAttributes(GT_GEOAttributeFilter());

    for (const GEO_AgentShapeInfoPtr &shape_info : shapes)
    {
        const GU_AgentShapeLib::ShapePtr shape
                = shapelib->findShape(shape_info->myShapeName);
        UT_ASSERT(shape);

        SdfPath shape_path = GEObuildUsdShapePath(shape_info->myShapeName);

        // Retrieve the packed primitive from the shape library.
        auto shape_prim = UTverify_cast<const GU_PrimPacked *>(
                shapelib_gdh.gdp()->getGEOPrimitive(shape->offset()));
        UT_ASSERT(shape_prim);

        auto gtpacked = UTmakeIntrusive<GT_GEOPrimPacked>(
                shapelib_gdh, shape_prim,
                /* transformed */ true,
                /* include_packed_attribs */ true);

        if (m_handlePackedPrims == GEO_PACKED_UNPACK)
        {
            GU_ConstDetailHandle shape_gdh = shape->shapeGeometry(*shapelib);
            if (!shape_gdh)
                continue;

            // If we can convert this geometry to a single USD prim, directly
            // create a prim with the shape's name. This preserves the
            // hierarchy correctly when round-tripping (e.g. avoids creating an
            // extra prim like 'shape_name/mesh_0').
            const GU_Detail &shape_gdp = *shape_gdh.gdp();
            const GA_Size num_prims = shape_gdp.getNumPrimitives();
            if (shape_gdp.countPrimitiveType(GA_PRIMPOLY) == num_prims
                || shape_gdp.countPrimitiveType(GA_PRIMPOLYSOUP) == num_prims
                || shape_gdp.countPrimitiveType(GA_PRIMNURBCURVE) == num_prims
                || GU_PrimPacked::countPackedPrimitives(shape_gdp) == num_prims
                || shape_gdp.countPrimitiveType(GA_PRIMSPHERE) == 1)
            {
                GEO_FileRefiner sub_refiner = createSubRefiner(
                        root_path, {}, shape_info);
                sub_refiner.m_overridePath = shape_path;
                sub_refiner.refineDetail(
                        shape->shapeGeometry(*shapelib), m_refineParms);
                continue;
            }
        }

        // Otherwise, set up the top-level primitive for the shape.
        GEO_PathHandle path = m_collector.add(
                root_path.AppendPath(shape_path), false,
                new GT_PrimPackedInstance(
                        gtpacked, GT_Transform::identity(),
                        detail_attribs->mergeNewAttributes(
                                gtpacked->getPointAttributes())),
                UT_Matrix4D::getIdentityMatrix(), m_topologyId,
                m_overridePurpose, m_agentShapeInfo);

        // Refine the shape's geometry underneath.
        GEO_FileRefiner sub_refiner = createSubRefiner(*path, {}, shape_info);
        sub_refiner.refineDetail(
                shape->shapeGeometry(*shapelib), m_refineParms);
    }
}

/// If either the 'usdvisibility' attrib is set to 'invisible', or the packed
/// viewportlod is set to hidden, then the USD prim should be invisible.
static SYS_FORCE_INLINE bool
GEOisVisible(const GT_GEOPrimPacked &gtpacked,
             const GT_AttributeListHandle &attribs,
             int i)
{
    static constexpr UT_StringLit theVisibilityAttrib("usdvisibility");

    if (attribs && attribs->hasName(theVisibilityAttrib.asRef()))
    {
        const GT_DataArrayHandle &vis_attrib =
            attribs->get(theVisibilityAttrib.asRef());

        if (vis_attrib->getS(i) == UsdGeomTokens->invisible.GetString())
            return false;
    }

    return gtpacked.getViewportLOD(i) != GEO_VIEWPORT_HIDDEN;
}

/// Returns whether the USD prim should be drawn in bbox mode.
static SYS_FORCE_INLINE bool
GEOdrawBounds(const GT_GEOPrimPacked &gtpacked, int i = 0)
{
    return gtpacked.getViewportLOD(i) == GEO_VIEWPORT_BOX;
}

/// Convert the mesh to a subd mesh if force_subd is true, or if the
/// subdivision scheme was specified via an attribute.
static void
GEOconvertMeshToSubd(GT_PrimitiveHandle &prim, bool force_subd)
{
    // Allow enabling subdivision with an attribute.
    static constexpr UT_StringLit theSubdSchemeName("osd_scheme");
    GT_Owner owner;
    GT_DataArrayHandle scheme_attrib =
        prim->findAttribute(theSubdSchemeName.asRef(), owner, 0);

    if (scheme_attrib && scheme_attrib->entries() > 0 &&
        scheme_attrib->getStorage() == GT_STORE_STRING)
    {
        // An empty string or 'none' will disable subdivision.
        GT_String scheme = scheme_attrib->getS(0);
        if (!scheme || scheme == UsdGeomTokens->none)
            return;
    }

    if (scheme_attrib || force_subd)
    {
        GT_Scheme scheme = GT_PrimSubdivisionMesh::lookupScheme(
            scheme_attrib, GT_CATMULL_CLARK);

        // Convert the mesh into a GT_PrimSubdivisionMesh.
        auto mesh = UTverify_cast<const GT_PrimPolygonMesh *>(prim.get());

        auto subd_mesh = new GT_PrimSubdivisionMesh(*mesh, scheme);
        GT_Util::addStandardSubdTagsFromAttribs(*subd_mesh,
                                                /* allow_uniform_parms */ true);

        prim.reset(subd_mesh);
    }
}

static GU_ConstDetailHandle
geoUnpackAndTransferAttribs(
        const GT_GEOPrimPacked &packed,
        const GT_AttributeListHandle &constant_attribs,
        const GT_RefineParms &refine_parms)
{
    // A bit of extra handling is necessary to transfer attribs from the packed
    // prim (without replacing attribs that also exist on the unpacked
    // geometry). Doing this while unpacking seems to be the most
    // straightforward approach, versus e.g. adding extra items into the
    // attribute lists of the resulting GT prims.
    GU_DetailHandle unpacked_gdh;
    unpacked_gdh.allocateAndSet(new GU_Detail());

    // If there happen to be normals on the packed prim, don't transfer to the
    // unpacked mesh!
    GT_AttributeListHandle filtered_attribs;
    if (constant_attribs)
        filtered_attribs = constant_attribs->removeAttribute(GA_Names::N);

    // Add the instance's (constant) attributes to the detail before unpacking.
    // This is an easy way to allow the unpacked geo's attributes to take
    // precedence if any attributes exist for both (see
    // GUmatchAttributesAndMerge).
    GT_Util::copyAttributeListToDetail(
            unpacked_gdh.gdpNC(), GA_ATTRIB_DETAIL, &refine_parms,
            filtered_attribs, 0);

    // Unpack the geometry without applying the packed prim's transform. We
    // will be setting the transform on the GT_GEODetail prim so that it
    // is converted into a USD prim xform rather than e.g. being baked into the
    // point positions.
    const UT_Matrix4D *no_packed_xform = nullptr;
    packed.getPrim()->sharedImplementation()->unpack(
            *unpacked_gdh.gdpNC(), no_packed_xform);

    return unpacked_gdh;
}

static GU_Agent::Matrix4Array
geoBuildAgentRestPose(const GU_Agent &agent)
{
    UT_ASSERT(agent.getRig());
    const GU_AgentRig &rig = *agent.getRig();

    GU_Agent::Matrix4Array rest_pose;
    rest_pose.setSizeNoInit(rig.transformCount());

    bool is_identity = true;
    for (exint i = 0, n = rig.transformCount(); i < n; ++i)
    {
        rest_pose[i] = rig.restWorldTransform(i);
        is_identity &= rest_pose[i].isIdentity();
    }

    // Agent rigs generated in older versions may not have a rest pose, so just
    // fall back to using the current pose instead.
    if (is_identity)
    {
        GU_Agent::Matrix4ArrayConstPtr current_transforms;
        if (agent.computeWorldTransforms(current_transforms))
            rest_pose = *current_transforms;
    }

    return rest_pose;
}

void
GEO_FileRefiner::addPrimitive( const GT_PrimitiveHandle& gtPrimIn )
{
    if(!gtPrimIn) {
        std::cout << "Attempting to add invalid prim" << std::endl;
        return;
    }
    GT_PrimitiveHandle gtPrim = gtPrimIn;     // copy to a non-const handle
    int primType = gtPrim->getPrimitiveType();
    std::string primName;

    if (m_overridePath.IsEmpty())
    {
	// Types can register a function to provide a prim name. 
	// Volumes do this to return a name stored in the f3d file. This is 
	// important for consistant cluster naming.
	std::string n;
	if( GusdPrimWrapper::getPrimName( gtPrim, n ))
	    primName = n;
    }
    else
    {
	// We are refining a USD packed prim with a specific path.
	primName = m_overridePath.GetString();
    }

    if( primName.empty() )
    {
        GT_AttributeListHandle primAttrs;

        if( primType == GT_GEO_PACKED ) {
            primAttrs = UTverify_cast<const GT_GEOPrimPacked*>(
		gtPrim.get())->getInstanceAttributes();
        } 
        else if( primType == GT_PRIM_POINT_MESH ) {
            primAttrs = gtPrim->getPointAttributes();
        }

        if( !primAttrs ) {
            primAttrs = gtPrim->getUniformAttributes();
        }
        if( !primAttrs ) {
            primAttrs = gtPrim->getDetailAttributes();
        }

        GT_DataArrayHandle dah;
        if( primAttrs ) {
	    for (auto &&path_attr_name : m_pathAttrNames)
	    {
		dah = primAttrs->get( path_attr_name );
		if( dah && dah->isValid() ) {
		    GT_String s = dah->getS(0);
		    if( UTisstring(s) ) {
			primName = s;
			break;
		    }
		}
	    }
        }
    }

    TfToken purpose = m_overridePurpose;
    {
	GT_Owner own = GT_OWNER_PRIMITIVE;
	GT_DataArrayHandle dah =
	    gtPrim->findAttribute( GUSD_PURPOSE_ATTR, own, 0 );
	if( dah && dah->isValid() ) {
	    purpose = TfToken(dah->getS(0));
	}
    }

    if (primType == GT_PRIM_AGENTS)
    {
        auto agent_collection =
            UTverify_cast<const GT_GEOPackedAgent *>(gtPrim.get());

        GT_GEOAttributeFilter attrib_filter;
        GT_GEODetailList detail(agent_collection->getDetail());

        GT_AttributeListHandle detail_attribs =
            detail.getDetailAttributes(attrib_filter);
        GT_AttributeMapHandle detail_map = detail_attribs->getMap();

        GT_AttributeListHandle instance_attribs =
            detail.getPrimitiveVertexAttributes(
                attrib_filter, agent_collection->primOffsets(),
                agent_collection->vtxOffsets(),
                GT_GEODetailList::GEO_INCLUDE_POINT);
        GT_AttributeMapHandle instance_map = instance_attribs->getMap();

        GT_AttributeMerge attrib_map(instance_map, detail_map);

        for (exint i = 0, n = agent_collection->getNumAgents(); i < n; ++i)
        {
            const GU_PrimPacked *packed_prim =
                agent_collection->getPackedAgent(i);
            const GU_Agent *agent =
                UTverify_cast<const GU_Agent *>(packed_prim->sharedImplementation());
            const GU_AgentDefinition *defn = &agent->definition();

            GT_PrimAgentDefinitionPtr defn_prim;
            auto it = m_knownAgentDefs.find(defn);

            if (it != m_knownAgentDefs.end())
            {
                defn_prim = it->second;
            }
            else if (
                    m_handleAgents != GEO_AGENT_SKELS
                    && m_handleAgents != GEO_AGENT_SKELROOTS)
            {
                // If we haven't seen the agent definition before, add a
                // primitive that will enclose the skeleton, shape library,
                // etc.
                // The agent definition doesn't need to be translated when only
                // importing animation.
                const GU_AgentRigConstPtr &rig = defn->rig();
                if (!rig)
                    continue;

                // Add a prim enclosing all of the agent definitions.
                SdfPath definition_root(GEO_AgentPrimTokens->agentdefinitions);

                // Attempt to find a name for the agent definition from the
                // common 'agentname' attribute.
                GT_Owner agentname_owner;
                GT_DataArrayHandle agentname_attrib =
                    agent_collection->fetchAttributeData("agentname",
                                                         agentname_owner);
                UT_StringHolder agentname;
                if (agentname_attrib)
                    agentname = agentname_attrib->getS(0);

                SdfPath definition_path;
                if (agentname)
                {
                    definition_path
                            = definition_root.AppendChild(TfToken(agentname));
                }
                else
                {
                    UT_WorkBuffer buf;
                    buf.format("definition_{0}", m_knownAgentDefs.size());
                    definition_path =
                        definition_root.AppendChild(TfToken(buf.buffer()));
                }

                const bool import_shapes
                        = (m_handleAgents == GEO_AGENT_INSTANCED_SKELROOTS);
                const bool import_skels
                        = (m_handleAgents != GEO_AGENT_SKELANIMATIONS);

                // Figure out how many Skeleton prims we need to create.
                UT_Array<GT_PrimSkeletonPtr> skeletons;
                UT_Map<exint, exint> shape_to_skeleton;
                GEObuildUsdSkeletons(
                        *defn, geoBuildAgentRestPose(*agent), import_shapes,
                        skeletons, shape_to_skeleton);

                // Add the agent definition primitive with an explicitly chosen
                // path.
                defn_prim = UTmakeIntrusive<GT_PrimAgentDefinition>(
                        defn, m_pathPrefix.AppendPath(definition_path),
                        skeletons, shape_to_skeleton);

                if (import_skels)
                {
                    for (GT_PrimSkeletonPtr &skel_prim : skeletons)
                    {
                        SdfPath skel_path = definition_path.AppendChild(
                                GEO_AgentPrimTokens->skeleton);

                        GEO_PathHandle path = m_collector.add(
                                m_pathPrefix.AppendPath(skel_path),
                                /* addNumericSuffix */ false, skel_prim,
                                UT_Matrix4D::getIdentityMatrix(), m_topologyId,
                                purpose, m_agentShapeInfo);
                        skel_prim->setPath(path);
                    }

                    SdfPath prev_override_path = m_overridePath;
                    m_overridePath = definition_path;
                    addPrimitive(defn_prim);
                    m_overridePath = prev_override_path;
                }

                const GU_AgentShapeLibConstPtr &shapelib = defn->shapeLibrary();
                if (shapelib && import_shapes)
                {
                    // Add each of shapes as prims nested inside the agent
                    // definition.
                    SdfPath shapelib_path = definition_path.AppendChild(
                            GEO_AgentPrimTokens->shapelibrary);

                    UT_Array<GEO_AgentShapeInfoPtr> shapes_to_import;
                    for (auto &&shape_name : GEOfindShapesToImport(*defn))
                    {
                        const GU_AgentShapeLib::ShapePtr shape
                                = shapelib->findShape(shape_name);
                        const exint skel_id
                                = shape_to_skeleton.at(shape->uniqueId());

                        shapes_to_import.append(
                                UTmakeIntrusive<GEO_AgentShapeInfo>(
                                        defn, shape_name, skeletons[skel_id],
                                        nullptr));
                    }

                    refineAgentShapes(
                            gtPrim, m_pathPrefix.AppendPath(shapelib_path),
                            *defn, shapes_to_import);
                }

                // Record the prim for this agent definition.
                m_knownAgentDefs.emplace(defn, defn_prim);
            }

            // Add a primitive for the agent instance.
            auto agent_instance = UTmakeIntrusive<GT_PrimAgentInstance>(
                    agent_collection->getDetail(), agent,
                    GT_AttributeList::createConstantMerge(
                            attrib_map, instance_attribs, i, detail_attribs));
            if (defn_prim)
                agent_instance->setDefinitionPrim(defn_prim);

            UT_Matrix4D agent_xform;
            packed_prim->getFullTransform4(agent_xform);
            agent_instance->setPrimitiveTransform(
                new GT_Transform(&agent_xform, 1));

            addPrimitive(agent_instance);
        }

        return;
    }

    if( primName.empty() &&
        primType == GusdGT_PackedUSD::getStaticPrimitiveType() )
    {
        auto packedUSD = static_cast<const GusdGT_PackedUSD *>(gtPrim.get());
        SdfPath path = packedUSD->getPrimPath().StripAllVariantSelections();
	primName = path.GetString();
    }

    // If the prim path was not explicitly set, try to come up with a reasonable
    // default.
    bool addNumericSuffix = false;
    if( primName.empty() )
    {
        if( primType == GT_PRIM_POINT_MESH ||
	    primType == GT_PRIM_PARTICLE )
            primName = "points";
        else if( primType == GT_PRIM_POLYGON_MESH ||
		 primType == GT_PRIM_SUBDIVISION_MESH )
            primName = "mesh";
        else if( primType == GT_PRIM_NUPATCH )
            primName = "patch";
        else if( primType == GT_PRIM_CURVE_MESH )
            primName = "curve";
        else if( primType == GT_PRIM_SPHERE )
            primName = "sphere";
        else if( primType == GT_PRIM_TUBE )
        {
            auto tube = UTverify_cast<const GT_PrimTube *>(gtPrim.get());
            if (GEOisCone(*tube))
                primName = "cone";
            else
                primName = "cylinder";
        }
        else if(const char *n = GusdPrimWrapper::getUsdName( primType ))
            primName = n;
        else if( primType == GT_PRIM_VOXEL_VOLUME ||
		 primType == GT_PRIM_VDB_VOLUME )
            primName = "field";
        else
            primName = "obj";

        if( !primName.empty() ) {
            addNumericSuffix = true;
        }
    }

    std::string primPath = createPrimPath(primName, m_pathPrefix,
                                          m_prefixAbsolutePaths);

    if( primType == GT_PRIM_INSTANCE )
    {
	auto inst = UTverify_cast<const GT_PrimInstance*>(gtPrim.get());
	const GT_PrimitiveHandle geometry = inst->geometry();

        if ( geometry->getPrimitiveType() == GT_GEO_PACKED )
        {
            auto gtpacked = UTverify_cast<GT_GEOPrimPacked *>(geometry.get());
            GA_PrimitiveTypeId packed_type = gtpacked->getPrim()->getTypeId();

            if (m_handlePackedPrims == GEO_PACKED_POINTINSTANCER)
            {
                UT_StringArray instancer_paths;
                UT_Array<UT_Array<exint>> instancer_indices;
                geoPartitionInstances(*inst, instancer_paths,
                                      instancer_indices);

                for (exint i = 0, n = instancer_paths.entries(); i < n; ++i)
                {
                    // Set up the point instancer prim for this path, and
                    // ensure a prototype exists for the geometry.
                    UT_IntrusivePtr<GT_PrimPointInstancer> instancer =
                        addPointInstancer(instancer_paths[i], purpose);

                    const int proto_index = addPointInstancerPrototype(
                        *instancer, *gtpacked, purpose, primPath, primName,
                        addNumericSuffix);

                    GT_AttributeListHandle uniform =
                        inst->getUniformAttributes();
                    GT_TransformArrayHandle xforms = inst->transforms();
                    UT_SmallArray<exint> invisible_instances;

                    // Unless all the instances are going into the same point
                    // instancer, extract the transforms and uniform attribute
                    // values for this partition.
                    if (n != 1)
                    {
                        const UT_Array<exint> &indices = instancer_indices[i];

                        GT_DataArrayHandle indirect = new GT_DANumeric<exint>(
                            indices.data(), indices.entries(), 1);
                        uniform = uniform->createIndirect(indirect);

                        xforms = new GT_TransformArray();
                        xforms->setEntries(indices.entries());
                        for (exint j = 0; j < indices.entries(); ++j)
                        {
                            const exint idx = indices[j];
                            xforms->set(j, inst->transforms()->get(idx));

                            if (!GEOisVisible(*gtpacked, uniform, idx))
                                invisible_instances.append(idx);
                        }
                    }
                    else
                    {
                        // If we have a trivial list of all instances, build
                        // the visibility array.
                        for (exint j = 0; j < inst->entries(); ++j)
                        {
                            if (!GEOisVisible(*gtpacked, uniform, j))
                                invisible_instances.append(j);
                        }
                    }

                    // Register the instances for this prototype.
                    instancer->addInstances(proto_index, *xforms,
                                            invisible_instances, uniform,
                                            inst->getDetailAttributes());
                }
            }
            else
            {
                GU_ConstDetailHandle gdh;
                if (packed_type != GU_PackedDisk::typeId())
                    gdh = gtpacked->getPackedDetail();

                // Set up the prototype prim when doing native instancing.
                GEO_PathHandle prototype_path;
                if (m_handlePackedPrims == GEO_PACKED_NATIVEINSTANCES &&
                    packed_type != GU_PackedDisk::typeId())
                {
                    prototype_path = addNativePrototype(
                        *gtpacked, purpose, primPath, addNumericSuffix);
                }

                GT_AttributeMapHandle uniform_map;
                if (inst->uniform())
                    uniform_map = inst->uniform()->getMap();

                GT_AttributeMapHandle detail_map;
                if (inst->detail())
                    detail_map = inst->detail()->getMap();

                GT_AttributeMerge attrib_map(uniform_map, detail_map);

                for (GT_Size i = 0; i < inst->transforms()->entries(); ++i)
                {
                    // Create an entry for the USD Xform prim that represents
                    // the packed prim itself and the top-level transform &
                    // attribs.
                    GT_TransformHandle xform_h = inst->transforms()->get(i);
                    UT_Matrix4D xform;
                    xform_h->getMatrix(xform);

                    GT_AttributeListHandle attribs =
                        GT_AttributeList::createConstantMerge(
                            attrib_map, inst->uniform(), i, inst->detail());

                    if (m_handlePackedPrims == GEO_PACKED_UNPACK)
                    {
                        // If we don't need any additional hierarchy, just
                        // continue refining the packed primitives' contents.
                        auto unpacked_detail = geoUnpackAndTransferAttribs(
                                *gtpacked, attribs, m_refineParms);

                        GEO_FileRefiner subRefiner = createSubRefiner(
                                m_pathPrefix, m_pathAttrNames,
                                m_agentShapeInfo);
                        subRefiner.refineDetail(
                                unpacked_detail, m_refineParms, xform_h);

                        continue;
                    }

                    const bool visible = GEOisVisible(
                        *gtpacked, inst->uniform(), i);
                    const bool draw_bounds = GEOdrawBounds(*gtpacked, i);
                    UT_IntrusivePtr<GT_PrimPackedInstance> packed_instance =
                        new GT_PrimPackedInstance(gtpacked, xform_h, attribs,
                                                  visible, draw_bounds);

                    GEO_PathHandle newPath = m_collector.add(
                            SdfPath(primPath), addNumericSuffix,
                            packed_instance, xform, m_topologyId, purpose,
                            m_agentShapeInfo);

                    if (packed_type != GU_PackedDisk::typeId() && gdh.isValid())
                    {
                        if (m_handlePackedPrims == GEO_PACKED_NATIVEINSTANCES)
                        {
                            // Create an instance of the prototype prim, which
                            // has the embedded geometry.
                            packed_instance->setPrototypePath(prototype_path);
                        }
                        else // GEO_PACKED_XFORMS
                        {
                            // Refine the embedded geometry underneath.
                            GEO_FileRefiner subRefiner = createSubRefiner(
                                    *newPath, m_pathAttrNames);
                            subRefiner.refineDetail(gdh, m_refineParms);
                        }
                    }
                }
            }

            return;
        }
    }
    else if (primType == GT_GEO_PACKED)
    {
        // Handle other types of packed primitives that don't refine to
        // GT_PRIM_INSTANCE.
        auto gt_packed = UTverify_cast<GT_GEOPrimPacked *>(gtPrim.get());
        GT_TransformHandle gt_xform = geoGetPackedTransform(*gt_packed);

        auto instance_attribs = gt_packed->getInstanceAttributes();
        const bool visible = GEOisVisible(
            *gt_packed, gt_packed->getInstanceAttributes(), 0);

        if (m_handlePackedPrims == GEO_PACKED_UNPACK)
        {
            // If we don't need any additional hierarchy, just continue
            // refining the packed primitives' contents.
            auto unpacked_detail = geoUnpackAndTransferAttribs(
                    *gt_packed, instance_attribs, m_refineParms);

            GEO_FileRefiner subRefiner = createSubRefiner(
                    m_pathPrefix, m_pathAttrNames, m_agentShapeInfo);
            subRefiner.refineDetail(unpacked_detail, m_refineParms, gt_xform);
        }
        else if (m_handlePackedPrims == GEO_PACKED_POINTINSTANCER)
        {
            UT_StringHolder instancer_path
                    = geoGetInstancerPath(instance_attribs);
            UT_IntrusivePtr<GT_PrimPointInstancer> instancer =
                addPointInstancer(instancer_path, purpose);

            const int proto_index = addPointInstancerPrototype(
                *instancer, *gt_packed, purpose, primPath, primName,
                addNumericSuffix);

            GT_TransformArray xforms;
            xforms.append(gt_xform);

            UT_SmallArray<exint> invisible_instances;
            if (!visible)
                invisible_instances.append(0);

            instancer->addInstances(
                    proto_index, xforms, invisible_instances, instance_attribs,
                    nullptr);
        }
        else
        {
            // Create native instances, or xform prims with no instancing.
            UT_Matrix4D xform;
            gt_xform->getMatrix(xform);

            auto packed_instance = UTmakeIntrusive<GT_PrimPackedInstance>(
                    gt_packed, gt_xform, instance_attribs, visible,
                    GEOdrawBounds(*gt_packed));
            GEO_PathHandle path = m_collector.add(
                    SdfPath(primPath), false, packed_instance, xform,
                    m_topologyId, m_overridePurpose, m_agentShapeInfo);

            if (m_handlePackedPrims == GEO_PACKED_NATIVEINSTANCES)
            {
                packed_instance->setPrototypePath(addNativePrototype(
                    *gt_packed, purpose, primPath, addNumericSuffix));
            }
            else // GEO_PACKED_XFORMS
            {
                GU_ConstDetailHandle embedded_geo
                        = geoGetPackedGeometry(*gt_packed);

                GEO_FileRefiner sub_refiner = createSubRefiner(
                        *path, m_pathAttrNames, m_agentShapeInfo);
                sub_refiner.refineDetail(embedded_geo, m_refineParms);
            }
        }
        return;
    }
    else if (primType == GT_PrimAgentInstance::getStaticPrimitiveType())
    {
        auto agent_instance
                = UTverify_cast<GT_PrimAgentInstance *>(gtPrim.get());
        const GU_Agent &agent = agent_instance->getAgent();

        UT_Matrix4D xform;
        gtPrim->getPrimitiveTransform()->getMatrix(xform);
        GEO_PathHandle agent_path = m_collector.add(
                SdfPath(primPath), addNumericSuffix, gtPrim, xform,
                m_topologyId, purpose, m_agentShapeInfo);

        UT_SmallArray<GT_PrimSkeletonPtr> skeletons;
        if (m_handleAgents == GEO_AGENT_SKELS
            || m_handleAgents == GEO_AGENT_SKELROOTS)
        {
            // Once we know the agent instance's path, create the skeleton prim
            // underneath.
            const bool import_shapes = (m_handleAgents == GEO_AGENT_SKELROOTS);

            UT_Map<exint, exint> shape_to_skeleton;
            GEObuildUsdSkeletons(
                    agent.definition(), geoBuildAgentRestPose(agent),
                    import_shapes, skeletons, shape_to_skeleton);

            for (auto &&skel_prim : skeletons)
            {
                SdfPath skel_path(createPrimPath(
                        geoGetSkeletonPath(*gtPrim), *agent_path,
                        m_prefixAbsolutePaths));

                GEO_PathHandle path = m_collector.add(
                        skel_path, /* addNumericSuffix */ false, skel_prim,
                        UT_Matrix4D::getIdentityMatrix(), m_topologyId, purpose,
                        m_agentShapeInfo);
                skel_prim->setPath(path);
            }

            // Import only the shapes from the agent's current layer.
            if (import_shapes)
            {
                GU_AgentDefinitionConstPtr defn = &agent.definition();
                UT_Array<GEO_AgentShapeInfoPtr> shapes_to_import;

                for (const GU_AgentLayerConstPtr &layer :
                     agent.getCurrentLayers())
                {
                    for (auto &&binding : *layer)
                    {
                        const exint skel_id
                                = shape_to_skeleton.at(binding.shapeId());
                        shapes_to_import.append(
                                UTmakeIntrusive<GEO_AgentShapeInfo>(
                                        defn, binding.shapeName(),
                                        skeletons[skel_id], &binding));
                    }
                }

                refineAgentShapes(
                        gtPrim, *agent_path, agent.definition(),
                        shapes_to_import);
            }
        }
        else
        {
            // The SkelAnimation prim can just reference the first skeleton
            // prim from the agent definition. Any extra skeletons only have a
            // different bind pose.
            UT_ASSERT(agent_instance->getDefinitionPrim());
            auto &&defn_prim = agent_instance->getDefinitionPrim();
            UT_ASSERT(!defn_prim->getSkeletons().isEmpty());
            skeletons = defn_prim->getSkeletons();
        }

        UT_ASSERT(!skeletons.isEmpty());
        GT_PrimSkeletonPtr exemplar_skel = skeletons[0];

        // Set up the SkelAnimation prim.
        auto anim_prim = UTmakeIntrusive<GT_PrimSkelAnimation>(
                &agent, exemplar_skel);
        SdfPath target_anim_path(
                createPrimPath(geoGetSkelAnimationPath(*gtPrim), *agent_path,
                               m_prefixAbsolutePaths));

        GEO_PathHandle anim_path = m_collector.add(
                target_anim_path, /* addNumericSuffix */ false, anim_prim,
                UT_Matrix4D::getIdentityMatrix(), m_topologyId, purpose,
                m_agentShapeInfo);
        anim_prim->setPath(anim_path);
        agent_instance->setAnimPath(anim_path);

        // Bind the non-instanced skeletons to their animation.
        if (m_handleAgents == GEO_AGENT_SKELS
            || m_handleAgents == GEO_AGENT_SKELROOTS)
        {
            for (const GT_PrimSkeletonPtr &skel : skeletons)
                skel->setAnimPath(anim_path);
        }

        return;
    }
    else if (primType == GT_PRIM_VOXEL_VOLUME || primType == GT_PRIM_VDB_VOLUME)
    {
        const bool has_name = (!primName.empty() && primName[0] != '/');

        UT_IntrusivePtr<GT_PrimVolumeCollection> volume = addVolumeCollection(
            *gtPrim, has_name ? primName : std::string(), purpose);

        // Unless the field prim has an explicit path set, author it as a child
        // of the volume prim (suggested in the schema).
        SdfPath field_path;
        if (has_name)
        {
            UT_String validname(primName);
            HUSDmakeValidUsdName(validname, false);

            field_path = volume->getPath()->AppendChild(TfToken(validname));
        }
        else
            field_path = SdfPath(primPath);

        UT_Matrix4D xform;
        gtPrim->getPrimitiveTransform()->getMatrix(xform);

        GEO_PathHandle new_path = m_collector.add(
                field_path, addNumericSuffix, gtPrim, xform, m_topologyId,
                purpose, m_agentShapeInfo);
        volume->addField(new_path, primName, gtPrim);
        m_collector.registerVolumeGeometry(*gtPrim);

        return;
    }
    else if (
            primType == GT_GEO_PRIMTPSURF
            && m_handleNurbsSurfs == GEO_NURBSSURF_PATCHES)
    {
        auto surf = UTverify_cast<const GT_GEOPrimTPSurf*>(gtPrim.get());
        addPrimitive(surf->buildNuPatch());
        return;
    }

    if( GEOisGTPrimSupported(gtPrim) )
    {
        UT_Matrix4D xform;
	gtPrim->getPrimitiveTransform()->getMatrix(xform);

        if (primType == GT_PRIM_POLYGON_MESH)
            GEOconvertMeshToSubd(gtPrim, m_markMeshesAsSubd);

        m_collector.add(
                SdfPath(primPath), addNumericSuffix, gtPrim, xform,
                m_topologyId, purpose, m_agentShapeInfo);
    }
    else
    {
        bool prev_subd = m_markMeshesAsSubd;
        if (GEOshouldRefineToSubdMesh(primType))
            m_markMeshesAsSubd = true;

        gtPrim->refine(*this, &m_refineParms);
        m_markMeshesAsSubd = prev_subd;
    }
}

GEO_PathHandle
GEO_FileRefinerCollector::add(
        const SdfPath &path,
        bool addNumericSuffix,
        GT_PrimitiveHandle prim,
        const UT_Matrix4D &xform,
        GA_DataId topologyId,
        const TfToken &purpose,
        const GEO_AgentShapeInfoPtr &agentShapeInfo)
{
    UT_ASSERT(path.IsAbsolutePath());

    // If addNumericSuffix is true, use the name directly unless there
    // is a conflict. Otherwise add a numeric suffix to keep names unique.
    size_t count = 0;
    auto it = m_names.find( path );
    if( it == m_names.end() ) {
        // Name has not been used before
        m_names[path] = NameInfo();
        if( !addNumericSuffix ) {
            auto path_handle = UTmakeShared<SdfPath>(path);
            m_gprims.push_back(GEO_FileGprimArrayEntry(
                path_handle, prim, xform, topologyId, purpose, agentShapeInfo));
            return path_handle;
        }
    }
    else {
        if( !addNumericSuffix && it->second.count == 0 ) {

            for (GEO_FileGprimArrayEntry &entry : m_gprims) {
                if( *entry.path == path ) {
                    // We have a name conflict. Go back and change the 
                    // name of the first prim to use this name.
                    *entry.path = SdfPath( path.GetString() + "_0" );
                }
                else if( TfStringStartsWith(entry.path->GetString(),
					    path.GetString()) ) {
                    *entry.path = SdfPath(path.GetString() + "_0" +
			entry.path->GetString().substr(
			    path.GetString().length()));
                }
            }
        }
        ++it->second.count;
        count = it->second.count;
    }

    // Add a numeric suffix to get a unique name
    auto newPath =
        UTmakeShared<SdfPath>(TfStringPrintf("%s_%zu", path.GetText(), count));

    m_gprims.push_back(GEO_FileGprimArrayEntry(
            newPath, prim, xform, topologyId, purpose, agentShapeInfo));
    return newPath;
}

void
GEO_FileRefinerCollector::finish( GEO_FileRefiner& refiner )
{
}

void
GEO_FileRefinerCollector::registerVolumeGeometry(
        const GT_Primitive &gt_volume)
{
    GU_ConstDetailHandle gdh;
    if (gt_volume.getPrimitiveType() == GT_PRIM_VOXEL_VOLUME)
        gdh = UTverify_cast<const GT_PrimVolume *>(&gt_volume)->getDetail();
    else if (gt_volume.getPrimitiveType() == GT_PRIM_VDB_VOLUME)
        gdh = UTverify_cast<const GT_PrimVDB *>(&gt_volume)->getDetail();
    else
    {
        UT_ASSERT_MSG(false, "Unexpected GT volume type");
        return;
    }

    const GU_Detail *gdp = gdh.gdp();
    if (myVolumeFilePaths.contains(gdp))
        return; // Already registered, nothing to do.

    // When registering the locked geo, use the original file path but with an
    // extra unique argument for the unpacked detail.
    // We don't need to use the original arguments since the detail pointer is
    // enough to determine uniqueness, and this allows sharing if the same
    // unpacked detail is produced at different SOP cook times.
    // (Packed prims do an addPreserveRequest() so any SOP recooks will produce
    // a new detail).
    UT_WorkBuffer buf;
    buf.sprintf("%p", gdp);
    XUSD_LockedGeoArgs args;
    args["unpack_id"] = buf.toStdString();

    XUSD_LockedGeoPtr locked_geo = XUSD_LockedGeoRegistry::createLockedGeo(
            myPrimaryFilePath, args, gdh);

    myUnpackedGeos.append(locked_geo);
    myVolumeFilePaths[gdp] = SdfAssetPath(SdfLayer::CreateIdentifier(
            myPrimaryFilePath + HUSD_Constants::getVolumeSopSuffix().toStdString(),
            args));
}

PXR_NAMESPACE_CLOSE_SCOPE
