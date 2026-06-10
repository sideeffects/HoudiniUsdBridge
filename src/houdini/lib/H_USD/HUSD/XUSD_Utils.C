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

#include "XUSD_Utils.h"
#include "XUSD_Data.h"
#include "XUSD_DataLock.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_Format.h"
#include "XUSD_LockedGeoRegistry.h"
#include "XUSD_Tokens.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_LayerOffset.h"
#include "HUSD_LoadMasks.h"
#include "HUSD_PathSet.h"
#include "HUSD_Preferences.h"
#include "HUSD_TimeCode.h"
#include "XUSD_ExistenceTracker.h"
#include "UsdHoudini/houdiniEditableAPI.h"
#include "UsdHoudini/houdiniSelectableAPI.h"
#include <OP/OP_Node.h>
#include <OP/OP_Director.h>
#include <GA/GA_Types.h>
#include <CH/CH_Manager.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_Function.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONValueMap.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_OptionEntry.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_StdUtil.h>
#include <UT/UT_StringStream.h>
#include <UT/UT_VarEncode.h>
#include <FS/UT_DSO.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Gf.h>
#include <pxr/pxr.h>
#include <pxr/usd/pcp/composeSite.h>
#include <pxr/usd/usdRender/pass.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdUtils/dependencies.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <pxr/usd/usdUtils/stitch.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usd/clipsAPI.h>
#include <pxr/usd/usd/flattenUtils.h>
#include <pxr/usd/usd/schemaBase.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/variableExpression.h>
#include <pxr/usd/sdf/variantSpec.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/base/vt/value.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/warning.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/tf/type.h>
#include <pxr/base/gf/numericCast.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/size2.h>
#include <pxr/base/gf/size3.h>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

// utility functions, not to be exposed as public facing API
namespace {

UT_StringMap<SdfPath>    theKnownDefaultPrims;
UT_StringMap<SdfPath>    theKnownAutomaticPrims;
const std::string        theLopTagPrefix = "LOP";
const std::string        theLopStageRootLayerIdentifier = "LOP:rootlayer";

TF_MAKE_STATIC_DATA(TfType, theSchemaBaseType) {
    *theSchemaBaseType = TfType::Find<UsdSchemaBase>();
    TF_VERIFY(!theSchemaBaseType->IsUnknown());
}

static void
_ApplyLoadMasksToStage(const UsdStageRefPtr &stage,
    const HUSD_LoadMasks *load_masks)
{
    if (stage && load_masks && !load_masks->muteLayers().empty())
    {
        std::vector<std::string>	 mutelayers;

        // We don't need to worry about updating layer muting for anonymous
        // LOP layers because we only get here creating a stage from a layer
        // on disk, which guarantees no inclusion of any LOP layers.
        for (auto &&identifier : load_masks->muteLayers())
            mutelayers.push_back(identifier.toStdString());
        stage->MuteAndUnmuteLayers(
            mutelayers, std::vector<std::string>());
    }
    if (stage && load_masks && !load_masks->loadAll())
    {
        UsdStageLoadRules        loadrules(UsdStageLoadRules::LoadNone());

        for (auto &&path : load_masks->loadPaths())
            loadrules.LoadWithDescendants(HUSDgetSdfPath(path));

        stage->SetLoadRules(loadrules);
    }
}

class husd_UpdateReferencesFromMap
{
public:
    husd_UpdateReferencesFromMap(
            const std::map<std::string, std::string> &pathmap)
        : myPathMap(pathmap)
    { }
    ~husd_UpdateReferencesFromMap()
    { }

    std::string operator()(const std::string &assetPath)
    {
        auto it = myPathMap.find(assetPath);

        if (it != myPathMap.end())
            return it->second;

        return assetPath;
    }

private:
    const std::map<std::string, std::string> &myPathMap;
};

class husd_UpdateReferencesToFullPaths
{
public:
    husd_UpdateReferencesToFullPaths(
            const SdfLayerRefPtr &sourceLayer)
        : mySourceLayer(sourceLayer)
    { }
    ~husd_UpdateReferencesToFullPaths()
    { }

    std::string operator()(const std::string &assetPath)
    {
        // ComputeAbsolutePath may return an empty string if it doesn't
        // know what to do with a path (such as an op: path pointing to
        // a SOP).
        std::string newpath = mySourceLayer->ComputeAbsolutePath(assetPath);

        if (!newpath.empty())
            return newpath;

        return assetPath;
    }

private:
    const SdfLayerRefPtr &mySourceLayer;
};

class xusd_FindVisibleLightPrim final : public XUSD_FindPrimsTaskData
{
public:
    xusd_FindVisibleLightPrim(const HUSD_TimeCode &tc)
        : XUSD_FindPrimsTaskData(),
          myTimeCode(HUSDgetUsdTimeCode(tc))
    { }

