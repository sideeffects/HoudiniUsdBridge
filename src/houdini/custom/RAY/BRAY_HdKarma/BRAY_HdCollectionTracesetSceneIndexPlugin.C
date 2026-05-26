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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#include "BRAY_HdCollectionTracesetSceneIndexPlugin.h"
#include <pxr/imaging/hd/collectionsSchema.h>
#include <pxr/imaging/hd/collectionExpressionEvaluator.h>
#include <pxr/imaging/hd/collectionSchema.h>
#include "pxr/imaging/hd/containerDataSourceEditor.h"
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/sceneIndexPrimView.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdsi/utils.h>

#include <UT/UT_Array.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_WorkBuffer.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((karmaCustomTracesets, "karma:object:custom_tracesets"))
    ((sceneIndexPluginName, "BRAY_HdCollectionTracesetSceneIndexPlugin"))
);

namespace
{

static constexpr UT_StringLit theTracesetPrefix("karma:traceset:");

TF_DECLARE_WEAK_AND_REF_PTRS(_SceneIndex);

class _SceneIndex : public HdSingleInputFilteringSceneIndexBase
{
public:
    static _SceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr &inputSceneIndex)
    {
        return TfCreateRefPtr(new _SceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

    using AliasName = std::pair<UT_StringHolder, UT_StringHolder>;
    // Returns list of traceset alias,name pair that the prim belongs to
    UT_Array<AliasName> findTraceset(const SdfPath &path) const
    {
        UT_Array<AliasName> result;
        for (auto &it_entry: myEntryToCollections)
        {
            const Collections &cols = it_entry.second;
            for (auto &&it_col : cols)
            {
                const Collection &col = it_col.second;
                if (col.myEval && col.myEval->Match(path))
                {
                    UT_WorkBuffer tmp;
                    tmp.append(it_entry.first);
                    tmp.append("/");
                    tmp.append(it_col.first);
                    result.append({it_col.first, tmp});
                }
            }
        }
        return result;
    }

protected:
    _SceneIndex(
        const HdSceneIndexBaseRefPtr &inputSceneIndex)
      : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
        SetDisplayName("Karma Collection-based Traceset Scene Index");
    }

    void _PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        HdSceneIndexObserver::DirtiedPrimEntries dirtyentries;
        updateCollections(entries, dirtyentries);

        _SendPrimsAdded(entries);
        if (!dirtyentries.empty())
            _SendPrimsDirtied(dirtyentries);
    }

    void _PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries) override
    {
        HdSceneIndexObserver::DirtiedPrimEntries dirtyentries;
        updateCollections(entries, dirtyentries);

        _SendPrimsRemoved(entries);
        if (!dirtyentries.empty())
            _SendPrimsDirtied(dirtyentries);
    }

    void _PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries) override
    {
        HdSceneIndexObserver::DirtiedPrimEntries dirtyentries;
        updateCollections(entries, dirtyentries);

        _SendPrimsDirtied(entries);
        if (!dirtyentries.empty())
            _SendPrimsDirtied(dirtyentries);
    }

    struct Collection
    {
        UT_StringHolder                                 myAlias;
        SdfPathExpression                               myExpr;
        std::optional<HdCollectionExpressionEvaluator>  myEval;
    };
    using Collections = UT_StringMap<Collection>;
    UT_StringMap<Collections> myEntryToCollections;

    // Update tracesets and returns list of dirty rprims affected by the change
    template <typename ENTRIES>
    void updateCollections(
        const ENTRIES &entries,
        HdSceneIndexObserver::DirtiedPrimEntries &dirtyentries)
    {
        UT_Array<Collection> dirtycollections;
        for (auto &&entry : entries)
        {
            HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(entry.primPath);

            if (!prim.primType.IsEmpty())
                continue;

            if constexpr (SYS_IsSame_v<ENTRIES,
                HdSceneIndexObserver::RemovedPrimEntries>)
            {
                // can't seem to get collections schema when it's being
                // removed. try to erase:
                auto it = myEntryToCollections.find(entry.primPath.GetString());
                if (it != myEntryToCollections.end())
                {
                    for (auto &&it_col : it->second)
                        dirtycollections.append(it_col.second);
                    myEntryToCollections.erase(it);
                }
                continue;
            }

            HdCollectionsSchema collectionsschema =
                HdCollectionsSchema::GetFromParent(prim.dataSource);

            if (!collectionsschema)
                continue;

            UT_StringMap<Collections>::const_iterator it_oldcols =
                myEntryToCollections.find(entry.primPath.GetString());

            // compile collections
            Collections newcols;
            TfTokenVector names = collectionsschema.GetCollectionNames();
            for (const TfToken &colname : names)
            {
                if (TfStringStartsWith(colname.GetString(),
                    theTracesetPrefix.c_str()))
                {
                    UT_StringHolder alias =
                        colname.GetString().substr(theTracesetPrefix.length());
                    UT_StringHolder uniquename = entry.primPath.GetString();
                    uniquename += alias;

                    Collection &col = newcols[alias];
                    SdfPathExpression prevexpr = col.myExpr;
                    HdSceneIndexBaseRefPtr inputSceneIndex = _GetInputSceneIndex();
                    HdsiUtilsCompileCollection(collectionsschema,
                                           colname,
                                           inputSceneIndex,
                                           &col.myExpr,
                                           &col.myEval);
                }
            }

            if (it_oldcols != myEntryToCollections.end())
            {
                // updating existing collections
                const Collections &oldcols = it_oldcols->second;
                for (auto &&it : oldcols)
                {
                    if (!newcols.contains(it.first))
                    {
                        // Handle collections that either no longer exist
                        // (under the same entry)
                        dirtycollections.append(it.second);
                    }
                    else if (newcols[it.first].myExpr != it.second.myExpr)
                    {
                        // Handle expression changed
                        dirtycollections.append(newcols[it.first]);
                        dirtycollections.append(it.second);
                    }
                }
            }
            else
            {
                // new collections
                for (auto &&it : newcols)
                    dirtycollections.append(it.second);
            }
            myEntryToCollections[entry.primPath.GetString()] = newcols;
        }

        // early out if no change
        if (dirtycollections.isEmpty())
            return;

        // dirty meshes that match dirty collection(s)
        // TODO: match against affected subtree?
        for (const SdfPath &path : HdSceneIndexPrimView(_GetInputSceneIndex()))
        {
            const HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(path);
            if (!HdPrimTypeIsGprim(prim.primType))
                continue;

            for (auto &&col : dirtycollections)
            {
                if (col.myEval && col.myEval->Match(path))
                {
                    HdDataSourceLocatorSet locators;
                    locators.insert(HdPrimvarsSchema::GetDefaultLocator());
                    dirtyentries.push_back({path,locators});
                    break;
                }
            }
        }
    }
};

class TracesetDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(TracesetDataSource);

    TfTokenVector GetNames() override
    {
        return myPrim.dataSource->GetNames();
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (!mySceneIndex || !myPrim.dataSource)
        {
            return nullptr;
        }

        if (name == HdPrimvarsSchema::GetSchemaToken())
        {
            UT_Array<_SceneIndex::AliasName> aliasnames =
                mySceneIndex->findTraceset(myPrimPath);

            // construct space-separated alias/traceset string
            if (!aliasnames.isEmpty())
            {
                // Py dictionary format (but make sure there are no spaces so
                // that it's considered a single entry)
                UT_WorkBuffer val;
                val.append("{");
                for (const _SceneIndex::AliasName &aliasname : aliasnames)
                {
                    val.appendFormat("\\'{}\\':\\'{}\\',",
                        aliasname.first, aliasname.second);
                }
                val.append("}");

                // append user-defined custom traceset value
                HdPrimvarsSchema primvars =
                    HdPrimvarsSchema::GetFromParent(myPrim.dataSource);
                if (HdSampledDataSourceHandle customds =
                    primvars.GetPrimvar(
                    _tokens->karmaCustomTracesets).GetPrimvarValue())
                {
                    VtValue v = customds->GetValue(0);
                    if (v.IsHolding<std::string>())
                    {
                        val.append(" ");
                        val.append(v.UncheckedGet<std::string>());
                    }
                }

                // set primvar
                HdContainerDataSourceHandle primvarsds =
                    primvars.GetContainer();

                HdContainerDataSourceEditor primvareditor(primvarsds);

                HdContainerDataSourceHandle tracesetds =
                    HdPrimvarSchema::Builder().SetPrimvarValue(
                            HdRetainedTypedSampledDataSource<std::string>::New(
                                val.toStdString()))
                        .SetInterpolation(HdPrimvarSchema::
                            BuildInterpolationDataSource(
                                HdPrimvarSchemaTokens->constant)).Build();

                primvareditor.Overlay(
                    HdDataSourceLocator(_tokens->karmaCustomTracesets),
                    tracesetds);

                return primvareditor.Finish();
            }
        }
        return myPrim.dataSource->Get(name);
    }

private:
    TracesetDataSource(
        _SceneIndexConstPtr const& sceneindex,
        SdfPath const& primPath,
        HdSceneIndexPrim const& prim)
    : mySceneIndex(sceneindex)
    , myPrimPath(primPath)
    , myPrim(prim)
    {
    }

    const _SceneIndexConstPtr mySceneIndex;
    const SdfPath myPrimPath;
    const HdSceneIndexPrim myPrim;
};

HdSceneIndexPrim
_SceneIndex::GetPrim(const SdfPath &primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (!myEntryToCollections.empty() && prim.dataSource)
    {
        // Overrides happen in the prim-level data source.
        if (HdPrimTypeIsGprim(prim.primType))
        {
            prim.dataSource = TracesetDataSource::New(
                TfCreateWeakPtr(this), primPath, prim);
        }
    }
    return prim;
}

}//ns

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, BRAY_HdCollectionTracesetSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<BRAY_HdCollectionTracesetSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, BRAY_HdCollectionTracesetSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 100;

    for (auto &&plugin : { "Karma XPU", "Karma CPU" })
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            plugin,
            _tokens->sceneIndexPluginName,
            nullptr,
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

BRAY_HdCollectionTracesetSceneIndexPlugin::
    BRAY_HdCollectionTracesetSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
BRAY_HdCollectionTracesetSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return _SceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
