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
#include "HUSD_ProjectConfig.h"
#include "XUSD_Data.h"
#include "XUSD_ExistenceTracker.h"
#include "XUSD_LockedGeoRegistry.h"
#include "XUSD_Utils.h"
#include <gusd/stageCache.h>
#include <OBJ/OBJ_Node.h>
#include <SOP/SOP_Node.h>
#include <OP/OP_Director.h>
#include <OP/OP_Node.h>
#include <CH/CH_Manager.h>
#include <GA/GA_Handle.h>
#include <GA/GA_SaveOptions.h>
#include <GU/GU_Detail.h>
#include <GU/GU_SopResolver.h>
#include <GEO/GEO_Primitive.h>
#include <IMG/IMG_File.h>
#include <IMG/IMG_SaveRastersToFilesParms.h>
#include <IMX/IMX_Layer.h>
#include <IMX/IMX_UDIMUtils.h>
#include <TIL/TIL_CopResolver.h>
#include <TIL/TIL_Raster.h>
#include <TIL/TIL_MakeTexture.h>
#include <UT/UT_Map.h>
#include <UT/UT_Assert.h>
#include <UT/UT_Defines.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_ErrorManager.h>
#include <UT/UT_FileUtil.h>
#include <SYS/SYS_ParseNumber.h>
#include <tools/henv.h>
#include <pxr/usd/usdUtils/dependencies.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <pxr/usd/usdUtils/stitch.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/variableExpression.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <cctype>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

bool
beginSaveOutputProcessors(
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        OP_Node *config_node,
        OP_Node *lop_node,
        fpreal t,
        const VtDictionary &stage_variables_dict)
{
    UT_Options stage_variables;
    bool success = true;

    HUSDconvertDictionary(stage_variables, stage_variables_dict);
    for (auto &&processor : output_processors)
    {
        if (processor.myProcessor)
        {
            UT_String error;
            processor.myProcessor->beginSave(
                config_node, processor.myOverrides, lop_node, t,
                stage_variables, error);
            // If there's an error, surface it, but keep running  beginSave
            // on each output processor.
            if (error.isstring())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_OUTPUT_PROCESSOR_ERROR, error.c_str());
                success = false;
            }
        }
    }

    return success;
}

UT_StringHolder
runOutputProcessors(
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        const UT_StringRef &asset_path,
        const UT_StringRef &referencing_layer_path,
        bool asset_is_layer,
        bool for_save,
        UT_String &error)
{
    UT_StringHolder  processedpath(asset_path);

    for (auto &&processor : output_processors)
    {
        if (processor.myProcessor)
        {
            UT_String tmpprocessed;

            if (for_save)
            {
                if (processor.myProcessor->processSavePath(
                        processedpath,
                        referencing_layer_path,
                        asset_is_layer,
                        tmpprocessed,
                        error) &&
                    tmpprocessed.isstring())
                    processedpath = tmpprocessed;
            }
            else if (SdfVariableExpression::IsExpression(processedpath.c_str()))
            {
                if (processor.myProcessor->processReferenceExpression(
                        processedpath,
                        referencing_layer_path,
                        asset_is_layer,
                        tmpprocessed,
                        error) &&
                    tmpprocessed.isstring())
                    processedpath = tmpprocessed;
            }
            else
            {
                if (processor.myProcessor->processReferencePath(
                        processedpath,
                        referencing_layer_path,
                        asset_is_layer,
                        tmpprocessed,
                        error) &&
                    tmpprocessed.isstring())
                    processedpath = tmpprocessed;
            }

            if (error.isstring())
                break;
        }
    }

    return processedpath;
}

bool
shouldSaveFile(
    const HUSD_OutputProcessorAndOverridesArray &output_processors,
    const UT_PathPattern *save_files_pattern,
    const UT_StringRef &final_path,
    const UT_StringRef &layer_identifier,
    UT_String &error)
{
    HUSD_OutputProcessor::HUSD_ShouldSave should_save =
        HUSD_OutputProcessor::SHOULD_SAVE_NO_OPINION;

    for (auto &&processor : output_processors)
    {
        if (processor.myProcessor)
        {
            auto result = processor.myProcessor->shouldSave(
                final_path, layer_identifier, error);
            // In case of an error, don't save the file or continue running
            // output processors.
            if (error.isstring())
                return false;
            if (result != HUSD_OutputProcessor::SHOULD_SAVE_NO_OPINION)
                should_save = result;
        }
    }

    // If the last output processor to express an opinion said to not save
    // the file, then don't save the file.
    if (should_save == HUSD_OutputProcessor::SHOULD_SAVE_FALSE)
        return false;

    // Check if this file we are about to save is part of the
    // pattern of files that we have been asked to save. No
    // pattern means we accept all files. This parameter is only
    // respected for files about which output processors have no
    // opinion.
    if (save_files_pattern &&
        should_save == HUSD_OutputProcessor::SHOULD_SAVE_NO_OPINION &&
        !save_files_pattern->matches(final_path))
        return false;

    return true;
}

void
endSaveOutputProcessors(
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        OP_Node *config_node,
        OP_Node *lop_node,
        fpreal t,
        const VtDictionary &stage_variables_dict,
        const UT_StringArray &saved_paths,
        const UT_String &error_messages)
{
    UT_Options stage_variables;

    HUSDconvertDictionary(stage_variables, stage_variables_dict);
    for (auto &&processor : output_processors)
    {
        if (processor.myProcessor)
        {
            UT_String error;
            processor.myProcessor->endSave(
                config_node, processor.myOverrides, lop_node, t,
                stage_variables, saved_paths, error_messages, error);
            // Even if there's an error, call endSave on all processors.
            if (error.isstring())
                HUSD_ErrorScope::addError(
                    HUSD_ERR_OUTPUT_PROCESSOR_ERROR, error.c_str());
        }
    }
}

class husd_UpdateReferencesWithOutputProcessors
{
public:
    husd_UpdateReferencesWithOutputProcessors(
            const HUSD_OutputProcessorAndOverridesArray &output_processors,
            const UT_StringHolder &layer_save_path,
            const std::map<std::string, std::string> &replace_map,
            UT_String &error)
        : myLayerSavePath(layer_save_path),
          myOutputProcessors(output_processors),
          myReplaceMap(replace_map),
          myError(error)
    { }
    ~husd_UpdateReferencesWithOutputProcessors()
    { }

    std::string
    operator()(const std::string &assetPath)
    {
        if (myError.isstring())
            return assetPath;

        auto replace_it = myReplaceMap.find(assetPath);
        if (replace_it != myReplaceMap.end())
            return replace_it->second;

        UT_StringHolder processed = runOutputProcessors(
            myOutputProcessors,
            assetPath,
            myLayerSavePath,
            false,
            false,
            myError);
        if (myError.isstring())
            return assetPath;

        if (processed.isstring() && processed != assetPath)
            return processed.toStdString();

        return assetPath;
    }

private:
    const UT_StringHolder &myLayerSavePath;
    const HUSD_OutputProcessorAndOverridesArray &myOutputProcessors;
    const std::map<std::string, std::string> &myReplaceMap;
    UT_String &myError;
};

class husd_VolumeSavePrim
{
public:
    husd_VolumeSavePrim()
    { }
    ~husd_VolumeSavePrim()
    { }

    UT_StringHolder      myVolumeName;
    int                  myVolumeIndex = -1;

