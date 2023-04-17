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

#include "HUSD_PythonConverter.h"
#include "XUSD_LockedGeoRegistry.h"
#include "XUSD_OverridesData.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
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
HUSD_PythonConverter::getPrim(const UT_StringRef &primpath) const
{
    if (myAnyLock)
    {
        XUSD_ConstDataPtr     outdata = myAnyLock->constData();

        if (outdata && outdata->isStageValid())
        {
            SdfPath              sdfpath = HUSDgetSdfPath(primpath);
            UsdStageWeakPtr      ptr = outdata->stage();
            UT_SharedPtr<UsdPrim> prim(new UsdPrim(ptr->GetPrimAtPath(sdfpath)));

            if (prim)
            {
                PY_InterpreterAutoLock pylock;

                return BOOST_NS::python::objects::make_ptr_instance<UsdPrim,
                    BOOST_NS::python::objects::pointer_holder<
                        UT_SharedPtr<UsdPrim>, UsdPrim>>::execute(prim);
            }
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

std::string
HUSD_PythonConverter::addLockedGeo(
        const UT_StringHolder &identifier,
        const std::map<std::string, std::string> &args,
        const GU_DetailHandle &gdh) const
{
    bool success = false;

    if (myAnyLock)
    {
        auto *layerlock = dynamic_cast<HUSD_AutoLayerLock*>(myAnyLock);
        if (!layerlock)
        {
            auto *writelock = dynamic_cast<HUSD_AutoWriteLock*>(myAnyLock);
            if (!writelock)
                return std::string();
            layerlock = new HUSD_AutoLayerLock(*writelock);
        }

        // If the following line stops compiling, it's likely because
        // the definition of SdfFileFormat::FileFormatArguments changed
        const XUSD_LockedGeoArgs &lgargs = args;
        XUSD_LockedGeoPtr lg = XUSD_LockedGeoRegistry::createLockedGeo(
                identifier, lgargs, gdh);

        if (layerlock->constData())
        {
            layerlock->addLockedGeos(XUSD_LockedGeoArray{lg});
            success = true;
        }
        
        if (layerlock != myAnyLock)
            delete layerlock;
        
        if (success)
            return SdfLayer::CreateIdentifier(identifier.toStdString(), lgargs);
    }

    return std::string();
}

bool
HUSD_PythonConverter::addHeldLayer(const UT_StringRef &identifier) const
{
    bool success = false;
    if (myAnyLock)
    {
        auto *layerlock = dynamic_cast<HUSD_AutoLayerLock*>(myAnyLock);
        if (!layerlock)
        {
            auto *writelock = dynamic_cast<HUSD_AutoWriteLock*>(myAnyLock);
            if (!writelock)
                return false;
            layerlock = new HUSD_AutoLayerLock(*writelock);
        }
        if (layerlock)
        {
            SdfLayerRefPtr layer = SdfLayer::Find(identifier.c_str());
            if (layer)
            {
                layerlock->addHeldLayers(XUSD_LayerArray{layer});
                success = true;
            }
        }
        if (layerlock != myAnyLock)
            delete layerlock;
    }
    return success;
}

bool
HUSD_PythonConverter::addSubLayer(const UT_StringRef &identifier) const
{
    bool success = false;

    if (myAnyLock)
    {
        auto *writelock = dynamic_cast<HUSD_AutoWriteLock *>(myAnyLock);
        if (writelock)
        {
            auto data = writelock->data();
            if (data)
            {
                success = data->addLayer(identifier.toStdString(),
                    SdfLayerOffset(), 0, XUSD_ADD_LAYERS_ALL_LOCKED, false);
            }
        }
    }

    return success;
}
