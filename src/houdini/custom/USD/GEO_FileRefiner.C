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
#include "GEO_FilePrimUtils.h"
#include "GEO_FilePrimVolumeUtils.h"
#include <gusd/purpose.h>
#include <gusd/primWrapper.h>
#include <gusd/GU_PackedUSD.h>
#include <gusd/GT_PackedUSD.h>
#include <gusd/GU_USD.h>
#include <gusd/stageCache.h>

#include <HUSD/HUSD_Utils.h>
#include <GOP/GOP_Manager.h>
#include <GU/GU_Agent.h>
#include <GU/GU_PackedDisk.h>
#include <GEO/GEO_Primitive.h>
#include <GEO/GEO_PrimitiveP.h>
#include <GT/GT_AttributeMerge.h>
#include <GT/GT_PrimCollect.h>
#include <GT/GT_PrimInstance.h>
#include <GT/GT_GEOAttributeFilter.h>
#include <GT/GT_GEOPackedAgent.h>
#include <GT/GT_GEOPrimCollect.h>
#include <GT/GT_GEOPrimPacked.h>
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
    const UT_StringArray&   pathAttrNames )
    : m_collector( collector )
    , m_pathPrefix( pathPrefix )
    , m_pathAttrNames( pathAttrNames )
    , m_topologyId( GA_INVALID_DATAID )
    , m_markMeshesAsSubd( false )
    , m_handleUsdPackedPrims( GEO_USD_PACKED_IGNORE )
    , m_handlePackedPrims( GEO_PACKED_XFORMS )
{
}

GEO_FileRefiner::~GEO_FileRefiner()
{
}

