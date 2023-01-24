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

#ifndef __HUSD_DataHandle_h__
#define __HUSD_DataHandle_h__

#include "HUSD_API.h"
#include <UT/UT_Assert.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_SharedPtr.h>
#include <UT/UT_WeakPtr.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_StringSet.h>
#include <pxr/pxr.h>
#include <functional>

class UT_Color;
class UT_StringArray;
class HUSD_FindPrims;
class HUSD_MirrorRootLayer;

class HUSD_Overrides;
typedef UT_IntrusivePtr<HUSD_Overrides>		 HUSD_OverridesPtr;
typedef UT_IntrusivePtr<const HUSD_Overrides>	 HUSD_ConstOverridesPtr;
class HUSD_PostLayers;
typedef UT_IntrusivePtr<HUSD_PostLayers>	 HUSD_PostLayersPtr;
typedef UT_IntrusivePtr<const HUSD_PostLayers>	 HUSD_ConstPostLayersPtr;

class HUSD_LockedStage;
typedef UT_SharedPtr<HUSD_LockedStage> HUSD_LockedStagePtr;
typedef UT_Array<HUSD_LockedStagePtr> HUSD_LockedStageArray;
typedef UT_WeakPtr<HUSD_LockedStage> HUSD_LockedStageWeakPtr;

class HUSD_DataHandle;
typedef UT_StringMap<HUSD_DataHandle> HUSD_DataHandleMap;

// Use a SharedPtr instead of an IntrusivePtr for HUSD_LoadMasks because we
// want to be able to copy these objects.
class HUSD_LoadMasks;
typedef UT_SharedPtr<HUSD_LoadMasks>		 HUSD_LoadMasksPtr;

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_Data;
typedef UT_IntrusivePtr<XUSD_Data>		 XUSD_DataPtr;
typedef UT_IntrusivePtr<const XUSD_Data>	 XUSD_ConstDataPtr;
class XUSD_Layer;
typedef UT_IntrusivePtr<XUSD_Layer>		 XUSD_LayerPtr;
typedef UT_IntrusivePtr<const XUSD_Layer>	 XUSD_ConstLayerPtr;
class XUSD_DataLock;
typedef UT_IntrusivePtr<XUSD_DataLock>		 XUSD_DataLockPtr;
class XUSD_LockedGeo;
typedef UT_IntrusivePtr<XUSD_LockedGeo>		 XUSD_LockedGeoPtr;
typedef UT_Array<XUSD_LockedGeoPtr>		 XUSD_LockedGeoArray;
template <class T> class TfRefPtr;
class SdfLayer;
typedef TfRefPtr<SdfLayer> SdfLayerRefPtr;
typedef UT_Array<SdfLayerRefPtr> XUSD_LayerArray;

PXR_NAMESPACE_CLOSE_SCOPE

enum HUSD_MirroringType {
    HUSD_NOT_FOR_MIRRORING,
    HUSD_FOR_MIRRORING,
    HUSD_EXTERNAL_STAGE,
};

typedef std::function<UT_StringHolder(const UT_StringRef &oldpath)>
    HUSD_MakeNewPathFunc;

class HUSD_API HUSD_DataHandle
{
public:
				 HUSD_DataHandle(HUSD_MirroringType
					mirroring = HUSD_NOT_FOR_MIRRORING);
				 HUSD_DataHandle(const HUSD_DataHandle &src);
    explicit			 HUSD_DataHandle(void *stage_ptr);
				~HUSD_DataHandle();

