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

#include "HUSD_Save.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_Preferences.h"
#include "XUSD_Data.h"
#include "XUSD_TicketRegistry.h"
#include "XUSD_Utils.h"
#include <OP/OP_Node.h>
#include <GU/GU_Detail.h>
#include <UT/UT_Map.h>
#include <UT/UT_Assert.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_FileUtil.h>
#include <UT/UT_ErrorManager.h>
#include <pxr/usd/usdUtils/stitch.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/ar/resolver.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

void
beginSaveOutputProcessors(const HUSD_OutputProcessorArray &output_processors,
        OP_Node *config_node,
        fpreal t)
{
    for (auto &&processor : output_processors)
    {
        if (processor)
        {
            processor->beginSave(config_node, t);
        }
    }
}

void
endSaveOutputProcessors(const HUSD_OutputProcessorArray &output_processors)
{
    for (auto &&processor : output_processors)
    {
        if (processor)
        {
            processor->endSave();
        }
    }
}

UT_StringHolder
runOutputProcessors(const HUSD_OutputProcessorArray &output_processors,
        const UT_StringRef &asset_path,
        const UT_StringRef &asset_path_for_save,
        const UT_StringRef &referencing_layer_path,
        bool asset_is_layer,
        bool for_save)
{
    UT_StringHolder  processedpath(asset_path);
    UT_String        error;

    for (auto &&processor : output_processors)
    {
        if (processor)
        {
            UT_String    tmpprocessed;

            if (processor->processAsset(processedpath,
                    asset_path_for_save,
                    referencing_layer_path,
                    asset_is_layer, for_save,
                    tmpprocessed, error) &&
                tmpprocessed.isstring())
                processedpath = tmpprocessed;
        }
    }

    return processedpath;
}

UT_StringHolder
updateAssetPath(const UT_StringRef &asset_path,
	const UT_StringRef &layer_save_path,
        const HUSD_OutputProcessorArray &output_processors)
{
    return runOutputProcessors(output_processors, asset_path,
        UT_StringRef(), layer_save_path, false, false);
}

VtArray<SdfAssetPath>
updateAssetPaths(
	const VtValue &file_path_value,
	const UT_StringRef &layer_save_path,
        const HUSD_OutputProcessorArray &output_processors,
	UT_StringMap<std::string> &saved_geo_map)
{
    // Update an array of paths from being relative to the cwd to being
    // relative to the layer save location. Returns the modified array or
    // an empty array if nothing needed to change.
    if (file_path_value.IsEmpty())
	return VtArray<SdfAssetPath>();

    VtArray<SdfAssetPath> assetpaths =
	file_path_value.Get<VtArray<SdfAssetPath> >();
    bool		  any_changed = false;

    for (int i = 0, n = assetpaths.size(); i < n; i++)
    {
	UT_StringHolder	 oldpath = assetpaths[i].GetAssetPath();
	UT_StringHolder	 newpath = updateAssetPath(oldpath, layer_save_path,
                            output_processors);

	if (newpath != oldpath)
	{
	    assetpaths[i] = SdfAssetPath(newpath.toStdString());
	    any_changed = true;
	}
    }

    return any_changed ? assetpaths : VtArray<SdfAssetPath>();
}

