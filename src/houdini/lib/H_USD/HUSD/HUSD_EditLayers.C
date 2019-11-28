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

#include "HUSD_EditLayers.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include "XUSD_TicketRegistry.h"
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_EditLayers::HUSD_EditLayers(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock),
      myEditRootLayer(true),
      myAddLayerPosition(0)
{
}

HUSD_EditLayers::~HUSD_EditLayers()
{
}

bool
HUSD_EditLayers::removeLayer(const UT_StringRef &filepath) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	if (filepath.isstring())
	{
	    if (myEditRootLayer)
	    {
		return outdata->removeLayer(filepath.toStdString());
	    }
	    else
	    {
		auto	 paths = outdata->activeLayer()->GetSubLayerPaths();
		int	 index = paths.Find(filepath.toStdString());

		if (index >= 0)
		    outdata->activeLayer()->RemoveSubLayerPath(index);
	    }
	}

	return true;
    }

    return false;
}

bool
HUSD_EditLayers::addLayer(const UT_StringRef &filepath,
	const HUSD_LayerOffset &offset,
	const UT_StringMap<UT_StringHolder> &refargs,
	const GU_DetailHandle &gdh) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	SdfFileFormat::FileFormatArguments args;

	for (auto &&it : refargs)
	    args[it.first.toStdString()] = it.second.toStdString();

	if (gdh.isValid())
	    outdata->addTicket(
		XUSD_TicketRegistry::createTicket(filepath, args, gdh));

	if (filepath.isstring())
	{
	    std::string		 fileid;

	    fileid = SdfLayer::CreateIdentifier(filepath.toStdString(), args);
	    if (myEditRootLayer)
	    {
		if (!outdata->addLayer(fileid,
		    HUSDgetSdfLayerOffset(offset),
		    myAddLayerPosition,
		    XUSD_ADD_LAYER_LOCKED))
		    return false;
	    }
	    else
	    {
		SdfLayerRefPtr	 layer = outdata->activeLayer();

		if ((int)layer->GetSubLayerPaths().Find(fileid) < 0)
		{
		    int pos = myAddLayerPosition;

		    if (pos < 0 || pos > layer->GetNumSubLayerPaths())
			pos = layer->GetNumSubLayerPaths();
		    layer->InsertSubLayerPath(fileid, pos);
		    layer->SetSubLayerOffset(
			HUSDgetSdfLayerOffset(offset), pos);
		}
		else
		{
		    HUSD_ErrorScope::addError(HUSD_ERR_DUPLICATE_SUBLAYER,
			filepath.c_str());
		    return false;
		}
	    }
	}

	return true;
    }

    return false;
}

bool
HUSD_EditLayers::addLayerForEdit(const UT_StringRef &filepath,
	const UT_StringMap<UT_StringHolder> &refargs,
	const GU_DetailHandle &gdh) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	SdfFileFormat::FileFormatArguments	 args;

	for (auto &&it : refargs)
	    args[it.first.toStdString()] = it.second.toStdString();

	// Even though we will be making a copy of this layer to an
	// anonymous new USD layer, we must keep the ticket active in case
	// there are volume primitives that need to be kept in memory.
	if (gdh.isValid())
	    outdata->addTicket(
		XUSD_TicketRegistry::createTicket(filepath, args, gdh));

	if (filepath.isstring())
	{
	    // Pass 0 for the layer position, since we can
	    // only edit the strongest layer in the stage.
	    if (!outdata->addLayer(
		SdfLayer::CreateIdentifier(filepath.toStdString(), args),
		SdfLayerOffset(), 0,
		XUSD_ADD_LAYER_EDITABLE))
		return false;
	}

	return true;
    }

    return false;
}

bool
HUSD_EditLayers::addLayerFromSource(const UT_StringRef &usdsource,
	bool allow_editing) const
{
    auto		 outdata = myWriteLock.data();
    bool		 success = false;

    if (outdata && outdata->isStageValid())
    {
	if (outdata->addLayer())
	{
	    SdfLayerRefPtr	 layer = outdata->activeLayer();
	    SdfLayerRefPtr	 tmplayer = HUSDcreateAnonymousLayer();

	    tmplayer->TransferContent(layer);
	    if (layer->ImportFromString(usdsource.toStdString()))
	    {
		SdfPath	 infopath = HUSDgetSdfPath(
			    HUSD_Constants::getHoudiniLayerInfoPrimPath());

		if (HUSDcopySpec(tmplayer, infopath, layer, infopath))
		{
		    if (!allow_editing)
			success = outdata->addLayer();
		    else
			success = true;
		}
	    }
	}
    }

    return success;
}

bool
HUSD_EditLayers::addLayer() const
{
    auto		 outdata = myWriteLock.data();
    bool		 success = false;

    // We don't allow adding an empty layer as a sublayer on the active layer.
    // This only makes sense for editing the root layer.
    UT_ASSERT(myEditRootLayer);
    if (myEditRootLayer && outdata && outdata->isStageValid())
	success = outdata->addLayer();

    return success;
}

bool
HUSD_EditLayers::applyLayerBreak() const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
	return outdata->applyLayerBreak();

    return false;
}

