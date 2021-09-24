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
#include "XUSD_ExistenceTracker.h"
#include "XUSD_LockedGeoRegistry.h"
#include "XUSD_Utils.h"
#include <gusd/stageCache.h>
#include <OP/OP_Node.h>
#include <GU/GU_Detail.h>
#include <UT/UT_Map.h>
#include <UT/UT_Assert.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_FileUtil.h>
#include <UT/UT_ErrorManager.h>
#include <pxr/usd/usdUtils/dependencies.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <pxr/usd/usdUtils/stitch.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/ar/resolver.h>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

void
beginSaveOutputProcessors(
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        OP_Node *config_node,
        fpreal t)
{
    for (auto &&processor : output_processors)
    {
        if (processor.myProcessor)
        {
            processor.myProcessor->beginSave(config_node, processor.myOverrides, t);
        }
    }
}

void
endSaveOutputProcessors(
        const HUSD_OutputProcessorAndOverridesArray &output_processors)
{
    for (auto &&processor : output_processors)
    {
        if (processor.myProcessor)
        {
            processor.myProcessor->endSave();
        }
    }
}

UT_StringHolder
runOutputProcessors(
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
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
        if (processor.myProcessor)
        {
            UT_String    tmpprocessed;

            if (processor.myProcessor->processAsset(processedpath,
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

class husd_UpdateReferencesWithOutputProcessors
{
public:
    husd_UpdateReferencesWithOutputProcessors(
            const HUSD_OutputProcessorAndOverridesArray &output_processors,
            const UT_StringHolder &layer_save_path,
            const std::map<std::string, std::string> &replace_map)
        : myLayerSavePath(layer_save_path),
          myOutputProcessors(output_processors),
          myReplaceMap(replace_map)
    { }
    ~husd_UpdateReferencesWithOutputProcessors()
    { }

    std::string
    operator()(const std::string &assetPath)
    {
        auto replace_it = myReplaceMap.find(assetPath);
        if (replace_it != myReplaceMap.end())
            return replace_it->second;

        UT_StringHolder processed = runOutputProcessors(
            myOutputProcessors,
            assetPath,
            UT_StringRef(),
            myLayerSavePath,
            false,
            false);

        if (processed.isstring() && processed != assetPath)
            return processed.toStdString();

        return assetPath;
    }

private:
    const UT_StringHolder &myLayerSavePath;
    const HUSD_OutputProcessorAndOverridesArray &myOutputProcessors;
    const std::map<std::string, std::string> &myReplaceMap;
};

class husd_VolumeSavePrim
{
public:
    husd_VolumeSavePrim()
        : myVolumeIndex(-1)
    { }
    ~husd_VolumeSavePrim()
    { }

    UT_StringHolder      myVolumeName;
    int                  myVolumeIndex;

    UT_StringHolder      mySourcePath;
    UT_StringHolder      mySourceVolumeName;
    int                  mySourceVolumeIndex;
};
typedef UT_Array<husd_VolumeSavePrim> husd_VolumeSavePrimArray;
class husd_VolumeSaveFile
{
public:
    husd_VolumeSaveFile()
    { myDetailHandle.allocateAndSet(new GU_Detail()); }
    ~husd_VolumeSaveFile()
    { }

    husd_VolumeSavePrim
    addVolume(const GEO_Primitive *srcprim,
        const UT_StringHolder &sourcepath,
        const UT_StringHolder &volumename,
        int volumeindex)
    {
        // Check if we've already added this volume to this file.
        for (auto &&volumeprim : myVolumePrims)
        {
            if (volumeprim.mySourcePath == sourcepath &&
                volumeprim.mySourceVolumeName == volumename &&
                volumeprim.mySourceVolumeIndex == volumeindex)
                return volumeprim;
        }

        // If this volume is new to this file, add the volume to our list.
        GU_Detail *gdp = myDetailHandle.gdpNC();

        myVolumePrims.append();
        myVolumePrims.last().myVolumeName = volumename;
        // Houdini volume field index is the prim index in the destination
        // detail. Other volumes use the index to differentiate between
        // multiple fields with the same name.
        if (srcprim->getTypeId() == GEO_PRIMVOLUME)
            myVolumePrims.last().myVolumeIndex = gdp->getNumPrimitives();
        else
            myVolumePrims.last().myVolumeIndex = myNameCounts[volumename];
        myVolumePrims.last().mySourcePath = sourcepath;
        myVolumePrims.last().mySourceVolumeName = volumename;
        myVolumePrims.last().mySourceVolumeIndex = volumeindex;
        myNameCounts[volumename]++;
        gdp->merge(*srcprim);

        return myVolumePrims.last();
    }

    GU_DetailHandle              myDetailHandle;
    husd_VolumeSavePrimArray     myVolumePrims;
    UT_StringMap<int>            myNameCounts;
};
typedef UT_StringMap<husd_VolumeSaveFile> husd_VolumeSaveMap;

husd_VolumeSavePrim
saveVolumesWithSavePath(const GU_Detail *gdp,
        bool is_vdb,
        const UT_StringRef &sourcepath,
        const UT_StringRef &volumename,
        int volumeindex,
        const char *newpath,
        husd_VolumeSaveMap &volume_save_map)
{
    const GEO_Primitive *srcprim = nullptr;

    // Find the source volume prim.
    if (gdp)
    {
        GA_Offset field_offset = GA_INVALID_OFFSET;

        // For Houdini volumes, the field index is the primary identifier,
        // and has no need to use the name.
        if (field_offset == GA_INVALID_OFFSET && !is_vdb)
            field_offset = gdp->primitiveOffset(GA_Index(volumeindex));

        if (field_offset == GA_INVALID_OFFSET && volumename.isstring())
        {
            GA_PrimCompat::TypeMask primtype;

            if (!is_vdb)
                primtype = GEO_PrimTypeCompat::GEOPRIMVOLUME;
            else
                primtype = GEO_PrimTypeCompat::GEOPRIMVDB;

            // For Houdini volumes, always use the first name match (the
            // field index, if it exists, is a prim number, not a match
            // number). For other volume types the field index is the
            // match number.
            int matchnumber = 0;
            if (is_vdb)
                matchnumber = (volumeindex >= 0 ? volumeindex : 0);

            const GEO_Primitive *prim = gdp->findPrimitiveByName(
                volumename, primtype, "name", matchnumber);
            if (prim)
                field_offset = prim->getMapOffset();
        }

        if (field_offset != GA_INVALID_OFFSET)
            srcprim = gdp->getGEOPrimitive(field_offset);
    }

    // Copy the source volume prim into the destination gdp.
    if (srcprim)
    {
        auto it = volume_save_map.find(newpath);
        husd_VolumeSaveFile &volumefile =
            (it == volume_save_map.end())
                ? volume_save_map[newpath]
                : it->second;
        return volumefile.addVolume(
            srcprim, sourcepath, volumename, volumeindex);
    }

    return husd_VolumeSavePrim();
}

void
getVolumePrimDetails(const SdfPrimSpecHandle &primspec,
        const UsdTimeCode &timecode,
        std::string &volumesavepath,
        std::string &volumename,
        int &volumeindex)
{
    SdfAttributeSpecHandle   savepathspec;
    SdfAttributeSpecHandle   namespec;
    SdfAttributeSpecHandle   indexspec;

    savepathspec = primspec->GetAttributeAtPath(
        SdfPath::ReflexiveRelativePath().AppendProperty(
            HUSDgetSavePathToken()));
    if (savepathspec)
    {
        std::string savepath;

        if (timecode.IsDefault())
        {
            savepath = savepathspec->GetDefaultValue().Get<std::string>();
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
    namespec = primspec->GetAttributeAtPath(
        SdfPath::ReflexiveRelativePath().AppendProperty(
            UsdVolTokens->fieldName));
    if (namespec)
    {
        TfToken name;

        if (timecode.IsDefault())
        {
            name = namespec->GetDefaultValue().Get<TfToken>();
        }
        else
        {
            auto samples = namespec->GetTimeSampleMap();
            auto sampleit = samples.find(timecode.GetValue());

            if (sampleit != samples.end())
                name = sampleit->second.Get<TfToken>();
        }
        if (!name.IsEmpty())
            volumename = name.GetString();
    }
    indexspec = primspec->GetAttributeAtPath(
        SdfPath::ReflexiveRelativePath().AppendProperty(
            UsdVolTokens->fieldIndex));
    if (indexspec)
    {
        int index = -1;

        if (timecode.IsDefault())
        {
            index = indexspec->GetDefaultValue().Get<int>();
        }
        else
        {
            auto samples = indexspec->GetTimeSampleMap();
            auto sampleit = samples.find(timecode.GetValue());

            if (sampleit != samples.end())
                index = sampleit->second.Get<int>();
        }
        if (index >= 0)
            volumeindex = index;
    }
}

std::pair<SdfAssetPath, int>
saveVolumeGeo(const SdfPrimSpecHandle &primspec,
        const UsdTimeCode &timecode,
	bool is_vdb,
	const VtValue &file_path_value,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
	const UT_StringRef &layer_save_path,
	std::map<std::string, std::string> &saved_geo_map,
        husd_VolumeSaveMap &volume_save_map)
{
    UT_StringHolder	 newrefaspath;
    int                  newindex = 0;

    if (file_path_value.IsEmpty())
	return { SdfAssetPath(), 0 };

    SdfAssetPath         assetpath = file_path_value.Get<SdfAssetPath>();
    std::string	         oldpath = assetpath.GetAssetPath();

    if (HUSDisSopLayer(oldpath))
    {
        // If the asset being referenced is a volume from inside a SOP, we need
        // to write out this volume to its own file, and update the asset path
        // to refer to the new volume file location. VDB volumes are saved to
        // a .vdb file, and so will have a different destination file path than
        // Houdini volumes (which are saved to .bgeo.sc files).
        std::string geo_map_key = oldpath;
        std::string volumesavepath;
        std::string volumename;
        int volumeindex;
        UT_String newpath;

        // Read the volume save path and other source information off the
        // primspec's attributes.
        getVolumePrimDetails(primspec, timecode,
            volumesavepath, volumename, volumeindex);

        if (!volumesavepath.empty())
        {
            geo_map_key += "->";
            geo_map_key += volumesavepath;
        }
        else if (is_vdb)
            geo_map_key += ".vdb";

        // Figure out the full path to the file where we want to write this
        // volume. Run output processors on the file to get the full path and
        // the path for saving to the layer.
        auto it = saved_geo_map.find(geo_map_key);
        if (it == saved_geo_map.end())
        {
            UT_String origpath;
            UT_String newdir;
            UT_String newfile;

            if (volumesavepath.empty())
            {
                char numstr[64];

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
            newpath = runOutputProcessors(output_processors, origpath,
                UT_StringRef(), layer_save_path, false, true);

            // Create the directory for holding the processed file path.
            newpath.splitPath(newdir, newfile);
            if (newdir.isstring() && UT_FileUtil::makeDirs(newdir))
            {
                // Record information for updating the volume file name
                // and saving out the volume data to a file later.
                newrefaspath = runOutputProcessors(output_processors, origpath,
                    newpath, layer_save_path, false, false);
                saved_geo_map[geo_map_key] = newpath;
                saved_geo_map[newpath.toStdString()] = newrefaspath;
            }
        }
        else
        {
            newpath = it->second;
            newrefaspath = saved_geo_map[newpath.toStdString()];
        }

        if (newrefaspath.isstring())
        {
            SdfFileFormat::FileFormatArguments args;
            std::string oldfilepath;
            GU_DetailHandle gdh;

            SdfLayer::SplitIdentifier(oldpath, &oldfilepath, &args);
            gdh = XUSD_LockedGeoRegistry::getGeometry(oldfilepath, args);
            if (gdh)
            {
                GU_DetailHandleAutoReadLock lock(gdh);
                const GU_Detail *gdp = lock.getGdp();

                if (gdp)
                {
                    husd_VolumeSavePrim saveprim = saveVolumesWithSavePath(
                        gdp, is_vdb, oldpath, volumename, volumeindex,
                        newpath.c_str(), volume_save_map);
                    newindex = saveprim.myVolumeIndex;
                }
            }
        }
    }

    return {
        (newrefaspath.isstring())
            ? SdfAssetPath(newrefaspath.toStdString())
            : SdfAssetPath(),
        newindex
    };
}

void
saveVolumes(const SdfLayerRefPtr &layer,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
	const UT_StringRef &layer_save_path,
	std::map<std::string, std::string> &saved_geo_map,
	std::map<std::string, std::string> &replace_map,
        husd_VolumeSaveMap &volume_save_map)
{
    static const TfToken	 theVDBPrimType("OpenVDBAsset");
    static const TfToken	 theHoudiniPrimType("HoudiniFieldAsset");
    static const SdfPath         theFileAttrPath =
                                    SdfPath::ReflexiveRelativePath().
                                    AppendProperty(UsdVolTokens->filePath);
    static const SdfPath         theFieldIndexAttrPath =
                                    SdfPath::ReflexiveRelativePath().
                                    AppendProperty(UsdVolTokens->fieldIndex);

    // Recursive run through all primitives looking for volumes. Save any
    // SOP volumes to disk, and record the mapping of SOP path to the file
    // path requested on the volume prim.
    layer->Traverse(SdfPath::AbsoluteRootPath(),
	[&layer, &layer_save_path, &output_processors,
         &saved_geo_map, &replace_map, &volume_save_map](const SdfPath &path)
	{
            SdfPrimSpecHandle	primspec = layer->GetPrimAtPath(path);

            if (primspec &&
                (primspec->GetTypeName() == theVDBPrimType ||
                 primspec->GetTypeName() == theHoudiniPrimType))
            {
                SdfAttributeSpecHandle fileattr =
                    primspec->GetAttributeAtPath(theFileAttrPath);

                if (fileattr &&
                    fileattr->GetTypeName().GetScalarType() ==
                        SdfValueTypeNames->Asset)
                {
                    SdfAttributeSpecHandle indexattr =
                        primspec->GetAttributeAtPath(theFieldIndexAttrPath);
                    if (!indexattr ||
                        indexattr->GetTypeName().GetScalarType() !=
                            SdfValueTypeNames->Int)
                        indexattr = SdfAttributeSpec::New(primspec,
                            UsdVolTokens->fieldIndex, SdfValueTypeNames->Int);

                    SdfTimeSampleMap samples = fileattr->GetTimeSampleMap();
                    SdfTimeSampleMap indexsamples;
                    bool samples_changed = false;

                    // Save out and update any volumes in time samples.
                    for (auto it = samples.begin(); it != samples.end(); ++it)
                    {
                        auto newinfo(saveVolumeGeo(primspec,
                            UsdTimeCode(it->first),
                            primspec->GetTypeName() == theVDBPrimType,
                            it->second, output_processors,
                            layer_save_path, saved_geo_map,
                            volume_save_map));

                        if (!newinfo.first.GetAssetPath().empty())
                        {
                            // We've already run the output processors on this
                            // path. Add it as an identity to the replace_map
                            // so we don't process them again.
                            replace_map.emplace(newinfo.first.GetAssetPath(),
                                newinfo.first.GetAssetPath());
                            it->second = VtValue(newinfo.first);
                            indexsamples.emplace(it->first, newinfo.second);
                            samples_changed = true;
                        }
                    }
                    if (samples_changed)
                    {
                        fileattr->SetField(
                            SdfFieldKeys->TimeSamples, samples);
                        indexattr->SetField(
                            SdfFieldKeys->TimeSamples, indexsamples);
                    }

                    // Save out and update the volume default value.
                    auto newinfo(saveVolumeGeo(primspec,
                        UsdTimeCode::Default(),
                        primspec->GetTypeName() == theVDBPrimType,
                        fileattr->GetDefaultValue(), output_processors,
                        layer_save_path, saved_geo_map,
                        volume_save_map));
                    if (!newinfo.first.GetAssetPath().empty())
                    {
                        // We've already run the output processors on this
                        // path. Add it as an identity to the replace_map
                        // so we don't process them again.
                        replace_map.emplace(newinfo.first.GetAssetPath(),
                            newinfo.first.GetAssetPath());
                        fileattr->SetDefaultValue(VtValue(newinfo.first));
                        indexattr->SetDefaultValue(VtValue(newinfo.second));
                    }
                }
	    }
	}
    );
}

inline void
eraseHoudiniCustomData(SdfDictionaryProxy &dict, const TfToken &key)
{
    if (dict.find(key) != dict.end())
	dict.erase(key);
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

		    eraseHoudiniCustomData(prop_data,
			HUSDgetDataIdToken());
		    eraseHoudiniCustomData(prop_data,	 
			HUSDgetMaterialIdToken());
		}
	    }
	    else if (path.IsPrimPath())
	    {
		SdfPrimSpecHandle primspec = layer->GetPrimAtPath(path);

		if (primspec)
		{
		    auto prim_data = primspec->GetCustomData();

		    eraseHoudiniCustomData(prim_data,
                        HUSDgetPrimEditorNodesToken());
		    eraseHoudiniCustomData(prim_data,
                        HUSDgetSourceNodeToken());
		    eraseHoudiniCustomData(prim_data,
                        HUSDgetHasAutoPreviewShaderToken());

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
saveLayer(SdfLayerRefPtr layer, const UT_StringRef &fullfilepath)
{
    SdfLayer::FileFormatArguments args;
    std::string splitfilepath;

    SdfLayer::SplitIdentifier(
        fullfilepath.toStdString(), &splitfilepath, &args);

    if (!layer->Export(splitfilepath, std::string(), args))
    {
        HUSD_ErrorScope::addError(
            HUSD_ERR_LAYER_SAVE_FAILED,
            fullfilepath.c_str());
        return false;
    }

    return true;
}

bool
saveStage(const UsdStageWeakPtr &stage,
	const UT_StringRef &filepath,
        bool filepath_is_time_dependent,
	const UT_PathPattern *save_files_pattern,
	HUSD_SaveStyle save_style,
        const husd_SaveProcessorData &processordata,
        const husd_SaveDefaultPrimData &defaultprimdata,
        const husd_SaveTimeData &timedata,
        const husd_SaveConfigFlags &flags,
	UT_StringMap<XUSD_SavePathInfo> &saved_path_info_map,
	std::map<std::string, std::string> &saved_geo_map)
{
    husd_VolumeSaveMap   volume_save_map;
    bool		 success = false;

    beginSaveOutputProcessors(processordata.myProcessors,
                              processordata.myConfigNode,
                              processordata.myConfigTime);

    if (save_style == HUSD_SAVE_FLATTENED_STAGE)
    {
        UT_StringHolder			     fullfilepath;
        std::map<std::string, std::string>   replace_map;
	auto				     layer = stage->Flatten();

        configureTimeData(layer, timedata);
	configureDefaultPrim(layer, defaultprimdata);

        // Let asset processors change the path where the file will be saved.
        fullfilepath = runOutputProcessors(processordata.myProcessors,
            filepath.toStdString(), UT_StringRef(), UT_StringRef(), true, true);
        // Make sure the save path is an absolute path.
        if (!UTisAbsolutePath(fullfilepath))
            UTmakeAbsoluteFilePath(fullfilepath);

        saveVolumes(layer,
            processordata.myProcessors,
            fullfilepath,
            saved_geo_map,
            replace_map,
            volume_save_map);
        HUSDmodifyAssetPaths(layer,
            husd_UpdateReferencesWithOutputProcessors(
                processordata.myProcessors,
                fullfilepath,
                replace_map));

	if (flags.myClearHoudiniCustomData)
	    clearHoudiniCustomData(layer);
        if (flags.myEnsureMetricsSet)
            ensureMetricsSet(layer, stage);
        if (saved_path_info_map.contains(fullfilepath))
        {
            // We've been asked to save to this layer before. Load the
            // existing file, stitch the new data into it, and save it
            // out.
            SdfLayerRefPtr existinglayer;

            existinglayer = SdfLayer::FindOrOpen(
                fullfilepath.toStdString());
            if (existinglayer)
            {
                // Call the USD implementation directly instead of
                // HUSDstitchLayers because at this point we've
                // already made all Solaris-specific modifications we
                // might want to make to these layers.
                UsdUtilsStitchLayers(existinglayer, layer);
                existinglayer->Save();
            }
            else
                success = saveLayer(layer, fullfilepath);
        }
        else
        {
            // This is the first time this save operation has seen this
            // file. Overwrite any existing file with the layer
            // contents.
            success = saveLayer(layer, fullfilepath);
            saved_path_info_map.emplace(fullfilepath, XUSD_SavePathInfo(
                fullfilepath, filepath, false, filepath_is_time_dependent));
        }
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
            UT_StringHolder      orig_path;
            UT_StringHolder      final_path;
	    bool                 using_node_path = false;
            bool                 time_dependent = false;

            // Get the path specified by the user in node parameters while
            // cooking the network.
	    if (identifier != rootidentifier)
            {
		orig_path = HUSDgetLayerSaveLocation(layer, &using_node_path);
		time_dependent = HUSDgetSavePathIsTimeDependent(layer);
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
            {
		orig_path = filepath.c_str();
                time_dependent = filepath_is_time_dependent;
            }

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
		final_path, orig_path, using_node_path, time_dependent);
	}

	// For all layers we want to save, make a copy of the layer. Then
	// update all paths from lop or internal paths to the locations
	// where those layers will be saved to disk. Also update full paths
	// to relative paths for files on disk. Finally save the updated
	// layer to its desired location on disk.
        success = true;
	for (auto &&it : idtolayermap)
	{
            std::string              identifier = it.first;
	    const XUSD_SavePathInfo &outpathinfo = idtosavepathmap[identifier];
	    const UT_StringHolder   &outfinalpath = outpathinfo.myFinalPath;
	    std::string              save_control;

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

                UT_StringArray time_dependent_references;
                std::map<std::string, std::string> replace_map;
		auto refs = layer->GetExternalReferences();

		for (auto &&ref : refs)
		{
		    // If the reference is an empty string, ignore it.
		    if (ref.empty())
			continue;

                    UT_StringHolder      newpath(ref);
		    auto                 updateit = idtosavepathmap.find(ref);

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
                        if (!outpathinfo.myTimeDependent &&
                            updateit->second.myTimeDependent)
                            time_dependent_references.append(
                                updateit->second.myFinalPath);
                    }

		    if (ref != newpath.c_str())
                        replace_map[ref] = newpath;
		}
                saveVolumes(layercopy,
                    processordata.myProcessors,
                    outfinalpath,
                    saved_geo_map,
                    replace_map,
                    volume_save_map);
                HUSDmodifyAssetPaths(layercopy,
                    husd_UpdateReferencesWithOutputProcessors(
                        processordata.myProcessors,
                        outfinalpath,
                        replace_map));

		if (flags.myClearHoudiniCustomData)
		    clearHoudiniCustomData(layercopy);
		if (flags.myEnsureMetricsSet)
		    ensureMetricsSet(layercopy, stage);
                if (saved_path_info_map.contains(outfinalpath))
                {
                    // We've been asked to save to this layer before. Load the
                    // existing file, stitch the new data into it, and save it
                    // out.
                    SdfLayerRefPtr existinglayer;

                    existinglayer = SdfLayer::FindOrOpen(
                        outfinalpath.toStdString());
                    if (existinglayer)
                    {
                        // Call the USD implementation directly instead of
                        // HUSDstitchLayers because at this point we've
                        // already made all Solaris-specific modifications we
                        // might want to make to these layers.
                        UsdUtilsStitchLayers(existinglayer, layercopy);
                        existinglayer->Save();
                    }
                    else
                        success &= saveLayer(layercopy, outfinalpath);
                }
                else
                {
                    // This is the first time this save operation has seen this
                    // file.  Overwrite any existing file with the layer
                    // contents.
                    success &= saveLayer(layercopy, outfinalpath);
                    saved_path_info_map.emplace(outfinalpath, outpathinfo);
                }

                XUSD_SavePathInfo &outinfo = saved_path_info_map[outfinalpath];
                if (!outinfo.myWarnedAboutMixedTimeDependency &&
                    !time_dependent_references.isEmpty())
                {
                    UT_WorkBuffer msgbuf;

                    msgbuf.sprintf("'%s' references:\n", outfinalpath.c_str());
                    msgbuf.append(time_dependent_references, "\n");
                    HUSD_ErrorScope::addWarning(
                        HUSD_ERR_MIXED_SAVE_PATH_TIME_DEPENDENCY,
                        msgbuf.buffer());
                    outinfo.myWarnedAboutMixedTimeDependency = true;
                }
	    }
	}
    }
    endSaveOutputProcessors(processordata.myProcessors);

    // Do the actual saving of the volumes now that we've collected all
    // the information about them.
    for (auto &&savemapit : volume_save_map)
        savemapit.second.myDetailHandle.gdp()->save(savemapit.first, nullptr);

    // Call Reload for any layers we just saved.
    std::set<SdfLayerHandle>	 saved_layers;
    UT_StringSet                 paths;
    for (auto it = saved_path_info_map.begin();
              it != saved_path_info_map.end(); ++it)
    {
	auto existing_layer = SdfLayer::Find(it->first.toStdString());
	if (existing_layer)
	    saved_layers.insert(existing_layer);
        paths.insert(it->first);
    }

    {
	// Create an error scope to eat any errors triggered by the reload.
	UT_ErrorManager		 errmgr;
	HUSD_ErrorScope		 scope(&errmgr);
        GusdStageCacheWriter	 cache;

        // Clear the whole cache of automatic ref prim paths, because the
        // layers we are saving may be used by any stage, and so may affect
        // the default/automatic default prim of any stage.
        cache.Clear(paths);
        HUSDclearBestRefPathCache();
	SdfLayer::ReloadLayers(saved_layers, true);
    }

    return success;
}

} // end namespace

class HUSD_Save::husd_SavePrivate {
public:
    void                            clearAfterSingleFrameSave()
                                    {
                                        myStage.Reset();
                                        myHoldLayers.clear();
                                        myLockedGeoArray.clear();
                                        myReplacementLayerArray.clear();
                                        myLockedStages.clear();
                                        // Keep the set of saved layers and
                                        // geometry files.
                                    }
    void                            clearSaveHistory()
                                    {
                                        // Explicit request to clear the saved
                                        // layer and geometry files.
                                        mySavedGeoMap.clear();
                                        mySavedPathInfoMap.clear();
                                    }

    UsdStageRefPtr		        myStage;
    SdfLayerRefPtrVector	        myHoldLayers;
    XUSD_LockedGeoArray		        myLockedGeoArray;
    XUSD_LayerArray		        myReplacementLayerArray;
    HUSD_LockedStageArray	        myLockedStages;
    UT_StringMap<XUSD_SavePathInfo>     mySavedPathInfoMap;
    std::map<std::string, std::string>  mySavedGeoMap;
    XUSD_ExistenceTracker               myExistenceTracker;
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
HUSD_Save::addCombinedTimeSample(const HUSD_AutoReadLock &lock,
        const HUSD_TimeCode &timecode)
{
    auto		 indata = lock.data();
    bool		 success = false;

    if (!myPrivate->myStage)
    {
        // If we are flattening the input stage, and the input has a load mask,
        // use it, so that the layer muting and population masking affect the
        // resulting stage that we save. When not flattening the stage, the
        // meaning of the load mask is a lot less clear, so don't try to
        // account for it.
        if (mySaveStyle == HUSD_SAVE_FLATTENED_STAGE &&
            lock.constData()->loadMasks())
        {
            myPrivate->myStage = HUSDcreateStageInMemory(
                lock.constData()->loadMasks().get(),
                indata->stage());
        }
        else
        {
            myPrivate->myStage = HUSDcreateStageInMemory(
                mySaveStyle == HUSD_SAVE_FLATTENED_STAGE
                    ? UsdStage::LoadAll
                    : UsdStage::LoadNone,
                indata->stage());
        }
    }

    if (indata && indata->isStageValid())
    {
        // Set the force_notifiable_file_format parameter to false because
        // here we are writing these files to disk, so we don't need
        // accurate fine grained notifications to generate correct output.
	success = HUSDaddStageTimeSample(indata->stage(), myPrivate->myStage,
            HUSDgetUsdTimeCode(timecode), myPrivate->myHoldLayers, false, true,
            trackPrimExistence() ? &myPrivate->myExistenceTracker : nullptr);
	myPrivate->myLockedGeoArray.concat(indata->lockedGeos());
	myPrivate->myReplacementLayerArray.concat(indata->replacements());
	myPrivate->myLockedStages.concat(indata->lockedStages());
    }

    return success;
}

bool
HUSD_Save::saveCombined(const UT_StringRef &filepath,
        bool filepath_is_time_dependent,
	UT_StringArray &saved_paths)
{
    bool		 success = false;

    if (myPrivate->myStage)
    {
        if (myPrivate->myExistenceTracker.getVisibilityLayer())
            UsdUtilsStitchLayers(myPrivate->myStage->GetRootLayer(),
                myPrivate->myExistenceTracker.getVisibilityLayer());

        success = saveStage(myPrivate->myStage,
            filepath,
            filepath_is_time_dependent,
	    mySaveFilesPattern.get(),
            mySaveStyle,
            myProcessorData,
            myDefaultPrimData,
            myTimeData,
            myFlags,
	    myPrivate->mySavedPathInfoMap,
	    myPrivate->mySavedGeoMap);
    }
    for (auto it = myPrivate->mySavedPathInfoMap.begin();
              it != myPrivate->mySavedPathInfoMap.end(); ++it)
        saved_paths.append(it->first);

    return success;
}

void
HUSD_Save::clearSaveHistory()
{
    myPrivate->clearSaveHistory();
}

bool
HUSD_Save::save(const HUSD_AutoReadLock &lock,
        const HUSD_TimeCode &timecode,
	const UT_StringRef &filepath,
        bool filepath_is_time_dependent,
	UT_StringArray &saved_paths)
{
    bool                 success = false;

    // If the file path value is time dependent, and we are doing per-frame
    // save operations, we do not want to do existence tracking. It would
    // write animated visibility to each per-frame file, which makes no sense.
    // So turn off the option and produec a warning.
    if (trackPrimExistence() && filepath_is_time_dependent)
    {
        setTrackPrimExistence(false);
        HUSD_ErrorScope::addWarning(
            HUSD_ERR_EXISTENCE_TRACKING_PER_FRAME_FILES);
    }
    // Even when saving a single time sample, we need to run the combine code,
    // which stitches layers together, and makes sure that all layers paths
    // that will be written to are unique (even if multiple layers indicate
    // that they want to be written to the same location on disk).
    success = addCombinedTimeSample(lock, timecode);
    if (success)
        success = saveCombined(filepath,
            filepath_is_time_dependent, saved_paths);
    // Wipe out any record of this save operation, otherwise we'll combine it
    // with the next one, if there is one.
    myPrivate->clearAfterSingleFrameSave();

    return success;
}
