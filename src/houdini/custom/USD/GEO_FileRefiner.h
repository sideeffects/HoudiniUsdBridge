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

#ifndef __GEO_FILEREFINER_H__
#define __GEO_FILEREFINER_H__

#include "GEO_FileUtils.h"
#include "GEO_FilePrimUtils.h"
#include "GEO_FilePrimAgentUtils.h"
#include "GEO_FilePrimInstancerUtils.h"
#include <GT/GT_Refine.h>
#include <GT/GT_RefineParms.h>
#include <GU/GU_AgentDefinition.h>
#include <GU/GU_DetailHandle.h>
#include <UT/UT_SharedPtr.h>
#include <UT/UT_Map.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/token.h>

class GT_GEOPrimPacked;

PXR_NAMESPACE_OPEN_SCOPE

// Class used to refine GT prims so that they can be treated as part of an
// imported USD file.
//
// The basic idea is that the refiner looks at each prim, if it is a type that
// can be understood by USD it adds it to the "gprim array",  if not it
// continues to refine it.
// 
// The refiner supports namespace hierarchy. Some prims types are added to the
// the gprim array and then add thier children as well. Packed prims do this.
// The packed prim becomes a group node in USD.
//
// The refiner calculates the primPath (location in the USD file). This can
// come from an attribute on the prim being refined or it can be computed. The
// computed path is based on a prefix provided by the client, a prim name and
// possibly a hierarchy pf group names supplied by packed prims.
//
// The gprim array can contain prims from several OBJ nodes. The obj nodes
// provide a coordinate space and a set of options. We stash this stuff with
// the prims in the prim array.

class GEO_FileRefinerCollector;
class GT_PrimPointInstancer;
class GT_PrimVolumeCollection;
class XUSD_LockedGeo;
using XUSD_LockedGeoPtr = UT_IntrusivePtr<XUSD_LockedGeo>;

class GEO_FileRefiner : public GT_Refine 
{
public:

    // A struct representing GT prims refined to a USD prim.
    // localXform is the transform from the prim's space to its parent.
    // parentXform is the transform from the prim's parent's space to World.
    struct GEO_FileGprimArrayEntry {
        GEO_PathHandle      path;
        GT_PrimitiveHandle  prim;
        UT_Matrix4D         xform;
        GA_DataId           topologyId;
        TfToken             purpose;
        GEO_AgentShapeInfoPtr agentShapeInfo;

        GEO_FileGprimArrayEntry() : topologyId(GA_INVALID_DATAID) {}
        GEO_FileGprimArrayEntry( 
            const GEO_PathHandle&       path, 
            const GT_PrimitiveHandle&   prim,
            const UT_Matrix4D&          xform,
            GA_DataId                   topologyId,
            const TfToken&              purpose,
            const GEO_AgentShapeInfoPtr& agentShapeInfo )
                : path( path )
                , prim( prim )
                , xform( xform )
                , topologyId( topologyId )
                , purpose(purpose)
                , agentShapeInfo(agentShapeInfo) {}
    };
    using GEO_FileGprimArray = std::vector<GEO_FileGprimArrayEntry>;

    ///////////////////////////////////////////////////////////////////////////

    // Construct a refiner for refining the prims in a detail.
    // Typically the ROP constructs a refiner for its cooked detail, and then
    // as we process GT prims, if a GEO Packed Prim is encountered, We create a
    // new refiner and recurse.  We need to keep track of the transform as we
    // recurse through packed prims. Note that we only write packed prims that
    // have been tagged with a prim path. We kee track of the transform of the
    // last group we wrote in parentToWorldXform \p localToWorldXform is
    // initialized to the OBJ Node's transform by the ROP.
    GEO_FileRefiner(
        GEO_FileRefinerCollector&   collector,
        const SdfPath&          pathPrefix,
        const UT_StringArray&   pathAttrNames,
        bool                    prefixAbsolutePaths);

    ~GEO_FileRefiner() override;

    bool allowThreading() const override { return false; }

    void addPrimitive( const GT_PrimitiveHandle& gtPrim ) override;

    void refineDetail(
            const GU_ConstDetailHandle& detail,
            const GT_RefineParms& parms,
            const GT_TransformHandle& xform = nullptr);

    void refinePrim(
            const GT_Primitive& prim,
            const GT_RefineParms& parms);

    const GEO_FileGprimArray& finish();

    //////////////////////////////////////////////////////////////////////////

    // A string specifying the group of primitives to import (blank means all).
    UT_StringHolder		 m_importGroup;
    GA_AttributeOwner            m_importGroupType = GA_ATTRIB_PRIMITIVE;

    // A string specifying the group of primitives to import as subdivision
    // surfaces.
    UT_StringHolder		 m_subdGroup;

    // Setting to control the processing of USD packed prims.
    GEO_HandleUsdPackedPrims	 m_handleUsdPackedPrims;

    // Setting to control the processing of packed prims.
    GEO_HandlePackedPrims	 m_handlePackedPrims;
    GEO_HandleAgents	         m_handleAgents;
    GEO_HandleNurbsSurfs	 m_handleNurbsSurfs;

    //////////////////////////////////////////////////////////////////////////

private:
    // Convert a prim's name into a prim path taking into account prefix and
    // modifying to be a valid Usd prim path.
    static std::string createPrimPath(
            const std::string& primName,
            const SdfPath& prefix,
            bool prefixAbsolutePaths = false);

    /// Create a new refiner and copy any settings that should be propagated to
    /// a sub-refiner.
    GEO_FileRefiner createSubRefiner(
            const SdfPath& pathPrefix,
            const UT_StringArray& pathAttrNames,
            const GEO_AgentShapeInfoPtr& agentShapeInfo = nullptr);

