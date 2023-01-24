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

#include "HUSD_DataHandle.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_Overrides.h"
#include "HUSD_LoadMasks.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <UT/UT_StringArray.h>

PXR_NAMESPACE_USING_DIRECTIVE

bool
HUSD_AutoAnyLock::isStageValid() const
{
    return (constData() && constData()->isStageValid());
}

HUSD_AutoReadLock::HUSD_AutoReadLock(
	const HUSD_DataHandle &handle)
    : HUSD_AutoAnyLock(handle)
{
    myData = dataHandle().readLock(HUSD_ConstOverridesPtr(),
        HUSD_ConstPostLayersPtr(), false);
}

HUSD_AutoReadLock::HUSD_AutoReadLock(
	const HUSD_DataHandle &handle,
	HUSD_OverridesUnchangedType)
    : HUSD_AutoAnyLock(handle)
{
    myData = dataHandle().readLock(dataHandle().currentOverrides(),
        dataHandle().currentPostLayers(), false);
}

HUSD_AutoReadLock::HUSD_AutoReadLock(
	const HUSD_DataHandle &handle,
	const HUSD_ConstOverridesPtr &overrides,
	const HUSD_ConstPostLayersPtr &postlayers,
        HUSD_RemoveLayerBreaksType lbtype)
    : HUSD_AutoAnyLock(handle)
{
    myData = dataHandle().readLock(overrides, postlayers,
        lbtype == REMOVE_LAYER_BREAKS);
}

HUSD_AutoReadLock::~HUSD_AutoReadLock()
{
    dataHandle().release();
}

XUSD_ConstDataPtr
HUSD_AutoReadLock::constData() const
{
    return myData;
}

HUSD_AutoWriteLock::HUSD_AutoWriteLock(const HUSD_DataHandle &handle)
    : HUSD_AutoAnyLock(handle)
{
    myData = dataHandle().writeLock();
}

HUSD_AutoWriteLock::~HUSD_AutoWriteLock()
{
    dataHandle().release();
}

void
HUSD_AutoWriteLock::addLockedStages(const HUSD_LockedStageArray &stages)
{
    myData->addLockedStages(stages);
}

XUSD_ConstDataPtr
HUSD_AutoWriteLock::constData() const
{
    return myData;
}

HUSD_AutoWriteOverridesLock::HUSD_AutoWriteOverridesLock(
	const HUSD_DataHandle &handle,
	const HUSD_OverridesPtr &overrides)
    : HUSD_AutoAnyLock(handle),
      myOverrides(overrides)
{
    myData = dataHandle().writeOverridesLock(overrides);
}

HUSD_AutoWriteOverridesLock::~HUSD_AutoWriteOverridesLock()
{
    dataHandle().release();
}

PXR_NS::XUSD_ConstDataPtr
HUSD_AutoWriteOverridesLock::constData() const
{
    return myData;
}

HUSD_AutoLayerLock::HUSD_AutoLayerLock(const HUSD_DataHandle &handle,
        ChangeBlockTag change_block)
    : HUSD_AutoAnyLock(handle),
      myOwnsHandleLock(true)
{
    // The layerLock call can create an SdfChangeBlock which is destroyed when
    // this object is destroyed. Choose based on the ChangeBlockTag.
    myLayer = dataHandle().layerLock(myData,
        change_block == ChangeBlock);
}

HUSD_AutoLayerLock::HUSD_AutoLayerLock(const HUSD_AutoWriteLock &lock,
        ChangeBlockTag change_block)
    : HUSD_AutoAnyLock(lock.dataHandle()),
      myOwnsHandleLock(false)
{
    // If the creator of this object knows it will leave scope before the
    // write lock is used again, it _is_ safe to create an SdfChangeBlock.
    myData = lock.data();
    if (myData && myData->isStageValid())
	myLayer = new XUSD_Layer(myData->activeLayer(),
            change_block == ChangeBlock);
}

HUSD_AutoLayerLock::~HUSD_AutoLayerLock()
{
    if (myOwnsHandleLock)
	dataHandle().release();
}

void
HUSD_AutoLayerLock::addLockedGeos(const PXR_NS::XUSD_LockedGeoArray &lockedgeos)
{
    myData->addLockedGeos(lockedgeos);
}

void
HUSD_AutoLayerLock::addHeldLayers(const PXR_NS::XUSD_LayerArray &layers)
{
    myData->addHeldLayers(layers);
}

