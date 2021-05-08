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
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_LayerOffset.h"
#include "HUSD_LoadMasks.h"
#include "HUSD_PathSet.h"
#include "HUSD_Preferences.h"
#include "HUSD_TimeCode.h"
#include <OP/OP_Node.h>
#include <OP/OP_Director.h>
#include <GA/GA_Types.h>
#include <CH/CH_Manager.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONValueMap.h>
#include <UT/UT_OptionEntry.h>
#include <UT/UT_PathSearch.h>
#include <FS/UT_DSO.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdUtils/dependencies.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <pxr/usd/usdUtils/stitch.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usd/schemaBase.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>
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

TF_MAKE_STATIC_DATA(TfType, theSchemaBaseType) {
    *theSchemaBaseType = TfType::Find<UsdSchemaBase>();
    TF_VERIFY(!theSchemaBaseType->IsUnknown());
}

class husd_TypeAliases
{
public:
                                 husd_TypeAliases();
                                ~husd_TypeAliases();

    bool                         hasAlias(const UT_StringRef &alias,
                                        std::string &real_type_name) const;

private:
    UT_StringMap<std::string>    myAliasMap;
};

husd_TypeAliases::husd_TypeAliases()
{
    const UT_PathSearch *pathsearch =
        UT_PathSearch::getInstance(UT_HOUDINI_PATH);
    UT_StringArray aliasfiles;

    pathsearch->findAllFiles("UsdTypeAliases.json", aliasfiles);
    for (auto &&aliasfile : aliasfiles)
    {
        UT_JSONValue         value;
        bool                 success = false;

        if (value.loadFromFile(aliasfile.c_str()))
        {
            UT_JSONValueMap *map = value.getMap();

            if (map)
            {
                UT_StringArray       keys;

                map->getKeyReferences(keys);
                success = true;
                for (auto &&key : keys)
                {
                    const UT_JSONValue      *value = map->get(key);

                    if (value && value->getType() == UT_JSONValue::JSON_STRING)
                    {
                        std::string          valuestr;

                        valuestr = value->getS();
                        myAliasMap.emplace(key, valuestr);
                    }
                    else
                        success = false;
                }
            }
        }
        if (!success)
            std::cerr << "Error parsing '" << aliasfile << "'." << std::endl;
    }
}

husd_TypeAliases::~husd_TypeAliases()
{
}

bool
husd_TypeAliases::hasAlias(const UT_StringRef &alias,
        std::string &real_type_name) const
{
    auto it = myAliasMap.find(alias);

    if (it != myAliasMap.end())
    {
        real_type_name = it->second;
        return true;
    }

    return false;
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
        // Leave absolute paths and "search" paths alone. We only want to
        // update file-relative paths to be absolute.
        if (!assetPath.empty() &&
            ArGetResolver().IsRelativePath(assetPath) &&
            !ArGetResolver().IsSearchPath(assetPath))
        {
            // ComputeAbsolutePath may return an empty string if it doesn't
            // know what to do with a path (such as an op: path pointing to
            // a SOP).
            std::string newpath = mySourceLayer->ComputeAbsolutePath(assetPath);

            if (!newpath.empty())
                return newpath;
        }

        return assetPath;
    }

private:
    const SdfLayerRefPtr &mySourceLayer;
};