SdfAssetPath
updateAssetPathAndSaveVolumeGeo(
        const SdfPrimSpecHandle &primspec,
        const UsdTimeCode &timecode,
	bool is_volume, bool is_vdb,
	const VtValue &file_path_value,
	const UT_StringRef &layer_save_path,
        const HUSD_OutputProcessorArray &output_processors,
	UT_StringMap<std::string> &saved_geo_map)
{
    UT_StringHolder	 newrefaspath;

    if (file_path_value.IsEmpty())
	return SdfAssetPath();

    SdfAssetPath         assetpath = file_path_value.Get<SdfAssetPath>();
    std::string	         oldpath = assetpath.GetAssetPath();

    if (is_volume && HUSDisSopLayer(oldpath))
    {
	// If the asset being referenced is a volume from inside a SOP, we need
	// to write out this volume to its own file, and update the asset path
	// to refer to the new volume file location.  VDB volumes are saved to
	// a .vdb file, and so will have a different destination file path than
	// Houdini volumes (which are saved to .bgeo.sc files).
	std::string	 geo_map_key = oldpath;

	if (is_vdb)
	    geo_map_key += ".vdb";

	auto it = saved_geo_map.find(geo_map_key);

	if (it != saved_geo_map.end())
	{
	    newrefaspath = it->second;
	}
	else
	{
	    SdfFileFormat::FileFormatArguments	 args;
	    std::string				 oldfilepath;
	    GU_DetailHandle			 gdh;

	    SdfLayer::SplitIdentifier(oldpath, &oldfilepath, &args);
	    gdh = XUSD_TicketRegistry::getGeometry(oldfilepath, args);
	    if (gdh)
	    {
		GU_DetailHandleAutoReadLock	 lock(gdh);
		const GU_Detail			*gdp = lock.getGdp();
                SdfAttributeSpecHandle           savepathspec;
                std::string                      volumesavepath;
                UT_String	                 origpath;
                UT_String	                 newpath;
                UT_String                        newdir;
                UT_String                        newfile;

                // Read the volume save path off the primspec's save path
                // attribute, if it exists.
                savepathspec = primspec->GetAttributeAtPath(
                    SdfPath::ReflexiveRelativePath().AppendProperty(
                        HUSDgetSavePathToken()));
                if (savepathspec)
                {
                    std::string savepath;

                    if (timecode.IsDefault())
                    {
                        savepath = savepathspec->
                            GetDefaultValue().Get<std::string>();
                    }
                    else
                    {
                        auto samples = savepathspec->GetTimeSampleMap();
                        auto sampleit = samples.find(timecode.GetValue());

                        if (sampleit != samples.end())
                            savepath = sampleit->second.Get<std::string>();
                    }
                    if (!savepath.empty())
                        volumesavepath = savepath;
                }

                if (volumesavepath.empty())
                {
                    char	                         numstr[64];

                    // Create a volume file path based on the path where the
                    // layer will be saved.
                    UT_String::itoa(numstr, saved_geo_map.size());
                    origpath.harden(layer_save_path);
                    origpath += ".volumes/";
                    origpath += numstr;
                    if (is_vdb)
                        origpath += ".vdb";
                    else
                        origpath += ".bgeo.sc";
                }
                else
                    origpath = volumesavepath;

                // Run the new path through the asset processors.
                newpath = runOutputProcessors(output_processors,
                    origpath, UT_StringRef(), layer_save_path, false, true);

                // Create the directory for holding the processed file path.
                newpath.splitPath(newdir, newfile);
		if (newdir.isstring() && UT_FileUtil::makeDirs(newdir))
		{
		    gdp->save(newpath.c_str(), nullptr);
                    newrefaspath = runOutputProcessors(output_processors,
                        origpath, newpath, layer_save_path, false, false);
		    saved_geo_map[geo_map_key] = newrefaspath;
		}
	    }
	}
    }
    else
    {
	// Any non-volume asset has a chance to have its relative path updated
	// to a new path relative to where the layer is being saved.
	newrefaspath = updateAssetPath(oldpath, layer_save_path,
            output_processors);
    }

    return (newrefaspath == oldpath)
        ? SdfAssetPath()
        : SdfAssetPath(newrefaspath.toStdString());
}

