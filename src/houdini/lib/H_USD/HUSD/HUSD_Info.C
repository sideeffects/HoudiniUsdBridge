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

#include "HUSD_Info.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_Path.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_FindPrimsTask.h"
#include <gusd/UT_Gf.h>
#include <PY/PY_Python.h>
#include <PY/PY_Result.h>
#include <UT/UT_BoundingBox.h>
#include <UT/UT_Debug.h>
#include <UT/UT_ErrorManager.h>
#include <UT/UT_InfoTree.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Options.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <SYS/SYS_Hash.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdGeom/curves.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usd/schemaBase.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/base/tf/type.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
    class PrimInfo
    {
    public:
	bool			 operator==(const PrimInfo &other) const
				 {
				     return myPrimType == other.myPrimType &&
					    myPrimKind == other.myPrimKind;
				 }
	SYS_HashType		 hash() const
				 {
				     SYS_HashType   h = SYShash(myPrimType);
				     SYShashCombine(h, myPrimKind);
				     return h;
				 }

	const UT_StringHolder	 myPrimType;
	const UT_StringHolder	 myPrimKind;
    };

    SYS_FORCE_INLINE size_t hash_value(const PrimInfo &priminfo)
    { return priminfo.hash(); }

    inline UsdPrim
    husdGetPrim(HUSD_AutoAnyLock &lock, const UT_StringRef &primpath)
    {
        if (!primpath.isstring())
            return UsdPrim();

        auto data = lock.constData();
        if( !data || !data->isStageValid() )
            return UsdPrim();

        SdfPath sdfpath(HUSDgetSdfPath(primpath));
        return data->stage()->GetPrimAtPath(sdfpath);
    }

    template <typename T>
    inline bool
    husdSetPrimpaths(UT_StringArray &primpaths, const T &sdfpaths)
    {
        primpaths.setSize(0);
        primpaths.setCapacity(sdfpaths.size());
        for (auto &&sdf_path : sdfpaths)
            primpaths.append(HUSD_Path(sdf_path).pathStr());
        return true;
    }

    UT_StringHolder
    husdGetLayerLabel(const SdfLayerHandle &layer)
    {
        UT_StringHolder          label;
        std::string              savecontrol;
        std::string              savepath;
        std::string              creator;

        if (layer->IsAnonymous())
        {
            UT_WorkBuffer	 buf;

            HUSDgetSaveControl(layer, savecontrol);
            HUSDgetSavePath(layer, savepath);
            HUSDgetCreatorNode(layer, creator);
            if (HUSD_Constants::getSaveControlPlaceholder() ==
                savecontrol)
            {
                buf.append("<placeholder>");
            }
            else if (HUSD_Constants::getSaveControlIsFileFromDisk() ==
                     savecontrol)
            {
                buf.sprintf("%s (modified)", savepath.c_str());
            }
            else if (!creator.empty())
            {
                if (!savepath.empty())
                    buf.sprintf("%s (%s)",
                        creator.c_str(), savepath.c_str());
                else
                    buf.sprintf("%s", creator.c_str());
            }
            else if (!savepath.empty())
            {
                buf.append(savepath.c_str());
            }
            else
                buf.append("<unknown name>");

            buf.stealIntoStringHolder(label);
        }
        else
            label = layer->GetDisplayName();

        return label;
    }

    class XUSD_FindPrimStatsTaskData : public XUSD_FindPrimsTaskData
    {
    public:
                 XUSD_FindPrimStatsTaskData(
                        HUSD_Info::DescendantStatsFlags flags);
                ~XUSD_FindPrimStatsTaskData() override;
        void addToThreadData(UsdPrim &prim) override;

        void gatherStatsFromThreads(UT_Options &stats);

    private:
        enum StatGroups {
            STAT_SIMPLE,
            STAT_PURPOSE_DEFAULT,
            STAT_PURPOSE_RENDER,
            STAT_PURPOSE_PROXY,
            STAT_PURPOSE_GUIDE,
            NUM_STAT_GROUPS
        };
        typedef std::map<SdfPath, UsdGeomImageable::PurposeInfo>
            PurposeInfoMap;

        class FindPrimStatsTaskThreadData
        {
            public:
                UT_StringMap<size_t> myStats[NUM_STAT_GROUPS];
                PurposeInfoMap myPurposeMap;
                std::map<SdfPath, size_t> myMasterPrims;
        };
        typedef UT_ThreadSpecificValue<FindPrimStatsTaskThreadData *>
            FindPrimStatsTaskThreadDataTLS;

        static const UsdGeomImageable::PurposeInfo &
        computePurposeInfo(PurposeInfoMap &map, const UsdPrim &prim)
        {
            auto it = map.find(prim.GetPath());

            if (it == map.end())
            {
                UsdPrim parent = prim.GetParent();

                if (parent)
                {
                    const auto &parent_info = computePurposeInfo(map, parent);
                    UsdGeomImageable imageable(prim);

                    if (imageable)
                        it = map.emplace(prim.GetPath(),
                            imageable.ComputePurposeInfo(parent_info)).first;
                    else
                        it = map.emplace(prim.GetPath(), parent_info).first;
                }
                else
                    it = map.emplace(prim.GetPath(),
                        UsdGeomImageable::PurposeInfo()).first;
            }

            return it->second;
        }

        UT_StringMap<size_t> &
        getStats(UsdPrim &prim, FindPrimStatsTaskThreadData &threadData)
        {
            if ((myFlags & HUSD_Info::STATS_PURPOSE_COUNTS) != 0 &&
                prim.IsA<UsdGeomImageable>())
            {
                const auto &info =
                    computePurposeInfo(threadData.myPurposeMap, prim);

                if (info.purpose == UsdGeomTokens->default_)
                    return threadData.myStats[STAT_PURPOSE_DEFAULT];
                else if (info.purpose == UsdGeomTokens->render)
                    return threadData.myStats[STAT_PURPOSE_RENDER];
                else if (info.purpose == UsdGeomTokens->proxy)
                    return threadData.myStats[STAT_PURPOSE_PROXY];
                else if (info.purpose == UsdGeomTokens->guide)
                    return threadData.myStats[STAT_PURPOSE_GUIDE];
            }

            return threadData.myStats[STAT_SIMPLE];
        }

        FindPrimStatsTaskThreadDataTLS    myThreadData;
        HUSD_Info::DescendantStatsFlags   myFlags;
    };

    XUSD_FindPrimStatsTaskData::XUSD_FindPrimStatsTaskData(
            HUSD_Info::DescendantStatsFlags flags)
        : myFlags(flags)
    {
    }

    XUSD_FindPrimStatsTaskData::~XUSD_FindPrimStatsTaskData()
    {
        for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
        {
            if(auto* tdata = it.get())
                delete tdata;
        }
    }

    void
    XUSD_FindPrimStatsTaskData::addToThreadData(UsdPrim &prim)
    {
        auto *&threadData = myThreadData.get();
        if(!threadData)
            threadData = new FindPrimStatsTaskThreadData;
        UT_StringMap<size_t> &stats = getStats(prim, *threadData);

        UT_StringRef primtype = prim.GetTypeName().GetText();
        if (!primtype.isstring())
            primtype = "Untyped";
        stats[primtype]++;

        UsdPrim master(prim.GetMaster());
        if (master)
            threadData->myMasterPrims[master.GetPath()]++;

        if ((myFlags & HUSD_Info::STATS_GEOMETRY_COUNTS) == 0)
            return;

        UsdGeomPointInstancer ptinstancer(prim);
        if (ptinstancer)
        {
            UsdAttribute indices = ptinstancer.GetProtoIndicesAttr();

            if (indices)
            {
                UT_WorkBuffer countkey;
                countkey.sprintf("%s (Instances)",
                    prim.GetTypeName().GetText());

                VtValue indicesvalue;
                indices.Get(&indicesvalue, UsdTimeCode::EarliestTime());
                size_t ptinstcount = indicesvalue.GetArraySize();
                stats[countkey.buffer()] += ptinstcount;
            }

            UsdRelationship prototypes = ptinstancer.GetPrototypesRel();

            if (prototypes)
            {
                UT_WorkBuffer countkey;
                countkey.sprintf("%s (Prototypes)",
                    prim.GetTypeName().GetText());

                SdfPathVector targets;
                prototypes.GetTargets(&targets);
                stats[countkey.buffer()] += targets.size();
            }
            return;
        }

        UsdGeomMesh mesh(prim);
        if (mesh)
        {
            UsdAttribute meshvc = mesh.GetFaceVertexCountsAttr();

            if (meshvc)
            {
                UT_WorkBuffer countkey;
                countkey.sprintf("%s (Polygons)", prim.GetTypeName().GetText());

                VtValue meshvcvalue;
                meshvc.Get(&meshvcvalue, UsdTimeCode::EarliestTime());
                size_t meshvccount = meshvcvalue.GetArraySize();
                stats[countkey.buffer()] += meshvccount;
            }
            return;
        }

        UsdGeomCurves curves(prim);
        if (curves)
        {
            UsdAttribute curvesvc = curves.GetCurveVertexCountsAttr();

            if (curvesvc)
            {
                UT_WorkBuffer countkey;
                countkey.sprintf("%s (Curves)", prim.GetTypeName().GetText());

                VtValue curvesvcvalue;
                curvesvc.Get(&curvesvcvalue, UsdTimeCode::EarliestTime());
                size_t curvesvccount = curvesvcvalue.GetArraySize();
                stats[countkey.buffer()] += curvesvccount;
            }
            return;
        }

        UsdGeomPoints points(prim);
        if (points)
        {
            UsdAttribute pointsvc = points.GetPointsAttr();

            if (pointsvc)
            {
                UT_WorkBuffer countkey;
                countkey.sprintf("%s (Points)", prim.GetTypeName().GetText());

                VtValue pointsvcvalue;
                pointsvc.Get(&pointsvcvalue, UsdTimeCode::EarliestTime());
                size_t pointsvccount = pointsvcvalue.GetArraySize();
                stats[countkey.buffer()] += pointsvccount;
            }
            return;
        }
    }

    void
    XUSD_FindPrimStatsTaskData::gatherStatsFromThreads(UT_Options &stats)
    {
        static const UT_StringHolder theStatSuffixes[NUM_STAT_GROUPS] = {
            ":Total",
            ":Default",
            ":Render",
            ":Proxy",
            ":Guide",
        };
        UT_WorkBuffer statbuf;
        std::map<SdfPath, size_t> masterprims;

        for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
        {
            if(const auto* tdata = it.get())
            {
                // Add up all the per-purpose primitive counts.
                for (int statidx = 0; statidx < NUM_STAT_GROUPS; statidx++)
                {
                    auto &tstats = tdata->myStats[statidx];

                    for (auto it = tstats.begin(); it != tstats.end(); ++it)
                    {
                        if (statidx > 0)
                        {
                            statbuf.strcpy(it->first);
                            statbuf.strcat(theStatSuffixes[statidx]);
                            stats.setOptionI(statbuf.buffer(),
                                stats.getOptionI(statbuf.buffer())+it->second);
                        }
                        statbuf.strcpy(it->first);
                        statbuf.strcat(theStatSuffixes[0]);
                        stats.setOptionI(statbuf.buffer(),
                            stats.getOptionI(statbuf.buffer())+it->second);
                    }
                }

                // Make a unified map of all master prims.
                for (auto it = tdata->myMasterPrims.begin();
                          it != tdata->myMasterPrims.end(); ++it)
                    masterprims[it->first] += it->second;
            }
        }
        if (!masterprims.empty())
        {
            size_t   totalinstances = 0;

            for (auto &&it : masterprims)
                totalinstances += it.second;
            stats.setOptionI("Instance Masters", masterprims.size());
            stats.setOptionI("Instances", totalinstances);
        }
    }
};

