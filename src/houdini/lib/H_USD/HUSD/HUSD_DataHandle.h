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

class HUSD_LockedStage;
typedef UT_SharedPtr<HUSD_LockedStage> HUSD_LockedStagePtr;
typedef UT_Array<HUSD_LockedStagePtr> HUSD_LockedStageArray;
typedef UT_WeakPtr<HUSD_LockedStage> HUSD_LockedStageWeakPtr;

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
class XUSD_Ticket;
typedef UT_IntrusivePtr<XUSD_Ticket>		 XUSD_TicketPtr;
typedef UT_Array<XUSD_TicketPtr>		 XUSD_TicketArray;
template <class T> class TfRefPtr;
class SdfLayer;
typedef TfRefPtr<SdfLayer> SdfLayerRefPtr;
typedef UT_Array<SdfLayerRefPtr> XUSD_LayerArray;

PXR_NAMESPACE_CLOSE_SCOPE

enum HUSD_MirroringType {
    HUSD_NOT_FOR_MIRRORING,
    HUSD_FOR_MIRRORING
};

typedef std::function<UT_StringHolder(const UT_StringRef &oldpath)>
    HUSD_MakeNewPathFunc;

class HUSD_API HUSD_DataHandle
{
public:
				 HUSD_DataHandle(HUSD_MirroringType
					mirroring = HUSD_NOT_FOR_MIRRORING);
				 HUSD_DataHandle(const HUSD_DataHandle &src);
				~HUSD_DataHandle();

    void			 reset(int nodeid);
    const HUSD_DataHandle	&operator=(const HUSD_DataHandle &src);
    void			 createNewData(
					const HUSD_LoadMasksPtr &
					    load_masks = HUSD_LoadMasksPtr(),
					const HUSD_DataHandle *
					    resolver_context_data = nullptr);
    bool			 createSoftCopy(const HUSD_DataHandle &src,
					const HUSD_LoadMasksPtr &load_masks,
					bool make_new_implicit_layer);
    bool			 createCopyWithReplacement(
					const HUSD_DataHandle &src,
					const UT_StringRef &frompath,
					const UT_StringRef &topath,
					HUSD_MakeNewPathFunc make_new_path,
					UT_StringSet &replaced_layers);
    bool			 flattenLayers();
    bool			 flattenStage();
    bool			 mirror(const HUSD_DataHandle &src,
					const HUSD_LoadMasks &load_masks);
    bool                         mirrorUpdateRootLayer(
                                        const HUSD_MirrorRootLayer &rootlayer);

    bool			 hasLayerColorIndex(int &clridx) const;
    int				 layerCount() const;
    int				 nodeId() const
				 { return myNodeId; }
    HUSD_LoadMasksPtr		 loadMasks() const;
    const std::string		&rootLayerIdentifier() const;

    PXR_NS::XUSD_ConstDataPtr	 readLock(const HUSD_ConstOverridesPtr
					&overrides,
					bool remove_layer_break) const;
    PXR_NS::XUSD_DataPtr	 writeOverridesLock(const HUSD_OverridesPtr
					&overrides) const;
    PXR_NS::XUSD_DataPtr	 writeLock() const;
    PXR_NS::XUSD_LayerPtr	 layerLock(PXR_NS::XUSD_DataPtr &data) const;
    void			 release() const;

private:
    HUSD_ConstOverridesPtr	 currentOverrides() const;

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
    enum HUSD_RemoveLayerBreaksType { REMOVE_LAYER_BREAKS };

    explicit				 HUSD_AutoReadLock(
						const HUSD_DataHandle &handle);
    explicit				 HUSD_AutoReadLock(
						const HUSD_DataHandle &handle,
						HUSD_OverridesUnchangedType);
    explicit				 HUSD_AutoReadLock(
						const HUSD_DataHandle &handle,
						HUSD_RemoveLayerBreaksType);
    explicit				 HUSD_AutoReadLock(
						const HUSD_DataHandle &handle,
						const HUSD_ConstOverridesPtr
						    &overrides);
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
    enum ScopedTag { Scoped };

    explicit				 HUSD_AutoLayerLock(
						const HUSD_DataHandle &handle);
    explicit				 HUSD_AutoLayerLock(
						const HUSD_AutoWriteLock &lock);
    explicit				 HUSD_AutoLayerLock(
						const HUSD_AutoWriteLock &lock,
                                                ScopedTag);
                                        ~HUSD_AutoLayerLock() override;

    const PXR_NS::XUSD_LayerPtr		&layer() const
					 { return myLayer; }
    void				 addTickets(const PXR_NS::
					    XUSD_TicketArray &tickets);
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

