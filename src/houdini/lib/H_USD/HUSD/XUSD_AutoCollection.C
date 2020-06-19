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
#include "HUSD_Utils.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_Utils.h"
#include <FS/UT_DSO.h>
#include <pxr/usd/usd/modelAPI.h>
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
XUSD_SimpleAutoCollection::matchPrimitives(const UsdStageRefPtr &stage,
        XUSD_PathSet &matches) const
{
    UsdPrim root = stage->GetPseudoRoot();
    auto predicate = HUSDgetUsdPrimPredicate(HUSD_TRAVERSAL_DEFAULT_DEMANDS);

    if (root)
    {
        XUSD_FindPrimPathsTaskData data;
        auto &task = *new(UT_Task::allocate_root())
            XUSD_FindPrimsTask(root, data, predicate,
                nullptr, this);
        UT_Task::spawnRootAndWait(task);

        data.gatherPathsFromThreads(matches);
    }
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
                         { }
                        ~XUSD_KindAutoCollection() override
                         { }

    bool                 matchPrimitive(const UsdPrim &prim,
                                bool *prune_branch) const override
    {
        UsdModelAPI model(prim);

        if (model)
        {
            TfToken kind;

            if (model.GetKind(&kind))
            {
                // Don't prune any part of the kind hierarchy.
                return KindRegistry::IsA(kind, myRequestedKind);
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
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AutoCollection registration
////////////////////////////////////////////////////////////////////////////

void
XUSD_AutoCollection::registerPlugins()
{
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_KindAutoCollection>("kind:"));
    if (!thePluginsInitialized)
    {
        UT_DSO dso;

        dso.run("newAutoCollection");
        thePluginsInitialized = true;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