HUSD_Info::HUSD_Info(HUSD_AutoAnyLock &lock)
    : myAnyLock(lock)
{
}

HUSD_Info::~HUSD_Info()
{
}

/* static */ bool
HUSD_Info::isArrayValueType(const UT_StringRef &valueType)
{
    return SdfSchema::GetInstance().FindType(
	valueType.toStdString()).IsArray();
}

/* static */ bool
HUSD_Info::isTokenArrayValueType(const UT_StringRef &valueType)
{
    return (SdfSchema::GetInstance().FindType(valueType.toStdString()) ==
        SdfValueTypeNames->TokenArray);
}

/* static */ bool
HUSD_Info::isPrimvarName(const UT_StringRef &name)
{
    // The following codes copied from the pxf/usd/usdGeom/primvar.cpp,
    // preform exactly the same thing as the USD private function
    // _IsValidPrimvarName.
    static constexpr const char* primvarsPrefix = "primvars:";
    static constexpr const char* indicesSuffix = ":indices";
    return (TfStringStartsWith(name.toStdString(), primvarsPrefix) &&
            !TfStringEndsWith(name.toStdString(), indicesSuffix));
}

/* static */ void
HUSD_Info::getPrimitiveKinds(UT_StringArray &kinds)
{
    auto &&usd_kinds = KindRegistry::GetAllKinds();

    for(auto &kind : usd_kinds)
    {
	// "model" kind is just a base class. Derived classes are "concrete",
	// and can meaningfully be assigned to prims. Model seemingly cannot.
	if (kind == KindTokens->model)
	    continue;
	kinds.append(kind.GetText());
    }
}

/* static */ void
HUSD_Info::getUsdVersionInfo(UT_StringMap<UT_StringHolder> &info)
{

    static constexpr UT_StringLit thePackageUrlTag("packageurl");
    static constexpr UT_StringLit thePackageRevisionTag("packagerevision");
    static constexpr UT_StringLit theUsdVersionTag("usdversion");
    UT_WorkBuffer versionbuf;

    // If the user has built their own USD library, these defines may not
    // exist.
    #ifndef PXR_PACKAGE_URL
        #define PXR_PACKAGE_URL ""
    #endif
    #ifndef PXR_PACKAGE_REVISION
        #define PXR_PACKAGE_REVISION ""
    #endif
    versionbuf.sprintf("%d.%d", PXR_VERSION / 100, PXR_VERSION % 100);
    info[thePackageUrlTag.asHolder()] = PXR_PACKAGE_URL;
    info[thePackageRevisionTag.asHolder()] = PXR_PACKAGE_REVISION;
    info[theUsdVersionTag.asHolder()] = versionbuf.buffer();
}

/* static */ bool
HUSD_Info::reload(const UT_StringRef &filepath, bool recursive)
{
    SdfLayerHandle		 layer = SdfLayer::Find(filepath.toStdString());

    if (layer)
    {
	// Create an error scope to eat any errors triggered by the reload.
	UT_ErrorManager		 errmgr;
	HUSD_ErrorScope		 scope(&errmgr);

	// We don't want to call reload on anonymous layers, but if we are
	// passed an anonymous layer to reload, we still want to scan it for
	// external references and reload those.
	if (!layer->IsAnonymous())
            layer->Reload(true);

	if (recursive)
	{
	    std::set<std::string>	 all_layer_paths;
	    std::vector<SdfLayerHandle>	 layers_to_scan;
            std::set<SdfLayerHandle>     all_layers;

	    all_layer_paths.insert(filepath.toStdString());
	    layers_to_scan.push_back(layer);
            while (layers_to_scan.size() > 0)
            {
                std::vector<SdfLayerHandle>      new_layers_to_scan;
                std::set<SdfLayerHandle>         layers_to_reload;

                for (int i = 0; i < layers_to_scan.size(); i++)
                {
                    std::set<std::string>	 refs;

                    refs = layers_to_scan[i]->GetExternalReferences();
                    for (auto &&path : refs)
                    {
                        if (!SdfLayer::IsAnonymousLayerIdentifier(path))
                        {
                            std::string	 fullpath;

                            fullpath = layers_to_scan[i]->
                                ComputeAbsolutePath(path);
                            if (all_layer_paths.find(fullpath) ==
                                    all_layer_paths.end())
                            {
                                layer = SdfLayer::Find(fullpath);
                                if (layer &&
                                    all_layers.find(layer) == all_layers.end())
                                {
                                    layers_to_reload.insert(layer);
                                    new_layers_to_scan.push_back(layer);
                                    all_layers.insert(layer);
                                    all_layer_paths.insert(fullpath);
                                }
                            }
                        }
                    }
                }
                SdfLayer::ReloadLayers(layers_to_reload, true);
                layers_to_scan.swap(new_layers_to_scan);
            }
	}

        // Clear the whole cache of automatic ref prim paths, because the
        // layers we are reloading may be used by any stage, and so may affect
        // the default/automatic default prim of any stage.
        HUSDclearBestRefPathCache();

	return true;
    }

    return false;
}

bool
HUSD_Info::isStageValid() const
{
    bool		 valid = false;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
	valid = true;

    return valid;
}