    /// Creates or returns the point instancer for the given primitive path.
    UT_IntrusivePtr<GT_PrimPointInstancer>
    addPointInstancer(const UT_StringHolder &instancer_path,
                      const TfToken &purpose);

    /// Adds a prototype for the packed primitive's geometry, if it hasn't been
    /// seen before.
    /// Returns the prototype's index in the point instancer.
    int addPointInstancerPrototype(GT_PrimPointInstancer &instancer,
                                   GT_GEOPrimPacked &gtpacked,
                                   const TfToken &purpose,
                                   const std::string &primPath,
                                   const std::string &primName,
                                   bool addNumericSuffix);

    /// Refines the agent shapes under the given path prefix.
    void refineAgentShapes(
            const GT_PrimitiveHandle& src_prim,
            const SdfPath& root_path,
            const GU_AgentDefinition& defn,
            const UT_Array<GEO_AgentShapeInfoPtr>& shapes);

    /// Adds a prototype for the packed primitive's geometry (for native
    /// instancing), if it hasn't been seen before.
    /// Returns the path to the prototype prim.
    GEO_PathHandle
    addNativePrototype(GT_GEOPrimPacked &gtpacked, const TfToken &purpose,
                       const std::string &primPath, bool addNumericSuffix);

    /// Creates or returns the volume collection prim for the given path.
    UT_IntrusivePtr<GT_PrimVolumeCollection>
    addVolumeCollection(const GT_Primitive &field_prim,
                        const std::string &field_name, const TfToken &purpose);

    // Place to collect refined prims
    GEO_FileRefinerCollector&   m_collector;

    // Refine parms are passed to refineDetail and then held on to.
    GT_RefineParms          m_refineParms; 

    // Prefix added to all relative prim paths.
    SdfPath                 m_pathPrefix;

    // Specify a specific path to use for refining packed USD prims.
    SdfPath                 m_overridePath;

    // Specify a specific purpose to use for refining packed USD prims.
    TfToken                 m_overridePurpose;

    // The name of the attribute that specifies what USD object to write to.
    UT_StringArray          m_pathAttrNames;
    
    // Whether to use m_pathPrefix when the values in m_pathAttrNames are absolute paths.
    bool                    m_prefixAbsolutePaths;

    // Data ID for the current detail's topology and path attributes.
    GA_DataId               m_topologyId;

    // Mark any polygon meshes as subdivision surfaces. Used when refining
    // primitives (e.g. tapered tubes) that don't have an exact USD equivalent.
    bool                    m_markMeshesAsSubd;

    // Tracks the source agent shape when refining a shape library.
    GEO_AgentShapeInfoPtr   m_agentShapeInfo;

    // The known agent definitions and their prim paths
    UT_Map<GU_AgentDefinitionConstPtr, GT_PrimAgentDefinitionPtr> m_knownAgentDefs;

    // Map from a packed primitive to the path where it was unpacked. Used for
    // converting packed primitives to native instances.
    UT_Map<GT_PackedInstanceKey, GEO_PathHandle> m_knownInstancedGeos;

    // Tracks the volume and field prims.
    UT_Map<SdfPath, UT_IntrusivePtr<GT_PrimVolumeCollection>> m_volumeCollections;

    /// Accumulates packed primitives into point instancers.
    UT_Map<SdfPath, UT_IntrusivePtr<GT_PrimPointInstancer>> m_pointInstancers;
};

// As we recurse down a packed prim hierarchy, we create a new refiner at each
// level so we can carry the appropriate parametera. However, we need a object
// shared by all the refiners to collect the refined prims.
class GEO_FileRefinerCollector
{
public:
    GEO_FileRefinerCollector(
            GEO_VolumeFileMap& volume_file_paths,
            UT_Array<XUSD_LockedGeoPtr>& unpacked_geos,
            const std::string& primary_file_path)
        : myVolumeFilePaths(volume_file_paths)
        , myUnpackedGeos(unpacked_geos)
        , myPrimaryFilePath(primary_file_path)
    {
    }

    using GEO_FileGprimArrayEntry = GEO_FileRefiner::GEO_FileGprimArrayEntry;
    using GEO_FileGprimArray = GEO_FileRefiner::GEO_FileGprimArray;

    // Struct used to keep names unique
    struct NameInfo {
        size_t count;       // number of times name has been used.

        NameInfo() : count(0) {}
    };

    ////////////////////////////////////////////////////////////////////////////

    GEO_PathHandle add( 
        const SdfPath&              path,
        bool                        addNumericSuffix,
        GT_PrimitiveHandle          prim,
        const UT_Matrix4D&          xform,
        GA_DataId                   topologyId,
        const TfToken &             purpose,
        const GEO_AgentShapeInfoPtr& agentShapeInfo);

    // Complete any final work after refining all prims.
    void finish( GEO_FileRefiner& refiner );

    /// Add an XUSD_LockedGeo for the geometry containing the volume / VDB, and
    /// determine a suitable file path identifier.
    void registerVolumeGeometry(const GT_Primitive &gt_volume);

    ////////////////////////////////////////////////////////////////////////////

    // The results of the refine
    GEO_FileRefiner::GEO_FileGprimArray m_gprims;

    // Map used to generate unique names for each prim
    UT_Map<SdfPath, NameInfo> m_names;

private:
    GEO_VolumeFileMap &myVolumeFilePaths;
    UT_Array<XUSD_LockedGeoPtr> &myUnpackedGeos;
    const std::string& myPrimaryFilePath;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_FILEREFINER_H__