    void addToThreadData(const UsdPrim &prim, bool *prune) override
    {
        UsdLuxLightAPI      lux(prim);
        if (lux)
        {
            UsdGeomImageable imageable(prim);

            // If the light somehow is not an imageable prim, or if the
            // light is visible, then we have found a light. Invisible
            // lights are not counted.
            if (!imageable ||
                imageable.ComputeVisibility(myTimeCode) !=
                    UsdGeomTokens->invisible)
                myFound = true;
        }
        if (prune)
            *prune = myFound;
    }
    bool foundAnyVisibleLight() const
    { return myFound; }

private:
    UsdTimeCode      myTimeCode;
    bool             myFound = false;
};

void
_MergeClipSet(VtDictionary *strong, const VtDictionary &weak)
{
    VtArray<SdfAssetPath> weakPaths;
    VtVec2dArray weakActive;
    VtVec2dArray weakTimes;
    bool clip_data_handled = false;

    if (VtDictionaryIsHolding<VtArray<SdfAssetPath>>(weak,
            UsdClipsAPIInfoKeys->assetPaths))
        weakPaths = VtDictionaryGet<VtArray<SdfAssetPath>>(weak,
            UsdClipsAPIInfoKeys->assetPaths);
    if (VtDictionaryIsHolding<VtVec2dArray>(weak,
            UsdClipsAPIInfoKeys->active))
        weakActive = VtDictionaryGet<VtVec2dArray>(weak,
            UsdClipsAPIInfoKeys->active);
    if (VtDictionaryIsHolding<VtVec2dArray>(weak,
            UsdClipsAPIInfoKeys->times))
        weakTimes = VtDictionaryGet<VtVec2dArray>(weak,
            UsdClipsAPIInfoKeys->times);

    if (!weakPaths.empty() && !weakActive.empty() && !weakTimes.empty())
    {
        VtArray<SdfAssetPath> strongPaths;
        VtVec2dArray strongActive;
        VtVec2dArray strongTimes;

        if (VtDictionaryIsHolding<VtArray<SdfAssetPath>>(*strong,
                UsdClipsAPIInfoKeys->assetPaths))
            strongPaths = VtDictionaryGet<VtArray<SdfAssetPath>>(*strong,
                UsdClipsAPIInfoKeys->assetPaths);
        if (VtDictionaryIsHolding<VtVec2dArray>(*strong,
                UsdClipsAPIInfoKeys->active))
            strongActive = VtDictionaryGet<VtVec2dArray>(*strong,
                UsdClipsAPIInfoKeys->active);
        if (VtDictionaryIsHolding<VtVec2dArray>(*strong,
                UsdClipsAPIInfoKeys->times))
            strongTimes = VtDictionaryGet<VtVec2dArray>(*strong,
                UsdClipsAPIInfoKeys->times);

        UT_Set<double> strong_active_times;
        for (auto &&vec : strongActive)
            strong_active_times.insert(vec[0]);
        UT_Set<double> strong_times_start_times;
        for (auto &&vec : strongTimes)
            strong_times_start_times.insert(vec[0]);

        UT_StringMap<size_t> asset_paths;
        for (size_t i = 0; i < strongPaths.size(); ++i)
            asset_paths.emplace(strongPaths[i].GetAssetPath(), i);

        // Test for duplicate "active" declarations, and ignore weak ones that
        // match strong ones. Only add asset paths that are new, and that
        // correspond to weak "active" declarations that we actually keep.
        for (auto &&active : weakActive)
        {
            if (strong_active_times.contains(active[0]) ||
                (int)active[1] >= weakPaths.size())
                continue;
            auto asset_path = weakPaths[(int)active[1]];
            auto it = asset_paths.find(asset_path.GetAssetPath());
            if (it == asset_paths.end())
            {
                it = asset_paths.emplace(
                    asset_path.GetAssetPath(), strongPaths.size()).first;
                strongPaths.push_back(asset_path);
            }
            strongActive.push_back(GfVec2d(active[0], (double)it->second));
        }

        // Don't add weak "times" entries that start at the same time as
        // existing strong "times" entries (avoids adding duplicates when the
        // clip data is the same in the weak and strong layers).
        for (auto &&time : weakTimes)
        {
            if (!strong_times_start_times.contains(time[0]))
                strongTimes.push_back(time);
        }

        // Set the combined data back into the strong dictionary.
        (*strong)[UsdClipsAPIInfoKeys->active] = strongActive;
        (*strong)[UsdClipsAPIInfoKeys->assetPaths] = strongPaths;
        (*strong)[UsdClipsAPIInfoKeys->times] = strongTimes;
        clip_data_handled = true;
    }

    for (auto it = TfMakeIterator(weak); it; ++it)
    {
        // If both dictionaries have values that are in turn dictionaries,
        // recurse:
        if (VtDictionaryIsHolding<VtDictionary>(*strong, it->first) &&
            VtDictionaryIsHolding<VtDictionary>(weak, it->first))
        {
            const VtDictionary &weakSubDict =
                VtDictionaryGet<VtDictionary>(weak, it->first);

            // Swap out the stored dictionary, mutate it, then swap it back in
            // place.  This avoids expensive copying.  There may still be a copy
            // if the VtValue storage is shared.
            VtDictionary::iterator i = strong->find(it->first);
            VtDictionary strongSubDict;
            i->second.Swap(strongSubDict);
            // Modify the extracted dict.
            VtDictionaryOverRecursive(&strongSubDict, weakSubDict);
            // Swap the modified dict back into place.
            i->second.Swap(strongSubDict);
        }
        else if (clip_data_handled &&
                 (it->first == UsdClipsAPIInfoKeys->assetPaths ||
                  it->first == UsdClipsAPIInfoKeys->active ||
                  it->first == UsdClipsAPIInfoKeys->times))
        {
            // These special clip dictionary values were handled above.
            continue;
        }
        else
        {
            // Insert will set strong with value from weak only if
            // strong does not already have a value for that key.
            strong->insert(*it);
        }
    }
}

void
_MergeClipDictionaries(VtDictionary *strong, const VtDictionary &weak)
{
    for (auto it = TfMakeIterator(weak); it; ++it)
    {
        // If both dictionaries have values that are in turn dictionaries,
        // these are overlapping clip sets that must be merged.
        if (VtDictionaryIsHolding<VtDictionary>(*strong, it->first) &&
            VtDictionaryIsHolding<VtDictionary>(weak, it->first))
        {
            const VtDictionary &weakSubDict =
                VtDictionaryGet<VtDictionary>(weak, it->first);

            // Swap out the stored dictionary, mutate it, then swap it back in
            // place.  This avoids expensive copying.  There may still be a copy
            // if the VtValue storage is shared.
            VtDictionary::iterator i = strong->find(it->first);
            VtDictionary strongSubDict;
            i->second.Swap(strongSubDict);
            // Modify the extracted dict.
            _MergeClipSet(&strongSubDict, weakSubDict);
            // Swap the modified dict back into place.
            i->second.Swap(strongSubDict);
        }
        else
        {
            // Insert will set strong with value from weak only if
            // strong does not already have a value for that key.
            strong->insert(*it);
        }
    }
}

UsdUtilsStitchValueStatus 
_StitchCallback(
    const TfToken& field, const SdfPath& path,
    const SdfLayerHandle& strongLayer, bool fieldInStrongLayer,
    const SdfLayerHandle& weakLayer, bool fieldInWeakLayer,
    VtValue* stitchedValue,
    HUSD_PathSet* varyingDefaultPaths)
{
    // If both strong and weak layers contain values for time samples or
    // custom data, we need to stitch together values sparsely. Otherwise,
    // we can just use default stitching behavior.
    if (fieldInStrongLayer && fieldInWeakLayer)
    {
	if (field == SdfFieldKeys->TimeSamples ||
	    field == SdfFieldKeys->CustomData)
	{
	    const VtValue strongDataId = strongLayer->GetFieldDictValueByKey(
		path, SdfFieldKeys->CustomData, HUSDgetDataIdToken());
	    const VtValue weakDataId = weakLayer->GetFieldDictValueByKey(
		path, SdfFieldKeys->CustomData, HUSDgetDataIdToken());

	    if (UT_EnvControl::getInt(ENV_HOUDINI_LOP_STITCH_DEDUPLICATE_SAMPLES))
	    {
		// If dataIds stored in customData are the same valid value,
		// don't stitch any values together.
		//
		// NOTE: As this only looks at the previous sample, it does
		//       not let us capture the end endpoint of a series of
		//       constant samples surrounded by other values.
		//       For example, if the incoming data is:
		//       1.0@t=1 0.0@t=2 0.0@t=3 0.0@t=4 0.0@t=5 1.0@t=6
		//       Then the resulting stitched time samples will be:
		//       1.0@t=1 0.0@t=2                         1.0@t=6
		//       Which, due to USD's linear interpolation of sampled
		//       data, will synthesise a value of 0.5@t=4
		//       Ideally we'd instead want to generate:
		//       1.0@t=1 0.0@t=2                 0.0@t=5 1.0@t=6
		if (!weakDataId.IsEmpty() &&
		    weakDataId != VtValue(GA_INVALID_DATAID) &&
		    strongDataId == weakDataId)
		    return UsdUtilsStitchValueStatus::NoStitchedValue;
	    }

	    // Otherwise, stitch time samples and custom data as normal,
	    // but merge in the data id from the weaker layer into the
	    // stronger layer.
	    if (field == SdfFieldKeys->CustomData && !weakDataId.IsEmpty())
	    {
		const VtDictionary strongCustomData = 
		    strongLayer->GetFieldAs<VtDictionary>(path, field);
		const VtDictionary weakCustomData = 
		    weakLayer->GetFieldAs<VtDictionary>(path, field);

		VtDictionary mergedCustomData = VtDictionaryOverRecursive(
		    strongCustomData, weakCustomData);
		mergedCustomData[HUSDgetDataIdToken()] = weakDataId;

		stitchedValue->Swap(mergedCustomData);
		return UsdUtilsStitchValueStatus::UseSuppliedValue;
	    }
	}
	else if (varyingDefaultPaths && field == SdfFieldKeys->Default)
	{
	    bool varyingDefaultValue = false;
	    VtValue strongValue = strongLayer->GetField(path, field);
	    VtValue weakValue = weakLayer->GetField(path, field);
	    // In some circumstances (details unclear/unknown),
	    // SdfAssetPath values can be holding a resolved path (or not).
	    // Since we ultimately only case about the asset path, ws
	    // explicitly only compare that component here.
	    if (strongValue.IsHolding<SdfAssetPath>() &&
		weakValue.IsHolding<SdfAssetPath>())
	    {
		if (strongValue.UncheckedGet<SdfAssetPath>().GetAssetPath() !=
		    weakValue.UncheckedGet<SdfAssetPath>().GetAssetPath())
		{
		    varyingDefaultValue = true;
		}
	    }
	    else if (strongValue != weakValue)
		varyingDefaultValue = true;
	    
	    if (varyingDefaultValue)
		varyingDefaultPaths->insert(path);
	}
        else if (field == UsdTokens->clips)
        {
            // When merging clips metadata, merge each clip set separately,
            // by combining the active, times, and assetPaths lists.
            VtDictionary strongValue, weakValue;

            if (strongLayer->HasField(path, field, &strongValue) &&
                weakLayer->HasField(path, field, &weakValue))
            {
                VtDictionary merged = strongValue;
                _MergeClipDictionaries(&merged, weakValue);
                *stitchedValue = merged;

                return UsdUtilsStitchValueStatus::UseSuppliedValue;
            }
        }
    }

    return UsdUtilsStitchValueStatus::UseDefaultValue;
}

template <class T> T
_FixInternalSubrootPaths(
    const T& ref,
    const SdfPath& srcPrefix,
    const SdfPath& dstPrefix,
    const SdfLayerHandle& srcLayer)
{
    // Only try to fix up internal non-root references,
    // or relative references to files on disk, which need
    // to be converted to use full paths.
    if (!ref.GetAssetPath().empty())
    {
        T fixedRef = ref;
        fixedRef.SetAssetPath(
            SdfComputeAssetPathRelativeToLayer(
                srcLayer, ref.GetAssetPath()));
        return fixedRef;
    }

    if (ref.GetPrimPath().IsEmpty() ||
        ref.GetPrimPath() == SdfPath::AbsoluteRootPath()) {
        return ref;
    }
    
    T fixedRef = ref;
    if (!ref.GetPrimPath().HasPrefix(
            srcPrefix.IsEmpty()
            ? SdfPath::AbsoluteRootPath()
            : srcPrefix))
    {
        HUSD_ErrorScope::addWarning(HUSD_ERR_UNABLE_TO_RELOCATE_REF,
                                    ref.GetPrimPath().GetText());
    }
    fixedRef.SetPrimPath(ref.GetPrimPath()
        .ReplacePrefix(
            srcPrefix.IsEmpty()
                ? SdfPath::AbsoluteRootPath()
                : srcPrefix,
            dstPrefix.IsEmpty()
                ? SdfPath::AbsoluteRootPath()
                : dstPrefix));

    return fixedRef;
}

bool
_ShouldCopyValue(
    const SdfPath& srcRootPath,
    const SdfPath& dstRootPath,
    const fpreal frameoffset,
    const fpreal frameratescale,
    SdfSpecType specType,
    const TfToken& field,
    const SdfLayerHandle& srcLayer,
    const SdfPath& srcPath,
    bool fieldInSrc,
    const SdfLayerHandle& dstLayer,
    const SdfPath& dstPath,
    bool fieldInDst,
    std::optional<VtValue>* valueToCopy)
{
    if (fieldInSrc) {
        if (field == SdfFieldKeys->ConnectionPaths || 
            field == SdfFieldKeys->TargetPaths ||
            field == SdfFieldKeys->InheritPaths ||
            field == SdfFieldKeys->Specializes) {
	    SdfPathListOp srcListOp;
            if (srcLayer->HasField(srcPath, field, &srcListOp)) {
                const SdfPath& srcPrefix = 
                    srcRootPath.GetPrimPath().StripAllVariantSelections();
                const SdfPath& dstPrefix = 
                    dstRootPath.GetPrimPath().StripAllVariantSelections();

                srcListOp.ModifyOperations(
                    [&srcPrefix, &dstPrefix](const SdfPath& path) {
			return path.ReplacePrefix(
			    srcPrefix.IsEmpty()
				? SdfPath::AbsoluteRootPath()
				: srcPrefix,
			    dstPrefix.IsEmpty()
				? SdfPath::AbsoluteRootPath()
				: dstPrefix);
                    });

                *valueToCopy = VtValue::Take(srcListOp);
            }
        }
        else if (field == SdfFieldKeys->References) {
	    SdfReferenceListOp refListOp;
            if (srcLayer->HasField(srcPath, field, &refListOp)) {
                const SdfPath& srcPrefix = 
                    srcRootPath.GetPrimPath().StripAllVariantSelections();
                const SdfPath& dstPrefix = 
                    dstRootPath.GetPrimPath().StripAllVariantSelections();

                refListOp.ModifyOperations(
                    std::bind(&_FixInternalSubrootPaths<SdfReference>,
                        std::placeholders::_1, std::cref(srcPrefix),
                        std::cref(dstPrefix), std::cref(srcLayer)));

                *valueToCopy = VtValue::Take(refListOp);
            }
        }
        else if (field == SdfFieldKeys->Payload) {
	    SdfPayloadListOp payloadListOp;
            if (srcLayer->HasField(srcPath, field, &payloadListOp)) {
                const SdfPath& srcPrefix = 
                    srcRootPath.GetPrimPath().StripAllVariantSelections();
                const SdfPath& dstPrefix = 
                    dstRootPath.GetPrimPath().StripAllVariantSelections();

                payloadListOp.ModifyOperations(
                    std::bind(&_FixInternalSubrootPaths<SdfPayload>,
                        std::placeholders::_1, std::cref(srcPrefix),
                        std::cref(dstPrefix), std::cref(srcLayer)));

                *valueToCopy = VtValue::Take(payloadListOp);
            }
        }
        else if (field == SdfFieldKeys->Relocates) {
	    SdfRelocatesMap relocates;
            if (srcLayer->HasField(srcPath, field, &relocates)) {
                const SdfPath& srcPrefix = 
                    srcRootPath.GetPrimPath().StripAllVariantSelections();
                const SdfPath& dstPrefix = 
                    dstRootPath.GetPrimPath().StripAllVariantSelections();

		SdfRelocatesMap updatedRelocates;
                for (const auto& entry : relocates) {
                    const SdfPath updatedSrcPath = 
                        entry.first.ReplacePrefix(
				srcPrefix.IsEmpty()
				    ? SdfPath::AbsoluteRootPath()
				    : srcPrefix,
				dstPrefix.IsEmpty()
				    ? SdfPath::AbsoluteRootPath()
				    : dstPrefix);
                    const SdfPath updatedTargetPath = 
                        entry.second.ReplacePrefix(
				srcPrefix.IsEmpty()
				    ? SdfPath::AbsoluteRootPath()
				    : srcPrefix,
				dstPrefix.IsEmpty()
				    ? SdfPath::AbsoluteRootPath()
				    : dstPrefix);
                    updatedRelocates[updatedSrcPath] = updatedTargetPath;
                }

                *valueToCopy = VtValue::Take(updatedRelocates);
            }
        }
	else if (field == SdfFieldKeys->CustomLayerData ||
                 field == SdfFieldKeys->TimeCodesPerSecond ||
                 field == SdfFieldKeys->FramesPerSecond ||
                 field == SdfFieldKeys->StartTimeCode ||
                 field == SdfFieldKeys->EndTimeCode ||
                 field == SdfFieldKeys->Comment ||
                 field == SdfFieldKeys->DefaultPrim ||
                 field == UsdGeomTokens->metersPerUnit ||
                 field == UsdGeomTokens->upAxis)
	{
	    // Only allow copying custom layer data onto the root prim of
	    // the destination. It's not valid metadata on any other prim.
	    return (dstPath.GetPrimPath() == SdfPath::AbsoluteRootPath());
	}
	else if (field == SdfFieldKeys->TimeSamples
	         && (frameoffset != 0 || frameratescale != 1))
	{
	    SdfTimeSampleMap samples;
	    for (const double time : srcLayer->ListTimeSamplesForPath(srcPath))
	    {
		VtValue srcSample;
		srcLayer->QueryTimeSample(srcPath, time, &srcSample);
		samples[time * frameratescale + frameoffset].Swap(srcSample);
	    }

	    if (!samples.empty())
	    {
		*valueToCopy = VtValue::Take(samples);
		return true;
	    }
	}
    }
    return true;
}

bool
_ShouldCopyChildren(
    const SdfPath& srcRootPath,
    const SdfPath& dstRootPath,
    const TfToken& childrenField,
    const SdfLayerHandle& srcLayer,
    const SdfPath& srcPath,
    bool fieldInSrc,
    const SdfLayerHandle& dstLayer,
    const SdfPath& dstPath,
    bool fieldInDst,
    std::optional<VtValue>* srcChildren, 
    std::optional<VtValue>* dstChildren)
{
    static TfToken	 theHoudiniLayerInfoName(
	HUSD_Constants::getHoudiniLayerInfoPrimName().toStdString());

    if (fieldInSrc) {
	if (srcPath == SdfPath::AbsoluteRootPath() &&
	    childrenField == SdfChildrenKeys->PrimChildren)
	{
	    // Don't use HUSDcopySpec to copy the HoudiniLayerInfo prim from
	    // one layer to another.
	    TfTokenVector children;
            if (srcLayer->HasField(srcPath, childrenField, &children)) {
		auto it = std::find(children.begin(), children.end(),
		    theHoudiniLayerInfoName);

		if (it != children.end())
		{
		    children.erase(it);
		    *srcChildren = VtValue(children);
		    *dstChildren = VtValue::Take(children);
		}
            }
	}
	else if (childrenField == SdfChildrenKeys->ConnectionChildren ||
		 childrenField == SdfChildrenKeys->RelationshipTargetChildren||
		 childrenField == SdfChildrenKeys->MapperChildren)
	{
	    SdfPathVector children;
            if (srcLayer->HasField(srcPath, childrenField, &children)) {
                *srcChildren = VtValue(children);

                const SdfPath& srcPrefix = 
                    srcRootPath.GetPrimPath().StripAllVariantSelections();
                const SdfPath& dstPrefix = 
                    dstRootPath.GetPrimPath().StripAllVariantSelections();

                for (SdfPath& child : children) {
                    child = child.ReplacePrefix(
			srcPrefix.IsEmpty()
			    ? SdfPath::AbsoluteRootPath()
			    : srcPrefix,
			dstPrefix.IsEmpty()
			    ? SdfPath::AbsoluteRootPath()
			    : dstPrefix);
                }

                *dstChildren = VtValue::Take(children);
            }
        }
    }

    return true;
}

std::string
_FlattenLayerStackResolveAssetPath(const UsdFlattenResolveAssetPathContext &ctx)
{
    // Used in calls to UsdFlattenLayerStack to help resolve layer paths
    // from that function into new paths. We are always flattening to in-memory
    // stages. For absolute and search paths, we want to leave asset paths
    // alone. For relative paths we want to make them absolute. If the asset
    // path is anonymous, we don't want to touch it, and if the source layer is
    // anonymous we don't need to touch it (because the assetPath can be
    // assumed to already be what it is supposed to be).
    //
    // Note that we don't use the "isLopLayer" function here, because we
    // really are interested in treating _all_ anonymous layers in a special
    // way here (because by definition they don't have files backing them
    // which can meaningfully have their paths manipulated).
    //
    // Also ignore all asset paths that use variable expressions. We don't
    // want to lose the expression, so we have to leave it totally untouched
    // and leave any special handling to output processors.
    if (!SdfVariableExpression::IsExpression(ctx.assetPath) &&
        !ctx.assetPath.empty() &&
	!SdfLayer::IsAnonymousLayerIdentifier(ctx.assetPath) &&
	!HUSDisLopLayer(ctx.sourceLayer))
        return ctx.sourceLayer->ComputeAbsolutePath(ctx.assetPath);

    return ctx.assetPath;
}

void
_GetLayersToFlatten(const UsdStageWeakPtr &stage,
	int flatten_flags,
	XUSD_LayerAtPathArray &layers)
{
    if (flatten_flags & HUSD_FLATTEN_FULL_STACK)
    {
        double stagetcps = stage->GetTimeCodesPerSecond();

	for (auto &&layer : stage->GetLayerStack(false))
        {
            UsdEditTarget edittarget = stage->GetEditTargetForLocalLayer(layer);
            SdfLayerOffset offset = edittarget.GetMapFunction().GetTimeOffset();
            double layertcps = layer->GetTimeCodesPerSecond();

            // If there is a difference between the layer and stage tcps values,
            // we want to eliminate this contribution from the edit target time
            // offset calculation. This portion of the time offset will be
            // preserved in the substage we use for layer flattening. If we
            // include it in the layer offset as well, this portion of the
            // time offset will be double applied (see bug 113246).
            if (layertcps != stagetcps)
                offset.SetScale(offset.GetScale() * layertcps / stagetcps);
	    layers.append(XUSD_LayerAtPath(layer,
                layer->GetIdentifier(), offset));
        }
    }
    else
    {
	SdfLayerHandle	 root_layer = stage->GetRootLayer();
	SdfSubLayerProxy sublayer_proxy = root_layer->GetSubLayerPaths();

	layers.append(XUSD_LayerAtPath(stage->GetRootLayer()));
        for (int i = 0, n = sublayer_proxy.size(); i < n; i++)
	{
            std::string          path = sublayer_proxy[i];
	    SdfLayerHandle	 layer = SdfLayer::Find(path);

	    layers.append(XUSD_LayerAtPath(layer, path,
                root_layer->GetSubLayerOffset(i)));
	    if (!layer)
		HUSD_ErrorScope::addWarning(
		    HUSD_ERR_CANT_FIND_LAYER, std::string(path).c_str());
	}
    }
}

SdfLayerRefPtr
_FlattenLayerPartitions(const UsdStageWeakPtr &stage,
	int flatten_flags,
	SdfLayerRefPtrVector &explicit_layers,
	std::map<std::string, std::string> &references_map)
{
    SdfLayerRefPtrVector	 layers_to_scan_for_references;
    XUSD_LayerAtPathArray	 all_layers;
    std::vector<std::string>	 explicit_paths;
    std::vector<SdfLayerOffset>	 explicit_offsets;
    std::vector<std::vector<std::string> > partitions;
    std::vector<std::vector<SdfLayerOffset> > partition_offsets;
    std::map<size_t, std::vector<std::string> > sublayers_map;
    std::map<size_t, SdfLayerOffsetVector> sublayer_offsets_map;
    bool			 flatten_file_layers;
    bool			 flatten_sop_layers;
    bool			 flatten_explicit_layers;
    bool			 flatten_full_stack;

    // Just in case we are passed a null stage, return a null layer instead
    // of inflicting the inevitable crash on the user. This can happen when
    // an invalid extension is specified on a layer save path (Bug 110485).
    if (!stage)
        return SdfLayerRefPtr();

    flatten_file_layers = (flatten_flags & HUSD_FLATTEN_FILE_LAYERS);
    flatten_sop_layers = (flatten_flags & HUSD_FLATTEN_SOP_LAYERS);
    flatten_explicit_layers = (flatten_flags & HUSD_FLATTEN_EXPLICIT_LAYERS);
    flatten_full_stack = (flatten_flags & HUSD_FLATTEN_FULL_STACK);
    _GetLayersToFlatten(stage, flatten_flags, all_layers);
    for (auto &&layer : all_layers)
    {
	// We don't want to directly flatten the root layer because it
	// sublayers all the other layers. But it may have other useful
	// information besides the sublayering. So make a new copy of the
	// root layer but without the sublayering, and incorporate that
	// layer into the partition. This happens when the root layer is
	// coming from an HUSD_LockedStage, which moves the strongest
	// sublayer contents into the root layer itself.
	if (layer.myLayer == stage->GetRootLayer())
	{
	    SdfLayerRefPtr	 root_copy_layer;
	    root_copy_layer = HUSDcreateAnonymousLayer();
	    root_copy_layer->TransferContent(stage->GetRootLayer());
	    root_copy_layer->SetSubLayerPaths(std::vector<std::string>());
	    explicit_layers.push_back(root_copy_layer);
	    layer = XUSD_LayerAtPath(root_copy_layer);
	}

	// Get the save control metadata for this layer.
	std::string	 save_control;
	HUSDgetSaveControl(layer.myLayer, save_control);

	// First time through we need a new partition.
	// File and SOP layers get their own partition depending on the
	// separation parameters passed to this function.
	// Layers with save paths are explicit, and so get a partition.
	bool		 is_sop_layer = HUSDisSopLayer(layer.myLayer);
	bool		 is_file_layer = false;

	if (!is_sop_layer &&
	    (!layer.isLopLayer() ||
	     HUSD_Constants::getSaveControlIsFileFromDisk() == save_control))
	    is_file_layer = true;

	// Just skip over placeholder layers as if they don't exist.
	if (HUSD_Constants::getSaveControlPlaceholder() == save_control)
	    continue;

	if (partitions.size() == 0 ||
	    (!flatten_file_layers && is_file_layer) ||
	    (!flatten_sop_layers && is_sop_layer) ||
	    (!flatten_explicit_layers && save_control ==
	     HUSD_Constants::getSaveControlExplicit().toStdString()))
        {
	    partitions.push_back(std::vector<std::string>());
	    partition_offsets.push_back(std::vector<SdfLayerOffset>());
        }

	// Special handling of nested sublayers if we are not flattening the
	// whole layer stack, but instead just one level of sublayers at a
	// time.
	if (!flatten_full_stack &&
	    layer.isLopLayer() &&
	    layer.myLayer->GetNumSubLayerPaths() > 0)
	{
	    // For anonymous layers, stash their sublayers and sublayer
	    // offsets, then clear the sublayers. We don't want to flatten
	    // these nested sublayers.
	    SdfSubLayerProxy sublayer_proxy = layer.myLayer->GetSubLayerPaths();
	    std::vector<std::string> &sublayers =
		sublayers_map[partitions.size()];
	    SdfLayerOffsetVector &sublayer_offsets =
		sublayer_offsets_map[partitions.size()];

	    sublayers.insert(sublayers.begin(),
		sublayer_proxy.begin(), sublayer_proxy.end());
	    sublayer_offsets = layer.myLayer->GetSubLayerOffsets();

	    // Create a copy of the layer with all the same content except
	    // with no sublayers. This is the layer we will flatten with the
	    // other layers in this partition. Then we will add the sublayers
	    // onto the flattened partition.
	    SdfLayerRefPtr	 copy_layer;
	    copy_layer = HUSDcreateAnonymousLayer();
	    copy_layer->TransferContent(layer.myLayer);
	    copy_layer->SetSubLayerPaths(std::vector<std::string>());
	    explicit_layers.push_back(copy_layer);
	    layer = XUSD_LayerAtPath(copy_layer);
	}

	std::vector<std::string> &partition=partitions.back();
	std::vector<SdfLayerOffset> &partition_offset=partition_offsets.back();

	partition.push_back(layer.myIdentifier);
        partition_offset.push_back(layer.myOffset);

	// If we are putting files or sops in their own partitions, we need
	// to skip to the next partition regardless of what the next layer
	// indicates. If we create another partition above during the next
	// iteration, that's okay. Empty partitions are ignored below.
	//
	// If we have created a sublayer map entry for this partition, we must
	// also move on to another partition. In order to ensure an exact
	// match to the composed stage, each partition must have at most one
	// set of sublayers, and those sublayers must be on the weakest layer
	// of the partition.
	if ((!flatten_file_layers && is_file_layer) ||
	    (!flatten_sop_layers && is_sop_layer) ||
	    (!flatten_full_stack && sublayers_map.count(partitions.size()) > 0))
        {
	    partitions.push_back(std::vector<std::string>());
	    partition_offsets.push_back(std::vector<SdfLayerOffset>());
        }
    }

    SdfLayerRefPtr		 new_layer;
    bool			 first_partition = true;
    for (int i = 0, n = partitions.size(); i < n; i++)
    {
	std::vector<std::string> &partition = partitions[i];
	std::vector<SdfLayerOffset> &partition_offset = partition_offsets[i];

	// Ignore empty partitions. These may happen as a result of the
	// way the partitions are created in the loop above.
	if (partition.size() == 0)
	    continue;

	if (partition.size() == 1 &&
	    !HUSDisLopLayer(partition.front()))
	{
	    // A single SOP or file layer in a partition should just be added
	    // directly to the explicit paths. If this layer is the strongest
	    // layer, create an empty layer to hold all the explicit sublayers.
	    if (first_partition)
	    {
		new_layer = HUSDcreateAnonymousLayer(stage->GetRootLayer());
		layers_to_scan_for_references.push_back(new_layer);
		first_partition = false;
	    }
	    explicit_paths.push_back(partition.front());
            explicit_offsets.push_back(partition_offset.front());
	}
	else
	{
            // Flatten the layers in the partition.
	    SdfLayerRefPtr created_layer = HUSDflattenLayersInChunks(
                partition, partition_offset, stage);

	    if (first_partition)
	    {
		new_layer = created_layer;
		first_partition = false;
	    }
	    else
	    {
		explicit_layers.push_back(created_layer);
                explicit_offsets.push_back(SdfLayerOffset());
		explicit_paths.push_back(created_layer->GetIdentifier());
	    }
	    layers_to_scan_for_references.push_back(created_layer);

	    // Any sublayers from the layers in this partition are now added
	    // as sublayers to the flattened partition. These sublayers will
	    // be picked up for recursive flattening in the next section.
	    if (sublayers_map.count(i+1) > 0)
	    {
		const std::vector<std::string> &sublayers = sublayers_map[i+1];
		const SdfLayerOffsetVector &offsets = sublayer_offsets_map[i+1];

		created_layer->SetSubLayerPaths(sublayers);
		for (int si = 0, sn = sublayers.size(); si < sn; si++)
		    created_layer->SetSubLayerOffset(offsets[si], si);
	    }
	}
    }

    // Add any explicit sublayers (newly created or files from disk) to the
    // new root layer's sublayers. Don't simply set the sublayers because the
    // root layer may already have sublayers (added directly to this layer in
    // the LOP Network) which should be stronger than all the additional
    // layers created from the partitions.
    for (int i = 0, n = explicit_paths.size(); i < n; i++)
    {
        int newsublayerindex = new_layer->GetNumSubLayerPaths();

	new_layer->InsertSubLayerPath(explicit_paths[i]);
        new_layer->SetSubLayerOffset(explicit_offsets[i], newsublayerindex);
    }

    // Now that we've partitioned and flattened all the sublayers, look for
    // any other composition types (references or payloads) that point to
    // anonymous layers. These will have been set up by the other LOPs, and
    // we want to do the same partitioning of the sublayers that make up
    // each of these referenced layers.
    for (auto &&update_layer : layers_to_scan_for_references)
    {
        std::map<std::string, std::string> pathmap;
	auto refs = HUSDgetExternalReferences(update_layer);

	for (auto &&it : refs)
	{
	    // Only interested in references that are not sublayers, and that
	    // are anonymous layers.
            auto ref = it.first;
	    if (HUSDisLopLayer(ref))
	    {
		auto	it = std::find(explicit_paths.begin(),
				    explicit_paths.end(), ref);

		if (it == explicit_paths.end())
		{
		    auto	 refit = references_map.find(ref);

		    // We may have encountered a reference to this layer before,
		    // in which case we don't want to flatten it again. We
		    // just want to update the references to the identifier
		    // for the flattened equivalent to this reference.
		    if (refit == references_map.end())
		    {
			// Add an empty entry to the references map so we
			// can easily detect recursive references.
			references_map[ref] = std::string();

			SdfLayerRefPtr flatlayer =
			    _FlattenLayerPartitions(
				UsdStage::Open(ref),
				flatten_flags,
				explicit_layers,
				references_map);
			if (!flatlayer)
			{
			    // The only way a reference flattens to nothing is
			    // if the first referenced layer is marked as
			    // "RemoveFromSublayers". But this shouldn't
			    // happen because the layer should have been
			    // removed in the HUSD_Save processing.
                            //
                            // The other way this can happen is via Bug 110485.
                            // See the other comment at the top of this function
                            // relating to this bug id.
			    UT_ASSERT(!"Flattened reference to nothing.");
			    continue;
			}

			explicit_layers.push_back(flatlayer);
			explicit_paths.push_back(flatlayer->GetIdentifier());
			references_map[ref] = explicit_paths.back();
                        pathmap[ref] = explicit_paths.back();
		    }
		    else if (refit->second == std::string())
		    {
                        // This shouldn't happen. It either indicates that the
                        // user actually authored a reference loop using LOP
                        // nodes (which should be prevented by the node cook
                        // process), or the HUSDaddStageTimeSample and
                        // _StitchLayersRecursive methods transformed some
                        // references in a way that created a recursive
                        // reference loop.
			UT_ASSERT(!"Recursive reference found.");
		    }
		    else
                        pathmap[ref] = refit->second;
		}
	    }
	}
        HUSDupdateExternalReferences(update_layer, pathmap);
    }

    return new_layer;
}

void
_ClipReferencesRecursive(const SdfPrimSpecHandle &primspec,
        std::set<std::string> &refs)
{
    VtValue clipsValue = primspec->GetInfo(UsdTokens->clips);
    if (!clipsValue.IsEmpty() && clipsValue.IsHolding<VtDictionary>())
    {
        const VtDictionary clipsDict = clipsValue.UncheckedGet<VtDictionary>();

        for (auto &&clip : clipsDict)
        {
            if (!clip.second.IsHolding<VtDictionary>())
                continue;

            VtDictionary clipDict = clip.second.UncheckedGet<VtDictionary>();

            if (VtDictionaryIsHolding<VtArray<SdfAssetPath>>(clipDict,
                    UsdClipsAPIInfoKeys->assetPaths.GetString()))
            {
                const VtArray<SdfAssetPath> &assetPaths =
                    VtDictionaryGet<VtArray<SdfAssetPath>>(clipDict,
                        UsdClipsAPIInfoKeys->assetPaths.GetString());

                for (auto &&assetPath : assetPaths)
                    refs.insert(assetPath.GetAssetPath());
            }

            if (VtDictionaryIsHolding<SdfAssetPath>(clipDict,
                    UsdClipsAPIInfoKeys->manifestAssetPath.GetString()))
            {
                const SdfAssetPath &assetPath =
                    VtDictionaryGet<SdfAssetPath>(clipDict,
                        UsdClipsAPIInfoKeys->manifestAssetPath.GetString());

                refs.insert(assetPath.GetAssetPath());
            }
        }
    }

    // Recurse on named children.
    for (const SdfPrimSpecHandle &child : primspec->GetNameChildren()) {
        _ClipReferencesRecursive(child, refs);
    }
    // Recurse on variant sets and variants, which may contain clips.
    for (auto &&vsetit : primspec->GetVariantSets()) {
        const SdfVariantSetSpecHandle &vset = vsetit.second;
        for (const SdfVariantSpecHandle &v : vset->GetVariants()) {
            _ClipReferencesRecursive(v->GetPrimSpec(), refs);
        }
    }
}

std::set<std::string>
_ClipReferences(const SdfLayerRefPtr &layer)
{
    std::set<std::string>    refs;

    _ClipReferencesRecursive(layer->GetPseudoRoot(), refs);

    return refs;
}

void
_ClipFixActiveDataFromPathMapRecursive(const SdfPrimSpecHandle &primspec,
        const std::map<std::string, std::string> &deletingpathmap)
{
    VtValue clipsValue = primspec->GetInfo(UsdTokens->clips);
    if (!clipsValue.IsEmpty() && clipsValue.IsHolding<VtDictionary>())
    {
        const VtDictionary clipsDict = clipsValue.UncheckedGet<VtDictionary>();

        for (auto &&clip : clipsDict)
        {
            if (!clip.second.IsHolding<VtDictionary>())
                continue;

            VtDictionary clipDict = clip.second.UncheckedGet<VtDictionary>();

            if (VtDictionaryIsHolding<VtArray<SdfAssetPath>>(clipDict,
                    UsdClipsAPIInfoKeys->assetPaths.GetString()) &&
                VtDictionaryIsHolding<VtVec2dArray>(clipDict,
                    UsdClipsAPIInfoKeys->active.GetString()))
            {
                const VtArray<SdfAssetPath> &assetPaths =
                    VtDictionaryGet<VtArray<SdfAssetPath>>(clipDict,
                        UsdClipsAPIInfoKeys->assetPaths.GetString());
                const VtVec2dArray &actives =
                    VtDictionaryGet<VtVec2dArray>(clipDict,
                        UsdClipsAPIInfoKeys->active.GetString());

                // Go through the asset paths looking for entries in the
                // deletingpathmap. If the destination path is already in
                // the assetpaths array, we should update active entries to
                // point to these existing asset paths. If the deletingpathmap
                // entry is not in the assetpaths yet, we want to change the
                // path (rather than letting it get deleted later).

                // newassetpaths will contain the new value for assetPaths.
                // If no matches to the deletingpathmap are found, this array
                // will remain empty.
                VtArray<SdfAssetPath> newassetpaths;
                // idxmap will contain a map of old active index values to the
                // new values that should replace them.
                std::map<int, int> idxmap;
                // remapidx tracks the new active index to use when we end up
                // removing entries from the assetPaths array.
                int remapidx = 0;

                for (int idx = 0, n = assetPaths.size(); idx < n; idx++)
                {
                    const auto &assetPath = assetPaths[idx];
                    auto it = deletingpathmap.find(assetPath.GetAssetPath());
                    if (it != deletingpathmap.end())
                    {
                        if (newassetpaths.empty())
                            newassetpaths.assign(assetPaths.begin(),
                                assetPaths.begin() + idx);

                        auto assetit = std::find(assetPaths.begin(),
                            assetPaths.end(), SdfAssetPath(it->second));
                        if (assetit == assetPaths.end())
                        {
                            newassetpaths.push_back(SdfAssetPath(it->second));
                            remapidx++;
                            if (idx != remapidx)
                                idxmap.emplace(idx, remapidx);
                        }
                        else
                        {
                            // New asset paths are always appended, so there
                            // should be no way for us to have an existing
                            // asset path in the array with an index greater
                            // than idx (the entry that is going to be
                            // deleted). Note that we don't add an entry to
                            // the newassetpaths, and we don't bump remapidx.
                            // This marks a point where we are really removing
                            // an asset path, so from now on every active index
                            // will be changed.
                            UT_ASSERT(idx > assetit - assetPaths.begin());
                            idxmap.emplace(idx, assetit - assetPaths.begin());
                        }
                    }
                    else if (!newassetpaths.empty())
                    {
                        // We've already had some kind of change. We need to
                        // continue building newassetpaths, and track the
                        // remapidx (in case it no longer matches idx).
                        newassetpaths.push_back(assetPath);
                        remapidx++;
                        if (idx != remapidx)
                            idxmap.emplace(idx, remapidx);
                    }
                    else
                        remapidx = idx;
                }

                // If anything changed, update both the assetPaths and active
                // arrays with the new values.
                if (!newassetpaths.empty())
                {
                    clipDict[UsdClipsAPIInfoKeys->assetPaths] = newassetpaths;
                    if (!idxmap.empty())
                    {
                        VtVec2dArray newactives(actives);

                        for (int i = 0, n = newactives.size(); i < n; i++)
                        {
                            int oldidx = (int)newactives[i][1];
                            auto it = idxmap.find(oldidx);
                            if (it != idxmap.end())
                                newactives[i] =
                                    { newactives[i][0], (double)it->second };
                        }
                        clipDict[UsdClipsAPIInfoKeys->active] = newactives;
                    }
                    primspec->SetInfoDictionaryValue(UsdTokens->clips,
                        TfToken(clip.first), VtValue(clipDict));
                }
            }
        }
    }

    // Recurse on nameChildren
    for (const SdfPrimSpecHandle &child : primspec->GetNameChildren()) {
        _ClipFixActiveDataFromPathMapRecursive(child, deletingpathmap);
    }
}

void
_ClipFixActiveDataFromPathMap(const SdfLayerRefPtr &layer,
        const std::map<std::string, std::string> &deletingpathmap)
{
    // If there is no chance of us changing anything, there's no point in
    // traversing the layer's primspecs.
    if (!deletingpathmap.empty())
        _ClipFixActiveDataFromPathMapRecursive(
            layer->GetPseudoRoot(), deletingpathmap);
}

bool
_StitchLayersRecursive(const SdfLayerRefPtr &src,
	const SdfLayerRefPtr &dest,
        const std::set<std::string> *strip_sublayer_identifiers,
        XUSD_IdentifierToReferenceInfoMap &destreferenceinfomap,
	XUSD_IdentifierToSavePathMap &stitchedpathmap,
	std::set<std::string> &newdestlayers,
        std::map<std::string, SdfLayerRefPtr> &currentsamplesavelocations,
        bool save_locations_case_sensitive,
        bool force_notifiable_file_format,
        bool set_layer_override_save_paths,
        HUSD_PathSet *varying_default_paths)
{
    auto    should_ignore_layer_fn = [&](std::string layerpath)
            {
                return (strip_sublayer_identifiers &&
                        strip_sublayer_identifiers->find(layerpath) !=
                            strip_sublayer_identifiers->end()) ||
                       HUSDisLayerPlaceholder(layerpath);
            };
    bool    success = true;

    // Make sure we haven't already processed this layer, which we may have
    // done if the same layer is referenced from within several other layers.
    if (stitchedpathmap.find(src->GetIdentifier()) != stitchedpathmap.end())
	return success;

    XUSD_IdentifierToReferenceInfoMap srcreferenceinfomap;

    HUSDaddExternalReferencesToLayerMap(src, srcreferenceinfomap, false, true);

    // Stitch the source layer into the destination layer.
    HUSDstitchLayers(dest, src, varying_default_paths);
    // If we have been asked to strip out certain sublayers, do so now.
    // This will catch all the sublayers blindly copied over when an empty
    // dest layer is first populated from the source layer.
    if (strip_sublayer_identifiers)
    {
        auto sublayer_identifiers = dest->GetSubLayerPaths();
        for (int i = sublayer_identifiers.size(); i --> 0; )
        {
            if (strip_sublayer_identifiers->find(sublayer_identifiers[i]) !=
                strip_sublayer_identifiers->end())
                dest->RemoveSubLayerPath(i);
        }
    }
    stitchedpathmap.emplace(src->GetIdentifier(),
	XUSD_SavePathInfo(dest->GetIdentifier()));

    // Go through all externally referenced layers to find other layers that
    // we need to stitch together and save to disk.
    for (auto &&srcit : srcreferenceinfomap)
    {
	SdfLayerRefPtr	         srclayer = srcit.second.myLayer;
        XUSD_ExternalRefType     srcreftype = srcit.second.myReferenceType;

        // If we failed to find a layer, exit with an error. The actual error
        // message will have been added by HUSDaddExternalReferencesToLayerMap.
	if (!srclayer)
	{
	    success = false;
	    break;
	}

        // If we are handed a placeholder layer, or there is a src layer we
        // have been asked to strip out, create an entry in the path update
        // map to remove this identifier from the dest layer. We don't
        // want these layers showing up in our stitched result.
        if (should_ignore_layer_fn(srclayer->GetIdentifier()))
        {
            stitchedpathmap.emplace(srclayer->GetIdentifier(),
                XUSD_SavePathInfo());
            continue;
        }

        bool             srcsavelocationtimedep = false;
        std::string      srcsavelocation;
        std::string      savekey;
	SdfLayerRefPtr	 destlayer;

        // If we find an existing layer that we are already saving to the
        // desired location, but we've already requested a save to this
        // location from the current time sample, this indicates we have
        // multiple unique layers that we are being asked to save to the
        // same location. This is not okay. We want to warn the user, and
        // increment the file name until we find one that is unique among
        // the layers being saved within this time sample.
        srcsavelocation = HUSDgetLayerSaveLocation(srclayer);
        srcsavelocationtimedep = HUSDgetSavePathIsTimeDependent(srclayer);
        if (save_locations_case_sensitive)
            savekey = srcsavelocation;
        else
            savekey = UT_StringRef(srcsavelocation).toLower().toStdString();
        if (currentsamplesavelocations.count(savekey) > 0)
        {
            // If we are finding the same layer for the second time (it is
            // perhaps referenced in by two separate sublayers), just skip
            // this occurrence of it. It has already been stitched in with
            // the new time sample.
            if (currentsamplesavelocations[savekey] == srclayer)
                continue;

            std::string      testpath = srcsavelocation;
            UT_StringHolder  ext = UT_String(testpath).fileExtension();
            UT_WorkBuffer    errbuf;
            std::string      nodepath;
            std::string      testkey;
            UT_String        noext = UT_String(testpath).pathUpToExtension();

            // Make a unique save path for this layer.
            noext.append("_duplicate1");
            testpath = noext;
            testpath.append(ext);
            if (save_locations_case_sensitive)
                testkey = testpath;
            else
                testkey = UT_StringRef(testpath).toLower().toStdString();
            while (currentsamplesavelocations.count(testkey) > 0)
            {
                noext.incrementNumberedName();
                testpath = noext;
                testpath.append(ext);
                if (save_locations_case_sensitive)
                    testkey = testpath;
                else
                    testkey = UT_StringRef(testpath).toLower().toStdString();
            }

            if (HUSDgetCreatorNode(srclayer, nodepath))
                errbuf.sprintf("layer created by '%s' saving to '%s'.\n"
                    "Saving to '%s' instead.",
                    nodepath.c_str(), srcsavelocation.c_str(),
                    testpath.c_str());
            else
                errbuf.sprintf("'%s' saving to '%s' at frame %f.\n"
                    "Saving to '%s' instead.",
                    srclayer->GetIdentifier().c_str(),
                    srcsavelocation.c_str(),
                    CHgetSampleFromTime(CHgetEvalTime()),
                    testpath.c_str());
            HUSD_ErrorScope::addWarning(
                HUSD_ERR_LAYERS_SHARING_SAVE_PATH,
                errbuf.buffer());

            srcsavelocation = testpath;
            savekey = testkey;
        }
        currentsamplesavelocations.emplace(savekey, srclayer);

	for (auto &&destit : destreferenceinfomap)
	{
            if (!UT_StringRef(HUSDgetLayerSaveLocation(destit.second.myLayer)).
                    compare(srcsavelocation.c_str(),
                         !save_locations_case_sensitive))
	    {
		destlayer = destit.second.myLayer;
		break;
	    }
	}

	if (!destlayer)
	{
	    // A new layer to save. We must make a copy.
	    UT_ASSERT(HUSDshouldSaveLayerToDisk(srclayer));
            if (force_notifiable_file_format)
                destlayer = HUSDcreateAnonymousLayer();
            else
                destlayer = HUSDcreateAnonymousLayer(
                    SdfLayerHandle(), srcsavelocation);
            destreferenceinfomap[destlayer->GetIdentifier()] =
                { destlayer, srcreftype, SdfLayerRefPtr() };
	    newdestlayers.insert(destlayer->GetIdentifier());
	}
	else
	{
	    // Another time sample for an existing layer to save.
	    // Stitch it in recursively.
	    UT_ASSERT(HUSDshouldSaveLayerToDisk(destlayer));
	}

        // Use the recursive stitch code so that every layer to save in the
        // hierarchy is copied to a new layer that can safely be used to
        // combine multiple time samples.
        // Note that we always pass nullptr for strip_sublayer_identifiers,
        // because we only need to strip layers that are direct sublayers
        // of the root layer.
        _StitchLayersRecursive(srclayer, destlayer, nullptr,
            destreferenceinfomap, stitchedpathmap,
            newdestlayers, currentsamplesavelocations,
            save_locations_case_sensitive,
            force_notifiable_file_format,
            set_layer_override_save_paths,
            varying_default_paths);

        // After stitching, make sure the new layer is configured to save to
        // the source layer save location we determined above. Do this by
        // setting the override save path, which doesn't obscure the fact
        // that the path originally came from a node path (if it did).
        if (set_layer_override_save_paths)
            HUSDsetSavePath(destlayer, UT_StringRef(),
                srcsavelocationtimedep, srcsavelocation);
    }

    // Update references from src layer identifiers to dest layer identifiers.
    std::map<std::string, std::string> pathmap;
    std::map<std::string, std::string> deletingpathmap;

    for (auto &&srcit : srcreferenceinfomap)
    {
	auto	 mapit = stitchedpathmap.find(srcit.first);

	// If we created the destination layer during this stitch operation, we
	// want to update any references to the source into references to this
	// new dest layer. If the destination layer existed before the current
	// stitch, the assumption is that anywhere that referenced this source
	// layer on a previous stitch will already have the updated reference
	// as well as a new reference to the source layer added by the
	// stitching of the reference lists.  If we try to update this source
	// reference to the dest, USD will detect that the dest layer is
	// already referenced, and not do the update. Then we're stuck with the
	// one reference to the dest that we actually want, and a second
	// reference to the anonymous source. So we just want to delete all
	// references to this source layer.
	if (mapit != stitchedpathmap.end())
        {
            std::string fullpath = mapit->second.myFinalPath.toStdString();

            if (newdestlayers.find(fullpath) == newdestlayers.end())
            {
                pathmap[srcit.first] = std::string();
                deletingpathmap[srcit.first] = fullpath;
            }
            else
		pathmap[srcit.first] = fullpath;
	}
    }
    // Update clip "active" data for scenarios where we are about to remove
    // duplicate asset paths.
    _ClipFixActiveDataFromPathMap(dest, deletingpathmap);
    // Update the actual layer references (which may remove clip asset path
    // entries).
    HUSDupdateExternalReferences(dest, pathmap);

    // Add any sublayers from the source that are not on the dest.
    auto	 srcsubpaths = src->GetSubLayerPaths();
    auto	 destsubpaths = dest->GetSubLayerPaths();

    for (int i = 0, n = srcsubpaths.size(); i < n; i++)
    {
        std::string      srcsubpath = srcsubpaths[i];
        SdfLayerOffset   srcoffset = src->GetSubLayerOffset(i);

	// Don't stitch together anonymous placeholder layers or layers from
	// the source that we have been asked to strip out.
        if (should_ignore_layer_fn(srcsubpath))
            continue;

	auto		 destpathit = stitchedpathmap.find(srcsubpath);
	std::string	 destpath;
	bool		 foundsubpath = false;

	if (destpathit != stitchedpathmap.end())
	    destpath = destpathit->second.myFinalPath;
	else
	    destpath = srcsubpath;

	if (destpath.length() > 0)
	{
	    for (auto &&destsubpath : destsubpaths)
	    {
		if (destsubpath == destpath)
		{
		    foundsubpath = true;
		    break;
		}
	    }

	    if (!foundsubpath)
            {
                int newsublayerindex = dest->GetNumSubLayerPaths();

		dest->InsertSubLayerPath(destpath);
                dest->SetSubLayerOffset(srcoffset, newsublayerindex);
            }
	}
    }

    return success;
}

} // end anon namespace

