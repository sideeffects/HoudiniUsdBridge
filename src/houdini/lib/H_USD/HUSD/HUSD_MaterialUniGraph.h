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

#ifndef __HUSD_MaterialUniGraph_h__
#define __HUSD_MaterialUniGraph_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Path.h"
#include <UN/UN_GraphData.h>
#include <UN/UN_UniGraph.h>
#include <string>

#define HUSD_SHADER_GRAPH_CATEGORY "UsdShadeGraph"
#define HUSD_NODEGRAPH_NODE_TYPE "NodeGraph"
using HUSD_MaterialUniGraphDataHandleProvider = std::function<const HUSD_DataHandle & ()>;

/// Why do we inherit from UN_BasicGraph and construct a UN_Graph from the
/// UsdShade network? Why not just look at the stage to answer queries about
/// the network? Two reasons:
///   1. The USD graph is not designed to answer queries like the ones we make
///      through UNI_Graph. dstWires is a good example - you have to scan all
///      input ports in the whole subnet. Even wires themselves don't actually
///      exist so building "ids" for them could be complex, especially if you
///      want to enable quick looks for all the queries we need to support.
///
///      With enough clever caching of the graph structure, we can answer the
///      UNI_Graph queries. With even more caching we can answer the questions
///      quickly. But by then we've basically rebuilt UN_Graph. So instead we
///      accept that UN_Graph is the ultra-fast already-written caching
///      mechanism for the graph structure that lets us satisfy the UNI_Graph
///      API we need to make for a satisfying graph editing experience.
///   2. In order to answer queries based on the USD stage, every time anything
///      changes in the graph, we need to recompose the USD stage. When editing
///      a cached UN_Graph representation we are free to edit the overlay layer
///      as much as we want, with no obligation to actually recompose the USD
///      stage just to keep giving up to date answers to queries. This is a
///      huge performance boost when making a bunch of small changes to a
///      graph. It gives us the equivalent of an SdfChangeBlock around an
///      arbitrarily large/complex series of graph edit operations.
class HUSD_API HUSD_MaterialUniGraph : public UN_BasicUniGraph
{
public:
    static UNI_GraphHandle createGraph(
        HUSD_MaterialUniGraphDataHandleProvider data_handle_provider,
        const HUSD_Path &material_path,
        const std::string &in_layer,
        int material_index);

    ~HUSD_MaterialUniGraph() override;

    /// Fetch all our edits as a USDA layer.
    bool            getEditLayerContent(std::string *out_layer) const;
    /// Get the index of the material on the node we are representing.
    int             getMaterialIndex() const;

    /// @{ Overrides of UN_UniGraph "getter" methods that need to be
    /// specialized for UsdShade graphs.
    UT_StringHolder parmDialogScript( UNI_NodeID node_id ) const override;
    UNI_ParmValue   parmValue( UNI_NodeID node_id,
                        const UT_StringRef &parm_name ) const override;
    /// @}

    /// Save items to a stream.
    bool            copyItems(UNI_NodeID parent_id,
                        const UNI_NodeIDList &node_ids,
                        const UNI_StickyNoteIDList &note_ids,
                        std::ostream &os) const override;

protected:
    /// @{ Non-const methods are all protected.
    /// Unhide base class virtual method with other signatures besides the
    /// ones explicitly overridden in this class.
    using UN_UniGraph::setName;
    using UN_UniGraph::setPosition;
    using UN_UniGraph::setColor;
    using UN_UniGraph::destroy;

    void            setWireStyle(UNI_WireStyle wire_style) override;

    UNI_NodeID      createNode( UNI_NodeID parent_id,
                        const UT_StringHolder &node_name,
                        const UT_StringHolder &node_type_name,
                        const UT_StringHolder &signature_name 
                                = UT_StringHolder()) override;
    void            setName( UNI_NodeID node_id,
                        const UT_StringHolder &name) override;
    void            setSignature( UNI_NodeID node_id,
                        const UT_StringHolder &signature_name ) override;
    void            setPosition(UNI_NodeID node_id,
                        const UT_Vector2D &pos) override;
    void            setColor( UNI_NodeID node_id,
                        const UT_Color &clr) override;
    void            setComment( UNI_NodeID node_id,
                        const UT_StringHolder &comment) override;
    void            setTags( UNI_NodeID node_id,
                        const UT_StringArray &tags) override;
    void            setParmValue( UNI_NodeID node_id,
                        const UT_StringRef &parm_name,
                        const UNI_ParmValue &value ) override;
    void            destroy( UNI_NodeID node_id ) override;

    void            setContainerConnectPosition( UNI_NodeID parent_id,
                        UNI_ContainerConnectType contype,
                        const UT_Vector2D &pos ) override;
    void            setContainerConnectColor( UNI_NodeID parent_id,
                        UNI_ContainerConnectType contype,
                        const UT_Color &clr ) override;

    UNI_WireID      createWire( UNI_PortID src, UNI_PortID dst ) override;
    void            destroy( UNI_WireID wire_id ) override;

    UNI_StickyNoteID createStickyNote( UNI_NodeID parent_id,
                        const UT_StringHolder &name ) override;
    void            setName( UNI_StickyNoteID note_id,
                        const UT_StringHolder &name ) override;
    void            setPosition( UNI_StickyNoteID note_id,
                        const UT_Vector2D &pos ) override;
    void            setSize( UNI_StickyNoteID note_id,
                        const UT_Vector2D &size ) override;
    void            setColor( UNI_StickyNoteID note_id,
                        const UT_Color &clr ) override;
    void            setText( UNI_StickyNoteID note_id,
                        const UT_StringHolder &text ) override;
    void            setTextColor( UNI_StickyNoteID note_id,
                        const UT_Color &clr ) override;
    void            setTextSize( UNI_StickyNoteID note_id,
                        const float size ) override;
    void            setCollapsed( UNI_StickyNoteID note_id,
                        bool collapsed ) override;
    void            setBackgroundHidden( UNI_StickyNoteID note_id,
                        bool hidden ) override;
    void            destroy( UNI_StickyNoteID note_id ) override;
    bool            pasteItems(UNI_NodeID parent_id,
                        UNI_NodeIDList &node_ids,
                        UNI_StickyNoteIDList &note_ids,
                        std::istream &is,
                        UT_StringMap<UT_StringHolder> *rename_map) override;
    /// @}

                    HUSD_MaterialUniGraph(
                        HUSD_MaterialUniGraphDataHandleProvider data_handle_provider,
                        const HUSD_Path &material_path,
                        const std::string &in_layer,
                        int material_index);

private:
    void            buildGraphFromMaterial(const HUSD_AutoAnyLock &lock);

    class husd_MaterialUniGraphPrivate;
    UT_UniquePtr<husd_MaterialUniGraphPrivate>	 myPrivate;
    HUSD_MaterialUniGraphDataHandleProvider      myDataHandleProviderFn;
    const HUSD_Path                              myMaterialPath;
    const int                                    myMaterialIndex;
    UT_Map<UNI_NodeID, HUSD_Path>                myNodeIdToPathMap;
    UT_Map<UNI_StickyNoteID, HUSD_Path>          myStickyNoteIdToPathMap;
};

#endif