bool
HUSD_Info::getSourceLayers(UT_StringArray &names,
	UT_StringArray &identifiers,
	UT_IntArray &anonymous,
        UT_IntArray &fromsops) const
{
    bool		 success = false;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
    {
	auto	&sublayers = myAnyLock.constData()->sourceLayers();

	// Return layers in strongest to weakest order (the reverse order
	// of the source layers array).
	for (int i = sublayers.size(); i --> 0; )
	{
	    names.append(husdGetLayerLabel(sublayers(i).myLayer));
	    anonymous.append(sublayers(i).myLayer->IsAnonymous());
	    fromsops.append(HUSDisSopLayer(sublayers(i).myLayer));
	    identifiers.append(sublayers(i).myIdentifier);
	}
	success = true;
    }

    return success;
}

static void
husdGetLayerHierarchy(const SdfLayerHandle &layer,
	UT_InfoTree &hierarchy)
{
    if (layer)
    {
	UT_InfoTree *child_tree = hierarchy.addChildMap(layer->GetIdentifier());

	for (auto path : layer->GetSubLayerPaths())
	{
	    SdfLayerHandle	 child_layer = SdfLayer::Find(path);

	    husdGetLayerHierarchy(child_layer, *child_tree);
	}
    }
}

bool
HUSD_Info::getLayerHierarchy(UT_InfoTree &hierarchy) const
{
    bool		 success = false;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
    {
	ArResolverContextBinder binder(
	    myAnyLock.constData()->stage()->GetPathResolverContext());
	auto &layers = myAnyLock.constData()->sourceLayers();

	for (auto &&layer : layers)
	    husdGetLayerHierarchy(layer.myLayer, hierarchy);
	success = true;
    }

    return success;
}

bool
HUSD_Info::getLayerSavePath(UT_StringHolder &savepath) const
{
    bool		 success = false;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
    {
        SdfLayerRefPtr   layer = myAnyLock.constData()->activeLayer();

        if (layer)
        {
            std::string      savelocation;

            success = HUSDgetSavePath(layer, savelocation);
            savepath = savelocation;
        }
    }

    return success;
}

bool
HUSD_Info::getLayersAboveLayerBreak(UT_StringArray &identifiers) const
{
    bool		 success = false;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
    {
        std::set<std::string> stdidentifiers;

        stdidentifiers = myAnyLock.constData()->
            getStageLayersToRemoveFromLayerBreak();
        for (auto &&identifier : stdidentifiers)
            identifiers.append(identifier);
        success = true;
    }

    return success;
}

bool
HUSD_Info::getLayerExists(const UT_StringRef &filepath) const
{
    SdfLayerRefPtr	 layer;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
    {
	ArResolverContextBinder binder(
	    myAnyLock.constData()->stage()->GetPathResolverContext());

	layer = SdfLayer::FindOrOpen(filepath.toStdString());
    }
    else
	layer = SdfLayer::FindOrOpen(filepath.toStdString());

    return layer;
}

bool
HUSD_Info::getStageRootLayer(UT_StringHolder &identifier) const
{
    bool		 success = false;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
    {
	identifier = myAnyLock.constData()->stage()->
	    GetRootLayer()->GetIdentifier();
	success = true;
    }

    return success;
}

bool
HUSD_Info::getStartTimeCode(fpreal64 &starttimecode) const
{
    bool		 success = false;

    starttimecode = 0.0;
    if (myAnyLock.constData() &&
        myAnyLock.constData()->isStageValid())
    {
        auto stage = myAnyLock.constData()->stage();
        if (stage->HasAuthoredTimeCodeRange())
        {
            starttimecode = stage->GetStartTimeCode();
            success = true;
        }
    }

    return success;
}

bool
HUSD_Info::getEndTimeCode(fpreal64 &endtimecode) const
{
    bool		 success = false;

    endtimecode = 0.0;
    if (myAnyLock.constData() &&
        myAnyLock.constData()->isStageValid())
    {
        auto stage = myAnyLock.constData()->stage();
        if (stage->HasAuthoredTimeCodeRange())
        {
            endtimecode = stage->GetEndTimeCode();
            success = true;
        }
    }

    return success;
}

bool
HUSD_Info::getFramesPerSecond(fpreal64 &fps) const
{
    bool		 success = false;

    fps = 24.0;
    if (myAnyLock.constData() &&
        myAnyLock.constData()->isStageValid())
    {
        auto stage = myAnyLock.constData()->stage();
        fps = stage->GetFramesPerSecond();
        success = true;
    }

    return success;
}

bool
HUSD_Info::getTimeCodesPerSecond(fpreal64 &tcs) const
{
    bool		 success = false;

    tcs = 24.0;
    if (myAnyLock.constData() &&
        myAnyLock.constData()->isStageValid())
    {
        auto stage = myAnyLock.constData()->stage();
        tcs = stage->GetTimeCodesPerSecond();
        success = true;
    }

    return success;
}

bool
HUSD_Info::getMetrics(UT_StringHolder &upaxis,
        fpreal64 &metersperunit) const
{
    bool		 success = false;

    upaxis = UsdGeomGetFallbackUpAxis().GetString();
    metersperunit = 0.01;
    if (myAnyLock.constData() &&
        myAnyLock.constData()->isStageValid())
    {
        auto stage = myAnyLock.constData()->stage();
        metersperunit = UsdGeomGetStageMetersPerUnit(stage);
        upaxis = UsdGeomGetStageUpAxis(stage).GetString();
        success = true;
    }

    return success;
}

UT_StringHolder
HUSD_Info::getCurrentRenderSettings() const
{
    UT_StringHolder      path;

    if (myAnyLock.constData() &&
        myAnyLock.constData()->isStageValid())
    {
        auto stage = myAnyLock.constData()->stage();
        auto settings = UsdRenderSettings::GetStageRenderSettings(stage);
        if (settings)
            path = HUSD_Path(settings.GetPrim().GetPath()).pathStr();
    }

    return path;
}

bool
HUSD_Info::getAllRenderSettings(UT_StringArray &paths) const
{
    bool		 success = false;

    if (myAnyLock.constData() &&
        myAnyLock.constData()->isStageValid())
    {
        auto stage = myAnyLock.constData()->stage();
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
        success = true;
    }

    return success;
}

bool
HUSD_Info::getVariantSets(const UT_StringRef &primpath,
	UT_StringArray &vset_names) const
{
    bool		 success = false;

    if (primpath.isstring())
    {
	if (myAnyLock.constData() &&
	    myAnyLock.constData()->isStageValid())
	{
	    SdfPath sdfpath(HUSDgetSdfPath(primpath));
	    auto prim = myAnyLock.constData()->stage()->GetPrimAtPath(sdfpath);

	    if (prim)
	    {
		auto vsets = prim.GetVariantSets();
		std::vector<std::string> names = vsets.GetNames();

		for (auto &&name : names)
		    vset_names.append(name);
		success = true;
	    }
	}
    }

    return success;
}

bool
HUSD_Info::getVariants(const UT_StringRef &primpath,
	const UT_StringRef &variantset,
	UT_StringArray &vset_names) const
{
    bool		 success = false;

    if (primpath.isstring() && variantset.isstring())
    {
	if (myAnyLock.constData() &&
	    myAnyLock.constData()->isStageValid())
	{
	    SdfPath sdfpath(HUSDgetSdfPath(primpath));
	    auto prim = myAnyLock.constData()->stage()->GetPrimAtPath(sdfpath);

	    if (prim)
	    {
		auto vset = prim.GetVariantSet(variantset.toStdString());
		std::vector<std::string> names = vset.GetVariantNames();

		for (auto &&name : names)
		    vset_names.append(name);
		success = true;
	    }
	}
    }

    return success;
}

UT_StringHolder
HUSD_Info::getVariantSelection(const UT_StringRef &primpath,
	const UT_StringRef &variantset) const
{
    UT_StringHolder	 variant_selection;

    if (primpath.isstring() && variantset.isstring())
    {
	if (myAnyLock.constData() &&
	    myAnyLock.constData()->isStageValid())
	{
	    SdfPath sdfpath(HUSDgetSdfPath(primpath));
	    auto prim = myAnyLock.constData()->stage()->GetPrimAtPath(sdfpath);

	    if (prim)
	    {
		auto vsets = prim.GetVariantSets();
		std::string vsetstr(variantset.toStdString());

		if (vsets.HasVariantSet(vsetstr))
		    variant_selection = vsets[vsetstr].GetVariantSelection();
	    }
	}
    }

    return variant_selection;
}