bool
HUSDgetNodePath(int nodeid, UT_StringHolder &nodepath)
{
    OP_Node	*node = OP_Node::lookupNode(nodeid);

    if (node)
    {
	nodepath = node->getFullPath();

	return true;
    }

    return false;
}

std::string
HUSDgetTag(const XUSD_DataLockPtr &datalock)
{
    UT_StringHolder		 nodepath;

    if (datalock)
    {
        HUSDgetNodePath(datalock->getLockedNodeId(), nodepath);
        if (nodepath.findCharIndex('.') >= 0)
            nodepath.substitute(".", "_");
    }

    return nodepath.toStdString();
}

const TfToken &
HUSDgetDataIdToken()
{
    static const TfToken	 theToken("HoudiniDataId");

    return theToken;
}

const TfToken &
HUSDgetSavePathToken()
{
    static const TfToken	 theToken("HoudiniSavePath");

    return theToken;
}

const TfToken &
HUSDgetOverrideSavePathToken()
{
    static const TfToken	 theToken("HoudiniOverrideSavePath");

    return theToken;
}

const TfToken &
HUSDgetSavePathIsTimeDependentToken()
{
    static const TfToken	 theToken("HoudiniSavePathIsTimeDependent");

    return theToken;
}

const TfToken &
HUSDgetSaveControlToken()
{
    static const TfToken	 theToken("HoudiniSaveControl");

    return theToken;
}

const TfToken &
HUSDgetSourcePathToken()
{
    static const TfToken	 theToken("HoudiniSourcePath");

    return theToken;
}

const TfToken &
HUSDgetCreatorNodeToken()
{
    static const TfToken	 theToken("HoudiniCreatorNode");

    return theToken;
}

const TfToken &
HUSDgetEditorNodesToken()
{
    static const TfToken	 theToken("HoudiniEditorNodes");

    return theToken;
}

const TfToken &
HUSDgetSoloLightPathsToken()
{
    static const TfToken	 theToken("HoudiniSoloLightPaths");

    return theToken;
}

const TfToken &
HUSDgetSoloGeometryPathsToken()
{
    static const TfToken	 theToken("HoudiniSoloGeometryPaths");

    return theToken;
}

const TfToken &
HUSDgetTreatAsSopLayerToken()
{
    static const TfToken	 theToken("HoudiniTreatAsSopLayer");

    return theToken;
}

const TfToken &
HUSDgetVolumeFilePathsToken()
{
    static const TfToken	 theToken("HoudiniVolumeFilePaths");

    return theToken;
}

const TfToken &
HUSDgetMaterialIdToken()
{
    static const TfToken	 theToken("HoudiniMaterialId");

    return theToken;
}

const TfToken &
HUSDgetHasAutoPreviewShaderToken()
{
    static const TfToken	 theToken("HoudiniHasAutoPreviewShader");

    return theToken;
}

const TfToken &
HUSDgetIsAutoCreatedShaderToken()
{
    static const TfToken	 theToken("HoudiniIsAutoCreatedShader");

    return theToken;
}

const TfToken &
HUSDgetPreviewTagsToken()
{
    static const TfToken	 theToken("HoudiniPreviewTags");

    return theToken;
}

const TfToken &
HUSDgetPreviewDefaultValueKeyPathToken()
{
    static const TfToken	 theToken("HoudiniPreviewTags:default_value");

    return theToken;
}

const TfToken &
HUSDgetPrimEditorNodesToken()
{
    static const TfToken	 theToken("HoudiniPrimEditorNodes");

    return theToken;
}

const TfToken &
HUSDgetSourceNodeToken()
{
    static const TfToken	 theToken("HoudiniSourceNode");

    return theToken;
}

const TfToken &
HUSDgetDialogScriptToken()
{
    // Note, when saving USD files we strip metadata that starts with
    // "Houdini", but we don't want to strip the parm dialog script,
    // so we follow the schema-like convention of using "houdini:" prefix.
    static const TfToken	 theToken("houdini:dialogScript");

    return theToken;

}

const TfToken &
HUSDgetNodeTypeNameToken()
{
    // See the note in HUSDgetDialogScriptToken().
    static const TfToken	 theToken("houdini:nodeTypeName");

    return theToken;

}

const TfType &
HUSDfindType(const UT_StringRef &type_name)
{
    // Note, we call FindDerivedByName() instead of FindByName() so that
    // we find aliases too. Otherwise we will find "UsdGeomCube" but not "Cube".
    return theSchemaBaseType->FindDerivedByName(type_name.toStdString());
}

bool
HUSDisDerivedType(const UsdPrim &prim, const TfType &base_type)
{
    if (base_type.IsUnknown())
	return true;

    const std::string &type_name = prim.GetTypeName().GetString();
    if (type_name.empty())
	return false;

    if (!theSchemaBaseType->FindDerivedByName(type_name).IsA(base_type))
	return false;

    return true;
}

UT_StringHolder
HUSDgetSpecifier(const UsdPrim &prim)
{
    if (prim)
    {
	switch (prim.GetSpecifier())
	{
	    case SdfSpecifierDef:
		return HUSD_Constants::getPrimSpecifierDefine();
	    case SdfSpecifierClass:
		return HUSD_Constants::getPrimSpecifierClass();
	    case SdfSpecifierOver:
		return HUSD_Constants::getPrimSpecifierOverride();
	    case SdfNumSpecifiers:
		// Not a valid value. Just fall through.
		break;
	}
    }

    return UT_StringHolder();
}

bool
HUSDisPrimEditable(const UsdPrim &prim)
{
    if (prim)
    {
        UsdHoudiniHoudiniEditableAPI editableapi(prim);

        if (editableapi)
        {
            UsdAttribute editableattr =
                editableapi.GetHoudiniEditableAttr();
            bool editable = true;

            if (editableattr &&
                editableattr.Get(&editable) &&
                !editable)
                return false;
        }

        // Without the editable API, the primitive is editable.
        return true;
    }

    // Prims that don't exist are "editable" in the sense that we should
    // be allowed to create a primitive in this location.
    return true;
}

bool
HUSDisPrimSelectable(const UsdPrim &prim, UT_Map<HUSD_Path, bool> *cache)
{
    if (!prim)
        return false;

    UsdPrim testprim = prim;
    SdfPathVector paths;
    bool selectable = true;

    while (testprim && !testprim.IsPseudoRoot())
    {
        if (cache)
        {
            auto it = cache->find(testprim.GetPrimPath());

            // If we find a cached value before we find an authored attribute,
            // we can just return the cached value.
            if (it != cache->end())
            {
                selectable = it->second;
                break;
            }
            paths.push_back(testprim.GetPrimPath());
        }

        UsdHoudiniHoudiniSelectableAPI selectableapi(testprim);

        if (selectableapi)
        {
            UsdAttribute selectableattr =
                selectableapi.GetHoudiniSelectableAttr();

            // Stop traversing up the tree as soon as we hit an authored
            // attribute, whether it is true or false.
            if (selectableattr &&
                selectableattr.Get(&selectable))
                break;
        }
        testprim = testprim.GetParent();
    }

    if (cache)
    {
        for (auto &&path : paths)
            cache->emplace(path, selectable);
    }

    return selectable;
}

bool
HUSDisPrimHiddenInUi(const UsdPrim &prim)
{
    if (prim)
        return prim.IsHidden();

    // Prims that don't exist are not "hidden" in the sense that we should
    // show a primitive in this location if one gets created.
    return false;
}

SdfPath
HUSDgetSdfPath(const UT_StringRef &path)
{
    if (path.isstring())
	return SdfPath(path.toStdString());

    return SdfPath();
}

SdfPathVector
HUSDgetSdfPaths(const UT_StringArray &paths)
{
    SdfPathVector result;

    result.reserve(paths.size());
    for( auto &&path : paths )
    {
	SdfPath sdfpath = HUSDgetSdfPath(path);

	if (!sdfpath.IsEmpty())
	    result.emplace_back(sdfpath);
    }

    return result;
}

const SdfPath &
HUSDgetHoudiniLayerInfoSdfPath()
{
    static SdfPath thePath(
	HUSD_Constants::getHoudiniLayerInfoPrimPath().toStdString());

    return thePath;
}

const SdfPath &
HUSDgetHoudiniFreeCameraSdfPath()
{
    static SdfPath thePath(
	HUSD_Constants::getHoudiniFreeCameraPrimPath().toStdString());

    return thePath;
}
    
UsdTimeCode
HUSDgetUsdTimeCode(const HUSD_TimeCode &timecode)
{
    if (timecode.isDefault())
	return UsdTimeCode::Default();

    return UsdTimeCode(timecode.frame());
}

UsdTimeCode
HUSDgetCurrentUsdTimeCode()
{
    return UsdTimeCode(CHgetSampleFromTime(CHgetEvalTime()));
}

UsdTimeCode
HUSDgetNonDefaultUsdTimeCode(const HUSD_TimeCode &timecode)
{
    return UsdTimeCode(timecode.frame());
}

UsdTimeCode
HUSDgetEffectiveUsdTimeCode(const HUSD_TimeCode &timecode,
	const UsdAttribute &attr)
{
    return HUSDgetUsdTimeCode( HUSDgetEffectiveTimeCode( timecode, attr ));
}

HUSD_TimeCode
HUSDgetEffectiveTimeCode( const HUSD_TimeCode &timecode,
	const UsdAttribute &attr)
{
    return HUSDgetEffectiveTimeCode( timecode, HUSDgetValueTimeSampling(attr) );
}

UsdHoudiniHoudiniXformCommonAPI::RotationOrder
HUSDcastRotOrder(const UT_XformOrder &order)
{
    switch (order.rotOrder())
    {
        case UT_XformOrder::XYZ:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ;
        case UT_XformOrder::XZY:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderXZY;
        case UT_XformOrder::YXZ:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderYXZ;
        case UT_XformOrder::YZX:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderYZX;
        case UT_XformOrder::ZXY:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderZXY;
        case UT_XformOrder::ZYX:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderZYX;
        default:
            return UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ;
    }
}

UT_XformOrder
HUSDcastRotOrder(const UsdHoudiniHoudiniXformCommonAPI::RotationOrder rotOrder)
{
    UT_XformOrder::xyzOrder houdiniRot = UT_XformOrder::XYZ;
    switch (rotOrder)
    {
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderXYZ:
            houdiniRot = UT_XformOrder::XYZ;
            break;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderXZY:
            houdiniRot = UT_XformOrder::XZY;
            break;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderYXZ:
            houdiniRot = UT_XformOrder::YXZ;
            break;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderYZX:
            houdiniRot = UT_XformOrder::YZX;
            break;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderZXY:
            houdiniRot = UT_XformOrder::ZXY;
            break;
        case UsdHoudiniHoudiniXformCommonAPI::RotationOrderZYX:
            houdiniRot = UT_XformOrder::ZYX;
            break;
    }
    return UT_XformOrder(UT_XformOrder::SRT, houdiniRot);
}

SdfLayerOffset
HUSDgetSdfLayerOffset(const HUSD_LayerOffset &layeroffset)
{
    return SdfLayerOffset(layeroffset.offset(), layeroffset.scale());
}

HUSD_LayerOffset
HUSDgetLayerOffset(const SdfLayerOffset &layeroffset)
{
    return HUSD_LayerOffset(layeroffset.GetOffset(), layeroffset.GetScale());
}

Usd_PrimFlagsPredicate
HUSDgetUsdPrimPredicate(HUSD_PrimTraversalDemands demands)
{
    Usd_PrimFlagsConjunction	 conj;

    if (demands & HUSD_TRAVERSAL_ACTIVE_PRIMS)
	conj = conj && UsdPrimIsActive;
    if (demands & HUSD_TRAVERSAL_DEFINED_PRIMS)
	conj = conj && UsdPrimIsDefined;
    if (demands & HUSD_TRAVERSAL_LOADED_PRIMS)
	conj = conj && UsdPrimIsLoaded;
    if (demands & HUSD_TRAVERSAL_NONABSTRACT_PRIMS)
	conj = conj && !UsdPrimIsAbstract;

    Usd_PrimFlagsPredicate	 pred(conj);

    if (demands & HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES)
	pred.TraverseInstanceProxies(true);

    // Note that this function ignores the "HUSD_TRAVERSAL_ALLOW_PROTOTYPES"
    // flag, which has no corresponding traversal flag in USD. That flag
    // must be handled explicitly in the (very rare) cases where we might
    // want to respect it.

    return pred;
}

UsdListPosition
HUSDgetUsdListPosition(const UT_StringRef &editopstr)
{
    UsdListPosition	 editop(UsdListPositionFrontOfAppendList);

    if (editopstr == HUSD_Constants::getEditOpAppendFront())
	editop = UsdListPositionFrontOfAppendList;
    else if (editopstr == HUSD_Constants::getEditOpAppendBack())
	editop = UsdListPositionBackOfAppendList;
    else if (editopstr == HUSD_Constants::getEditOpPrependFront())
	editop = UsdListPositionFrontOfPrependList;
    else if (editopstr == HUSD_Constants::getEditOpPrependBack())
	editop = UsdListPositionBackOfPrependList;

    return editop;
}

UsdStagePopulationMask
HUSDgetUsdStagePopulationMask(const HUSD_LoadMasks &load_masks)
{
    if (load_masks.populateAll())
	return UsdStagePopulationMask::All();

    UsdStagePopulationMask	 usdmask;

    if (!load_masks.populatePaths().empty())
    {
	std::vector<SdfPath>	 sdfpaths;

	for (const auto &path : load_masks.populatePaths())
	    sdfpaths.push_back(HUSDgetSdfPath(path));
        // The free camera prim and the layer info prim are both required for
        // some low level underlying functionality to work. So make sure these
        // special prims are always included in the population mask.
        sdfpaths.push_back(HUSDgetHoudiniFreeCameraSdfPath());
        sdfpaths.push_back(HUSDgetSdfPath(
            HUSD_Constants::getHoudiniLayerInfoPrimPath()));
	usdmask.Add(UsdStagePopulationMask(sdfpaths));
    }

    return usdmask;
}

SdfVariability
HUSDgetSdfVariability(HUSD_Variability variability)
{
    switch (variability)
    {
	case HUSD_VARIABILITY_VARYING:
	    return SdfVariabilityVarying;

	case HUSD_VARIABILITY_UNIFORM:
	    return SdfVariabilityUniform;
    };

    return SdfVariabilityVarying;
}

SdfSpecifier
HUSDgetSdfSpecifier(const UT_StringRef &specifier, bool *valid)
 {
     if (valid)
         *valid = true;

     if (specifier == HUSD_Constants::getPrimSpecifierClass().c_str())
         return SdfSpecifierClass;
     else if (specifier == HUSD_Constants::getPrimSpecifierDefine().c_str())
         return SdfSpecifierDef;
     else if (specifier == HUSD_Constants::getPrimSpecifierOverride().c_str())
         return SdfSpecifierOver;

     if (valid)
         *valid = false;

     return SdfSpecifierOver;
 }

SdfPrimSpecHandle
HUSDgetLayerInfoPrim(const SdfLayerHandle &layer, bool create)
{
    static SdfPath	 theLayerInfoPath(
	HUSD_Constants::getHoudiniLayerInfoPrimPath().toStdString());
    SdfPrimSpecHandle	 infoprim(
	layer->GetPrimAtPath(theLayerInfoPath));

    if (create && !infoprim)
    {
	infoprim = SdfCreatePrimInLayer(layer, theLayerInfoPath);
	// The attempt to create the prim will fail if we are trying to create
	// this info on a layer that we don't have permission to edit. This
	// generally means that the HoudiniLayerInfo prim was deleted on some
	// source layer up the chain, and another process, like a Merge, is
	// trying to restore this info. But the layer is a source layer that
	// doesn't belong to us, so we should be failing here, leaving this
	// layer without a layer info prim.
	if (infoprim)
	{
	    infoprim->SetSpecifier(SdfSpecifierDef);
	    infoprim->SetTypeName(
		HUSD_Constants::getHoudiniLayerInfoPrimType().toStdString());
	}
    }

    return infoprim;
}

