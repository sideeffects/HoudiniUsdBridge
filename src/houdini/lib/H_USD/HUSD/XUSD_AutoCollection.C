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

#include "XUSD_AutoCollection.h"
#include "HUSD_DataHandle.h"
#include "XUSD_Data.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_Utils.h"
#include <FS/UT_DSO.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/pcp/node.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/kind/registry.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {
    static bool      thePluginsInitialized = false;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AutoCollection
////////////////////////////////////////////////////////////////////////////

UT_Array<XUSD_AutoCollectionFactory *> XUSD_AutoCollection::theFactories;

XUSD_AutoCollection::XUSD_AutoCollection(const char *token)
    : myToken(token)
{
}

XUSD_AutoCollection::~XUSD_AutoCollection()
{
}

bool
XUSD_AutoCollection::canCreateAutoCollection(const char *token)
{
    for(auto &&factory : theFactories)
        if (factory->canCreateAutoCollection(token))
            return true;

    return false;
}

XUSD_AutoCollection *
XUSD_AutoCollection::create(const char *token)
{
    for(auto &&factory : theFactories)
    {
        XUSD_AutoCollection *ac = factory->create(token);
        if (ac)
            return ac;
    }

    return nullptr;
}

void
XUSD_AutoCollection::registerPlugin(XUSD_AutoCollectionFactory *factory)
{
    theFactories.append(factory);
}

////////////////////////////////////////////////////////////////////////////
// XUSD_SimpleAutoCollection
////////////////////////////////////////////////////////////////////////////

XUSD_SimpleAutoCollection::XUSD_SimpleAutoCollection(const char *token)
    : XUSD_AutoCollection(token)
{
}

XUSD_SimpleAutoCollection::~XUSD_SimpleAutoCollection()
{
}

void
XUSD_SimpleAutoCollection::matchPrimitives(HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode,
        XUSD_PathSet &matches,
        UT_StringHolder &error) const
{
    UsdStageRefPtr stage = lock.constData()->stage();
    UsdPrim root = stage->GetPseudoRoot();
    auto predicate = HUSDgetUsdPrimPredicate(demands);

    if (root)
    {
        XUSD_FindPrimPathsTaskData data;
        auto &task = *new(UT_Task::allocate_root())
            XUSD_FindPrimsTask(root, data, predicate,
                nullptr, this);
        UT_Task::spawnRootAndWait(task);

        data.gatherPathsFromThreads(matches);
    }
    error = myTokenParsingError;
}

////////////////////////////////////////////////////////////////////////////
// XUSD_KindAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_KindAutoCollection : public XUSD_SimpleAutoCollection
{
public:
    XUSD_KindAutoCollection(const char *token)
        : XUSD_SimpleAutoCollection(token),
          myRequestedKind(token)
    {
        myRequestedKindIsValid = KindRegistry::
            HasKind(myRequestedKind);
        myRequestedKindIsModel = KindRegistry::
            IsA(myRequestedKind, KindTokens->model);
        if (!myRequestedKindIsValid)
            setTokenParsingError("The specified kind does not exist.");
    }
    ~XUSD_KindAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        if (myRequestedKindIsValid)
        {
            UsdModelAPI model(prim);

            if (model && (!myRequestedKindIsModel || model.IsModel()))
            {
                TfToken kind;

                if (model.GetKind(&kind))
                {
                    // Don't prune any part of the kind hierarchy.
                    return KindRegistry::IsA(kind, myRequestedKind);
                }
            }
        }

        // If we hit any non-model prim, or any prim without a kind, we can
        // stop traversing. A valid model hierarchy must start at the root
        // prim, and be contiguous in the scene graph hierarchy.
        *prune_branch = true;

        return false;
    }

private:
    TfToken              myRequestedKind;
    bool                 myRequestedKindIsModel;
    bool                 myRequestedKindIsValid;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_PrimTypeAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_PrimTypeAutoCollection : public XUSD_SimpleAutoCollection
{
public:
    XUSD_PrimTypeAutoCollection(const char *token)
        : XUSD_SimpleAutoCollection(token)
    {
        UT_String tokenstr(token);
        UT_StringArray primtypes;
        UT_StringArray invalidtypes;
        tokenstr.tokenize(primtypes, ",");
        for (auto &&primtype : primtypes)
        {
            const TfType &tfprimtype = HUSDfindType(primtype);

            if (!tfprimtype.IsUnknown())
                myPrimTypes.append(&tfprimtype);
            else
                invalidtypes.append(primtype);
        }
        if (!invalidtypes.isEmpty())
        {
            UT_WorkBuffer msgbuf;
            msgbuf.append("The specified primitive type(s) do not exist: ");
            msgbuf.append(invalidtypes, ", ");
            setTokenParsingError(msgbuf.buffer());
        }
    }
    ~XUSD_PrimTypeAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        for (auto &&primtype : myPrimTypes)
            if (prim.IsA(*primtype))
                return true;

        return false;
    }