static inline UsdCollectionAPI
husdGetCollectionAPI(HUSD_AutoAnyLock &lock, const UT_StringRef &collectionpath)
{
    if (!collectionpath.isstring())
	return UsdCollectionAPI();

    auto data = lock.constData();
    if( !data || !data->isStageValid() )
	return UsdCollectionAPI();

    SdfPath sdfpath(HUSDgetSdfPath(collectionpath));
    return UsdCollectionAPI::Get(data->stage(), sdfpath);
}

bool
HUSD_Info::isCollectionAtPath(const UT_StringRef &collectionpath) const
{
    return (bool) husdGetCollectionAPI(myAnyLock, collectionpath);
}

UT_StringHolder	
HUSD_Info::getCollectionExpansionRule(const UT_StringRef &collectionpath) const
{
    auto api = husdGetCollectionAPI(myAnyLock, collectionpath);
    if( !api )
	return UT_StringHolder();

    auto attr = api.GetExpansionRuleAttr();
    if( !attr )
	return UT_StringHolder();

    TfToken rule;
    attr.Get(&rule);
    if( rule.IsEmpty() )
	rule = UsdTokens->expandPrims; // that's the USD default

    return UT_StringHolder( rule.GetString() );
}

static inline bool
husdGetCollectionRelationshipPaths( UT_StringArray &primpaths,
	HUSD_AutoAnyLock &lock, const UT_StringRef &collectionpath, 
	UsdRelationship (UsdCollectionAPI::*method)() const)
{
    auto api = husdGetCollectionAPI(lock, collectionpath);
    if( !api )
	return false;

    SdfPathVector sdfpaths;
    (api.*method)().GetTargets(&sdfpaths);

    return husdSetPrimpaths( primpaths, sdfpaths );
}

bool
HUSD_Info::getCollectionIncludePaths( const UT_StringRef &collectionpath,
	UT_StringArray &primpaths) const
{
    return husdGetCollectionRelationshipPaths(primpaths, myAnyLock,
	    collectionpath, &UsdCollectionAPI::GetIncludesRel);
}

bool
HUSD_Info::getCollectionExcludePaths( const UT_StringRef &collectionpath,
	UT_StringArray &primpaths) const
{
    return husdGetCollectionRelationshipPaths(primpaths, myAnyLock,
	    collectionpath, &UsdCollectionAPI::GetExcludesRel);
}

bool
HUSD_Info::getCollectionComputedPaths( const UT_StringRef &collectionpath,
	UT_StringArray &primpaths) const
{
    auto api = husdGetCollectionAPI(myAnyLock, collectionpath);
    if( !api )
	return false;

    auto query = api.ComputeMembershipQuery();
    auto sdfpaths = UsdCollectionAPI::ComputeIncludedPaths(query,
        myAnyLock.constData()->stage());

    husdSetPrimpaths(primpaths, sdfpaths);

    return true;
}

bool
HUSD_Info::collectionContains( const UT_StringRef &collectionpath,
	const UT_StringRef &primpath) const
{
    auto api = husdGetCollectionAPI(myAnyLock, collectionpath);
    if( !api )
	return false;

    auto query = api.ComputeMembershipQuery();
    return query.IsPathIncluded(HUSDgetSdfPath(primpath));
}

bool
HUSD_Info::getCollections(const UT_StringRef &primpath,
        HUSD_CollectionInfoMap &collection_info_map) const
{
    auto prim = husdGetPrim(myAnyLock, primpath);
    if (!prim) 
	return false;

    std::vector<UsdCollectionAPI>	 collections;
    collections = UsdCollectionAPI::GetAllCollections(prim);

    for (auto &&collection : collections)
    {
        UsdRelationship include_rel = collection.GetIncludesRel();
        UT_StringHolder icon;

        if (include_rel)
        {
            auto data=include_rel.GetCustomData();
            auto it=data.find(HUSD_Constants::getIconCustomDataName().c_str());

            if (it != data.end())
                icon = it->second.Get<std::string>();
        }
	collection_info_map.emplace(
	    HUSD_Path(collection.GetCollectionPath()).pathStr(),
            icon);
    }

    return true;
}

UT_StringHolder
HUSD_Info::getAncestorOfKind(const UT_StringRef &primpath,
	const UT_StringRef &kind) const
{
    UT_StringHolder	 kindpath;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
    {
	SdfPath sdfpath(HUSDgetSdfPath(primpath));
	TfToken tfkind(kind.toStdString());
	auto prim = myAnyLock.constData()->stage()->GetPrimAtPath(sdfpath);

	while (prim)
	{
	    UsdModelAPI	 modelapi(prim);
	    TfToken	 primkind;

	    if (modelapi &&
		modelapi.GetKind(&primkind) &&
		KindRegistry::IsA(primkind, tfkind))
	    {
		kindpath = HUSD_Path(prim.GetPath()).pathStr();
		break;
	    }
	    else
		prim = prim.GetParent();
	}
    }

    return kindpath;
}

UT_StringHolder
HUSD_Info::getAncestorInstanceRoot(const UT_StringRef &primpath) const
{
    UT_StringHolder	 instancerootpath;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
    {
	SdfPath sdfpath(HUSDgetSdfPath(primpath));
	auto prim = myAnyLock.constData()->stage()->GetPrimAtPath(sdfpath);

	while (prim)
	{
	    if (!prim.IsInstanceProxy())
	    {
		instancerootpath = HUSD_Path(prim.GetPath()).pathStr();
		break;
	    }
	    else
		prim = prim.GetParent();
	}
    }

    return instancerootpath;
}

static inline UsdPrim
husdGetPrimAtPath(HUSD_AutoAnyLock &lock, const UT_StringRef &primpath) 
{
    UsdPrim prim;

    if (primpath.isstring() &&
	lock.constData() &&
	lock.constData()->isStageValid())
    {
	SdfPath sdfpath(HUSDgetSdfPath(primpath));
	prim = lock.constData()->stage()->GetPrimAtPath(sdfpath);
    }

    return prim;
}

bool
HUSD_Info::isPrimAtPath(const UT_StringRef &primpath,
	const UT_StringRef &prim_type) const
{
    bool isprim = false;

    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    if (prim &&
	(!prim_type.isstring() || prim_type == prim.GetTypeName().GetString()))
    {
	isprim = true;
    }

    return isprim;
}

bool
HUSD_Info::isActive(const UT_StringRef &primpath) const
{
    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    return prim && prim.IsActive();
}

static inline HUSD_TimeSampling 
husdGetVisibleTimeSampling(UsdPrim prim)
{
    HUSD_TimeSampling sampling = HUSD_TimeSampling::NONE;

    while( prim )
    {
	UsdGeomImageable api(prim);
	if( api )
	    HUSDupdateValueTimeSampling( sampling, api.GetVisibilityAttr() );

	if( sampling == HUSD_TimeSampling::MULTIPLE )
	    break; // can't get any higher

	prim = prim.GetParent();
    }

    return sampling;
}

bool
HUSD_Info::isVisible(const UT_StringRef &primpath,
	const HUSD_TimeCode &time_code, HUSD_TimeSampling *time_sampling ) const
{
    UsdGeomImageable	 imageable(husdGetPrimAtPath(myAnyLock, primpath));
    if( !imageable )
	return false;

    if( time_sampling != nullptr )
	*time_sampling = husdGetVisibleTimeSampling( imageable.GetPrim() );

    UsdTimeCode usd_tc = HUSDgetNonDefaultUsdTimeCode(time_code);
    return imageable.ComputeVisibility(usd_tc) != UsdGeomTokens->invisible;
}
    
bool
HUSD_Info::isInstance(const UT_StringRef &primpath) const
{
    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    return prim && prim.IsInstance();
}

UT_StringHolder
HUSD_Info::getKind(const UT_StringRef &primpath) const
{
    UT_StringHolder	 kind;
    TfToken		 kind_tk;

    UsdModelAPI model_api(husdGetPrimAtPath(myAnyLock, primpath));
    if (model_api && model_api.GetKind(&kind_tk))
	kind = kind_tk.GetString();

    return kind;
}

bool
HUSD_Info::isKind(const UT_StringRef &primpath, const UT_StringRef &kind) const
{
    TfToken		 kind_tk;

    UsdModelAPI model_api(husdGetPrimAtPath(myAnyLock, primpath));
    return model_api && model_api.GetKind(&kind_tk) &&
	KindRegistry::IsA(kind_tk, TfToken(kind.toStdString()));
}

