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

#ifndef __XUSD_Utils_h__
#define __XUSD_Utils_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Utils.h"
#include "XUSD_PathSet.h"
#include <OP/OP_ItemId.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringMMPattern.h>
#include <UT/UT_Map.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerOffset.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usd/stagePopulationMask.h>

class HUSD_LayerOffset;
class HUSD_LoadMasks;
class HUSD_TimeCode;

PXR_NAMESPACE_OPEN_SCOPE

class UsdGeomPrimvar;
class UsdGeomXformCache;

class XUSD_StageFactory
{
public:
				 XUSD_StageFactory()
				 { }
    virtual			~XUSD_StageFactory()
				 { }

    virtual int			 getPriority() const = 0;
    virtual UsdStageRefPtr	 createStage(UsdStage::InitialLoadSet loadset,
					int nodeid) const = 0;
};

extern "C" {
    SYS_VISIBILITY_EXPORT extern void newStageFactory(
	UT_Array<XUSD_StageFactory *> *factories);
};

class XUSD_SavePathInfo
{
public:
    explicit		 XUSD_SavePathInfo()
			     : myNodeBasedPath(false)
			 { }
    explicit		 XUSD_SavePathInfo(const UT_StringHolder &finalpath)
			     : myFinalPath(finalpath),
                               myOriginalPath(finalpath),
			       myNodeBasedPath(false)
			 { }
    explicit		 XUSD_SavePathInfo(const UT_StringHolder &finalpath,
                                const UT_StringHolder &originalpath,
				bool node_based_path)
			    : myFinalPath(finalpath),
                              myOriginalPath(originalpath),
			      myNodeBasedPath(node_based_path)
			 { }

    UT_StringHolder	 myFinalPath;
    UT_StringHolder	 myOriginalPath;
    bool		 myNodeBasedPath;
};

typedef UT_Map<std::string, SdfLayerRefPtr>
    XUSD_IdentifierToLayerMap;
typedef UT_Map<std::string, XUSD_SavePathInfo>
    XUSD_IdentifierToSavePathMap;

// Helper function to convert a node id directly to a node path, and return
// true if the conversion was successful.
HUSD_API bool HUSDgetNodePath(int nodeid, UT_StringHolder &nodepath);
// Similar to the above method, but for the dedicated purpose of returning a
// std::string to pass to HUSDcreateAnonymousLayer.
HUSD_API std::string HUSDgetTag(const XUSD_DataLockPtr &datalock);

HUSD_API const TfToken &HUSDgetDataIdToken();
HUSD_API const TfToken &HUSDgetSavePathToken();
HUSD_API const TfToken &HUSDgetSaveControlToken();
HUSD_API const TfToken &HUSDgetCreatorNodeToken();
HUSD_API const TfToken &HUSDgetEditorNodesToken();
HUSD_API const TfToken &HUSDgetMaterialIdToken();
HUSD_API const TfToken &HUSDgetMaterialBindingIdToken();
HUSD_API const TfToken &HUSDgetSoloLightPathsToken();
HUSD_API const TfToken &HUSDgetSoloGeometryPathsToken();
HUSD_API const TfToken &HUSDgetPrimEditorNodeIdToken();
HUSD_API const TfToken &HUSDgetSourceNodeToken();

HUSD_API const TfType  &HUSDfindType(const UT_StringRef &type_name);
HUSD_API bool		HUSDisDerivedType(const UsdPrim &prim,
				const TfType &base_type);

// Path conversion functions.
HUSD_API SdfPath	HUSDgetSdfPath(const UT_StringRef &path);
HUSD_API SdfPathVector	HUSDgetSdfPaths(const UT_StringArray &paths);
HUSD_API const SdfPath &HUSDgetHoudiniLayerInfoSdfPath();

// Timecode conversion functions.
HUSD_API UsdTimeCode	HUSDgetUsdTimeCode(const HUSD_TimeCode &timecode);
HUSD_API UsdTimeCode	HUSDgetCurrentUsdTimeCode();
HUSD_API UsdTimeCode	HUSDgetNonDefaultUsdTimeCode(
				const HUSD_TimeCode &timecode);
HUSD_API UsdTimeCode	HUSDgetEffectiveUsdTimeCode(
				const HUSD_TimeCode &timecode,
				const UsdAttribute &attr);
