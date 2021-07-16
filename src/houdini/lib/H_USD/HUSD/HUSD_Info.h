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
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_Info_h__
#define __HUSD_Info_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringMap.h>
#include <UT/UT_ArrayStringSet.h>

class HUSD_TimeCode;
enum class HUSD_XformType;
enum class HUSD_TimeSampling;
template <typename T> class UT_BoundingBoxT;
using UT_BoundingBoxD = UT_BoundingBoxT<fpreal64>;
class UT_InfoTree;
class UT_Options;
typedef UT_StringMap<UT_StringHolder> HUSD_CollectionInfoMap;

class HUSD_API HUSD_Info
{
public:
			 HUSD_Info();
			 HUSD_Info(HUSD_AutoAnyLock &lock);
			~HUSD_Info();

    static bool		 isArrayValueType(const UT_StringRef &valueType);
    static bool		 isTokenArrayValueType(const UT_StringRef &valueType);
    static bool		 isPrimvarName(const UT_StringRef &name);
    static void		 getPrimitiveKinds(UT_StringArray &kinds);
    static void          getUsdVersionInfo(UT_StringMap<UT_StringHolder> &info);

    // Get kind hierarchy information.
    static bool          isModelKind(const UT_StringRef &kind);
    static bool          isGroupKind(const UT_StringRef &kind);
    static bool          isComponentKind(const UT_StringRef &kind);

    // Get basic information from the auto lock used to construct this
    // info object.
    bool		 isStageValid() const;
    bool		 getStageRootLayer(UT_StringHolder &identifier) const;

    // Reload a layer. Does the USD reload and clears Houdini-specific caches
    // associated with loaded layers. Optionally finds all referenced layers
    // and also reloads them (recursively).
    static bool		 reload(const UT_StringRef &filepath,
                                bool recursive);
    // Reloads as above, but uses the asset resolver context from the auto lock
    // used to construct this info object.
    bool                 reloadWithContext(const UT_StringRef &filepath,
                                bool recursive) const;

    // Returns the identifiers and a human readable name for all sublayers of
    // the stage root layer in strongest to weakest order.
    bool		 getSourceLayers(UT_StringArray &names,
				UT_StringArray &identifiers,
				UT_IntArray &anonymous,
                                UT_IntArray &fromsops) const;
    bool		 getLayerHierarchy(UT_InfoTree &hierarchy) const;
    bool		 getLayerSavePath(UT_StringHolder &savepath) const;
    bool                 getLayersAboveLayerBreak(
                                UT_StringArray &identifiers) const;

    // Check if the layer specified by the file path can be found. Uses the
    // stage's resolver context if this object was created with a lock.
    bool		 getLayerExists(const UT_StringRef &filepath) const;

    // Layer information
    bool                 getStartTimeCode(fpreal64 &starttimecode) const;
    bool                 getEndTimeCode(fpreal64 &endtimecode) const;
    bool                 getFramesPerSecond(fpreal64 &fps) const;
    bool                 getTimeCodesPerSecond(fpreal64 &tcs) const;
    bool                 getMetrics(UT_StringHolder &upaxis,
                                fpreal64 &metersperunit) const;
    UT_StringHolder      getCurrentRenderSettings() const;
    bool                 getAllRenderSettings(UT_StringArray &paths) const;

    // General primitive information (parent, children, kinds)
    bool		 isPrimAtPath(const UT_StringRef &primpath) const;
    bool		 isActive(const UT_StringRef &primpath) const;
    bool		 isVisible(const UT_StringRef &primpath,
				const HUSD_TimeCode &time_code,
				HUSD_TimeSampling *time_sampling=nullptr) const;
    bool		 isInstance(const UT_StringRef &primpath) const;
    UT_StringHolder	 getKind(const UT_StringRef &primpath) const;
    bool		 isKind(const UT_StringRef &primpath, 
				const UT_StringRef &kind) const;
    UT_StringHolder	 getPrimType(const UT_StringRef &primpath) const;
    bool		 isPrimType(const UT_StringRef &primpath, 
				const UT_StringRef &type) const;
    bool		 hasPrimAPI(const UT_StringRef &primpath, 
				const UT_StringRef &api) const;
    bool		 hasPayload(const UT_StringRef &primpath) const;
    UT_StringHolder	 getIcon(const UT_StringRef &primpath) const;
    UT_StringHolder	 getPurpose(const UT_StringRef &primpath) const;
    UT_StringHolder	 getDrawMode(const UT_StringRef &primpath) const;
    UT_StringHolder	 getAutoParentPrimKind(
				const UT_StringRef &primpath) const;
    bool		 hasChildren(const UT_StringRef &primpath) const;
    void		 getChildren(const UT_StringRef &primpath,
				UT_StringArray &childnames) const;

