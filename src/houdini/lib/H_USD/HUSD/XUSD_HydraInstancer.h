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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraInstancer.h (HUSD Library, C++)
 *
 * COMMENTS:	Basic instancer for creating instance transforms.
 *
 */

#ifndef XUSD_HydraInstancer_h
#define XUSD_HydraInstancer_h

#include "HUSD_API.h"
#include "HUSD_Path.h"
#include <UT/UT_Lock.h>
#include <UT/UT_Map.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_UniquePtr.h>
#include <GT/GT_Transform.h>
#include <GT/GT_TransformArray.h>

#include <pxr/pxr.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/base/tf/hashmap.h>
#include <pxr/base/tf/token.h>

class HUSD_Scene;

PXR_NAMESPACE_OPEN_SCOPE

class HUSD_API XUSD_HydraInstancer final : public HdInstancer
{
public:
    XUSD_HydraInstancer(HdSceneDelegate* del,
			SdfPath const& id);
    ~XUSD_HydraInstancer() override;

    void        Sync(HdSceneDelegate* delegate,
                        HdRenderParam* renderParam,
                        HdDirtyBits* dirtyBits) override;

    // Grab the transforms for this instancer, and flatten it with any parent
    // instancers if 'recurse' is true. syncPrimvars() must be called first.
    VtMatrix4dArray	computeTransforms(
                                const SdfPath    &protoId,
                                bool              recurse,
                                int               hou_proto_id);

    // Grab the transforms and scene ids for each instance. If 'recurse' is
    // true, flatten both the transforms and ids for nested instancers.
    // syncPrimvars() must be called first.
    VtMatrix4dArray	computeTransformsAndIDs(
                                const SdfPath    &protoId,
                                bool              recurse,
                                int               level,
                                UT_IntArray      &ids,
                                HUSD_Scene       *scene,
                                int               hou_proto_id,
                                bool              dirty_indices);

    int                 id() const { return myID; }

    const VtValue      &primvarValue(const TfToken &name) const;

private:
    // Checks the change tracker to determine whether instance primvars are
    // dirty, and if so pulls them. Since primvars can only be pulled once,
    // and are cached, this function is not re-entrant. However, this function
    // is called by ComputeInstanceTransforms, which is called (potentially)
    // by HdMantraMesh::Sync(), which is dispatched in parallel, so it needs
    // to be guarded by _instanceLock.
    //
    // Pulled primvars are cached in _primvarMap.
    void syncPrimvars(HdSceneDelegate* delegate,
                        HdRenderParam* renderParam,
                        HdDirtyBits* dirtyBits);

    // Map of the latest primvar data for this instancer, keyed by
    // primvar name. Primvar values are VtValue, an any-type; they are
    // interpreted at consumption time (here, in ComputeInstanceTransforms).
    UT_Map<TfToken, VtValue, TfToken::HashFunctor>	myPrimvarMap;

    mutable UT_Lock myLock;

    UT_StringHolder findParentInstancer() const;

    VtMatrix4dArray privComputeTransforms(const SdfPath &prototypeId,
                            bool recurse,
                            int level,
                            UT_IntArray *ids,
                            HUSD_Scene *scene,
                            int hou_proto_id,
                            bool dirty_indices,
                            XUSD_HydraInstancer *child_instancer);

    int                                  myID;
};

class XUSD_HydraTransforms : public GT_TransformArray
{
public:
	     XUSD_HydraTransforms() : myDataId(-1) {}
            ~XUSD_HydraTransforms() override {}

    void     setDataId(int64 id)           { myDataId = id; }
    int64    getDataId() const override    { return myDataId; }

private:
    int64 myDataId;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
