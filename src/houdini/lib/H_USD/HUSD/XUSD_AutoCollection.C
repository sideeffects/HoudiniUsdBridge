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
#include "HUSD_PathSet.h"
#include "HUSD_Path.h"
#include "XUSD_Data.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_Utils.h"
#include <gusd/UT_Gf.h>
#include <FS/UT_DSO.h>
#include <BV/BV_Overlap.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/pcp/node.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/frustum.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {
    static bool      thePluginsInitialized = false;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AutoCollection
////////////////////////////////////////////////////////////////////////////

UT_Array<XUSD_AutoCollectionFactory *> XUSD_AutoCollection::theFactories;

XUSD_AutoCollection::XUSD_AutoCollection(const char *token,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
    : myToken(token),
      myLock(lock),
      myDemands(demands),
      myNodeId(nodeid),
      myHusdTimeCode(timecode),
      myUsdTimeCode(HUSDgetNonDefaultUsdTimeCode(timecode))
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
XUSD_AutoCollection::create(const char *token,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
{
    for(auto &&factory : theFactories)
    {
        XUSD_AutoCollection *ac = factory->create(token,
            lock, demands, nodeid, timecode);
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

XUSD_SimpleAutoCollection::XUSD_SimpleAutoCollection(
        const char *token,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
    : XUSD_AutoCollection(token, lock, demands, nodeid, timecode)
{
}

XUSD_SimpleAutoCollection::~XUSD_SimpleAutoCollection()
{
}

void
XUSD_SimpleAutoCollection::matchPrimitives(XUSD_PathSet &matches) const
{
    UsdStageRefPtr stage = myLock.constData()->stage();
    UsdPrim root = stage->GetPseudoRoot();
    auto predicate = HUSDgetUsdPrimPredicate(myDemands);

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
// XUSD_RandomAccessAutoCollection
////////////////////////////////////////////////////////////////////////////

XUSD_RandomAccessAutoCollection::XUSD_RandomAccessAutoCollection(
        const char *token,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
    : XUSD_AutoCollection(token, lock, demands, nodeid, timecode)
{
}

XUSD_RandomAccessAutoCollection::~XUSD_RandomAccessAutoCollection()
{
}

bool
XUSD_RandomAccessAutoCollection::matchRandomAccessPrimitive(
        const SdfPath &path,
        bool *prune_branch) const
{
    UsdStageRefPtr stage = myLock.constData()->stage();
    UsdPrim prim = stage->GetPrimAtPath(path);

    if (prim)
        return matchPrimitive(prim, prune_branch);

    // We should never be passed an invalid/non-existent prim path.
    UT_ASSERT(false);
    *prune_branch = true;
    return false;
}

////////////////////////////////////////////////////////////////////////////
// XUSD_KindAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_KindAutoCollection : public XUSD_SimpleAutoCollection
{
public:
    XUSD_KindAutoCollection(
            const char *token,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_SimpleAutoCollection(token, lock, demands, nodeid, timecode),
          myRequestedKind(token)
    {
        myRequestedKindIsValid = KindRegistry::
            HasKind(myRequestedKind);
        myRequestedKindIsModel = KindRegistry::
            IsA(myRequestedKind, KindTokens->model);
        if (!myRequestedKindIsValid)
            myTokenParsingError = "The specified kind does not exist.";
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

class XUSD_PrimTypeAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_PrimTypeAutoCollection(
            const char *token,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(token, lock, demands, nodeid, timecode)
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
            myTokenParsingError = msgbuf.buffer();
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
    XUSD_PurposeAutoCollection(
            const char *token,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_SimpleAutoCollection(token, lock, demands, nodeid, timecode)
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
            myTokenParsingError = msgbuf.buffer();
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

class XUSD_ReferenceAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_ReferenceAutoCollection(
            const char *token,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(token, lock, demands, nodeid, timecode)
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

class XUSD_InstanceAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_InstanceAutoCollection(
            const char *token,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(token, lock, demands, nodeid, timecode)
    {
        mySrcPath = HUSDgetSdfPath(token);
        initialize(lock);
    }

    ~XUSD_InstanceAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        // Exit immediately and stop searching this branch if the source prim
        // doesn't have a master.
        if (myMasterPath.IsEmpty())
        {
            *prune_branch = true;
            return false;
        }

        if (prim.GetMaster().GetPath() == myMasterPath)
        {
            // A child of an instance prim can't have that same prim as an
            // instance again.
            *prune_branch = true;
            return true;
        }

        return false;
    }

private:
    void initialize(HUSD_AutoAnyLock &lock)
    {
        if (lock.constData() && lock.constData()->isStageValid())
        {
            UsdStageRefPtr  stage = lock.constData()->stage();
            UsdPrim         prim = stage->GetPrimAtPath(mySrcPath);
            UsdPrim         master = prim ? prim.GetMaster() : UsdPrim();

            if (master)
                myMasterPath = master.GetPath();
        }
    }

    SdfPath                                      mySrcPath;
    SdfPath                                      myMasterPath;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_BoundAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_BoundAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_BoundAutoCollection(
            const char *token,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(token, lock, demands, nodeid, timecode)
    {
        myPath = HUSDgetSdfPath(token);
        initialize(lock);
    }
    ~XUSD_BoundAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        BBoxCachePtr &bboxcache = myBBoxCache.get();

        if (!bboxcache)
            bboxcache.reset(new UsdGeomBBoxCache(myUsdTimeCode,
                UsdGeomImageable::GetOrderedPurposeTokens(), true, true));

        GfBBox3d primbox = bboxcache->ComputeWorldBound(prim);
        if (myBoundsType == FRUSTUM)
        {
            // Grab the shared initialized frustum if we haven't been
            // initialized on this thread yet. Each thread needs its own
            // furstum object because the Intersects call isn't thread safe.
            if (!myFrustumInitialized.get())
            {
                myFrustum.get() = myFrustumShared;
                myFrustumInitialized.get() = true;
            }
            // We only want to actually match imageable prims. This is the
            // level at which it is possible to compute a meaningful bound.
            if (myFrustum.get().Intersects(primbox))
                return prim.IsA<UsdGeomImageable>();
        }
        else if (myBoundsType == BOX)
        {
            UT_Vector3D bmin = GusdUT_Gf::Cast(primbox.GetRange().GetMin());
            UT_Vector3D bmax = GusdUT_Gf::Cast(primbox.GetRange().GetMax());
            UT_Matrix4D bxform = GusdUT_Gf::Cast(primbox.GetMatrix());
            UT_Vector3D bdelta = (bmax + bmin) * 0.5;
            bxform.pretranslate(bdelta);

            // Transform the prim bbox into the space of the main bbox.
            // Scale the prim bbox at the origin before extracting the
            // translations and rotations, which are the only transforms
            // that can be passed to doBoxBoxOverlap.
            UT_Matrix4D dxform = bxform * myBoxIXform;
            UT_Matrix3D dscale;
            if (dxform.makeRigidMatrix(&dscale))
            {
                UT_Vector3D dtrans;
                UT_Matrix3D drot(dxform);
                drot.makeRotationMatrix();
                dxform.getTranslates(dtrans);

                UT_Vector3D rb = SYSabs(bmin - bdelta);
                rb *= dscale;

                // We only want to actually match imageable prims. This is the
                // level at which it is possible to compute a meaningful bound.
                if (BV_Overlap::doBoxBoxOverlap(myBox, rb, drot, dtrans))
                    return prim.IsA<UsdGeomImageable>();
            }
        }

        // Handle the INVALID state, and any out-of-bounds results. If a prim
        // is out of bounds, all its children will be out of bounds too, if
        // the bounds hierarchy is authored correctly.
        *prune_branch = true;
        return false;
    }

private:
    enum BoundsType {
        BOX,
        FRUSTUM,
        INVALID
    };
    typedef UT_UniquePtr<UsdGeomBBoxCache> BBoxCachePtr;

    void initialize(HUSD_AutoAnyLock &lock)
    {
        if (lock.constData() && lock.constData()->isStageValid())
        {
            UsdStageRefPtr  stage = lock.constData()->stage();
            UsdPrim         prim = stage->GetPrimAtPath(myPath);

            UsdGeomCamera cam(prim);
            if (cam)
            {
                myFrustumShared = cam.GetCamera(myUsdTimeCode).GetFrustum();
                myBoundsType = FRUSTUM;
                return;
            }

            UsdGeomImageable imageable(prim);
            if (imageable)
            {
                UsdGeomBBoxCache   bboxcache(myUsdTimeCode,
                    UsdGeomImageable::GetOrderedPurposeTokens(), true, true);

                // Pre-calculate values from the box that we'll need for the
                // intersection tests.
                GfBBox3d box = bboxcache.ComputeWorldBound(prim);
                UT_Vector3D bmin = GusdUT_Gf::Cast(box.GetRange().GetMin());
                UT_Vector3D bmax = GusdUT_Gf::Cast(box.GetRange().GetMax());
                UT_Vector3D bcenter = (bmin + bmax) * 0.5;

                myBoxIXform = GusdUT_Gf::Cast(box.GetInverseMatrix());
                myBoxIXform.translate(-bcenter);
                myBox = SYSabs(bmin - bcenter);
                myBoundsType = BOX;
                return;
            }
        }
    }

    SdfPath                                          myPath;
    UT_Matrix4D                                      myBoxIXform;
    UT_Vector3D                                      myBox;
    GfFrustum                                        myFrustumShared;
    mutable UT_ThreadSpecificValue<GfFrustum>        myFrustum;
    mutable UT_ThreadSpecificValue<bool>             myFrustumInitialized;
    BoundsType                                       myBoundsType = INVALID;
    mutable UT_ThreadSpecificValue<BBoxCachePtr>     myBBoxCache;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_GeoFromMatAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_GeoFromMatAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_GeoFromMatAutoCollection(
            const char *token,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(token, lock, demands, nodeid, timecode)
    {
        UT_String tokenstr(token);
        UT_StringArray subtokens;
        tokenstr.tokenize(subtokens, ",");

        if (subtokens.size() > 0)
        {
            // Special case looking for prims with no bound material.
            if (subtokens(0) == "none")
            {
                myMaterialUnbound = true;
            }
            else
            {
                myMaterialPath = HUSDgetSdfPath(subtokens(0));
                myMaterialUnbound = false;
            }
        }

        if (subtokens.size() > 1)
            myMaterialPurpose = TfToken(subtokens(1).toStdString());
        else
            myMaterialPurpose = UsdShadeTokens->allPurpose;
    }
    ~XUSD_GeoFromMatAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        UsdShadeMaterial material =
            UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial(
                &myBindingsCache, &myCollectionCache,
                myMaterialPurpose);

        if (material)
        {
            if (!myMaterialUnbound && material.GetPath() == myMaterialPath)
                return true;
        }
        else
        {
            if (myMaterialUnbound)
                return true;
        }

        return false;
    }

private:
    SdfPath                                                  myMaterialPath;
    TfToken                                                  myMaterialPurpose;
    bool                                                     myMaterialUnbound;
    mutable UsdShadeMaterialBindingAPI::BindingsCache        myBindingsCache;
    mutable UsdShadeMaterialBindingAPI::CollectionQueryCache myCollectionCache;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_MatFromGeoAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_MatFromGeoAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_MatFromGeoAutoCollection(
            const char *token,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(token, lock, demands, nodeid, timecode)
    {
        UT_String tokenstr(token);
        UT_StringArray subtokens;
        tokenstr.tokenize(subtokens, ",");

        if (subtokens.size() > 0)
            myGeoPath = HUSDgetSdfPath(subtokens(0));

        if (subtokens.size() > 1)
            myMaterialPurpose = TfToken(subtokens(1).toStdString());
        else
            myMaterialPurpose = UsdShadeTokens->allPurpose;
    }
    ~XUSD_MatFromGeoAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();
        UsdPrim root = stage->GetPrimAtPath(myGeoPath);
        auto predicate = HUSDgetUsdPrimPredicate(myDemands);
        std::vector<UsdPrim> prims;

        if (root)
        {
            XUSD_FindUsdPrimsTaskData data;
            auto &task = *new(UT_Task::allocate_root())
                XUSD_FindPrimsTask(root, data, predicate,
                    nullptr, nullptr);
            UT_Task::spawnRootAndWait(task);

            data.gatherPrimsFromThreads(prims);

            std::vector<UsdShadeMaterial> materials =
                UsdShadeMaterialBindingAPI::ComputeBoundMaterials(
                    prims, myMaterialPurpose);

            for (auto &&material : materials)
            {
                if (material)
                    matches.insert(material.GetPath());
            }
        }
    }

private:
    SdfPath                          myGeoPath;
    TfToken                          myMaterialPurpose;
};


////////////////////////////////////////////////////////////////////////////
// XUSD_RelationshipAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RelationshipAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_RelationshipAutoCollection(
            const char *token,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(token, lock, demands, nodeid, timecode)
    {
        myRelationshipPath = HUSDgetSdfPath(token);
        initialize(lock);
    }
    ~XUSD_RelationshipAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        if (myTargetPaths.containsPathOrDescendant(HUSD_Path(prim.GetPath())))
            return myTargetPaths.contains(prim.GetPath());

        *prune_branch = true;
        return false;
    }

private:
    void initialize(HUSD_AutoAnyLock &lock)
    {
        if (lock.constData() && lock.constData()->isStageValid())
        {
            UsdStageRefPtr  stage = lock.constData()->stage();
            UsdRelationship rel =
                stage->GetRelationshipAtPath(myRelationshipPath);

            if (rel)
            {
                SdfPathVector   targets;

                rel.GetForwardedTargets(&targets);
                for (auto &&target : targets)
                    myTargetPaths.insert(HUSD_Path(target));
            }
        }
    }

    SdfPath                          myRelationshipPath;
    HUSD_PathSet                     myTargetPaths;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RenderSettingsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RenderSettingsAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RenderSettingsAutoCollection(
           const char *token,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(token, lock, demands, nodeid, timecode)
    { }
    ~XUSD_RenderSettingsAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();
        auto settingsPrim = UsdRenderSettings::GetStageRenderSettings(stage);
        if (settingsPrim)
            matches.insert(settingsPrim.GetPath());
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RenderCameraAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RenderCameraAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RenderCameraAutoCollection(
           const char *token,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(token, lock, demands, nodeid, timecode)
    {
        if (UT_String(token).length() > 1 && *token == ':')
            mySettingsPath = HUSDgetSdfPath(token+1);
    }
    ~XUSD_RenderCameraAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();

        auto settingsPrim = UsdRenderSettings::GetStageRenderSettings(stage);
        if (!mySettingsPath.IsEmpty())
            settingsPrim = UsdRenderSettings::Get(stage, mySettingsPath);
        
        if (settingsPrim)
        {
            UsdRelationship cameraRel = settingsPrim.GetCameraRel();
            if(cameraRel)
            {
                SdfPathVector   targets;
                cameraRel.GetForwardedTargets(&targets);
                if(!targets.empty())
                    matches.insert(targets.front());
            }
        }
    }

protected:
    SdfPath                          mySettingsPath;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RenderProductsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RenderProductsAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RenderProductsAutoCollection(
           const char *token,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(token, lock, demands, nodeid, timecode)
    {
        if (UT_String(token).length() > 1 && *token == ':')
            mySettingsPath = HUSDgetSdfPath(token+1);
    }
    ~XUSD_RenderProductsAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();

        auto settingsPrim = UsdRenderSettings::GetStageRenderSettings(stage);
        if (!mySettingsPath.IsEmpty())
            settingsPrim = UsdRenderSettings::Get(stage, mySettingsPath);
        if(settingsPrim)
        {
            UsdRelationship productsRel = settingsPrim.GetProductsRel();
            if(productsRel)
            {
                SdfPathVector   targets;
                productsRel.GetForwardedTargets(&targets);
                for (auto &&target : targets)
                    matches.insert(target);
            }
        }
    }

protected:
    SdfPath                          mySettingsPath;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RenderVarsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RenderVarsAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RenderVarsAutoCollection(
           const char *token,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(token, lock, demands, nodeid, timecode)
    {
        if (UT_String(token).length() > 1 && *token == ':')
            mySettingsPath = HUSDgetSdfPath(token+1);
    }
    ~XUSD_RenderVarsAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();

        auto settingsPrim = UsdRenderSettings::GetStageRenderSettings(stage);
        if (!mySettingsPath.IsEmpty())
            settingsPrim = UsdRenderSettings::Get(stage, mySettingsPath);
        if(settingsPrim)
        {
            UsdRelationship productsRel = settingsPrim.GetProductsRel();
            if(productsRel)
            {
                SdfPathVector   products;
                productsRel.GetForwardedTargets(&products);
                for (auto &&product : products)
                {
                    auto productPrim = UsdRenderProduct::Get(stage, product);
                    if(productPrim)
                    {
                        UsdRelationship varsRel =
                            productPrim.GetOrderedVarsRel();
                        if(varsRel)
                        {
                            SdfPathVector   targets;
                            varsRel.GetForwardedTargets(&targets);
                            for (auto &&target : targets)
                                matches.insert(target);
                        }
                    }
                }
            }
        }
    }

protected:
    SdfPath                          mySettingsPath;
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
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_BoundAutoCollection>("bound:"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_GeoFromMatAutoCollection>("geofrommat:"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_MatFromGeoAutoCollection>("matfromgeo:"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RelationshipAutoCollection>("rel:"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RenderSettingsAutoCollection>("rendersettings"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RenderCameraAutoCollection>("rendercamera"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RenderProductsAutoCollection>("renderproducts"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RenderVarsAutoCollection>("rendervars"));
    if (!thePluginsInitialized)
    {
        UT_DSO dso;

        dso.run("newAutoCollection");
        thePluginsInitialized = true;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