void
updateAssetPathsAndSaveVolumes(const SdfLayerRefPtr &layer,
	const UT_StringRef &layer_save_path,
        const HUSD_OutputProcessorArray &output_processors,
	UT_StringMap<std::string> &saved_geo_map)
{
    static const TfToken	 theVDBPrimType("OpenVDBAsset");
    static const TfToken	 theHoudiniPrimType("HoudiniFieldAsset");

    // Recursive run through all attributes looking for asset paths. Update any
    // relative asset file paths to be relative to the layer save location
    // instead of being relative to the cwd.
    layer->Traverse(SdfPath::AbsoluteRootPath(),
	[&layer, &layer_save_path, &output_processors,
         &saved_geo_map](const SdfPath &path)
	{
	    SdfAttributeSpecHandle attrspec = layer->GetAttributeAtPath(path);
	    SdfPrimSpecHandle primspec = layer->GetPrimAtPath(path);

	    if (attrspec &&
		attrspec->GetTypeName().GetScalarType() ==
		    SdfValueTypeNames->Asset)
	    {
		SdfPrimSpecHandle	primspec =
		    layer->GetPrimAtPath(path.GetPrimPath());
		bool			asset_is_volume = false;

		if (path.GetNameToken() == UsdVolTokens->filePath &&
		    primspec &&
		    (primspec->GetTypeName() == theVDBPrimType ||
		     primspec->GetTypeName() == theHoudiniPrimType))
		    asset_is_volume = true;

		auto samples = attrspec->GetTimeSampleMap();
		bool samples_changed = false;

		if (attrspec->GetTypeName().IsArray())
		{
		    // Handles arrays of asset paths. These will never be
		    // volumes, which are always a single asset path.
		    //
		    // Save out and update any time samples.
		    for (auto it = samples.begin(); it != samples.end(); ++it)
		    {
			VtArray<SdfAssetPath> newpaths(updateAssetPaths(
			    it->second, layer_save_path,
                            output_processors, saved_geo_map));

			if (!newpaths.empty())
			{
			    it->second = VtValue(newpaths);
			    samples_changed = true;
			}
		    }
		    if (samples_changed)
			attrspec->SetField(SdfFieldKeys->TimeSamples, samples);

		    // Save out and update the default value.
		    VtArray<SdfAssetPath> newpaths(updateAssetPaths(
			attrspec->GetDefaultValue(), layer_save_path,
                        output_processors, saved_geo_map));
		    if (!newpaths.empty())
			attrspec->SetDefaultValue(VtValue(newpaths));
		}
		else
		{
		    // Handles single asset paths. These may be volumes or any
		    // other kind of asset (texture maps, etc).
		    //
		    // Save out and update any time samples.
		    for (auto it = samples.begin(); it != samples.end(); ++it)
		    {
			SdfAssetPath newpath(updateAssetPathAndSaveVolumeGeo(
			    primspec, UsdTimeCode(it->first), asset_is_volume,
			    primspec->GetTypeName() == theVDBPrimType,
			    it->second, layer_save_path,
                            output_processors, saved_geo_map));

			if (!newpath.GetAssetPath().empty())
			{
			    it->second = VtValue(newpath);
			    samples_changed = true;
			}
		    }
		    if (samples_changed)
			attrspec->SetField(SdfFieldKeys->TimeSamples, samples);

		    // Save out and update the default value.
		    SdfAssetPath newpath(updateAssetPathAndSaveVolumeGeo(
			primspec, UsdTimeCode::Default(), asset_is_volume,
			primspec->GetTypeName() == theVDBPrimType,
			attrspec->GetDefaultValue(), layer_save_path,
                        output_processors, saved_geo_map));
		    if (!newpath.GetAssetPath().empty())
			attrspec->SetDefaultValue(VtValue(newpath));
		}
	    }
            else if (primspec)
            {
                // For primspecs, we want to check for clip metadata, and
                // update any asset paths we find in there. We also need to
                // look for explicit reference and payload removals, which are
                // not reported by GetExternalReferences().
                VtValue value = primspec->GetInfo(UsdTokens->clips);
                auto reflist = primspec->GetReferenceList();
                auto payloadlist = primspec->GetPayloadList();

                if (value.IsHolding<VtDictionary>())
                {
                    VtDictionary clipsets = value.UncheckedGet<VtDictionary>();
                    bool changed = false;

                    for (auto it = clipsets.begin(); it != clipsets.end(); ++it)
                    {
                        if (!it->second.IsHolding<VtDictionary>())
                            continue;

                        VtDictionary clipset =
                            it->second.UncheckedGet<VtDictionary>();

                        for (auto datait = clipset.begin();
                             datait != clipset.end();
                             ++datait)
                        {
                            const VtValue &data = datait->second;

                            if (data.IsHolding<SdfAssetPath>())
                            {
                                SdfAssetPath oldpath(
                                    data.UncheckedGet<SdfAssetPath>());
                                SdfAssetPath newpath(
                                    updateAssetPath(
                                        oldpath.GetAssetPath(),
                                        layer_save_path,
                                        output_processors).toStdString());
                                clipsets.SetValueAtPath(
                                    std::vector<std::string>(
                                        { it->first, datait->first }),
                                    VtValue(newpath));
                                changed = true;
                            }
                            else if (data.IsHolding<VtArray<SdfAssetPath> >())
                            {
                                VtArray<SdfAssetPath> newpaths(
                                    updateAssetPaths(
                                        data,
                                        layer_save_path,
                                        output_processors,
                                        saved_geo_map));
                                clipsets.SetValueAtPath(
                                    std::vector<std::string>(
                                        { it->first, datait->first }),
                                    VtValue(newpaths));
                                changed = true;
                            }
                        }
                    }

                    if (changed)
                        primspec->SetInfo(UsdTokens->clips, VtValue(clipsets));
                }
                if (!reflist.GetDeletedItems().empty())
                {
                    auto dellist = reflist.GetDeletedItems();
                    for (auto it = dellist.begin(); it != dellist.end(); ++it)
                    {
                        SdfReference delref = *it;
                        UT_StringHolder oldpath = delref.GetAssetPath();
                        UT_StringHolder newpath = updateAssetPath(
                            oldpath, layer_save_path, output_processors);
                        delref.SetAssetPath(newpath.toStdString());
                        *it = delref;
                    }
                }
                if (!payloadlist.GetDeletedItems().empty())
                {
                    auto dellist = payloadlist.GetDeletedItems();
                    for (auto it = dellist.begin(); it != dellist.end(); ++it)
                    {
                        SdfPayload delpayload = *it;
                        UT_StringHolder oldpath = delpayload.GetAssetPath();
                        UT_StringHolder newpath = updateAssetPath(
                            oldpath, layer_save_path, output_processors);
                        delpayload.SetAssetPath(newpath.toStdString());
                        *it = delpayload;
                    }
                }
            }
	}
    );
}

