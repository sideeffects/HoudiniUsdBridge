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
#include "GU_PackedUSD.h"

#include "GT_PackedUSD.h"
#include "GT_Utils.h"
#include "primWrapper.h"

#include "GU_USD.h"
#include "stageEdit.h"

#include "GT_PrimCache.h"
#include "USD_XformCache.h"
#include "boundsCache.h"

#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdVol/fieldBase.h>

#include <OP/OP_Node.h>
#include <OP/OP_Channels.h>
#include <OP/OP_DataTypes.h>
#include <OP/OP_Director.h>
#include <CH/CH_Manager.h>
#include <GA/GA_SaveMap.h>
#include <GT/GT_PrimInstance.h>
#include <GT/GT_GEODetail.h>
#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_PrimPointMesh.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_RefineCollect.h>
#include <GT/GT_RefineParms.h>
#include <GT/GT_TransformArray.h>
#include <GU/GU_MergeUtils.h>
#include <GU/GU_PackedFactory.h>
#include <GU/GU_PrimPacked.h>
#include <UT/UT_DMatrix4.h>
#include <UT/UT_Map.h>
#include <SYS/SYS_TypeTraits.h>

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;

#ifdef DEBUG
#define DBG(x) x
#else
#define DBG(x)
#endif

namespace {

static constexpr UT_StringLit thePackedTypeName("PackedUSD");
static constexpr UT_StringLit thePackedTypeLabel("Packed USD");
static constexpr UT_StringLit thePackedTypeIcon("PRIMITIVES_packedusd");

static constexpr UT_StringLit theUsdFileNameName("usdFileName");
static constexpr UT_StringLit theUsdAltFileNameName("usdAltFileName");
static constexpr UT_StringLit theUsdPrimPathName("usdPrimPath");
static constexpr UT_StringLit theUsdLocalToWorldTransformName("usdLocalToWorldTransform");
static constexpr UT_StringLit theUsdFrameName("usdFrame");
static constexpr UT_StringLit theUsdSrcPrimPathName("usdSrcPrimPath");
static constexpr UT_StringLit theUsdIndexName("usdIndex");
static constexpr UT_StringLit theUsdTypeName("usdType");
static constexpr UT_StringLit theUsdPurposeName("usdPurpose");
static constexpr UT_StringLit theUsdViewportPurposeName("usdViewportPurpose");

class UsdPackedFactory : public GU_PackedFactory
{
public:
    UsdPackedFactory()
        : GU_PackedFactory(
                  thePackedTypeName.asHolder(),
                  thePackedTypeLabel.asHolder(),
                  thePackedTypeIcon.asHolder())
        , theDefaultImpl(UTmakeIntrusive<GusdGU_PackedUSD>())
    {
        registerIntrinsic(theUsdFileNameName.asHolder(),
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicFileName),
            StringHolderSetterCast(&GusdGU_PackedUSD::setFileName));
        registerIntrinsic(theUsdAltFileNameName.asHolder(),
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicAltFileName),
            StringHolderSetterCast(&GusdGU_PackedUSD::setAltFileName));
        registerIntrinsic(theUsdPrimPathName.asHolder(),
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicPrimPath),
            StringHolderSetterCast(&GusdGU_PackedUSD::setPrimPath));
        // The USD prim's localToWorldTransform is stored in this intrinsic.
        // This may differ from the packed prim's actual transform.
        registerTupleIntrinsic(theUsdLocalToWorldTransformName.asHolder(),
            IntGetterCast(&GusdGU_PackedUSD::usdLocalToWorldTransformSize),
            F64VectorGetterCast(&GusdGU_PackedUSD::usdLocalToWorldTransform),
            NULL);
        registerIntrinsic(theUsdFrameName.asHolder(),
            FloatGetterCast(&GusdGU_PackedUSD::intrinsicFrame),
            FloatSetterCast(&GusdGU_PackedUSD::setFrame));
        registerIntrinsic(theUsdSrcPrimPathName.asHolder(),
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicSrcPrimPath),
            StringHolderSetterCast(&GusdGU_PackedUSD::setSrcPrimPath));
        registerIntrinsic(theUsdIndexName.asHolder(),
            IntGetterCast(&GusdGU_PackedUSD::index),
            IntSetterCast(&GusdGU_PackedUSD::setIndex));
        registerIntrinsic(theUsdTypeName.asHolder(),
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicType));
        registerTupleIntrinsic(theUsdViewportPurposeName.asHolder(),
            IntGetterCast(&GusdGU_PackedUSD::getNumViewportPurposes),
            StringArrayGetterCast(&GusdGU_PackedUSD::getIntrinsicViewportPurposes),
            StringArraySetterCast(&GusdGU_PackedUSD::setIntrinsicViewportPurposes),
	    nullptr,
	    GU_PackedFactory::CollapseSingletons::NO);
        registerIntrinsic(theUsdPurposeName.asHolder(),
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicPurpose));
    }
    ~UsdPackedFactory() override {}

    const UT_IntrusivePtr<GU_PackedImpl> &defaultImpl() const override
    {
        return theDefaultImpl;
    }

    GU_PackedImpl *create() const override
    {
        return new GusdGU_PackedUSD();
    }

    exint clearCachedGeometry() override
    {
        GusdStageCacheWriter cache;
        return cache.ClearEntriesFromDisk();
    }

    UT_IntrusivePtr<GU_PackedImpl> theDefaultImpl;
};

static UsdPackedFactory *theFactory = nullptr;

} // close namespace 

GusdPackedUSDTracker GusdGU_PackedUSD::thePackedUSDTracker;
GA_PrimitiveTypeId GusdGU_PackedUSD::theTypeId(-1);

void
GusdGU_PackedUSD::setPackedUSDTracker(GusdPackedUSDTracker tracker)
{
    // This callback should only be set once.
    UT_ASSERT(!thePackedUSDTracker);
    thePackedUSDTracker = tracker;
}

/* static */
GU_PrimPacked* 
GusdGU_PackedUSD::Build( 
    GU_Detail&              detail, 
    const UT_StringHolder&  fileName, 
    const SdfPath&          primPath, 
    UsdTimeCode             frame, 
    const char*             lod,
    GusdPurposeSet          purposes,
    const UsdPrim&          prim,
    const UT_Matrix4D*      xform,
    PivotLocation           pivotloc )
{
    auto packedPrim = GU_PrimPacked::build(detail, thePackedTypeName.asRef());
    auto impl = UTverify_cast<GusdGU_PackedUSD *>(packedPrim->hardenImplementation());
    impl->m_fileName = fileName;
    impl->m_primPath = primPath;
    impl->m_frame = frame;

    if( lod )
    {
        impl->intrinsicSetViewportLOD( packedPrim, lod );
    }
    impl->setPurposes( packedPrim, purposes );

    // It seems that Houdini may reuse memory for packed implementations with
    // out calling the constructor to initialize data. 
    impl->resetCaches();

    // If a UsdPrim was passed in, make sure it is used.
    impl->m_usdPrim = prim;

    impl->initializePivot(packedPrim, pivotloc);

    if (xform) {
        impl->setTransform(packedPrim, *xform);
    } else {
        impl->updateTransform(packedPrim);
    }

    // Register this newly built packed USD prim. The m_usdPrim is already
    // set, so we want to register right away.
    if (thePackedUSDTracker)
        thePackedUSDTracker(impl, true);

    return packedPrim;
}

/* static */
GU_PrimPacked* 
GusdGU_PackedUSD::Build( 
    GU_Detail&              detail, 
    const UT_StringHolder&  fileName, 
    const SdfPath&          primPath, 
    const SdfPath&          srcPrimPath,
    int                     index,
    UsdTimeCode             frame, 
    const char*             lod,
    GusdPurposeSet          purposes,
    const UsdPrim&          prim,
    const UT_Matrix4D*      xform,
    PivotLocation           pivotloc )
{
    auto packedPrim = GU_PrimPacked::build(detail, thePackedTypeName.asRef());
    auto impl = UTverify_cast<GusdGU_PackedUSD *>(packedPrim->hardenImplementation());
    impl->m_fileName = fileName;
    impl->m_primPath = primPath;
    impl->m_srcPrimPath = srcPrimPath;
    impl->m_index = index;
    impl->m_frame = frame;
    if( lod ) {
        impl->intrinsicSetViewportLOD( packedPrim, lod );
    }
    impl->setPurposes( packedPrim, purposes );

    // It seems that Houdini may reuse memory for packed implementations with
    // out calling the constructor to initialize data. 
    impl->resetCaches();

    // If a UsdPrim was passed in, make sure it is used.
    impl->m_usdPrim = prim;

    impl->initializePivot(packedPrim, pivotloc);

    if (xform) {
        impl->setTransform(packedPrim, *xform);
    } else {
        impl->updateTransform(packedPrim);
    }

    // Register this newly built packed USD prim. The m_usdPrim is already
    // set, so we want to register right away.
    if (thePackedUSDTracker)
        thePackedUSDTracker(impl, true);

    return packedPrim;
}


/* static */
GU_PrimPacked* 
GusdGU_PackedUSD::Build( 
    GU_Detail&              detail,
    const UsdPrim&          prim,
    UsdTimeCode             frame,
    const char*             lod,
    GusdPurposeSet          purposes,
    const UT_Matrix4D*      xform,
    PivotLocation           pivotloc )
{
    const std::string &filename =
        prim.GetStage()->GetRootLayer()->GetIdentifier();
    return GusdGU_PackedUSD::Build(detail, filename,
                                   prim.GetPath(), frame, lod,
                                   purposes, prim, xform, pivotloc);
}


GusdGU_PackedUSD::GusdGU_PackedUSD()
    : GU_PackedImpl()
    , m_transformCacheValid(false)
    , m_prototypePathCacheValid(false)
    , m_index(-1)
    , m_frame(std::numeric_limits<float>::min())
    , m_purposes( GusdPurposeSet( GUSD_PURPOSE_DEFAULT | GUSD_PURPOSE_PROXY ))
{
}

GusdGU_PackedUSD::GusdGU_PackedUSD( const GusdGU_PackedUSD &src )
    : GU_PackedImpl( src )
    , m_fileName( src.m_fileName )
    , m_altFileName( src.m_altFileName )
    , m_primPath( src.m_primPath )
    , m_srcPrimPath( src.m_srcPrimPath )
    , m_index( src.m_index )
    , m_frame( src.m_frame )
    , m_purposes( src.m_purposes )
    , m_usdPrim( src.m_usdPrim )
    , m_transformCacheValid( src.m_transformCacheValid )
    , m_transformCache( src.m_transformCache )
    , m_prototypePathCacheValid( src.m_prototypePathCacheValid )
    , m_prototypePathCache( src.m_prototypePathCache )
    , m_gtPrimCache( NULL )
{
    // Register this new packed USD prim if m_usdPrim has already been set.
    // Otherwise we can wait until getUsdPrim is called.
    if (m_usdPrim && thePackedUSDTracker)
        thePackedUSDTracker(this, true);
}

GusdGU_PackedUSD::~GusdGU_PackedUSD()
{
    // Deregister this packed USD prim.
    if (thePackedUSDTracker)
        thePackedUSDTracker(this, false);
}