void
HUSD_AutoLayerLock::addReplacements(const PXR_NS::XUSD_LayerArray &replacements)
{
    myData->addReplacements(replacements);
}

void
HUSD_AutoLayerLock::addLockedStages(const HUSD_LockedStageArray &stages)
{
    myData->addLockedStages(stages);
}

XUSD_ConstDataPtr
HUSD_AutoLayerLock::constData() const
{
    return myData;
}

HUSD_DataHandle::HUSD_DataHandle(HUSD_MirroringType mirroring)
    : myNodeId(OP_INVALID_ITEM_ID),
      myMirroring(mirroring)
{
}

HUSD_DataHandle::HUSD_DataHandle(const HUSD_DataHandle &src)
    : myMirroring(HUSD_NOT_FOR_MIRRORING)
{
    *this = src;
}

HUSD_DataHandle::HUSD_DataHandle(void *stage_ptr)
    : myNodeId(OP_INVALID_ITEM_ID),
      myMirroring(HUSD_EXTERNAL_STAGE)
{
    UsdStageWeakPtr stage =
            BOOST_NS::python::extract<UsdStageWeakPtr>((PyObject*)stage_ptr);
    myData.reset(new XUSD_Data(stage));
    myDataLock = myData->myDataLock;
}

HUSD_DataHandle::~HUSD_DataHandle()
{
}

void
HUSD_DataHandle::reset(int nodeid)
{
    myData.reset();
    myDataLock.reset();
    myNodeId = nodeid;
}

const HUSD_DataHandle &
HUSD_DataHandle::operator=(const HUSD_DataHandle &src)
{
    // For safe assignment, data handles must already have the same
    // mirroring value.
    myMirroring = src.myMirroring;
    myData = src.myData;
    myDataLock = src.myDataLock;
    myNodeId = src.myNodeId;

    return *this;
}

void
HUSD_DataHandle::createNewData(const HUSD_LoadMasksPtr &load_masks,
	const HUSD_DataHandle *resolver_context_data)
{
    ArResolverContext	 resolver_context;

    // We need to get the resolver context before resetting our data in case
    // the resolver_context_data == this.
    if (resolver_context_data && resolver_context_data->myData)
	resolver_context = resolver_context_data->myData->resolverContext();

    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING);
    if (!myData || myData->isStageValid())
	myData.reset(new XUSD_Data(myMirroring));

    // If we are passed an HUSD_DataHandle to provide our resolver context, we
    // don't need for that data handle to be locked. It is always safe to ask
    // for the resolver context from an XUSD_Data because the resolver context
    // is immutable on the stage.
    if (resolver_context_data && resolver_context_data->myData)
	myData->createNewData(load_masks, myNodeId,
	    UsdStageWeakPtr(), &resolver_context);
    else
	myData->createNewData(load_masks, myNodeId,
	    UsdStageWeakPtr(), nullptr);
    myDataLock = myData->myDataLock;
}

bool
HUSD_DataHandle::createSoftCopy(const HUSD_DataHandle &src,
	const HUSD_LoadMasksPtr &load_masks,
	bool make_new_implicit_layer)
{
    // We are just looking at the layers on the src, so it's safe to use
    // whatever overrides are currently there when locking, to avoid doing
    // any useless recomposition.
    HUSD_AutoReadLock	 lock(src, HUSD_AutoReadLock::OVERRIDES_UNCHANGED);
    bool		 success = false;

    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING &&
	      src.myMirroring == HUSD_NOT_FOR_MIRRORING);
    if (!myData || myData->isStageValid())
	myData.reset(new XUSD_Data(myMirroring));
    if (lock.data() && lock.data()->isStageValid())
    {
	myData->createSoftCopy(
	    *lock.data(), load_masks, make_new_implicit_layer);
	success = true;
    }
    myDataLock = myData->myDataLock;

    return success;
}

bool
HUSD_DataHandle::createCopyWithReplacement(
	const HUSD_DataHandle &src,
	const UT_StringRef &frompath,
	const UT_StringRef &topath,
	HUSD_MakeNewPathFunc make_new_path,
	UT_StringSet &replaced_layers)
{
    HUSD_AutoReadLock	 lock(src, HUSD_AutoReadLock::OVERRIDES_UNCHANGED);
    bool		 success = false;

    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING &&
	      src.myMirroring == HUSD_NOT_FOR_MIRRORING);
    if (!myData || myData->isStageValid())
	myData.reset(new XUSD_Data(myMirroring));
    if (lock.data() && lock.data()->isStageValid())
    {
	myData->createCopyWithReplacement(
	    *lock.data(), frompath, topath, myNodeId,
	    make_new_path, replaced_layers);
	success = true;
    }
    myDataLock = myData->myDataLock;

    return success;
}