void
clearHoudiniCustomData(const SdfLayerRefPtr &layer)
{
    auto	 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
	layer->RemoveRootPrim(infoprim);

    // Erase the data id from any primitive properties.
    layer->Traverse(SdfPath::AbsoluteRootPath(),
	[&layer](const SdfPath &path)
	{
	    if (path.IsPrimPropertyPath())
	    {
		SdfPropertySpecHandle propspec = layer->GetPropertyAtPath(path);

		if (propspec)
		{
		    auto prop_data = propspec->GetCustomData();

		    if (prop_data.find(HUSDgetDataIdToken()) !=
			    prop_data.end())
		    {
			prop_data.erase(HUSDgetDataIdToken());
		    }
		    if (prop_data.find(HUSDgetMaterialIdToken()) !=
			    prop_data.end())
		    {
			prop_data.erase(HUSDgetMaterialIdToken());
		    }
		    if (prop_data.find(HUSDgetMaterialBindingIdToken()) !=
			    prop_data.end())
		    {
			prop_data.erase(HUSDgetMaterialBindingIdToken());
		    }
		}
	    }
	    else if (path.IsPrimPath())
	    {
		SdfPrimSpecHandle primspec = layer->GetPrimAtPath(path);

		if (primspec)
		{
		    auto prim_data = primspec->GetCustomData();

		    if (prim_data.find(HUSDgetPrimEditorNodeIdToken()) !=
			    prim_data.end())
		    {
			prim_data.erase(HUSDgetPrimEditorNodeIdToken());
		    }
		    if (prim_data.find(HUSDgetSourceNodeToken()) !=
			    prim_data.end())
		    {
			prim_data.erase(HUSDgetSourceNodeToken());
		    }
		    if (prim_data.find(HUSDgetMaterialIdToken()) !=
			    prim_data.end())
		    {
			prim_data.erase(HUSDgetMaterialIdToken());
		    }

                    auto save_path_prop = primspec->GetPropertyAtPath(
                        SdfPath::ReflexiveRelativePath().
                            AppendProperty(HUSDgetSavePathToken()));
                    if (save_path_prop)
                        primspec->RemoveProperty(save_path_prop);
		}
	    }
	});
}