GEO_FileRefiner
GEO_FileRefiner::createSubRefiner(
    const SdfPath &pathPrefix, const UT_StringArray &pathAttrNames,
    const GT_PrimitiveHandle &src_prim,
    const GEO_AgentShapeInfo &agentShapeInfo)
{
    GEO_FileRefiner subrefiner(m_collector, pathPrefix, pathAttrNames);
    subrefiner.m_handleUsdPackedPrims = m_handleUsdPackedPrims;
    subrefiner.m_handlePackedPrims = m_handlePackedPrims;
    subrefiner.m_agentShapeInfo =
        agentShapeInfo ? agentShapeInfo : m_agentShapeInfo;

    subrefiner.m_writeCtrlFlags = m_writeCtrlFlags;
    subrefiner.m_writeCtrlFlags.update(src_prim);
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
    const GU_ConstDetailHandle& detail,
    const GT_RefineParms& refineParms )
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

    GOP_Manager groupparse;
    const GA_PrimitiveGroup *importGroup = nullptr;
    UT_UniquePtr<GA_PrimitiveGroup> nonUsdGroup(
        gdp->newDetachedPrimitiveGroup());
    GA_PrimitiveTypeId packedusd_typeid = GusdGU_PackedUSD::typeId();

    geoFindStringAttribs(*gdp, GA_ATTRIB_PRIMITIVE, m_pathAttrNames,
                         partitionAttrs);

    if (m_importGroup.isstring())
	importGroup = groupparse.parsePrimitiveGroups(m_importGroup,
	    GOP_Manager::GroupCreator(gdp));

    // Parse the subdivision group if subdivision is enabled.
    const bool subd = m_refineParms.getPolysAsSubdivision();
    const GA_PrimitiveGroup *subdGroup = nullptr;
    if (subd && m_subdGroup.isstring())
    {
        subdGroup = groupparse.parsePrimitiveGroups(
            m_subdGroup, GOP_Manager::GroupCreator(gdp));
    }

    nonUsdGroup->addAll();
    if (m_handleUsdPackedPrims == GEO_USD_PACKED_IGNORE)
    {
	GA_Range allPrimRange = gdp->getPrimitiveRange(importGroup);
	for (auto primIt = allPrimRange.begin(); !primIt.atEnd(); ++primIt)
	{
	    GEO_ConstPrimitiveP prim(gdp, *primIt);

	    if (prim->getTypeId() == packedusd_typeid)
		nonUsdGroup->remove(prim);
	}
    }
    if (importGroup)
	*nonUsdGroup &= *importGroup;

    // If there is a subdivision group, split based on that group and then
    // further partition based on the partition attributes.
    UT_UniquePtr<GA_PrimitiveGroup> nonUsdSubdGroup;
    UT_Array<Partition> partitions;
    if (!subdGroup)
    {
        geoPartitionRange(*gdp, gdp->getPrimitiveRange(nonUsdGroup.get()), subd,
                          partitionAttrs, partitions);
    }
    else
    {
        nonUsdSubdGroup.reset(gdp->newDetachedPrimitiveGroup());
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
	    detailPrim->refine(*this, &m_refineParms);
    }

    // Unless a primitive group was specified, refine the unused points
    // (possibly partitioned by an attribute).
    GA_OffsetList unused_pts;
    if (!importGroup && gdp->findUnusedPoints(&unused_pts))
    {
        partitions.clear();
        partitionAttrs.clear();

        geoFindStringAttribs(*gdp, GA_ATTRIB_POINT, m_pathAttrNames,
                             partitionAttrs);

        GA_Range pt_range(gdp->getPointMap(), unused_pts);
        geoPartitionRange(*gdp, pt_range, false, partitionAttrs, partitions);

        for (const Partition &partition : partitions)
        {
            GT_PrimitiveHandle prim =
                GT_GEODetail::makePointMesh(detail, &partition.myRange);
            addPrimitive(prim);
        }
    }

    m_overridePath = SdfPath();
    m_overridePurpose = TfToken();
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
GEO_FileRefiner::createPrimPath(const std::string& primName)
{
    std::string primPath;

    if( !primName.empty() && primName[0] == '/' )
    {
        // Use an explicit absolute path
        primPath = primName;
    }
    else
    {
        // add prefix to relative path
        primPath = m_pathPrefix.GetString();
        if( !primName.empty() )
	{
            if( primPath.empty() || primPath.back() != '/' )
                primPath += "/";
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

/// Returns the 'usdinstancerpath' string attribute.
static GT_DataArrayHandle
geoFindInstancerPathAttrib(const GT_Primitive &prim, GT_Owner &owner)
{
    static constexpr UT_StringLit theInstancerPathAttrib("usdinstancerpath");

    GT_DataArrayHandle path_attrib =
        prim.findAttribute(theInstancerPathAttrib.asRef(), owner, 0);
    if (path_attrib && path_attrib->getStorage() != GT_STORE_STRING)
        path_attrib.reset();

    return path_attrib;
}

/// Returns the instancer path that should be used for the given packed
/// primitive.
static UT_StringHolder
geoGetInstancerPath(const GT_Primitive &prim)
{
    GT_Owner owner;
    GT_DataArrayHandle path_attrib = geoFindInstancerPathAttrib(prim, owner);

    if (path_attrib)
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
    SdfPath instancer_path(createPrimPath(orig_instancer_path.toStdString()));

    UT_IntrusivePtr<GT_PrimPointInstancer> &instancer =
        m_pointInstancers[instancer_path];
    if (!instancer)
    {
        instancer.reset(new GT_PrimPointInstancer());
        GEO_PathHandle path =
            m_collector.add(instancer_path,
                            /* addNumericSuffix */ false, instancer,
                            UT_Matrix4D::getIdentityMatrix(), m_topologyId,
                            purpose, m_writeCtrlFlags, m_agentShapeInfo);
        instancer->setPath(path);
    }

    return instancer;
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
    if (!primName.empty() && primName[0] != '/')
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

            GEO_PathHandle path = m_collector.add(
                init_prototype_path, addNumericSuffix, prototype_prim,
                UT_Matrix4D::getIdentityMatrix(), m_topologyId, purpose,
                m_writeCtrlFlags, m_agentShapeInfo);

            // Refine the embedded geometry, unless it is a file reference.
            GA_PrimitiveTypeId packed_type = gtpacked.getPrim()->getTypeId();
            if (packed_type != GU_PackedDisk::typeId())
            {
                GEO_FileRefiner sub_refiner = createSubRefiner(
                    *path, m_pathAttrNames, &gtpacked);

                GT_PrimitiveHandle embedded_geo;
                GT_TransformHandle gt_xform;
                gtpacked.geometryAndTransform(
                    &m_refineParms, embedded_geo, gt_xform);
                embedded_geo->refine(sub_refiner, &m_refineParms);
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

        GEO_PathHandle prototype_path = m_collector.add(
            path, addNumericSuffix, prototype_prim,
            UT_Matrix4D::getIdentityMatrix(), m_topologyId, purpose,
            m_writeCtrlFlags, m_agentShapeInfo);

        GEO_FileRefiner sub_refiner =
            createSubRefiner(*prototype_path, m_pathAttrNames, &gtpacked);

        GT_PrimitiveHandle embedded_geo;
        GT_TransformHandle gt_xform;
        gtpacked.geometryAndTransform(&m_refineParms, embedded_geo, gt_xform);
        embedded_geo->refine(sub_refiner, &m_refineParms);

        return prototype_path;
    });
}

UT_IntrusivePtr<GT_PrimVolumeCollection>
GEO_FileRefiner::addVolumeCollection(const GT_Primitive &field_prim,
                                     const std::string &field_name,
                                     const TfToken &purpose)
{
    static constexpr UT_StringLit theVolumePathAttrib("usdvolumepath");

    GT_Owner owner;
    GT_DataArrayHandle path_attrib =
        field_prim.findAttribute(theVolumePathAttrib.asRef(), owner, 0);
    if (path_attrib && path_attrib->getStorage() != GT_STORE_STRING)
        path_attrib.reset();

    bool custom_path = true;
    UT_StringHolder orig_volume_path;
    if (path_attrib)
        orig_volume_path = path_attrib->getS(0);

    if (!orig_volume_path)
    {
        custom_path = false;
        orig_volume_path =
            GusdUSD_Utils::TokenToStringHolder(GEO_VolumePrimTokens->volume);
    }

    SdfPath target_volume_path(createPrimPath(orig_volume_path.toStdString()));
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
            m_writeCtrlFlags, m_agentShapeInfo);
        volume->setPath(volume_path);
    }

    return volume;
}

