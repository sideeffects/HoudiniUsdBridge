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
#include "XUSD_LockedGeoRegistry.h"
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_EditLayers::HUSD_EditLayers(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock),
      myEditRootLayer(true),
      myAddLayerPosition(0),
      myCopyRootPrimMetadataToStage(false)
{
}

HUSD_EditLayers::~HUSD_EditLayers()
{
}

bool
HUSD_EditLayers::removeLayers(const UT_StringArray &filepaths) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
        if (myEditRootLayer)
        {
            std::set<std::string>    pathset;

            for (auto &&filepath : filepaths)
                if (filepath.isstring())
                    pathset.insert(filepath.toStdString());

            return outdata->removeLayers(pathset);
        }
        else
        {
            auto paths = outdata->activeLayer()->GetSubLayerPaths();
            SdfChangeBlock changeblock;

            for (auto &&filepath : filepaths)
            {
                if (filepath.isstring())
                {
                    int index = paths.Find(filepath.toStdString());

                    if (index >= 0)
                        outdata->activeLayer()->RemoveSubLayerPath(index);
                }
            }
        }

	return true;
    }

    return false;
}

bool
HUSD_EditLayers::addLayers(const UT_StringArray &filepaths,
        const UT_Array<HUSD_LayerOffset> &offsets,
        const UT_Array<UT_StringMap<UT_StringHolder>> &refargs,
        const UT_Array<GU_DetailHandle> &gdhs) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
        std::vector<std::string>     paths_to_add;
        SdfLayerOffsetVector         offsets_to_add;
        
        for (exint i = 0; i < filepaths.size(); ++i)
        {
            std::string pathstr = filepaths(i).toStdString();
            SdfFileFormat::FileFormatArguments args;
            if (refargs.size() > i)
                HUSDconvertToFileFormatArguments(refargs(i), args);
            if (gdhs.size() > i)
                outdata->addLockedGeo(
                        XUSD_LockedGeoRegistry::createLockedGeo(
                                pathstr, args, gdhs(i)));
            paths_to_add.push_back(SdfLayer::CreateIdentifier(pathstr, args));
        }

        for (auto &&offset : offsets)
            offsets_to_add.push_back(HUSDgetSdfLayerOffset(offset));

        if (myEditRootLayer)
        {
            if (!outdata->addLayers(paths_to_add, offsets_to_add,
                    myAddLayerPosition, XUSD_ADD_LAYERS_ALL_LOCKED,
                    myCopyRootPrimMetadataToStage))
                return false;
        }
        else
        {
            SdfLayerRefPtr	 layer = outdata->activeLayer();
            SdfChangeBlock       changeblock;

            for (int i = 0; i < paths_to_add.size(); i++)
            {
                auto &path = paths_to_add[i];
                SdfLayerOffset offset;

                if (offsets_to_add.size() > i)
                    offset = offsets_to_add[i];

                if ((int)layer->GetSubLayerPaths().Find(path) < 0)
                {
                    int pos = myAddLayerPosition;

                    if (pos < 0 || pos > layer->GetNumSubLayerPaths())
                        pos = layer->GetNumSubLayerPaths();
                    layer->InsertSubLayerPath(path, pos);
                    layer->SetSubLayerOffset(offset, pos);
                }
                else
                {
                    HUSD_ErrorScope::addError(HUSD_ERR_DUPLICATE_SUBLAYER,
                        path.c_str());
                    return false;
                }
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
        HUSDconvertToFileFormatArguments(refargs, args);

	if (gdh.isValid())
	    outdata->addLockedGeo(XUSD_LockedGeoRegistry::
                createLockedGeo(filepath, args, gdh));

	if (filepath.isstring())
	{
            std::string fileid = SdfLayer::CreateIdentifier(
                    filepath.toStdString(), args);
            SdfLayerRefPtr layer;
            if (gdh.isValid())
            {
                // Also keep the locked geos for any unpacked volumes (see
                // addLayerForEdit()).
                layer = SdfLayer::FindOrOpen(fileid);
                if (layer)
                    HUSDaddVolumeLockedGeos(*outdata, layer);
            }

            if (myEditRootLayer)
	    {
		if (!outdata->addLayer(fileid,
		    HUSDgetSdfLayerOffset(offset),
		    myAddLayerPosition,
		    XUSD_ADD_LAYERS_ALL_LOCKED,
                    myCopyRootPrimMetadataToStage))
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
        HUSDconvertToFileFormatArguments(refargs, args);

	// Even though we will be making a copy of this layer to an
	// new USD lop layer, we must keep the lockedgeo active in case
	// there are volume primitives that need to be kept in memory.
	if (gdh.isValid())
	    outdata->addLockedGeo(XUSD_LockedGeoRegistry::
                createLockedGeo(filepath, args, gdh));

	if (filepath.isstring())
	{
            std::string layer_path = SdfLayer::CreateIdentifier(
                    filepath.toStdString(), args);

            SdfLayerRefPtr layer;
            if (gdh.isValid())
            {
                // Keep the locked geos active for any volume primitives from
                // unpacked details that need to be kept in memory.
                //
                // Note that the lifetime of the layer is very important here!
                // outdata->addLayer() loads the layer and then discards it
                // after copying into an editable layer.
                // We need to grab the locked geos before the layer
                // (GEO_FileData) is destroyed and clears out its locked geo
                // references.
                // So, we load the layer up front and keep it alive for the
                // rest of the scope so that outdata->addLayer() just gets the
                // same cached layer instead of loading it a second time.
                layer = SdfLayer::FindOrOpen(layer_path);
                if (layer)
                    HUSDaddVolumeLockedGeos(*outdata, layer);
            }

            // Pass 0 for the layer position, since we can
	    // only edit the strongest layer in the stage.
            if (!outdata->addLayer(
                        layer_path, SdfLayerOffset(), 0,
                        XUSD_ADD_LAYERS_LAST_EDITABLE,
                        myCopyRootPrimMetadataToStage))
            {
                return false;
            }
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
            if (myCopyRootPrimMetadataToStage)
                outdata->setStageRootLayerData(tmplayer);
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

