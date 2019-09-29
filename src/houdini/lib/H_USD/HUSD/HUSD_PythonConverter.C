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

#include "HUSD_PythonConverter.h"
#include "XUSD_OverridesData.h"
#include "XUSD_Data.h"
#include <PY/PY_InterpreterAutoLock.h>
#include <pxr/base/tf/pyPtrHelpers.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_PythonConverter::HUSD_PythonConverter(
	HUSD_AutoAnyLock &lock)
    : myAnyLock(&lock)
{
}

HUSD_PythonConverter::HUSD_PythonConverter(
	const HUSD_ConstOverridesPtr &overrides)
    : myAnyLock(nullptr),
      myOverrides(overrides)
{
}

HUSD_PythonConverter::~HUSD_PythonConverter()
{
}

void *
HUSD_PythonConverter::getEditableLayer() const
{
    if (myAnyLock)
    {
	HUSD_AutoLayerLock	*layerlock =
	    dynamic_cast<HUSD_AutoLayerLock *>(myAnyLock);

	if (layerlock)
	{
	    XUSD_LayerPtr	 outdata = layerlock->layer();

	    if (outdata)
	    {
		SdfLayerRefPtr		 ptr = outdata->layer();
		PY_InterpreterAutoLock	 pylock;

		return TfMakePyPtr<SdfLayerHandle>::Execute(ptr).first;
	    }
	}
    }

    return nullptr;
}

void *
HUSD_PythonConverter::getEditableOverridesLayer() const
{
    if (myAnyLock)
    {
	HUSD_AutoWriteOverridesLock	*writeoverrideslock =
	    dynamic_cast<HUSD_AutoWriteOverridesLock *>(myAnyLock);

	if (writeoverrideslock)
	{
	    SdfLayerRefPtr	 layer = writeoverrideslock->
		overrides()->data().layer(HUSD_OVERRIDES_CUSTOM_LAYER);

	    if (layer)
	    {
		PY_InterpreterAutoLock	 pylock;

		return TfMakePyPtr<SdfLayerHandle>::Execute(layer).first;
	    }
	}
    }

    return nullptr;
}

void *
HUSD_PythonConverter::getActiveLayer() const
{
    if (myAnyLock)
    {
	XUSD_ConstDataPtr	 outdata = myAnyLock->constData();

	if (outdata && outdata->isStageValid())
	{
	    SdfLayerRefPtr		 ptr = outdata->activeLayer();

	    // Because we may be called with just a read lock, it is possible
	    // that we will get back an empty pointer from activeLayer.
	    if (ptr)
	    {
		PY_InterpreterAutoLock	 pylock;

		return TfMakePyPtr<SdfLayerHandle>::Execute(ptr).first;
	    }
	}
    }

    return nullptr;
}

void *
HUSD_PythonConverter::getEditableStage() const
{
    if (myAnyLock)
    {
	HUSD_AutoWriteLock	*writelock =
	    dynamic_cast<HUSD_AutoWriteLock *>(myAnyLock);

	if (writelock)
	{
	    XUSD_DataPtr	 outdata = writelock->data();

	    if (outdata && outdata->isStageValid())
	    {
		UsdStageWeakPtr		 ptr = outdata->stage();
		PY_InterpreterAutoLock	 pylock;

		return TfMakePyPtr<UsdStageWeakPtr>::Execute(ptr).first;
	    }
	}
    }

    return nullptr;
}

void *
HUSD_PythonConverter::getEditableOverridesStage() const
{
    if (myAnyLock)
    {
	HUSD_AutoWriteOverridesLock	*writeoverrideslock =
	    dynamic_cast<HUSD_AutoWriteOverridesLock *>(myAnyLock);

	if (writeoverrideslock)
	{
	    XUSD_DataPtr	 outdata = writeoverrideslock->data();

	    if (outdata && outdata->isStageValid())
	    {
		UsdStageWeakPtr		 ptr = outdata->stage();
		PY_InterpreterAutoLock	 pylock;

		return TfMakePyPtr<UsdStageWeakPtr>::Execute(ptr).first;
	    }
	}
    }

    return nullptr;
}

void *
HUSD_PythonConverter::getStage() const
{
    if (myAnyLock)
    {
	XUSD_ConstDataPtr	 outdata = myAnyLock->constData();

	if (outdata && outdata->isStageValid())
	{
	    UsdStageWeakPtr		 ptr = outdata->stage();
	    PY_InterpreterAutoLock	 pylock;

	    return TfMakePyPtr<UsdStageWeakPtr>::Execute(ptr).first;
	}
    }

    return nullptr;
}

void *
HUSD_PythonConverter::getSourceLayer(int layerindex) const
{
    if (myAnyLock)
    {
	XUSD_ConstDataPtr	 outdata = myAnyLock->constData();

	if (outdata && outdata->isStageValid())
	{
	    const auto		&layers = outdata->sourceLayers();

	    if (layerindex < layers.size())
	    {
		SdfLayerRefPtr		 ptr = layers(layerindex).myLayer;
		PY_InterpreterAutoLock	 pylock;

		return TfMakePyPtr<SdfLayerHandle>::Execute(ptr).first;
	    }
	}
    }

    return nullptr;
}

int
HUSD_PythonConverter::getSourceLayerCount() const
{
    if (myAnyLock)
    {
	XUSD_ConstDataPtr	 outdata = myAnyLock->constData();

	if (outdata && outdata->isStageValid())
	{
	    const auto		&layers = outdata->sourceLayers();

	    return layers.size();
	}
    }

    return 0;
}

void *
HUSD_PythonConverter::getOverridesLayer(HUSD_OverridesLayerId layer_id) const
{
    if (myOverrides)
    {
	SdfLayerRefPtr		layer = myOverrides->data().layer(layer_id);
	
	if (layer)
	{
	    PY_InterpreterAutoLock	 pylock;

	    return TfMakePyPtr<SdfLayerHandle>::Execute(layer).first;
	}
    }

    return nullptr;
}