private:
    UT_Array<const TfType *>     myPrimTypes;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_PurposeAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_PurposeAutoCollection : public XUSD_SimpleAutoCollection
{
public:
    XUSD_PurposeAutoCollection(const char *token)
        : XUSD_SimpleAutoCollection(token)
    {
        const auto &allpurposes = UsdGeomImageable::GetOrderedPurposeTokens();
        UT_String tokenstr(token);
        UT_StringArray purposes;
        UT_StringArray invalidpurposes;
        tokenstr.tokenize(purposes, ",");
        for (auto &&purpose : purposes)
        {
            TfToken tfpurpose(purpose.toStdString());

            if (std::find(allpurposes.begin(), allpurposes.end(), tfpurpose) !=
                allpurposes.end())
                myPurposes.push_back(tfpurpose);
            else
                invalidpurposes.append(purpose);
        }
        if (!invalidpurposes.isEmpty())
        {
            UT_WorkBuffer msgbuf;
            msgbuf.append("The specified purpose(s) do not exist: ");
            msgbuf.append(invalidpurposes, ", ");
            setTokenParsingError(msgbuf.buffer());
        }
    }
    ~XUSD_PurposeAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        const auto &info = computePurposeInfo(myPurposeInfoCache.get(), prim);
        auto it = std::find(myPurposes.begin(), myPurposes.end(), info.purpose);

        return (it != myPurposes.end());
    }

private:
    typedef std::map<SdfPath, UsdGeomImageable::PurposeInfo> PurposeInfoMap;

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

    TfTokenVector                                    myPurposes;
    mutable UT_ThreadSpecificValue<PurposeInfoMap>   myPurposeInfoCache;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_ReferenceAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_ReferenceAutoCollection : public XUSD_SimpleAutoCollection
{
public:
    XUSD_ReferenceAutoCollection(const char *token)
        : XUSD_SimpleAutoCollection(token)
    {
        myRefPath = HUSDgetSdfPath(token);
        // We are only interested in direct composition authored on this prim,
        // that may be references, inherits, or specializes. We don't care
        // about variants or payloads (though payloads come along with
        // references).
        myQueryFilter.arcTypeFilter =
            UsdPrimCompositionQuery::ArcTypeFilter::NotVariant;
        myQueryFilter.dependencyTypeFilter =
            UsdPrimCompositionQuery::DependencyTypeFilter::Direct;
    }
    ~XUSD_ReferenceAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        // Quick check this this prim has at least some inherit, specialize,
        // or reference metadata authored on it.
        if (prim.HasAuthoredReferences() ||
            prim.HasAuthoredInherits() ||
            prim.HasAuthoredSpecializes())
        {
            // Use a UsdPrimCompositionQuery to find all composition arcs.
            UsdPrimCompositionQuery query(prim, myQueryFilter);
            auto arcs = query.GetCompositionArcs();
            size_t narcs = arcs.size();

            // A reference, inherit, or specialize arc to this stage will
            // always show up as the second or later arc, pointing to the
            // same layer stack as the "root" arc (which ties the prim to
            // this stage).
            if (narcs > 1 && arcs[0].GetArcType() == PcpArcTypeRoot)
            {
                const PcpLayerStackRefPtr &root_layer_stack =
                    arcs[0].GetTargetNode().GetLayerStack();

                if (root_layer_stack)
                {
                    // Check subsequent arcs for reference, inherit, or
                    // specialize arcs that point to the requested node in
                    // the root layer stack.
                    for (int i = 1; i < narcs; i++)
                    {
                        PcpNodeRef target = arcs[i].GetTargetNode();
                        PcpArcType arctype = target.GetArcType();

                        if (arctype == PcpArcTypeInherit ||
                            arctype == PcpArcTypeReference ||
                            arctype == PcpArcTypeSpecialize)
                        {
                            if (target.GetPath() == myRefPath &&
                                target.GetLayerStack() == root_layer_stack)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
        }

        return false;
    }

private:
    SdfPath                          myRefPath;
    UsdPrimCompositionQuery::Filter  myQueryFilter;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_InstanceAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_InstanceAutoCollection : public XUSD_SimpleAutoCollection
{
public:
    XUSD_InstanceAutoCollection(const char *token)
        : XUSD_SimpleAutoCollection(token)
    {
        mySrcPath = HUSDgetSdfPath(token);
    }
    ~XUSD_InstanceAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        MasterInfo  &masterinfo = myMasterInfo.get();
        if (!masterinfo.myInitialized)
        {
            UsdPrim srcprim = prim.GetStage()->GetPrimAtPath(mySrcPath);
            UsdPrim master = srcprim ? srcprim.GetMaster() : UsdPrim();
            if (master)
                masterinfo.myPath = master.GetPath();
            masterinfo.myInitialized = true;
        }

        // Exit immediately and stop searching this branch if the source prim
        // doesn't have a master.
        if (masterinfo.myPath.IsEmpty())
        {
            *prune_branch = true;
            return false;
        }

        if (prim.GetMaster().GetPath() == masterinfo.myPath)
        {
            // A child of an instance prim ca't have that same prim as an
            // instance again.
            *prune_branch = true;
            return true;
        }

        return false;
    }

private:
    class MasterInfo
    {
    public:
        SdfPath      myPath;
        bool         myInitialized = false;
    };

    SdfPath                                      mySrcPath;
    mutable UT_ThreadSpecificValue<MasterInfo>   myMasterInfo;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AutoCollection registration
////////////////////////////////////////////////////////////////////////////

void
XUSD_AutoCollection::registerPlugins()
{
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_KindAutoCollection>("kind:"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PrimTypeAutoCollection>("type:"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PurposeAutoCollection>("purpose:"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_ReferenceAutoCollection>("reference:"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_InstanceAutoCollection>("instance:"));
    if (!thePluginsInitialized)
    {
        UT_DSO dso;

        dso.run("newAutoCollection");
        thePluginsInitialized = true;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