UT_StringHolder
HUSD_Info::getPrimType(const UT_StringRef &primpath) const
{
    UT_StringHolder	 primtype;

    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    if (prim)
	primtype = prim.GetTypeName().GetString();

    return primtype;
}

bool
HUSD_Info::isPrimType(const UT_StringRef &ppath, const UT_StringRef &type) const
{
    const TfType    &tf_type = HUSDfindType(type);
    auto	    prim = husdGetPrimAtPath(myAnyLock, ppath);

    return prim && prim.IsA(tf_type);
}

bool
HUSD_Info::hasPrimAPI(const UT_StringRef &ppath, const UT_StringRef &type) const
{
    const TfType    &tf_type = HUSDfindType(type);
    auto	    prim = husdGetPrimAtPath(myAnyLock, ppath);

    return prim && prim.HasAPI(tf_type);
}

bool
HUSD_Info::hasPayload(const UT_StringRef &primpath) const
{
    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    return prim && prim.HasAuthoredPayloads();
}

UT_StringHolder
HUSD_Info::getIcon(const UT_StringRef &primpath) const
{
    UsdPrim     	 prim(husdGetPrimAtPath(myAnyLock, primpath));
    UT_StringHolder	 icon;

    // This function's logic must be kept in sync with the ptyhon function
    // usdprimicons.getIconForPrim().
    if (prim)
    {
        auto data = prim.GetCustomData();
        auto it = data.find(HUSD_Constants::getIconCustomDataName().c_str());

        if (it != data.end())
            icon = it->second.Get<std::string>();

        if (!icon.isstring())
        {
            UsdLuxShapingAPI     shaping(prim);

            if (shaping)
                icon = "SCENEGRAPH_shapedlight";
        }

        if (!icon.isstring())
        {
            static UT_Map<PrimInfo, UT_StringHolder> thePrimIconMap;
            TfToken primtype;
            TfToken primkind;

            primtype = prim.GetTypeName();
            UsdModelAPI(prim).GetKind(&primkind);
            const PrimInfo priminfo = {
                UT_StringHolder(primtype.GetString()),
                UT_StringHolder(primkind.GetString())
            };

            if (!thePrimIconMap.contains(priminfo))
            {
                UT_WorkBuffer	 expr;
                PY_Result	 result;

                expr.sprintf(
                    "__import__('usdprimicons')."
                    "getIconForPrimTypeAndKind('%s', '%s')",
                    primtype.GetString().c_str(),
                    primkind.GetString().c_str());
                result = PYrunPythonExpression(
                    expr.buffer(), PY_Result::STRING);
                if (result.myResultType == PY_Result::STRING)
                    thePrimIconMap[priminfo] = result.myStringValue;
                else
                    thePrimIconMap[priminfo] = "";
            }

            icon = thePrimIconMap[priminfo];
        }
    }

    return icon;
}

UT_StringHolder
HUSD_Info::getPurpose(const UT_StringRef &primpath) const
{
    UT_StringHolder	 purpose;
    UsdGeomImageable	 imageable(husdGetPrimAtPath(myAnyLock, primpath));

    if (imageable)
	purpose = imageable.ComputePurpose().GetString();

    return purpose;
}

UT_StringHolder
HUSD_Info::getDrawMode(const UT_StringRef &primpath) const
{
    UT_StringHolder	 drawmode;
    auto		 prim = husdGetPrimAtPath(myAnyLock, primpath);

    if (prim && !prim.IsPseudoRoot() && !prim.IsModel())
    {
	UsdGeomModelAPI	api(prim);
	drawmode = api.ComputeModelDrawMode().GetString();
    }

    return drawmode;
}

UT_StringHolder
HUSD_Info::getAutoParentPrimKind(const UT_StringRef &primpath) const
{
    auto		 prim = husdGetPrimAtPath(myAnyLock, primpath);

    if (prim)
    {
	UsdModelAPI	 modelapi(prim);
	TfToken		 childkind;

	if (modelapi && modelapi.GetKind(&childkind))
	{
	    TfToken parentkind = HUSDgetParentKind(childkind);

	    if (!parentkind.IsEmpty())
		return parentkind.GetString();
	}
    }

    return UT_StringHolder::theEmptyString;
}

bool
HUSD_Info::hasChildren(const UT_StringRef &primpath) const
{
    auto		 prim = husdGetPrimAtPath(myAnyLock, primpath);

    if (prim)
	return !prim.GetAllChildren().empty();

    return false;
}

void
HUSD_Info::getChildren(const UT_StringRef &primpath,
	UT_StringArray &childnames) const
{
    auto		 prim = husdGetPrimAtPath(myAnyLock, primpath);

    if (prim)
    {
	auto		 children = prim.GetAllChildren();

	for (auto &&child : children)
	    childnames.append(child.GetName().GetText());
    }
}

void
HUSD_Info::getDescendantStats(const UT_StringRef &primpath,
        UT_Options &stats,
        DescendantStatsFlags flags) const
{
    auto		 prim = husdGetPrimAtPath(myAnyLock, primpath);

    if (prim)
    {
        auto demands = HUSD_PrimTraversalDemands(
            HUSD_TRAVERSAL_DEFAULT_DEMANDS |
            HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES);
        auto predicate = HUSDgetUsdPrimPredicate(demands);
        XUSD_FindPrimStatsTaskData data(flags);
        auto &task = *new(UT_Task::allocate_root())
            XUSD_FindPrimsTask(prim, data, predicate, nullptr, nullptr);
        UT_Task::spawnRootAndWait(task);

        data.gatherStatsFromThreads(stats);
    }
}

UT_StringHolder	
HUSD_Info::getBoundMaterial(const UT_StringRef &primpath) const
{
    auto prim = husdGetPrimAtPath(myAnyLock, primpath); 
    if( !prim )
	return UT_StringHolder();

    UsdShadeMaterialBindingAPI api( prim );
    auto material = api.ComputeBoundMaterial();
    if( !material )
	return UT_StringHolder();

    return HUSD_Path(material.GetPath()).pathStr();
}


template <typename F>
UT_Matrix4D
husdGetXformMatrix(HUSD_AutoAnyLock &lock, const UT_StringRef &primpath,
	const HUSD_TimeCode &tc, F callback)
{

    UsdGeomXformable	 xformable(husdGetPrimAtPath(lock, primpath));
    UT_Matrix4D		 xform;

    xform.zero();
    if( !xformable )
	return xform;

    UsdTimeCode usd_tc = HUSDgetNonDefaultUsdTimeCode(tc);

    GfMatrix4d	gf_xform;
    if( callback( xformable, gf_xform, usd_tc ))
	xform = GusdUT_Gf::Cast(gf_xform);

    return xform;
}

UT_Matrix4D
HUSD_Info::getLocalXform(const UT_StringRef &primpath,
	const HUSD_TimeCode &time_code, HUSD_TimeSampling *time_sampling) const
{
    if( time_sampling != nullptr )
	*time_sampling = HUSDgetLocalTransformTimeSampling(
		husdGetPrimAtPath(myAnyLock, primpath));

    return husdGetXformMatrix( myAnyLock, primpath, time_code,
	    []( const UsdGeomXformable &xformable, GfMatrix4d &gf_xform,
		 UsdTimeCode usd_tc )
	    {
		bool is_reset;
		return xformable.GetLocalTransformation(&gf_xform, &is_reset, 
			usd_tc);
	    });
}

UT_Matrix4D
HUSD_Info::getWorldXform(const UT_StringRef &primpath,
	const HUSD_TimeCode &time_code, HUSD_TimeSampling *time_sampling) const
{
    if( time_sampling != nullptr )
	*time_sampling = HUSDgetWorldTransformTimeSampling(
		husdGetPrimAtPath(myAnyLock, primpath));

    return husdGetXformMatrix( myAnyLock, primpath, time_code,
	    []( const UsdGeomXformable &xformable, GfMatrix4d &gf_xform,
		 UsdTimeCode usd_tc )
	    {
		gf_xform = xformable.ComputeLocalToWorldTransform( usd_tc );
		return true;
	    });
}

UT_Matrix4D
HUSD_Info::getParentXform(const UT_StringRef &primpath,
	const HUSD_TimeCode &time_code, HUSD_TimeSampling *time_sampling) const
{
    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    if( time_sampling != nullptr && prim )
	*time_sampling = HUSDgetWorldTransformTimeSampling( prim.GetParent() );

    return husdGetXformMatrix( myAnyLock, primpath, time_code,
	    []( const UsdGeomXformable &xformable, GfMatrix4d &gf_xform,
		 UsdTimeCode usd_tc )
	    {
		gf_xform = xformable.ComputeParentToWorldTransform( usd_tc );
		return true;
	    });
}

