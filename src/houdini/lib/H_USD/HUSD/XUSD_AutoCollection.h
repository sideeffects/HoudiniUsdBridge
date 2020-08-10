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

#ifndef __XUSD_AutoCollection_h__
#define __XUSD_AutoCollection_h__

#include "HUSD_API.h"
#include "HUSD_TimeCode.h"
#include "HUSD_Utils.h"
#include "XUSD_PathSet.h"
#include "XUSD_PerfMonAutoCookEvent.h"
#include <UT/UT_StringHolder.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

extern "C" {
    SYS_VISIBILITY_EXPORT extern void	newAutoCollection(void *unused);
};
class HUSD_AutoAnyLock;

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_AutoCollection;

class XUSD_AutoCollectionFactory
{
public:
                         XUSD_AutoCollectionFactory()
                         { }
    virtual		~XUSD_AutoCollectionFactory()
                         { }

    virtual bool         canCreateAutoCollection(const char *token) const = 0;
    virtual XUSD_AutoCollection *create(const char *token,
                                HUSD_AutoAnyLock &lock,
                                HUSD_PrimTraversalDemands demands,
                                int nodeid,
                                const HUSD_TimeCode &timecode) const = 0;
};

template <class AutoCollection>
class XUSD_SimpleAutoCollectionFactory : public XUSD_AutoCollectionFactory
{
public:
                         XUSD_SimpleAutoCollectionFactory(const char *prefix)
                             : myPrefix(prefix)
                         { }
                        ~XUSD_SimpleAutoCollectionFactory() override
                         { }

    bool                 canCreateAutoCollection(
                                const char *token) const override
    {
        return (strncmp(token, myPrefix.c_str(), myPrefix.length()) == 0);
    }
    XUSD_AutoCollection *create(const char *token,
                                HUSD_AutoAnyLock &lock,
                                HUSD_PrimTraversalDemands demands,
                                int nodeid,
                                const HUSD_TimeCode &timecode) const override
    {
        if (strncmp(token, myPrefix.c_str(), myPrefix.length()) == 0)
            return new AutoCollection(token + myPrefix.length(),
                lock, demands, nodeid, timecode);

        return nullptr;
    }

private:
    UT_StringHolder      myPrefix;
};

class HUSD_API XUSD_AutoCollection
{
public:
                         XUSD_AutoCollection(const char *token,
                                HUSD_AutoAnyLock &lock,
                                HUSD_PrimTraversalDemands demands,
                                int nodeid,
                                const HUSD_TimeCode &timecode);
    virtual		~XUSD_AutoCollection();

    // Determines whether or not this class works in random access mode.
    virtual bool         randomAccess() const = 0;

    // A non-random access auto collection does its own traversal of the stage
    // all at once when the auto collection is created, generating a full set
    // of all matching paths at once.
    virtual void         matchPrimitives(XUSD_PathSet &matches) const
                         { UT_ASSERT(false); }

    // A random access auto collection gets no benefit from being executed
    // as a depth first traversal of the whole stage. It will be called for
    // each primitive as part of the overall pattern matching process.
    virtual bool         matchRandomAccessPrimitive(const SdfPath &path,
                                bool *prune_branch) const
                         { UT_ASSERT(false); return false; }

    const UT_StringHolder &getTokenParsingError() const
                         { return myTokenParsingError; }

    static bool          canCreateAutoCollection(const char *token);
    static XUSD_AutoCollection *create(const char *token,
                                HUSD_AutoAnyLock &lock,
                                HUSD_PrimTraversalDemands demands,
                                int nodeid,
                                const HUSD_TimeCode &timecode);
    static void          registerPlugins();
    static void          registerPlugin(XUSD_AutoCollectionFactory *factory);

protected:
    UT_StringHolder              myToken;
    HUSD_AutoAnyLock            &myLock;
    HUSD_PrimTraversalDemands    myDemands;
    int                          myNodeId;
    UsdTimeCode                  myUsdTimeCode;
    HUSD_TimeCode                myHusdTimeCode;
    UT_StringHolder              myTokenParsingError;

private:
    static UT_Array<XUSD_AutoCollectionFactory *> theFactories;
};

class HUSD_API XUSD_SimpleAutoCollection : public XUSD_AutoCollection
{
public:
                         XUSD_SimpleAutoCollection(const char *token,
                                HUSD_AutoAnyLock &lock,
                                HUSD_PrimTraversalDemands demands,
                                int nodeid,
                                const HUSD_TimeCode &timecode);
                        ~XUSD_SimpleAutoCollection() override;

    bool                 randomAccess() const override
                         { return false; }
    void                 matchPrimitives(XUSD_PathSet &matches) const override;
    virtual bool         matchPrimitive(const UsdPrim &prim,
                                bool *prune_branch) const = 0;
};

class HUSD_API XUSD_RandomAccessAutoCollection : public XUSD_AutoCollection
{
public:
                         XUSD_RandomAccessAutoCollection(const char *token,
                                HUSD_AutoAnyLock &lock,
                                HUSD_PrimTraversalDemands demands,
                                int nodeid,
                                const HUSD_TimeCode &timecode);
                        ~XUSD_RandomAccessAutoCollection() override;

    bool                 randomAccess() const override
                         { return true; }
    bool                 matchRandomAccessPrimitive(const SdfPath &path,
                                bool *prune_branch) const override;
    virtual bool         matchPrimitive(const UsdPrim &prim,
                                bool *prune_branch) const = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

