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

    virtual bool         canCreateAutoCollection(const char *token) = 0;
    virtual XUSD_AutoCollection *create(const char *token) = 0;
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

    bool                 canCreateAutoCollection(const char *token) override
    {
        return (strncmp(token, myPrefix.c_str(), myPrefix.length()) == 0);
    }
    XUSD_AutoCollection *create(const char *token) override
    {
        if (strncmp(token, myPrefix.c_str(), myPrefix.length()) == 0)
            return new AutoCollection(token + myPrefix.length());

        return nullptr;
    }

private:
    UT_StringHolder      myPrefix;
};

class HUSD_API XUSD_AutoCollection
{
public:
                         XUSD_AutoCollection(const char *token);
    virtual		~XUSD_AutoCollection();

    virtual void         matchPrimitives(HUSD_AutoAnyLock &lock,
                                HUSD_PrimTraversalDemands demands,
                                int nodeid,
                                const HUSD_TimeCode &timecode,
                                XUSD_PathSet &matches,
                                UT_StringHolder &error) const = 0;

    static bool          canCreateAutoCollection(const char *token);
    static XUSD_AutoCollection *create(const char *token);
    static void          registerPlugins();
    static void          registerPlugin(XUSD_AutoCollectionFactory *factory);

protected:
    UT_StringHolder      myToken;

private:
    static UT_Array<XUSD_AutoCollectionFactory *> theFactories;
};

class HUSD_API XUSD_SimpleAutoCollection : public XUSD_AutoCollection
{
public:
                         XUSD_SimpleAutoCollection(const char *token);
    virtual		~XUSD_SimpleAutoCollection();

    void                 matchPrimitives(HUSD_AutoAnyLock &lock,
                                HUSD_PrimTraversalDemands demands,
                                int nodeid,
                                const HUSD_TimeCode &timecode,
                                XUSD_PathSet &matches,
                                UT_StringHolder &error) const override;

    virtual bool         matchPrimitive(const UsdPrim &prim,
                                bool *prune_branch) const = 0;

protected:
    void                 setTokenParsingError(const UT_StringHolder &error)
                         { myTokenParsingError = error; }

private:
    UT_StringHolder      myTokenParsingError;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif

