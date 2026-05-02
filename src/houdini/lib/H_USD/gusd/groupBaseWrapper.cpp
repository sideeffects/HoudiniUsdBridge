//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "groupBaseWrapper.h"

#include "context.h"
#include "GU_PackedUSD.h"
#include "GU_USD.h"
#include "USD_XformCache.h"
#include "UT_Gf.h"

#include "pxr/usd/usdGeom/boundable.h"

#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_PrimCollect.h>
#include <GT/GT_Refine.h>
#include <GT/GT_RefineParms.h>


PXR_NAMESPACE_OPEN_SCOPE

// drand48 and srand48 defined in SYS_Math.h as of 13.5.153. and conflicts with imath.
#undef drand48
#undef srand48

#ifdef DEBUG
#define DBG(x) x
#else
#define DBG(x)
#endif

GusdGroupBaseWrapper::GusdGroupBaseWrapper() 
    : GusdPrimWrapper()
{
}

GusdGroupBaseWrapper::GusdGroupBaseWrapper( 
        UsdTimeCode     time, 
        GusdPurposeSet  purposes  ) 
    : GusdPrimWrapper( time, purposes )
{
}

GusdGroupBaseWrapper::GusdGroupBaseWrapper( const GusdGroupBaseWrapper &in )
    : GusdPrimWrapper( in )
{
}

GusdGroupBaseWrapper::~GusdGroupBaseWrapper()
{}

/// The prim can be unpacked to a child prim that is imageable and matches the
/// purpose filter.
static bool
gusdShouldUnpackChild(const UsdPrim& p, GusdPurposeSet purposes)
{
    UsdGeomImageable ip(p);
    if (!ip)
        return false;

    TfToken purpose;
    ip.GetPurposeAttr().Get(&purpose);
    if (!GusdPurposeInSet(purpose, purposes) && !p.IsPrototype())
        return false;

    return true;
}

bool
GusdGroupBaseWrapper::unpack(UT_Array<GU_DetailHandle> &details,
                             const UT_StringRef &fileName,
                             const SdfPath &primPath,
                             const UT_Matrix4D *xform,
                             fpreal frame,
                             const char *viewportLod,
                             GusdPurposeSet purposes,
                             const GT_RefineParms &rparms) const
{
    UsdPrim usdPrim = getUsdPrim().GetPrim();

    UT_Matrix4D gt_prim_xform(1.0);
    if (getPrimitiveTransform())
        getPrimitiveTransform()->getMatrix(gt_prim_xform);

    // To unpack a xform or a group, create a packed prim for
    // each child
    UT_Array<UsdPrim> usefulChildren;
    for( const auto& child : usdPrim.GetFilteredChildren(
                        UsdTraverseInstanceProxies(UsdPrimDefaultPredicate)) )
    {
        if (gusdShouldUnpackChild(child, purposes))
            usefulChildren.append(child);
    }

    if (usefulChildren.isEmpty())
        return true;

    // Sort the children to maintain consistency in unpacking.
    GusdUSD_Utils::SortPrims(usefulChildren);

    GU_DetailHandle gdh;
    gdh.allocateAndSet(new GU_Detail());
    GU_DetailHandleAutoWriteLock gdp(gdh);

    const auto pivot = static_cast<GusdGU_PackedUSD::PivotLocation>(
            GT_RefineParms::getInt(&rparms, GUSD_REFINE_PIVOTLOCATION, 0));

    SdfPath strippedPathHead(primPath.StripAllVariantSelections());
    for( const auto &child : usefulChildren )
    {
        // Replace the head of the path to perserve variant specs.
        SdfPath path = child.GetPath().ReplacePrefix(
                strippedPathHead, primPath);

        UT_Matrix4D child_xform;
        GusdUSD_XformCache::GetInstance().GetLocalTransformation(
                child, frame, child_xform);
        child_xform *= gt_prim_xform;
        if (xform)
            child_xform *= *xform;

        GusdGU_PackedUSD::Build(
                *gdp, fileName, path, frame, viewportLod, purposes, child,
                &child_xform, pivot);
    }

    details.append(gdh);
    return true;
}