void
ensureMetricsSet(const SdfLayerRefPtr &layer, const UsdStageWeakPtr &stage)
{
    if (!layer->GetPseudoRoot()->HasInfo(UsdGeomTokens->metersPerUnit))
    {
        double   metersperunit(HUSD_Preferences::defaultMetersPerUnit());

        stage->GetPseudoRoot().GetMetadata(
            UsdGeomTokens->metersPerUnit, &metersperunit);
        layer->GetPseudoRoot()->SetInfo(
            UsdGeomTokens->metersPerUnit, VtValue(metersperunit));
    }
    if (!layer->GetPseudoRoot()->HasInfo(UsdGeomTokens->upAxis))
    {
        TfToken  upaxis(HUSD_Preferences::defaultUpAxis().toStdString());

        stage->GetPseudoRoot().GetMetadata(
            UsdGeomTokens->upAxis, &upaxis);
        layer->GetPseudoRoot()->SetInfo(
            UsdGeomTokens->upAxis, VtValue(upaxis));
    }
}

void
configureDefaultPrim(const SdfLayerRefPtr &layer,
        const husd_SaveDefaultPrimData &data)
{
    if (data.myDefaultPrim.isstring())
    {
        UT_String	 fixed_defaultprim(data.myDefaultPrim.c_str());

        if (HUSDmakeValidDefaultPrim(fixed_defaultprim, true))
            layer->SetDefaultPrim(TfToken(fixed_defaultprim.toStdString()));
    }

    if (data.myRequireDefaultPrim)
    {
        if (layer->GetDefaultPrim().IsEmpty())
            HUSD_ErrorScope::addError(
                HUSD_ERR_SAVED_FILE_WITH_EMPTY_DEFAULTPRIM);
    }
}

void
configureTimeData(const SdfLayerRefPtr &layer,
        const husd_SaveTimeData &timedata)
{
    if (timedata.myStartFrame > -SYS_FP64_MAX)
        layer->SetStartTimeCode(timedata.myStartFrame);
    if (timedata.myEndFrame < SYS_FP64_MAX)
        layer->SetEndTimeCode(timedata.myEndFrame);
    if (timedata.myTimeCodesPerSecond < SYS_FP64_MAX)
        layer->SetTimeCodesPerSecond(timedata.myTimeCodesPerSecond);
    if (timedata.myFramesPerSecond < SYS_FP64_MAX)
        layer->SetFramesPerSecond(timedata.myFramesPerSecond);
}