bool
HUSD_DataHandle::recreateWithLoadMasks(const HUSD_LoadMasks &load_masks)
{
    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING);
    // If we don't already have a stage, or if this data handle is currently
    // locked, this operation immediately fails.
    if (!myData || myDataLock->isLocked())
        return false;

    bool load_masks_empty =
        load_masks.populateAll() &&
        load_masks.loadAll() &&
        load_masks.muteLayers().empty();

    // Early exit if nothing has changed.
    if (!loadMasks() && load_masks_empty)
        return true;
    if (loadMasks() && *loadMasks() == load_masks)
        return true;

    // Change our XUSD_Data to be a copy of our old XUSD_Data, but with a
    // different HUSD_LoadMasksPtr value.
    auto old_data = myData;
    myData.reset(new XUSD_Data(myMirroring));
    myData->createSoftCopy(
        *old_data, UTmakeShared<HUSD_LoadMasks>(load_masks), false);
    myDataLock = myData->myDataLock;

    return true;
}

bool
HUSD_DataHandle::mirror(const HUSD_DataHandle &src,
	const HUSD_LoadMasks &load_masks)
{
    // We are just looking at the layers on the src, so it's safe to use
    // whatever overrides are currently there when locking, to avoid doing
    // any useless recomposition.
    HUSD_AutoReadLock	 lock(src, HUSD_AutoReadLock::OVERRIDES_UNCHANGED);
    bool		 success = false;

    UT_ASSERT(myMirroring == HUSD_FOR_MIRRORING &&
	      src.myMirroring == HUSD_NOT_FOR_MIRRORING);
    if (!myData)
	myData.reset(new XUSD_Data(myMirroring));
    if (lock.data() && lock.data()->isStageValid())
    {
	myData->mirror(*lock.data(), load_masks);
	myDataLock = myData->myDataLock;
	success = true;
    }
    else
    {
	reset(myNodeId);
	success = true;
    }

    return success;
}

bool
HUSD_DataHandle::mirrorUpdateRootLayer(const HUSD_MirrorRootLayer &rootlayer)
{
    if (myData)
        return myData->mirrorUpdateRootLayer(rootlayer);

    return true;
}

bool
HUSD_DataHandle::flattenLayers()
{
    XUSD_DataPtr	 new_data;
    bool		 success = false;

    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING);
    if (myData)
    {
	HUSD_AutoReadLock	 lock(*this);

	// Lock ourselves for reading, and make sure we have a valid stage.
	if (lock.data() && lock.data()->isStageValid())
	{
	    // Create a new XUSD_Data and initialize it by flattening the
	    // layers from the read lock on ourselves.
	    new_data.reset(new XUSD_Data(myMirroring));
	    new_data->flattenLayers(*lock.data(), myNodeId);
	    success = true;
	}
    }
    else
    {
	new_data.reset(new XUSD_Data(myMirroring));
	success = true;
    }

    if (success)
    {
	myData = new_data;
	myDataLock = myData->myDataLock;
    }

    return success;
}

bool
HUSD_DataHandle::flattenStage()
{
    XUSD_DataPtr	 new_data;
    bool		 success = false;

    UT_ASSERT(myMirroring == HUSD_NOT_FOR_MIRRORING);
    if (myData)
    {
	HUSD_AutoReadLock	 lock(*this);

	// Lock ourselves for reading, and make sure we have a valid stage.
	if (lock.data() && lock.data()->isStageValid())
	{
	    // Create a new XUSD_Data and initialize it by flattening the
	    // stage from the read lock on ourselves.
	    new_data.reset(new XUSD_Data(myMirroring));
	    new_data->flattenStage(*lock.data(), myNodeId);
	    success = true;
	}
    }
    else
    {
	new_data.reset(new XUSD_Data(myMirroring));
	success = true;
    }

    if (success)
    {
	myData = new_data;
	myDataLock = myData->myDataLock;
    }

    return success;
}

bool
HUSD_DataHandle::hasLayerColorIndex(int &clridx) const
{
    if (myData && myDataLock && myData->sourceLayers().size() > 0)
	return myData->sourceLayers().last().hasLayerColorIndex(clridx);

    return false;
}