static SYS_FORCE_INLINE bool
GEOisVisible(const GT_GEOPrimPacked &gtpacked, int i)
{
    return gtpacked.getViewportLOD(i) != GEO_VIEWPORT_HIDDEN;
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
                UTverify_cast<const GU_Agent *>(packed_prim->implementation());
            const GU_AgentDefinition *defn = &agent->definition();

            SdfPath definition_path;
            auto it = m_knownAgentDefs.find(defn);

            // If we haven't seen the agent definition before, add a primitive
            // that will enclose the skeleton, shape library, etc.
            if (it == m_knownAgentDefs.end())
            {
                const GU_AgentRigConstPtr &rig = defn->rig();
                const GU_AgentShapeLibConstPtr &shapelib = defn->shapeLibrary();
                if (!rig || !shapelib)
                    continue;

                // Add a prim enclosing all of the agent definitions.
                SdfPath definition_root = m_pathPrefix.AppendChild(
                    GEO_AgentPrimTokens->agentdefinitions);

                // Attempt to find a name for the agent definition from the
                // common 'agentname' attribute.
                GT_Owner agentname_owner;
                GT_DataArrayHandle agentname_attrib =
                    agent_collection->fetchAttributeData("agentname",
                                                         agentname_owner);
                if (agentname_attrib)
                {
                    definition_path = definition_root.AppendChild(
                        TfToken(agentname_attrib->getS(0)));
                }
                else
                {
                    UT_WorkBuffer buf;
                    buf.format("definition_{0}", m_knownAgentDefs.size() - 1);
                    definition_path =
                        definition_root.AppendChild(TfToken(buf.buffer()));
                }

                // If there aren't any deforming shapes, we still need a bind
                // pose for the skeleton so that it can be imaged correctly.
                // Just use the current pose of the exemplar agent.
                GU_Agent::Matrix4ArrayConstPtr bind_pose;
                agent->computeWorldTransforms(bind_pose);

                // Add the agent definition primitive with an explicitly chosen
                // path.
                GT_PrimitiveHandle defn_prim =
                    new GT_PrimAgentDefinition(defn, bind_pose);

                SdfPath prev_override_path = m_overridePath;
                m_overridePath = definition_path;
                addPrimitive(defn_prim);
                m_overridePath = prev_override_path;

                // Add each of shapes as prims nested inside the agent
                // definition.
                SdfPath shapelib_path = definition_path.AppendChild(
                    GEO_AgentPrimTokens->shapelibrary);

                GU_ConstDetailHandle shapelib_gdh = shapelib->detail();
                GT_GEODetailList dtl_prim(shapelib_gdh);
                auto detail_attribs =
                    dtl_prim.getDetailAttributes(GT_GEOAttributeFilter());

                for (auto &&entry : *shapelib)
                {
                    UT_String shape_name(entry.first);
                    HUSDmakeValidUsdName(shape_name, false);
                    SdfPath shape_path =
                        shapelib_path.AppendChild(TfToken(shape_name));

                    // Retrieve the packed primitive from the shape library.
                    const GU_AgentShapeLib::ShapePtr &shape = entry.second;
                    auto shape_prim = UTverify_cast<const GU_PrimPacked *>(
                        shapelib_gdh.gdp()->getGEOPrimitive(shape->offset()));
                    UT_ASSERT(shape_prim);

                    UT_IntrusivePtr<GT_GEOPrimPacked> gtpacked =
                        new GT_GEOPrimPacked(shapelib_gdh, shape_prim,
                                             /* transformed */ true,
                                             /* include_packed_attribs */ true);

                    // Set up the top-level primitive for the shape.
                    GEO_PathHandle path = m_collector.add(
                        shape_path, false,
                        new GT_PrimPackedInstance(
                            gtpacked, GT_Transform::identity(),
                            detail_attribs->mergeNewAttributes(
                                gtpacked->getPointAttributes())),
                        UT_Matrix4D::getIdentityMatrix(), m_topologyId,
                        m_overridePurpose, m_writeCtrlFlags,
                        m_agentShapeInfo);

                    // Refine the shape's geometry underneath.
                    GEO_AgentShapeInfo shape_info(defn, entry.first);
                    GEO_FileRefiner sub_refiner =
                        createSubRefiner(*path, {}, gtPrim, shape_info);
                    sub_refiner.refineDetail(
                        entry.second->shapeGeometry(*shapelib), m_refineParms);
                }

                // Record the prim path for this agent definition.
                m_knownAgentDefs.emplace(defn, definition_path);
            }
            else
            {
                definition_path = it->second;
            }

            // Add a primitive for the agent instance.
            GT_PrimitiveHandle agent_instance = new GT_PrimAgentInstance(
                agent_collection->getDetail(), agent, definition_path,
                GT_AttributeList::createConstantMerge(
                    attrib_map, instance_attribs, i, detail_attribs));

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
        else if( primType == GT_PRIM_CURVE_MESH ||
		 primType == GT_PRIM_SUBDIVISION_CURVES )
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

    std::string primPath = createPrimPath(primName);

    TfToken purpose = m_overridePurpose;

    if (purpose.IsEmpty())
	purpose = UsdGeomTokens->default_;

    {
	GT_Owner own = GT_OWNER_PRIMITIVE;
	GT_DataArrayHandle dah =
	    gtPrim->findAttribute( GUSD_PURPOSE_ATTR, own, 0 );
	if( dah && dah->isValid() ) {
	    purpose = TfToken(dah->getS(0));
	}
    }

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

                            if (!GEOisVisible(*gtpacked, idx))
                                invisible_instances.append(idx);
                        }
                    }
                    else
                    {
                        // If we have a trivial list of all instances, build
                        // the visibility array.
                        for (exint j = 0; j < inst->entries(); ++j)
                        {
                            if (!GEOisVisible(*gtpacked, j))
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

                    const bool visible = GEOisVisible(*gtpacked, i);
                    UT_IntrusivePtr<GT_PrimPackedInstance> packed_instance =
                        new GT_PrimPackedInstance(gtpacked, xform_h, attribs,
                                                  visible);

                    GEO_PathHandle newPath = m_collector.add(
                        SdfPath(primPath), addNumericSuffix, packed_instance,
                        xform, m_topologyId, purpose, m_writeCtrlFlags,
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
                                *newPath, m_pathAttrNames, geometry);
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
        GT_PrimitiveHandle embedded_geo;
        GT_TransformHandle gt_xform;
        gt_packed->geometryAndTransform(&m_refineParms, embedded_geo, gt_xform);
        const bool visible = GEOisVisible(*gt_packed, 0);

        if (m_handlePackedPrims == GEO_PACKED_POINTINSTANCER)
        {
            UT_StringHolder instancer_path = geoGetInstancerPath(*gt_packed);
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

            instancer->addInstances(proto_index, xforms, invisible_instances,
                                    gt_packed->getInstanceAttributes(),
                                    nullptr);
        }
        else
        {
            // Create native instances, or xform prims with no instancing.
            UT_Matrix4D xform;
            gt_xform->getMatrix(xform);

            UT_IntrusivePtr<GT_PrimPackedInstance> packed_instance =
                new GT_PrimPackedInstance(gt_packed, gt_xform,
                                          gt_packed->getInstanceAttributes(),
                                          visible);
            GEO_PathHandle path = m_collector.add(
                SdfPath(primPath), false, packed_instance, xform, m_topologyId,
                m_overridePurpose, m_writeCtrlFlags, m_agentShapeInfo);

            if (m_handlePackedPrims == GEO_PACKED_NATIVEINSTANCES)
            {
                packed_instance->setPrototypePath(addNativePrototype(
                    *gt_packed, purpose, primPath, addNumericSuffix));
            }
            else // GEO_PACKED_XFORMS
            {
                GEO_FileRefiner sub_refiner = createSubRefiner(
                    *path, m_pathAttrNames, gtPrim, m_agentShapeInfo);
                embedded_geo->refine(sub_refiner, &m_refineParms);
            }
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
            field_path, addNumericSuffix, gtPrim, xform, m_topologyId, purpose,
            m_writeCtrlFlags, m_agentShapeInfo);
        volume->addField(new_path, primName);

        return;
    }

    if( GEOisGTPrimSupported(gtPrim) )
    {
        UT_Matrix4D xform;
	gtPrim->getPrimitiveTransform()->getMatrix(xform);

        if (primType == GT_PRIM_POLYGON_MESH)
            GEOconvertMeshToSubd(gtPrim, m_markMeshesAsSubd);

        m_collector.add(SdfPath(primPath), addNumericSuffix, gtPrim, xform,
                        m_topologyId, purpose, m_writeCtrlFlags,
                        m_agentShapeInfo);
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
    const SdfPath&              path,
    bool                        addNumericSuffix,
    GT_PrimitiveHandle          prim,
    const UT_Matrix4D&          xform,
    GA_DataId                   topologyId,
    const TfToken &             purpose,
    const GusdWriteCtrlFlags&   writeCtrlFlagsIn,
    const GEO_AgentShapeInfo&   agentShapeInfo )
{
    UT_ASSERT(path.IsAbsolutePath());

    // Update the write control flags from the attributes on the prim
    GusdWriteCtrlFlags writeCtrlFlags = writeCtrlFlagsIn;

    writeCtrlFlags.update( prim );

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
                path_handle, prim, xform, topologyId, purpose, writeCtrlFlags,
                agentShapeInfo));
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

    m_gprims.push_back(GEO_FileGprimArrayEntry(newPath, prim, xform, topologyId,
                                               purpose, writeCtrlFlags,
                                               agentShapeInfo));
    return newPath;
}

void
GEO_FileRefinerCollector::finish( GEO_FileRefiner& refiner )
{
}

PXR_NAMESPACE_CLOSE_SCOPE