bool
saveStage(const UsdStageWeakPtr &stage,
	const UT_StringRef &filepath,
	const UT_PathPattern *save_files_pattern,
	HUSD_SaveStyle save_style,
        const husd_SaveProcessorData &processordata,
        const husd_SaveDefaultPrimData &defaultprimdata,
        const husd_SaveTimeData &timedata,
        const husd_SaveConfigFlags &flags,
	UT_StringArray &saved_paths)
{
    bool		 success = false;

    beginSaveOutputProcessors(processordata.myProcessors,
        processordata.myConfigNode, processordata.myConfigTime);

    if (save_style == HUSD_SAVE_FLATTENED_STAGE)
    {
	UT_StringMap<std::string>	 saved_geo_map;
        UT_StringHolder			 fullfilepath;
	auto				 layer = stage->Flatten();

        configureTimeData(layer, timedata);
	configureDefaultPrim(layer, defaultprimdata);

        // Let asset processors change the path where the file will be saved.
        fullfilepath = runOutputProcessors(processordata.myProcessors,
            filepath.toStdString(), UT_StringRef(), UT_StringRef(), true, true);
        // Make sure the save path is an absolute path.
        if (!UTisAbsolutePath(fullfilepath))
            UTmakeAbsoluteFilePath(fullfilepath);

	updateAssetPathsAndSaveVolumes(
	    layer, fullfilepath,
            processordata.myProcessors, saved_geo_map);
	if (flags.myClearHoudiniCustomData)
	    clearHoudiniCustomData(layer);
        if (flags.myEnsureMetricsSet)
            ensureMetricsSet(layer, stage);
	success = layer->Export(fullfilepath.toStdString());
	saved_paths.append(fullfilepath);
    }
    else
    {
	SdfLayerRefPtr		 rootlayer;
	SdfLayerRefPtrVector	 temp_layers;
	SdfLayerRefPtr		 first_sublayer;
	std::string		 first_sublayer_identifier;
	int			 flatten_flags = 0;

	if (save_style == HUSD_SAVE_FLATTENED_IMPLICIT_LAYERS)
	{
	    if (flags.myFlattenFileLayers)
		flatten_flags |= HUSD_FLATTEN_FILE_LAYERS;
	    if (flags.myFlattenSopLayers)
		flatten_flags |= HUSD_FLATTEN_SOP_LAYERS;
	    rootlayer = HUSDflattenLayerPartitions(stage,
		flatten_flags, temp_layers);
	}
	else if (save_style == HUSD_SAVE_FLATTENED_ALL_LAYERS)
	{
	    flatten_flags |= HUSD_FLATTEN_FILE_LAYERS |
		HUSD_FLATTEN_SOP_LAYERS |
		HUSD_FLATTEN_EXPLICIT_LAYERS |
		HUSD_FLATTEN_FULL_STACK;
	    rootlayer = HUSDflattenLayerPartitions(stage,
		flatten_flags, temp_layers);
	}
	else // save_style == HUSD_SAVE_SEPARATE_LAYERS
	{
	    // Make a copy of the root layer, so we can edit the sublayer
	    // paths, removing any placeholder layers.
	    rootlayer = HUSDcreateAnonymousLayer();
	    rootlayer->TransferContent(stage->GetRootLayer());

	    auto	 paths = rootlayer->GetSubLayerPaths();
	    int		 sublayeridx = 0;
	    UT_IntArray	 sublayers_to_remove;
	    for (auto &&identifier : rootlayer->GetSubLayerPaths())
	    {
                if (HUSDisLayerPlaceholder(identifier))
                    sublayers_to_remove.append(sublayeridx);
		sublayeridx++;
	    }
	    for (int i = sublayers_to_remove.size(); i --> 0; )
		rootlayer->RemoveSubLayerPath(sublayers_to_remove(i));

	    // Find the strongest sublayer of the root layer. We will want to
	    // copy the layer metadata from this layer to the root layer. We
	    // only do this when keeping separate layers, as the flatten
	    // partitions operation already returns the strongest sublayer
	    // as the root layer, so we should use the layer metadata that is
	    // already there.
	    if (!rootlayer->GetSubLayerPaths().empty())
		first_sublayer_identifier =
		    *rootlayer->GetSubLayerPaths().begin();
	}

	XUSD_IdentifierToLayerMap	 idtolayermap;
	XUSD_IdentifierToSavePathMap	 idtosavepathmap;
	std::string			 rootidentifier;

	rootidentifier = rootlayer->GetIdentifier();

        configureTimeData(rootlayer, timedata);
	configureDefaultPrim(rootlayer, defaultprimdata);

	// Create mapping of layer identifiers to layer ref ptrs for all layers
	// on the stage, either as sublayers or references.
	idtolayermap[rootidentifier] = rootlayer;
	HUSDaddExternalReferencesToLayerMap(rootlayer, idtolayermap, true);

	// Create mapping of layer identifiers to the paths on disk where the
	// layer is going to be saved for all layers in our map.
	for (auto &&it : idtolayermap)
	{
	    auto                 identifier = it.first;
	    auto                 layer = it.second;
	    bool                 using_node_path = false;
            UT_StringHolder      orig_path;
            UT_StringHolder      final_path;

            // Get the path specified by the user in node parameters while
            // cooking the network.
	    if (identifier != rootidentifier)
            {
		orig_path = HUSDgetLayerSaveLocation(layer, &using_node_path);
                // If we are using a LOP node path as the save file path,
                // turn it into an absolute path by prefixing the output
                // file path.
                if (using_node_path)
                {
                    UT_String orig_path_str(orig_path);
                    UT_String dirpath, filename;

                    UT_String(filepath.c_str()).splitPath(dirpath, filename);
                    UTmakeAbsoluteFilePath(orig_path_str, dirpath);
                    orig_path = orig_path_str.toStdString();
                }
            }
	    else
		orig_path = filepath.c_str();

            // Send this path to asset processors to get the final save path.
            final_path = runOutputProcessors(processordata.myProcessors,
                orig_path, UT_StringRef(), UT_StringRef(), true, true);
            // Make sure the save path is an absolute path.
            if (!UTisAbsolutePath(final_path))
                UTmakeAbsoluteFilePath(final_path);

	    // When we hit the strongest sublayer, record the SdfLayerRefPtr
	    // for it for use later. This is only tracked when keeping separate
	    // layers so we can copy metadata from the strongest sublayer onto
	    // the root layer.
	    if (identifier == first_sublayer_identifier)
		first_sublayer = layer;

	    idtosavepathmap[identifier] = XUSD_SavePathInfo(
		final_path, orig_path, using_node_path);
	}

	UT_StringMap<std::string>	 saved_geo_map;

	// For all layers we want to save, make a copy of the layer. Then
	// update all paths from anonymous or internal paths to the locations
	// where those layers will be saved to disk. Also update full paths
	// to relative paths for files on disk. Finally save the updated
	// layer to its desired location on disk.
	for (auto &&it : idtolayermap)
	{
	    auto	 identifier = it.first;
	    auto	 outpathinfo = idtosavepathmap[identifier];
	    auto	 outfinalpath = outpathinfo.myFinalPath;
	    std::string	 save_control;

	    if (outfinalpath.length() > 0)
	    {
		auto	 layer = it.second;

		// Check if this file we are about to save is part of the
		// pattern of files that we have been asked to save. No
		// pattern means we accept all files.
		if (save_files_pattern &&
		    !save_files_pattern->matches(outfinalpath))
		    continue;

		// If we have been asked to not save "files from disk", and
		// this is a file from disk, don't save it. Files from disk
		// are anonymous copies of layers loaded from disk but
		// modified to point to a new version of a sublayered
		// or referenced file.
		if (!flags.mySaveFilesFromDisk &&
		    HUSDgetSaveControl(layer, save_control) &&
		    HUSD_Constants::getSaveControlIsFileFromDisk() ==
			save_control)
		    continue;

		// If we are saving this layer to a location defined by a node
		// path (instead of an explicitly set save path), we want to
		// add either a warning or an error.
		if (outpathinfo.myNodeBasedPath)
		{
		    if (flags.myErrorSavingImplicitPaths)
			HUSD_ErrorScope::addError(
			    HUSD_ERR_SAVED_FILE_WITH_NODE_PATH,
			    outfinalpath.c_str());
		    else if (!flags.myIgnoreSavingImplicitPaths)
			HUSD_ErrorScope::addWarning(
			    HUSD_ERR_SAVED_FILE_WITH_NODE_PATH,
			    outfinalpath.c_str());
		}

		// Copy the layer.
		auto	 layercopy = HUSDcreateAnonymousLayer();

		layercopy->TransferContent(layer);
		// If we are copying the root layer, and we are keeping
		// layers as separate as possible, copy the metadata from
		// its strongest sublayer onto it. This is to ensure the
		// default prim and time information from the last layer
		// configuration set by the user is propagated onto the
		// primary output file (the root layer).
		if (identifier == rootidentifier && first_sublayer)
		    HUSDcopyLayerMetadata(first_sublayer, layercopy);

                std::map<std::string, std::string> replacemap;
		auto refs = layer->GetExternalReferences();

		for (auto &&ref : refs)
		{
                    UT_StringHolder      newpath(ref);
		    auto                 updateit = idtosavepathmap.find(ref);

		    // If the reference is an empty string, ignore it.
		    if (ref.empty())
			continue;

		    if (updateit == idtosavepathmap.end())
                    {
                        // If the referenced file is not one that we are
                        // saving, run it through our asset processors.
                        newpath = runOutputProcessors(
                            processordata.myProcessors,
                            ref, std::string(), outfinalpath, true, false);
                    }
                    else
                    {
                        // If the referenced file is a layer we are saving,
                        // we want to update this reference to point to the
                        // path where this layer will be saved. This path will
                        // have already been fully processed.
                        newpath = updateit->second.myOriginalPath;
                        newpath = runOutputProcessors(
                            processordata.myProcessors,
                            updateit->second.myOriginalPath,
                            updateit->second.myFinalPath,
                            outfinalpath, true, false);
                    }

		    if (ref != newpath.c_str())
                        replacemap[ref] = newpath;
		}
                HUSDupdateExternalReferences(layercopy, replacemap);

		updateAssetPathsAndSaveVolumes(
		    layercopy, outfinalpath,
                    processordata.myProcessors, saved_geo_map);
		if (flags.myClearHoudiniCustomData)
		    clearHoudiniCustomData(layercopy);
		if (flags.myEnsureMetricsSet)
		    ensureMetricsSet(layercopy, stage);
		layercopy->Export(outfinalpath.toStdString());
		saved_paths.append(outfinalpath);
	    }
	}

	success = true;
    }
    endSaveOutputProcessors(processordata.myProcessors);

    // Call Reload for any layers we just saved.
    std::set<SdfLayerHandle>	 saved_layers;
    for (auto &&saved_path : saved_paths)
    {
	auto existing_layer = SdfLayer::Find(saved_path.toStdString());
	if (existing_layer)
	    saved_layers.insert(existing_layer);
    }

    {
	// Create an error scope to eat any errors triggered by the reload.
	UT_ErrorManager		 errmgr;
	HUSD_ErrorScope		 scope(&errmgr);

        // Clear the whole cache of automatic ref prim paths, because the
        // layers we are saving may be used by any stage, and so may affect
        // the default/automatic default prim of any stage.
        HUSDclearBestRefPathCache();
	SdfLayer::ReloadLayers(saved_layers, true);
    }

    return success;
}

} // end namespace