int
HUSD_DataHandle::layerCount() const
{
    if (myData && myDataLock)
	return myData->sourceLayers().size();

    return 0;
}

HUSD_ConstOverridesPtr
HUSD_DataHandle::currentOverrides() const
{
    if (myData && myDataLock)
    {
	UT_Lock::Scope	 lock(myDataLock->myMutex);

	return myData->overrides();
    }

    return HUSD_ConstOverridesPtr();
}

HUSD_ConstPostLayersPtr
HUSD_DataHandle::currentPostLayers() const
{
    if (myData && myDataLock)
    {
        UT_Lock::Scope	 lock(myDataLock->myMutex);

        return myData->postLayers();
    }

    return HUSD_ConstPostLayersPtr();
}

HUSD_LoadMasksPtr
HUSD_DataHandle::loadMasks() const
{
    if (myData)
	return myData->loadMasks();

    return HUSD_LoadMasksPtr();
}

const std::string &
HUSD_DataHandle::rootLayerIdentifier() const
{
    static const std::string	 theEmptyString;

    if (myData)
	return myData->rootLayerIdentifier();

    return theEmptyString;
}

bool
HUSD_DataHandle::isLocked() const
{
    return (myData && myDataLock->isLocked());
}

XUSD_ConstDataPtr
HUSD_DataHandle::readLock(const HUSD_ConstOverridesPtr &overrides,
        const HUSD_ConstPostLayersPtr &postlayers,
	bool remove_layer_breaks) const
{
    // It's okay to try to lock an empty handle. Just return nullptr.
    if (!myData || !myDataLock)
	return XUSD_ConstDataPtr();

    UT_Lock::Scope	 lock(myDataLock->myMutex);

    if (myDataLock->myWriteLock ||
	myDataLock->myLayerLock ||
	(myDataLock->myLockedNodeId != OP_INVALID_ITEM_ID &&
	 myDataLock->myLockedNodeId != myNodeId) ||
	(myDataLock->myLockCount > 0 &&
	 myData->overrides() != overrides))
    {
        // We shouldn't be in here if we are the only one holding a pointer
        // to our data lock. For one, the other holder of our lock should
        // have a shared pointer to the same object. And if not, then the
        // UT_Lock::Scope above will set a byte to zero in a freed memeory
        // area when we leave this function. Note that if we are the only
        // holder of this lock pointer, its ref count will be 2 (myDataLock
        // and myData->myDataLock).
        UT_ASSERT(myDataLock->use_count() > 2);

	XUSD_DataPtr	 locked_data = myData;
	UT_StringHolder	 nodepath;

	HUSDgetNodePath(myNodeId, nodepath);
	HUSD_ErrorScope::addWarning(HUSD_ERR_READ_LOCK_FAILED,
	    nodepath.c_str());
	SYSconst_cast(this)->createNewData(locked_data->loadMasks(), this);
	SYSconst_cast(this)->myData->createHardCopy(*locked_data);
    }

    myDataLock->myLockCount++;
    if (myDataLock->myLockCount == 1)
    {
	myDataLock->myLockedNodeId = myNodeId;
	myData->afterLock(false, overrides,
	    HUSD_OverridesPtr(), postlayers, remove_layer_breaks);
    }

    return myData;
}

XUSD_DataPtr
HUSD_DataHandle::writeLock() const
{
    // It's okay to try to lock an empty handle. Just return nullptr.
    if (!myData || !myDataLock)
	return XUSD_DataPtr();

    UT_Lock::Scope	 lock(myDataLock->myMutex);

    if (myDataLock->myWriteLock ||
	myDataLock->myLayerLock ||
	myDataLock->myLockCount ||
	myDataLock->myLockedNodeId != OP_INVALID_ITEM_ID)
    {
        // We shouldn't be in here if we are the only one holding a pointer
        // to our data lock. See the corresponding comment in readLock for a
        // more detailed explanation.
        UT_ASSERT(myDataLock->use_count() > 2);

	XUSD_DataPtr	 locked_data = myData;
	UT_StringHolder	 nodepath;

	HUSDgetNodePath(myNodeId, nodepath);
	HUSD_ErrorScope::addWarning(HUSD_ERR_WRITE_LOCK_FAILED,
	    nodepath.c_str());
	SYSconst_cast(this)->createNewData(locked_data->loadMasks(), this);
	SYSconst_cast(this)->myData->createHardCopy(*locked_data);
    }

    myDataLock->myLockCount++;
    myDataLock->myLockedNodeId = myNodeId;
    myDataLock->myWriteLock = true;
    myData->afterLock(true);

    return myData;
}