    // Gather general statistics about the descendants of a primitive.
    enum DescendantStatsFlags {
        STATS_SIMPLE_COUNTS = 0x0000,
        STATS_PURPOSE_COUNTS = 0x0001,
        STATS_GEOMETRY_COUNTS = 0x0002
    };
    void                 getDescendantStats(const UT_StringRef &primpath,
                                UT_Options &stats,
                                DescendantStatsFlags
                                    flags = STATS_SIMPLE_COUNTS) const;

    UT_StringHolder	 getAncestorOfKind(const UT_StringRef &primpath,
				const UT_StringRef &kind) const;
    UT_StringHolder	 getAncestorInstanceRoot(
                                const UT_StringRef &primpath) const;

    // Attributes
    enum class QueryAspect
    {
	ANY,	    // Any attribute 
	ARRAY	    // Attribute of som array type.
    };
    // Checks existence or property of a prim's attribute.
    bool		 isAttribAtPath(const UT_StringRef &attribpath,
				QueryAspect query = QueryAspect::ANY) const;
    bool		 isAttribAtPath(const UT_StringRef &primpath,
				const UT_StringRef &attribname,
				QueryAspect query = QueryAspect::ANY) const;

    // Length of array attributes (1 for non-arrays).
    exint		 getAttribLength(const UT_StringRef &attribpath,
				const HUSD_TimeCode &time_code,
				HUSD_TimeSampling *time_sampling=nullptr) const;
    exint		 getAttribLength(const UT_StringRef &primpath,
				const UT_StringRef &attribname,
				const HUSD_TimeCode &time_code,
				HUSD_TimeSampling *time_sampling=nullptr) const;

    // Tuple size of attributes (eg, 2,3,4 for vectors, 1 for scalars)
    // For array attributes, returns the tuple size of contained element type.
    exint		 getAttribSize(const UT_StringRef &attribpath) const;
    exint		 getAttribSize(const UT_StringRef &primpath,
				const UT_StringRef &attribname) const;

    // Returns the name of the attribute type (eg, "float", "double3[]"). 
    // Note, this is different than attribute value type name (eg, "GfVec3d")
    UT_StringHolder	 getAttribTypeName(const UT_StringRef &attrpath) const;
    UT_StringHolder	 getAttribTypeName(const UT_StringRef &primpath,
				const UT_StringRef &attribname) const;

    // Time samples array (may be empty)
    bool		 getAttribTimeSamples(const UT_StringRef &attribpath,
				UT_FprealArray &time_samples) const;
    bool		 getAttribTimeSamples(const UT_StringRef &primpath,
				const UT_StringRef &attribname,
				UT_FprealArray &time_samples) const;

    // Transforms
    UT_Matrix4D		 getLocalXform(const UT_StringRef &primpath,
				const HUSD_TimeCode &time_code,
				HUSD_TimeSampling *time_sampling=nullptr) const;
    UT_Matrix4D		 getWorldXform(const UT_StringRef &primpath,
				const HUSD_TimeCode &time_code,
				HUSD_TimeSampling *time_sampling=nullptr) const;
    UT_Matrix4D		 getParentXform(const UT_StringRef &primpath,
				const HUSD_TimeCode &time_code,
				HUSD_TimeSampling *time_sampling=nullptr) const;
    bool		 getXformOrder(const UT_StringRef &primpath,
				UT_StringArray &xform_order) const;
    bool		 isXformReset(const UT_StringRef &primpath ) const;

    UT_StringHolder	 findXformName(const UT_StringRef &primpath,
				const UT_StringRef &xform_name_suffix) const;
    UT_StringHolder	 getUniqueXformName(const UT_StringRef &primpath,
				HUSD_XformType type, 
				const UT_StringRef &xform_name_suffix) const;

    static const UT_StringHolder &getTransformAttribName();
    static const UT_StringHolder &getTimeVaryingAttribName();
    void		 getAttributeNames(const UT_StringRef &primpath,
				UT_ArrayStringSet &attrib_names) const;
    void		 extractAttributes(const UT_StringRef &primpath,
				const UT_ArrayStringSet &which_attribs,
				const HUSD_TimeCode &tc,
				UT_Options &values,
				HUSD_TimeSampling *time_sampling=nullptr) const;

    // Bounds
    UT_BoundingBoxD	 getBounds(const UT_StringRef &primpath,
				const UT_StringArray &purposes,
				const HUSD_TimeCode &time_code) const;