HUSD_API HUSD_TimeCode	HUSDgetEffectiveTimeCode(
				const HUSD_TimeCode &timecode,
				const UsdAttribute &attr);


// Layer offset conversion.
HUSD_API SdfLayerOffset
HUSDgetSdfLayerOffset(const HUSD_LayerOffset &layeroffset);
HUSD_API HUSD_LayerOffset
HUSDgetLayerOffset(const SdfLayerOffset &layeroffset);

// Other functions to convert from HUSD classes to USD equivalents.
HUSD_API Usd_PrimFlagsPredicate
HUSDgetUsdPrimPredicate(HUSD_PrimTraversalDemands demands);
HUSD_API UsdListPosition
HUSDgetUsdListPosition(const UT_StringRef &editopstr);
HUSD_API UsdStagePopulationMask
HUSDgetUsdStagePopulationMask(const HUSD_LoadMasks &load_masks);
HUSD_API SdfVariability
HUSDgetSdfVariability(HUSD_Variability variability);

// Determine if a layer comes from a SOP or not.
HUSD_API bool
HUSDisSopLayer(const std::string &identifier);
HUSD_API bool
HUSDisSopLayer(const SdfLayerHandle &layer);

// Determine if the specified layer should be saved to disk when saving a
// LOP network which sublayers or references this layer.
HUSD_API bool
HUSDshouldSaveLayerToDisk(const SdfLayerHandle &layer);

// Figures out from the layer metadata where the layer should be saved. This
// method only works on layers that return true from HUSDshouldSaveLayerToDisk.
HUSD_API std::string
HUSDgetLayerSaveLocation(const SdfLayerHandle &layer,
	bool *using_node_path = nullptr);

// Get (creating if requested) the special prim that gets put on LOP layers
// to hold special layer information (save path, creator node, editor nodes,
// etc.) We store this on the custom data of a dedicated prim instead of on
// the layer root because custom data on the layer root can cause a whole lot
// of recomposition.
HUSD_API SdfPrimSpecHandle
HUSDgetLayerInfoPrim(const SdfLayerHandle &layer, bool create);

// Get or set the save path custom data on a layer. Used by the above
// HUSDgetLayerSaveLocation as one of the methods to determine where a layer
// should be saved.
HUSD_API void
HUSDsetSavePath(const SdfLayerHandle &layer,
	const UT_StringRef &savepath);
HUSD_API bool
HUSDgetSavePath(const SdfLayerHandle &layer,
	std::string &savepath);

// Get or set the save control token which modified how the USD ROP treats
// this layer when it is being saved with various options.
HUSD_API void
HUSDsetSaveControl(const SdfLayerHandle &layer,
	const UT_StringRef &savecontrol);
HUSD_API bool
HUSDgetSaveControl(const SdfLayerHandle &layer,
	std::string &savecontrol);

HUSD_API void
HUSDsetCreatorNode(const SdfLayerHandle &layer, int node_id);
HUSD_API void
HUSDsetCreatorNode(const SdfLayerHandle &layer, const std::string &nodepath);
HUSD_API bool
HUSDgetCreatorNode(const SdfLayerHandle &layer, std::string &nodepath);

HUSD_API void
HUSDsetSourceNode(const UsdPrim &prim, int node_id);
HUSD_API bool
HUSDgetSourceNode(const UsdPrim &prim, std::string &nodepath);

HUSD_API void
HUSDclearEditorNodes(const SdfLayerHandle &layer);
HUSD_API void
HUSDaddEditorNode(const SdfLayerHandle &layer, int node_id);
HUSD_API bool
HUSDgetEditorNodes(const SdfLayerHandle &layer,
	std::vector<std::string> &nodepaths);

// Set the list of SdfPaths of all solo'ed lights. This information is stored
// as custom data on the HoudiniLayerInfo prim. These methods should only be
// used by HUSD_Overrides.
HUSD_API void
HUSDsetSoloLightPaths(const SdfLayerHandle &layer,
	const std::vector<std::string> &paths);
HUSD_API bool
HUSDgetSoloLightPaths(const SdfLayerHandle &layer,
	std::vector<std::string> &paths);

// Set the list of SdfPaths of all solo'ed geometry. This information is stored
// as custom data on the HoudiniLayerInfo prim. These methods should only be
// used by HUSD_Overrides.
HUSD_API void
HUSDsetSoloGeometryPaths(const SdfLayerHandle &layer,
	const std::vector<std::string> &paths);