XUSD_DataPtr
HUSD_DataHandle::writeOverridesLock(
	const HUSD_OverridesPtr &overrides) const
{
    // It's okay to try to lock an empty handle. Just return nullptr.
    if (!myData || !myDataLock)
	return XUSD_DataPtr();

    UT_Lock::Scope	 lock(myDataLock->myMutex);

    if (myDataLock->myWriteLock ||
	myDataLock->myLayerLock ||
	myDataLock->myLockCount ||
	myDataLock->myLockedNodeId != OP_INVALID_ITEM_ID)
    {
        // We shouldn't be in here if we are the only one holding a pointer
        // to our data lock. See the corresponding comment in readLock for a
        // more detailed explanation.
        UT_ASSERT(myDataLock->use_count() > 2);

	XUSD_DataPtr	 locked_data = myData;
	UT_StringHolder	 nodepath;

	HUSDgetNodePath(myNodeId, nodepath);
	HUSD_ErrorScope::addWarning(HUSD_ERR_OVERRIDE_LOCK_FAILED,
	    nodepath.c_str());
	SYSconst_cast(this)->createNewData(locked_data->loadMasks(), this);
	SYSconst_cast(this)->myData->createHardCopy(*locked_data);
    }

    myDataLock->myLockCount++;
    myDataLock->myLockedNodeId = myNodeId;
    myDataLock->myWriteLock = true;
    myData->afterLock(false, HUSD_ConstOverridesPtr(), overrides);

    return myData;
}

XUSD_LayerPtr
HUSD_DataHandle::layerLock(XUSD_DataPtr &data, bool create_change_block) const
{
    // It's okay to try to lock an empty handle. Just return nullptr.
    if (!myData || !myDataLock)
	return XUSD_LayerPtr();

    UT_Lock::Scope	 lock(myDataLock->myMutex);

    if (myDataLock->myWriteLock ||
	myDataLock->myLayerLock ||
	myDataLock->myLockCount ||
	myDataLock->myLockedNodeId != OP_INVALID_ITEM_ID)
    {
        // We shouldn't be in here if we are the only one holding a pointer
        // to our data lock. See the corresponding comment in readLock for a
        // more detailed explanation.
        UT_ASSERT(myDataLock->use_count() > 2);

	XUSD_DataPtr	 locked_data = myData;
	UT_StringHolder	 nodepath;

	HUSDgetNodePath(myNodeId, nodepath);
	HUSD_ErrorScope::addWarning(HUSD_ERR_LAYER_LOCK_FAILED,
	    nodepath.c_str());
	SYSconst_cast(this)->createNewData(locked_data->loadMasks(), this);
	SYSconst_cast(this)->myData->createHardCopy(*locked_data);
    }

    myDataLock->myLockCount++;
    myDataLock->myLockedNodeId = myNodeId;
    myDataLock->myLayerLock = true;
    myData->afterLock(false);
    data = myData;

    return myData->editActiveSourceLayer(create_change_block);
}

void
HUSD_DataHandle::release() const
{
    if (myData && myDataLock)
    {
	UT_Lock::Scope	 lock(myDataLock->myMutex);

        // We shouldn't be unlocking something we didn't lock, or that isn't
        // actually locked any more.
        UT_ASSERT(myDataLock->myLockedNodeId == myNodeId);
        UT_ASSERT(myDataLock->myLockCount > 0);
	if (myDataLock->myLockedNodeId == myNodeId)
	{
	    if (myDataLock->myWriteLock)
	    {
		myData->afterRelease();
		myDataLock->myWriteLock = false;
		myDataLock->myLockCount--;
		UT_ASSERT(myDataLock->myLockCount == 0);
		myDataLock->myLockedNodeId = OP_INVALID_ITEM_ID;
	    }
	    else if (myDataLock->myLayerLock)
	    {
		myData->afterRelease();
		myDataLock->myLayerLock = false;
		myDataLock->myLockCount--;
		UT_ASSERT(myDataLock->myLockCount == 0);
		myDataLock->myLockedNodeId = OP_INVALID_ITEM_ID;
	    }
	    else
	    {
		myDataLock->myLockCount--;
		if (myDataLock->myLockCount == 0)
		{
		    myData->afterRelease();
		    myDataLock->myLockedNodeId = OP_INVALID_ITEM_ID;
		}
	    }
	}
    }
}