UsdUtilsStitchValueStatus 
_StitchCallback(
    const TfToken& field, const SdfPath& path,
    const SdfLayerHandle& strongLayer, bool fieldInStrongLayer,
    const SdfLayerHandle& weakLayer, bool fieldInWeakLayer,
    VtValue* stitchedValue)
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

	    // If dataIds stored in customData are the same valid value,
	    // don't stitch any values together.
	    if (!weakDataId.IsEmpty() &&
		weakDataId != VtValue(GA_INVALID_DATAID) &&
		strongDataId == weakDataId)
		return UsdUtilsStitchValueStatus::NoStitchedValue;

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
    BOOST_NS::optional<VtValue>* valueToCopy)
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
    BOOST_NS::optional<VtValue>* srcChildren, 
    BOOST_NS::optional<VtValue>* dstChildren)
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
_FlattenLayerStackResolveAssetPath(
    const SdfLayerHandle& sourceLayer,
    const std::string& assetPath)
{
    // Used in calls to UsdUtilsFlattenLayerStack to help resolve layer paths
    // from that function into new paths. We are always flattening to in-memory
    // stages. For absolute and search paths, we want to leave asset paths
    // alone. For relative paths we want to make them absolute. If the asset
    // path is anonymous, we don't want to touch it, and if the source layer is
    // anonymous we don't need to touch it (because the assetPath can be
    // assumed to already be what it is supposed to be.
    if (!assetPath.empty() &&
	!SdfLayer::IsAnonymousLayerIdentifier(assetPath) &&
	!sourceLayer->IsAnonymous())
    {
	ArResolver	&resolver = ArGetResolver();

	if (resolver.IsRelativePath(assetPath) &&
	    !resolver.IsSearchPath(assetPath))
	    return sourceLayer->ComputeAbsolutePath(assetPath);
    }

    return assetPath;
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
	    (!layer.isLayerAnonymous() ||
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
	    layer.isLayerAnonymous() &&
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
	    !SdfLayer::IsAnonymousLayerIdentifier(partition.front()))
	{
	    // A single SOP or file layer in a partition should just be added
	    // directly to the explicit paths. If this layer is the strongest
	    // layer, create an empty layer to hold all the explicit sublayers.
	    if (first_partition)
	    {
		new_layer = HUSDcreateAnonymousLayer(stage);
		layers_to_scan_for_references.push_back(new_layer);
		first_partition = false;
	    }
	    explicit_paths.push_back(partition.front());
            explicit_offsets.push_back(partition_offset.front());
	}
	else
	{
	    // We have more than one layer in this partition. Flatten the
	    // layers together.
	    UsdStageRefPtr substage = HUSDcreateStageInMemory(
		UsdStage::LoadNone, stage);
	    SdfLayerRefPtr created_layer;

            // Create an error scope as we compose this temporary stage,
            // which exists only as a holder for the layers we wish to
            // flatten together. If there are warnings or errors during
            // this composition, either they are safe to ignore, or they
            // will show up agai when we compose the flattened layer is
            // composed onto the main stage.
            {
                UT_ErrorManager      ignore_errors_mgr;
                HUSD_ErrorScope      ignore_errors(&ignore_errors_mgr);

                substage->GetRootLayer()->SetSubLayerPaths(partition);
		for (int si = 0, sn = partition_offset.size(); si < sn; si++)
		    substage->GetRootLayer()->SetSubLayerOffset(
                        partition_offset[si], si);
            }

            // Flatten the layers in the partition.
	    created_layer = UsdUtilsFlattenLayerStack(substage,
		_FlattenLayerStackResolveAssetPath);
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
	std::set<std::string> refs = update_layer->GetExternalReferences();

	for (auto &&ref : refs)
	{
	    // Only interested in references that are not sublayers, and that
	    // are anonymous layers.
	    if (SdfLayer::IsAnonymousLayerIdentifier(ref))
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

static bool
_StitchLayersRecursive(const SdfLayerRefPtr &src,
	const SdfLayerRefPtr &dest,
	XUSD_IdentifierToLayerMap &destlayermap,
	XUSD_IdentifierToSavePathMap &stitchedpathmap,
	std::set<std::string> &newdestlayers,
        std::map<std::string, SdfLayerRefPtr> &currentsamplesavelocations,
        bool force_notifiable_file_format)
{
    bool		 success = true;

    // Make sure we haven't already processed this layer, which we may have
    // done if the same layer is referenced from within several other layers.
    if (stitchedpathmap.find(src->GetIdentifier()) != stitchedpathmap.end())
	return success;

    XUSD_IdentifierToLayerMap	 srclayermap;

    HUSDaddExternalReferencesToLayerMap(src, srclayermap, false);

    // Stitch the source layer into the destination layer.
    HUSDstitchLayers(dest, src);
    stitchedpathmap.emplace(src->GetIdentifier(),
	XUSD_SavePathInfo(dest->GetIdentifier()));

    // Go through all externally referenced layers to find other layers that
    // we need to stitch together and save to disk.
    for (auto &&srcit : srclayermap)
    {
	SdfLayerRefPtr	 srclayer = srcit.second;

	if (!srclayer)
	{
	    success = false;
	    break;
	}

        bool             srcsavenodepath = false;
        bool             srcsavelocationtimedep = false;
        std::string      srcsavelocation;
	SdfLayerRefPtr	 destlayer;

        // If we find an existing layer that we are already saving to the
        // desired location, but we've already requested a save to this
        // location from the current time sample, this indicates we have
        // multiple unique layers that we are being asked to save to the
        // same location. This is not okay. We want to warn the user, and
        // increment the file name until we find one that is unique among
        // the layers being saved within this time sample.
        srcsavelocation = HUSDgetLayerSaveLocation(srclayer, &srcsavenodepath);
        srcsavelocationtimedep = HUSDgetSavePathIsTimeDependent(srclayer);
        if (currentsamplesavelocations.count(srcsavelocation) > 0)
        {
            // If we are finding the same layer for the second time (it is
            // perhaps referenced in by two separate sublayers), just skip
            // this occurrence of it. It has already been stitched in with
            // the new time sample.
            if (currentsamplesavelocations[srcsavelocation] == srclayer)
                continue;

            std::string      testpath = srcsavelocation;
            UT_StringHolder  ext = UT_String(testpath).fileExtension();
            UT_WorkBuffer    errbuf;
            std::string      nodepath;
            UT_String        noext = UT_String(testpath).pathUpToExtension();

            // Make a unique save path for this layer.
            noext.append("_duplicate1");
            testpath = noext;
            testpath.append(ext);
            while (currentsamplesavelocations.count(testpath) > 0)
            {
                noext.incrementNumberedName();
                testpath = noext;
                testpath.append(ext);
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
        }
        currentsamplesavelocations.emplace(srcsavelocation, srclayer);

	for (auto &&destit : destlayermap)
	{
	    if (HUSDgetLayerSaveLocation(destit.second) == srcsavelocation)
	    {
		destlayer = destit.second;
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
                    UsdStageWeakPtr(), srcsavelocation);
	    destlayermap[destlayer->GetIdentifier()] = destlayer;
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
        _StitchLayersRecursive(srclayer, destlayer,
            destlayermap, stitchedpathmap,
            newdestlayers, currentsamplesavelocations,
            force_notifiable_file_format);

        // After stitching, make sure the new layer is configured to save to
        // the source layer save location we determined above. We want to
        // either fake the creator node or the save path, depending on where
        // we got the save location originally.
        if (srcsavenodepath)
        {
            // The save location will be "./node/path.usd". Strip the extension
            // and the leading ".".
            UT_String    loc = UT_String(srcsavelocation).pathUpToExtension();
            std::string  srcnodepath(loc.c_str() + 1);

            HUSDsetCreatorNode(destlayer, srcnodepath);
        }
        else
            HUSDsetSavePath(destlayer, srcsavelocation, srcsavelocationtimedep);
    }

    // Update references from src layer identifiers to dest layer identifiers.
    std::map<std::string, std::string> pathmap;

    for (auto &&srcit : srclayermap)
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

	    if (newdestlayers.find(fullpath) != newdestlayers.end())
		pathmap[srcit.first] = fullpath;
	    else
		pathmap[srcit.first] = std::string();
	}
    }
    HUSDupdateExternalReferences(dest, pathmap);

    // Add any sublayers from the source that are not on the dest.
    auto	 srcsubpaths = src->GetSubLayerPaths();
    auto	 destsubpaths = dest->GetSubLayerPaths();

    for (int i = 0, n = srcsubpaths.size(); i < n; i++)
    {
        std::string      srcsubpath = srcsubpaths[i];
        SdfLayerOffset   srcoffset = src->GetSubLayerOffset(i);

	// Don't stitch together anonymous placeholder layers.
        if (HUSDisLayerPlaceholder(srcsubpath))
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
HUSDgetMaterialIdToken()
{
    static const TfToken	 theToken("HoudiniMaterialId");

    return theToken;
}

const TfToken &
HUSDgetMaterialBindingIdToken()
{
    static const TfToken	 theToken("HoudiniMaterialBindingId");

    return theToken;
}

const TfToken &
HUSDgetIsAutoPreviewShaderToken()
{
    static const TfToken	 theToken("HoudiniIsAutoPreviewShader");

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
HUSDgetPrimEditorNodeIdToken()
{
    static const TfToken	 theToken("HoudiniPrimEditorNodeId");

    return theToken;
}

const TfToken &
HUSDgetSourceNodeToken()
{
    static const TfToken	 theToken("HoudiniSourceNode");

    return theToken;
}

const TfType &
HUSDfindType(const UT_StringRef &type_name)
{
    static husd_TypeAliases theTypeAliases;
    std::string real_type_name;

    // Note, we call FindDerivedByName() instead of FindByName() so that
    // we find aliases too. Otherwise we will find "UsdGeomCube" but not "Cube".
    if (theTypeAliases.hasAlias(type_name, real_type_name))
        return theSchemaBaseType->FindDerivedByName(real_type_name);

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

    return pred;
}

UsdListPosition
HUSDgetUsdListPosition(const UT_StringRef &editopstr)
{
    UsdListPosition	 editop(UsdListPositionFrontOfAppendList);

    if (editopstr == HUSD_Constants::getReferenceEditOpAppendFront())
	editop = UsdListPositionFrontOfAppendList;
    else if (editopstr == HUSD_Constants::getReferenceEditOpAppendBack())
	editop = UsdListPositionBackOfAppendList;
    else if (editopstr == HUSD_Constants::getReferenceEditOpPrependFront())
	editop = UsdListPositionFrontOfPrependList;
    else if (editopstr == HUSD_Constants::getReferenceEditOpPrependBack())
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
        bool savepath_is_time_dependent)
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
	else
        {
	    data.erase(HUSDgetSavePathToken());
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
    UT_StringHolder	 nodepath;

    if (HUSDgetNodePath(nodeid, nodepath))
    {
	auto		 infoprim = HUSDgetLayerInfoPrim(layer, true);

	if (infoprim)
	{
	    auto		 data = infoprim->GetCustomData();

	    data[HUSDgetCreatorNodeToken()] =
		VtValue(nodepath.toStdString());
	}
    }
}

void
HUSDsetCreatorNode(const SdfLayerHandle &layer, const std::string &nodepath)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, true);

    if (infoprim)
    {
        auto		 data = infoprim->GetCustomData();

        data[HUSDgetCreatorNodeToken()] = VtValue(nodepath);
    }
}

bool
HUSDgetCreatorNode(const SdfLayerHandle &layer, std::string &nodepath)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
	auto		 data = infoprim->GetCustomData();
	auto		 it = data.find(HUSDgetCreatorNodeToken());

	if (it != data.end())
	    nodepath = it->second.Get().Get<std::string>();
	else
	    nodepath.clear();
    }
    else
	nodepath.clear();

    return (nodepath.length() > 0);
}

void
HUSDsetSourceNode(const UsdPrim &prim, int nodeid)
{
    UT_StringHolder	 nodepath;

    if (HUSDgetNodePath(nodeid, nodepath))
	prim.SetCustomDataByKey(HUSDgetSourceNodeToken(),
	    VtValue(nodepath.toStdString()));
}

bool
HUSDgetSourceNode(const UsdPrim &prim, std::string &nodepath)
{
    auto		 data = prim.GetCustomData();
    auto		 it = data.find(HUSDgetCreatorNodeToken());

    if (it != data.end())
	nodepath = it->second.Get<std::string>();
    else
	nodepath.clear();

    return (nodepath.length() > 0);
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
    UT_StringHolder	 nodepath;

    if (HUSDgetNodePath(nodeid, nodepath))
    {
	auto		 infoprim = HUSDgetLayerInfoPrim(layer, true);

	if (infoprim)
	{
	    auto		 data = infoprim->GetCustomData();
	    auto		 it = data.find(HUSDgetEditorNodesToken());
	    VtArray<std::string> vtnodepaths;

	    if (it != data.end())
		vtnodepaths = it->second.Get().Get<VtArray<std::string> >();
	    if (std::find(vtnodepaths.begin(), vtnodepaths.end(),
		    nodepath.toStdString()) == vtnodepaths.end())
	    {
		vtnodepaths.push_back(nodepath.toStdString());
		data[HUSDgetEditorNodesToken()] = VtValue(vtnodepaths);
	    }
	}
    }
}

bool
HUSDgetEditorNodes(const SdfLayerHandle &layer,
	std::vector<std::string> &nodepaths)
{
    auto		 infoprim = HUSDgetLayerInfoPrim(layer, false);

    if (infoprim)
    {
	auto		 data = infoprim->GetCustomData();
	auto		 it = data.find(HUSDgetEditorNodesToken());

	if (it != data.end())
	{
	    VtArray<std::string>	 vtnodepaths;

	    vtnodepaths = it->second.Get().Get<VtArray<std::string> >();
	    nodepaths.insert(nodepaths.begin(),
		vtnodepaths.begin(), vtnodepaths.end());
	}
	else
	    nodepaths.clear();
    }
    else
	nodepaths.clear();

    return (nodepaths.size() > 0);
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
HUSDsetPrimEditorNodeId(const UsdPrim &prim, int nodeid)
{
    if (prim)
	prim.SetCustomDataByKey(HUSDgetPrimEditorNodeIdToken(),
	    VtValue(nodeid));
}

void
HUSDsetPrimEditorNodeId(const SdfPrimSpecHandle &prim, int nodeid)
{
    if (prim)
	prim->SetCustomData(HUSDgetPrimEditorNodeIdToken(),
	    VtValue(nodeid));
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
    static std::map<TfToken, TfToken>	 theModelHierarchy;

    if (theModelHierarchy.empty())
    {
	theModelHierarchy[KindTokens->subcomponent] = KindTokens->component;
	theModelHierarchy[KindTokens->component] = KindTokens->group;
	theModelHierarchy[KindTokens->group] = KindTokens->group;
	theModelHierarchy[KindTokens->assembly] = KindTokens->assembly;
    }

    auto	 it = theModelHierarchy.find(kind);

    if (it != theModelHierarchy.end())
	return it->second;

    return TfToken();
}

bool
HUSDallExistingAncestorsActive(const UsdStageWeakPtr &stage,
	const SdfPath &path)
{
    // We can only handle absolute paths. Return false because the question
    // doesn't even really make sense.
    UT_ASSERT(path.IsAbsolutePath());
    if (!path.IsAbsolutePath())
        return false;

    // The absolute root path always exists and can't be inactive.
    if (path != SdfPath::AbsoluteRootPath())
    {
        SdfPath  testpath = path.GetParentPath();

        while (testpath != SdfPath::AbsoluteRootPath())
        {
            UsdPrim		 prim = stage->GetPrimAtPath(testpath);

            if (prim)
                return prim.IsActive();

            testpath = testpath.GetParentPath();
        }
    }

    return true;
}

SdfPrimSpecHandle
HUSDcreatePrimInLayer(const UsdStageWeakPtr &stage,
	const SdfLayerHandle &layer,
	const SdfPath &path,
	const TfToken &kind,
	bool parent_prims_define,
	const std::string &parent_prims_type)
{
    // We have to have an absolute path, because we don't know what a relative
    // path is meant to be relative to.
    if (!path.IsAbsolutePath())
        return SdfPrimSpecHandle();

    // Make sure we aren't trying to create a primitive that is going to be a
    // child of an inactive primitive. The creation will work, but subsequent
    // operations will fail (somewhat mysteriously). Better to catch the error
    // sooner.
    if (!HUSDallExistingAncestorsActive(stage, path))
        return SdfPrimSpecHandle();

    UsdPrim		 prim = stage->GetPrimAtPath(path);
    SdfPrimSpecHandle	 existing_parent_spec;
    bool		 traverse_parents = false;

    if (parent_prims_define ||
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

    // If the prim already exists on the stage, we don't want to make any
    // further changes to the prim in this layer. We must check for the prim
    // on the stage before creating the primspec on the layer because the
    // layer may be composed on the stage (depending on whether we are doing
    // direct layer editing or not).
    if (!prim && primspec)
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

		if (parent_prims_define)
		    parentspec->SetSpecifier(SdfSpecifierDef);
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
        const UsdUtilsModifyAssetPathFn &modifyFn)
{
    SdfChangeBlock       changeblock;

    // The UsdUtilsModifyAssetPaths method sets the layer offset to a no-op
    // for any sublayer where the path is changed. We are just manipulating
    // the paths, but pointing to the same files, so we want to preserve any
    // layer offset values. Stash the values before the update, and restore
    // them after it's done.
    SdfLayerOffsetVector oldoffsets = layer->GetSubLayerOffsets();
    UsdUtilsModifyAssetPaths(layer, modifyFn);
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
	const SdfLayerHandle &weakLayer)
{
    UsdUtilsStitchLayers(strongLayer, weakLayer, _StitchCallback);
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
    if (layer->IsAnonymous())
    {
	std::string	 nodepath;

	// If a SOP layer is anonymous, it will have a creator node.
	if (HUSDgetCreatorNode(layer, nodepath))
	{
	    // And that creator node will be a SOP.
	    if (OPgetDirector()->findSOPNode(nodepath.c_str()))
	    {
		return true;
	    }
	}
    }
    else
	return HUSDisSopLayer(layer->GetIdentifier());

    return false;
}

bool
HUSDshouldSaveLayerToDisk(const SdfLayerHandle &layer)
{
    if (SdfLayer::IsAnonymousLayerIdentifier(layer->GetIdentifier()))
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
    std::string	 savepath;
    std::string	 savecontrol;

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

    UT_ASSERT(!SdfLayer::IsAnonymousLayerIdentifier(savepath));

    return savepath;
}

void
HUSDaddExternalReferencesToLayerMap(const SdfLayerRefPtr &layer,
	XUSD_IdentifierToLayerMap &layermap,
	bool recursive)
{
    std::set<std::string>	 refs;

    refs = layer->GetExternalReferences();
    for (auto &&ref : refs)
    {
	if (layermap.find(ref) == layermap.end())
	{
	    // Quick pre-check to avoid finding/opening the layer just to
	    // test if it should be saved to disk.
	    if (SdfLayer::IsAnonymousLayerIdentifier(ref) ||
		HUSDisSopLayer(ref))
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
		    layermap[ref] = reflayer;
		    if (recursive)
			HUSDaddExternalReferencesToLayerMap(
			    reflayer, layermap, recursive);
		}
	    }
	}
    }
}

bool
HUSDaddStageTimeSample(const UsdStageWeakPtr &src,
	const UsdStageRefPtr &dest,
	SdfLayerRefPtrVector &hold_layers,
        bool force_notifiable_file_format)
{
    ArResolverContextBinder	          binder(src->GetPathResolverContext());
    auto			          srclayer = src->GetRootLayer();
    auto			          destlayer = dest->GetRootLayer();
    XUSD_IdentifierToLayerMap	          destlayermap;
    XUSD_IdentifierToSavePathMap          stitchedpathmap;
    std::set<std::string>	          newdestlayers;
    std::map<std::string, SdfLayerRefPtr> currentsamplesavelocations;
    bool			          success = false;

    HUSDaddExternalReferencesToLayerMap(destlayer, destlayermap, true);

    success = _StitchLayersRecursive(srclayer, destlayer,
	destlayermap, stitchedpathmap,
        newdestlayers, currentsamplesavelocations,
        force_notifiable_file_format);

    for (auto &&it : destlayermap)
	hold_layers.push_back(it.second);

    return success;
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
	theFactories.stdsort(
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
	    "rootlayer",
	    *resolver_context,
	    load);
    }
    else if (context_stage)
    {
	// When building a stage based on an existing stage, copy the
	// resolver context. Plugin factories don't even get a chance.
	stage = UsdStage::CreateInMemory(
	    "rootlayer",
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
		"rootlayer",
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
        stage->SetTimeCodesPerSecond(CHgetManager()->getSamplesPerSec());
        stage->SetFramesPerSecond(CHgetManager()->getSamplesPerSec());
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

    stage = HUSDcreateStageInMemory(
	(load_masks && !load_masks->loadAll())
	    ? UsdStage::LoadNone
	    : UsdStage::LoadAll,
	context_stage,
	resolver_context_nodeid,
	resolver_context);

    // Set the stage mask on the new stage.
    if (load_masks)
    {
	auto stage_mask = HUSDgetUsdStagePopulationMask(*load_masks);
	if (stage_mask != UsdStagePopulationMask::All())
	    stage->SetPopulationMask(stage_mask);
	if (!load_masks->muteLayers().empty())
	{
	    std::vector<std::string>	 mutelayers;

	    for (auto &&identifier : load_masks->muteLayers())
		mutelayers.push_back(identifier.toStdString());
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

SdfLayerRefPtr
HUSDcreateAnonymousLayer(
        const UsdStageWeakPtr &context_stage,
        const std::string &tag)
{
    SdfLayerRefPtr layer;

    layer = SdfLayer::CreateAnonymous(tag);
    if (context_stage)
    {
        SdfPrimSpecHandle layerroot =
            layer->GetPseudoRoot();
        SdfPrimSpecHandle stageroot =
            context_stage->GetRootLayer()->GetPseudoRoot();
        VtValue value;

        if (layerroot && stageroot)
        {
            static const TfTokenVector theMatchStageFields({
                UsdGeomTokens->upAxis,
                UsdGeomTokens->metersPerUnit,
                SdfFieldKeys->FramesPerSecond,
                SdfFieldKeys->TimeCodesPerSecond
            });
            for (auto &&field : theMatchStageFields)
                if (stageroot->HasField(field, &value))
                    layerroot->SetInfo(field, value);
        }
    }

    return layer;
}

SdfLayerRefPtr
HUSDcreateAnonymousCopy(SdfLayerRefPtr srclayer, const std::string &tag)
{
    SdfLayerRefPtr copylayer = HUSDcreateAnonymousLayer(UsdStageWeakPtr(), tag);

    // Copy the source layer contents.
    copylayer->TransferContent(srclayer);

    // For layers being copied from disk, we need to go through all external
    // references and make them full paths.
    if (!srclayer->IsAnonymous())
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
    return UsdUtilsFlattenLayerStack(stage,
	_FlattenLayerStackResolveAssetPath);
}

bool
HUSDisLayerEmpty(const SdfLayerHandle &layer,
        const UsdStageRefPtr &compare_stage_root_prim)
{
    // If the layer has a sublayer path or more than one root prim, it's
    // not empty.
    if (!layer->GetSubLayerPaths().empty() ||
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

	// Any custom data other than a creator node, treat the layer as
	// not empty.
	for (auto it : data)
	{
	    if (it.first != HUSDgetCreatorNodeToken())
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
    if (layer->IsAnonymous())
    {
	std::string	 save_control;

	if (HUSDgetSaveControl(layer, save_control) &&
	    HUSD_Constants::getSaveControlPlaceholder() == save_control)
	    return true;
    }

    return false;
}

bool
HUSDisLayerPlaceholder(const std::string &identifier)
{
    if (SdfLayer::IsAnonymousLayerIdentifier(identifier))
    {
        SdfLayerRefPtr	 srclayer = SdfLayer::Find(identifier);

        if (srclayer && HUSDisLayerPlaceholder(srclayer))
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

static inline void
husdUpdateTimeSampling( HUSD_TimeSampling &sampling,
	HUSD_TimeSampling new_sampling )
{
    if( new_sampling > sampling ) 
	sampling = new_sampling;
}

static inline HUSD_TimeSampling 
husdGetLocalTransformTimeSampling(const UsdPrim &prim, bool *resets)
{
    HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;

    UsdGeomXformable xformable(prim);
    if( !xformable )
	return time_sampling;

    std::vector<UsdGeomXformOp> ops = xformable.GetOrderedXformOps( resets );
    for( auto &&op : ops )
	husdUpdateTimeSampling( time_sampling, 
		husdGetTimeSampling( op.GetNumTimeSamples() ));
   
    return time_sampling;
}

HUSD_TimeSampling
HUSDgetValueTimeSampling(const UsdAttribute &attrib)
{
    if( !attrib )
	return HUSD_TimeSampling::NONE;

    return husdGetTimeSampling( attrib.GetNumTimeSamples() );
}

HUSD_TimeSampling
HUSDgetValueTimeSampling(const UsdGeomPrimvar &primvar)
{
    HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;

    HUSDupdateValueTimeSampling( time_sampling, primvar );
    return time_sampling;
}

HUSD_TimeSampling
HUSDgetLocalTransformTimeSampling(const UsdPrim &prim)
{
    return husdGetLocalTransformTimeSampling(prim, nullptr);
}

HUSD_TimeSampling
HUSDgetWorldTransformTimeSampling(const UsdPrim &prim)
{
    HUSD_TimeSampling	time_sampling = HUSD_TimeSampling::NONE;
    UsdPrim		testprim(prim);
    bool		resets = false;

    while (testprim)
    {
	husdUpdateTimeSampling( time_sampling, 
		husdGetLocalTransformTimeSampling(testprim, &resets));
	
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
    husdUpdateTimeSampling( sampling, new_sampling );
}

void
HUSDupdateValueTimeSampling( HUSD_TimeSampling &sampling,
	const UsdAttribute &attrib)
{
    husdUpdateTimeSampling( sampling, HUSDgetValueTimeSampling( attrib ));
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
    husdUpdateTimeSampling( sampling, HUSDgetLocalTransformTimeSampling(prim));
}

void
HUSDupdateWorldTransformTimeSampling(HUSD_TimeSampling &sampling,
	const UsdPrim &prim)
{
    husdUpdateTimeSampling( sampling, HUSDgetWorldTransformTimeSampling(prim));
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

PXR_NAMESPACE_CLOSE_SCOPE