void
HUSDsetSavePath(const SdfLayerHandle &layer,
	const UT_StringRef &savepath,
        bool savepath_is_time_dependent,
        const UT_StringRef &override_savepath)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, true);

    if (infoprim)
    {
	auto		 data = infoprim->GetCustomData();

	if (savepath.isstring())
        {
	    data[HUSDgetSavePathToken()] =
		VtValue(savepath.toStdString());
	    data[HUSDgetSavePathIsTimeDependentToken()] =
                VtValue(savepath_is_time_dependent);
        }
        else if (override_savepath.isstring())
        {
            data[HUSDgetOverrideSavePathToken()] =
                VtValue(override_savepath.toStdString());
            data[HUSDgetSavePathIsTimeDependentToken()] =
                VtValue(savepath_is_time_dependent);
        }
	else
        {
	    data.erase(HUSDgetSavePathToken());
            data.erase(HUSDgetOverrideSavePathToken());
	    data.erase(HUSDgetSavePathIsTimeDependentToken());
        }
    }
}

bool
HUSDgetSavePath(const SdfLayerHandle &layer,
	std::string &savepath)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
	auto		 data = infoprim->GetCustomData();
        auto		 it = data.find(HUSDgetSavePathToken());

	if (it != data.end())
	    savepath = it->second.Get().Get<std::string>();
	else
	    savepath.clear();
    }
    else
	savepath.clear();

    return (savepath.length() > 0);
}

bool
HUSDgetOverrideSavePath(const SdfLayerHandle &layer,
        std::string &override_savepath)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
        auto		 data = infoprim->GetCustomData();
        auto		 it = data.find(HUSDgetOverrideSavePathToken());

        if (it != data.end())
            override_savepath = it->second.Get().Get<std::string>();
        else
            override_savepath.clear();
    }
    else
        override_savepath.clear();

    return (override_savepath.length() > 0);
}

bool
HUSDgetSavePathIsTimeDependent(const SdfLayerHandle &layer)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
	auto		 data = infoprim->GetCustomData();
	auto		 it = data.find(HUSDgetSavePathIsTimeDependentToken());

	if (it != data.end())
	    return it->second.Get().Get<bool>();
    }

    return false;
}

void
husdAddVolumeLockedGeos(
        const UT_Function<void (const XUSD_LockedGeoPtr &)> &addfunc,
        const SdfLayerRefPtr &layer)
{
    // The bgeo file format plugin records a list of its locked geos on the
    // layer info prim (e.g. for any volumes produced by unpacking)
    auto infoprim = HUSDgetLayerInfoPrim(layer, false);
    if (!infoprim)
        return;

    auto data = infoprim->GetCustomData();
    auto it = data.find(HUSDgetVolumeFilePathsToken());
    if (it == data.end())
        return;

    auto file_paths = it->second.Get().Get<VtArray<SdfAssetPath>>();
    for (auto &&file_path : file_paths)
    {
        SdfFileFormat::FileFormatArguments args;
        std::string path;
        SdfLayer::SplitIdentifier(file_path.GetAssetPath(), &path, &args);

        auto locked_geo = XUSD_LockedGeoRegistry::getLockedGeo(path, args);
        UT_ASSERT(locked_geo); // The locked geo should still be active!
        if (locked_geo)
            addfunc(locked_geo);
    }
}

void
HUSDaddVolumeLockedGeos(XUSD_Data &outdata,
        const SdfLayerRefPtr &layer)
{
    husdAddVolumeLockedGeos(
        [&outdata](const XUSD_LockedGeoPtr &locked_geo) {
            outdata.addLockedGeo(locked_geo);
        }, layer);
}

void
HUSDaddVolumeLockedGeos(XUSD_LockedGeoSet &locked_geos,
        const SdfLayerRefPtr &layer)
{
    husdAddVolumeLockedGeos(
        [&locked_geos](const XUSD_LockedGeoPtr &locked_geo) {
            locked_geos.insert(locked_geo);
        }, layer);
}

void
HUSDsetSourcePath(const SdfLayerHandle &layer,
        const UT_StringRef &sourcepath)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, true);

    if (infoprim)
    {
        auto		 data = infoprim->GetCustomData();

        if (sourcepath.isstring())
            data[HUSDgetSourcePathToken()] =
                VtValue(sourcepath.toStdString());
        else
            data.erase(HUSDgetSourcePathToken());
    }
}

bool
HUSDgetSourcePath(const SdfLayerHandle &layer,
        std::string &sourcepath)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
        auto		 data = infoprim->GetCustomData();
        auto		 it = data.find(HUSDgetSourcePathToken());

        if (it != data.end())
            sourcepath = it->second.Get().Get<std::string>();
        else
            sourcepath.clear();
    }
    else
        sourcepath.clear();

    return (sourcepath.length() > 0);
}

void
HUSDsetSaveControl(const SdfLayerHandle &layer,
	const UT_StringRef &savecontrol)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, true);

    if (infoprim)
    {
	auto		 data = infoprim->GetCustomData();

	if (savecontrol.isstring())
	    data[HUSDgetSaveControlToken()] =
		VtValue(savecontrol.toStdString());
	else
	    data.erase(HUSDgetSaveControlToken());
    }
}

bool
HUSDgetSaveControl(const SdfLayerHandle &layer,
	std::string &savecontrol)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
	auto		 data = infoprim->GetCustomData();
	auto		 it = data.find(HUSDgetSaveControlToken());

	if (it != data.end())
	    savecontrol = it->second.Get().Get<std::string>();
	else
	    savecontrol.clear();
    }
    else
	savecontrol.clear();

    return (savecontrol.length() > 0);
}

void
HUSDsetCreatorNode(const SdfLayerHandle &layer, int nodeid)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, true);

    if (infoprim)
    {
        auto		 data = infoprim->GetCustomData();

        data[HUSDgetCreatorNodeToken()] = VtValue(nodeid);
    }
}

bool
HUSDgetCreatorNode(const SdfLayerHandle &layer, std::string &nodepath)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    nodepath.clear();
    if (infoprim)
    {
	auto		 data = infoprim->GetCustomData();
	auto		 it = data.find(HUSDgetCreatorNodeToken());

	if (it != data.end())
        {
	    int nodeid = it->second.Get().Get<int>();
            OP_Node *node = OP_Node::lookupNode(nodeid);

            if (node)
                nodepath = node->getFullPath().toStdString();
        }
    }

    return (nodepath.length() > 0);
}

void
HUSDsetSourceNode(const UsdPrim &prim, int nodeid)
{
    prim.SetCustomDataByKey(HUSDgetSourceNodeToken(), VtValue(nodeid));
}

void
HUSDclearEditorNodes(const SdfLayerHandle &layer)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
	auto		 data = infoprim->GetCustomData();

	data.erase(HUSDgetEditorNodesToken());
    }
}

void
HUSDaddEditorNode(const SdfLayerHandle &layer, int nodeid)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, true);

    if (infoprim)
    {
        auto             data = infoprim->GetCustomData();
        auto             it = data.find(HUSDgetEditorNodesToken());
        VtArray<int>     vtnodeids;

        if (it != data.end())
            vtnodeids = it->second.Get().Get<VtArray<int> >();
        if (std::find(vtnodeids.begin(), vtnodeids.end(), nodeid) ==
            vtnodeids.end())
        {
            vtnodeids.push_back(nodeid);
            data[HUSDgetEditorNodesToken()] = VtValue(vtnodeids);
        }
    }
}

void
HUSDsetSoloLightPaths(const SdfLayerHandle &layer,
	const HUSD_PathSet &paths)
{
    if (paths.size() > 0)
    {
        auto             infoprim = HUSDgetLayerInfoPrim(layer, true);

        if (infoprim)
        {
            auto                     data = infoprim->GetCustomData();
            VtArray<std::string>     vtpaths;

            for (auto &&path : paths.sdfPathSet())
                vtpaths.push_back(path.GetString());
            data[HUSDgetSoloLightPathsToken()] = VtValue(vtpaths);
        }
    }
    else if (auto infoprim = HUSDgetLayerInfoPrim(layer, false))
    {
        // This assumes the solo light paths custom data is the only data we
        // will have on the info prim on this layer (which is true for the one
        // situation where we should be calling this method).
        layer->RemoveRootPrim(infoprim);
    }
}

bool
HUSDgetSoloLightPaths(const SdfLayerHandle &layer,
	HUSD_PathSet &paths)
{
    auto                 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
	auto             data = infoprim->GetCustomData();
	auto             it = data.find(HUSDgetSoloLightPathsToken());

	if (it != data.end())
	{
	    VtArray<std::string>         vtpaths;

	    vtpaths = it->second.Get().Get<VtArray<std::string> >();
            for (auto &&path : vtpaths)
                paths.sdfPathSet().insert(SdfPath(path));
	}
	else
	    paths.clear();
    }
    else
	paths.clear();

    return (paths.size() > 0);
}

bool
HUSDhasAnyVisibleLights(const UsdStageRefPtr &stage, const HUSD_TimeCode &tc)
{
    UsdPrim     root = stage->GetPseudoRoot();
    auto        predicate = HUSDgetUsdPrimPredicate(
        HUSD_PrimTraversalDemands(HUSD_TRAVERSAL_ACTIVE_PRIMS |
                                  HUSD_TRAVERSAL_DEFINED_PRIMS |
                                  HUSD_TRAVERSAL_NONABSTRACT_PRIMS |
                                  HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES));

    xusd_FindVisibleLightPrim   data(tc);
    XUSDfindPrims(root, data, predicate);

    return data.foundAnyVisibleLight();
}

void
HUSDsetSoloGeometryPaths(const SdfLayerHandle &layer,
	const HUSD_PathSet &paths)
{
    if (paths.size() > 0)
    {
        auto                 infoprim = HUSDgetLayerInfoPrim(layer, true);

        if (infoprim)
        {
            auto                     data = infoprim->GetCustomData();
            VtArray<std::string>     vtpaths;

            for (auto &&path : paths.sdfPathSet())
                vtpaths.push_back(path.GetString());
            data[HUSDgetSoloGeometryPathsToken()] = VtValue(vtpaths);
        }
    }
    else if (auto infoprim = HUSDgetLayerInfoPrim(layer, false))
    {
        // This assumes the solo geometry paths custom data is the only data we
        // will have on the info prim on this layer (which is true for the one
        // situation where we should be calling this method).
        layer->RemoveRootPrim(infoprim);
    }
}

bool
HUSDgetSoloGeometryPaths(const SdfLayerHandle &layer,
	HUSD_PathSet &paths)
{
    auto                 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
	auto             data = infoprim->GetCustomData();
	auto             it = data.find(HUSDgetSoloGeometryPathsToken());

	if (it != data.end())
	{
	    VtArray<std::string>         vtpaths;

	    vtpaths = it->second.Get().Get<VtArray<std::string> >();
            for (auto &&path : vtpaths)
                paths.sdfPathSet().insert(SdfPath(path));
	}
	else
	    paths.clear();
    }
    else
	paths.clear();

    return (paths.size() > 0);
}

void
HUSDsetTreatAsSopLayer(const SdfLayerHandle &layer, bool treatassoplayer)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, true);

    if (infoprim)
    {
        auto		 data = infoprim->GetCustomData();

        data[HUSDgetTreatAsSopLayerToken()] = VtValue(treatassoplayer);
    }
}

bool
HUSDgetTreatAsSopLayer(const SdfLayerHandle &layer)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
        auto		 data = infoprim->GetCustomData();
        auto		 it = data.find(HUSDgetTreatAsSopLayerToken());

        if (it != data.end())
        {
            bool treatassoplayer = it->second.Get().Get<bool>();

            return treatassoplayer;
        }
    }

    return false;
}

void
HUSDaddPrimEditorNodeId(const UsdPrim &prim, int nodeid)
{
    if (prim)
    {
        VtValue oldvalue;
        VtArray<int> ids;

        oldvalue = prim.GetCustomDataByKey(HUSDgetPrimEditorNodesToken());
        if (oldvalue.IsHolding<VtArray<int>>())
            ids = oldvalue.UncheckedGet<VtArray<int>>();
        if (std::find(ids.begin(), ids.end(), nodeid) == ids.end())
        {
            ids.push_back(nodeid);
            prim.SetCustomDataByKey(
                HUSDgetPrimEditorNodesToken(), VtValue(ids));
        }
    }
}

void
HUSDaddPrimEditorNodeId(const SdfPrimSpecHandle &prim, int nodeid)
{
    if (prim)
    {
        auto customdata = prim->GetCustomData();
        auto it = customdata.find(HUSDgetPrimEditorNodesToken());
        VtArray<int> ids;

        if (it != customdata.end() &&
            it->second.Get().IsHolding<VtArray<int>>())
            ids = it->second.Get().UncheckedGet<VtArray<int>>();
        if (std::find(ids.begin(), ids.end(), nodeid) == ids.end())
        {
            ids.push_back(nodeid);
            prim->SetCustomData(HUSDgetPrimEditorNodesToken(), VtValue(ids));
        }
    }
}

void
HUSDclearPrimEditorNodeIds(const UsdPrim &prim)
{
    if (prim)
        prim.SetCustomDataByKey(HUSDgetPrimEditorNodesToken(),
            VtValue(VtArray<int>()));
}

void
HUSDclearPrimEditorNodeIds(const SdfPrimSpecHandle &prim)
{
    if (prim)
        prim->SetCustomData(HUSDgetPrimEditorNodesToken(),
            VtValue(VtArray<int>()));
}

void
HUSDaddPropertyEditorNodeId(const UsdProperty &property, int nodeid)
{
    if (property)
    {
        VtValue oldvalue;
        VtArray<int> ids;

        oldvalue = property.GetCustomDataByKey(HUSDgetPrimEditorNodesToken());
        if (oldvalue.IsHolding<VtArray<int>>())
            ids = oldvalue.UncheckedGet<VtArray<int>>();
        if (std::find(ids.begin(), ids.end(), nodeid) == ids.end())
        {
            ids.push_back(nodeid);
            property.SetCustomDataByKey(
                HUSDgetPrimEditorNodesToken(), VtValue(ids));
        }
    }
}

void
HUSDclearPropertyEditorNodeIds(const UsdProperty &property)
{
    if (property)
    {
        property.SetCustomDataByKey(
            HUSDgetPrimEditorNodesToken(), VtValue(VtArray<int>()));
    }
}

void
HUSDbumpPropertiesForHydra(const UsdAttributeVector &attrs)
{
    if (attrs.empty())
        return;

    SdfChangeBlock	 changeblock;

    for (auto &&attr : attrs)
    {
        static SYS_AtomicCounter     theFilterIdCounter;
        VtValue			     id( theFilterIdCounter.add(1) );

        attr.SetCustomDataByKey( HUSDgetMaterialIdToken(), id );
    }
}

void
HUSDclearDataId(const UsdAttribute &attr)
{
    static VtValue	 theInvalidDataIdValue(GA_INVALID_DATAID);
    VtValue		 value = attr.GetCustomDataByKey(HUSDgetDataIdToken());

    // Simply clearing the data id value won't get rid of weaker opinions.
    // We need to explicitly author a stronger opinion setting the data id
    // to an invalid value. Don't do this unless there is already a valid
    // data id value.
    if (!value.IsEmpty() && value != theInvalidDataIdValue)
	attr.SetCustomDataByKey(HUSDgetDataIdToken(), theInvalidDataIdValue);
}

TfToken
HUSDgetParentKind(const TfToken &kind)
{
    if (KindRegistry::IsA(kind, KindTokens->component))
        return KindTokens->group;
    if (KindRegistry::IsA(kind, KindTokens->subcomponent))
        return KindTokens->component;

    // If it's not a component or subcomponent, then treat is like a "group"
    // kind, which can contain other models. In the absence of any better
    // guess, a group's parent should be a group, and an assembly's parent
    // should be an assembly. So assume all custom kinds should also be
    // self-containing.
    return kind;
}

bool
HUSDprimAndAllExistingAncestorsActive(const UsdStageWeakPtr &stage,
	const SdfPath &path)
{
    // We can only handle absolute paths. Return false because the question
    // doesn't even really make sense.
    UT_ASSERT(path.IsAbsolutePath());
    if (!path.IsAbsolutePath())
        return false;

    // The absolute root path always exists and can't be inactive.
    SdfPath testpath = path;
    while (testpath != SdfPath::AbsoluteRootPath())
    {
        UsdPrim prim = stage->GetPrimAtPath(testpath);
        if (prim)
            return prim.IsActive();
        testpath = testpath.GetParentPath();
    }

    return true;
}

SdfPrimSpecHandle
HUSDcreatePrimInLayer(const UsdStageWeakPtr &stage,
	const SdfLayerHandle &layer,
	const SdfPath &path,
	const TfToken &kind,
        SdfSpecifier specifier,
        SdfSpecifier parent_prims_specifier,
	const std::string &parent_prims_type)
{
    // We have to have an absolute path, because we don't know what a relative
    // path is meant to be relative to.
    if (!path.IsAbsolutePath())
        return SdfPrimSpecHandle();

    // If for some reason we are asked to create the root prim, just claim
    // success and return the root prim. No point trying to set the specifier
    // or other data on the root prim.
    if (path.IsAbsoluteRootPath())
        return layer->GetPseudoRoot();

    // Make sure we aren't trying to create a primitive that is going to be a
    // child of an inactive primitive. The creation will work, but subsequent
    // operations will fail (somewhat mysteriously). Better to catch the error
    // sooner.
    if (!HUSDprimAndAllExistingAncestorsActive(stage, path))
        return SdfPrimSpecHandle();

    UsdPrim		 prim = stage->GetPrimAtPath(path);
    SdfPrimSpecHandle	 existing_parent_spec;
    bool		 traverse_parents = false;

    if (SdfIsDefiningSpecifier(parent_prims_specifier) ||
	!parent_prims_type.empty() ||
	!kind.IsEmpty())
	traverse_parents = true;

    if (traverse_parents &&
	!path.IsEmpty() &&
	path != SdfPath::AbsoluteRootPath())
    {
	SdfPath		 existing_parent_path = path;

	do {
	    existing_parent_path = existing_parent_path.GetParentPath();
	    existing_parent_spec = layer->GetPrimAtPath(existing_parent_path);
	} while (!existing_parent_spec &&
		 existing_parent_path != SdfPath::AbsoluteRootPath());
    }

    SdfPrimSpecHandle	 primspec = SdfCreatePrimInLayer(layer, path);

    // If we have been asked to put a specifier on the prim, do it even if
    // the prim already exists.
    if (primspec && SdfIsDefiningSpecifier(specifier))
        primspec->SetSpecifier(specifier);

    // If the prim is already defined on the stage, we don't want to make any
    // further changes to the prim in this layer. We must check for the prim
    // on the stage before creating the primspec on the layer because the
    // layer may be composed on the stage (depending on whether we are doing
    // direct layer editing or not).
    if (!(prim && prim.IsDefined()) && primspec)
    {
	TfToken	 parentkind;

	if (!kind.IsEmpty())
	{
	    primspec->SetKind(kind);
	    parentkind = HUSDgetParentKind(kind);
	}
	if (traverse_parents)
	{
	    auto parentspec = primspec->GetNameParent();

	    while (parentspec && parentspec != existing_parent_spec)
	    {
		auto parentprim = stage->GetPrimAtPath(parentspec->GetPath());

		// Stop modifying parent primspecs when we hit a primitive
		// that is already defined on our reference stage.
		if (parentprim && parentprim.IsDefined())
		    break;

		if (SdfIsDefiningSpecifier(parent_prims_specifier))
		    parentspec->SetSpecifier(parent_prims_specifier);
		if (!parent_prims_type.empty())
		    parentspec->SetTypeName(parent_prims_type);
		if (!parentkind.IsEmpty())
		{
		    parentspec->SetKind(parentkind);
		    parentkind = HUSDgetParentKind(parentkind);
		}
		parentspec = parentspec->GetNameParent();
	    }
	}
    }

    return primspec;
}

bool
HUSDcopySpec(const SdfLayerHandle &srclayer,
	const SdfPath &srcpath,
	const SdfLayerHandle &destlayer,
	const SdfPath &destpath,
	const SdfPath &srcroot,
	const SdfPath &destroot,
	const fpreal frameoffset /*=0*/,
	const fpreal frameratescale /*=1*/)
{
    namespace			 ph = std::placeholders;

    // Source and destination paths must be absolute or SdfCopySpec can end
    // up in an infinite loop.
    if (!srcpath.IsAbsolutePath() || !destpath.IsAbsolutePath())
	return false;

    SdfPath	 realsrcroot(srcroot.IsEmpty() ? srcpath : srcroot);
    SdfPath	 realdestroot(destroot.IsEmpty() ? destpath : destroot);

    return SdfCopySpec(
	srclayer, srcpath,
	destlayer, destpath,
	std::bind(_ShouldCopyValue,
	    std::cref(realsrcroot), std::cref(realdestroot),
	    std::cref(frameoffset), std::cref(frameratescale),
	    ph::_1, ph::_2, ph::_3, ph::_4, ph::_5,
	    ph::_6, ph::_7, ph::_8, ph::_9),
	std::bind(_ShouldCopyChildren,
	    std::cref(realsrcroot), std::cref(realdestroot),
	    ph::_1, ph::_2, ph::_3, ph::_4, ph::_5,
	    ph::_6, ph::_7, ph::_8, ph::_9));
}

void
HUSDmodifyAssetPaths(const SdfLayerHandle &layer,
        const UsdUtilsModifyAssetPathFn &modifyFn,
        bool keep_empty_assets_in_arrays)
{
    SdfChangeBlock       changeblock;

    // The UsdUtilsModifyAssetPaths method sets the layer offset to a no-op
    // for any sublayer where the path is changed. We are just manipulating
    // the paths, but pointing to the same files, so we want to preserve any
    // layer offset values. Stash the values before the update, and restore
    // them after it's done.
    SdfLayerOffsetVector oldoffsets = layer->GetSubLayerOffsets();
    UsdUtilsModifyAssetPaths(layer, modifyFn, keep_empty_assets_in_arrays);
    SdfLayerOffsetVector newoffsets = layer->GetSubLayerOffsets();

    // If the number of sublayers changed, we can't really correlate the old
    // and new offsets, so don't bother trying.
    if (oldoffsets.size() == newoffsets.size())
    {
        for (int i = 0, n = newoffsets.size(); i < n; i++)
        {
            if (newoffsets[i] != oldoffsets[i])
            {
                UT_ASSERT(newoffsets[i] == SdfLayerOffset());
                layer->SetSubLayerOffset(oldoffsets[i], i);
            }
        }
    }
}

bool
HUSDupdateExternalReferences(const SdfLayerHandle &layer,
	const std::map<std::string, std::string> &pathmap)
{
    if (pathmap.empty())
        return false;

    SdfChangeBlock	 changeblock;

    HUSDmodifyAssetPaths(layer,
        husd_UpdateReferencesFromMap(pathmap));

    return true;
}

void
HUSDstitchLayers(const SdfLayerHandle &strongLayer,
	const SdfLayerHandle &weakLayer,
	HUSD_PathSet *varyingDefaultPaths /*=nullptr*/)
{
    // It's possible to end up in a state where there is no "subLayers"
    // field on the root prim, but there is an (empty) subLayerOffsets field.
    // When we stitch in a weaker layer with both these fields, the subLayers
    // get copied over, but the subLayerOffsets don't (because they are a
    // weaker opinion). Then we have a mismatch in the size of the subLayer
    // and subLayerOffsets arrays. This makes PcpLayerStack::_BuildLayerStack
    // crash. So if we are in this state, delete the (empty) subLayerOffsets
    // array from the stronger layer before stitching in the weaker layer.
    if (strongLayer->HasField(SdfPath::AbsoluteRootPath(),
            SdfFieldKeys->SubLayerOffsets) &&
        !strongLayer->HasField(SdfPath::AbsoluteRootPath(),
            SdfFieldKeys->SubLayers))
    {
        // The subLayerOffsets array should at least be _empty_, of something
        // is even more wrong going on here...
        UT_ASSERT(strongLayer->GetSubLayerOffsets().size() == 0);
        strongLayer->EraseField(SdfPath::AbsoluteRootPath(),
            SdfFieldKeys->SubLayerOffsets);
    }
    using namespace std::placeholders;
    auto stitchCB = std::bind(_StitchCallback, _1, _2, _3, _4, _5, _6, _7,
                              varyingDefaultPaths);
    UsdUtilsStitchLayers(strongLayer, weakLayer, stitchCB);
}