void
GusdGU_PackedUSD::install( GA_PrimitiveFactory &gafactory )
{
    if (theFactory)
        return;

    theFactory = new UsdPackedFactory();
    GU_PrimPacked::registerPacked( &gafactory, theFactory );  
    theTypeId = theFactory->typeDef().getId();

    const GA_PrimitiveDefinition *def
            = GU_PrimPacked::lookupTypeDef(thePackedTypeName.asRef());

    // Bind GEOPrimCollect for collecting GT prims for display in the viewport
    static GusdGT_PrimCollect *collector = new GusdGT_PrimCollect();
    collector->bind(def->getId());  
}

void
GusdGU_PackedUSD::resetCaches()
{
    clearBoxCache();
    m_usdPrim = UsdPrim();
    m_transformCacheValid = false;
    m_gtPrimCache = GT_PrimitiveHandle();
}

void
GusdGU_PackedUSD::updateTransform( GU_PrimPacked* prim )
{
    // Just mark as dirty - getLocalTransform() will provide the updated USD
    // xform.
    prim->transformDirty();
}

void
GusdGU_PackedUSD::setTransform( GU_PrimPacked* prim, const UT_Matrix4D& mx )
{
    UT_Matrix4D xform = getUsdTransform();
    xform.invert();
    xform *= mx;

    UT_Vector3 pivot;
    prim->getPivot(pivot);

    prim->setLocalTransform(UT_Matrix3D(xform));
    prim->setPos3(0, pivot * xform);
}

void
GusdGU_PackedUSD::initializePivot(GU_PrimPacked *prim, PivotLocation pivotloc)
{
    switch (pivotloc)
    {
    case PivotLocation::Origin:
    {
        // Place at the origin of the coordinate frame.
        UT_Vector3 pivot;
        getUsdTransform().getTranslates(pivot);

        prim->setPivot(pivot);
        prim->setPos3(0, pivot);
        break;
    }

    case PivotLocation::Centroid:
    {
        UT_Vector3 center(0, 0, 0);
        UT_BoundingBox bbox;
        if (getBounds(bbox))
            center = bbox.center();

        // getBounds() returns the untransformed bounds, so transform the
        // center to world space.
        const UT_Vector3 pivot = center * getUsdTransform();
        prim->setPivot(pivot);
        prim->setPos3(0, pivot);
        break;
    }
    }
}

void
GusdGU_PackedUSD::setFileName( GU_PrimPacked* prim, const UT_StringHolder& fileName )
{
    if( fileName != m_fileName )
    {
        // Deregister this USD prim because the file name is about to change.
        // The resetCaches call clears the m_usdPrim member, and so we will be
        // re-registered when getUsdPrim is called.
        if (thePackedUSDTracker)
            thePackedUSDTracker(this, false);
        m_fileName = fileName;
        resetCaches();
        // Notify base primitive that topology has changed
        prim->topologyDirty();
        updateTransform(prim);
    }
}

void
GusdGU_PackedUSD::setAltFileName( const UT_StringHolder& fileName ) 
{
    if( fileName != m_altFileName )
    {
        m_altFileName = fileName;
    }
}

void
GusdGU_PackedUSD::setPrimPath( GU_PrimPacked* prim, const UT_StringHolder& p ) 
{
    SdfPath path;
    GusdUSD_Utils::CreateSdfPath(p, path);
    setPrimPath(prim, path);
}


void
GusdGU_PackedUSD::setPrimPath( GU_PrimPacked* prim, const SdfPath &path ) 
{
    if( path != m_primPath )
    {
        m_primPath = path;
        resetCaches();
        // Notify base primitive that topology has changed
        prim->topologyDirty();
        updateTransform(prim);
    }
}

void
GusdGU_PackedUSD::setSrcPrimPath( const UT_StringHolder& p )
{
    SdfPath path;
    GusdUSD_Utils::CreateSdfPath(p, path);
    setSrcPrimPath(path);
}

void
GusdGU_PackedUSD::setSrcPrimPath( const SdfPath &path ) 
{
    if( path != m_srcPrimPath ) {
        m_srcPrimPath = path;
    }
}

void
GusdGU_PackedUSD::setIndex( exint index ) 
{
    if( index != m_index ) {
        m_index = index;
    }
}

void
GusdGU_PackedUSD::setFrame(
        GU_PrimPacked *prim,
        UsdTimeCode frame,
        UT_Optional<PivotLocation> pivotloc)
{
    if( frame != m_frame )
    {
        m_frame = frame;
        resetCaches();

        if (pivotloc)
            initializePivot(prim, *pivotloc);

        // Notify base primitive that topology has changed
        prim->topologyDirty();
        updateTransform(prim);
    }
}

void
GusdGU_PackedUSD::setFrame( GU_PrimPacked* prim, fpreal frame )
{
    setFrame(prim, UsdTimeCode(frame));
}

UT_StringHolder
GusdGU_PackedUSD::intrinsicPurpose(const GU_PrimPacked *prim) const
{
    TfToken purpose;
    UsdPrim usd_prim = getUsdPrim();
    if (UsdGeomImageable imageable = UsdGeomImageable(usd_prim))
        purpose = imageable.ComputePurpose();

    return GusdUSD_Utils::TokenToStringHolder(purpose);
}

exint
GusdGU_PackedUSD::getNumViewportPurposes() const
{
    exint rv = 0;
    if( m_purposes & GUSD_PURPOSE_PROXY )
        ++rv;
    if( m_purposes & GUSD_PURPOSE_RENDER )
        ++rv;
    if( m_purposes & GUSD_PURPOSE_GUIDE )
        ++rv;
    return rv;
}

