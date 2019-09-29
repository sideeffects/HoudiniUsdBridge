/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_Data_h__
#define __HUSD_Data_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "XUSD_DataLock.h"
#include "HUSD_Overrides.h"
#include "XUSD_PathSet.h"
#include "XUSD_Ticket.h"
#include <UT/UT_Color.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringSet.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_SharedPtr.h>
#include <UT/UT_UniquePtr.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/stagePopulationMask.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

enum XUSD_AddLayerOp
{
    XUSD_ADD_LAYER_LOCKED,
    XUSD_ADD_LAYER_EDITABLE,
};

class XUSD_LayerAtPath
{
public:
			 XUSD_LayerAtPath();
    explicit		 XUSD_LayerAtPath(const SdfLayerRefPtr &layer,
				const SdfLayerOffset &offset = SdfLayerOffset(),
				int layer_badge_index = 0);
    explicit		 XUSD_LayerAtPath(const SdfLayerRefPtr &layer,
				const std::string &filepath,
				const SdfLayerOffset &offset = SdfLayerOffset(),
				int layer_badge_index = 0);

    bool		 hasLayerColorIndex(int &clridx) const;
    bool		 isLayerAnonymous() const;

    SdfLayerRefPtr	 myLayer;
    std::string		 myIdentifier;
    SdfLayerOffset	 myOffset;
    int			 myLayerColorIndex;
    bool		 myRemoveWithLayerBreak;
    bool		 myLayerIsMissingFile;
};

class XUSD_OverridesInfo
{
public:
			 XUSD_OverridesInfo(const UsdStageRefPtr &stage);
			~XUSD_OverridesInfo();

    bool		 isEmpty() const
			 { return !myReadOverrides && !myWriteOverrides; }

    HUSD_ConstOverridesPtr	 myReadOverrides;
    HUSD_OverridesPtr		 myWriteOverrides;
    SdfLayerRefPtr		 mySessionLayers[HUSD_OVERRIDES_NUM_LAYERS];
    exint			 myOverridesVersionId;
};

typedef UT_Array<XUSD_LayerAtPath>	 XUSD_LayerAtPathArray;