HUSD_API bool
HUSDgetSoloGeometryPaths(const SdfLayerHandle &layer,
	std::vector<std::string> &paths);

// Set the Editor node for a specific USD primitive. This is stored as custom
// data on the primitive, and indicates the node that last modified this
// primitive, and so the node that we should use for any future requests to
// edit the prim.
HUSD_API void
HUSDsetPrimEditorNodeId(const UsdPrim &prim, int node_id);
HUSD_API void
HUSDsetPrimEditorNodeId(const SdfPrimSpecHandle &prim, int node_id);

HUSD_API void
HUSDclearDataId(const UsdAttribute &attr);

HUSD_API TfToken
HUSDgetParentKind(const TfToken &kind);

// Create a new primitive in the specified layer. The stage parameter may or
// may not include the layer. It is used only to look up any existing prims
// so we know which ancestors of the new prim should be defined and which
// should simply be over prims. If the requested prim already exists on the
// stage, this function does nothing.
HUSD_API SdfPrimSpecHandle
HUSDcreatePrimInLayer(const UsdStageWeakPtr &stage,
	const SdfLayerHandle &layer,
	const SdfPath &path,
	const TfToken &kind,
	bool parent_prims_define,
	const std::string &parent_prims_type);

HUSD_API bool
HUSDcopySpec(const SdfLayerHandle &srclayer,
	const SdfPath &srcpath,
	const SdfLayerHandle &destlayer,
	const SdfPath &destath,
	const SdfPath &srcroot = SdfPath(),
	const SdfPath &destroot = SdfPath());

// This function duplicates the functionality of
// SdfLayer::UpdateExternalRefernce, but can retarget a bunch of references
// with a single method call, and thus a single traversal.
HUSD_API bool
HUSDupdateExternalReferences(const SdfLayerHandle &layer,
	const std::map<std::string, std::string> &pathmap);

HUSD_API bool
HUSDcopyLayerMetadata(const SdfLayerHandle &srclayer,
	const SdfLayerHandle &destlayer);

HUSD_API bool
HUSDclearLayerMetadata(const SdfLayerHandle &destlayer);

// Utility function used for stitching stages together and saving them.
HUSD_API void
HUSDaddExternalReferencesToLayerMap(const SdfLayerRefPtr &layer,
	XUSD_IdentifierToLayerMap &layermap,
	bool recursive);

// Calls the USD stitch function but with a callback that looks for SOP data
// ids on the attributes to avoid creating duplicate time samples.
HUSD_API void
HUSDstitchLayers(const SdfLayerHandle &strongLayer,
	const SdfLayerHandle &weakLayer);
// Stitch two stages together by stitching together their "corresponding"
// layers, as determined by the requested save paths for each layer.
HUSD_API bool
HUSDaddStageTimeSample(const UsdStageWeakPtr &src,
	const UsdStageRefPtr &dest,
	SdfLayerRefPtrVector &hold_layers);

// Create a new in-memory stage. Use this method instead of calling
// UsdStage::CreateInMemory directly, as we want to configure the stage
// with a reasonable identifier, and a path resolver context. The first
// version only sets the payload loading option, which must be set when
// the stage is constructed. The second version sets the payload loading
// option, the stage population maks, and the layer muting based on the
// values set in the provided load masks object.
HUSD_API UsdStageRefPtr
HUSDcreateStageInMemory(UsdStage::InitialLoadSet load,
	int resolver_context_nodeid = OP_INVALID_ITEM_ID,
	const UsdStageWeakPtr &resolver_context_state = UsdStageWeakPtr(),
	const ArResolverContext *resolver_context = nullptr);
HUSD_API UsdStageRefPtr
HUSDcreateStageInMemory(const HUSD_LoadMasks *load_masks,
	int resolver_context_nodeid = OP_INVALID_ITEM_ID,
	const UsdStageWeakPtr &resolver_context_state = UsdStageWeakPtr(),
	const ArResolverContext *resolver_context = nullptr);

// Create a new anonymous layer. Usd this method instead of calling
// SdfLayer::CreateAnonymous directly, as we want to configure the layer
// with some common default data.
HUSD_API SdfLayerRefPtr
HUSDcreateAnonymousLayer(const std::string &tag = std::string(),
	bool set_up_axis = false);