    void			 reset(int nodeid);
    const HUSD_DataHandle	&operator=(const HUSD_DataHandle &src);
    // Create a new, empty stage in our XUSD_Data.
    void			 createNewData(
					const HUSD_LoadMasksPtr &
					    load_masks = HUSD_LoadMasksPtr(),
					const HUSD_DataHandle *
					    resolver_context_data = nullptr);
    // Share the stage from the src XUSD_Data object, unless the load_masks
    // is supplied, in which case we may need to create a new stage with the
    // specified load masks.
    bool			 createSoftCopy(const HUSD_DataHandle &src,
					const HUSD_LoadMasksPtr &load_masks,
					bool make_new_implicit_layer);
    // Create a new stage that is a copy of the src stage, but replacing any
    // composition of the "frompath" with the "topath". This check works
    // recursively, which may require performing other layer replacements
    // as we make modified versions of referencing layers (and layers that
    // reference those layers, and so on).
    bool			 createCopyWithReplacement(
					const HUSD_DataHandle &src,
					const UT_StringRef &frompath,
					const UT_StringRef &topath,
					HUSD_MakeNewPathFunc make_new_path,
					UT_StringSet &replaced_layers);
    // If the supplied load masks differ from the current load masks, create
    // a new stage that is a copy of our current stage but with the new load
    // masks (using createSoftCopy).
    bool        		 recreateWithLoadMasks(
                                        const HUSD_LoadMasks &load_masks);
    // Make a new stage by flattening the layer stack of our current stage.
    bool			 flattenLayers();
    // Make a new stage by flattening our current stage.
    bool			 flattenStage();

    // For an HUSD_FOR_MIRRORING data handle, create a duplicate of the src
    // data handle's stage and layer stack.
    bool			 mirror(const HUSD_DataHandle &src,
					const HUSD_LoadMasks &load_masks);
    // For an HUSD_FOR_MIRRORING data handle, update our stage's root layer
    // with the information in the HUSD_MirrorRootLayer. This is currently
    // just a description of the viewport camera.
    bool                         mirrorUpdateRootLayer(
                                        const HUSD_MirrorRootLayer &rootlayer);

    bool			 hasLayerColorIndex(int &clridx) const;
    int				 layerCount() const;
    int				 nodeId() const
				 { return myNodeId; }
    HUSD_LoadMasksPtr		 loadMasks() const;
    const std::string		&rootLayerIdentifier() const;

    bool                         isLocked() const;
    PXR_NS::XUSD_ConstDataPtr	 readLock(
                                        const HUSD_ConstOverridesPtr
                                            &overrides,
                                        const HUSD_ConstPostLayersPtr
                                            &postlayers,
					bool remove_layer_break) const;
    PXR_NS::XUSD_DataPtr	 writeOverridesLock(
                                        const HUSD_OverridesPtr
					    &overrides) const;
    PXR_NS::XUSD_DataPtr	 writeLock() const;
    PXR_NS::XUSD_LayerPtr	 layerLock(PXR_NS::XUSD_DataPtr &data,
                                        bool create_change_block) const;
    void			 release() const;

private:
    HUSD_ConstOverridesPtr	 currentOverrides() const;
    HUSD_ConstPostLayersPtr	 currentPostLayers() const;

    friend class HUSD_AutoReadLock;

    PXR_NS::XUSD_DataPtr	 myData;
    PXR_NS::XUSD_DataLockPtr	 myDataLock;
    HUSD_MirroringType		 myMirroring;
    int				 myNodeId;
};

// Parent class for read and write locks that permits reading.
class HUSD_API HUSD_AutoAnyLock : UT_NonCopyable
{
public:
    explicit				 HUSD_AutoAnyLock(
						const HUSD_DataHandle &handle)
					    : myHandle(handle)
					 { }
    virtual				~HUSD_AutoAnyLock()
					 { }

    bool                                 isStageValid() const;
    const HUSD_DataHandle		&dataHandle() const
					 { return myHandle; }
    virtual PXR_NS::XUSD_ConstDataPtr	 constData() const = 0;

private:
    const HUSD_DataHandle		&myHandle;
};

// Locks an HUSD_DataHandle for read-only operations.
class HUSD_API HUSD_AutoReadLock : public HUSD_AutoAnyLock
{
public:
    enum HUSD_OverridesUnchangedType { OVERRIDES_UNCHANGED };
    enum HUSD_RemoveLayerBreaksType { REMOVE_LAYER_BREAKS, KEEP_LAYER_BREAKS };