bool
HUSDisSopLayer(const std::string &identifier)
{
    // If a SOP layer is not anonymous, it's identifier will start with
    // an "op:" prefix.
    return (identifier.compare(0, OPREF_PREFIX_LEN, OPREF_PREFIX) == 0);
}

bool
HUSDisSopLayer(const SdfLayerHandle &layer)
{
    if (HUSDisLopLayer(layer))
        return HUSDgetTreatAsSopLayer(layer);

    return HUSDisSopLayer(layer->GetIdentifier());
}

bool
HUSDisLopLayer(const std::string &identifier)
{
    if (SdfLayer::IsAnonymousLayerIdentifier(identifier))
    {
        std::string tag = SdfLayer::GetDisplayNameFromIdentifier(identifier);

        // Layers created by LOPs will have the "LOP" prefix. Layers created
        // by stage or layer flattening will simply have ".usda" as their
        // display name (see UsdStage::Flatten implementation).
        if (tag.find(theLopTagPrefix) == 0 || tag == ".usda")
        {
            return true;
        }
    }

    return false;
}

bool
HUSDisLopLayer(const SdfLayerHandle &layer)
{
    return HUSDisLopLayer(layer->GetIdentifier());
}

bool
HUSDshouldSaveLayerToDisk(const SdfLayerHandle &layer)
{
    if (HUSDisLopLayer(layer->GetIdentifier()))
    {
	std::string	 savecontrol;

	// We don't want to save placeholder layers or layers marked as "do
	// not save", or which are anonymous copies of layers on disk.
	if (HUSDgetSaveControl(layer, savecontrol) &&
	    (HUSD_Constants::getSaveControlPlaceholder() == savecontrol ||
	     HUSD_Constants::getSaveControlDoNotSave() == savecontrol))
	    return false;

	return true;
    }
    else if (HUSDisSopLayer(layer->GetIdentifier()))
	return true;

    return false;
}

std::string
HUSDgetLayerSaveLocation(const SdfLayerHandle &layer, bool *using_node_path)
{
    std::string  savepath;
    std::string  savecontrol;
    std::string  override_savepath;

    if (!HUSDgetSavePath(layer, savepath))
    {
	if (HUSDgetCreatorNode(layer, savepath))
	{
	    if (*savepath.begin() == '/')
		savepath.insert(0, ".");
	    else
		savepath.insert(0, "./");
            savepath.append(".usd");
	}
	else
	{
	    SdfFileFormat::FileFormatArguments	 args;

	    layer->SplitIdentifier(layer->GetIdentifier(), &savepath, &args);
	    if (savepath.compare(0, OPREF_PREFIX_LEN, OPREF_PREFIX) == 0)
	    {
		savepath.erase(0, OPREF_PREFIX_LEN);
		if (*savepath.begin() == '/')
		    savepath.insert(0, ".");
		else
		    savepath.insert(0, "./");
	    }
	}
	if (using_node_path)
	    *using_node_path = true;
    }
    else if (using_node_path)
	*using_node_path = false;

    // If our save path has been overridden, use the override path even if
    // we are using the node path to get the save location. But we still want
    // to know that the save path originally came from a node path or else
    // the HUSD_Save saveStage code for making node paths relative to the
    // primary USD file won't work properly.
    if (HUSDgetOverrideSavePath(layer, override_savepath))
        savepath = override_savepath;

    UT_ASSERT(!HUSDisLopLayer(savepath));

    return savepath;
}

std::map<std::string, XUSD_ExternalRefType>
HUSDgetExternalReferences(const SdfLayerRefPtr &layer,
        bool sublayers_only)
{
    std::map<std::string, XUSD_ExternalRefType>  result;

    if (sublayers_only)
    {
        // Just get sublayer references.
        auto sublayerpaths = layer->GetSubLayerPaths();
        for (auto &&ref : sublayerpaths)
            result.emplace(ref, XUSD_EXTERNAL_REF_OTHER);
    }
    else
    {
        std::set<std::string>	                 otherrefs;
        std::set<std::string>	                 cliprefs;

        // Get all payload, reference, and sublayer references.
        otherrefs = layer->GetCompositionAssetDependencies();
        for (auto &&ref : otherrefs)
            result.emplace(ref, XUSD_EXTERNAL_REF_OTHER);
        // Get all references stored in clip metadata.
        cliprefs = _ClipReferences(layer);
        for (auto &&ref : cliprefs)
            result.emplace(ref, XUSD_EXTERNAL_REF_VALUE_CLIP);
    }

    return result;
}

void
HUSDaddExternalReferencesToLayerMap(const SdfLayerRefPtr &layer,
        XUSD_IdentifierToReferenceInfoMap &referenceinfomap,
	bool recursive,
        bool include_placeholders)
{
    auto refs = HUSDgetExternalReferences(layer);

    for (auto &&it : refs)
    {
        auto ref = it.first;
        auto reftype = it.second;

	if (referenceinfomap.find(ref) == referenceinfomap.end())
	{
	    // Quick pre-check to avoid finding/opening the layer just to
	    // test if it should be saved to disk.
	    if (HUSDisLopLayer(ref) || HUSDisSopLayer(ref))
	    {
		auto		 reflayer = SdfLayer::FindOrOpen(ref);

                if (!reflayer)
                {
                    HUSD_ErrorScope::addWarning(
                        HUSD_ERR_CANT_FIND_LAYER,
                        ref.c_str());
                }
                else if (HUSDshouldSaveLayerToDisk(reflayer))
		{
                    referenceinfomap[ref] = { reflayer, reftype, layer };
		    if (recursive)
			HUSDaddExternalReferencesToLayerMap(
			    reflayer, referenceinfomap, recursive);
		}
                else if (include_placeholders &&
                         HUSDisLayerPlaceholder(reflayer))
                {
                    referenceinfomap[ref] = { reflayer, reftype, layer };
                }
	    }
	}
    }
}

bool
HUSDaddStageTimeSample(const UsdStageWeakPtr &src,
	const UsdStageRefPtr &dest,
        const UsdTimeCode &timecode,
        const std::set<std::string> *strip_sublayer_identifiers,
	XUSD_LayerSet &held_layers,
        bool force_notifiable_file_format,
        bool set_layer_override_save_paths,
        XUSD_ExistenceTracker *existence_tracker,
        HUSD_PathSet *varying_default_paths /*=nullptr*/)
{
    ArResolverContextBinder	          binder(src->GetPathResolverContext());
    SdfLayerRefPtr		          srclayer = src->GetRootLayer();
    SdfLayerRefPtr		          destlayer = dest->GetRootLayer();
    XUSD_IdentifierToReferenceInfoMap     destreferenceinfomap;
    XUSD_IdentifierToSavePathMap          stitchedpathmap;
    std::set<std::string>	          newdestlayers;
    std::map<std::string, SdfLayerRefPtr> currentsamplesavelocations;
    bool			          success = false;

    // If the source stage has a session layer, incorporate the session layer
    // data into the root layer of the stage. This is how we save data from
    // post layers.
    if (src->GetSessionLayer())
    {
        // Flatten the session layers into a single layer.
        UsdStageRefPtr poststage =
            HUSDcreateStageInMemory(UsdStage::LoadNone, src);
        poststage->GetRootLayer()->TransferContent(src->GetSessionLayer());
        srclayer = HUSDflattenLayers(poststage);
        // Stitch the original root layer into the flattened session layer.
        HUSDstitchLayers(srclayer, src->GetRootLayer(), varying_default_paths);
    }

    HUSDaddExternalReferencesToLayerMap(destlayer, destreferenceinfomap, true);

    if (existence_tracker)
        existence_tracker->collectNewStageData(src);
    {
        // Make sure all the work we do stitching together all the various
        // layers doesn't cause any recomposition of the destination stage
        // until all the changes are finished.
        SdfChangeBlock changeblock;
        success = _StitchLayersRecursive(srclayer, destlayer,
            strip_sublayer_identifiers,
            destreferenceinfomap, stitchedpathmap,
            newdestlayers, currentsamplesavelocations,
            UT_EnvControl::getInt(ENV_HOUDINI_CASE_SENSITIVE_FS) != 0,
            force_notifiable_file_format,
            set_layer_override_save_paths,
            varying_default_paths);
    }
    if (existence_tracker)
        existence_tracker->authorVisibility(dest, timecode);

    for (auto &&it : destreferenceinfomap)
	held_layers.insert(it.second.myLayer);

    return success;
}

const std::string &
HUSDgetStageRootLayerIdentifier()
{
    return theLopStageRootLayerIdentifier;
}

UsdStageRefPtr
HUSDcreateStageInMemory(UsdStage::InitialLoadSet load,
	const UsdStageWeakPtr &context_stage,
	int resolver_context_nodeid,
	const ArResolverContext *resolver_context)
{
    static UT_Array<XUSD_StageFactory *>	 theFactories;
    static bool					 theFirstCall = true;

    if (theFirstCall)
    {
	UT_DSO		 dso;

	dso.run("newStageFactory", &theFactories);
	theFactories.sort(
	    [](const XUSD_StageFactory *f1, const XUSD_StageFactory *f2) {
		return (f1->getPriority() < f2->getPriority());
	    });
	theFirstCall = false;
    }

    UsdStageRefPtr	 stage;

    if (resolver_context)
    {
	// When building a stage based on an existing resolver context,
	// plugin factories don't even get a chance.
	stage = UsdStage::CreateInMemory(
            HUSDgetStageRootLayerIdentifier(),
	    *resolver_context,
	    load);
    }
    else if (context_stage)
    {
	// When building a stage based on an existing stage, copy the
	// resolver context. Plugin factories don't even get a chance.
	stage = UsdStage::CreateInMemory(
            HUSDgetStageRootLayerIdentifier(),
	    context_stage->GetPathResolverContext(),
	    load);
    }
    else
    {
	// Go through factories in descending priority order until one of them
	// returns a stage.
	for (int i = theFactories.size(); !stage && (i --> 0); )
	    stage = theFactories(i)->createStage(load, resolver_context_nodeid);

	// Last resort. Just use a default context object.
	if (!stage)
	    stage = UsdStage::CreateInMemory(
                HUSDgetStageRootLayerIdentifier(),
		ArGetResolver().CreateDefaultContext(),
		load);
    }

    if (context_stage)
    {
        // Copy data from the context stage's root layer to our new root layer.
        XUSD_RootLayerData rootlayerdata(context_stage);
        rootlayerdata.toStage(stage);
    }
    else
    {
        // Set the basic root prim metadata that can only exist on the root
        // prim and which can affect composition or operation of some LOP
        // nodes.
        UsdGeomSetStageMetersPerUnit(stage,
            HUSD_Preferences::defaultMetersPerUnit());

        // We are using the FPS context option to set our stage's FPS.
        // Add a micronode dependency so this node will recook if the @fps
        // context option changes.
        static constexpr UT_StringLit theFpsOption("fps");
        int thread = SYSgetSTID();
        CH_EvalContext &ctx = CHgetManager()->evalContext(thread);
        DEP_ContextOptionsReadHandle options = ctx.contextOptions();
        UT_OptionEntryPtr entry;
        fpreal fps;

        // Use the default context options if there isn't one in the
        // current CH evaluation context.
        if (options.isNull())
            options = CHgetManager()->getDefaultContextOptions();

        if (!options.isNull() && options->hasOption(theFpsOption.asRef()))
            entry = options->getOptionEntry(theFpsOption.asRef())->clone();

        // There should always be a numeric fps context option available.
        UT_ASSERT(entry && entry->getType() == UT_OPTION_FPREAL);
        if (entry && entry->getType() == UT_OPTION_FPREAL)
            fps = entry->getOptionF();
        else
            fps = CHgetManager()->getSamplesPerSec();
        stage->SetTimeCodesPerSecond(fps);
        stage->SetFramesPerSecond(fps);
        // Add dependency tracking in case the global or context-specific
        // @fps value changes.
        if (OP_Node *node = OP_Node::lookupNode(resolver_context_nodeid))
            CHgetManager()->addContextOptionDependency(theFpsOption.asRef(),
                CHgetManager()->shouldSetGlobalContextOptionDependency(
                    theFpsOption.asRef(), options),
                node->dataMicroNode());
    }

    return stage;
}

UsdStageRefPtr
HUSDcreateStageInMemory(const HUSD_LoadMasks *load_masks,
	const UsdStageWeakPtr &context_stage,
	int resolver_context_nodeid,
	const ArResolverContext *resolver_context)
{
    UsdStageRefPtr		 stage;
    PcpVariantFallbackMap        oldfallbacks;
    bool                         restorefallbacks = false;

    if (load_masks)
    {
        PcpVariantFallbackMap    fallbacks;

        oldfallbacks = UsdStage::GetGlobalVariantFallbacks();
        HUSDconvertVariantSelectionFallbacks(
            load_masks->variantSelectionFallbacks(), fallbacks);
        UsdStage::SetGlobalVariantFallbacks(fallbacks);
        restorefallbacks = true;
    }
    stage = HUSDcreateStageInMemory(
	(load_masks && !load_masks->loadAll())
	    ? UsdStage::LoadNone
	    : UsdStage::LoadAll,
	context_stage,
	resolver_context_nodeid,
	resolver_context);
    if (restorefallbacks)
        UsdStage::SetGlobalVariantFallbacks(oldfallbacks);

    // Set the stage mask on the new stage.
    if (load_masks)
    {
	auto stage_mask = HUSDgetUsdStagePopulationMask(*load_masks);
	if (stage_mask != UsdStagePopulationMask::All())
	    stage->SetPopulationMask(stage_mask);
	if (!load_masks->muteLayers().empty())
	{
	    std::vector<std::string>	 mutelayers;

	    // Skip over any LOP layers set to be muted. We can't actually
	    // mute them yet because we don't know the identifiers of the
	    // stage layers that will be copies of these layers.
	    for (auto &&identifier : load_masks->muteLayers())
	    {
	        if (!HUSD_LoadMasks::isLopMutingIdentifier(identifier))
	            mutelayers.push_back(identifier.toStdString());
	    }
	    stage->MuteAndUnmuteLayers(
		mutelayers, std::vector<std::string>());
	}

        if (!load_masks->loadAll())
        {
            UsdStageLoadRules        loadrules(UsdStageLoadRules::LoadNone());

            for (auto &&path : load_masks->loadPaths())
                loadrules.LoadWithDescendants(HUSDgetSdfPath(path));

            stage->SetLoadRules(loadrules);
        }
    }

    return stage;
}

UsdStageRefPtr
HUSDcreateStageFromRootLayer(const SdfLayerRefPtr &rootlayer,
    const HUSD_LoadMasks *load_masks,
    const UsdStageWeakPtr &context_stage)
{
    UsdStageRefPtr		 stage;

    // We should always be passed a stage from which to copy our asset resolver
    // context. This method is only used by HUSD_LockedStage to create a stage
    // with a file on disk as the root layer. This wrapper function allows us
    // to preserve the resolver context and load masks from the source LOP
    // node.
    UT_ASSERT(context_stage);
    UsdStagePopulationMask stage_mask = load_masks
        ? HUSDgetUsdStagePopulationMask(*load_masks)
        : UsdStagePopulationMask::All();
    if (stage_mask == UsdStagePopulationMask::All())
        stage = UsdStage::Open(rootlayer,
            (context_stage
                ? context_stage->GetPathResolverContext()
                : ArGetResolver().CreateDefaultContext()),
            (load_masks && !load_masks->loadAll())
                ? UsdStage::LoadNone
                : UsdStage::LoadAll);
    else
        stage = UsdStage::OpenMasked(rootlayer,
            context_stage
                ? context_stage->GetPathResolverContext()
                : ArGetResolver().CreateDefaultContext(),
            stage_mask,
            (load_masks && !load_masks->loadAll())
                ? UsdStage::LoadNone
                : UsdStage::LoadAll);

    _ApplyLoadMasksToStage(stage, load_masks);

    return stage;
}

UsdStageRefPtr
HUSDcreateStageFromFile(const UT_StringRef &filepath,
        const HUSD_LoadMasks *load_masks,
        const UsdStageWeakPtr &context_stage)
{
    UsdStageRefPtr		 stage;

    UsdStagePopulationMask stage_mask = load_masks
        ? HUSDgetUsdStagePopulationMask(*load_masks)
        : UsdStagePopulationMask::All();
    if (stage_mask == UsdStagePopulationMask::All())
        stage = UsdStage::Open(filepath.toStdString(),
            (context_stage
                ? context_stage->GetPathResolverContext()
                : ArGetResolver().CreateDefaultContext()),
            (load_masks && !load_masks->loadAll())
                ? UsdStage::LoadNone
                : UsdStage::LoadAll);
    else
        stage = UsdStage::OpenMasked(filepath.toStdString(),
            context_stage
                ? context_stage->GetPathResolverContext()
                : ArGetResolver().CreateDefaultContext(),
            stage_mask,
            (load_masks && !load_masks->loadAll())
                ? UsdStage::LoadNone
                : UsdStage::LoadAll);

    _ApplyLoadMasksToStage(stage, load_masks);

    return stage;
}

void
HUSDcopyMinimalRootPrimMetadata(const SdfLayerRefPtr &dest,
        const SdfLayerHandle &src)
{
    UT_ASSERT(dest);
    if (src)
    {
        SdfPrimSpecHandle destroot = dest->GetPseudoRoot();
        SdfPrimSpecHandle srcroot = src->GetPseudoRoot();
        VtValue destvalue;
        VtValue srcvalue;

        if (destroot && srcroot)
        {
            static const TfTokenVector theMatchStageFields({
                UsdGeomTokens->upAxis,
                UsdGeomTokens->metersPerUnit,
                SdfFieldKeys->FramesPerSecond,
                SdfFieldKeys->TimeCodesPerSecond
            });
            for (auto &&field : theMatchStageFields)
            {
                if (srcroot->HasField(field, &srcvalue))
                {
                    if (!destroot->HasField(field, &destvalue) ||
                        srcvalue != destvalue)
                        destroot->SetInfo(field, srcvalue);
                }
                else if (destroot->HasField(field))
                    destroot->ClearField(field);
            }
        }
    }
}

SdfLayerRefPtr
HUSDcreateAnonymousLayer(
        const SdfLayerHandle &context_layer,
        const std::string &tag)
{
    SdfLayerRefPtr layer;
    std::string loptag = theLopTagPrefix;

    if (!tag.empty())
    {
        loptag += ":";
        loptag += tag;
    }
    layer = SdfLayer::CreateAnonymous(loptag);
    HUSDcopyMinimalRootPrimMetadata(layer, context_layer);

    return layer;
}

SdfLayerRefPtr
HUSDcreateAnonymousCopy(SdfLayerRefPtr srclayer, const std::string &tag)
{
    SdfLayerRefPtr copylayer = HUSDcreateAnonymousLayer(SdfLayerHandle(), tag);

    // Copy the source layer contents.
    copylayer->TransferContent(srclayer);

    // For layers being copied from disk, we need to go through all external
    // references and make them full paths.
    if (!HUSDisLopLayer(srclayer))
    {
        SdfChangeBlock	 changeblock;

        HUSDmodifyAssetPaths(copylayer,
            husd_UpdateReferencesToFullPaths(srclayer));
    }

    return copylayer;
}

SdfLayerRefPtr
HUSDflattenLayerPartitions(const UsdStageWeakPtr &stage,
	int flatten_flags,
	SdfLayerRefPtrVector &explicit_layers)
{
    std::map<std::string, std::string>	 references_map;
    ArResolverContextBinder		 binder(stage->
					    GetPathResolverContext());

    return _FlattenLayerPartitions(stage,
	flatten_flags,
	explicit_layers,
	references_map);
}

SdfLayerRefPtr
HUSDflattenLayers(const UsdStageWeakPtr &stage)
{
    PcpPrimIndex index = stage->GetPseudoRoot().GetPrimIndex();
    return UsdFlattenLayerStack(
        index.GetRootNode().GetLayerStack(),
        _FlattenLayerStackResolveAssetPath);
}

SdfLayerRefPtr
HUSDflattenLayersInChunks(const std::vector<std::string> &sublayers,
        const std::vector<SdfLayerOffset> &sublayeroffsets,
        const UsdStageRefPtr &context_stage)
{
    auto createFlattenedLayer =
        [](const std::vector<std::string> &sublayers,
           const std::vector<SdfLayerOffset> &sublayeroffsets,
           const UsdStageRefPtr &context_stage)
    {
        UsdStageRefPtr       stage_to_flatten;

        stage_to_flatten = HUSDcreateStageInMemory(
            UsdStage::LoadNone, context_stage);

        // Create an error scope as we compose this temporary stage,
        // which exists only as a holder for the layers we wish to
        // flatten together. If there are warnings or errors during
        // this composition, either they are safe to ignore, or they
        // will show up again when we compose the flattened layer is
        // composed onto the main stage.
        {
            UT_ErrorManager          ignore_errors_mgr;
            HUSD_ErrorScope          ignore_errors(&ignore_errors_mgr);

            stage_to_flatten->GetRootLayer()->SetSubLayerPaths(sublayers);
            for (int i = 0, n = sublayers.size(); i < n; i++)
                stage_to_flatten->GetRootLayer()->SetSubLayerOffset(
                    sublayeroffsets[i], i);
        }

        return HUSDflattenLayers(stage_to_flatten);
    };

    // If we have lots of layers to flatten, we are better off breaking them
    // into chunks, flattening the chunks, and then flattening the flattened
    // output of each chunk.
    static const size_t theSubLayerChunkSize = 32;
    if (sublayers.size() > (2 * theSubLayerChunkSize))
    {
        std::vector<std::string> sublayerscopy(sublayers);
        std::vector<SdfLayerOffset> sublayeroffsetscopy(sublayeroffsets);
        std::vector<SdfLayerRefPtr> chunkoutputlayers;

        // If there are many, many layers, we may want to do multiple rounds
        // of chunking.
        while (sublayerscopy.size() >= (2 * theSubLayerChunkSize))
        {
            std::vector<std::vector<std::string>> chunks;
            std::vector<std::vector<SdfLayerOffset>> chunkoffsets;
            std::vector<SdfLayerRefPtr> newchunkoutputlayers;
            std::vector<std::string> newsublayers;
            std::vector<SdfLayerOffset> newsublayeroffsets;
            int chunkcount = (sublayerscopy.size()-1)/theSubLayerChunkSize+1;

            // Prepare input and output vectors for multithreading.
            chunks.resize(chunkcount);
            chunkoffsets.resize(chunkcount);
            newchunkoutputlayers.resize(chunkcount);
            newsublayers.resize(chunkcount);
            newsublayeroffsets.resize(chunkcount);

            // Create the input chunks.
            for (int chunk = 0; chunk < chunkcount; chunk++)
            {
                size_t start = chunk * theSubLayerChunkSize;
                size_t end = start + theSubLayerChunkSize;
                if (end > sublayerscopy.size())
                    end = sublayerscopy.size();
                chunks[chunk].insert(chunks[chunk].end(),
                    sublayerscopy.begin() + start,
                    sublayerscopy.begin() + end);
                chunkoffsets[chunk].insert(chunkoffsets[chunk].end(),
                    sublayeroffsetscopy.begin() + start,
                    sublayeroffsetscopy.begin() + end);
            }

            // Flatten the chunks on multiple threads. Flattening is a single
            // threaded operation (once the stage is composed).
            UTparallelForEachNumber(chunks.size(),
                [&](const UT_BlockedRange<size_t> &range)
            {
                for (size_t i = range.begin(); i != range.end(); ++i)
                {
                    SdfLayerRefPtr slice = createFlattenedLayer(
                        chunks[i],
                        chunkoffsets[i],
                        context_stage);
                    newchunkoutputlayers[i] = slice;
                    newsublayers[i] = slice->GetIdentifier();
                    newsublayeroffsets[i] = SdfLayerOffset();
                }
            });

            // Swap the original arrays (or the results of the last round of
            // chunking) with the results of the current round of chunking.
            sublayerscopy.swap(newsublayers);
            sublayeroffsetscopy.swap(newsublayeroffsets);
            chunkoutputlayers.swap(newchunkoutputlayers);
        }

        // One final flattening of the flattened chunks.
        return createFlattenedLayer(sublayerscopy,
            sublayeroffsetscopy,
            context_stage);
    }

    // If we didn't need to create chunks, just do a simple flattening.
    return createFlattenedLayer(sublayers, sublayeroffsets, context_stage);
}