    UT_StringHolder      mySourcePath;
    UT_StringHolder      mySourceVolumeName;
    int                  mySourceVolumeIndex = -1;
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
typedef UT_StringMap<UT_StringHolder> husd_NodeDataSaveMap;

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
        husd_VolumeSaveMap &volume_save_map,
        UT_String &error)
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

            if (volumesavepath.empty())
            {
                char numstr[UT_NUMBUF];

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
                layer_save_path, false, true, error);
            if (error.isstring())
                return { SdfAssetPath(), 0 };

            // Record information for updating the volume filePath attribute
            // and saving out the volume data to a file later.
            newrefaspath = runOutputProcessors(output_processors,
                newpath, layer_save_path, false, false, error);
            if (error.isstring())
                return { SdfAssetPath(), 0 };
            saved_geo_map[geo_map_key] = newpath;
            saved_geo_map[newpath.toStdString()] = newrefaspath;
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
            GU_ConstDetailHandle gdh;

            SdfLayer::SplitIdentifier(oldpath, &oldfilepath, &args);
            gdh = XUSD_LockedGeoRegistry::getGeometry(oldfilepath, args);
            if (gdh)
            {
                GU_DetailHandleAutoReadLock lock(gdh);
                const GU_Detail *gdp = lock.getGdp();

                if (gdp)
                {
                    // Before writing the VDB file to disk, resolve the path
                    // to maximize the chance we'll have a real path to a file
                    // on disk that the VDB writing code will understand.
                    ArResolvedPath resolved_path = ArGetResolver().
                        ResolveForNewAsset(newpath.toStdString());
                    UT_String diskpath = newpath.c_str();
                    UT_String diskdir, diskfile;
                    if (!resolved_path.IsEmpty())
                    {
                        diskpath = resolved_path.GetPathString();
                        // Windows resolver returns paths with backslashes. Our
                        // file path handling code doesn't like that.
                        diskpath.substitute('\\', '/');
                    }

                    // Create the directory for holding the processed file path.
                    diskpath.splitPath(diskdir, diskfile);
                    if (diskdir.isstring() && UT_FileUtil::makeDirs(diskdir))
                    {
                        // Write the volume to disk.
                        husd_VolumeSavePrim saveprim = saveVolumesWithSavePath(
                            gdp, is_vdb, oldpath, volumename, volumeindex,
                            diskpath, volume_save_map);
                        newindex = saveprim.myVolumeIndex;
                    }
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

// Shared tail of saveImage and saveGeometry. Runs the output processors
// on a candidate base path, ensures the output directory exists,
// deduplicates and uniquifies against the node data save map, invokes
// the caller-supplied save_to_disk callback to actually write the file,
// records the saved path, and produces the file path that should be
// written back into the USD layer to reference the saved file.
template <typename SaveCallback>
SdfAssetPath
saveNodeDataAsset(
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        const UT_StringRef &layer_save_path,
        const UT_PathPattern *save_files_pattern,
        const UT_StringRef &basepath,
        const UT_StringRef &oldpath,
        bool user_supplied_path,
        int overwrite_error_code,
        husd_NodeDataSaveMap &node_data_save_map,
        SaveCallback &&save_to_disk,
        UT_String &error,
        int udim_tile = 0)
{
    UT_StringHolder      newrefaspath;
    UT_String            newpath;

    // Run the new path through the asset processors.
    newpath = runOutputProcessors(output_processors, basepath,
        layer_save_path, false, true, error);
    if (error.isstring())
        return SdfAssetPath();
    if (!shouldSaveFile(output_processors, save_files_pattern,
            newpath, UT_StringHolder::theEmptyString, error))
        return SdfAssetPath();

    // Resolve the path to maximize the chance we'll have a real path to a
    // file on disk that the asset writing code will understand.
    ArResolvedPath resolved_path = ArGetResolver().
        ResolveForNewAsset(newpath.toStdString());
    UT_String diskpath = newpath.c_str();
    UT_String diskdir, diskfile;
    if (!resolved_path.IsEmpty())
    {
        diskpath = resolved_path.GetPathString();
        // Windows resolver returns paths with backslashes. Our file path
        // handling code doesn't like that.
        diskpath.substitute('\\', '/');
    }

    // Create the directory for holding the processed file path.
    diskpath.splitPath(diskdir, diskfile);
    if (diskdir.isstring() && UT_FileUtil::makeDirs(diskdir))
    {
        auto it = node_data_save_map.find(diskpath);

        // If we already wrote this original op: path to the requested path
        // on disk, we can just skip the actual save.
        if (it == node_data_save_map.end() || it->second != oldpath)
        {
            // Make sure the new file name is unique, and add an entry to
            // the node data save map.
            if (it != node_data_save_map.end())
            {
                if (!user_supplied_path)
                {
                    char *dot = diskfile.findChar('.');
                    UT_StringHolder root;
                    UT_StringHolder ext;
                    int unique_number = 1;
                    if (dot)
                    {
                        root = UT_StringHolder(diskfile, dot - diskfile.c_str());
                        ext = UT_StringHolder(dot + 1);
                    }
                    else
                        root = diskfile.c_str();

                    while (node_data_save_map.contains(diskpath))
                    {
                        diskfile.sprintf("%s.%d.%s",
                            root.c_str(), unique_number++, ext.c_str());
                        diskpath.sprintf("%s/%s",
                            diskdir.c_str(), diskfile.c_str());
                    }

                    // Apply the same basename change to newpath so the
                    // path written into the USD layer references the file
                    // we actually wrote to disk. The Ar resolver may have
                    // rewritten the directory portion of diskpath, but the
                    // basename is shared between newpath and diskpath.
                    UT_String newdir, newfile;
                    newpath.splitPath(newdir, newfile);
                    if (newdir.isstring())
                        newpath.sprintf("%s/%s",
                            newdir.c_str(), diskfile.c_str());
                    else
                        newpath = diskfile;
                }
                else
                {
                    UT_WorkBuffer msg;
                    msg.sprintf("'%s' over '%s' as '%s'",
                        oldpath.c_str(), it->second.c_str(),
                        diskpath.c_str());
                    HUSD_ErrorScope::addWarning(
                        overwrite_error_code,
                        msg.buffer());
                }
            }
            
            if (udim_tile != 0)
            {
                UT_String tile_string;
                tile_string.itoa(udim_tile);
                diskpath.substitute("<UDIM>", tile_string.c_str());
            }

            // Don't use insert/emplace, we want to force a replacement in
            // case this is not the first time we're writing out this file.
            node_data_save_map[diskpath] = oldpath;
            // Save the file to disk.
            save_to_disk(diskpath);
        }
        // Use output processors to generate the file path that should be
        // put in the USD layer to reference the file we just saved.
        newrefaspath = runOutputProcessors(output_processors,
            newpath, layer_save_path, false, false, error);
        if (error.isstring())
            return SdfAssetPath();
    }

    return (newrefaspath.isstring())
        ? SdfAssetPath(newrefaspath.toStdString())
        : SdfAssetPath();
}

// Utility for savePotentialUDIMImage. Constructs the path to save the image
// without the udim tile number or extension. 
UT_WorkBuffer
constructImageBasePathRoot(const UT_StringRef &oldpath,
        const UT_StringRef &layer_save_path)
{
    UT_WorkBuffer        basepath;
    UT_WorkBuffer        color;
    UT_WorkBuffer        alpha;
    fpreal               frame = SYS_FPREAL_MAX;
    int                  cindex = -1;
    int                  aindex = -1;
    int                  xres = -1;
    int                  yres = -1;
    int                  copnodeid = OP_INVALID_NODE_ID;
    OP_Node             *copnode = nullptr;
    UT_StringHolder      copnodepath;
    UT_String            numstr;
    bool                 specific_frame = false;
    bool                 timedep = false;

    if (TIL_CopResolver::splitPath(oldpath.c_str(), copnodeid, frame,
            color, cindex, alpha, aindex, xres, yres) > 0 &&
        frame != SYS_FPREAL_MAX)
        specific_frame = true;
    else
        frame = CHgetSampleFromTime(CHgetEvalTime());
    copnode = OP_Node::lookupNode(copnodeid);
    if (CAST_COPNODE(copnode))
    {
        copnodepath = copnode->getFullPath();
        if (!specific_frame)
            timedep = copnode->dataMicroNode().isTimeDependent();
    }

    // Create an image file path based on the path where the
    // layer will be saved.
    basepath.append(layer_save_path);
    basepath.append(".textures");
    basepath.append(copnodepath);
    // Include the frame number in the path if the op: path specified a
    // particular frame or the COP is time dependent.
    if (timedep || specific_frame)
    {
        numstr.sprintf("%g", CH_Manager::niceNumber(frame));
        basepath.append(".");
        basepath.append(numstr);
    }
    if (color.isstring())
    {
        basepath.append(".");
        basepath.append(color);
        if (cindex >= 0)
        {
            numstr.itoa(cindex);
            basepath.append(".");
            basepath.append(numstr);
        }
    }
    if (alpha.isstring())
    {
        basepath.append(".");
        basepath.append(alpha);
        if (aindex >= 0)
        {
            numstr.itoa(aindex);
            basepath.append(".");
            basepath.append(numstr);
        }
    }

    return basepath;
}

// Save a single image or single tile of UDIM texture.
// udim_tile = 0 indicates that the image does not use UDIM mapping.
SdfAssetPath
saveImage(const UT_StringRef &oldpath,
        const UT_StringRef &layer_save_path,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        const UT_PathPattern *save_files_pattern,
        husd_NodeDataSaveMap &node_data_save_map,
        const UT_WorkBuffer &save_path_root,
        UT_String &error,
        int udim_tile = 0)
{
    // Get the IMX_Layer from the COP node.
    UT_SharedPtr<const IMX_Layer>    imxlayer;
    UT_UniquePtr<TIL_Raster>         raster;
    int                              copnodeid = OP_INVALID_NODE_ID;

    if (udim_tile != 0)
        imxlayer = TIL_CopResolver::getLayer(oldpath.c_str(), copnodeid, udim_tile);
    else
        imxlayer = TIL_CopResolver::getLayer(oldpath.c_str(), copnodeid);
    
    if (imxlayer)
        raster = imxlayer->buildRaster();
    if (!raster)
    {
        HUSD_ErrorScope::addError(
            HUSD_ERR_COP_TEXTURE_NOT_FOUND,
            oldpath.c_str());
        return SdfAssetPath();
    }

    UT_WorkBuffer        basepath;
    UT_OptionsHolder     layerattributes = imxlayer->properties();
    bool                 user_supplied_path = false;

    if (layerattributes->hasOption("savepath") &&
        layerattributes->getOptionType("savepath") == UT_OPTION_STRING)
    {
        basepath = layerattributes->getOptionS("savepath");
        user_supplied_path = true;
    }
    else
    {
        basepath.append(save_path_root);
        
        if (udim_tile != 0)
            basepath.append(".<UDIM>");

        basepath.append(IMG_File::getAutoTextureSaveFileExtention());
    }

    return saveNodeDataAsset(output_processors, layer_save_path,
        save_files_pattern, basepath, oldpath, user_supplied_path,
        HUSD_ERR_COP_TEXTURE_NOT_FOUND, node_data_save_map,
        [&raster](const UT_String &diskpath) {
            IMG_SaveRastersToFilesParms saveparms;
            IMG_File::saveRasterAsFile(diskpath, raster.get(), saveparms);
            if (UT_StringView(diskpath).endsWith(".exr", false))
            {
                TIL_MakeTexture maker;
                maker.makeTexture(diskpath, diskpath);
            }
        }, error, udim_tile);
}

SdfAssetPath
savePotentialUDIMImage(const SdfAssetPath &assetpath,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        const UT_StringRef &layer_save_path,
        const UT_PathPattern *save_files_pattern,
        husd_NodeDataSaveMap &node_data_save_map,
        UT_String &error)
{
    // We only care about texture paths with an "op:" prefix.
    std::string	         oldpath = assetpath.GetAssetPath();
    if (!UT_String(oldpath.c_str()).startsWith(OPREF_PREFIX))
        return SdfAssetPath();

    size_t udim_index = oldpath.find("?udim=");

    UT_StringHolder newpath;
    UT_StringHolder	newrefaspath;
    UT_StringHolder path(oldpath.c_str(),
                        udim_index == std::string::npos ? oldpath.size() : udim_index);

    UT_WorkBuffer savepathroot = constructImageBasePathRoot(path, layer_save_path);

    if (udim_index == std::string::npos)
        return saveImage(path, layer_save_path, output_processors,
            save_files_pattern, node_data_save_map,
            savepathroot, error);


    UT_StringHolder udim_pattern(UT_StringHolder::REFERENCE, oldpath.c_str() + udim_index + 6);
    UT_Array<int> udim_list = IMXparseUDIMList(udim_pattern);

    SdfAssetPath result;
    for(int tile : udim_list)
    {
        result = saveImage(path, layer_save_path, output_processors,
            save_files_pattern, node_data_save_map, savepathroot,
            error, tile);
    }

    return result;
}

// Shared tail of saveGeometry and saveLockedGeometry. Honors an optional
// "usdconfigsavepath" detail attribute as a path override, then writes the
// geometry to disk via saveNodeDataAsset. Pass info_time = SYS_FPREAL_MAX
// to skip the GA "info:time" option (used by saveLockedGeometry, which
// has no live OP_Context to read a cook time from).
SdfAssetPath
saveDetailAsBgeoFile(
        const GU_Detail *gdp,
        const UT_StringRef &oldpath,
        const UT_StringRef &default_basepath,
        fpreal info_time,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        const UT_StringRef &layer_save_path,
        const UT_PathPattern *save_files_pattern,
        husd_NodeDataSaveMap &node_data_save_map,
        UT_String &error)
{
    UT_WorkBuffer        basepath;
    bool                 user_supplied_path = false;

    // If the geometry has an explicit "usdconfigsavepath" detail string
    // attribute, use that as the destination file path.
    GA_ROHandleS         savepath_h(gdp, GA_ATTRIB_DETAIL, "usdconfigsavepath");
    if (savepath_h.isValid())
    {
        UT_StringHolder savepath_value = savepath_h.get(GA_Offset(0));
        if (savepath_value.isstring())
        {
            basepath = savepath_value;
            user_supplied_path = true;
        }
    }

    if (!user_supplied_path)
        basepath.append(default_basepath);

    return saveNodeDataAsset(output_processors, layer_save_path,
        save_files_pattern, basepath, oldpath, user_supplied_path,
        HUSD_ERR_SOP_GEOMETRY_OVERWRITTEN, node_data_save_map,
        [&](const UT_String &diskpath)
        {
            GA_SaveOptions saveoptions;
            if (info_time != SYS_FPREAL_MAX)
                saveoptions.setOptionF("info:time", info_time);
            gdp->save(diskpath.c_str(), &saveoptions);
        },
        error);
}

SdfAssetPath
saveSopGeometry(const GU_SopQuery &sopquery,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        const UT_StringRef &layer_save_path,
        const UT_PathPattern *save_files_pattern,
        husd_NodeDataSaveMap &node_data_save_map,
        UT_String &error)
{
    // Reconstruct the original "op:" asset path from the SOP node. This is
    // used as the source identity in the node data save map and in any
    // diagnostic messages. Note that this path will be resolved to the
    // SOP even if the path points to an OBJ node. This is a good thing so
    // we treat /obj/geo1 and /obj/geo1/__render__ as being identical.
    SOP_Node *sop = CAST_SOPNODE(OP_Node::lookupNode(sopquery.getOpId()));
    // By default we cook the SOP at the current evaluation time.
    fpreal cook_frame = CHgetSampleFromTime(CHgetEvalTime());
    // Override the cook time from the SOP query, if it was set there.
    sopquery.getFrame(cook_frame);
    // Rebuild our path from the SOP path and cook time.
    UT_WorkBuffer oldpath;
    oldpath.format("{}{}[{}]", OPREF_PREFIX, sop->getFullPath(),
        CH_Manager::niceNumber(cook_frame));
    // Cook the SOP and grab its detail.
    OP_Context context(CHgetTimeFromFrame(cook_frame));
    GU_DetailHandle gdh = sop->getCookedGeoHandle(context);
    GU_DetailHandleAutoReadLock readlock(gdh);
    const GU_Detail *gdp = readlock.getGdp();
    if (!gdp)
    {
        HUSD_ErrorScope::addError(
            HUSD_ERR_SOP_GEOMETRY_NOT_FOUND,
            oldpath.c_str());
        return SdfAssetPath();
    }

    // Build the default base path from the SOP node path. Include the
    // frame number when the SOP cook is time dependent.
    UT_WorkBuffer    default_basepath;
    UT_String        sopnodepath;
    UT_String        numstr;
    bool             timedep = sop->dataMicroNode().isTimeDependent();

    sop->getFullPath(sopnodepath);

    default_basepath.append(layer_save_path);
    default_basepath.append(".geometry");
    default_basepath.append(sopnodepath);
    if (timedep)
    {
        numstr.sprintf("%g", CH_Manager::niceNumber(cook_frame));
        default_basepath.append(".");
        default_basepath.append(numstr);
    }
    default_basepath.append(".bgeo.sc");

    return saveDetailAsBgeoFile(gdp, oldpath, default_basepath,
        context.getTime(),
        output_processors, layer_save_path, save_files_pattern,
        node_data_save_map, error);
}

SdfAssetPath
saveLockedGeometry(const SdfAssetPath &assetpath,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        const UT_StringRef &layer_save_path,
        const UT_PathPattern *save_files_pattern,
        husd_NodeDataSaveMap &node_data_save_map,
        UT_String &error)
{
    const std::string &oldpath = assetpath.GetAssetPath();

    // The locked-geo identifier encodes both the SOP-style node path and
    // the FileFormatArguments used at registration time. Split them back
    // out to look up the cached GU_Detail.
    SdfFileFormat::FileFormatArguments args;
    std::string path;
    SdfLayer::SplitIdentifier(oldpath, &path, &args);

    GU_ConstDetailHandle gdh =
        XUSD_LockedGeoRegistry::getGeometry(path, args);
    GU_DetailHandleAutoReadLock readlock(gdh);
    const GU_Detail *gdp = readlock.getGdp();
    if (!gdp)
    {
        HUSD_ErrorScope::addError(
            HUSD_ERR_SOP_GEOMETRY_NOT_FOUND,
            oldpath.c_str());
        return SdfAssetPath();
    }

    // Build a default base path from the locked-geo identifier path.
    UT_String cleanpath(path.c_str(), true);
    // Strip of a leading "op:" if it's there (though it isn't required).
    if (cleanpath.startsWith(OPREF_PREFIX))
        cleanpath.replacePrefix(OPREF_PREFIX, "");
    // Make sure the path starts with a "/" to create a directory under the
    // layer_save_path location (like we do with volumes).
    if (!cleanpath.startsWith("/"))
        cleanpath.insert(0, "/");
    // Strip any extension so the on-disk file name doesn't carry a
    // redundant suffix. Collisions between different source path "types"
    // will be resolved by our normal collision detection and resolution.
    if (cleanpath.lastChar('.'))
        *cleanpath.lastChar('.') = '\0';

    UT_WorkBuffer default_basepath;
    default_basepath.append(layer_save_path);
    default_basepath.append(".geometry");
    default_basepath.append(cleanpath);
    default_basepath.append(".bgeo.sc");

    return saveDetailAsBgeoFile(gdp, oldpath, default_basepath,
        SYS_FPREAL_MAX,
        output_processors, layer_save_path, save_files_pattern,
        node_data_save_map, error);
}

VtValue
saveImageOrGeometry(const VtValue &file_path_value,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        const UT_StringRef &layer_save_path,
        const UT_PathPattern *save_files_pattern,
        husd_NodeDataSaveMap &node_data_save_map,
        std::map<std::string, std::string> &replace_map,
        UT_String &error)
{
    if (file_path_value.IsEmpty())
        return VtValue();

    // Process a single SdfAssetPath. Returns the rewritten path, or an
    // empty SdfAssetPath if no save was needed. Dispatches in priority
    // order to: a registered locked-geo entry, a SOP referenced via an
    // "op:" path, or a COP texture (saveImage). COP paths may carry
    // frame/plane suffixes that findNode can't resolve, which is why
    // saveImage is the catch-all using TIL_CopResolver.
    auto process_one = [&](const SdfAssetPath &assetpath) -> SdfAssetPath
    {
        const std::string &oldpath = assetpath.GetAssetPath();

        // Locked-geo identifiers are SOP-style paths (e.g. /obj/geo1.sop)
        // rather than "op:" URLs, so they wouldn't be caught by the
        // OPREF_PREFIX branch below. Check the registry first.
        {
            SdfFileFormat::FileFormatArguments args;
            std::string path;
            SdfLayer::SplitIdentifier(oldpath, &path, &args);
            if (XUSD_LockedGeoRegistry::getLockedGeo(path, args))
                return saveLockedGeometry(assetpath,
                    output_processors, layer_save_path, save_files_pattern,
                    node_data_save_map, error);
        }

        if (UT_String(oldpath.c_str()).startsWith(OPREF_PREFIX))
        {
            GU_SopQuery sopquery;
            if (GU_SopResolver::lookup(oldpath.c_str(), sopquery))
                return saveSopGeometry(sopquery,
                    output_processors, layer_save_path, save_files_pattern,
                    node_data_save_map, error);
        }

        return savePotentialUDIMImage(assetpath,
            output_processors, layer_save_path, save_files_pattern,
            node_data_save_map, error);
    };

    // Add an identity entry to the replace_map so HUSDmodifyAssetPaths
    // doesn't run the output processors over the freshly-rewritten path.
    auto record_replace = [&](const SdfAssetPath &newpath)
    {
        replace_map.emplace(newpath.GetAssetPath(), newpath.GetAssetPath());
    };

    if (file_path_value.IsHolding<SdfAssetPath>())
    {
        SdfAssetPath newpath = process_one(
            file_path_value.UncheckedGet<SdfAssetPath>());
        if (newpath.GetAssetPath().empty())
            return VtValue();
        record_replace(newpath);
        return VtValue(newpath);
    }

    if (file_path_value.IsHolding<VtArray<SdfAssetPath>>())
    {
        const VtArray<SdfAssetPath> &paths =
            file_path_value.UncheckedGet<VtArray<SdfAssetPath>>();
        VtArray<SdfAssetPath> result(paths.size());
        bool changed = false;
        for (size_t i = 0, n = paths.size(); i < n; i++)
        {
            SdfAssetPath newpath = process_one(paths[i]);
            if (!newpath.GetAssetPath().empty())
            {
                record_replace(newpath);
                result[i] = newpath;
                changed = true;
            }
            else
            {
                result[i] = paths[i];
            }
        }
        if (!changed)
            return VtValue();
        return VtValue(result);
    }

    return VtValue();
}

void
saveNodeData(const SdfLayerRefPtr &layer,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
	const UT_StringRef &layer_save_path,
        const UT_PathPattern *save_files_pattern,
	std::map<std::string, std::string> &saved_geo_map,
	std::map<std::string, std::string> &replace_map,
        husd_VolumeSaveMap &volume_save_map,
        husd_NodeDataSaveMap &node_data_save_map,
        UT_String &error)
{
    static const TfToken	 theVDBPrimType("OpenVDBAsset");
    static const TfToken	 theHoudiniPrimType("HoudiniFieldAsset");
    static const TfToken	 theUsdShadePrimType("Shader");
    static const SdfPath         theFileAttrPath =
                                    SdfPath::ReflexiveRelativePath().
                                    AppendProperty(UsdVolTokens->filePath);
    static const SdfPath         theFieldIndexAttrPath =
                                    SdfPath::ReflexiveRelativePath().
                                    AppendProperty(UsdVolTokens->fieldIndex);

    // Recursive run through all primitives looking for op: asset paths
    // referencing live node data. Save any SOP volumes, COP textures, and
    // SOP geometry to disk, and update the asset paths to point at the
    // newly written files.
    layer->Traverse(SdfPath::AbsoluteRootPath(),
	[&layer, &layer_save_path, &output_processors,
         &save_files_pattern, &saved_geo_map, &replace_map,
         &volume_save_map, &node_data_save_map,
         &error](const SdfPath &path)
	{
            SdfPrimSpecHandle	primspec = layer->GetPrimAtPath(path);

            if (!error.isstring() &&
                primspec &&
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
                            volume_save_map, error));

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
                        volume_save_map, error));
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
            else if (primspec)
            {
                // Except for the specific cases of Volumes above, assume any
                // other asset path starting with "op:" is a COP image or
                // SOP geometry. These can show up in Shader, Material,
                // Light, and Camera prims. Best to be very inclusive (even
                // if it wastes some time excessively checking for the
                // "op:" prefix).
                for (auto &&attr : primspec->GetAttributes())
                {
                    if (attr->GetTypeName() == SdfValueTypeNames->Asset ||
                        attr->GetTypeName() == SdfValueTypeNames->AssetArray)
                    {
                        SdfTimeSampleMap samples = attr->GetTimeSampleMap();
                        bool samples_changed = false;

                        // Save out and update any data in time samples.
                        for (auto it = samples.begin();
                                  it != samples.end(); ++it)
                        {
                            VtValue newvalue(saveImageOrGeometry(it->second,
                                output_processors,
                                layer_save_path,
                                save_files_pattern,
                                node_data_save_map,
                                replace_map,
                                error));

                            if (!newvalue.IsEmpty())
                            {
                                it->second = newvalue;
                                samples_changed = true;
                            }
                        }
                        if (samples_changed)
                        {
                            attr->SetField(
                                SdfFieldKeys->TimeSamples, samples);
                        }

                        // Save out and update the default value.
                        VtValue newvalue(saveImageOrGeometry(
                            attr->GetDefaultValue(),
                            output_processors,
                            layer_save_path,
                            save_files_pattern,
                            node_data_save_map,
                            replace_map,
                            error));
                        if (!newvalue.IsEmpty())
                            attr->SetDefaultValue(newvalue);
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
                       HUSDgetPrimEditorNodesToken());
		    eraseHoudiniCustomData(prop_data,
			HUSDgetDataIdToken());
		    eraseHoudiniCustomData(prop_data,	 
			HUSDgetMaterialIdToken());
		}
	    }
	    else if (path.IsPrimOrPrimVariantSelectionPath())
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
filterTimeSamples(const SdfLayerRefPtr &layer, const UT_IntervalD &range)
{
    UT_ASSERT(range.isValid());
    layer->Traverse(SdfPath::AbsoluteRootPath(),
        [&layer, range](const SdfPath &path)
        {
            if (path.IsPrimPropertyPath())
            {
                SdfPropertySpecHandle propspec = layer->GetPropertyAtPath(path);
                if (!propspec)
                    return;
                
                // Find the range of time samples that encompasses the passed
                // interval, padded on either side by one extra sample.
                SdfTimeSampleMap timesamples;
                if (propspec->HasField(SdfFieldKeys->TimeSamples, &timesamples)) 
                {
                    auto iterl = timesamples.lower_bound(range.min);
                    if (iterl != timesamples.begin())
                        --iterl;
                    auto iteru = timesamples.upper_bound(range.max);
                    if (iteru != timesamples.end())
                        ++iteru;
                    
                    // Update the value in the layer if we've identified a subset
                    if (iterl != timesamples.begin() || iteru != timesamples.end())
                        propspec->SetInfo(SdfFieldKeys->TimeSamples,
                                        VtValue(SdfTimeSampleMap(iterl, iteru)));
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

        HUSDmakeValidDefaultPrim(fixed_defaultprim, true);
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
        const husd_SaveTimeData &timedata,
        const UsdStageWeakPtr &stage)
{
    // Set time code range, FPS, and TCPS values. Top priority goes to
    // explicit values set in the timedata structure. If no explicit value
    // is provided, check if the value on the layer is different from the
    // layer on the stage. In this case, we want to update the layer's
    // root metadata to match the values from the stage so that when we
    // save this layer and load it back in with UsdStage::Open, the
    // resulting stage will have the same root metadata as the composed
    // stage. We only force this value authoring if the stage and layer
    // values don't already match, which avoids writing explicit 24 FPS/TCPS
    // values all the time.
    if (timedata.myStartFrame > -SYS_FP64_MAX)
        layer->SetStartTimeCode(timedata.myStartFrame);
    else if (stage && stage->HasAuthoredMetadata(SdfFieldKeys->StartTimeCode))
        layer->SetStartTimeCode(stage->GetStartTimeCode());

    if (timedata.myEndFrame < SYS_FP64_MAX)
        layer->SetEndTimeCode(timedata.myEndFrame);
    else if (stage && stage->HasAuthoredMetadata(SdfFieldKeys->EndTimeCode))
        layer->SetEndTimeCode(stage->GetEndTimeCode());

    if (timedata.myTimeCodesPerSecond < SYS_FP64_MAX)
        layer->SetTimeCodesPerSecond(timedata.myTimeCodesPerSecond);
    else if (stage &&
             stage->GetTimeCodesPerSecond() != layer->GetTimeCodesPerSecond())
        layer->SetTimeCodesPerSecond(stage->GetTimeCodesPerSecond());

    if (timedata.myFramesPerSecond < SYS_FP64_MAX)
        layer->SetFramesPerSecond(timedata.myFramesPerSecond);
    else if (stage &&
             stage->GetFramesPerSecond() != layer->GetFramesPerSecond())
        layer->SetFramesPerSecond(stage->GetFramesPerSecond());
}

bool
saveLayer(SdfLayerRefPtr layer,
        const UT_StringRef &fullfilepath,
        const UT_PathPattern *save_files_pattern,
        const HUSD_OutputProcessorAndOverridesArray &output_processors,
        bool mute_before_save,
        bool &actually_saved_file,
        UT_String &error)
{
    SdfLayer::FileFormatArguments args;
    std::string splitfilepath;

    SdfLayer::SplitIdentifier(
        fullfilepath.toStdString(), &splitfilepath, &args);

    HUSD_ErrorScope blockerrors(HUSD_ErrorScope::CopyExistingScope);

    // We want to treat errors as errors, and ignore everything else.
    blockerrors.setErrorSeverityMapping(UT_ERROR_MESSAGE, UT_ERROR_NONE);
    blockerrors.setErrorSeverityMapping(UT_ERROR_WARNING, UT_ERROR_NONE);
    blockerrors.setErrorSeverityMapping(UT_ERROR_ABORT, UT_ERROR_ABORT);
    blockerrors.setErrorSeverityMapping(UT_ERROR_FATAL, UT_ERROR_ABORT);

    // Check if this file we are about to save should actually be
    // saved, based on the output processor shouldSave method and
    // the save_files_pattern set on the ROP. If we don't write out the
    // file, this is still a "success".
    bool success = true;
    if (shouldSaveFile(output_processors,
            save_files_pattern,
            fullfilepath,
            layer->GetIdentifier().c_str(),
            error))
    {
        // Allow the output processors to modify the layer just before it is
        // saved to disk.
        for (auto &&processor : output_processors)
        {
            if (processor.myProcessor)
            {
                processor.myProcessor->processLayer(
                    layer->GetIdentifier(), fullfilepath, error);
                if (error.isstring())
                    return false;
            }
        }

        // We may want to compare this layer to existing layer on disk
        // (if there is one). If nothing has changed, no need to save.
        // This can prevent creating a new version of this file.
        if (HUSDgetDoLayerDiffCallback() &&
            HUSDgetDoLayerDiffCallback()(splitfilepath) &&
            HUSDareLayersEqual(splitfilepath, layer->GetIdentifier()))
            return true;

        SdfLayerRefPtr oldlayer;
        if (mute_before_save)
            oldlayer = SdfLayer::Find(splitfilepath, args);
        bool muteoldlayer = oldlayer && !oldlayer->IsMuted();

        if (muteoldlayer)
            oldlayer->SetMuted(true);
        success = layer->Export(splitfilepath, std::string(), args);
        if (!success)
            HUSD_ErrorScope::addError(
                HUSD_ERR_LAYER_SAVE_FAILED,
                fullfilepath.c_str());
        else
            actually_saved_file = true;
        if (muteoldlayer)
            oldlayer->SetMuted(false);
    }

    return success;
}

bool
saveStageLayersNodeData(const UsdStageWeakPtr &stage,
        const UT_StringRef &filepath,
        bool filepath_is_time_dependent,
        const UT_PathPattern *save_files_pattern,
        HUSD_SaveStyle save_style,
        const husd_SaveProcessorData &processordata,
        const husd_SaveDefaultPrimData &defaultprimdata,
        const husd_SaveTimeData &timedata,
        const husd_SaveConfigFlags &flags,
        UT_StringMap<XUSD_SavePathInfo> &saved_path_info_map,
        std::map<std::string, std::string> &saved_geo_map,
        husd_NodeDataSaveMap &node_data_save_map,
        husd_VolumeSaveMap &volume_save_map)
{
    auto should_exit_with_error_fn = [](const UT_String &error) {
        if (error.isstring())
        {
            HUSD_ErrorScope::addError(
                HUSD_ERR_OUTPUT_PROCESSOR_ERROR, error.c_str());
            return true;
        }

        return false;
    };
    auto pre_save_layer_fn = [&](const SdfLayerRefPtr &layer,
        const UT_StringHolder &fullfilepath,
        std::map<std::string, std::string> &replace_map,
        UT_String &error)
    {
        saveNodeData(layer,
            processordata.myProcessors,
            fullfilepath,
            save_files_pattern,
            saved_geo_map,
            replace_map,
            volume_save_map,
            node_data_save_map,
            error);
        if (should_exit_with_error_fn(error))
            return false;
        HUSDmodifyAssetPaths(layer,
            husd_UpdateReferencesWithOutputProcessors(
                processordata.myProcessors,
                fullfilepath,
                replace_map,
                error));
        if (should_exit_with_error_fn(error))
            return false;

        if (flags.myClearHoudiniCustomData)
            clearHoudiniCustomData(layer);
        if (flags.myEnsureMetricsSet)
            ensureMetricsSet(layer, stage);
        if (flags.myTimeSamplesRange.isValid())
            filterTimeSamples(layer,
                { flags.myTimeSamplesRange.min -
                    flags.myTimeSamplesRangePadding,
                  flags.myTimeSamplesRange.max +
                    flags.myTimeSamplesRangePadding });

        return true;
    };
    auto save_existing_layer_fn = [&](const SdfLayerRefPtr &layer,
        const UT_StringMap<XUSD_SavePathInfo>::iterator &saved_path_info_it,
        UT_String &error)
    {
        // We've been asked to save to this layer before. Load the
        // existing file, stitch the new data into it, and save it
        // out.
        SdfLayerRefPtr existinglayer;
        bool success = true;

        existinglayer = SdfLayer::FindOrOpen(
            saved_path_info_it->second.
                myFinalPathWithVersionSpecifier.toStdString());
        if (existinglayer)
        {
            if (!saved_path_info_it->second.myActuallySavedFile &&
                HUSDgetDoLayerDiffCallback() &&
                HUSDgetDoLayerDiffCallback()(existinglayer->GetIdentifier()) &&
                HUSDareLayersEqual(existinglayer->GetIdentifier(),
                    layer->GetIdentifier()))
            {
                // The layer wasn't saved the first time we were asked to.
                // And the layer in memory is still the same as the one on
                // disk, so once again we don't need to save it.
                success = true;
            }
            else
            {
                HUSDstitchLayers(existinglayer, layer);
                success = existinglayer->Save();
                if (success && !saved_path_info_it->second.myActuallySavedFile)
                {
                    // We are here because we previously didn't save the file
                    // because it wasn't different from the one on disk, but
                    // now it is. So we have saved it and now want to lock in
                    // the current version number for saving more frames.
                    if (HUSDgetPathWithVersionSpecifierCallback())
                    {
                        // Record the final path with an explicit version
                        // specifier in case we are going to close this
                        // file and write to it again.
                        saved_path_info_it->second.
                            myFinalPathWithVersionSpecifier =
                                HUSDgetPathWithVersionSpecifierCallback()(
                                    saved_path_info_it->second.myFinalPath);
                    }
                    saved_path_info_it->second.myActuallySavedFile = true;
                }
            }
        }
        else
        {
            // This is an odd situation that probably should never happen. It's
            // interaction with versioning systems is particularly unclear. So
            // trigger an assertion, but otherwise leave this code unchanged
            // from its original form (from before we gave any thought to
            // versioning resolvers). It might be better to just turn this
            // code path into an error situation.
            UT_ASSERT(!"Couldn't read a file we just wrote to disk.");
            success = saveLayer(layer,
                saved_path_info_it->second.
                    myFinalPathWithVersionSpecifier.toStdString(),
                save_files_pattern,
                processordata.myProcessors,
                flags.myMuteLayersBeforeSave,
                saved_path_info_it->second.myActuallySavedFile,
                error);
            if (should_exit_with_error_fn(error))
                success = false;
        }

        return success;
    };
    auto save_new_layer_fn = [&](const SdfLayerRefPtr &layer,
        const XUSD_SavePathInfo &save_path_info,
        UT_String &error)
    {
        // This is the first time this save operation has seen this
        // file. Overwrite any existing file with the layer
        // contents.
        bool actually_saved_file = false;
        bool success = true;
        success = saveLayer(layer, save_path_info.myFinalPath,
            save_files_pattern,
            processordata.myProcessors,
            flags.myMuteLayersBeforeSave,
            actually_saved_file,
            error);
        if (should_exit_with_error_fn(error))
            return false;
        auto saved_path_info_it = saved_path_info_map.emplace(
            save_path_info.myFinalPath, save_path_info).first;
        saved_path_info_it->second.myActuallySavedFile =
            actually_saved_file;
        // Record the final path with an explicit version
        // specifier in case we are going to close this
        // file and write to it again.
        if (HUSDgetPathWithVersionSpecifierCallback() &&
            actually_saved_file)
            saved_path_info_it->second.
                myFinalPathWithVersionSpecifier =
                    HUSDgetPathWithVersionSpecifierCallback()(
                        saved_path_info_it->second.myFinalPath);

        return success;
    };
    UT_String error;

    if (save_style == HUSD_SAVE_FLATTENED_STAGE)
    {
        UT_StringHolder			     fullfilepath;
        std::map<std::string, std::string>   replace_map;
        auto				     layer = stage->Flatten();

        configureTimeData(layer, timedata, UsdStageWeakPtr());
        configureDefaultPrim(layer, defaultprimdata);

        // Let asset processors change the path where the file will be saved.
        fullfilepath = runOutputProcessors(processordata.myProcessors,
            filepath.toStdString(), UT_StringRef(), true, true, error);
        if (should_exit_with_error_fn(error))
            return false;
        // Make sure the save path is an absolute path.
        if (!UTisAbsolutePath(fullfilepath))
            UTmakeAbsoluteFilePath(fullfilepath);

        if (!pre_save_layer_fn(layer, fullfilepath, replace_map, error))
            return false;

        auto saved_path_info_it = saved_path_info_map.find(fullfilepath);
        if (saved_path_info_it != saved_path_info_map.end())
        {
            if (!save_existing_layer_fn(layer, saved_path_info_it, error))
                return false;
        }
        else
        {
            XUSD_SavePathInfo save_path_info(fullfilepath, filepath,
                XUSD_EXTERNAL_REF_OTHER, std::string(),
                false, filepath_is_time_dependent);
            if (!save_new_layer_fn(layer, save_path_info, error))
                return false;
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

        XUSD_IdentifierToReferenceInfoMap referenceinfomap;
        XUSD_IdentifierToSavePathMap      idtosavepathmap;
        std::string                       rootidentifier;

        rootidentifier = rootlayer->GetIdentifier();

        configureTimeData(rootlayer, timedata, stage);
        configureDefaultPrim(rootlayer, defaultprimdata);

        // Create mapping of layer identifiers to layer ref ptrs for all layers
        // on the stage, either as sublayers or references.
        referenceinfomap[rootidentifier] =
            { rootlayer, XUSD_EXTERNAL_REF_OTHER, SdfLayerRefPtr() };
        HUSDaddExternalReferencesToLayerMap(rootlayer, referenceinfomap, true);

        // Create mapping of layer identifiers to the paths on disk where the
        // layer is going to be saved for all layers in our map.
        // NOTE: We need to do this in two passes, specifically we need to
        //       defer running the output processors until *after* we've
        //       collected all the pre-processed output paths.
        //       This is because the output processors can mangle the output
        //       names to ensure they remain unique. For example, if we have two
        //       layers a/out.usd and b/out.usd and use an output processor to
        //       put them in the same directory, we'll likely end up with
        //       out.usd and out1.usd being generated. If we're generating
        //       multiple frames of data into a single USD file, it's critical
        //       that we get a consistent mapping between the original paths and
        //       the final paths, and that requires running the processors on
        //       the layers in the same order, and the best/only way we have of
        //       ordering them is based off of their original save path.
        for (auto &&data : referenceinfomap)
        {
            auto identifier = data.first;
            auto layer = data.second.myLayer;
            UT_StringHolder orig_path;
            bool using_node_path = false;
            bool time_dependent = false;

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

            // When we hit the strongest sublayer, record the SdfLayerRefPtr
            // for it for use later. This is only tracked when keeping separate
            // layers so we can copy metadata from the strongest sublayer onto
            // the root layer.
            if (identifier == first_sublayer_identifier)
                first_sublayer = layer;

            // Get the identifier of the layer that referenced this layer.
            std::string referencing_identifier =
                data.second.myReferenceLayer
                    ? data.second.myReferenceLayer->GetIdentifier()
                    : std::string();
            // As per the long comment above, we'll take note of the data we've
            // collected so far, and will run the output processors in a second
            // pass.
            idtosavepathmap[identifier] = XUSD_SavePathInfo(
                orig_path, orig_path, data.second.myReferenceType,
                referencing_identifier, using_node_path, time_dependent);
        }

        // Now we need to produce an ordering of the XUSD_SavePathInfo entries
        UT_Array<std::string> ids(idtosavepathmap.size());
        for (auto &&id : idtosavepathmap.key_range())
            ids.emplace_back(id);
        auto get_depth_fn = [&idtosavepathmap](const std::string &id) {
            int depth = 0;
            std::string searchid = id;
            auto it = idtosavepathmap.find(searchid);
            while (it != idtosavepathmap.end())
            {
                depth++;
                searchid = it->second.myReferenceLayerId;
                it = idtosavepathmap.find(searchid);
            }
            return depth;
        };
        ids.sort([&](const std::string &lhs, const std::string &rhs) {
            // Do a depth-first traversal in terms of layers referenced
            // by other layers.
            int lhsdepth = get_depth_fn(lhs);
            int rhsdepth = get_depth_fn(rhs);
            if (lhsdepth != rhsdepth)
                return lhsdepth < rhsdepth;

            // Otherwise we base our order on the pre-processed path
            const UT_StringHolder &lpath = idtosavepathmap[lhs].myOriginalPath;
            const UT_StringHolder &rpath = idtosavepathmap[rhs].myOriginalPath;
            return lpath < rpath;
        });

        // And now we can run the output processors and update the map with the
        // final save paths.
        for (auto &&id : ids)
        {
            auto &savepathmap = idtosavepathmap[id];
            // Send this path to asset processors to get the final save path.
            savepathmap.myFinalPath = runOutputProcessors(
                processordata.myProcessors,
                savepathmap.myOriginalPath,
                idtosavepathmap[savepathmap.myReferenceLayerId].myFinalPath,
                true, true,
                error);
            if (should_exit_with_error_fn(error))
                return false;

            if (!UTisAbsolutePath(savepathmap.myFinalPath))
                UTmakeAbsoluteFilePath(savepathmap.myFinalPath);
        };

        // For all layers we want to save, make a copy of the layer. Then
        // update all paths from lop or internal paths to the locations
        // where those layers will be saved to disk. Also update full paths
        // to relative paths for files on disk. Finally save the updated
        // layer to its desired location on disk.
        for (const std::string &identifier : ids)
        {
            const XUSD_SavePathInfo &outpathinfo = idtosavepathmap[identifier];
            const UT_StringHolder   &outfinalpath = outpathinfo.myFinalPath;
            std::string              save_control;

            if (outfinalpath.length() > 0)
            {
                auto layer = referenceinfomap[identifier].myLayer;

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

                // Copy the layer. Use a tag of ".usdc" so that we will save
                // the layer in binary format if the user requests that we
                // save it to a ".usd" extension - see the USD function
                // UsdUsdFileFormat::WriteToFile, where it uses the current
                // file format of layer if the destination "real path" is
                // equal to the layer real path. This happens when the layer
                // is anonymous (as it always is) and the destination path
                // has no "real path", likely because the path is a URL that
                // is only understood by an asset resolver.
                auto  layercopy = HUSDcreateAnonymousLayer(
                    SdfLayerHandle(), ".usdc");
                layercopy->TransferContent(layer);

                UT_StringArray time_dependent_references;
                std::map<std::string, std::string> replace_map;
                auto refs = HUSDgetExternalReferences(layer);

                for (auto &&it : refs)
                {
                    // If the reference is an empty string, ignore it.
                    auto ref = it.first;
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
                            ref, outfinalpath, true, false, error);
                        if (should_exit_with_error_fn(error))
                            return false;
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
                            updateit->second.myFinalPath,
                            outfinalpath, true, false, error);
                        if (should_exit_with_error_fn(error))
                            return false;
                        // Warn if the referenced file path is time dependent,
                        // but the referencing file is not, as this is not
                        // supported in USD, except in the case of value clips.
                        if (!outpathinfo.myTimeDependent &&
                            updateit->second.myTimeDependent &&
                            updateit->second.myReferenceType !=
                                XUSD_EXTERNAL_REF_VALUE_CLIP)
                            time_dependent_references.append(
                                updateit->second.myFinalPath);
                    }

                    if (ref != newpath.c_str())
                        replace_map[ref] = newpath;
                }

                if (!pre_save_layer_fn(layer, outfinalpath, replace_map, error))
                    return false;

                auto saved_path_info_it = saved_path_info_map.find(outfinalpath);
                if (saved_path_info_it != saved_path_info_map.end())
                {
                    if (!save_existing_layer_fn(layer, saved_path_info_it, error))
                        return false;
                }
                else
                {
                    if (!save_new_layer_fn(layer, outpathinfo, error))
                        return false;
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
	std::map<std::string, std::string> &saved_geo_map,
        husd_NodeDataSaveMap &node_data_save_map)
{
    // In case any code does asset resolution during the save operation (which
    // at least the saveVolume code does, but output processors may as well),
    // Bind the resolver context from the stage we are saving out.
    ArResolverContextBinder context_binder(stage->GetPathResolverContext());
    husd_VolumeSaveMap volume_save_map;
    bool success = false;

    // If something goes wrong during beginSave, we still want to call all
    // the endSave methods on the output processors.
    success = beginSaveOutputProcessors(processordata.myProcessors,
        processordata.myConfigNode,
        processordata.myLopNode,
        processordata.myConfigTime,
        stage->GetRootLayer()->GetExpressionVariables());

    if (success)
        success = saveStageLayersNodeData(stage,
            filepath,
            filepath_is_time_dependent,
            save_files_pattern,
            save_style,
            processordata,
            defaultprimdata,
            timedata,
            flags,
            saved_path_info_map,
            saved_geo_map,
            node_data_save_map,
            volume_save_map);

    // Do the actual saving of the volumes now that we've collected all
    // the information about them.
    for (auto &&savemapit : volume_save_map)
    {
        UT_String error;

        if (!shouldSaveFile(processordata.myProcessors,
                save_files_pattern,
                savemapit.first,
                UT_StringHolder::theEmptyString,
                error))
        {
            // In case of an error, indicate the failure and exit this loop
            // so we stop saving volumes. But we need to ensure that the
            // "endSave" method is still called on all output processors.
            if (error.isstring())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_OUTPUT_PROCESSOR_ERROR, error.c_str());
                success = false;
                break;
            }
            continue;
        }

        savemapit.second.myDetailHandle.gdp()->save(savemapit.first, nullptr);
        saved_path_info_map.emplace(savemapit.first,
            XUSD_SavePathInfo(savemapit.first));
    }

    // Images and SOP geometry will already have been saved, but record them
    // in our list of saved paths.
    for (auto &&savemapit : node_data_save_map)
        saved_path_info_map.emplace(savemapit.first,
            XUSD_SavePathInfo(savemapit.first));

    // Always give the output processors a chance to run shutdown code.
    // But let them know what we saved, and what errors occurred.
    UT_StringArray saved_paths;
    for (auto &&it : saved_path_info_map)
        saved_paths.append(it.second.myFinalPath);
    UT_String error_messages;
    HUSD_ErrorScope::getErrorMessages(error_messages, UT_ERROR_ABORT);
    endSaveOutputProcessors(processordata.myProcessors,
        processordata.myConfigNode,
        processordata.myLopNode,
        processordata.myConfigTime,
        stage->GetRootLayer()->GetExpressionVariables(),
        saved_paths,
        error_messages);

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
        cache.Clear(paths);

        UT_AutoLock lockscope(HUSDgetLayerReloadLock());

        // Clear the whole cache of automatic ref prim paths, because the
        // layers we are saving may be used by any stage, and so may affect
        // the default/automatic default prim of any stage.
        HUSDclearBestRefPathCache();

        // Do the actual reloading of the layers.
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
                                        myHeldLayers.clear();
                                        myLockedGeos.clear();
                                        myReplacementLayers.clear();
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
                                        myNodeDataSaveMap.clear();
                                    }

    UsdStageRefPtr		        myStage;
    XUSD_LayerSet		        myHeldLayers;
    XUSD_LockedGeoSet		        myLockedGeos;
    XUSD_LayerSet		        myReplacementLayers;
    HUSD_LockedStageSet 	        myLockedStages;
    UT_StringMap<XUSD_SavePathInfo>     mySavedPathInfoMap;
    std::map<std::string, std::string>  mySavedGeoMap;
    husd_NodeDataSaveMap                   myNodeDataSaveMap;
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
        if (stripLayersAboveLayerBreaks())
        {
            std::set<std::string> strip_sublayer_identifiers =
                indata->getStageLayersToRemoveFromLayerBreak(
                    XUSD_Data::LayerPathFormat::SubLayerPath);
            success = HUSDaddStageTimeSample(indata->stage(),
                myPrivate->myStage,
                HUSDgetUsdTimeCode(timecode),
                &strip_sublayer_identifiers,
                myPrivate->myHeldLayers, false, true,
                trackPrimExistence() ? &myPrivate->myExistenceTracker : nullptr);
        }
        else
        {
            success = HUSDaddStageTimeSample(indata->stage(),
                myPrivate->myStage,
                HUSDgetUsdTimeCode(timecode), nullptr,
                myPrivate->myHeldLayers, false, true,
                trackPrimExistence() ? &myPrivate->myExistenceTracker : nullptr);
        }
	myPrivate->myLockedGeos.insert(indata->lockedGeos().begin(),
            indata->lockedGeos().end());
	myPrivate->myReplacementLayers.insert(indata->replacements().begin(),
            indata->replacements().end());
	myPrivate->myLockedStages.insert(indata->lockedStages().begin(),
            indata->lockedStages().end());
	myPrivate->myHeldLayers.insert(indata->heldLayers().begin(),
            indata->heldLayers().end());
    }

    return success;
}

bool
HUSD_Save::saveCombined(const UT_StringRef &filepath,
        bool filepath_is_time_dependent,
	UT_StringMap<UT_StringHolder> &saved_paths)
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
	    myPrivate->mySavedGeoMap,
            myPrivate->myNodeDataSaveMap);
    }
    for (auto it = myPrivate->mySavedPathInfoMap.begin();
              it != myPrivate->mySavedPathInfoMap.end(); ++it)
        saved_paths.emplace(it->second.myOriginalPath, it->second.myFinalPath);

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
	UT_StringMap<UT_StringHolder> &saved_paths)
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