void 
GusdGU_PackedUSD::setPurposes( GU_PrimPacked* prim, GusdPurposeSet purposes )
{
    m_purposes = purposes;
    if (prim)
        prim->topologyDirty();
    resetCaches();
}

void
GusdGU_PackedUSD::getIntrinsicViewportPurposes(UT_StringArray &purposes) const
{
    static const UT_StringHolder theProxyName
            = GusdUSD_Utils::TokenToStringHolder(UsdGeomTokens->proxy);
    static const UT_StringHolder theRenderName
            = GusdUSD_Utils::TokenToStringHolder(UsdGeomTokens->render);
    static const UT_StringHolder theGuideName
            = GusdUSD_Utils::TokenToStringHolder(UsdGeomTokens->guide);

    purposes.clear();
    if (m_purposes & GUSD_PURPOSE_PROXY)
        purposes.append(theProxyName);
    if (m_purposes & GUSD_PURPOSE_RENDER)
        purposes.append(theRenderName);
    if (m_purposes & GUSD_PURPOSE_GUIDE)
        purposes.append(theGuideName);
}

void 
GusdGU_PackedUSD::setIntrinsicViewportPurposes( GU_PrimPacked* prim, const UT_StringArray& purposes )
{
    // always includ default purpose
    setPurposes(prim,
        GusdPurposeSet(GusdPurposeSetFromArray(purposes)|
                               GUSD_PURPOSE_DEFAULT));
}

UT_StringHolder
GusdGU_PackedUSD::intrinsicType() const
{
    // Return the USD prim type so it can be displayed in the spreadsheet.
    UsdPrim prim = getUsdPrim();
    if (!prim)
        return UT_StringHolder::theEmptyString;

    return GusdUSD_Utils::TokenToStringHolder(prim.GetTypeName());
}

const UT_Matrix4D &
GusdGU_PackedUSD::getUsdTransform() const
{
    if( m_transformCacheValid )
        return m_transformCache;

    UsdPrim prim = getUsdPrim();

    if( !prim ) {
        TF_WARN( "Invalid prim! %s", m_primPath.GetText() );
        m_transformCache = UT_Matrix4D(1);
        return m_transformCache;
    }

    if (GusdUSD_XformCache::GetInstance().GetLocalToWorldTransform(
                prim, m_frame, m_transformCache))
    {
        m_transformCacheValid = true;
    }
    else
        m_transformCache.identity();


    return m_transformCache;
}

void
GusdGU_PackedUSD::usdLocalToWorldTransform(fpreal64* val, exint size) const
{
    UT_ASSERT(size == 16);

    if( isPointInstance() )
    {
        UT_Matrix4D ident(1);
        std::copy( ident.data(), ident.data()+16, val );
    }
    else
    {
        const UT_Matrix4D &m = getUsdTransform();
        std::copy( m.data(), m.data()+16, val );
    }
}

GU_PackedFactory*
GusdGU_PackedUSD::getFactory() const
{
    return theFactory;
}

GU_PackedImpl*
GusdGU_PackedUSD::copy() const
{
    return new GusdGU_PackedUSD(*this);
}

void
GusdGU_PackedUSD::clearData()
{
}

bool
GusdGU_PackedUSD::isValid() const
{
    return bool(m_usdPrim);
}

bool
GusdGU_PackedUSD::load(GU_PrimPacked *prim, const UT_Options &options, const GA_LoadMap &map)
{
    OP_Node     *lop = nullptr;
    int          output_index = 0;
    UT_Options   opts;
    fpreal       t;
    bool         s;

    update( prim, options );

    // If we are loading a packed USD prim that points to a LOP node as its
    // "file", we need to set up a dependency from the source LOP node to the
    // node that has caused this USD packed prim to be loaded from disk.
    if (GusdStageCache::SplitLopStageIdentifier(m_fileName,
            lop, output_index, s, t, opts))
    {
        int          tid = SYSgetSTID();
        CH_Manager  *chman = CHgetManager();
        OP_Channels *destch = CAST_OPCHANNELS(chman->getEvalCollection(tid));
        OP_Node     *destnode = destch ? destch->getNode() : nullptr;

        if (destnode)
            destnode->addExtraInput(lop, OP_INTEREST_DATA);
    }

    return true;
}

void    
GusdGU_PackedUSD::update(GU_PrimPacked *prim, const UT_Options &options)
{
    UT_StringHolder fileName, altFileName, primPath;
    if (options.importOption(theUsdFileNameName.asHolder(), fileName)
        || options.importOption("fileName"_UTsh, fileName))
    {
        // Deregister this USD prim because the file name is about to change.
        // The resetCaches call clears the m_usdPrim member, and so we will be
        // re-registered when getUsdPrim is called.
        if (thePackedUSDTracker)
            thePackedUSDTracker(this, false);
        m_fileName = fileName;
    }

    if (options.importOption(theUsdAltFileNameName.asHolder(), altFileName)
        || options.importOption("altFileName"_UTsh, altFileName))
    {
        setAltFileName(altFileName);
    }

    if (options.importOption(theUsdPrimPathName.asHolder(), primPath)
        || options.importOption("nodePath"_UTsh, primPath))
    {
        GusdUSD_Utils::CreateSdfPath(primPath, m_primPath);
    }

    if (options.importOption(theUsdSrcPrimPathName.asHolder(), primPath))
    {
        GusdUSD_Utils::CreateSdfPath(primPath, m_srcPrimPath);
    }

    exint index;
    if (options.importOption(theUsdIndexName.asHolder(), index))
    {
        m_index = index;
    }

    fpreal frame;
    if (options.importOption(theUsdFrameName.asHolder(), frame)
        || options.importOption("frame"_UTsh, frame))
    {
        m_frame = frame;
    }

    UT_StringArray purposes;
    if (options.importOption(theUsdViewportPurposeName.asHolder(), purposes))
    {
        setIntrinsicViewportPurposes( prim, purposes );
    }
    resetCaches();
}