bool
HUSDisLayerEmpty(const SdfLayerHandle &layer,
        const UsdStageRefPtr &compare_stage_root_prim,
        bool ignore_sublayers)
{
    // If the layer has a sublayer path or more than one root prim, it's
    // not empty.
    if (!(layer->GetSubLayerPaths().empty() || ignore_sublayers) ||
	layer->GetRootPrims().size() > 1 ||
	layer->GetRootPrimOrder().size() > 1)
	return false;

    // If it has no root prims (and we already know it has no sublayers),
    // it is empty.
    if (layer->GetRootPrims().size() == 0)
        return true;

    // The layer has no sublayers, and only one prim. Check if it is our
    // layer info prim.
    SdfPrimSpecHandle		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    // The one prim isn't our layer info prim. It's not empty.
    if (!infoprim)
	return false;

    std::vector<TfToken>	 fields = infoprim->ListFields();

    // The layer info prim has more than three field. It's not empty.
    if (fields.size() > 3)
	return false;

    // If the layer info prim has no fields, consider it empty.
    if (fields.size() > 0)
    {
	// The info prim has one or two fields, but it's not Custom Data
	// and not a Specifier. The layer isn't empty.
	for (auto &&field : fields)
	    if (field != SdfFieldKeys->CustomData &&
		field != SdfFieldKeys->Specifier &&
		field != SdfFieldKeys->TypeName)
		return false;

	auto	 data = infoprim->GetCustomData();

	// Any custom data other than a creator node and the flag indicating
        // whether the layer should be treated as a SOP layer, treat the layer
        // as not empty.
	for (auto it : data)
	{
	    if (it.first != HUSDgetCreatorNodeToken() &&
                it.first != HUSDgetTreatAsSopLayerToken())
		return false;
	}
    }

    // If the root prim has any data on it, this layer is not
    // empty. The user set that data for a reason.
    SdfPrimSpecHandle		 pseudoroot = layer->GetPseudoRoot();
    if (pseudoroot)
    {
        SdfPrimSpecHandle        stageroot;
	auto                     fields = pseudoroot->ListFields();

        if (compare_stage_root_prim)
            stageroot = compare_stage_root_prim->
                GetRootLayer()->GetPseudoRoot();

        if (stageroot)
        {
            for (auto &&field: fields)
            {
                if (field == SdfChildrenKeys->PrimChildren)
                    continue;

                VtValue          layervalue = pseudoroot->GetField(field);
                VtValue          stagevalue;

                // If the stage root prim doesn't have the that is on the
                // layer root prim, or if the values don't match, then the
                // layer shouldn't be considered empty. We copy a number of
                // root prim metadata values from the stage to new layers in
                // HUSDcreateAnonymousLayer.
                if (!stageroot->HasField(field, &stagevalue) ||
                    stagevalue != layervalue)
                    return false;
            }
        }
        else
        {
            // The only field the root layer is allowed to have is a list of
            // prim children, which is the infoprim that must exist for us to
            // have gotten this far.
            if (fields.size() > 1 ||
                (fields.size() == 1 &&
                 fields[0] != SdfChildrenKeys->PrimChildren))
                return false;
        }
    }

    // Passed through all the tests. We have only one prim, it is the layer
    // info prim, and it has only the node creator custom data on it. This is
    // as empty as a LOP layer gets.
    return true;
}

bool
HUSDisLayerPlaceholder(const SdfLayerHandle &layer)
{
    if (HUSDisLopLayer(layer))
    {
	std::string	 save_control;

	if (HUSDgetSaveControl(layer, save_control) &&
	    HUSD_Constants::getSaveControlPlaceholder() == save_control)
	    return true;
    }

    return false;
}

std::string
HUSDgetLopLayerMutingIdentifier(const SdfLayerHandle &layer)
{
    std::string loppath;

    if (layer)
    {
        std::string loppathstr;
        if (HUSDgetCreatorNode(layer, loppathstr))
            loppath = std::string(HUSD_LOP_MUTING_IDENTIFIER_PREFIX) +
                loppathstr;
    }

    return loppath;
}

bool
HUSDisLayerPlaceholder(const std::string &identifier)
{
    if (HUSDisLopLayer(identifier))
    {
        SdfLayerRefPtr	 srclayer = SdfLayer::Find(identifier);

        if (srclayer && HUSDisLayerPlaceholder(srclayer))
            return true;
    }

    return false;
}

bool
HUSDisStageVariableExpression(const UT_StringRef &expr,
        bool check_for_errors)
{
    std::string exprstr = expr.toStdString();
    if (SdfVariableExpression::IsExpression(exprstr))
    {
        if (check_for_errors)
        {
            SdfVariableExpression expr(exprstr);
            auto result = expr.Evaluate(VtDictionary());
            if (!result.errors.empty())
            {
                UT_StringArray errors;
                UT_WorkBuffer buf;
                UTarrayFromStdVectorOfStrings(errors, result.errors);
                buf.append(errors, "\n    ");
                HUSD_ErrorScope::addError(
                    HUSD_ERR_INVALID_VARIABLE_EXPRESSION, buf.buffer());
            }
        }

        return true;
    }

    return false;
}

SdfPath
HUSDgetBestRefPrimPath(const UT_StringRef &reffilepath,
        const SdfFileFormat::FileFormatArguments &args,
        const UT_StringRef &refprimpath,
        UsdStageRefPtr &stage)
{
    // We have been given a specific primitive path.
    if (refprimpath.isstring() &&
        refprimpath != HUSD_Constants::getAutomaticPrimIdentifier() &&
        refprimpath != HUSD_Constants::getDefaultPrimIdentifier())
        return HUSDgetSdfPath(refprimpath);

    // If the file path is a stage variable expression, we can't accurately
    // evaluate the expression to find the layer, so just assume that the
    // user means the default prim path (and accept that we can't test for
    // errors like a layer with no default prim path).
    if (HUSDisStageVariableExpression(reffilepath, true))
        return SdfPath();

    ArResolverContextBinder binder(
        stage ? stage->GetPathResolverContext() : ArResolverContext());
    SdfLayerRefPtr layer = SdfLayer::Find(reffilepath.toStdString(), args);
    std::string layerid = layer ? layer->GetIdentifier() : std::string();
    SdfPath bestpath;

    if (layer)
    {
        if (refprimpath == HUSD_Constants::getAutomaticPrimIdentifier())
        {
            auto it = theKnownAutomaticPrims.find(layerid);
            if (it != theKnownAutomaticPrims.end())
                return it->second;
        }
        else if (refprimpath == HUSD_Constants::getDefaultPrimIdentifier())
        {
            auto it = theKnownDefaultPrims.find(layerid);
            if (it != theKnownDefaultPrims.end())
                return it->second;
        }
    }
    else
        layer = SdfLayer::FindOrOpen(reffilepath.toStdString(), args);

    // If we found or opened the layer, build a stage from it. Otherwise
    // return immediately. USD will generate some kind of error when it
    // can't open the requested layer.
    if (layer)
        stage = UsdStage::Open(layer, UsdStage::LoadAll);

    if (!layer || !stage)
        return bestpath;

    layerid = layer->GetIdentifier();
    if (stage->GetDefaultPrim())
    {
        // We have been asked to use the automatic or default prim, and there
        // is a valid default prim. Use it.
        theKnownDefaultPrims[layerid] = bestpath;
        return bestpath;
    }
    else if (refprimpath == HUSD_Constants::getDefaultPrimIdentifier())
    {
        // We have been asked to explicitly use the default primitive, but
        // there isn't a valid one set. Raise an informative error message if
        // this is going to be a problem, but return it anyway.
        if (!stage->GetDefaultPrim())
            HUSD_ErrorScope::addWarning(
                HUSD_ERR_DEFAULT_PRIM_IS_MISSING,
                reffilepath.c_str());

        theKnownDefaultPrims[layerid] = bestpath;
        return bestpath;
    }

    if (stage->GetPseudoRoot())
    {
        // We have been asked to pick a prim automatically, and there is no
        // default prim. Loop through our root prims looking for something
        // suitable.
        static const TfType &thePreferredBaseType =
            HUSDfindType("UsdGeomXformable");
        bool foundxformroot = false;
        int rootprimcount = 0;

        for (auto &&rootprim : stage->GetPseudoRoot().GetChildren())
        {
            UT_StringHolder primtypename = rootprim.GetTypeName().GetString();

            // Ignore HoudiniLayerInfo prims if there are any.
            if (primtypename != HUSD_Constants::getHoudiniLayerInfoPrimType())
            {
                const TfType &primtype = HUSDfindType(primtypename);

                // We found a root prim. If we have found an xform or scope
                // already, then we can exit this loop because we have nothign
                // else to learn here.
                rootprimcount++;
                if (rootprimcount > 1 && foundxformroot)
                    break;

                // The first xform prim is what we prefer over any other
                // primitive type. But until we find one, we accept the first
                // primitive, and keep looking.
                if (primtype.IsA(thePreferredBaseType))
                {
                    foundxformroot = true;
                    bestpath = rootprim.GetPath();
                }
                else if (bestpath.IsEmpty())
                    bestpath = rootprim.GetPath();
            }
        }

        // Add a warning if we chose a primitive, but there were other
        // valid choices (and so we may be missing information from the
        // referenced stage). In this case the user really should be
        // explicitly specifying the primitive they are interested in.
        if (rootprimcount > 1)
        {
            UT_WorkBuffer buf;

            buf.sprintf("'%s' in '%s'",
                bestpath.GetString().c_str(), reffilepath.c_str());
            HUSD_ErrorScope::addWarning(
                HUSD_ERR_AUTO_REFERENCE_MISSES_SOME_DATA,
                buf.buffer());
        }
    }

    if (refprimpath == HUSD_Constants::getAutomaticPrimIdentifier())
        theKnownAutomaticPrims[layerid] = bestpath;
    else if (refprimpath == HUSD_Constants::getDefaultPrimIdentifier())
        theKnownDefaultPrims[layerid] = bestpath;

    return bestpath;
}

void
HUSDclearBestRefPathCache(const std::string &layeridentifier)
{
    if (!layeridentifier.empty())
    {
        theKnownAutomaticPrims.erase(layeridentifier);
        theKnownDefaultPrims.erase(layeridentifier);
    }
    else
    {
        theKnownAutomaticPrims.clear();
        theKnownDefaultPrims.clear();
    }
}

static inline HUSD_TimeSampling
husdGetTimeSampling( exint num_of_samples )
{
    if( num_of_samples <= 0 )
	return HUSD_TimeSampling::NONE;
    if( num_of_samples == 1 )
	return HUSD_TimeSampling::SINGLE;
    return HUSD_TimeSampling::MULTIPLE;
}

HUSD_TimeSampling
HUSDgetLocalTransformTimeSampling(const UsdPrim &prim, bool *resets)
{
    HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;

    bool unused = false;
    if (resets)
        *resets = false;
    else
        resets = &unused;

    if (UsdGeomXformable xformable{prim})
    {
        for (auto &&op : xformable.GetOrderedXformOps(resets))
        {
            HUSDupdateTimeSampling(time_sampling,
                                   HUSDgetValueTimeSampling(op.GetAttr()));
        }
    }
   
    return time_sampling;
}

HUSD_TimeSampling
HUSDgetValueTimeSampling(const UsdAttribute &attrib)
{
    if (!attrib)
	return HUSD_TimeSampling::NONE;

    // The ValueMightBeTimeVarying function can be faster than actually
    // getting the number of time samples.
    if (attrib.ValueMightBeTimeVarying())
        return HUSD_TimeSampling::MULTIPLE;

    return husdGetTimeSampling(attrib.GetNumTimeSamples());
}

HUSD_TimeSampling
HUSDgetValueTimeSampling(const UsdGeomPrimvar &primvar)
{
    HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;

    HUSDupdateValueTimeSampling( time_sampling, primvar );
    return time_sampling;
}

HUSD_TimeSampling
HUSDgetWorldTransformTimeSampling(const UsdPrim &prim)
{
    HUSD_TimeSampling	time_sampling = HUSD_TimeSampling::NONE;
    UsdPrim		testprim(prim);
    bool		resets = false;

    while (testprim)
    {
	HUSDupdateTimeSampling(time_sampling,
            HUSDgetLocalTransformTimeSampling(testprim, &resets));
	
	// If we hit a transform that resets the transform stack, we can
	// stop looking for time time-sampled transforms on ancestors, since
	// they will have no impact.
	// Also if we've reached max level of sampling, we can bail out.
	if (resets || time_sampling == HUSD_TimeSampling::MULTIPLE)
	    break;

	testprim = testprim.GetParent();
    }

    return time_sampling;
}

void
HUSDupdateTimeSampling(HUSD_TimeSampling &sampling,
	HUSD_TimeSampling new_sampling )
{
    if (new_sampling > sampling)
	sampling = new_sampling;
}

void
HUSDupdateValueTimeSampling( HUSD_TimeSampling &sampling,
	const UsdAttribute &attrib)
{
    HUSDupdateTimeSampling( sampling, HUSDgetValueTimeSampling( attrib ));
}

void
HUSDupdateValueTimeSampling( HUSD_TimeSampling &sampling,
	const UsdGeomPrimvar &primvar)
{
    if(primvar.IsIndexed())
	HUSDupdateValueTimeSampling(sampling, primvar.GetIndicesAttr());

    HUSDupdateValueTimeSampling( sampling, primvar.GetAttr() );
}

void
HUSDupdateLocalTransformTimeSampling(HUSD_TimeSampling &sampling,
	const UsdPrim &prim)
{
    HUSDupdateTimeSampling( sampling, HUSDgetLocalTransformTimeSampling(prim));
}

void
HUSDupdateWorldTransformTimeSampling(HUSD_TimeSampling &sampling,
	const UsdPrim &prim)
{
    HUSDupdateTimeSampling( sampling, HUSDgetWorldTransformTimeSampling(prim));
}

bool
HUSDvalueMightBeTimeVarying(const UsdAttribute &attrib)
{
    return attrib && attrib.ValueMightBeTimeVarying();
}

bool
HUSDlocalTransformMightBeTimeVarying(const UsdPrim &prim)
{
    UsdGeomXformable xformable(prim);
    if( !xformable )
	return false;

    // Note, it's equivalent to GetNumTimeSamples() > 1, but faster.
    return xformable.TransformMightBeTimeVarying();
}

VtValue
HUSDoptionToVtValue(const UT_OptionEntry *option)
{
    if (!option)
        return VtValue();

    switch (option->getType())
    {
        case UT_OPTION_BOOL:
            return VtValue(option->getOptionB());

        case UT_OPTION_INT:
            return VtValue(option->getOptionI());

        case UT_OPTION_INTARRAY:
        {
            auto &data = option->getOptionIArray();
            if(data.entries() == 1)
                return VtValue(data(0));
            else if(data.entries() == 2)
                return VtValue(GfVec2i(data(0), data(1)));
            else if(data.entries() == 3)
                return VtValue(GfVec3i(data(0), data(1), data(2)));
            else if(data.entries() == 4)
                return VtValue(GfVec4i(data(0), data(1), data(2), data(3)));
            else
            {
                VtArray<int> array;
                for(double v : data)
                    array.push_back(v);
                return VtValue(array);
            }
        }

        case UT_OPTION_FPREAL:
            return VtValue(option->getOptionF());

        case UT_OPTION_FPREALARRAY:
        {
            auto &data = option->getOptionFArray();
            switch (data.entries())
            {
                case 1:
                    return VtValue(data(0));
                case 2:
                    return VtValue(GfVec2d(data(0), data(1)));
                case 3:
                    return VtValue(GfVec3d(data(0), data(1), data(2)));
                case 4:
                    return VtValue(GfVec4d(data(0), data(1), data(2), data(3)));
                case 9:
                    return VtValue(GfMatrix3d(
                        data(0),data(1),data(2),
                        data(3),data(4),data(5),
                        data(6),data(7),data(8)));
                case 16:
                    return VtValue(GfMatrix4d(
                        data(0),data(1),data(2),data(3),
                        data(4),data(5),data(6),data(7),
                        data(8),data(9),data(10),data(11),
                        data(12),data(13),data(14),data(15)));
                default:
                {
                    VtArray<double> array;
                    for(double v : data)
                        array.push_back(v);
                    return VtValue(array);
                }
            }
            break;
        }

        case UT_OPTION_STRING:
            return VtValue(option->getOptionS().toStdString());

        case UT_OPTION_VECTOR2:
        case UT_OPTION_UV:
        {
            UT_Vector2D	v2;
            UT_VERIFY(option->importOption(v2));
            return VtValue(GfVec2d(v2.x(), v2.y()));
        }

        case UT_OPTION_VECTOR3:
        case UT_OPTION_UVW:
        {
            UT_Vector3D	v3;
            UT_VERIFY(option->importOption(v3));
            return VtValue(GfVec3d(v3.x(), v3.y(), v3.z()));
        }

        case UT_OPTION_VECTOR4:
        {
            UT_Vector4D	v4;
            UT_VERIFY(option->importOption(v4));
            return VtValue(GfVec4d(v4.x(), v4.y(), v4.z(), v4.w()));
        }

        default:
            UTdebugFormat("Unhandled option type: {}", int(option->getType()));
            return VtValue();
    }
}

void
HUSDgetMinimalPathsForInheritableProperty(
        bool skip_point_instancers,
        const UsdStageRefPtr &stage,
        XUSD_PathSet &paths)
{
    for (auto it = paths.begin(); it != paths.end();)
    {
        auto	 prim = stage->GetPrimAtPath(*it);
        auto     childit = it;
        bool     incrementit = true;

        // Remove from the set any children of the current entry.
        childit++;
        while (childit != paths.end() && childit->HasPrefix(*it))
            childit = paths.erase(childit);

        if (prim && !prim.IsPseudoRoot() )
        {
            auto     parent = prim.GetParent();

            if (parent && !parent.IsPseudoRoot())
            {
                bool         missingsibling = false;

                // Our parent shouldn't be in the set, because we would have
                // removed this path already if our parent was present.
                UT_ASSERT(paths.count(parent.GetPath()) == 0);
                for (auto sibling : parent.GetChildren())
                {
                    if (paths.find(sibling.GetPath()) == paths.end() ||
                        (skip_point_instancers &&
                         sibling.IsA<UsdGeomPointInstancer>()))
                    {
                        missingsibling = true;
                        break;
                    }
                }

                if (!missingsibling)
                {
                    // All children of our parent are present. Add an entry
                    // for our parent, and remove all entries that have
                    // this parent as a prefix. Next iteration we will check
                    // if this new parent entry now has all its siblings in
                    // the set.
                    auto childit = paths.insert(parent.GetPath()).first;
                    it = childit++;
                    while (childit != paths.end() &&
                           childit->HasPrefix(parent.GetPath()))
                        childit = paths.erase(childit);
                    incrementit = false;
                }
            }
        }

        if (incrementit)
            ++it;
    }
}

void
HUSDgetMinimalMostNestedPathsForInheritableProperty(
    const UsdStageRefPtr &stage,
    XUSD_PathSet &paths)
{
    for (auto it = paths.begin(); it != paths.end();)
    {
        auto	 prim = stage->GetPrimAtPath(*it);
        auto     childit = it;
        bool     incrementit = true;

        // See if we have any descendants in the set. If not, we definitely
        // want to keep this entry in the set.
        childit++;
        if (prim && !prim.IsPseudoRoot() &&
            (childit != paths.end() && childit->HasPrefix(*it)))
        {
            bool missingchild = false;

            for (auto child : prim.GetChildren())
            {
                if (paths.find(child.GetPath()) == paths.end())
                {
                    missingchild = true;
                    break;
                }
            }

            if (!missingchild)
            {
                // All children of this prim are present. Remove this prim
                // from the set. At this point we will have already done this
                // test for our parent, so we don't need to worry about the
                // impact of this removal on the removal of our parent.
                it = paths.erase(it);
                incrementit = false;
            }
        }

        if (incrementit)
            ++it;
    }
}

void
HUSDgenerateUniqueTransformOpSuffix(
        UT_StringHolder &suffix,
        const UsdGeomXformable &xformable,
        UsdGeomXformOp::Type type /*=UsdGeomXformOp::TypeTransform*/,
        bool test_base_xform /*=false*/)
{
    // The choice of UsdGeomXformable::GetPrim().HasAttribute() here
    // (instead of UsdGeomXformable::GetXformOpOrderAttr() and other such APIs)
    // is intentional to support the definition of "unique" explained in the
    // header file.
    //
    // If this implementation should ever be changed, please ensure the header
    // file is similarly updated to reflect the behaviour and motivation for it.
    
    if (test_base_xform)
    {
        TfToken opName = UsdGeomXformOp::GetOpName(type);
        if (!xformable.GetPrim().HasAttribute(opName))
        {
            suffix.clear();
            return;
        }
    }

    UT_String tmp_suffix(suffix);
    while (true)
    {
        TfToken opName = UsdGeomXformOp::GetOpName(type, TfToken(tmp_suffix.c_str()));
        if (!xformable.GetPrim().HasAttribute(opName))
        {
            break;
        }
        tmp_suffix.incrementNumberedName(true);
    }
    suffix = tmp_suffix;
}

void
HUSDconvertToFileFormatArguments(
        const UT_StringMap<UT_StringHolder> &ut_args,
        SdfFileFormat::FileFormatArguments &sdf_args)
{
    for (auto &&it : ut_args)
    {
        UT_StringHolder key = it.first;
        UT_StringHolder value = it.second;

        sdf_args[key.toStdString()] = value.toStdString();
    }
}

HUSD_TimeSampling
HUSDgetBoundsTimeSampling(const UsdPrim& prim, bool world_space_bounds)
{
    auto updateTimeSampling = [](const UsdPrim &testprim,
                                 bool test_xform,
                                 HUSD_TimeSampling &out_time_sampling) {
        // this test is copied (somewhat) from bboxCache, which seems to
        // assume only a subset of attributes can actually change the extents
        if (UsdGeomImageable imageable{testprim})
        {
            HUSDupdateTimeSampling(out_time_sampling,
                HUSDgetValueTimeSampling(imageable.GetVisibilityAttr()));
            if (test_xform)
                HUSDupdateTimeSampling(out_time_sampling,
                    HUSDgetLocalTransformTimeSampling(testprim));
            if (UsdGeomBoundable boundable{testprim})
                HUSDupdateTimeSampling(out_time_sampling,
                    HUSDgetValueTimeSampling(boundable.GetExtentAttr()));
            else if (UsdGeomModelAPI modelapi{testprim})
                HUSDupdateTimeSampling(out_time_sampling,
                    HUSDgetValueTimeSampling(modelapi.GetExtentsHintAttr()));
        }
    };

    HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;

    if (world_space_bounds)
    {
        updateTimeSampling(prim, false, time_sampling);
        HUSDupdateTimeSampling(time_sampling,
            HUSDgetWorldTransformTimeSampling(prim));
    }
    for (auto child : prim.GetDescendants())
    {
        if (time_sampling >= HUSD_TimeSampling::MULTIPLE)
            break;
        updateTimeSampling(child, true, time_sampling);
    }
    return time_sampling;
}

namespace {
#define SCALAR_VALUE(TYPE) \
    if (v.IsHolding<TYPE>()) { iv = v.UncheckedGet<TYPE>(); return true; } \
    /* end macro */
#define STRING_VALUE(TYPE, CONVERT) \
    if (v.IsHolding<TYPE>()) { \
        const auto &s = v.UncheckedGet<TYPE>(); \
        iv = CONVERT;  \
        return true; \
    } \
    /* end macro */
#define VEC_VALUE(TYPE) \
    if (v.IsHolding<TYPE>()) { \
        TYPE tmp = v.UncheckedGet<TYPE>(); \
        std::copy(tmp.data(), tmp.data()+T::tuple_size, iv.data()); \
        return true; \
    } \
    /* end macro */
#define VEC_COPY_VALUE(TYPE) \
    if (v.IsHolding<TYPE>()) { \
        TYPE tmp = v.UncheckedGet<TYPE>(); \
        for (int i = 0; i < T::tuple_size; ++i) { iv.data()[i] = tmp[i]; } \
        return true; \
    } \
    /* end macro */
#define ARRAY_VALUE(TYPE, ATYPE, GETVAL) \
    if (v.IsHolding<ATYPE<TYPE>>()) { \
        const ATYPE<TYPE>       &arr = v.UncheckedGet<ATYPE<TYPE>>(); \
        for (auto &&item : arr) \
            iv.append(GETVAL); \
        return true; \
    } \
    /* end macro */

    template <typename T>
    static bool
    intValue(T &iv, const VtValue &v)
    {
        SCALAR_VALUE(int32)
        SCALAR_VALUE(int64)
        SCALAR_VALUE(uint32)
        SCALAR_VALUE(uint64)
#if defined(MBSD)
        SCALAR_VALUE(unsigned long)	// OSX has different types for uint32 and uint64
#endif
        SCALAR_VALUE(bool)
        SCALAR_VALUE(int16)
        SCALAR_VALUE(uint16)
        SCALAR_VALUE(int8)
        SCALAR_VALUE(uint8)
        return false;
    }