class HUSD_Save::husd_SavePrivate {
public:
    void                         clear()
                                 {
                                    myStage.Reset();
                                    myHoldLayers.clear();
                                    myTicketArray.clear();
                                    myReplacementLayerArray.clear();
                                    myLockedStages.clear();
                                 }

    UsdStageRefPtr		 myStage;
    SdfLayerRefPtrVector	 myHoldLayers;
    XUSD_TicketArray		 myTicketArray;
    XUSD_LayerArray		 myReplacementLayerArray;
    HUSD_LockedStageArray	 myLockedStages;
};

HUSD_Save::HUSD_Save()
    : myPrivate(new husd_SavePrivate()),
      mySaveStyle(HUSD_SAVE_FLATTENED_IMPLICIT_LAYERS)
{
}

HUSD_Save::~HUSD_Save()
{
}

bool
HUSD_Save::addCombinedTimeSample(const HUSD_AutoReadLock &lock)
{
    auto		 indata = lock.data();
    bool		 success = false;

    if (!myPrivate->myStage)
	myPrivate->myStage = HUSDcreateStageInMemory(
	    mySaveStyle == HUSD_SAVE_FLATTENED_STAGE
		? UsdStage::LoadAll
		: UsdStage::LoadNone,
	    OP_INVALID_ITEM_ID, indata->stage());

    if (indata && indata->isStageValid())
    {
	success = HUSDaddStageTimeSample(indata->stage(), myPrivate->myStage,
	    myPrivate->myHoldLayers);
	myPrivate->myTicketArray.concat(indata->tickets());
	myPrivate->myReplacementLayerArray.concat(indata->replacements());
	myPrivate->myLockedStages.concat(indata->lockedStages());
    }

    return success;
}

bool
HUSD_Save::saveCombined(const UT_StringRef &filepath,
	UT_StringArray &saved_paths)
{
    bool		 success = false;

    if (myPrivate->myStage)
	success = saveStage(myPrivate->myStage,
            filepath,
	    mySaveFilesPattern.get(),
            mySaveStyle,
            myProcessorData,
            myDefaultPrimData,
            myTimeData,
            myFlags,
	    saved_paths);

    return success;
}

bool
HUSD_Save::save(const HUSD_AutoReadLock &lock,
	const UT_StringRef &filepath,
	UT_StringArray &saved_paths)
{
    bool                 success = false;

    // Even when saving a single time sample, we need to run the combine code,
    // which stitches layers together, and makes sure that all layers paths
    // that will be written to are unique (even if multiple layers indicate
    // that they want to be written to the same location on disk).
    success = addCombinedTimeSample(lock);
    if (success)
        success = saveCombined(filepath, saved_paths);
    // Wipe out any record of this save operation, otherwise we'll combine it
    // with the next one, if there is one.
    myPrivate->clear();

    return success;
}