bool
GusdGU_PackedUSD::save(UT_Options &options, const GA_SaveMap &map) const
{
    options.setOptionS(theUsdFileNameName.asHolder(), m_fileName);
    options.setOptionS(theUsdAltFileNameName.asHolder(), m_altFileName);
    options.setOptionS(theUsdPrimPathName.asHolder(), m_primPath.GetText());
    options.setOptionS(
            theUsdSrcPrimPathName.asHolder(), m_srcPrimPath.GetText());
    options.setOptionI(theUsdIndexName.asHolder(), m_index);
    options.setOptionF(
            theUsdFrameName.asHolder(), GusdUSD_Utils::GetNumericTime(m_frame));

    UT_StringArray purposes;
    getIntrinsicViewportPurposes(purposes);
    options.setOptionSArray(theUsdViewportPurposeName.asHolder(), purposes);
    return true;
}

bool
GusdGU_PackedUSD::getBounds(UT_BoundingBox &box) const
{
    UsdPrim prim = getUsdPrim();

    // It's perfectly valid to not have a primitive here when we are missing
    // the USD file, eg. when loading from a bgeo or stash/locked sop.
    // UT_ASSERT_MSG(prim, "Invalid USD prim");

    if(UsdGeomImageable visPrim = UsdGeomImageable(prim))
    {
        TfTokenVector purposes = GusdPurposeSetToTokens(m_purposes);

        if ( GusdBoundsCache::GetInstance().ComputeUntransformedBound(
                prim,
                UsdTimeCode( m_frame ),
                purposes,
                box )) {
            return true;
        }
    }
    box.makeInvalid();
    return false;
}

bool
GusdGU_PackedUSD::getRenderingBounds(UT_BoundingBox &box) const
{
    return getBoundsCached(box);
}

void
GusdGU_PackedUSD::getVelocityRange(UT_Vector3 &min, UT_Vector3 &max) const
{
    min.assign(0, 0, 0);
    max.assign(0, 0, 0);
}

void
GusdGU_PackedUSD::getWidthRange(fpreal &min, fpreal &max) const
{
    min = 0;
    max = 0;
}

bool
GusdGU_PackedUSD::getLocalTransform(UT_Matrix4D &m) const
{
    m = getUsdTransform();
    return true;
}

static void
Gusd_GetAttribPattern(
        GU_Detail &gdp,
        UT_StringSet &unique_names,
        const UT_StringRef &config_attrib)
{
    GA_ROHandleS pattern_attr = gdp.findStringTuple(
            GA_ATTRIB_DETAIL, config_attrib, 1);
    if (!pattern_attr.isValid())
        return;

    UT_String pattern(pattern_attr.get(GA_DETAIL_OFFSET));

    UT_StringArray attrib_names;
    pattern.tokenize(attrib_names, " ");
    unique_names.insert(attrib_names.begin(), attrib_names.end());

    // Remove the attribute - it will be created on the dest gdp after merging
    // to avoid any unwanted promotion.
    gdp.destroyAttribute(GA_ATTRIB_DETAIL, config_attrib);
}

/// Accumulate attribs like "usdconfigconstantattribs" for the details that
/// will be merged together.
static UT_StringHolder
Gusd_AccumulateAttribPattern(
        GU_Detail &destgdp,
        UT_Array<GU_DetailHandle> &details,
        const UT_StringRef &config_attrib)
{
    UT_StringSet unique_names;

    Gusd_GetAttribPattern(destgdp, unique_names, config_attrib);
    for (GU_DetailHandle &gdh : details)
    {
        GU_DetailHandleAutoWriteLock gdp(gdh);
        Gusd_GetAttribPattern(*gdp, unique_names, config_attrib);
    }

    UT_StringHolder pattern;
    if (!unique_names.empty())
    {
        // Sort the list of names.
        UT_StringArray attrib_names;
        attrib_names.setCapacity(unique_names.size());
        for (const UT_StringHolder &name : unique_names)
            attrib_names.append(name);
        attrib_names.sort();

        UT_WorkBuffer buf;
        buf.append(attrib_names, " ");
        pattern = std::move(buf);
    }

    return pattern;
}

namespace
{
using GusdPrimWrapperConstPtr = UT_IntrusivePtr<const GusdPrimWrapper>;

struct gusdUnpackPrimInfo
{
    GusdPrimWrapperConstPtr myPrim;
    exint myInstanceLevel = 0;
};

/// Performs additional iterations of unpacking (by refining the GT wrappers for
/// USD prims) before converting to SOP geometry.
class gusdPackedUSDRefiner : public GT_Refine
{
public:
    gusdPackedUSDRefiner(const GT_RefineParms &parms, bool unpack_to_polys)
        : myParms(parms)
        , myUnpackToPolys(unpack_to_polys)
        , myRemainingIterations(
                  GT_RefineParms::getInt(&myParms, GUSD_REFINE_ITERATIONS, 1))
    {
    }

    const UT_Array<gusdUnpackPrimInfo> &getPrimsToUnpack() const
    {
        return myPrimsToUnpack;
    }

    bool allowThreading() const final { return false; }