    template <typename T>
    static bool
    realValue(T &iv, const VtValue &v)
    {
        SCALAR_VALUE(fpreal32)
        SCALAR_VALUE(fpreal64)
        SCALAR_VALUE(fpreal16)
        return intValue(iv, v);
    }

    static bool
    stringValue(UT_StringHolder &iv, const VtValue &v)
    {
        STRING_VALUE(std::string, UT_StringHolder(s))
        STRING_VALUE(TfToken, s.GetText())
        STRING_VALUE(UT_StringHolder, s)
        STRING_VALUE(SdfPath, s.GetAsString())
        return false;
    }

    static bool
    stringArray(UT_StringArray &iv, const VtValue &v)
    {
        ARRAY_VALUE(std::string, VtArray, UT_StringHolder(item))
        ARRAY_VALUE(TfToken, VtArray, UT_StringHolder(item.GetText()))
        ARRAY_VALUE(SdfPath, VtArray, UT_StringHolder(item.GetAsString()));
        ARRAY_VALUE(UT_StringHolder, VtArray, item);
        ARRAY_VALUE(UT_StringHolder, UT_Array, item);
        return false;
    }
    static bool
    intArray(UT_Int64Array &iv, const VtValue &v)
    {
        ARRAY_VALUE(int32, VtArray, item)
        ARRAY_VALUE(int64, VtArray, item)
        ARRAY_VALUE(uint32, VtArray, item)
        ARRAY_VALUE(uint64, VtArray, item)
        return false;
    }
    static bool
    realArray(UT_Fpreal64Array &iv, const VtValue &v)
    {
        ARRAY_VALUE(fpreal32, VtArray, item)
        ARRAY_VALUE(fpreal64, VtArray, item)
        return false;
    }

    template <typename T> static bool
    v2value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfVec2i);
        VEC_VALUE(GfVec2f);
        VEC_VALUE(GfVec2d);
        VEC_COPY_VALUE(GfSize2);
        return false;
    }

    template <typename T> static bool
    v3value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfVec3i);
        VEC_VALUE(GfVec3f);
        VEC_VALUE(GfVec3d);
        VEC_COPY_VALUE(GfSize3);
        return false;
    }

    template <typename T> static bool
    v4value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfVec4i);
        VEC_VALUE(GfVec4f);
        VEC_VALUE(GfVec4d);
        return false;
    }

    template <typename T> static bool
    m2value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfMatrix2f);
        VEC_VALUE(GfMatrix2d);
        return false;
    }

    template <typename T> static bool
    m3value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfMatrix3f);
        VEC_VALUE(GfMatrix3d);
        return false;
    }

    template <typename T> static bool
    m4value(T &iv, const VtValue &v)
    {
        VEC_VALUE(GfMatrix4f);
        VEC_VALUE(GfMatrix4d);
        return false;
    }

    static void
    setOption(UT_Options &opts, const UT_StringHolder &key, const VtValue &v,
            const UT_StringMap<UT_StringHolder> *aliases)
    {
        int64               iv;
        fpreal64            fv;
        UT_StringHolder     sv;
        UT_Vector2D         v2;
        UT_Vector3D         v3;
        UT_Vector4D         v4;
        UT_Matrix2D         m2;
        UT_Matrix3D         m3;
        UT_Matrix4D         m4;
        UT_StringArray      sa;
        UT_Int64Array       ia;
        UT_Fpreal64Array    fa;

        if (intValue(iv, v))
            opts.setOptionI(key, iv);
        else if (realValue(fv, v))
            opts.setOptionF(key, fv);
        else if (stringValue(sv, v))
            opts.setOptionS(key, sv);
        else if (v2value(v2, v))
            opts.setOptionV2(key, v2);
        else if (v3value(v3, v))
            opts.setOptionV3(key, v3);
        else if (v4value(v4, v))
            opts.setOptionV4(key, v4);
        else if (m2value(m2, v))
            opts.setOptionM2(key, m2);
        else if (m3value(m3, v))
            opts.setOptionM3(key, m3);
        else if (m4value(m4, v))
            opts.setOptionM4(key, m4);
        else if (intArray(ia, v))
            opts.setOptionIArray(key, ia);
        else if (realArray(fa, v))
            opts.setOptionFArray(key, fa);
        else if (stringArray(sa, v))
            opts.setOptionSArray(key, sa);
        else if (v.IsHolding<VtDictionary>())
        {
            UT_OptionsHolder    dict;
            HUSDconvertDictionary(*dict.makeUnique(),
                    v.UncheckedGet<VtDictionary>(),
                    aliases);
            opts.setOptionDict(key, dict);
        }
        else if (v.IsHolding<VtArray<VtDictionary>>())
        {
            const VtArray<VtDictionary> &arr =
                        v.UncheckedGet<VtArray<VtDictionary>>();
            UT_Array<UT_OptionsHolder>  dicts(arr.size(), arr.size());
            for (exint i = 0, n = arr.size(); i < n; ++i)
                HUSDconvertDictionary(*dicts[i].makeUnique(), arr[i], aliases);
            opts.setOptionDictArray(key, dicts);
        }
        else
        {
            UT_OStringStream    sos;
            sos << v << std::ends;
            opts.setOptionS(key, sos.str());
        }
    }

    template <typename T> static bool
    jsonScalar(UT_JSONWriter &w, const T &v)
    {
        return w.jsonValue(v);
    }
    template <> bool
    jsonScalar<uint64>(UT_JSONWriter &w, const uint64 &v)
    {
        return w.jsonValue(int64(v));
    }

    template <typename T> static bool
    jsonQuaternion(UT_JSONWriter &w, const T &q)
    {
        const auto &im = q.GetImaginary();
        typename T::ScalarType  vals[4];
        vals[0] = q.GetReal();
        vals[1] = im[0];
        vals[2] = im[0];
        vals[3] = im[0];
        return w.jsonUniformArray(4, vals);
    }
    template <> bool
    jsonScalar<GfQuatf>(UT_JSONWriter &w, const GfQuatf &q)
    {
        return jsonQuaternion(w, q);
    }
    template <> bool
    jsonScalar<GfQuatd>(UT_JSONWriter &w, const GfQuatd &q)
    {
        return jsonQuaternion(w, q);
    }
    template <> bool
    jsonScalar<std::string>(UT_JSONWriter &w, const std::string &s)
    {
        return w.jsonValue(s);
    }
    template <> bool
    jsonScalar<TfToken>(UT_JSONWriter &w, const TfToken &s)
    {
        return w.jsonValue(s.GetText());
    }
    template <> bool
    jsonScalar<SdfPath>(UT_JSONWriter &w, const SdfPath &s)
    {
        return w.jsonValue(s.GetAsString());
    }

#define JSON_VECTOR(TYPE, SIZE) \
    template <> bool \
    jsonScalar<TYPE>(UT_JSONWriter &w, const TYPE &v) { \
        return w.jsonUniformArray(SIZE, v.data()); \
    } \
    /* end macro */
JSON_VECTOR(GfVec2i, 2)
JSON_VECTOR(GfVec2f, 2)
JSON_VECTOR(GfVec2d, 2)
JSON_VECTOR(GfVec3i, 3)
JSON_VECTOR(GfVec3f, 3)
JSON_VECTOR(GfVec3d, 3)
JSON_VECTOR(GfVec4i, 4)
JSON_VECTOR(GfVec4f, 4)
JSON_VECTOR(GfVec4d, 4)
JSON_VECTOR(GfMatrix2f, 4)
JSON_VECTOR(GfMatrix2d, 4)
JSON_VECTOR(GfMatrix3f, 9)
JSON_VECTOR(GfMatrix3d, 9)
JSON_VECTOR(GfMatrix4f, 16)
JSON_VECTOR(GfMatrix4d, 16)

    bool saveJSONValue(UT_JSONWriter &w, const VtValue &v);

    // Now something crazy
    template <> bool
    jsonScalar<VtDictionary>(UT_JSONWriter &w, const VtDictionary &v)
    {
        bool    ok = w.jsonBeginMap();
        for (const auto &item : v)
        {
            ok = ok && w.jsonKey(UT_StringHolder(item.first));
            ok = ok && saveJSONValue(w, item.second);
            if (!ok)
                break;
        }
        return ok && w.jsonEndMap();
    }

    template <typename T> static bool
    jsonScalarArray(UT_JSONWriter &w, const VtArray<T> &v)
    {
        bool ok = w.jsonBeginArray();
        for (exint i = 0, n = v.size(); ok && i < n; ++i)
            ok = ok && jsonScalar(w, v[i]);
        return ok && w.jsonEndArray();
    }

    bool
    saveJSONValue(UT_JSONWriter &w, const VtValue &v)
    {
        if (v.IsEmpty())
            return w.jsonNull();
#define SCALAR(CTYPE) \
        if (v.IsHolding<CTYPE>()) \
            return jsonScalar(w, v.UncheckedGet<CTYPE>()); \
        if (v.IsHolding<VtArray<CTYPE>>()) \
            return jsonScalarArray(w, v.UncheckedGet<VtArray<CTYPE>>()); \
        /* end macro */
        SCALAR(bool);
        SCALAR(int8);
        SCALAR(uint8);
        SCALAR(int16);
        SCALAR(uint16);
        SCALAR(int32);
        SCALAR(uint32);
        SCALAR(int64);
        SCALAR(uint64);
        SCALAR(fpreal32);
        SCALAR(fpreal64);
        SCALAR(UT_StringHolder);
        SCALAR(std::string);
        SCALAR(TfToken);
        SCALAR(SdfPath);
        SCALAR(GfVec2i)
        SCALAR(GfVec2f)
        SCALAR(GfVec2d)
        SCALAR(GfVec3i)
        SCALAR(GfVec3f)
        SCALAR(GfVec3d)
        SCALAR(GfVec4i)
        SCALAR(GfVec4f)
        SCALAR(GfVec4d)
        SCALAR(GfMatrix2f)
        SCALAR(GfMatrix2d)
        SCALAR(GfMatrix3f)
        SCALAR(GfMatrix3d)
        SCALAR(GfMatrix4f)
        SCALAR(GfMatrix4d)
        SCALAR(VtDictionary)
#undef SCALAR
        // Now we've done the easy cases, we need to handle some more
        // complicated types.
        if (v.IsHolding<VtArray<VtValue>>())
        {
            const VtArray<VtValue> &arr = v.UncheckedGet<const VtArray<VtValue>>();
            bool ok = w.jsonBeginArray();
            for (exint i = 0, n = arr.size(); i < n; ++i)
                ok = ok && saveJSONValue(w, arr[i]);
            return ok && w.jsonEndArray();
        }

        UTdebugFormat("Unhandled type: {}", v.GetType().GetTypeName());
        UT_OStringStream    sos;
        sos << v;       // Don't include the '\0'
        return w.jsonString(sos.str().buffer(), sos.str().length());
    }
}

bool
HUSDconvertDictionary(UT_Options &opts,
        const VtDictionary &dict,
        const UT_StringMap<UT_StringHolder> *aliases)
{
    opts.clear();
    UT_StringMap<UT_StringHolder>::const_iterator     alias;
    for (const auto &item : dict)
    {
        UT_StringHolder key(item.first);
        const VtValue   &v = item.second;
        if (aliases && (alias = aliases->find(key)) != aliases->end())
            key = alias->second;
        setOption(opts, key, v, aliases);
    }
    return true;
}

bool
HUSDconvertDictionary(UT_JSONWriter &w,
        const VtDictionary &dict,
        const UT_StringMap<UT_StringHolder> *aliases)
{
    UT_StringMap<UT_StringHolder>::const_iterator     alias;
    bool    ok = w.jsonBeginMap();
    for (const auto &item : dict)
    {
        UT_StringRef key = UT_StringRef(item.first);
        if (aliases && (alias = aliases->find(key)) != aliases->end())
            key = alias->second;
        ok = ok && w.jsonKey(key);
        ok = ok && saveJSONValue(w, item.second);
        if (!ok)
            break;
    }
    return ok && w.jsonEndMap();
}

bool
HUSDconvertValue(UT_JSONWriter &w, const VtValue &value)
{
    return saveJSONValue(w, value);
}

void
HUSDconvertVariantSelectionFallbacks(
        const UT_StringMap<UT_StringArray> &utfallbacks,
        PcpVariantFallbackMap &fallbacks)
{
    for (auto &&it : utfallbacks)
    {
        std::vector<std::string> stdarray;
        UTarrayToStdVectorOfStrings(it.second, stdarray);
        fallbacks.emplace(it.first.toStdString(), stdarray);
    }
}

void
HUSDconvertVariantSelectionFallbacks(
        const PcpVariantFallbackMap &fallbacks,
        UT_StringMap<UT_StringArray> &utfallbacks)
{
    for (auto &&it : fallbacks)
    {
        UT_StringArray utarray;
        UTarrayFromStdVectorOfStrings(utarray, it.second);
        utfallbacks.emplace(it.first.c_str(), utarray);
    }
}


template <typename VT_TYPE, typename UT_TYPE>
static void
xusdVtToUtArray(const VtArray<VT_TYPE> &vtarr, UT_Array<UT_TYPE> &utarr)
{
    utarr.setCapacityIfNeeded(vtarr.size());
    for (exint i = 0, n = vtarr.size(); i < n; ++i)
    {
        std::optional<UT_TYPE> result = GfNumericCast<UT_TYPE>(vtarr[i]);
        utarr.append(result.value_or(0));
    }
}

static void
xusdVtToUtArray(const VtArray<SdfAssetPath> &vtarr, UT_StringArray &utarr)
{
    utarr.setCapacityIfNeeded(vtarr.size());
    for (exint i = 0, n = vtarr.size(); i < n; ++i)
        utarr.append(vtarr[i].GetAssetPath());
    
}

static void
xusdVtToUtArray(const VtArray<TfToken> &vtarr, UT_StringArray &utarr)
{
    utarr.setCapacityIfNeeded(vtarr.size());
    for (exint i = 0, n = vtarr.size(); i < n; ++i)
        utarr.append(GusdUSD_Utils::TokenToStringHolder(vtarr[i]));
    
}

static void
xusdVtToUtArray(const VtArray<std::string> &vtarr, UT_StringArray &utarr)
{
    utarr.setCapacityIfNeeded(vtarr.size());
    for (exint i = 0, n = vtarr.size(); i < n; ++i)
        utarr.append(vtarr[i]);
    
}

template <typename VT_TYPE, typename UT_TYPE>
static VtArray<VT_TYPE>
xusdUtToVtArray(const UT_Array<UT_TYPE> &utarr)
{
    VtArray<VT_TYPE> vtarr(utarr.size());
    for (exint i = 0, n = utarr.size(); i < n; ++i)
        vtarr[i] = GfNumericCast<VT_TYPE>(utarr[i]).value_or(0);

    return vtarr;
}

static bool
xusdIsCameraProperty(const TfToken &attr_name)
{
    UsdGeomCamera cam;
    const UsdPrimDefinition *def = cam.GetSchemaClassPrimDefinition();
    return def->GetSpecType(attr_name) != SdfSpecTypeUnknown;
}

bool
HUSDgetCameraParms(const UsdGeomCamera &cam, const UsdTimeCode &tc,
                   UT_CameraParms &camparms)
{
    if (!cam)
        return false;

    UsdStageRefPtr stage = cam.GetPrim().GetStage();

    fpreal64 mpu;
    UT_StringHolder upaxis;
    HUSDgetMetrics(stage, upaxis, mpu);

    // First, populate from the GfCamera properties.
    GfCamera gf_cam = cam.GetCamera(tc);
    HUSDgetCameraParms(gf_cam, mpu, camparms);

    HUSD_Path rspath = HUSDgetBestRenderSettings(stage);
    UsdRenderSettings rsprim;
    
    if (!rspath.isEmpty())
        rsprim = UsdRenderSettings(stage->GetPrimAtPath(rspath.sdfPath()));
    
    if (rsprim)
    {
        GfVec2i res;
        rsprim.GetResolutionAttr().Get(&res, tc);
        camparms.resx = res[0];
        camparms.resy = res[1];
        float aspect;
        rsprim.GetPixelAspectRatioAttr().Get(&aspect, tc);
        camparms.pixelaspect = aspect;
    }
    else
    {
        camparms.resx = 1920;
        camparms.resy = int(fpreal(camparms.resx) * (gf_cam.GetVerticalAperture() / gf_cam.GetHorizontalAperture()));
        camparms.pixelaspect = 1.0;
    }

    fpreal64 open, close;
    cam.GetShutterOpenAttr().Get(&open, tc);
    cam.GetShutterCloseAttr().Get(&close, tc);
    camparms.shutteropen = open;
    camparms.shutterclose = close;

    UsdPrim usd_prim = cam.GetPrim();
    
    // get imaging distance
    fpreal64 imgdist = 1;
    if (usd_prim.HasAttribute(HusdCameraTokens->imagingDistance))
    {
        UsdAttribute imgdist_attr = usd_prim.GetAttribute(
                                        HusdCameraTokens->imagingDistance);
        imgdist_attr.Get(&imgdist, tc);
    }
    camparms.imagingdistance = imgdist;

    // get guidescale
    float guidescale = 1;
    if (usd_prim.HasAttribute(UsdHoudiniTokens->houdiniGuidescale))
    {
        UsdAttribute guidescale_attr = usd_prim.GetAttribute(
                                        UsdHoudiniTokens->houdiniGuidescale);
        guidescale_attr.Get(&guidescale, tc);
    }
    camparms.guidescale = guidescale;
    // No equivalent to camparms.orthozoom
    // No equivalent to camparms.cropx
    // No equivalent to camparms.cropy
    // No equivalent to camparms.winx
    // No equivalent to camparms.winy

    // Transfer Metadata

    UT_Options metadata;
    for (const UsdAttribute &attr : usd_prim.GetAuthoredAttributes())
    {
        UT_StringHolder name =
            GusdUSD_Utils::TokenToStringHolder(attr.GetName());
        
        // Skip attributes that are primvars (or primvar indices)
        // we also want to skip the camera attributes that already exist
        // and have been converted to UT_CameraParms
        if (TfStringStartsWith(attr.GetName(), "primvars:")
            || UsdGeomXformOp::IsXformOp(attr.GetName())
            || attr.GetName() == HusdCameraTokens->imagingDistance
            || attr.GetName() == UsdHoudiniTokens->houdiniGuidescale
            || attr.GetName() == UsdGeomTokens->xformOpOrder
            || xusdIsCameraProperty(attr.GetName()))
        {
            continue;
        }

        VtValue val;
        if (!attr.Get(&val, tc))
            continue;
        
        // this gets the user facing type like int64[]
        // and not the C++ type like VtArray
        const UT_StringHolder type_name = 
            GusdUSD_Utils::TokenToStringHolder(attr.GetTypeName().GetAsToken());

        const UT_StringHolder key = 
                    GusdUSD_Utils::TokenToStringHolder(attr.GetName());

        
        // add key.__usdtype__
        // so we have enough data to roundtrip back to usd
        UT_StringHolder key_type;
        key_type.format("{}.__usdtype__", key);

        // convert from USD type to something UT_Options friendly  
        
        // primWrapper::convertAttributeData has 
        // vec3, vec2, float and int as the most common types
        // but for metadata I think 
        // string, bool, float, int makes more sense ?

        // start with most common types for Metadata
        // at least I assume they would be
        if (val.IsHolding<float>())
            metadata.setOptionF(key, val.UncheckedGet<float>());
        else if (val.IsHolding<int>())
            metadata.setOptionI(key, val.UncheckedGet<int>());
        
        else if (val.IsHolding<bool>())
            metadata.setOptionB(key, val.UncheckedGet<bool>());
        
        else if (val.IsHolding<std::string>())
            metadata.setOptionS(key, UT_StringHolder(
                                            val.UncheckedGet<std::string>()));
        else if (val.IsHolding<TfToken>())
            metadata.setOptionS(key, 
                    GusdUSD_Utils::TokenToStringHolder(
                                            val.UncheckedGet<TfToken>()));
        else if (val.IsHolding<SdfAssetPath>())
            metadata.setOptionS(key, 
                            val.UncheckedGet<SdfAssetPath>().GetAssetPath());
        
        // float types
        else if (val.IsHolding<GfHalf>())
            metadata.setOptionF(key, 
                GfNumericCast<float>(val.UncheckedGet<GfHalf>()).value_or(0));
        else if (val.IsHolding<double>())
            metadata.setOptionF(key, val.UncheckedGet<double>());
        
        // int types
        else if (val.IsHolding<int64>())
            metadata.setOptionI(key, val.UncheckedGet<int64>());
        else if (val.IsHolding<uchar>())
            metadata.setOptionI(key, 
                GfNumericCast<int64>(val.UncheckedGet<uchar>()).value_or(0));
        else if (val.IsHolding<uint>())
            metadata.setOptionI(key, 
                GfNumericCast<int64>(val.UncheckedGet<uint>()).value_or(0));
        else if (val.IsHolding<uint64>())
            metadata.setOptionI(key, 
                GfNumericCast<int64>(val.UncheckedGet<uint64>()).value_or(0));
        
        // vectors (f, d, h ; 3, 2, 4)
        else if (val.IsHolding<GfVec3f>())
            metadata.setOptionV3(key, 
                            GusdUT_Gf::Cast(val.UncheckedGet<GfVec3f>()));
        else if (val.IsHolding<GfVec2f>())
            metadata.setOptionV2(key,
                            GusdUT_Gf::Cast(val.UncheckedGet<GfVec2f>()));
        else if (val.IsHolding<GfVec4f>())
            metadata.setOptionV4(key,
                            GusdUT_Gf::Cast(val.UncheckedGet<GfVec4f>()));
        
        else if (val.IsHolding<GfVec3d>())
            metadata.setOptionV3(key, 
                            GusdUT_Gf::Cast(val.UncheckedGet<GfVec3d>()));
        else if (val.IsHolding<GfVec2d>())
            metadata.setOptionV2(key,
                            GusdUT_Gf::Cast(val.UncheckedGet<GfVec2d>()));
        else if (val.IsHolding<GfVec4d>())
            metadata.setOptionV4(key,
                            GusdUT_Gf::Cast(val.UncheckedGet<GfVec4d>()));
        
        else if (val.IsHolding<GfVec3h>())
        {
            UT_Vector3D v;
            GusdUT_Gf::Convert(val.UncheckedGet<GfVec3h>(), v);
            metadata.setOptionV3(key, v);
        }
        else if (val.IsHolding<GfVec2h>())
        {
            UT_Vector2D v; 
            GusdUT_Gf::Convert(val.UncheckedGet<GfVec2h>(), v);
            metadata.setOptionV2(key, v);
        }
        else if (val.IsHolding<GfVec4h>())
        {
            UT_Vector4D v; 
            GusdUT_Gf::Convert(val.UncheckedGet<GfVec4h>(), v);
            metadata.setOptionV4(key, v);
        }
        
        // Quaternions
        else if (val.IsHolding<GfQuatf>())
        {
            UT_QuaternionD q;
            GusdUT_Gf::Convert(val.UncheckedGet<GfQuatf>(), q);
            metadata.setOptionQ(key, q);
        }
        else if (val.IsHolding<GfQuatd>())
        {
            UT_QuaternionD q;
            GusdUT_Gf::Convert(val.UncheckedGet<GfQuatd>(), q);
            metadata.setOptionQ(key, q);
        }
        else if (val.IsHolding<GfQuath>())
        {
            UT_QuaternionD q;
            GusdUT_Gf::Convert(val.UncheckedGet<GfQuath>(), q);
            metadata.setOptionQ(key, q);
        }
        
        // matrix types
        else if (val.IsHolding<GfMatrix4d>())
            metadata.setOptionM4(key,
                            GusdUT_Gf::Cast(val.UncheckedGet<GfMatrix4d>()));
        else if (val.IsHolding<GfMatrix3d>())
            metadata.setOptionM3(key,
                            GusdUT_Gf::Cast(val.UncheckedGet<GfMatrix3d>()));
        else if (val.IsHolding<GfMatrix2d>())
            metadata.setOptionM2(key,
                            GusdUT_Gf::Cast(val.UncheckedGet<GfMatrix2d>()));
        
        // VtArray types
        // Int Arrays
        else if (val.IsHolding<VtArray<int>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<int>>();
            metadata.setOptionIArray(key, vtarr.data(), vtarr.size());
        }        
        else if (val.IsHolding<VtArray<int64>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<int64>>();
            metadata.setOptionIArray(key, vtarr.data(), vtarr.size());
        }
        else if (val.IsHolding<VtArray<uchar>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<uchar>>();
            UT_Int64Array utarr;
            xusdVtToUtArray(vtarr, utarr);
            metadata.setOptionIArray(key, utarr);
        }
        else if (val.IsHolding<VtArray<uint>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<uint>>();
            UT_Int64Array utarr;
            xusdVtToUtArray(vtarr, utarr);
            metadata.setOptionIArray(key, utarr);
        }
        else if (val.IsHolding<VtArray<uint64>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<uint64>>();
            UT_Int64Array utarr;
            xusdVtToUtArray(vtarr, utarr);
            metadata.setOptionIArray(key, utarr);
        }
        
        // Float Arrays
        else if (val.IsHolding<VtArray<float>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<float>>();
            metadata.setOptionFArray(key, vtarr.data(), vtarr.size());
        }
        else if (val.IsHolding<VtArray<double>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<double>>();
            metadata.setOptionFArray(key, vtarr.data(), vtarr.size());
        }
        else if (val.IsHolding<VtArray<GfHalf>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<GfHalf>>();
            UT_Array<fpreal64> utarr;
            xusdVtToUtArray(vtarr, utarr);
            metadata.setOptionFArray(key, utarr);
        }

        // String Arrays
        else if (val.IsHolding<VtArray<std::string>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<std::string>>();
            UT_StringArray utarr;
            xusdVtToUtArray(vtarr, utarr);
            metadata.setOptionSArray(key, utarr);
        }
        else if (val.IsHolding<VtArray<TfToken>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<TfToken>>();
            UT_StringArray utarr;
            xusdVtToUtArray(vtarr, utarr);
            metadata.setOptionSArray(key, utarr);
        }
        else if (val.IsHolding<VtArray<SdfAssetPath>>())
        {
            auto vtarr = val.UncheckedGet<VtArray<SdfAssetPath>>();
            UT_StringArray utarr;
            xusdVtToUtArray(vtarr, utarr);
            metadata.setOptionSArray(key, utarr);
        }
        else
            continue;
        
        // record original USD type
        metadata.setOptionS(key_type, type_name);
    }
    // finally, make sure we record the camera's applied schemas !
    TfTokenVector apis = usd_prim.GetAppliedSchemas();
    
    UT_StringArray utarr;
    utarr.setCapacityIfNeeded(apis.size());
    for (exint i = 0, n = apis.size(); i < n; ++i)
        utarr.append(apis[i].GetText());
    
    metadata.setOptionSArray("usdapischemas", utarr);

    camparms.metadata = &metadata;
    return true;
}