    // Point Instancers
    bool		 getPointInstancerXforms( const UT_StringRef &primpath,
				UT_Array<UT_Matrix4D> &xforms,
				const HUSD_TimeCode &time_code);
    UT_BoundingBoxD	 getPointInstancerBounds(const UT_StringRef &primpath,
				exint instance_index,
				const UT_StringArray &purposes,
				const HUSD_TimeCode &time_code) const;

    // Variants
    bool		 getVariantSets(const UT_StringRef &primpath,
				UT_StringArray &vset_names) const;
    bool		 getVariants(const UT_StringRef &primpath,
				const UT_StringRef &variantset,
				UT_StringArray &vset_names) const;
    UT_StringHolder	 getVariantSelection(const UT_StringRef &primpath,
				const UT_StringRef &variantset) const;

    // Collections
    bool		 isCollectionAtPath(
				const UT_StringRef &collectionpath) const;
    UT_StringHolder	 getCollectionExpansionRule(
				const UT_StringRef &collectionpath) const;
    bool		 getCollectionIncludePaths(
				const UT_StringRef &collectionpath,
				UT_StringArray &primpaths) const;
    bool		 getCollectionExcludePaths(
				const UT_StringRef &collectionpath,
				UT_StringArray &primpaths) const;
    bool		 getCollectionComputedPaths(
				const UT_StringRef &collectionpath,
				UT_StringArray &primpaths) const;
    bool		 collectionContains(
				const UT_StringRef &collectionpath,
				const UT_StringRef &primpath) const;
    bool		 getCollections(const UT_StringRef &primpath,
				HUSD_CollectionInfoMap
                                    &collection_info_map) const;

    // Materials
    UT_StringHolder	 getBoundMaterial(const UT_StringRef &primpath) const;

    // Primvars
    bool		 isPrimvarAtPath(const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				QueryAspect query = QueryAspect::ANY) const;
    void		 getPrimvarNames(const UT_StringRef &primpath,
				UT_ArrayStringSet &primvar_names) const;
    exint		 getPrimvarLength(const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				const HUSD_TimeCode &time_code,
				HUSD_TimeSampling *time_sampling=nullptr) const;
    exint		 getPrimvarSize(const UT_StringRef &primpath,
				const UT_StringRef &primvarname) const;
    UT_StringHolder	 getPrimvarTypeName(const UT_StringRef &primpath,
				const UT_StringRef &primvarname) const;
    bool		 getPrimvarTimeSamples(const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				UT_FprealArray &time_samples) const;

    // Relationships
    void		 getRelationshipNames(const UT_StringRef &primpath,
				UT_ArrayStringSet &rel_names) const;

    bool		 isRelationshipAtPath(
				const UT_StringRef &relpath) const;
    bool		 isRelationshipAtPath(const UT_StringRef &primpath,
				const UT_StringRef &relationahipname) const;

    bool		 getRelationshipTargets (
				const UT_StringRef &relpath,
				UT_StringArray &target_paths) const;
    bool		 getRelationshipTargets (
				const UT_StringRef &primpath,
				const UT_StringRef &relationshipname,
				UT_StringArray &target_paths) const;

    bool		 getRelationshipForwardedTargets (
				const UT_StringRef &relpath,
				UT_StringArray &target_paths) const;
    bool		 getRelationshipForwardedTargets (
				const UT_StringRef &primpath,
				const UT_StringRef &relationshipname,
				UT_StringArray &target_paths) const;

    // Metadata
    void		 getMetadataNames(const UT_StringRef &object_path,
				UT_ArrayStringSet &metadata_names) const;
    bool		 isMetadataAtPath(const UT_StringRef &object_path,
				const UT_StringRef &metadata_name,
				QueryAspect query = QueryAspect::ANY) const;
    exint		 getMetadataLength(const UT_StringRef &object_path,
				const UT_StringRef &metadata_name) const;

    // Access information from the active layer, rather than the stage.
    bool		 isActiveLayerPrimAtPath(const UT_StringRef &primpath,
				const UT_StringRef &prim_type =
				    UT_StringHolder::theEmptyString) const;
    // Returns the identifiers and a human readable name for all sublayers of
    // the active layer in strongest to weakest order.
    bool		 getActiveLayerSubLayers(UT_StringArray &names,
				UT_StringArray &identifiers,
				UT_IntArray &anonymous,
                                UT_IntArray &fromsops) const;

    // Shader parameters.
    void		 getShaderInputAttributeNames(
				const UT_StringRef &primpath,
				UT_ArrayStringSet &attrib_names) const;

private:
    HUSD_AutoAnyLock	*myAnyLock;
};

#endif