bool
HUSD_Info::getXformOrder(const UT_StringRef &primpath,
	UT_StringArray &xform_order) const
{
    xform_order.clear();

    UsdGeomXformable xformable(husdGetPrimAtPath(myAnyLock, primpath));
    if( !xformable )
	return false;

    bool is_reset;
    for( auto &&it : xformable.GetOrderedXformOps( &is_reset ))
	xform_order.append( it.GetOpName().GetString() );

    return true;
}

bool
HUSD_Info::isXformReset(const UT_StringRef &primpath ) const
{
    UsdGeomXformable xformable(husdGetPrimAtPath(myAnyLock, primpath));
    if( !xformable )
	return false;

    return xformable.GetResetXformStack();
}

UT_BoundingBoxD
HUSD_Info::getBounds(const UT_StringRef &primpath,
	const UT_StringArray &purposes, const HUSD_TimeCode &time_code) const
{
    UT_BoundingBoxD bbox;

    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    if( !prim )
    {
	bbox.makeInvalid();
	return bbox;
    }

    TfTokenVector tf_purposes;
    for (auto &&purpose : purposes)
	tf_purposes.push_back( TfToken( purpose.toStdString() ));

    auto		usd_tc = HUSDgetNonDefaultUsdTimeCode(time_code);
    UsdGeomBBoxCache	bbox_cache( usd_tc, tf_purposes );

    GfBBox3d gf_bbox   = bbox_cache.ComputeUntransformedBound( prim );
    GfRange3d gf_range = gf_bbox.ComputeAlignedRange();

    bbox.setBounds(
	    gf_range.GetMin()[0], gf_range.GetMin()[1], gf_range.GetMin()[2],
	    gf_range.GetMax()[0], gf_range.GetMax()[1], gf_range.GetMax()[2] );
    return bbox;
}

UT_StringHolder
HUSD_Info::findXformName(const UT_StringRef &primpath,
	const UT_StringRef &name_suffix) const
{
    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    if (!prim)
	return UT_StringHolder();

    // Iterate over enums, skipping Invalid value (0); ie start at 1.
    for( int i = 1; i <= (int) HUSD_XformType::Transform; i++ )
    {
	auto full_name = HUSDgetXformName( (HUSD_XformType) i, name_suffix );
	if( prim.HasAttribute( TfToken( full_name )))
	    return full_name;
    }

    return UT_StringHolder();
}

UT_StringHolder
HUSD_Info::getUniqueXformName(const UT_StringRef &primpath,
	HUSD_XformType type, const UT_StringRef &name_suffix) const
{
    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    if (!prim)
	return UT_StringHolder();

    UT_String full_name( HUSDgetXformName( type, name_suffix ));
    while (prim.HasAttribute(TfToken(full_name)))
	full_name.incrementNumberedName();

    return UT_StringHolder( full_name );
}

bool
HUSD_Info::getPointInstancerXforms( const UT_StringRef &primpath,
				UT_Array<UT_Matrix4D> &xforms,
				const HUSD_TimeCode &time_code)
{
    UsdGeomPointInstancer api(husdGetPrimAtPath(myAnyLock, primpath));
    if (!api)
	return false;

    auto		usd_tc = HUSDgetNonDefaultUsdTimeCode(time_code);
    VtArray<GfMatrix4d> gf_xforms;
    if( !api.ComputeInstanceTransformsAtTime( &gf_xforms, usd_tc, usd_tc,
		UsdGeomPointInstancer::ProtoXformInclusion::IncludeProtoXform,
		UsdGeomPointInstancer::MaskApplication::IgnoreMask  ))
	return false;

    exint n = gf_xforms.size();
    xforms.setSizeNoInit( n );
    for( exint i = 0; i < n; i++ )
	xforms[i] = GusdUT_Gf::Cast(gf_xforms[i]);

    return true;
}

UT_BoundingBoxD	
HUSD_Info::getPointInstancerBounds(const UT_StringRef &primpath,
	exint instance_index, const UT_StringArray &purposes,
	const HUSD_TimeCode &time_code) const
{
    UT_BoundingBoxD bbox;

    UsdGeomPointInstancer api(husdGetPrimAtPath(myAnyLock, primpath));
    if (!api)
    {
	bbox.makeInvalid();
	return bbox;
    }

    TfTokenVector tf_purposes;
    for (auto &&purpose : purposes)
	tf_purposes.push_back( TfToken( purpose.toStdString() ));

    auto		usd_tc = HUSDgetNonDefaultUsdTimeCode(time_code);
    UsdGeomBBoxCache	bbox_cache( usd_tc, tf_purposes );

    GfBBox3d gf_bbox = bbox_cache.ComputePointInstanceUntransformedBound(
	    api, instance_index );
    GfRange3d gf_range = gf_bbox.ComputeAlignedRange();

    bbox.setBounds(
	    gf_range.GetMin()[0], gf_range.GetMin()[1], gf_range.GetMin()[2],
	    gf_range.GetMax()[0], gf_range.GetMax()[1], gf_range.GetMax()[2] );
    return bbox;
}

static inline UT_StringHolder
husdPropertyPath(const UT_StringRef &primpath, const UT_StringRef &attribname)
{
    SdfPath sdfprimpath(HUSDgetSdfPath(primpath));
    TfToken propname(attribname.toStdString());
    SdfPath sdfattribpath(sdfprimpath.AppendProperty(propname));
    
    return HUSD_Path(sdfattribpath).pathStr();
}

template <typename T>
static inline T
husdGetObjAtPath( HUSD_AutoAnyLock &lock, const UT_StringRef &path) 
{
    T result;

    if (path.isstring() &&
	lock.constData() && lock.constData()->isStageValid())
    {
	SdfPath sdfpath(HUSDgetSdfPath(path));
	auto obj = lock.constData()->stage()->GetObjectAtPath(sdfpath);
	result = obj.As<T>();
    }

    return result;
}

static inline UsdAttribute
husdGetAttribAtPath( HUSD_AutoAnyLock &lock, const UT_StringRef &attribpath) 
{
    return husdGetObjAtPath<UsdAttribute>(lock, attribpath);
}

bool
HUSD_Info::isAttribAtPath(const UT_StringRef &attribpath, 
	HUSD_Info::QueryAspect query) const
{
    auto attrib = husdGetAttribAtPath(myAnyLock, attribpath);
    if (!attrib)
	return false;

    if (query == QueryAspect::ARRAY)
	return attrib.GetTypeName().IsArray();

    return true; // QueryAspect::ANY
}

bool
HUSD_Info::isAttribAtPath(const UT_StringRef &primpath,
	const UT_StringRef &attribname, HUSD_Info::QueryAspect query) const
{
    return isAttribAtPath(husdPropertyPath(primpath, attribname), query);
}

exint
HUSD_Info::getAttribLength(const UT_StringRef &attribpath,
	const HUSD_TimeCode &time_code, HUSD_TimeSampling *time_sampling ) const
{
    exint length = 0;

    auto attrib = husdGetAttribAtPath(myAnyLock, attribpath);
    if (attrib && !attrib.GetTypeName().IsArray())
    {
	// Non-array values have a conceptual length of 1.
	length = 1; 
	if( time_sampling )
	    *time_sampling = HUSD_TimeSampling::NONE;
    }
    else if (attrib)
    {
	// Need to evaluate the attribute to find out the actual array length.
	auto	usd_tc = HUSDgetNonDefaultUsdTimeCode(time_code);
	VtValue value;
	if( attrib.Get(&value, usd_tc))
	    length = value.GetArraySize();
	if( time_sampling )
	    *time_sampling = HUSDgetValueTimeSampling( attrib );
    }

    return length;
}

exint
HUSD_Info::getAttribLength(const UT_StringRef &primpath,
	const UT_StringRef &attribname, 
	const HUSD_TimeCode &time_code, HUSD_TimeSampling *time_sampling ) const
{
    return getAttribLength(husdPropertyPath(primpath, attribname), time_code,
	    time_sampling);
}

