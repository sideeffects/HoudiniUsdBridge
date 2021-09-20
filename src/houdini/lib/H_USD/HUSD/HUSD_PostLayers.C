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

#include "HUSD_PostLayers.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <PY/PY_InterpreterAutoLock.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_StringHolder.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/base/tf/pyPtrHelpers.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    const UT_StringLit theLayerNamesArrayToken("layernames");
    const UT_StringLit theLayersArrayToken("layers");
};

HUSD_PostLayers::HUSD_PostLayers()
    : myLockedToDataHandle(nullptr),
      myLockedToLayerIndex(-1),
      myVersionId(0)
{
}

HUSD_PostLayers::~HUSD_PostLayers()
{
    if (myLockedToDataHandle)
        release(nullptr);
}

int
HUSD_PostLayers::layerCount() const
{
    return myLayers.size();
}

const UT_StringHolder &
HUSD_PostLayers::layerName(int i) const
{
    return myLayerNames[i];
}

bool
HUSD_PostLayers::hasLayer(const UT_StringRef &name) const
{
    return (myLayerNames.find(name) >= 0);
}

PXR_NS::XUSD_LayerPtr
HUSD_PostLayers::layer(int i) const
{
    if (i < 0 || i >= myLayers.size())
        return XUSD_LayerPtr();

    return myLayers[i];
}

PXR_NS::XUSD_LayerPtr
HUSD_PostLayers::layer(const UT_StringRef &name) const
{
    int i = myLayerNames.find(name);

    return layer(i);
}

void *
HUSD_PostLayers::pythonLayer(int i) const
{
    if (i < 0 || i >= myLayers.size())
        return nullptr;

    SdfLayerRefPtr		 ptr = myLayers[i]->layer();
    PY_InterpreterAutoLock	 pylock;

    return TfMakePyPtr<SdfLayerHandle>::Execute(ptr).first;
}

void *
HUSD_PostLayers::pythonLayer(const UT_StringRef &name) const
{
    int idx = myLayerNames.find(name);

    return pythonLayer(idx);
}

void
HUSD_PostLayers::clear()
{
    UT_ASSERT(!myLockedToDataHandle);
    myLayerNames.clear();
    myLayers.clear();
    myVersionId++;
}

bool
HUSD_PostLayers::removeLayer(int i)
{
    if (i >= 0 && i < myLayers.size())
    {
        myLayerNames.removeIndex(i);
        myLayers.removeIndex(i);
        myVersionId++;

        return true;
    }

    return false;
}

bool
HUSD_PostLayers::removeLayer(const UT_StringRef &name)
{
    int idx = myLayerNames.find(name);

    return removeLayer(idx);
}

void
HUSD_PostLayers::writeLock(const HUSD_DataHandle &datahandle,
        const HUSD_LoadMasksPtr &loadmasks,
        const UT_StringHolder &layername)
{
    // Create a soft copy of the source data handle, as if we are a LOP node
    // that is going to edit this data (but we are free to edit the active
    // layer of this stage any way we want).
    UT_ASSERT(!myLockedToDataHandle);
    myLockedToDataHandle = &datahandle;
    myLockedToLayerName = layername;
    myDataHandle.createSoftCopy(datahandle, loadmasks, true);

    XUSD_LayerAtPathArray    layerarray;
    SdfLayerRefPtr           locklayer;

    // Look for the requested layer in our layers.
    for (myLockedToLayerIndex = 0;
         myLockedToLayerIndex < myLayerNames.size();
         myLockedToLayerIndex++)
    {
        if (myLayerNames[myLockedToLayerIndex] == layername)
        {
            locklayer = myLayers[myLockedToLayerIndex]->layer();
            break;
        }
        layerarray.append(XUSD_LayerAtPath(
            myLayers[myLockedToLayerIndex]->layer()));
    }

    if (layerarray.size() > 0 || locklayer)
    {
        HUSD_AutoWriteLock writelock(myDataHandle);

        // Add any weaker post layers to the stage.
        if (layerarray.size() > 0)
            writelock.data()->addLayers(
                layerarray, 0, XUSD_ADD_LAYERS_ALL_LOCKED, false);

        // Copy the current contents of the post layer into the stage's
        // active layer.
        if (locklayer)
            writelock.data()->activeLayer()->TransferContent(locklayer);
    }
}

const HUSD_DataHandle &
HUSD_PostLayers::lockedDataHandle()
{
    UT_ASSERT(myLockedToDataHandle);
    return myDataHandle;
}