    void addPrimitive(const GT_PrimitiveHandle &prim) final
    {
        int prim_type = prim->getPrimitiveType();

        // Unpacking a non-leaf prim (e.g. Xform) produces a collection of child
        // prims.
        if (prim_type == GT_PRIM_COLLECT)
        {
            prim->refine(*this, &myParms);
            return;
        }

        // Unpacking a point instancer should produce separate packed USD prims
        // for each instance.
        if (prim_type == GT_PRIM_INSTANCE)
        {
            auto instancer = UTverify_cast<const GT_PrimInstance *>(prim.get());
            ++myInstanceLevel;
            instancer->flattenInstances(*this, &myParms);
            --myInstanceLevel;
            return;
        }

        if (prim_type != GusdPrimWrapper::getStaticPrimitiveType())
        {
            // Unpacking to polygons takes place in GusdPrimWrapper::unpack()
            // and shouldn't happen here.
            UT_ASSERT_MSG(
                    false, "We should not have unpacked to anything other than "
                           "GusdPrimWrapper!");
            return;
        }

        auto wrapper = UTverify_cast<const GusdPrimWrapper *>(prim.get());
        const UsdPrim usd_prim = wrapper->getUsdPrim().GetPrim();

        // If we reached a leaf USD prim, we're done. Otherwise, keep refining
        // to child prims until we hit the max number of iterations.
        // Note that we stop at 1 since there is implicitly one iteration
        // already (the initial traversal done in the Unpack USD SOP, along with
        // unpacking to polygons after). A negative number of iterations implies
        // unpacking as far as possible.
        if (myRemainingIterations == 1 || usd_prim.IsA<UsdGeomGprim>()
            || usd_prim.IsA<UsdVolFieldBase>()
            || usd_prim.IsA<UsdGeomCamera>())
        {
            // If we landed on a point instancer, add one since
            // GusdInstancerWrapper::unpack() will bring us to the next instance
            // level.
            exint instance_level = myInstanceLevel;
            if (myUnpackToPolys && usd_prim.IsA<UsdGeomPointInstancer>())
                ++instance_level;

            myPrimsToUnpack.append({wrapper, instance_level});
            return;
        }

        --myRemainingIterations;
        prim->refine(*this, &myParms);
        ++myRemainingIterations;
    }

private:
    GT_RefineParms myParms;
    const bool myUnpackToPolys = false;
    exint myRemainingIterations = 1;
    exint myInstanceLevel = 0;
    UT_SmallArray<gusdUnpackPrimInfo> myPrimsToUnpack;
};
}

bool
GusdGU_PackedUSD::unpackPrim(
    UT_Array<GU_DetailHandle> &details,
    const GU_Detail*        srcgdp,
    const GA_Offset         srcprimoff,
    UsdGeomImageable        prim, 
    const SdfPath&          primPath,
    const UT_Matrix4D*      xform,
    const GT_RefineParms&   rparms ) const
{
    GT_PrimitiveHandle gtPrim = 
        GusdPrimWrapper::defineForRead( 
                    prim,
                    m_frame,
                    m_purposes );

    if( !gtPrim ) {
        const TfToken &type = prim.GetPrim().GetTypeName();
	static const TfToken PxHairman("PxHairman");
        static const TfToken PxProcArgs("PxProcArgs");
        if( type != PxHairman && type != PxProcArgs ) {
            TF_WARN( "Can't convert prim for unpack. %s. Type = %s.", 
                      prim.GetPrim().GetPath().GetText(),
                      type.GetText() );
	}
        return false;
    }

    const char *lod = "full";
    if (srcgdp)
    {
        lod = intrinsicViewportLOD(UTverify_cast<const GU_PrimPacked *>(
                srcgdp->getPrimitive(srcprimoff)));
    }

    const bool unpack_to_polys = GT_RefineParms::getBool(
            &rparms, GUSD_REFINE_UNPACKTOPOLYGONS, true);

    // Perform any additional iterations of unpacking.
    gusdPackedUSDRefiner refiner(rparms, unpack_to_polys);
    refiner.addPrimitive(gtPrim);

    bool add_instance_attrib = false;
    UT_StringHolder instance_attrib_name;
    if (rparms.get(GUSD_REFINE_ADDINSTANCELEVELATTRIB, false)
        && rparms.import(GUSD_REFINE_INSTANCELEVELATTRIB, instance_attrib_name)
        && instance_attrib_name)
    {
        add_instance_attrib = true;
    }

    bool success = true;
    if (unpack_to_polys)
    {
        for (const gusdUnpackPrimInfo &info : refiner.getPrimsToUnpack())
        {
            exint details_start = details.size();
            success &= info.myPrim->unpack(
                    details, fileName(), primPath, xform, intrinsicFrame(), lod,
                    m_purposes, rparms);

            if (add_instance_attrib)
            {
                for (exint i = details_start, n = details.size(); i < n; ++i)
                {
                    GU_Detail &detail = *details[i].gdpNC();
                    GA_RWHandleI iterations_attrib = detail.addIntTuple(
                            GA_ATTRIB_PRIMITIVE, instance_attrib_name, 1);

                    iterations_attrib.makeConstant(info.myInstanceLevel);
                }
            }
        }
    }
    else
    {
        // If not unpacking to polygons, create packed prims for the referenced
        // USD prims.
        GU_DetailHandle gdh;
        gdh.allocateAndSet(new GU_Detail(), /*own=*/true);
        GU_Detail *gdp = gdh.gdpNC();

        GA_RWHandleI instance_attrib;
        if (add_instance_attrib)
        {
            instance_attrib = gdp->addIntTuple(
                    GA_ATTRIB_PRIMITIVE, instance_attrib_name, 1);
        }

        const auto pivot = static_cast<GusdGU_PackedUSD::PivotLocation>(
                GT_RefineParms::getInt(&rparms, GUSD_REFINE_PIVOTLOCATION, 0));

        for (const gusdUnpackPrimInfo &info : refiner.getPrimsToUnpack())
        {
            const GusdPrimWrapperConstPtr &wrapper = info.myPrim;
            UsdPrim prim = wrapper->getUsdPrim().GetPrim();

            UT_Matrix4D prim_xform;
            wrapper->getPrimitiveTransform()->getMatrix(prim_xform);
            if (xform)
                prim_xform *= *xform;

            GU_PrimPacked *packed = GusdGU_PackedUSD::Build(
                    *gdp, prim, intrinsicFrame(), lod, m_purposes, &prim_xform,
                    pivot);

            if (instance_attrib.isValid())
            {
                instance_attrib.set(
                        packed->getMapOffset(), info.myInstanceLevel);
            }
        }

        details.append(gdh);
    }

    return success;
}