    explicit				 HUSD_AutoReadLock(
						const HUSD_DataHandle &handle);
    explicit				 HUSD_AutoReadLock(
						const HUSD_DataHandle &handle,
						HUSD_OverridesUnchangedType);
    explicit				 HUSD_AutoReadLock(
                                                const HUSD_DataHandle &handle,
                                                const HUSD_ConstOverridesPtr
                                                    &overrides,
                                                const HUSD_ConstPostLayersPtr
                                                    &postlayers,
                                                HUSD_RemoveLayerBreaksType
                                                    lbtype = KEEP_LAYER_BREAKS);
                                        ~HUSD_AutoReadLock() override;

    const PXR_NS::XUSD_ConstDataPtr	&data() const
					 { return myData; }
    PXR_NS::XUSD_ConstDataPtr	         constData() const override;

private:
    PXR_NS::XUSD_ConstDataPtr		 myData;
};

// Locks an HUSD_DataHandle in a way that allows read-only operations on the
// data, but write access to the supplied overrides layer.
class HUSD_API HUSD_AutoWriteOverridesLock : public HUSD_AutoAnyLock
{
public:
    explicit				 HUSD_AutoWriteOverridesLock(
						const HUSD_DataHandle &handle,
						const HUSD_OverridesPtr
						    &overrides);
                                        ~HUSD_AutoWriteOverridesLock() override;

    const PXR_NS::XUSD_DataPtr		&data() const
					 { return myData; }
    const HUSD_OverridesPtr		&overrides() const
					 { return myOverrides; }
    PXR_NS::XUSD_ConstDataPtr	         constData() const override;

private:
    PXR_NS::XUSD_DataPtr		 myData;
    HUSD_OverridesPtr			 myOverrides;
};

// Locks an HUSD_DataHandle for writing to the active layer in the context
// of the current stage.
class HUSD_API HUSD_AutoWriteLock : public HUSD_AutoAnyLock
{
public:
    explicit				 HUSD_AutoWriteLock(
						const HUSD_DataHandle &handle);
                                        ~HUSD_AutoWriteLock() override;

    const PXR_NS::XUSD_DataPtr		&data() const
					 { return myData; }
    void				 addLockedStages(const
					    HUSD_LockedStageArray &stages);
    PXR_NS::XUSD_ConstDataPtr	         constData() const override;

private:
    PXR_NS::XUSD_DataPtr		 myData;
};

// Locks an HUSD_DataHandle for writing to the active layer outside the
// context of the current stage (which is accessible read-only while editing
// this off-stage layer).
//
// A layer lock can be constructed from an HUSD_AutoWriteLock as well. This
// doesn't re-lock the data handle, but instead grabs from the write lock all
// the data it needs to implement the interface of this class. If the
// ScopedLock flag is passed as a second argument, an SdfChangeBlock is
// created for the lifetime of this object. This is only safe if this
// object will be destroyed before the next time the WriteLock is used
// to edit the stage.
class HUSD_API HUSD_AutoLayerLock : public HUSD_AutoAnyLock
{
public:
    enum ChangeBlockTag {
        ChangeBlock,
        NoChangeBlock
    };

    explicit				 HUSD_AutoLayerLock(
                                                const HUSD_DataHandle &handle,
                                                ChangeBlockTag change_block =
                                                    ChangeBlock);
    explicit				 HUSD_AutoLayerLock(
						const HUSD_AutoWriteLock &lock,
                                                ChangeBlockTag change_block =
                                                    NoChangeBlock);
                                        ~HUSD_AutoLayerLock() override;

    const PXR_NS::XUSD_LayerPtr		&layer() const
					 { return myLayer; }
    void				 addLockedGeos(const PXR_NS::
					    XUSD_LockedGeoArray &lockedgeos);
    void				 addHeldLayers(const PXR_NS::
					    XUSD_LayerArray &layers);
    void				 addReplacements(const PXR_NS::
					    XUSD_LayerArray &replacements);
    void				 addLockedStages(const
					    HUSD_LockedStageArray &stages);
    PXR_NS::XUSD_ConstDataPtr	         constData() const override;

private:
    PXR_NS::XUSD_DataPtr		 myData;
    PXR_NS::XUSD_LayerPtr		 myLayer;
    bool				 myOwnsHandleLock;
};

#endif