exint
HUSD_Info::getAttribSize(const UT_StringRef &attribpath) const
{
    exint size = 0;

    auto attrib = husdGetAttribAtPath(myAnyLock, attribpath);
    if (attrib && attrib.GetTypeName().GetDimensions().size == 0)
	size = 1; // plain scalar; not a tuple
    else if (attrib && attrib.GetTypeName().GetDimensions().size == 1)
	size = attrib.GetTypeName().GetDimensions().d[0];
    else if (attrib && attrib.GetTypeName().GetDimensions().size == 2)
	size = attrib.GetTypeName().GetDimensions().d[0] * 
	       attrib.GetTypeName().GetDimensions().d[1];

    return size;
}

exint
HUSD_Info::getAttribSize(const UT_StringRef &primpath,
	const UT_StringRef &attribname) const
{
    return getAttribSize(husdPropertyPath(primpath, attribname));
}

UT_StringHolder	
HUSD_Info::getAttribTypeName(const UT_StringRef &attribpath) const
{
    UT_StringHolder type_name;

    auto attrib = husdGetAttribAtPath(myAnyLock, attribpath);
    if (attrib)
	type_name = attrib.GetTypeName().GetAsToken().GetString();

    return type_name;
}

UT_StringHolder	
HUSD_Info::getAttribTypeName(const UT_StringRef &primpath,
	const UT_StringRef &attribname) const
{
    return getAttribTypeName(husdPropertyPath(primpath, attribname));
}

bool
HUSD_Info::getAttribTimeSamples(const UT_StringRef &attribpath,
	UT_FprealArray &time_samples) const
{
    auto attrib = husdGetAttribAtPath(myAnyLock, attribpath);
    if (!attrib)
	return false;

    std::vector< double > times;
    if( !attrib.GetTimeSamples( &times ))
	return false;

    time_samples.setSize( times.size() );
    for( exint i = 0; i < times.size(); i++ )
	time_samples[i] = times[i];

    return true;
}

bool
HUSD_Info::getAttribTimeSamples(const UT_StringRef &primpath,
	const UT_StringRef &attribname, UT_FprealArray &time_samples) const
{
    return getAttribTimeSamples(husdPropertyPath(primpath, attribname), 
	    time_samples);
}

const UT_StringHolder &
HUSD_Info::getTransformAttribName()
{
    static constexpr UT_StringLit theTransformAttribName("!transform");

    return theTransformAttribName.asHolder();
}

const UT_StringHolder &
HUSD_Info::getTimeVaryingAttribName()
{
    static constexpr UT_StringLit theTimeVaryingAttribName("!timevarying");

    return theTimeVaryingAttribName.asHolder();
}

void
HUSD_Info::getAttributeNames(const UT_StringRef &primpath,
	UT_ArrayStringSet &attrib_names) const
{
    auto prim = husdGetPrimAtPath(myAnyLock, primpath);
    if (!prim)
	return;

    auto attributes = prim.GetAttributes();
    for(auto &&attrib : attributes)
	attrib_names.emplace(attrib.GetName().GetString());
}

void
HUSD_Info::extractAttributes(const UT_StringRef &primpath,
	const UT_ArrayStringSet &which_attribs,
	const HUSD_TimeCode &tc,
	UT_Options &values,
	HUSD_TimeSampling *time_sampling) const
{
    if (primpath.isstring())
    {
	if (myAnyLock.constData() &&
	    myAnyLock.constData()->isStageValid())
	{
	    SdfPath sdfpath(HUSDgetSdfPath(primpath));
	    auto prim = myAnyLock.constData()->stage()->GetPrimAtPath(sdfpath);

	    if (prim)
	    {
		auto		    time = HUSDgetUsdTimeCode(tc);
		HUSD_TimeSampling   sampling = HUSD_TimeSampling::NONE;

		if ((which_attribs.empty() ||
		     which_attribs.contains(getTransformAttribName())) &&
		    prim.IsA<UsdGeomXformable>())
		{
		    UsdGeomXformable tprim(prim);
		    GfMatrix4d usdtransform =
			tprim.ComputeLocalToWorldTransform(time);
		    UT_Matrix4D mat = GusdUT_Gf::Cast(usdtransform);
		    values.setOptionM4(getTransformAttribName(), mat);
		    HUSDupdateWorldTransformTimeSampling(sampling, prim);
		}

		auto va = prim.GetAttributes();
		for(const auto &a : va)
		{
		    UT_StringHolder name(a.GetName().GetText());
		    if (!which_attribs.empty() && !which_attribs.contains(name))
			continue;

		    VtValue v;
		    a.Get(&v, time);
		    HUSDupdateValueTimeSampling(sampling, a);

		    if(v.IsHolding<int32>())
			values.setOptionI(name, v.UncheckedGet<int32>());
		    else if(v.IsHolding<int64>())
			values.setOptionI(name, v.UncheckedGet<int64>());
		    else if(v.IsHolding<TfToken>())
			values.setOptionS(name,
			    v.UncheckedGet<TfToken>().GetText());
		    else if(v.IsHolding<fpreal32>())
			values.setOptionF(name, v.UncheckedGet<fpreal32>());
		    else if(v.IsHolding<fpreal64>())
			values.setOptionF(name, v.UncheckedGet<fpreal64>());
		    else if(v.IsHolding<GfVec2f>())
			values.setOptionV2(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfVec2f>()));
		    else if(v.IsHolding<GfVec2d>())
			values.setOptionV2(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfVec2d>()));
		    else if(v.IsHolding<GfVec3f>())
			values.setOptionV3(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfVec3f>()));
		    else if(v.IsHolding<GfVec3d>())
			values.setOptionV3(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfVec3d>()));
		    else if(v.IsHolding<GfVec4f>())
			values.setOptionV4(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfVec4f>()));
		    else if(v.IsHolding<GfVec4d>())
			values.setOptionV4(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfVec4d>()));
		    else if(v.IsHolding<GfMatrix2f>())
			values.setOptionM2(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfMatrix2f>()));
		    else if(v.IsHolding<GfMatrix2d>())
			values.setOptionM2(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfMatrix2d>()));
		    else if(v.IsHolding<GfMatrix3f>())
			values.setOptionM3(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfMatrix3f>()));
		    else if(v.IsHolding<GfMatrix3d>())
			values.setOptionM3(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfMatrix3d>()));
		    else if(v.IsHolding<GfMatrix4f>())
			values.setOptionM4(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfMatrix4f>()));
		    else if(v.IsHolding<GfMatrix4d>())
			values.setOptionM4(name,
			    GusdUT_Gf::Cast(v.UncheckedGet<GfMatrix4d>()));
		    else if(v.IsHolding<GfVec2i>())
                    {
                        UT_Int64Array a(2,2);
                        auto vec = v.UncheckedGet<GfVec2i>();
                        a(0) = vec[0];
                        a(1) = vec[1];
			values.setOptionIArray(name,a);
                    }
		    else if(v.IsHolding<GfVec3i>())
                    {
                        UT_Int64Array a(3,3);
                        auto vec = v.UncheckedGet<GfVec3i>();
                        a(0) = vec[0];
                        a(1) = vec[1];
                        a(2) = vec[2];
			values.setOptionIArray(name,a);
                    }
		    else if(v.IsHolding<GfVec4i>())
                    {
                        UT_Int64Array a(4,4);
                        auto vec = v.UncheckedGet<GfVec4i>();
                        a(0) = vec[0];
                        a(1) = vec[1];
                        a(2) = vec[2];
                        a(3) = vec[3];
			values.setOptionIArray(name,a);
                    }
		    else if(v.IsHolding<VtArray<TfToken> >())
		    {
			VtArray<TfToken> strings =
			    v.UncheckedGet<VtArray<TfToken> >();
			UT_StringArray our_strings;
			for(const auto &s : strings)
			    our_strings.append(s.GetText());
			values.setOptionSArray(name, our_strings);
		    }	
		    else
		    {
			// NOTE: unsure how to handle VtArray<GfVec#>.
			// UT_Options does not support vector arrays (though
			// scalar fpreal arrays are supported)
			
			//UTdebugPrint("No support for prim type",
			//	       v.GetType().GetTypeName());
		    }
		}

		// Set a special option indicating that some of the extracted
		// data is time varying.
		values.setOptionB(getTimeVaryingAttribName(), 
			HUSDisTimeVarying( sampling ));
		if( time_sampling != nullptr )
		    *time_sampling = sampling;
	    }
	}
    }
}

static inline UsdGeomPrimvar
husdGetPrimvar( HUSD_AutoAnyLock &lock, const UT_StringRef &primpath,
	const UT_StringRef &primvarname)
{
    UsdGeomPrimvarsAPI api(husdGetPrimAtPath(lock, primpath));
    if (!api)
	return UsdGeomPrimvar(UsdAttribute());

    return api.GetPrimvar(TfToken(primvarname.toStdString()));
}