void
HUSD_PostLayers::release(const HUSD_AutoWriteLock *writelock)
{
    // We shouldn't be releasing a postlayers that hasn't been locked.
    UT_ASSERT(myLockedToDataHandle);

    // If there was a write lock established on our data handle, then we want
    // to copy the active layer contents off the stage into the named scratch
    // layer. Otherwise, clear the named post layer. Bump the version id if
    // there is any chance something was changed.
    if (writelock)
    {
        XUSD_LayerPtr layer(new XUSD_Layer(HUSDcreateAnonymousLayer(
            UsdStageWeakPtr(), myLockedToLayerName.toStdString()),
            false));
        layer->layer()->TransferContent(writelock->data()->activeLayer());
        layer->layer()->SetPermissionToEdit(false);
        if (myLockedToLayerIndex >= myLayers.size())
        {
            myLayerNames.append(myLockedToLayerName);
            myLayers.append(layer);
        }
        else
            myLayers[myLockedToLayerIndex] = layer;
        myVersionId++;
    }
    else if (myLockedToLayerIndex < myLayers.size())
    {
        myLayers.removeIndex(myLockedToLayerIndex);
        myLayerNames.removeIndex(myLockedToLayerIndex);
        myVersionId++;
    }

    myLockedToDataHandle = nullptr;
    myLockedToLayerName.clear();
    myLockedToLayerIndex = -1;
}

void
HUSD_PostLayers::save(std::ostream &os) const
{
    UT_AutoJSONWriter	 writer(os, false);
    UT_JSONWriter	&w = *writer;

    w.jsonBeginMap();
    w.jsonKeyToken(theLayerNamesArrayToken.asRef());
    w.jsonBeginArray();
    for (auto &&layername : myLayerNames)
        w.jsonString(layername);
    w.jsonEndArray();

    w.jsonKeyToken(theLayersArrayToken.asRef());
    w.jsonBeginArray();
    for (auto &&layer : myLayers)
    {
	std::string	 str;
	layer->layer()->ExportToString(&str);
	w.jsonString(str.c_str());
    }
    w.jsonEndArray();
    w.jsonEndMap();
}

bool
HUSD_PostLayers::load(UT_IStream &is)
{
    UT_AutoJSONParser	 parser(is);
    UT_JSONValue	 rootvalue;

    clear();
    if (!rootvalue.parseValue(parser) || !rootvalue.getMap())
	return false;

    const UT_JSONValueMap	*map = rootvalue.getMap();
    const UT_JSONValueArray	*layernamesarray;
    const UT_JSONValueArray	*layersarray;

    layernamesarray = map->getArray(theLayerNamesArrayToken.asRef());
    layersarray = map->getArray(theLayersArrayToken.asRef());

    if (!layernamesarray ||
        !layersarray ||
        layernamesarray->size() != layersarray->size())
        return false;

    for (int i = 0; i < layernamesarray->size(); i++)
    {
        const UT_JSONValue *namevalue = layernamesarray->get(i);
        const UT_JSONValue *layervalue = layersarray->get(i);

        if (!namevalue || !namevalue->getStringHolder() ||
            !layervalue || !layervalue->getStringHolder())
        {
            clear();
            return false;
        };

        XUSD_LayerPtr layer(new XUSD_Layer(HUSDcreateAnonymousLayer(
            UsdStageWeakPtr(), namevalue->getStringHolder()->toStdString()),
            false));

        std::string layervaluestr(layervalue->getStringHolder()->toStdString());
        if (!layer->layer()->ImportFromString(layervaluestr))
        {
            clear();
            return false;
        }
        layer->layer()->SetPermissionToEdit(false);

        myLayerNames.append(*namevalue->getStringHolder());
        myLayers.append(layer);
    }

    return true;
}

void
HUSD_PostLayers::copy(const HUSD_PostLayers &src)
{
    clear();
    for (int i = 0; i < src.myLayerNames.size(); i++)
    {
        myLayerNames.append(src.myLayerNames[i]);
        myLayers.append(new XUSD_Layer(HUSDcreateAnonymousLayer(
            UsdStageWeakPtr(), src.myLayerNames[i].toStdString()),
            false));
        myLayers.last()->layer()->TransferContent(src.myLayers[i]->layer());
        myLayers.last()->layer()->SetPermissionToEdit(false);
    }
    myVersionId++;
}