bool
GusdGU_PackedUSD::unpackGeometry(
        GU_Detail &destgdp,
        const GU_Detail *srcgdp,
        const GA_Offset srcprimoff,
        const UT_StringRef &primvarPattern,
        bool importInheritedPrimvars,
        const UT_StringRef &attributePattern,
        bool translateSTtoUV,
        const UT_StringRef &nonTransformingPrimvarPattern,
        const UT_Matrix4D *transform,
        const GT_RefineParms *refineParms) const
{
    UT_StringHolder path_attr;
    if (!refineParms || !refineParms->import(GUSD_REFINE_PATHATTRIB, path_attr))
        path_attr = GUSD_PATH_ATTR;

    UT_StringHolder prim_path_attr;
    if (!refineParms
        || !refineParms->import(GUSD_REFINE_PRIMPATHATTRIB, prim_path_attr))
    {
        prim_path_attr = GUSD_PRIMPATH_ATTR;
    }

    UT_Array<GU_DetailHandle> details;
    if (!unpackGeometry(
                details, srcgdp, srcprimoff, primvarPattern,
                importInheritedPrimvars, attributePattern, translateSTtoUV,
                nonTransformingPrimvarPattern, transform, path_attr,
                prim_path_attr, refineParms))
    {
        return false;
    }

    mergeGeometry(destgdp, details);

    return true;
}

void
GusdGU_PackedUSD::mergeGeometry(GU_Detail &destgdp,
                                UT_Array<GU_DetailHandle> &details)
{
    static constexpr UT_StringLit theAttribPatternNames[] = {
        "usdconfigconstantattribs",
        "usdconfigscalarconstantattribs",
        "usdconfigboolattribs",
        "usdconfiguintattribs",
        "usdconfiguint64attribs",
        "usdconfigassetpathattribs",
        "usdconfigindexattribs",
        "usdconfigrelationshipattribs",
        "usdconfigsubsetgroups",
        "usdconfigpartitionattribs",
        // usdconfigprefixpartitionsubsets isn't a pattern, but is only ever
        // set to "0" if it's created, so we can use the same code to keep it
        // as a detail attrib
        "usdconfigprefixpartitionsubsets"
    };
    static constexpr int theNumAttribs = SYSarraySize(theAttribPatternNames);

    // Accumulate attribs like "usdconfigconstantattribs" for the details that
    // will be merged together.
    UT_StringHolder attrib_patterns[theNumAttribs];
    for (int i = 0; i < theNumAttribs; ++i)
    {
        attrib_patterns[i] = Gusd_AccumulateAttribPattern(
                destgdp, details, theAttribPatternNames[i].asRef());
    }

    if (details.size() == 1 && destgdp.isEmpty())
    {
        // Fast path if we're unpacking a single prim into an empty detail.
        destgdp.replaceWith(*details[0].gdp());
    }
    else
    {
        UT_SmallArray<GU_Detail *> gdps;
        for (GU_DetailHandle &gdh : details)
        {
            UT_ASSERT(gdh.isValid());
            gdps.append(gdh.gdpNC());
        }

        GUmatchAttributesAndMerge(destgdp, gdps);
    }

    // Write out the combined attrib patterns to the merged geometry.
    for (int i = 0; i < theNumAttribs; ++i)
    {
        const UT_StringHolder &pattern = attrib_patterns[i];
        if (!pattern.isstring())
            continue;

        GA_RWHandleS pattern_attr = destgdp.addStringTuple(
                GA_ATTRIB_DETAIL, theAttribPatternNames[i].asHolder(), 1);
        pattern_attr.set(GA_DETAIL_OFFSET, pattern);
    }
}

bool
GusdGU_PackedUSD::unpackGeometry(
        UT_Array<GU_DetailHandle> &details,
        const GU_Detail *srcgdp,
        const GA_Offset srcprimoff,
        const UT_StringRef &primvarPattern,
        bool importInheritedPrimvars,
        const UT_StringRef &attributePattern,
        bool translateSTtoUV,
        const UT_StringRef &nonTransformingPrimvarPattern,
        const UT_Matrix4D *transform,
        const UT_StringHolder &filePathAttrib,
        const UT_StringHolder &primPathAttrib,
        const GT_RefineParms *refineParms) const
{
    UsdPrim usdPrim = getUsdPrim();

    if (!usdPrim)
    {
        TF_WARN("Invalid prim found");
        return false;
    }

    GT_RefineParms rparms;
    if (refineParms)
        rparms = *refineParms;

    // Need to manually force polysoup to be turned off.
    rparms.setAllowPolySoup(false);

    rparms.set(GUSD_REFINE_PATHATTRIB, filePathAttrib);
    rparms.set(GUSD_REFINE_PRIMPATHATTRIB, primPathAttrib);

    rparms.set(
        GUSD_REFINE_NONTRANSFORMINGPATTERN, nonTransformingPrimvarPattern);
    rparms.set(GUSD_REFINE_TRANSLATESTTOUV, translateSTtoUV);
    rparms.set(GUSD_REFINE_IMPORTINHERITEDPRIMVARS, importInheritedPrimvars);
    rparms.set(GUSD_REFINE_PRIMVARPATTERN, primvarPattern);
    rparms.set(GUSD_REFINE_ATTRIBUTEPATTERN, attributePattern);

    return unpackPrim(details, srcgdp, srcprimoff, UsdGeomImageable(usdPrim),
                      m_primPath, transform, rparms);
}