bool
HUSD_Info::isPrimvarAtPath(const UT_StringRef &primpath,
	const UT_StringRef &primvarname, QueryAspect query ) const
{
    auto primvar(husdGetPrimvar(myAnyLock, primpath, primvarname));
    if (!primvar)
	return false;

    if (query == QueryAspect::ARRAY)
	return primvar.GetTypeName().IsArray();

    return true; // QueryAspect::ANY
}

void
HUSD_Info::getPrimvarNames(const UT_StringRef &primpath,
	UT_ArrayStringSet &primvar_names) const
{
    UsdGeomPrimvarsAPI api(husdGetPrimAtPath(myAnyLock, primpath));
    if (!api)
	return;

    auto primvars = api.GetPrimvars();
    for( auto &&primvar : primvars )
	primvar_names.emplace(primvar.GetName().GetString());
}

exint
HUSD_Info::getPrimvarLength(const UT_StringRef &primpath,
	const UT_StringRef &primvarname, const HUSD_TimeCode &time_code,
	HUSD_TimeSampling *time_sampling ) const
{
    return getAttribLength(primpath, HUSDgetPrimvarAttribName(primvarname), 
	    time_code, time_sampling);
}

exint
HUSD_Info::getPrimvarSize(const UT_StringRef &primpath,
	const UT_StringRef &primvarname) const
{
    return getAttribSize(primpath, HUSDgetPrimvarAttribName(primvarname));
}

UT_StringHolder	
HUSD_Info::getPrimvarTypeName(const UT_StringRef &primpath,
	const UT_StringRef &primvarname) const
{
    auto primvar(husdGetPrimvar(myAnyLock, primpath, primvarname));
    if (!primvar)
	return UT_StringHolder();

    return UT_StringHolder(primvar.GetTypeName().GetAsToken().GetString());
}

bool
HUSD_Info::getPrimvarTimeSamples(const UT_StringRef &primpath,
	const UT_StringRef &primvarname, UT_FprealArray &time_samples) const
{
    auto primvar(husdGetPrimvar(myAnyLock, primpath, primvarname));
    if (!primvar)
	return false;

    std::vector< double > times;
    if( !primvar.GetTimeSamples( &times ))
	return false;

    time_samples.setSize( times.size() );
    for( exint i = 0; i < times.size(); i++ )
	time_samples[i] = times[i];

    return true;
}

void
HUSD_Info::getRelationshipNames(const UT_StringRef &primpath,
	UT_ArrayStringSet &rel_names) const
{
    if (primpath.isstring())
    {
	if (myAnyLock.constData() &&
	    myAnyLock.constData()->isStageValid())
	{
	    SdfPath sdfpath(HUSDgetSdfPath(primpath));
	    auto prim = myAnyLock.constData()->stage()->GetPrimAtPath(sdfpath);

	    if (prim)
	    {
		auto relationships = prim.GetRelationships();

		for(const auto &attrib : relationships)
		    rel_names.emplace(attrib.GetName().GetString());
	    }
	}
    }
}

static inline UsdRelationship
husdGetRelationshipAtPath( HUSD_AutoAnyLock &lock, const UT_StringRef &relpath) 
{
    return husdGetObjAtPath<UsdRelationship>(lock, relpath);
}

bool
HUSD_Info::isRelationshipAtPath( const UT_StringRef &relpath) const
{
    return (bool) husdGetRelationshipAtPath(myAnyLock, relpath);
}

bool
HUSD_Info::isRelationshipAtPath(const UT_StringRef &primpath,
	const UT_StringRef &relname) const
{
    return isRelationshipAtPath(husdPropertyPath(primpath, relname));
}

bool
HUSD_Info::getRelationshipTargets ( const UT_StringRef &relpath,
	UT_StringArray &target_paths) const
{
    auto rel = husdGetRelationshipAtPath(myAnyLock, relpath);
    if (!rel)
	return false;

    SdfPathVector sdfpaths;
    if (!rel.GetTargets(&sdfpaths))
	return false;

    return husdSetPrimpaths( target_paths, sdfpaths );
}

bool
HUSD_Info::getRelationshipTargets ( const UT_StringRef &primpath,
	const UT_StringRef &relname, UT_StringArray &target_paths) const
{
    return getRelationshipTargets(husdPropertyPath(primpath, relname),
	    target_paths);
}

bool
HUSD_Info::getRelationshipForwardedTargets ( const UT_StringRef &relpath,
	UT_StringArray &target_paths) const
{
    auto rel = husdGetRelationshipAtPath(myAnyLock, relpath);
    if (!rel)
	return false;

    SdfPathVector sdfpaths;
    if (!rel.GetForwardedTargets(&sdfpaths))
	return false;

    return husdSetPrimpaths( target_paths, sdfpaths );
}

bool
HUSD_Info::getRelationshipForwardedTargets ( const UT_StringRef &primpath,
	const UT_StringRef &relname, UT_StringArray &target_paths) const
{
    return getRelationshipForwardedTargets(husdPropertyPath(primpath, relname),
	    target_paths);
}

void
HUSD_Info::getMetadataNames(const UT_StringRef &object_path,
	UT_ArrayStringSet &metadata_names) const
{
    auto obj = husdGetObjAtPath<UsdObject>(myAnyLock, object_path);
    if (!obj) 
	return;

    UsdMetadataValueMap map = obj.GetAllMetadata();
    for(auto &&it : map)
	metadata_names.emplace(it.first.GetString());
}

bool
HUSD_Info::isMetadataAtPath(const UT_StringRef &object_path,
	const UT_StringRef &metadata_name, QueryAspect query ) const
{
    auto obj = husdGetObjAtPath<UsdObject>(myAnyLock, object_path);
    if (!obj) 
	return false;

    TfToken name(metadata_name.toStdString());
    if (query == QueryAspect::ARRAY)
	return HUSDisArrayMetadata(obj, name);

    return HUSDhasMetadata(obj, name);
}

exint
HUSD_Info::getMetadataLength(const UT_StringRef &object_path,
	const UT_StringRef &metadata_name) const
{
    exint length = 0;

    TfToken name(metadata_name.toStdString());
    auto    obj = husdGetObjAtPath<UsdObject>(myAnyLock, object_path);
    if (obj) 
	length = HUSDgetMetadataLength(obj, name);

    return length;
}

static inline SdfPrimSpecHandle
husdGetActiveLayerPrimAtPath(HUSD_AutoAnyLock &lock,
	const UT_StringRef &primpath) 
{
    SdfPrimSpecHandle primspec;

    if (primpath.isstring() &&
	lock.constData() &&
	lock.constData()->isStageValid())
    {
	auto	 layer = lock.constData()->activeLayer();

	if (layer)
	{
	    SdfPath sdfpath(HUSDgetSdfPath(primpath));
	    primspec = layer->GetPrimAtPath(sdfpath);
	}
    }

    return primspec;
}

bool
HUSD_Info::isActiveLayerPrimAtPath(const UT_StringRef &primpath,
	const UT_StringRef &prim_type) const
{
    bool isprim = false;

    auto prim = husdGetActiveLayerPrimAtPath(myAnyLock, primpath);
    if (prim &&
	(!prim_type.isstring() || prim_type == prim->GetTypeName().GetString()))
    {
	isprim = true;
    }

    return isprim;
}

bool
HUSD_Info::getActiveLayerSubLayers(UT_StringArray &names,
	UT_StringArray &identifiers,
	UT_IntArray &anonymous,
        UT_IntArray &fromsops) const
{
    bool		 success = false;

    if (myAnyLock.constData() &&
	myAnyLock.constData()->isStageValid())
    {
	auto	 layer = myAnyLock.constData()->activeLayer();

	if (layer)
	{
	    ArResolverContextBinder binder(
		myAnyLock.constData()->stage()->GetPathResolverContext());

	    // Return layers in strongest to weakest order (the natural order
	    // of the sublayer paths vector).
	    for (auto path : layer->GetSubLayerPaths())
	    {
		SdfLayerRefPtr	 sublayer = SdfLayer::Find(path);

		if (!sublayer)
		    continue;

		names.append(husdGetLayerLabel(sublayer));
		identifiers.append((std::string)path);
		anonymous.append(sublayer->IsAnonymous());
		fromsops.append(HUSDisSopLayer(sublayer));
	    }
	}

	success = true;
    }

    return success;
}