bool
GusdGroupBaseWrapper::refineGroup( 
    const UsdPrim& prim,
    GT_Refine& refiner,
    const GT_RefineParms* parms ) const
{
    UT_IntrusivePtr<GT_PrimCollect> collection;
    for (const UsdPrim& child : prim.GetFilteredChildren(
                 UsdTraverseInstanceProxies(UsdPrimDefaultPredicate)))
    {
        if (!gusdShouldUnpackChild(child, m_purposes))
            continue;

        GT_PrimitiveHandle gtPrim = 
            GusdPrimWrapper::defineForRead( 
                    UsdGeomImageable(child), 
                    m_time,
                    m_purposes );

        if( gtPrim )
        {
            UT_Matrix4D m;
            GusdUSD_XformCache::GetInstance().GetLocalTransformation( 
                    child, m_time, m );
            gtPrim->setPrimitiveTransform(UTmakeIntrusive<GT_Transform>(&m, 1));

            if (!collection)
                collection = UTmakeIntrusive<GT_PrimCollect>();

            collection->appendPrimitive(gtPrim);
        }
    }

    if (collection)
    {
        collection->setPrimitiveTransform(getPrimitiveTransform());
        refiner.addPrimitive(collection);
        return true;
    }
    return false;
}

bool 
GusdGroupBaseWrapper::updateGroupFromGTPrim(
    const UsdGeomImageable&   destPrim,
    const GT_PrimitiveHandle& sourcePrim,
    const UT_Matrix4D&        houXform,
    const GusdContext&        ctxt,
    GusdSimpleXformCache&     xformCache )
{
    if( !destPrim )
        return false;

    if( !ctxt.writeOverlay && ctxt.purpose != UsdGeomTokens->default_ ) {
        destPrim.GetPurposeAttr().Set( ctxt.purpose );
    }

    if( !ctxt.writeOverlay || ctxt.overlayTransforms || ctxt.overlayAll )
    {
        GfMatrix4d xform = computeTransform( 
                        destPrim.GetPrim().GetParent(),
                        ctxt.time,
                        houXform,
                        xformCache );


        updateTransformFromGTPrim( xform, ctxt.time, 
                                   ctxt.granularity == GusdContext::PER_FRAME );

        // cache this transform so that if we write a child, we can compute its
        // relative transform.
        xformCache[destPrim.GetPrim().GetPath()] = houXform;
    }

    // sourcePrim can be NULL if the ROP wants to write a transform without having
    // a corresponding GT_Primitive
    if( !sourcePrim ) {
        return true;
    }

    if( !ctxt.writeOverlay || ctxt.overlayPrimvars || ctxt.overlayAll )
    {
        GusdGT_AttrFilter filter = ctxt.attributeFilter;
        filter.appendPattern(GT_OWNER_UNIFORM, "^P");
        if( const GT_AttributeListHandle uniformAttrs = sourcePrim->getUniformAttributes())
        {
            GusdGT_AttrFilter::OwnerArgs owners;
            owners << GT_OWNER_UNIFORM;
            filter.setActiveOwners(owners);
            updatePrimvarFromGTPrim( uniformAttrs, filter, UsdGeomTokens->uniform, ctxt.time );
        }
    }

    // Set active state
    updateGroupActiveFromGTPrim(destPrim, sourcePrim, ctxt.time);

    return true;
}

void
GusdGroupBaseWrapper::updateGroupActiveFromGTPrim(
        const UsdGeomImageable& destPrim,
        const GT_PrimitiveHandle& sourcePrim,
        UsdTimeCode time)
{
    UsdPrim prim = destPrim.GetPrim();

    GT_Owner attrOwner;
    GT_DataArrayHandle houAttr
        = sourcePrim->findAttribute(GUSD_ACTIVE_ATTR, attrOwner, 0);
    if (houAttr) {
        GT_String state = houAttr->getS(0);
        if (state) {
            if (strcmp(state, "active") == 0) {
                prim.SetActive(true);
            } else if (strcmp(state, "inactive") == 0) {
                prim.SetActive(false);
            }
        }
    }
}


PXR_NAMESPACE_CLOSE_SCOPE