bool
GusdGU_PackedUSD::unpack(GU_Detail &destgdp, const UT_Matrix4D *transform) const
{
    // Unpack with "*" as the primvar pattern, meaning unpack all primvars.
    return unpackGeometry(destgdp, nullptr, GA_INVALID_OFFSET, "*", false,
                          UT_StringHolder::theEmptyString, true, GA_Names::rest,
                          transform);
}

bool
GusdGU_PackedUSD::unpackUsingPolygons(GU_Detail &destgdp, const GU_PrimPacked *prim) const
{
    UT_Matrix4D xform;
    if( prim ) {
        prim->getFullTransform4(xform);
    }

    // Unpack with "*" as the primvar pattern, meaning unpack all primvars.
    return unpackGeometry(
            destgdp, prim ? (const GU_Detail *)&prim->getDetail() : nullptr,
            prim ? prim->getMapOffset() : GA_INVALID_OFFSET, "*", false,
            UT_StringHolder::theEmptyString, true, GA_Names::rest,
            prim ? &xform : nullptr);
}

bool
GusdGU_PackedUSD::unpackWithPrim(
    GU_Detail& destgdp,
    const UT_Matrix4D* transform,
    const GU_PrimPacked* prim) const
{
    return unpackGeometry(
        destgdp, prim ? (const GU_Detail *)&prim->getDetail() : nullptr,
        prim ? prim->getMapOffset() : GA_INVALID_OFFSET, "*", false,
        UT_StringHolder::theEmptyString, true, GA_Names::rest, transform);
}

bool
GusdGU_PackedUSD::getInstanceKey(UT_Options& key) const
{
    key.setOptionS("f", m_fileName);
    key.setOptionS("n", m_primPath.GetString());
    key.setOptionF("t", GusdUSD_Utils::GetNumericTime(m_frame));
    key.setOptionI("p", m_purposes );
    
    if( !m_prototypePathCacheValid ) {
        UsdPrim usdPrim = getUsdPrim();

        if( !usdPrim ) {
            return true;
        }

        // Disambiguate prototypes of instances by including the stage pointer.
        // Sometimes instances are opened on different stages, so their
        // path will both be "/__Prototype_1" even if they are different prims.
        // TODO: hash by the Usd instancing key if it becomes exposed.
        std::ostringstream ost;
        ost << (void const *)get_pointer(usdPrim.GetStage());
        std::string stagePtr = ost.str();
        if( usdPrim.IsValid() && usdPrim.IsInstance() ) {
            m_prototypePathCache = stagePtr +
                usdPrim.GetPrototype().GetPrimPath().GetString();
        } 
        else if( usdPrim.IsValid() && usdPrim.IsInstanceProxy() ) {
            m_prototypePathCache = stagePtr +
                usdPrim.GetPrimInPrototype().GetPrimPath().GetString();
        } 
        else{
            m_prototypePathCache = "";
        }
        m_prototypePathCacheValid = true;
    }

    if( !m_prototypePathCache.empty() ) {
        // If this prim is an instance, replace the prim path with the 
        // prototype's path so that instances can share GT prims.
        key.setOptionS("n", m_prototypePathCache );
    }

    return true;
}

int64 
GusdGU_PackedUSD::getMemoryUsage(bool inclusive) const
{
    int64 mem = inclusive ? sizeof(*this) : 0;

    // Don't count the (shared) GU_Detail, since that will greatly
    // over-estimate the overall memory usage.
    // mem += _detail.getMemoryUsage(false);

    return mem;
}

void 
GusdGU_PackedUSD::countMemory(UT_MemoryCounter &counter, bool inclusive) const
{
    // TODO
}

bool
GusdGU_PackedUSD::visibleGT() const
{
    return true;
}

UsdPrim 
GusdGU_PackedUSD::getUsdPrim(UT_ErrorSeverity sev) const
{
    if(m_usdPrim)
        return m_usdPrim;

    m_prototypePathCacheValid = false;

    SdfPath primPathWithoutVariants;
    GusdStageEditPtr edit;
    GusdStageEdit::GetPrimPathAndEditFromVariantsPath(
        m_primPath, primPathWithoutVariants, edit);

    GusdStageCacheReader cache;
    m_usdPrim = cache.GetPrim(m_fileName, primPathWithoutVariants, edit,
                              GusdStageOpts::LoadAll(), sev).first;

    // Register this packed USD prim now that we have set the m_usdPrim member.
    if (thePackedUSDTracker)
        thePackedUSDTracker(this, true);

    return m_usdPrim;
}


GT_PrimitiveHandle
GusdGU_PackedUSD::fullGT() const
{
    if( m_gtPrimCache )
        return m_gtPrimCache;

    if(UsdPrim usdPrim = getUsdPrim()) {
        m_gtPrimCache = GusdGT_PrimCache::GetInstance().GetPrim( 
                            m_usdPrim, 
                            m_frame,
                            m_purposes );
    }
    return m_gtPrimCache;
}

PXR_NAMESPACE_CLOSE_SCOPE