void
HUSDgetCameraParms(const GfCamera &cam, fpreal64 mpu, UT_CameraParms &ut_parms)
{
    ut_parms.focal = cam.GetFocalLength() * mpu * 100.0;
    ut_parms.aperture = cam.GetHorizontalAperture() * mpu * 100.0;
    ut_parms.focusdistance = cam.GetFocusDistance();
    ut_parms.fstop = cam.GetFStop();
    
    if (cam.GetProjection() == GfCamera::Orthographic)
        ut_parms.projection = OBJ_PROJ_ORTHO;
    else
        ut_parms.projection = OBJ_PROJ_PERSPECTIVE;
    
    // Ignore the focal/aperture in ortho projection
    if (ut_parms.projection == OBJ_PROJ_ORTHO)
        ut_parms.focal = ut_parms.aperture = 1;

    // Set resolution / pixel aspect as if there is no render settings prim.
    ut_parms.resx = 1920;
    ut_parms.resy
            = int(fpreal(ut_parms.resx)
                  * (cam.GetVerticalAperture() / cam.GetHorizontalAperture()));
    ut_parms.pixelaspect = 1.0;

    ut_parms.clipnear = cam.GetClippingRange().GetMin();
    ut_parms.clipfar = cam.GetClippingRange().GetMax();
}

static void
husdConvertCameraMetadata(const UT_Options &metadata, XUSD_CameraParms &parms)
{
    static constexpr UT_StringLit theApiSchemasAttrib("usdapischemas");
    static constexpr UT_StringLit theUsdTypeSuffix(".__usdtype__");

    // Handle API schema list separately from other metadata entries, which are
    // translated to custom attributes.
    parms.myAPISchemas.clear();
    if (metadata.getOptionType(theApiSchemasAttrib.asRef())
        == UT_OPTION_STRINGARRAY)
    {
        const UT_StringArray &api_schemas
                = metadata.getOptionSArray(theApiSchemasAttrib.asRef());

        parms.myAPISchemas.reserve(api_schemas.size());
        for (exint i = 0, n = api_schemas.size(); i < n; ++i)
            parms.myAPISchemas.push_back(TfToken(api_schemas[i].toStdString()));
    }

    UT_WorkBuffer usd_type_name_key;
    parms.myCustomAttribs.clear();
    for (auto it = metadata.begin(); !it.atEnd(); it.advance())
    {
        if (it.name() == theApiSchemasAttrib
            || it.name().endsWith(theUsdTypeSuffix.asRef()))
        {
            continue;
        }

        const TfToken name(it.name());
        const UT_OptionEntry *entry = it.entry();

        // Skip invalid identifiers. Note we could also consider encoding with
        // UT_VarEncode, but would need a new variant to allow namespaced
        // identifiers like "houdini:stuff".
        if (!SdfPath::IsValidNamespacedIdentifier(name.GetString()))
            continue;

        usd_type_name_key.format("{}{}", it.name(), theUsdTypeSuffix);
        SdfValueTypeName usd_type_name;
        if (metadata.hasOption(usd_type_name_key))
        {
            const UT_StringHolder &tinfo
                    = metadata.getOptionS(usd_type_name_key);
            usd_type_name = SdfSchema::GetInstance().FindType(tinfo.c_str());
        }

        switch (it.type())
        {
            case UT_OPTION_INT:
                if (usd_type_name == SdfValueTypeNames->UChar)
                {
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(GfNumericCast<uint8>(
                                entry->getOptionI()).value_or(0)));
                }
                else if (usd_type_name == SdfValueTypeNames->Int)
                {
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(GfNumericCast<int32>(
                                entry->getOptionI()).value_or(0)));
                }
                else if (usd_type_name == SdfValueTypeNames->UInt)
                {
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(GfNumericCast<uint32>(
                                entry->getOptionI()).value_or(0)));
                }
                else if (usd_type_name == SdfValueTypeNames->UInt64)
                {
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(GfNumericCast<uint64>(
                                entry->getOptionI()).value_or(0)));
                }
                else
                {
                    parms.myCustomAttribs.emplace_back(
                            name, SdfValueTypeNames->Int64,
                            VtValue(entry->getOptionI()));
                }
                break;
            case UT_OPTION_BOOL:
                parms.myCustomAttribs.emplace_back(
                        name, SdfValueTypeNames->Bool,
                        VtValue(entry->getOptionB()));
                break;
            case UT_OPTION_FPREAL:
                if (usd_type_name == SdfValueTypeNames->Half)
                {
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(GfNumericCast<GfHalf>(
                                entry->getOptionF()).value_or(0)));
                    
                }
                else if (usd_type_name == SdfValueTypeNames->Float)
                {
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(GfNumericCast<float>(
                                entry->getOptionF()).value_or(0)));
                }
                else
                {
                    parms.myCustomAttribs.emplace_back(
                            name, SdfValueTypeNames->Double,
                            VtValue(entry->getOptionF()));
                }
                break;
            case UT_OPTION_STRING:
                if (usd_type_name == SdfValueTypeNames->String)
                {
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(entry->getOptionS().toStdString()));
                }
                else if (usd_type_name == SdfValueTypeNames->Asset)
                {
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(SdfAssetPath(
                                    entry->getOptionS().toStdString())));
                }
                else
                {
                    parms.myCustomAttribs.emplace_back(
                            name, SdfValueTypeNames->Token,
                            VtValue(TfToken(
                                    entry->getOptionS().toStdString())));
                }
                break;
            case UT_OPTION_UV:
            case UT_OPTION_VECTOR2:
                if (usd_type_name == SdfValueTypeNames->Half2
                    || usd_type_name == SdfValueTypeNames->TexCoord2h)
                {
                    UT_Vector2D v = entry->getOptionV2();
                    GfVec2h gf_value;
                    GusdUT_Gf::Convert(v, gf_value);
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name, VtValue(gf_value));
                }
                else if (usd_type_name == SdfValueTypeNames->Float2
                         || usd_type_name == SdfValueTypeNames->TexCoord2f)
                {
                    UT_Vector2D v = entry->getOptionV2();
                    GfVec2f gf_value;
                    GusdUT_Gf::Convert(v, gf_value);
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name, VtValue(gf_value));
                }
                else
                {
                    UT_Vector2D v = entry->getOptionV2();
                    if (it.type() == UT_OPTION_UV)
                        usd_type_name = SdfValueTypeNames->TexCoord2d;
                    else
                        usd_type_name = SdfValueTypeNames->Double2;
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(GusdUT_Gf::Cast(v)));
                }
                break;
            case UT_OPTION_UVW:
            case UT_OPTION_VECTOR3:
                if (usd_type_name == SdfValueTypeNames->Half3
                    || usd_type_name == SdfValueTypeNames->TexCoord3h
                    || usd_type_name == SdfValueTypeNames->Point3h
                    || usd_type_name == SdfValueTypeNames->Normal3h
                    || usd_type_name == SdfValueTypeNames->Vector3h
                    || usd_type_name == SdfValueTypeNames->Color3h)
                {
                    UT_Vector3D v = entry->getOptionV3();
                    GfVec3h gf_value;
                    GusdUT_Gf::Convert(v, gf_value);
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name, VtValue(gf_value));
                }
                else if (usd_type_name == SdfValueTypeNames->Float3
                         || usd_type_name == SdfValueTypeNames->Point3f
                         || usd_type_name == SdfValueTypeNames->Normal3f
                         || usd_type_name == SdfValueTypeNames->Vector3f
                         || usd_type_name == SdfValueTypeNames->Color3f
                         || usd_type_name == SdfValueTypeNames->TexCoord3f)
                {
                    UT_Vector3D v = entry->getOptionV3();
                    GfVec3f gf_value;
                    GusdUT_Gf::Convert(v, gf_value);
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name, VtValue(gf_value));
                }
                else
                {
                    UT_Vector3D v = entry->getOptionV3();
                    if (it.type() == UT_OPTION_UVW)
                        usd_type_name = SdfValueTypeNames->TexCoord3d;
                    else
                        usd_type_name = SdfValueTypeNames->Double3;
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name,
                            VtValue(GusdUT_Gf::Cast(v)));
                }
                break;
            case UT_OPTION_VECTOR4:
                if (usd_type_name == SdfValueTypeNames->Half4
                    || usd_type_name == SdfValueTypeNames->Color4h)
                {
                    UT_Vector4D v = entry->getOptionV4();
                    GfVec4h gf_value;
                    GusdUT_Gf::Convert(v, gf_value);
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name, VtValue(gf_value));
                }
                else if (usd_type_name == SdfValueTypeNames->Float4
                         || usd_type_name == SdfValueTypeNames->Color4f)
                {
                    UT_Vector4D v = entry->getOptionV4();
                    GfVec4f gf_value;
                    GusdUT_Gf::Convert(v, gf_value);
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name, VtValue(gf_value));
                }
                else if (usd_type_name == SdfValueTypeNames->Quath)
                {
                    UT_QuaternionD v(entry->getOptionV4());
                    GfQuath gf_value;
                    GusdUT_Gf::Convert(v, gf_value);
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name, VtValue(gf_value));
                }
                else if (usd_type_name == SdfValueTypeNames->Quatf)
                {
                    UT_QuaternionD v(entry->getOptionV4());
                    GfQuatf gf_value;
                    GusdUT_Gf::Convert(v, gf_value);
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name, VtValue(gf_value));
                }
                else if (usd_type_name == SdfValueTypeNames->Quatd)
                {
                    UT_QuaternionD v(entry->getOptionV4());
                    GfQuatd gf_value;
                    GusdUT_Gf::Convert(v, gf_value);
                    parms.myCustomAttribs.emplace_back(
                            name, usd_type_name, VtValue(gf_value));
                }
                else
                {
                    UT_Vector4D v = entry->getOptionV4();
                    parms.myCustomAttribs.emplace_back(
                            name, SdfValueTypeNames->Double4,
                            VtValue(GusdUT_Gf::Cast(v)));
                }
                break;
            case UT_OPTION_QUATERNION:
                {
                    UT_QuaternionD v = entry->getOptionQ();
                    if (usd_type_name == SdfValueTypeNames->Quath)
                    {
                        GfQuath gf_value;
                        GusdUT_Gf::Convert(v, gf_value);
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name, VtValue(gf_value));
                    }
                    else if (usd_type_name == SdfValueTypeNames->Quatf)
                    {
                        GfQuatf gf_value;
                        GusdUT_Gf::Convert(v, gf_value);
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name, VtValue(gf_value));
                    }
                    else
                    {
                        GfQuatd gf_value;
                        GusdUT_Gf::Convert(v, gf_value);
                        parms.myCustomAttribs.emplace_back(
                                name, SdfValueTypeNames->Quatd,
                                VtValue(gf_value));
                    }
                }
                break;
            case UT_OPTION_MATRIX2:
                parms.myCustomAttribs.emplace_back(
                        name, SdfValueTypeNames->Matrix2d,
                        VtValue(GusdUT_Gf::Cast(entry->getOptionM2())));
                break;
            case UT_OPTION_MATRIX3:
                parms.myCustomAttribs.emplace_back(
                        name, SdfValueTypeNames->Matrix3d,
                        VtValue(GusdUT_Gf::Cast(entry->getOptionM3())));
                break;
            case UT_OPTION_MATRIX4:
                parms.myCustomAttribs.emplace_back(
                        name, SdfValueTypeNames->Matrix4d,
                        VtValue(GusdUT_Gf::Cast(entry->getOptionM4())));
                break;
            case UT_OPTION_INTARRAY:
                {
                    const UT_Array<int64> &ut_values = entry->getOptionIArray();

                    if (usd_type_name == SdfValueTypeNames->UCharArray)
                    {
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name,
                                VtValue(xusdUtToVtArray<uchar>(ut_values)));
                    }
                    else if (usd_type_name == SdfValueTypeNames->IntArray)
                    {
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name,
                                VtValue(xusdUtToVtArray<int>(ut_values)));
                    }
                    else if (usd_type_name == SdfValueTypeNames->UIntArray)
                    {
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name,
                                VtValue(xusdUtToVtArray<uint>(ut_values)));
                    }
                    else if (usd_type_name == SdfValueTypeNames->UInt64Array)
                    {
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name,
                                VtValue(xusdUtToVtArray<uint64>(ut_values)));
                    }
                    else
                    {
                        parms.myCustomAttribs.emplace_back(
                                name, SdfValueTypeNames->Int64Array,
                                VtValue(xusdUtToVtArray<int64>(ut_values)));
                    }
                }
                break;
            case UT_OPTION_FPREALARRAY:
                {
                    const UT_Fpreal64Array &ut_values = entry->getOptionFArray();

                    if (usd_type_name == SdfValueTypeNames->HalfArray)
                    {
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name,
                                VtValue(xusdUtToVtArray<GfHalf>(ut_values)));
                    }
                    else if (usd_type_name == SdfValueTypeNames->FloatArray)
                    {
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name,
                                VtValue(xusdUtToVtArray<float>(ut_values)));
                    }
                    else
                    {
                        parms.myCustomAttribs.emplace_back(
                                name, SdfValueTypeNames->DoubleArray,
                                VtValue(xusdUtToVtArray<double>(ut_values)));
                    }
                }
                break;
            case UT_OPTION_STRINGARRAY:
                {
                    const UT_StringArray &ut_values = entry->getOptionSArray();

                    if (usd_type_name == SdfValueTypeNames->StringArray)
                    {
                        VtArray<std::string> values(ut_values.size());
                        for (exint i = 0, n = ut_values.size(); i < n; ++i)
                            values[i] = ut_values(i).toStdString();
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name, VtValue(values));
                    }
                    else if (usd_type_name == SdfValueTypeNames->AssetArray)
                    {
                        VtArray<SdfAssetPath> values(ut_values.size());
                        for (exint i = 0, n = ut_values.size(); i < n; ++i)
                            values[i] = SdfAssetPath(ut_values(i).toStdString());
                        parms.myCustomAttribs.emplace_back(
                                name, usd_type_name, VtValue(values));
                    }
                    else
                    {
                        VtArray<TfToken> values(ut_values.size());
                        for (exint i = 0, n = ut_values.size(); i < n; ++i)
                            values[i] = TfToken(ut_values(i).toStdString());
                        parms.myCustomAttribs.emplace_back(
                                name, SdfValueTypeNames->TokenArray,
                                VtValue(values));
                    }
                }
                break;

            // these types are unsupported or invalid
            case UT_OPTION_DICT:
            case UT_OPTION_DICTARRAY:
            case UT_OPTION_STRINGRAW:
            case UT_OPTION_STRINGRAWARRAY:
            case UT_OPTION_STRINGUTF8:
            case UT_OPTION_STRINGUTF8ARRAY:
            case UT_OPTION_INVALID:
            case UT_OPTION_NUM_TYPES:
                continue;
        }
    }
}

void
HUSDconvertCameraParms(const UT_CameraParms &ut_parms, XUSD_CameraParms &parms)
{
    switch (ut_parms.projection)
    {
        case OBJ_PROJ_PERSPECTIVE:
            parms.myCamera.SetProjection(GfCamera::Perspective);
            break;
        case OBJ_PROJ_ORTHO:
            parms.myCamera.SetProjection(GfCamera::Orthographic);
            break;
        case NUM_OBJ_PROJECTIONS:
            UT_ASSERT(!"Invalid projection type");
            break;
    }

    parms.myCamera.SetFocusDistance(ut_parms.focusdistance);
    parms.myCamera.SetFStop(ut_parms.fstop);
    parms.myCamera.SetClippingRange(
            GfRange1f(ut_parms.clipnear, ut_parms.clipfar));
    parms.myShutterOpen = ut_parms.shutteropen;
    parms.myShutterClose = ut_parms.shutterclose;

    parms.myGuideScale = ut_parms.guidescale;
    parms.myImagingDistance = ut_parms.imagingdistance;

    // TODO - provide a scale parameter for callers which have access to the
    // stage units.
    // NOTE: We don't have access to the scene so we assume a default
    // scaling factor of 100
    // camera units are 10th of scene units
    // mm -> m = 1000
    // 1000/10 = 100
    const fpreal scale_factor = 1.0 / 100.0;

    parms.myCamera.SetFocalLength(ut_parms.focal * scale_factor);

    // set vertical and horizontal aperture
    // NOTE:
    // this is based off the code in $SHS/husdplugins/objtranslators/cam.py

    fpreal v_scale = 1.0 / ut_parms.getAspectRatio();
    fpreal hap, vap; 
    fpreal hapoff, vapoff;
    fpreal winsize_x, winsize_y;
    fpreal wincenter_x, wincenter_y;
    ut_parms.getWindowCenter(wincenter_x, wincenter_y);
    ut_parms.getWindowSize(winsize_x, winsize_y);

    if (ut_parms.projection == OBJ_PROJ_ORTHO)
    {
        fpreal mm = 1000 * 1.0; // 1.0 here is hou.scaleToMKS
        hap = ut_parms.orthozoom * scale_factor * mm;
        vap = ut_parms.orthozoom * v_scale * mm * scale_factor;
        hapoff = wincenter_x * ut_parms.orthozoom * scale_factor * mm;
        vapoff = wincenter_y * ut_parms.orthozoom * scale_factor * mm * v_scale;
    }
    else // perspective
    {
        hap = ut_parms.aperture * scale_factor * winsize_x;
        vap = ut_parms.aperture * v_scale * scale_factor * winsize_y;
        hapoff = wincenter_x * ut_parms.aperture * scale_factor;
        vapoff = wincenter_y * ut_parms.aperture * scale_factor * v_scale;
    }

    parms.myCamera.SetHorizontalAperture(hap);
    parms.myCamera.SetHorizontalApertureOffset(hapoff);
    parms.myCamera.SetVerticalAperture(vap);
    parms.myCamera.SetVerticalApertureOffset(vapoff);

    // NOTE: 
    // resolution exists on render settings in USD not cameras
    // same with Crop, pixel aspect

    husdConvertCameraMetadata(*ut_parms.metadata.options(), parms);
}

void 
HUSDgetMetrics(const UsdStageRefPtr &stage, UT_StringHolder &upaxis,
                    fpreal64 &metersperunit)
{
    upaxis = UsdGeomGetFallbackUpAxis().GetString();
    metersperunit = 0.01;
    metersperunit = UsdGeomGetStageMetersPerUnit(stage);
    upaxis = UsdGeomGetStageUpAxis(stage).GetString();
}

void
HUSDgetAllRenderSettings(const UsdStageRefPtr &stage,
                         UT_StringArray &paths)
{
    
    auto renderrootpath = HUSDgetSdfPath(
        HUSD_Constants::getRenderSettingsRootPrimPath());
    
    auto renderroot = stage->GetPrimAtPath(renderrootpath);

    if (renderroot)
    {
        auto range = renderroot.GetAllDescendants();
        for (auto it = range.begin(); it != range.end(); ++it)
        {
            UsdRenderSettings    settingsprim(*it);

            if (settingsprim)
                paths.append(HUSD_Path(settingsprim.GetPath()).pathStr());
        }
    }
}

HUSD_Path
HUSDgetBestRenderSettings(const UsdStageRefPtr &stage, 
                          const UT_StringRef &explicit_path,
                          const UT_StringRef &renderpass_path,
                          bool pick_first_of_many)
{
    // First priority goes to the explicitly provided path.
    if (explicit_path.isstring())
    {
        SdfPath testpath = HUSDgetSdfPath(explicit_path);
        if (!testpath.IsEmpty())
        {
            UsdPrim prim = stage->GetPrimAtPath(testpath);

            if (UsdRenderSettings(prim))
                return testpath;
        }
    }
    
    // Second priority goes to the renderSource attribute specified on the
    // render pass passed in via renderpass_path
    if (renderpass_path.isstring())
    {
        SdfPath testpath = HUSDgetSdfPath(renderpass_path);
        if (!testpath.IsEmpty())
        {
            UsdPrim pass_prim = stage->GetPrimAtPath(testpath);

            if (auto pass = UsdRenderPass(pass_prim))
            {
                SdfPathVector targets;
                if (pass.GetRenderSourceRel().GetForwardedTargets(&targets) &&
                    targets.size() >= 1)
                {
                    UT_ASSERT_MSG(targets.size() == 1,
                        "Multiple RenderSettings prims given in renderSource");
                    const SdfPath &settings_path = targets[0]; 
                    UsdPrim settings_prim = stage->GetPrimAtPath(settings_path);
                    if (UsdRenderSettings(settings_prim))
                        return settings_path;
                }
            }
        }
    }

    // Third priority goes to the current settings prim specified in
    // the stage layer metadata.
    auto settings = UsdRenderSettings::GetStageRenderSettings(stage);
    if (settings)
        return settings.GetPath();

    // Fourth priority goes to the one and only render settings prim.
    UT_StringArray all_settings;
    HUSDgetAllRenderSettings(stage, all_settings);
    if (!all_settings.isEmpty() &&
        (all_settings.size() == 1 || pick_first_of_many))
        return HUSDgetSdfPath(*all_settings.begin());

    // No good candidate render settings prim was found.
    return HUSD_Path();
}

bool
HUSDgetPointInstancerIds(const UsdPrim &prim,
        const HUSD_TimeCode &timecode,
        UT_Array<int64> &ids)
{
    UsdGeomPointInstancer instancer(prim);
    if (!instancer)
        return false;

    UsdTimeCode     usdtime = HUSDgetNonDefaultUsdTimeCode(timecode);
    UsdAttribute    ids_attr = instancer.GetIdsAttr();
    VtArray<int64>  vtids;

    if (ids_attr && ids_attr.Get(&vtids, usdtime))
    {
        ids.setSizeNoInit(vtids.size());
        for (int i = 0, n = vtids.size(); i < n; i++)
            ids(i) = vtids[i];
        UTsortAndRemoveDuplicates(ids);
    }
    else
    {
        UsdAttribute	 protoindices = instancer.GetProtoIndicesAttr();
        VtArray<int>	 indices;

        if (protoindices && protoindices.Get(&indices, usdtime))
        {
            ids.setSizeNoInit(indices.size());
            for (int i = 0, n = indices.size(); i < n; i++)
                ids(i) = i;
        }
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