enum HUSD_FlattenLayerFlags {
    HUSD_FLATTEN_FILE_LAYERS = 0x0001,
    HUSD_FLATTEN_SOP_LAYERS = 0x0002,
    HUSD_FLATTEN_EXPLICIT_LAYERS = 0x0004,
    HUSD_FLATTEN_FULL_STACK = 0x0008
};

// Combine layers together based on some options and custom data set on each
// layer in the stack (or referenced by a layer in the stack).
HUSD_API SdfLayerRefPtr
HUSDflattenLayerPartitions(const UsdStageWeakPtr &stage,
	int flatten_flags,
	SdfLayerRefPtrVector &explicit_layers);

// Combine all layers in the stack by calling the USD flatten layers method.
HUSD_API SdfLayerRefPtr
HUSDflattenLayers(const UsdStageWeakPtr &stage);

// Check if the supplied layer is completely devoid of any useful information.
// This includes both primitives and layer level metadata. However the presence
// of only a HoudiniLayerInfo prim may still indicate an "empty" layer if it
// only contains creator node information.
HUSD_API bool
HUSDisLayerEmpty(const SdfLayerHandle &layer);
// Check if the supplied layer is a placeholder layer.
HUSD_API bool
HUSDisLayerPlaceholder(const SdfLayerHandle &layer);
// As above, but takes an identifier, which is used to find the layer handle.
HUSD_API bool
HUSDisLayerPlaceholder(const std::string &identifier);

// Return the SdfPath that should be passed to create a reference to the
// specified layer. This gives priority to any passed in ref prim path
// string, then the layer's default prim (if one is set), then looks at
// the layer root prims and picks the first geometry primitive, or the
// first primitive if there are no geometry primitives. Raises an warning
// if it picks a primitive, but there are other primitives.
HUSD_API SdfPath
HUSDgetBestRefPrimPath(const UT_StringRef &reffilepath,
        const SdfFileFormat::FileFormatArguments &args,
        const UT_StringRef &refprimpath,
        UsdStageRefPtr &stage);
HUSD_API void
HUSDclearBestRefPathCache(const std::string &layeridentifier = std::string());

// Functions for checking the amount of time sampling of an attribute/xfrom:
HUSD_API HUSD_TimeSampling
HUSDgetValueTimeSampling(const UsdAttribute &attrib);
HUSD_API HUSD_TimeSampling
HUSDgetValueTimeSampling(const UsdGeomPrimvar &pvar);
HUSD_API HUSD_TimeSampling
HUSDgetLocalTransformTimeSampling(const UsdPrim &pr);
HUSD_API HUSD_TimeSampling
HUSDgetWorldTransformTimeSampling(const UsdPrim &pr);

// Conveninece methods for updating the given time sampling.
HUSD_API void
HUSDupdateTimeSampling(HUSD_TimeSampling &sampling,
        HUSD_TimeSampling new_sampling );
HUSD_API void
HUSDupdateValueTimeSampling( HUSD_TimeSampling &sampling,
        const UsdAttribute &attrib);
HUSD_API void
HUSDupdateValueTimeSampling( HUSD_TimeSampling &sampling,
        const UsdGeomPrimvar &primvar);
HUSD_API void
HUSDupdateLocalTransformTimeSampling(HUSD_TimeSampling &samplig,
        const UsdPrim &prim);
HUSD_API void
HUSDupdateWorldTransformTimeSampling(HUSD_TimeSampling &samplig,
        const UsdPrim &prim);

// Returns ture if an attribute (or any aspect of a local transform) 
// has more than 1 time sample.
HUSD_API bool
HUSDvalueMightBeTimeVarying(const UsdAttribute &attrib);
HUSD_API bool
HUSDlocalTransformMightBeTimeVarying(const UsdPrim &prim);

// Takes a set of paths, and compares them to a stage. In any case where all
// the children of a prim are in the set, remove the children, and add the
// parent prim instead. This finds the smallest possible set of prims on which
// to set an inheritable attribute such that it will affect the original set
// of paths. The "skip_point_instancers" flag can be set to true if point
// instancers should not be allowed to combine with its siblings, for cases
// where the point instancer instances need to be treated individually.
HUSD_API void
HUSDgetMinimalPathsForInheritableProperty(
        bool skip_point_instancers,
        const UsdStageRefPtr &stage,
        XUSD_PathSet &paths);

PXR_NAMESPACE_CLOSE_SCOPE

#endif