class HUSD_API XUSD_Data : public UT_IntrusiveRefCounter<XUSD_Data>,
			   public UT_NonCopyable
{
public:
				 XUSD_Data(HUSD_MirroringType mirroring);
				~XUSD_Data();

    // Return true if our stage value is set and has a valid root prim.
    bool			 isStageValid() const;
    // Returns our stage. Should only be accessed when this data is locked.
    UsdStageRefPtr		 stage() const;
    // Returns the active layer on our composed stage.
    SdfLayerRefPtr		 activeLayer() const;
    // Return the on-stage identifiers of any layers that are marked in the
    // source layers array to be removed due to a layer break.
    std::set<std::string>	 getStageLayersToRemoveFromLayerBreak() const;
    // Creates a layer by flattening all our source layers together. Also
    // strips out any layers tagged by a Layer Break LOP.
    SdfLayerRefPtr		 createFlattenedLayer(
					HUSD_StripLayerResponse response) const;
    // Creates a layer by flattening a stage consisting of our source layers
    // after stripping out any layers tagged by a Layer Break LOP.
    SdfLayerRefPtr		 createFlattenedStage(
					HUSD_StripLayerResponse response) const;

    // Return the array of source layers that are combined to make our stage.
    const XUSD_LayerAtPathArray	&sourceLayers() const;
    // Return the current session layer overrides set on our stage.
    const HUSD_ConstOverridesPtr&overrides() const;
    // Return a specific session layer object on our stage.
    const SdfLayerRefPtr	&sessionLayer(HUSD_OverridesLayerId id) const;
    // Return the current load masks set on our stge.
    const HUSD_LoadMasksPtr	&loadMasks() const;
    // Return the identifier of our stage's root layer. This can be used as
    // a quick check as to whether we have create a brand new stage.
    const std::string		&rootLayerIdentifier() const;

    // Add a layer using a file path, layer pointer, or an empty layer.
    // Files and layer pointers can be inserted at any position in the
    // sublayer list (0 is strongest, -1 is weakest). The layer op value
    // indicates whether the layer should be editable, which implies that
    // it must be added to the strongest position (the only place where a
    // layer can be edited).
    bool			 addLayer(const std::string &filepath,
					const SdfLayerOffset &offset,
					int position,
					XUSD_AddLayerOp add_layer_op);
    bool			 addLayer(const XUSD_LayerAtPath &layer,
					int position,
					XUSD_AddLayerOp add_layer_op);
    bool			 addLayer();
    // Remove one of our source layers.
    bool			 removeLayer(const std::string &filepath);

    // Apply a layer break, which tags all existing layers, and adds a new
    // empty layer for holding future modification.
    bool			 applyLayerBreak();

    // Store a ticket in with this data to keep alive cooked sop data in the
    // XUSD_TicketRegistry as long as it might be referenced by our stage.
    void			 addTicket(const XUSD_TicketPtr &ticket);
    void			 addTickets(const XUSD_TicketArray &tickets);
    const XUSD_TicketArray	&tickets() const;

    // Store pointers to arrays that were created automatically as part of a
    // process of replacing a layer on disk with an anonymous layer.
    void			 addReplacements(
					const XUSD_LayerArray &replacements);
    const XUSD_LayerArray	&replacements() const;

    // Store a locked stage with this data to keep alive cooked lop data in the
    // HUSD_LockedStageRegistry as long as it might be referenced by our stage.
    void			 addLockedStage(
					const HUSD_LockedStagePtr &stage);
    void			 addLockedStages(
					const HUSD_LockedStageArray &stages);
    const HUSD_LockedStageArray	&lockedStages() const;

private:
    void		 reset();
    void		 createNewData(const HUSD_LoadMasksPtr &load_masks,
				int resolver_context_nodeid,
				const UsdStageWeakPtr &resolver_context_stage,
				const ArResolverContext *resolver_context);
    void		 createHardCopy(const XUSD_Data &src);
    void		 createSoftCopy(const XUSD_Data &src,
				const HUSD_LoadMasksPtr &load_masks,
				bool make_new_implicit_layer);
    void		 createCopyWithReplacement(
				const XUSD_Data &src,
				const UT_StringRef &frompath,
				const UT_StringRef &topath,
				int nodeid,
				HUSD_MakeNewPathFunc make_new_path,
				UT_StringSet &replaced_layers);

    // Return the resolver context for our stage. Note this this method does
    // not require that the stage be locked, becaue it is unchanging data set
    // on the stage when it was created.
    ArResolverContext	 resolverContext() const;

    // Return a layer created by flattening all source layers up to the
    // strongest layer excluded by a Layer Break node.
    void		 flattenLayers(const XUSD_Data &src,
				int creator_node_id);
    // Return a layer created by flattening the stage composed of all source
    // layers up to the strongest layer excluded by a Layer Break node.
    void		 flattenStage(const XUSD_Data &src,
				int creator_node_id);
    // Utility method used by the two flatten methods above to separate all
    // layers preceding a layer break point. Optionally creates a warning if
    // a layer break is found.
    UsdStageRefPtr	 getOrCreateStageForFlattening(
				HUSD_StripLayerResponse response,
				UsdStage::InitialLoadSet loadset) const;

    void		 mirror(const XUSD_Data &src,
				const HUSD_LoadMasks &load_masks);

    void		 afterLock(bool for_write,
				const HUSD_ConstOverridesPtr
				    &read_overrides =
				    HUSD_ConstOverridesPtr(),
				const HUSD_OverridesPtr
				    &write_overrides =
				    HUSD_OverridesPtr(),
				bool remove_layer_breaks = false);
    XUSD_LayerPtr	 editActiveSourceLayer();
    void                 createInitialPlaceholderSublayers();
    void		 afterRelease();

    static void		 exitCallback(void *);

    UsdStageRefPtr			 myStage;
    UT_SharedPtr<UT_StringArray>	 myStageLayerAssignments;
    UT_SharedPtr<XUSD_LayerArray>	 myStageLayers;
    UT_SharedPtr<int>			 myStageLayerCount;
    UT_SharedPtr<XUSD_OverridesInfo>	 myOverridesInfo;
    XUSD_LayerAtPathArray		 mySourceLayers;
    HUSD_LoadMasksPtr			 myLoadMasks;
    XUSD_DataLockPtr			 myDataLock;
    XUSD_TicketArray			 myTicketArray;
    XUSD_LayerArray			 myReplacementLayerArray;
    HUSD_LockedStageArray		 myLockedStages;
    HUSD_MirroringType			 myMirroring;
    XUSD_PathSet			 myMirrorPayloadLoadPaths;
    XUSD_PathSet			 myMirrorPayloadUnloadPaths;
    bool				 myMirrorPayloadPathsChanged;
    bool				 myMirrorLoadAllPayloads;
    int					 myActiveLayerIndex;
    bool				 myOwnsActiveLayer;

    friend class ::HUSD_DataHandle;
};

class HUSD_API XUSD_Layer : public UT_IntrusiveRefCounter<XUSD_Layer>
{
public:
				 XUSD_Layer(const SdfLayerRefPtr &layer,
					 bool create_change_block)
				     : myLayer(layer),
				       myChangeBlock(nullptr)
				 {
				     if (create_change_block)
					 myChangeBlock.reset(
					     new SdfChangeBlock());
				 }
				~XUSD_Layer()
				 { }

    const SdfLayerRefPtr	&layer() const
				 { return myLayer; }

private:
    SdfLayerRefPtr		 myLayer;
    UT_UniquePtr<SdfChangeBlock> myChangeBlock;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

